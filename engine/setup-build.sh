#!/bin/bash
# Prepare the berry-v3 Chromium tree (sail-src:/src/cr/src) for our build host. Run inside sail-build with the
# sail-qnx800 volume at /root/qnx800 (the BB10 gcc 9.3 toolchain from gcc9/run.sh). Safe to run again.
set -e
. "$(dirname "$0")/qnx800-env.sh"
Q=/root/qnx800; SRC=/src/cr/src; T=arm-blackberry-qnx8eabi
[ -x $Q/bin/$T-g++ ] || { echo "toolchain missing: run gcc9/run.sh first"; exit 1; }

# 1. Paths the port hard-codes for an x86_64 host: point them at our aarch64 host build.
[ -e $Q/x86_64-linux ] || ln -s aarch64-linux $Q/x86_64-linux
[ -e $Q/aarch64-linux/$T/lib64 ] || ln -s . $Q/aarch64-linux/$T/lib64

# 2. The port links through ld.lld-17 behind GCC's ld (build/ld-wrapper.sh on the port's main branch),
#    with BB10's dynamic linker, libatomic and libgcc_eh from gcc 9.3. BB10's loader takes the first segment as
#    code and expects one data segment, so: no separate read-only segment and no RELRO split.
GCCLIB=$(dirname $($Q/bin/$T-gcc -print-libgcc-file-name))
ATOMIC=$(find $Q -name libatomic.a | head -1)
LD=$Q/$T/bin/ld
[ -e $LD.bfd ] || mv $LD $LD.bfd
cat > $LD <<EOF
#!/bin/bash
exec /usr/bin/ld.lld-17 --error-limit=0 --allow-shlib-undefined --strip-debug --no-warn-mismatch \\
  --no-rosegment -z norelro \\
  --dynamic-linker=/usr/lib/ldqnx.so.2 "\$@" --no-pie ${ATOMIC} $GCCLIB/libgcc_eh.a
EOF
chmod +x $LD
echo "ld -> lld (libatomic: ${ATOMIC:-none}, libgcc_eh: $GCCLIB)"

# 3. string.h: BB10's C++ const overloads become self-recursive under clang; the port guards them (see
#    build/config/qnx/BUILD.gn, -D_NO_CPP_INLINES).
python3 - $Q/include/string.h <<'PY'
import sys; p = sys.argv[1]; s = open(p).read()
old = '#ifdef __cplusplus\nextern "C++" {\n_C_STD_BEGIN\ninline void *memchr'
new = '#if defined(__cplusplus) && !defined(_NO_CPP_INLINES)\nextern "C++" {\n_C_STD_BEGIN\ninline void *memchr'
if new in s: print("string.h: already guarded")
else:
    assert old in s, "string.h layout changed"; open(p, "w").write(s.replace(old, new, 1)); print("string.h: guarded")
PY

# 4. The port's patch to Skia (a submodule, so not in the branch).
cd $SRC/third_party/skia
if git apply --check $SRC/deploy/patches/skia-qnx-bb10.patch 2>/dev/null; then git apply $SRC/deploy/patches/skia-qnx-bb10.patch; echo "skia: patched"
else echo "skia: already patched (or does not apply)"; fi

# 5. V8's snapshot tools are built for linux armhf (32-bit like the target); the build host can't run 32-bit code,
#    so V8's run.py starts ARM32 programs under qemu-arm.
cd $SRC
python3 - v8/tools/run.py <<'PY'
import sys; p = sys.argv[1]; s = open(p).read()
mark = "# sail: ARM32 tools under qemu"
if mark not in s:
    s = s.replace("result = subprocess.call(sys.argv[1:])", mark + """
import os
cmd = sys.argv[1:]
try:
  with open(cmd[0], 'rb') as f: h = f.read(20)
  if h[:4] == b'\\x7fELF' and h[4] == 1 and h[18] == 40:
    cmd = ['qemu-arm', '-L', '/usr/arm-linux-gnueabihf'] + cmd
except (OSError, IndexError):
  pass
result = subprocess.call(cmd)""", 1)
    open(p, "w").write(s); print("run.py: qemu wrapper added")
