#!/bin/bash

set -e
set -o xtrace

export DEBIAN_FRONTEND=noninteractive

apt-get install -y \
        ca-certificates \
        gnupg \
        unzip \
        wget

# Upstream LLVM package repository
apt-key add .gitlab-ci/container/llvm-snapshot.gpg.key
echo "deb https://apt.llvm.org/buster/ llvm-toolchain-buster-9 main" >/etc/apt/sources.list.d/llvm9.list

sed -i -e 's/http:\/\/deb/https:\/\/deb/g' /etc/apt/sources.list
echo 'deb https://deb.debian.org/debian buster-backports main' >/etc/apt/sources.list.d/backports.list

apt-get update

apt-get install -y --no-remove \
        $STABLE_EPHEMERAL \
        bison \
        ccache \
        clang-9 \
        dpkg-cross \
        flex \
        g++ \
        g++-mingw-w64-x86-64 \
        gcc \
        libclang-9-dev \
        libclc-dev \
        libelf-dev \
        libepoxy-dev \
        libexpat1-dev \
        libgtk-3-dev \
        libomxil-bellagio-dev \
        libpciaccess-dev \
        libunwind-dev \
        libva-dev \
        libvdpau-dev \
        libvulkan-dev \
        libx11-dev \
        libx11-xcb-dev \
        libxdamage-dev \
        libxext-dev \
        libxml2-utils \
        libxrandr-dev \
        libxrender-dev \
        libxshmfence-dev \
        libxvmc-dev \
        libxxf86vm-dev \
        libz-mingw-w64-dev \
        llvm-9-dev \
        pkg-config \
        python-mako \
        python3-mako \
        python3-pil \
        python3-requests \
        qemu-user \
        scons \
        wine64-development \
        x11proto-dri2-dev \
        x11proto-gl-dev \
        x11proto-randr-dev \
        xz-utils \
        zlib1g-dev

apt-get install -y --no-remove -t buster-backports \
        libclang-8-dev \
        libllvm8 \
        meson


# for the vulkan overlay layer
wget https://github.com/KhronosGroup/glslang/releases/download/master-tot/glslang-master-linux-Release.zip
unzip glslang-master-linux-Release.zip bin/glslangValidator
install -m755 bin/glslangValidator /usr/local/bin/
rm bin/glslangValidator glslang-master-linux-Release.zip


############### Uninstall ephemeral packages

apt-get purge -y \
        gnupg \
        unzip \
        wget

. .gitlab-ci/container/container_post_build.sh
