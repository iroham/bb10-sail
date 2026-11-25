#!/usr/bin/env python3
"""Header fixes for the BB10 gcc 9.3 sysroot (/root/qnx800/include) so Chromium compiles with clang 17 + libstdc++ 9.3.
The port did the same privately (it only documents string.h). Idempotent."""
import os, sys
I = "/root/qnx800/include"
def edit(rel, old, new):
    p = os.path.join(I, rel); s = open(p).read()
    if new in s: return print(f"{rel}: ok")
    assert old in s, f"{rel}: pattern not found"
    open(p, "w").write(s.replace(old, new, 1)); print(f"{rel}: fixed")
def append(rel, text):
    """Append at the very end of a header, outside its include guard; the text must guard itself."""
    p = os.path.join(I, rel); s = open(p).read()
    if text in s: return print(f"{rel}: ok")
    open(p, "w").write(s + "\n/* sail */\n" + text); print(f"{rel}: appended")
def write(rel, text):
    p = os.path.join(I, rel); os.makedirs(os.path.dirname(p), exist_ok=True)
    open(p, "w").write(text); print(f"{rel}: written")

# Dinkum's C++ overloads in BB10's C headers clash with libstdc++'s <cmath>/<clocale>; the port defines _NO_CPP_INLINES.
edit("locale.h", "#ifdef __cplusplus\n\t\t/* inlines, for C++ */\ninline struct lconv *localeconv(void)\n\t{\t/* return pointer to current locale */\n\treturn (&_Locale); }\n#else",
     "#ifdef __cplusplus\n# ifndef _NO_CPP_INLINES\n\t\t/* inlines, for C++ */\ninline struct lconv *localeconv(void)\n\t{\t/* return pointer to current locale */\n\treturn (&_Locale); }\n# endif\n#else")
edit("math.h", "\t\t/* GENERIC TEMPLATES */\n\n #if _HAS_GENERIC_TEMPLATES\n",
     "\t\t/* GENERIC TEMPLATES */\n\n #if _HAS_GENERIC_TEMPLATES && !defined(_NO_CPP_INLINES)\n")
# libstdc++ was configured as a cross build (--with-newlib) so it assumed no C99 library: std::stoi etc. are missing.
edit("libstdc++/9.3.0/arm-blackberry-qnx8eabi/bits/c++config.h", "/* #undef _GLIBCXX_USE_C99 */",
     "/* #undef _GLIBCXX_USE_C99 */\n/* sail: BB10's libc has the C99 functions */\n#define _GLIBCXX11_USE_C99_MATH 1\n#define _GLIBCXX11_USE_C99_STDIO 1\n"
     "#define _GLIBCXX11_USE_C99_STDLIB 1\n#define _GLIBCXX11_USE_C99_WCHAR 1\n#define _GLIBCXX98_USE_C99_MATH 1\n"
     "#define _GLIBCXX98_USE_C99_STDIO 1\n#define _GLIBCXX98_USE_C99_STDLIB 1\n#define _GLIBCXX98_USE_C99_WCHAR 1")
# std::isnan/isinf(double) from compiler built-ins, not BB10's ::isnan (it only exists via Dinkum's xtgmath.h).
edit("libstdc++/9.3.0/arm-blackberry-qnx8eabi/bits/c++config.h", "/* #undef _GLIBCXX_USE_C99 */",
     "/* #undef _GLIBCXX_USE_C99 */\n#define _GLIBCXX_NO_OBSOLETE_ISINF_ISNAN_DYNAMIC 1 /* sail */")
# C99 names libstdc++'s C99 mode expects globally. BB10 declares them only inside std (Dinkum), or only as C++ inlines
# (skipped with _NO_CPP_INLINES). All of them exist in libc/libm (checked with llvm-nm).
C99 = "/* sail: C99 names for C++ with _NO_CPP_INLINES */"
edit("stdlib.h", "#ifdef _STD_USING\n", "#ifdef _STD_USING\n" + C99 + "\nusing _CSTD lldiv_t; using _CSTD strtold;\n")
edit("wchar.h", "#endif /* __cplusplus */\n\n_C_STD_END",
     "#endif /* __cplusplus */\n\n#if defined(__cplusplus) && defined(_NO_CPP_INLINES)\n" + C99 + "\n_C_LIB_DECL\n"
     "float wcstof(const wchar_t *__nptr, wchar_t **__endptr);\nlong double wcstold(const wchar_t *__nptr, wchar_t **__endptr);\n"
     "_END_C_LIB_DECL\n#endif\n_C_STD_END")