PY

# 6. Build settings: the port's (build/args.gn + windowed Ozone/QNX Screen), plus our host.
mkdir -p out/sail
cat > out/sail/args.gn <<'EOF'
target_os = "qnx"
target_cpu = "arm"
is_debug = false
dcheck_always_on = false
is_component_build = false
use_sysroot = false
treat_warnings_as_errors = false
clang_base_path = "/usr/lib/llvm-17"
clang_use_chrome_plugins = false
use_lld = false
enable_nacl = false
use_custom_libcxx = false
is_clang = true
use_glib = false
use_dbus = false
enable_rust = false
use_v8_context_snapshot = false
symbol_level = 0
use_ozone = true
ozone_platform = "qnx_screen"
ozone_platform_qnx_screen = true
ozone_platform_headless = true
ozone_auto_platforms = false
proprietary_codecs = true
ffmpeg_branding = "Chrome"
v8_snapshot_toolchain = "//build/toolchain/linux:clang_arm"
EOF
echo "args written: out/sail/args.gn"

# 7. The DEPS hooks we need (the sync used --nohooks: most hooks fetch x86-64 host tools we don't use).
cd /src/cr
[ -f src/build/util/LASTCHANGE ] || python3 src/build/util/lastchange.py -o src/build/util/LASTCHANGE
python3 src/build/util/lastchange.py -m GPU_LISTS_VERSION --revision-id-only --header src/gpu/config/gpu_lists_version.h
python3 src/build/util/lastchange.py -m SKIA_COMMIT_HASH -s src/third_party/skia --header src/skia/ext/skia_commit_hash.h
python3 src/build/util/lastchange.py -s src/third_party/dawn --revision src/gpu/webgpu/DAWN_VERSION
echo "hooks: lastchange files written"
# content_shell bundles Chromium's test fonts (hook "test_fonts"): fetched by hash from the public bucket.
cd /src/cr/src/third_party/test_fonts
if [ ! -f test_fonts/Ahem.ttf ]; then
  h=$(cat test_fonts.tar.gz.sha1)
  curl -sfL -o test_fonts.tar.gz https://storage.googleapis.com/chromium-fonts/$h
  [ "$(sha1sum test_fonts.tar.gz | cut -d' ' -f1)" = "$h" ] || { echo "test_fonts: hash mismatch"; exit 1; }
  tar xzf test_fonts.tar.gz && rm test_fonts.tar.gz
fi
echo "test_fonts: $(ls test_fonts | wc -l) files"

# 8. Sysroot header fixes (fix-qnx800.py) and small source fixes in third-party code.
python3 "$(dirname "$0")/fix-qnx800.py"
cd /src/cr/src
python3 - <<'PY'
def edit(p, old, new):
    s = open(p).read()
    if new in s: return print(p, "ok")
    assert old in s, p; open(p, "w").write(s.replace(old, new, 1)); print(p, "fixed")
# ced: BB10's <sys/platform.h> defines _INT32/_UINT32 as type macros, so ced skipped its own typedefs.
edit("third_party/ced/src/util/basictypes.h", "#ifndef _INT32\n#define _INT32\ntypedef int                 int32;",
     "#if !defined(_INT32) || defined(__QNX__)\n#ifndef _INT32\n#define _INT32\n#endif\ntypedef int                 int32;")
edit("third_party/ced/src/util/basictypes.h", "#ifndef _UINT32\n#define _UINT32\n",
     "#if !defined(_UINT32) || defined(__QNX__)\n#ifndef _UINT32\n#define _UINT32\n#endif\n")
