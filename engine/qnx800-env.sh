# Environment the BB10 gcc 9.3 driver needs (from gcc9/env.sh with our paths). Source before building.
export QNX_ABI=arm-blackberry-qnx8eabi
export QNX_TARGET=/root/qnx800
export QNX_HOST=$QNX_TARGET/aarch64-linux
export QNX_INC=$QNX_TARGET/include
export QNX_BIN=$QNX_TARGET/bin
export QNX_PREBUILT=$QNX_TARGET/$QNX_ABI
export QNX_PREBUILT_BIN=$QNX_HOST/$QNX_ABI/bin
export QNX_PREBUILT_LIBEXEC=$QNX_HOST/$QNX_ABI
export QNX_PREBUILT_GCCLIB=$QNX_PREBUILT_LIBEXEC/gcc/$QNX_ABI/9.3.0
export PATH=$QNX_BIN:$QNX_PREBUILT_BIN:$PATH
