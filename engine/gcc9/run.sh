#!/bin/bash
# In the sail-gcc container: /g = this folder (read-only), /p6 = phase6 (BB10 SDK), /root/qnx800 and /work = volumes.
set -e
mkdir -p /work/tb && cp -r /g/. /work/tb/ && cd /work/tb
# our paths: SDK from phase6, install to /root/qnx800 (the port expects it there), 12 jobs, https downloads
sed -i 's|^APP_ROOT=.*|APP_ROOT="/root"|; s|^BBNDK_ROOT=.*|BBNDK_ROOT="/p6/qnxsdk"|; s|^export HOST_CPU_COUNT=.*|export HOST_CPU_COUNT="12"|; s|^BBNDK_HOST_OS=.*|BBNDK_HOST_OS="aarch64-linux"|' env.sh
sed -i 's|^DIST_DIR=.*|DIST_DIR="/work/dl"|' build.sh
sed -i 's|https://ftp.gnu.org/pub/gnu|https://mirrors.kernel.org/gnu|; s|ftp://gcc.gnu.org|https://gcc.gnu.org|; s|cd ${SRC_DIR}/${GCC_SRC} \&\& ./contrib/download_prerequisites|cd ${SRC_DIR}/${GCC_SRC} \&\& sed -i "s,ftp://,https://,g; s,http://gcc.gnu.org,https://gcc.gnu.org,g" contrib/download_prerequisites \&\& ./contrib/download_prerequisites|' download.sh
mkdir -p bin   # BlackBerry's packaging wrappers (unused here)
bash ./build.sh all build && bash ./build.sh all install
ls /root/qnx800/bin | head; /root/qnx800/bin/arm-blackberry-qnx8eabi-g++ --version | head -1