# Perfetto (always linked: Chromium's tracing service interface needs it) knows no QNX: build it as on Linux.
edit("third_party/perfetto/include/perfetto/base/build_config.h", "#elif defined(__linux__)\n", "#elif defined(__linux__) || defined(__QNX__)\n")
# google_benchmark: its QNX cache probe uses QNX 7's syspage cacheattr, which BB10 lacks.
edit("third_party/google_benchmark/src/src/sysinfo.cc",
     "#elif BENCHMARK_OS_QNX\nstd::vector<CPUInfo::CacheInfo> GetCacheSizesQNX() {\n  std::vector<CPUInfo::CacheInfo> res;\n",
     "#elif BENCHMARK_OS_QNX\nstd::vector<CPUInfo::CacheInfo> GetCacheSizesQNX() { return {}; }  // sail: no syspage cacheattr on BB10\n"
     "#if 0\nstd::vector<CPUInfo::CacheInfo> GetCacheSizesQNX_unused() {\n  std::vector<CPUInfo::CacheInfo> res;\n")
edit("third_party/google_benchmark/src/src/sysinfo.cc", "    cache = SYSPAGE_ARRAY_ADJ_OFFSET(cacheattr, cache, elsize);\n  }\n  return res;\n}\n#endif\n",
     "    cache = SYSPAGE_ARRAY_ADJ_OFFSET(cacheattr, cache, elsize);\n  }\n  return res;\n}\n#endif  // sail\n#endif\n")
# BoringSSL: BB10 has no getentropy(); read /dev/urandom like other non-Linux UNIXes.
edit("third_party/boringssl/src/crypto/fipsmodule/rand/internal.h", "#elif defined(OPENSSL_LINUX)\n#define OPENSSL_RAND_URANDOM",
     "#elif defined(OPENSSL_LINUX) || defined(__QNX__)  // sail\n#define OPENSSL_RAND_URANDOM")
# material_color_utilities uses round() without <cmath> (glibc headers pull it in transitively).
edit("third_party/material_color_utilities/src/cpp/palettes/tones.cc", '#include "cpp/palettes/tones.h"\n',
     '#include "cpp/palettes/tones.h"\n\n#include <cmath>  // sail\n')
# Perfetto builds as Linux on QNX (above), minus Linux-kernel features BB10 lacks; its generic UNIX paths take over.
PF = "third_party/perfetto/"
LA = "PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX) || \\\n    PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID)"
def no_qnx(f, cond):
    p = PF + f; s = open(p).read()
    if "/* sail-noqnx */" in s: return print(p, "ok")
    n = s.count("#if " + cond) + s.count("#elif " + cond)
    assert n, p
    s = s.replace("#if " + cond, "#if (" + cond + ") && !defined(__QNX__) /* sail-noqnx */")
    s = s.replace("#elif " + cond, "#elif (" + cond + ") && !defined(__QNX__) /* sail-noqnx */")
    open(p, "w").write(s); print(p, "fixed", n)
for f in ["src/base/unix_socket.cc", "src/base/thread_task_runner.cc", "src/base/event_fd.cc"]: no_qnx(f, LA)
no_qnx("src/base/periodic_task.cc", "PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX) || \\\n    (PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID) && __ANDROID_API__ >= 19)")
edit(PF + "src/base/unix_socket.cc", "#include <poll.h>\n", "#include <poll.h>\n#include <sys/time.h>  // sail\n")
edit(PF + "src/base/string_utils.cc", "double StrToD(const char* nptr, char** endptr) {\n#if PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID) || \\\n",
     "double StrToD(const char* nptr, char** endptr) {\n#if !defined(__QNX__) && /* sail: no newlocale */ ( \\\n    PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID) || \\\n")