edit("wchar.h", "#ifdef _STD_USING\n", "#ifdef _STD_USING\n#if defined(__cplusplus) && defined(_NO_CPP_INLINES)\nusing _CSTD wcstof; using _CSTD wcstold;\n#endif\n")
edit("math.h", "\t\t/* GENERIC TEMPLATES */\n",
     "#if defined(__cplusplus) && defined(_NO_CPP_INLINES)\n" + C99 + "\n_C_STD_BEGIN\nextern \"C\" double log2(double);\n_C_STD_END\n#endif\n\t\t/* GENERIC TEMPLATES */\n")

# __cxa_atexit: BB10 declares it without noexcept; libstdc++'s <cxxabi.h> declares it noexcept (clang rejects the mismatch).
edit("stdlib.h", "extern int __cxa_atexit(void (*__func)(void *), void *, void *);\n",
     "#if defined(__cplusplus) && __cplusplus >= 201103L\nextern int __cxa_atexit(void (*__func)(void *), void *, void *) noexcept; /* sail */\n#else\n"
     "extern int __cxa_atexit(void (*__func)(void *), void *, void *);\n#endif\n")
# madvise: BB10's libc has posix_madvise only.
edit("sys/mman.h", "extern int posix_madvise(void *__addr, _CSTD size_t __len, int __advice);\n",
     "extern int posix_madvise(void *__addr, _CSTD size_t __len, int __advice);\n"
     "/* sail: Chromium calls madvise(); BB10 only has posix_madvise */\n"
     "static __inline__ int madvise(void *__addr, _CSTD size_t __len, int __advice) { return posix_madvise(__addr, __len, __advice); }\n")
NZB = "#ifndef NZERO\n#define NZERO 20 /* sail: default nice value, as on Linux */\n#endif\n"
lp = os.path.join(I, "limits.h"); ls_ = open(lp).read()
if "#include <sys/platform.h>\n" + NZB in ls_: open(lp, "w").write(ls_.replace(NZB, "", 1)); print("limits.h: misplaced block removed")
# <sys/resource.h> #undefs NZERO (BB10 wants sysconf(_SC_NZERO)); define it again after that.
append("sys/resource.h", NZB)
append("limits.h", NZB + "#ifndef IOV_MAX\n#define IOV_MAX 1024 /* sail: as on Linux */\n#endif\n")
write("endian.h", """#pragma once
/* sail: glibc-style <endian.h> for BB10 (little-endian ARM) */
#include <stdint.h>
#ifndef __LITTLE_ENDIAN
#define __LITTLE_ENDIAN 1234
#define __BIG_ENDIAN 4321
#define __BYTE_ORDER __LITTLE_ENDIAN
#endif
#ifndef LITTLE_ENDIAN
#define LITTLE_ENDIAN __LITTLE_ENDIAN
#define BIG_ENDIAN __BIG_ENDIAN
#define BYTE_ORDER __BYTE_ORDER
#endif
#define htobe16(x) __builtin_bswap16(x)
#define htole16(x) ((uint16_t)(x))
#define be16toh(x) __builtin_bswap16(x)
#define le16toh(x) ((uint16_t)(x))
#define htobe32(x) __builtin_bswap32(x)
#define htole32(x) ((uint32_t)(x))
#define be32toh(x) __builtin_bswap32(x)
#define le32toh(x) ((uint32_t)(x))
#define htobe64(x) __builtin_bswap64(x)
#define htole64(x) ((uint64_t)(x))
#define be64toh(x) __builtin_bswap64(x)
#define le64toh(x) ((uint64_t)(x))
""")
write("elf.h", "#pragma once\n/* sail: BB10 keeps the ELF definitions in <sys/elf.h> */\n#include <sys/elf.h>\n")
write("syslog.h", """#pragma once
/* sail: BB10's libc has syslog()/openlog()/closelog() but ships no <syslog.h> */
#include <stdarg.h>
#define LOG_EMERG 0
#define LOG_ALERT 1
#define LOG_CRIT 2
#define LOG_ERR 3
#define LOG_WARNING 4
#define LOG_NOTICE 5
#define LOG_INFO 6
#define LOG_DEBUG 7
#define LOG_PID 0x01
#define LOG_CONS 0x02
#define LOG_NDELAY 0x08
#define LOG_USER (1<<3)
#define LOG_DAEMON (3<<3)
#ifdef __cplusplus
extern "C" {
#endif
void openlog(const char *, int, int);
void syslog(int, const char *, ...);
void vsyslog(int, const char *, va_list);
void closelog(void);
#ifdef __cplusplus
}
#endif
""")

