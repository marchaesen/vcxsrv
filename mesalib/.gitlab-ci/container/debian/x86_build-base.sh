#!/bin/bash
# shellcheck disable=SC2086 # we want word splitting

set -e
set -o xtrace

export DEBIAN_FRONTEND=noninteractive

apt-get install -y ca-certificates gnupg2 software-properties-common

# Add llvm 13 to the build image
apt-key add .gitlab-ci/container/debian/llvm-snapshot.gpg.key
add-apt-repository "deb https://apt.llvm.org/bullseye/ llvm-toolchain-bullseye-13 main"

sed -i -e 's/http:\/\/deb/https:\/\/deb/g' /etc/apt/sources.list

# Ephemeral packages (installed for this script and removed again at
# the end)
STABLE_EPHEMERAL=" \
        python3-pip \
        python3-setuptools \
        "

apt-get update

apt-get install -y --no-remove \
        $STABLE_EPHEMERAL \
        bison \
        ccache \
        dpkg-cross \
        findutils \
        flex \
        g++ \
        cmake \
        gcc \
        git \
        glslang-tools \
        kmod \
        libclang-13-dev \
        libclang-11-dev \
        libclc-dev \
        libelf-dev \
        libepoxy-dev \
        libexpat1-dev \
        libgtk-3-dev \
        libllvm13 \
        libllvm11 \
        libomxil-bellagio-dev \
        libpciaccess-dev \
        libunwind-dev \
        libva-dev \
        libvdpau-dev \
        libvulkan-dev \
        libx11-dev \
        libx11-xcb-dev \
        libxext-dev \
        libxml2-utils \
        libxrandr-dev \
        libxrender-dev \
        libxshmfence-dev \
        libxvmc-dev \
        libxxf86vm-dev \
        make \
        meson \
        pkg-config \
        python3-mako \
        python3-pil \
        python3-requests \
        qemu-user \
        valgrind \
        wget \
        x11proto-dri2-dev \
        x11proto-gl-dev \
        x11proto-randr-dev \
        xz-utils \
        zlib1g-dev \
	zstd

# Needed for ci-fairy, this revision is able to upload files to MinIO
pip3 install git+http://gitlab.freedesktop.org/freedesktop/ci-templates@34f4ade99434043f88e164933f570301fd18b125

# We need at least 0.61.4 for proper Rust
pip3 install meson==0.61.5

. .gitlab-ci/container/build-rust.sh

. .gitlab-ci/container/debian/x86_build-base-wine.sh

############### Uninstall ephemeral packages

apt-get purge -y $STABLE_EPHEMERAL

. .gitlab-ci/container/container_post_build.sh