edit(PF + "src/base/string_utils.cc", "    PERFETTO_BUILDFLAG(PERFETTO_OS_APPLE)\n  static auto c_locale", "    PERFETTO_BUILDFLAG(PERFETTO_OS_APPLE))\n  static auto c_locale")
edit(PF + "include/perfetto/base/thread_utils.h", "#elif PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX)\nusing PlatformThreadId = pid_t;\ninline PlatformThreadId GetThreadId() {\n  return static_cast<pid_t>(syscall(__NR_gettid));",
     "#elif defined(__QNX__)  // sail: thread ids come from pthread_self()\nusing PlatformThreadId = pid_t;\ninline PlatformThreadId GetThreadId() {\n  return static_cast<pid_t>(pthread_self());\n}\n"
     "#elif PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX)\nusing PlatformThreadId = pid_t;\ninline PlatformThreadId GetThreadId() {\n  return static_cast<pid_t>(syscall(__NR_gettid));")
edit(PF + "src/base/file_utils.cc", "      if (dirent->d_type == DT_DIR) {\n        dir_queue.push_back(cur_dir + dirent->d_name + '/');\n      } else if (dirent->d_type == DT_REG) {",
     "#if defined(__QNX__)  // sail: no d_type on BB10\n      struct stat qst;\n      const bool qok = stat((cur_dir + dirent->d_name).c_str(), &qst) == 0;\n"
     "      const bool is_dir = qok && S_ISDIR(qst.st_mode), is_reg = qok && S_ISREG(qst.st_mode);\n#else\n"
     "      const bool is_dir = dirent->d_type == DT_DIR, is_reg = dirent->d_type == DT_REG;\n#endif\n"
     "      if (is_dir) {\n        dir_queue.push_back(cur_dir + dirent->d_name + '/');\n      } else if (is_reg) {")
edit(PF + "src/base/test/vm_test_utils.cc", "  int res = mincore(start, size, page_states.get());\n",
     "#if defined(__QNX__)  // sail: no mincore on BB10; report every page resident\n  memset(page_states.get(), 1, num_pages * sizeof(PageState));\n  int res = 0;\n#else\n"
     "  int res = mincore(start, size, page_states.get());\n#endif\n")
edit(PF + "include/perfetto/ext/base/event_fd.h", "#if !PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX) &&",
     "#if (defined(__QNX__) || !PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX)) && /* sail: pipe-based on QNX */")
edit(PF + "include/perfetto/base/thread_utils.h", "#elif PERFETTO_BUILDFLAG(PERFETTO_OS_FUCHSIA)\n#include <zircon/types.h>\n",
     "#elif PERFETTO_BUILDFLAG(PERFETTO_OS_FUCHSIA)\n#include <zircon/types.h>\n#elif defined(__QNX__)  // sail\n#include <pthread.h>\n#include <sys/types.h>\n")
# V8 uses V8_TARGET_OS_LINUX (an empty macro) as a value here; harmless upstream only because x64 builds see it defined as 1.
edit("v8/src/wasm/std-object-sizes.h", "defined(__clang__) && V8_TARGET_OS_LINUX &&", "defined(__clang__) && defined(V8_TARGET_OS_LINUX) &&")
# zxcvbn: the port added std::stoul/stoi for QNX because its libstdc++ lacked them; ours has them (C99 mode).
for f in ["matching.cpp", "scoring.cpp"]:
    edit("third_party/zxcvbn-cpp/native-src/zxcvbn/" + f, "#ifdef __QNX__\n", "#if defined(__QNX__) && !defined(_GLIBCXX11_USE_C99_STDLIB) /* sail */\n")
# Perfetto: no memfd on QNX; a const-ref deleted copy (clang + libstdc++ 9 reject the non-const one inside optional).
edit(PF + "src/tracing/ipc/memfd.cc", "#define PERFETTO_MEMFD_ENABLED()             \\\n  PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID) || \\\n      PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX)\n",
     "#define PERFETTO_MEMFD_ENABLED()             \\\n  (!defined(__QNX__) && (PERFETTO_BUILDFLAG(PERFETTO_OS_ANDROID) || \\\n      PERFETTO_BUILDFLAG(PERFETTO_OS_LINUX))) /* sail */\n")
