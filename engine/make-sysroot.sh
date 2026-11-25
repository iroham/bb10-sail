#!/bin/bash
# Build /src/sysroot (in the sail-src volume) from the BB10 10.3.1 SDK: C headers, target libraries, gcc 4.8.3's
# libgcc and crt files, and phase6's static libunwind. Run inside sail-build with phase6 mounted at /p6.
set -e
S=/src/sysroot; Q=/p6/qnxsdk/target_10_3_1_995/qnx6; G=/p6/qnxsdk/host_10_3_1_12/linux/x86/usr/lib/gcc/arm-unknown-nto-qnx8.0.0eabi/4.8.3
rm -rf $S; mkdir -p $S/lib $S/gcc
cp -a $Q/usr/include $S/include
for d in $Q/armle-v7/lib $Q/armle-v7/usr/lib; do (cd $d && tar cf - --exclude=./gcc .) | (cd $S/lib && tar xf -); done
cp $G/libgcc.a $G/crtbegin.o $G/crtend.o $S/gcc/
cp /p6/toolchain/out/lib/libunwind.a $S/gcc/
# string.h: the C++ const overloads of memchr/strchr/strpbrk/strrchr/strstr sit inside extern "C" and become
# self-recursive under clang (the port found this); skip them when _NO_CPP_INLINES is set, as the port does.
python3 - "$S/include/string.h" <<'PY'
import sys; p = sys.argv[1]; s = open(p).read()
a = '#ifdef __cplusplus\nextern "C++" {\n_C_STD_BEGIN\ninline void *memchr'
assert a in s, "string.h layout changed"
s = s.replace(a, '#if defined(__cplusplus) && !defined(_NO_CPP_INLINES)\nextern "C++" {\n_C_STD_BEGIN\ninline void *memchr', 1)
open(p, "w").write(s)
PY
du -sh $S; ls $S/gcc
