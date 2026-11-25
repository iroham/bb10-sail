#!/bin/bash
# Build the Sail app (Cascades window + engine launcher) with the project's clang and the BB10 SDK
# (gcc 4.8.3 libstdc++, Qt 4.8, Cascades). From anywhere: native-browser/app/build.sh
#   -> app/build/sail         the app: every bit of interface, starts the engine
#   -> app/build/sail-launch  sets up the engine's environment and execs it
# The engine itself (app/build/payload/sail-engine) comes from the Chromium build (engine/).
set -o pipefail
cd "$(dirname "$0")/../.."
DOCKER_CONTEXT=colima-aosp docker run --rm -v a11-src:/src -v $PWD/phase6/qnxsdk:/qnxsdk:ro -v $PWD/phase6:/w -v $PWD/native-browser/app:/t -v tg-build:/b:ro -w /t a11-build bash -c '
. /w/toolchain/qnx-env.sh
set -e
O=build/obj; mkdir -p $O
GXX=$QT/usr/include/c++/4.8.3
INC="-nostdinc++ -isystem $GXX -isystem $GXX/arm-unknown-nto-qnx8.0.0eabi -isystem $GXX/backward $QNX_SYSINC -isystem $QT/usr/include/qt4 -isystem $QT/usr/include/qt4/QtCore -isystem $QT/usr/include/qt4/QtGui -isystem $QT/usr/include/qt4/QtDeclarative -Isrc -I$O"
FL="-std=gnu++98 -O2 -Wall -Wno-pragma-pack -fvisibility=hidden -DQT_NO_DEBUG -DQT_DECLARATIVE_LIB -DQT_CORE_LIB"
/b/host/moc -DQ_OS_BLACKBERRY src/engine.h -o $O/moc_engine.cpp
/b/host/moc -DQ_OS_BLACKBERRY src/store.h -o $O/moc_store.cpp
$QNX_CXX $FL $INC -c $O/moc_store.cpp -o $O/moc_store.o &
$QNX_CXX $FL $INC -c src/store.cpp -o $O/store.o &
$QNX_CXX $FL $INC -c $O/moc_engine.cpp -o $O/moc_engine.o &
$QNX_CXX $FL $INC -c src/engine.cpp -o $O/engine.o &
$QNX_CXX $FL $INC -c src/main.cpp -o $O/main.o &
$QNX_CC -std=gnu11 -O2 -Wall -c launch.c -o $O/launch.o &
wait
for o in moc_engine engine moc_store store main launch; do [ -f $O/$o.o ] || { echo "compile failed: $o"; exit 1; }; done
$QNX_LD $QNX_LDFLAGS_EXE -L$QT/armle-v7/usr/lib/qt4/lib -L$QT/armle-v7/lib/gcc/4.8.3 -rpath-link=$QT/armle-v7/usr/lib/qt4/lib \
  $O/main.o $O/engine.o $O/moc_engine.o $O/store.o $O/moc_store.o -lbb -lbbcascades -lbbsystem -lbps -lQtDeclarative -lQtGui -lQtCore -lscreen -lsocket -lstdc++ -lm $QNX_LIBS_EXE -o build/sail
$QNX_LD $QNX_LDFLAGS_EXE $O/launch.o $QNX_LIBS_EXE -o build/sail-launch
$CLANG/llvm-strip --strip-unneeded build/sail build/sail-launch
ls -la build/sail build/sail-launch' 2>&1 | { grep -v "requested image" || true; }
exit ${PIPESTATUS[0]}