edit(PF + "src/trace_processor/util/regex.h", "  Regex(Regex&) = delete;", "  Regex(const Regex&) = delete;  // sail")
# WebRTC: clang decides optional<Config>::emplace() isn't viable (nested struct with member initializers); assign instead.
edit("third_party/webrtc/modules/congestion_controller/goog_cc/loss_based_bwe_v2.cc", "  config.emplace();\n", "  config = Config();  // sail\n")
# WebRTC: BB10's CMSG_SPACE isn't a constant expression.
edit("third_party/webrtc/rtc_base/physical_socket_server.cc", "    char control[CMSG_SPACE(sizeof(struct timeval))] = {};\n",
     "#if defined(__QNX__)  // sail: CMSG_SPACE isn't constexpr on BB10\n    char control[64] = {};\n#else\n    char control[CMSG_SPACE(sizeof(struct timeval))] = {};\n#endif\n")
# FFmpeg has no QNX config; BB10 on ARM matches its Linux ARM one.
edit("third_party/ffmpeg/ffmpeg_options.gni", "} else if (is_chromeos || is_fuchsia) {\n  os_config = \"linux\"\n", "} else if (is_chromeos || is_fuchsia || is_qnx) {  # sail\n  os_config = \"linux\"\n")
PY
# 9. DevTools resources are built with Node; DEPS only fetches an x86-64 Node, so use the distro's arm64 one.
mkdir -p third_party/node/linux/node-linux-x64/bin
ln -sf /usr/bin/node third_party/node/linux/node-linux-x64/bin/node
# ...and the npm packages (hook "node_modules"), fetched by hash from the public bucket like the test fonts.
if [ ! -d third_party/node/node_modules ]; then
  h=$(cat third_party/node/node_modules.tar.gz.sha1)
  curl -sfL -o /tmp/nm.tgz https://storage.googleapis.com/chromium-nodejs/$h
  [ "$(sha1sum /tmp/nm.tgz | cut -d' ' -f1)" = "$h" ] || { echo "node_modules: hash mismatch"; exit 1; }
  tar xzf /tmp/nm.tgz -C third_party/node && rm /tmp/nm.tgz
fi
echo "node_modules: $(ls third_party/node/node_modules | wc -l) packages"
grep -q "angle_enable_gl" out/sail/args.gn || echo 'angle_enable_gl = false' >> out/sail/args.gn
# Vulkan headers (a nested DEPS entry of vulkan-deps the sync skipped): GPU service code includes them even with Vulkan off.
VH=third_party/vulkan-deps/vulkan-headers/src
if [ ! -f $VH/include/vulkan/vulkan_core.h ]; then
  rev=$(python3 -c "import re;print(re.search(r\"'vulkan_headers_revision': '([0-9a-f]+)'\", open('third_party/vulkan-deps/DEPS').read()).group(1))")
  rm -rf $VH; git init -q $VH
  git -C $VH fetch -q --depth 1 https://chromium.googlesource.com/external/github.com/KhronosGroup/Vulkan-Headers $rev
  git -C $VH checkout -q FETCH_HEAD
fi
echo "vulkan-headers: $(git -C $VH rev-parse --short HEAD)"
# catapult bundles six 1.15, whose six.moves importer doesn't work on Python 3.12: use the system's 1.16.
cp /usr/lib/python3/dist-packages/six.py third_party/catapult/third_party/six/six.py
# Not needed by Sail, and their headers aren't in the tree for QNX.
grep -q "use_kerberos" out/sail/args.gn || printf 'use_kerberos = false\n' >> out/sail/args.gn
# use_lld: host tools get lld-only flags; the port's compiler config skips -fuse-ld for QNX (THINLTO.md)
grep -q "^use_lld" out/sail/args.gn && sed -i 's/^use_lld = false/use_lld = true/' out/sail/args.gn
# Vulkan is off for QNX, but the GPU service's Ozone sources still include <vulkan/vulkan_core.h>: give them the headers.
python3 - <<'PY'
p = "gpu/ipc/service/BUILD.gn"; s = open(p).read()
old = '    deps += [ "//ui/ozone" ]\n'
new = old + '    configs += [ "//third_party/vulkan-deps/vulkan-headers/src:vulkan_headers_config" ]  # sail\n'
if new in s: print(p, "ok")
else:
    assert s.count(old) == 1, p; open(p, "w").write(s.replace(old, new, 1)); print(p, "fixed")