# malloc_usable_size: BB10's libc calls it _msize (same meaning). (Undo the first, misplaced version of this fix.)
MBLOCK = "#ifndef __SAIL_MSIZE\n#define __SAIL_MSIZE\n#include <stddef.h>\n#ifdef __cplusplus\nextern \"C\" {\n#endif\nextern size_t _msize(void *);\n" \
         "static __inline__ size_t malloc_usable_size(void *__p) { return __p ? _msize(__p) : 0; } /* sail */\n#ifdef __cplusplus\n}\n#endif\n#endif\n"
mp = os.path.join(I, "malloc.h"); ms = open(mp).read()
if "#include <sys/platform.h>\n" + MBLOCK in ms: open(mp, "w").write(ms.replace(MBLOCK, "", 1)); print("malloc.h: misplaced block removed")
append("malloc.h", MBLOCK)
# The port links -lpthread; BB10 has pthreads in libc, so an empty archive satisfies the linker.
import subprocess
for stub in ["pthread", "rt"]:  # librt too: clock_* and shm_* are in libc
    lp = "/root/qnx800/arm-blackberry-qnx8eabi/lib/lib%s.a" % stub
    if not os.path.exists(lp):
        subprocess.check_call(["llvm-ar-17", "rcs", lp]); print("lib%s.a: empty stub written" % stub)

# major()/minor() as function-like macros in <sys/types.h> break C++ members with those names (protobuf);
# glibc only has them in <sys/sysmacros.h>, which BB10 also provides.
tp = os.path.join(I, "sys/types.h"); ts = open(tp).read()
ts = ts.replace("#if defined(__EXT_UNIX_MISC) && !defined(__cplusplus) /* sail */\n#define minor(device)", "#if defined(__EXT_UNIX_MISC)\n#define minor(device)"); open(tp, "w").write(ts)
MM = "#define minor(device)                   ((int)((device) & 0x3ff))\n#define major(device)                   ((int)(((device) >> 10) & 0x3f))\n#define makedev(node,major,minor)       ((dev_t)(((node) << 16) | ((major) << 10) | (minor)))\n"
edit("sys/types.h", "#if defined(__EXT_UNIX_MISC)\n" + MM, "#if defined(__EXT_UNIX_MISC)\n#ifndef __cplusplus /* sail: C++ code has members named major/minor */\n" + MM + "#endif\n")
# glibc's <stdio.h> also provides va_list; V8 relies on that.
append("stdio.h", "#include <stdarg.h>\n")
# pthread_getattr_np for the calling thread. QNX keeps each thread's TLS block at the top of its stack and
# records the stack's low end in it, so the stack is [__stackaddr, __tls()).
append("pthread.h", """#if !defined(__SAIL_GETATTR_NP)
#define __SAIL_GETATTR_NP
#include <errno.h>
#include <sys/storage.h>
static __inline__ int pthread_getattr_np(pthread_t __t, pthread_attr_t *__a) {
  struct _thread_local_storage *__l = __tls();
  char *__top = (char *)__l, *__low = (char *)__l->__stackaddr;
  if (__t != pthread_self()) return ESRCH;
  if (!__low || __low >= __top) __low = __top - 256 * 1024;  /* unknown base: assume 256 KB */
  pthread_attr_init(__a);
  return pthread_attr_setstack(__a, __low, (size_t)(__top - __low));
}
#endif
""")
# zlib's CPU probe (the port maps QNX to its Linux branch and skips getauxval) still includes <asm/hwcap.h>.
write("asm/hwcap.h", "#pragma once\n/* sail: no Linux HWCAP on BB10; zlib's QNX branch doesn't query it */\n#define HWCAP_NEON (1 << 12)\n#define HWCAP2_PMULL (1 << 1)\n#define HWCAP2_CRC32 (1 << 4)\n")

# Dinkum's C++ generic-math templates (xtgmath.h) break when <math.h> is included inside extern "C" (FFmpeg via WebRTC),
# and with _NO_CPP_INLINES nothing uses them.
edit("math.h", "#define _MATH\n #include <xtgmath.h>\n", "#define _MATH\n#if !(defined(__cplusplus) && defined(_NO_CPP_INLINES)) /* sail */\n #include <xtgmath.h>\n#endif\n")
append("string.h", "#if defined(__cplusplus) && defined(_STD_USING) && !defined(__SAIL_STRNLEN)\n#define __SAIL_STRNLEN\nusing _CSTD strnlen;\n#endif\n")
# glibc's <ifaddrs.h> brings in struct sockaddr.
edit("ifaddrs.h", "#ifndef __TYPES_H_INCLUDED\n#include <sys/types.h>\n#endif\n", "#ifndef __TYPES_H_INCLUDED\n#include <sys/types.h>\n#endif\n#include <sys/socket.h> /* sail */\n")

