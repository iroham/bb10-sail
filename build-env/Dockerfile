# Engine build host: arm64 Ubuntu, clang/lld 17 (the port's compiler), depot_tools for gclient/gn.
# V8's snapshot tools must be 32-bit for an ARM32 target: built for linux armhf, run under qemu-arm.
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update -q && apt-get install -yq --no-install-recommends \
      clang-17 lld-17 llvm-17 libclang-rt-17-dev git curl ca-certificates python3 python3-venv \
      xz-utils bzip2 zstd file patchelf pkg-config make perl ninja-build \
      qemu-user gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf libc6-dev-armhf-cross \
      nodejs gperf python3-html5lib libnss3-dev libglib2.0-dev libdrm-dev libgbm-dev libxkbcommon-dev libexpat1-dev zlib1g-dev && rm -rf /var/lib/apt/lists/*
RUN git clone -q --depth 1 https://chromium.googlesource.com/chromium/tools/depot_tools.git /opt/depot_tools
ENV PATH=/opt/depot_tools:$PATH DEPOT_TOOLS_UPDATE=0 VPYTHON_BYPASS="manually managed python not supported by chrome operations"
WORKDIR /src/cr