PY
# FFmpeg's generated source lists are keyed on use_linux_config; without QNX in it the library is empty.
python3 - <<'PY'
p = "third_party/ffmpeg/ffmpeg_generated.gni"; s = open(p).read()
old = "use_linux_config = is_linux || is_chromeos || is_fuchsia\n"
new = "use_linux_config = is_linux || is_chromeos || is_fuchsia || is_qnx  # sail\n"
if new in s: print(p, "ok")
else:
    assert old in s, p; open(p, "w").write(s.replace(old, new, 1)); print(p, "fixed")
PY
# WebRTC's ARM assembly only marks symbols as functions on Linux; lld then refuses Thumb calls into them.
python3 - <<'PY'
p = "third_party/webrtc/rtc_base/system/asm_defines.h"; s = open(p).read()
# (assembly files don't get the port's -D__QNX__, so key on ELF alone)
old = "#if defined(__linux__) && defined(__ELF__)\n.type \\name,%function\n"
new = "#if defined(__ELF__)  // sail\n.type \\name,%function\n"
s = s.replace("#if (defined(__linux__) || defined(__QNX__)) && defined(__ELF__)  // sail\n.type", "#if defined(__linux__) && defined(__ELF__)\n.type")
if new in s: print(p, "ok")
else:
    assert old in s, p; open(p, "w").write(s.replace(old, new, 1)); print(p, "fixed")
PY

# Releasing memory: on BB10, POSIX_MADV_DONTNEED (4) does not free pages as Linux's MADV_DONTNEED does. It pushes them
# out and keeps their contents (about 20 ms per MB, and 14 ms per MB to touch them again), which stalled every
# garbage collection and typed-array free for seconds. POSIX_MADV_DISCARD_NP (5) frees and zeroes (about 1 ms per MB).
# The port also defined MADV_FREE as 8, which BB10 rejects. Changing the defines rebuilds everything.
python3 - <<'PY'
for p, pairs in [
    ("build/config/qnx/BUILD.gn", [('"-DMADV_FREE=8",', '"-DMADV_FREE=5",  # sail: POSIX_MADV_DISCARD_NP'),
                                   ('"-DMADV_DONTNEED=4",', '"-DMADV_DONTNEED=5",  # sail: POSIX_MADV_DISCARD_NP')]),
    ("base/allocator/partition_allocator/src/partition_alloc/page_allocator_internals_posix.h",
     [("PA_PCHECK(0 == posix_madvise(ptr, length, POSIX_MADV_DONTNEED));", "PA_PCHECK(0 == posix_madvise(ptr, length, POSIX_MADV_DISCARD_NP));  // sail")]),
    ("base/qnx/memory_qnx.cc", [("return posix_madvise(addr, size, POSIX_MADV_DONTNEED) == 0;", "return posix_madvise(addr, size, POSIX_MADV_DISCARD_NP) == 0;  // sail")]),
]:
    s = open(p).read(); changed = False
    for old, new in pairs:
        if new in s: continue
        assert old in s, (p, old); s = s.replace(old, new, 1); changed = True
    if changed: open(p, "w").write(s)
    print(p, "fixed" if changed else "ok")
PY

# Sail's own engine changes: run as a child window of the Sail app, with a private control channel (engine/sail/).
python3 "$(dirname "$0")/sail-embed.py"