# Linux-style headers Chromium includes: BB10 has dl_iterate_phdr in <sys/link.h>; no syscall numbers.
# BB10 defines dl_phdr_info inside namespace std (Dinkum) but uses it from a global prototype: move it out.
write("link.h", "#pragma once\n#include <sys/link.h>\n#ifndef ElfW\n#define ElfW(type) Elf32_##type\n#endif\n"
)
edit("sys/link.h", "struct dl_phdr_info {\n#if __PTR_BITS__ == 32\n", "_C_STD_END /* sail: define it globally, where dl_iterate_phdr's prototype refers to it */\nstruct dl_phdr_info {\n#if __PTR_BITS__ == 32\n")
edit("sys/link.h", "#error __PTR_BITS__ not set properly\n#endif\n};\n", "#error __PTR_BITS__ not set properly\n#endif\n};\n_C_STD_BEGIN /* sail */\n")
# Chromium (C++17 here, as in the port) uses a few C++20 library names; libstdc++ 9 has the bit ones under internal names.
LS = "libstdc++/9.3.0/"
append(LS + "bit", """#if __cplusplus >= 201402L && !defined(__cpp_lib_bitops) && !defined(__SAIL_BIT)
#define __SAIL_BIT
namespace std {
template<typename _Tp> constexpr int popcount(_Tp __x) noexcept { return std::__popcount(__x); }
template<typename _Tp> constexpr int countl_zero(_Tp __x) noexcept { return std::__countl_zero(__x); }
template<typename _Tp> constexpr int countl_one(_Tp __x) noexcept { return std::__countl_one(__x); }
template<typename _Tp> constexpr int countr_zero(_Tp __x) noexcept { return std::__countr_zero(__x); }
template<typename _Tp> constexpr int countr_one(_Tp __x) noexcept { return std::__countr_one(__x); }
template<typename _Tp> constexpr _Tp rotl(_Tp __x, int __s) noexcept { return std::__rotl(__x, __s); }
template<typename _Tp> constexpr _Tp rotr(_Tp __x, int __s) noexcept { return std::__rotr(__x, __s); }
template<typename _Tp> constexpr bool has_single_bit(_Tp __x) noexcept { return std::__ispow2(__x); }
template<typename _Tp> constexpr _Tp bit_ceil(_Tp __x) noexcept { return std::__ceil2(__x); }
template<typename _Tp> constexpr _Tp bit_floor(_Tp __x) noexcept { return std::__floor2(__x); }
template<typename _Tp> constexpr _Tp bit_width(_Tp __x) noexcept { return std::__log2p1(__x); }
}
#endif
""")
append(LS + "type_traits", """#if __cplusplus >= 201103L && __cplusplus <= 201703L && !defined(__SAIL_CVREF)
#define __SAIL_CVREF
namespace std {
template<typename _Tp> struct remove_cvref { typedef typename remove_cv<typename remove_reference<_Tp>::type>::type type; };
template<typename _Tp> using remove_cvref_t = typename remove_cvref<_Tp>::type;
}
#endif
""")
write("sys/syscall.h", "#pragma once\n/* sail: BB10 has no Linux syscall numbers; code using them is Linux-only. */\n")

# string.h (gcc 9.3 toolchain's version): the C++ wrappers for memchr/strchr/strpbrk/strrchr/strstr call
# QNX::strchr etc., which are extern "C" and so the very same functions as the wrappers. GCC calls libc anyway;
# clang sees endless self-recursion and compiles every caller to nothing. With _NO_CPP_INLINES declare them plainly.
edit("string.h", "#if defined(__cplusplus)\nnamespace QNX {\nextern \"C\" {\n#endif\nextern _Const_return void *memchr",
     "#if defined(__cplusplus) && !defined(_NO_CPP_INLINES) /* sail */\nnamespace QNX {\nextern \"C\" {\n#endif\nextern _Const_return void *memchr")
edit("string.h", "#if defined (__cplusplus)\n}}\ninline void *memchr", "#if defined (__cplusplus) && !defined(_NO_CPP_INLINES) /* sail */\n}}\ninline void *memchr")

# Note: ninja doesn't track these sysroot headers. After changing one, delete the target objects
# (find out/sail/obj -name '*.o' -delete) and rebuild; host tools and generated sources can stay.
