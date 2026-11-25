# Builds extrowerk/didacticode's BB10 gcc 9.3 toolchain (arm-blackberry-qnx8eabi) into /root/qnx800.
FROM ubuntu:20.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update -q && apt-get install -yq --no-install-recommends build-essential wget ca-certificates bison flex texinfo \
      autoconf automake libtool m4 patch file bzip2 xz-utils python3 && rm -rf /var/lib/apt/lists/*
