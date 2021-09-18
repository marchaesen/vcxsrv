#!/bin/bash

set -e
set -o xtrace

export DEBIAN_FRONTEND=noninteractive

apt-get install -y ca-certificates

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
        flex \
        g++ \
        g++-mingw-w64-x86-64 \
        gcc \
        git \
        glslang-tools \
        kmod \
        libclang-11-dev \
        libclang-9-dev \
        libclc-dev \
        libelf-dev \
        libepoxy-dev \
        libexpat1-dev \
        libgtk-3-dev \
        libllvm11 \
        libllvm9 \
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
        libz-mingw-w64-dev \
        make \
        meson \
        pkg-config \
        python3-mako \
        python3-pil \
        python3-requests \
        qemu-user \
        valgrind \
        wayland-protocols \
        wget \
        wine64 \
        x11proto-dri2-dev \
        x11proto-gl-dev \
        x11proto-randr-dev \
        xz-utils \
        zlib1g-dev

# Needed for ci-fairy, this revision is able to upload files to MinIO
pip3 install git+http://gitlab.freedesktop.org/freedesktop/ci-templates@6f5af7e5574509726c79109e3c147cee95e81366

############### Uninstall ephemeral packages

apt-get purge -y $STABLE_EPHEMERAL

. .gitlab-ci/container/container_post_build.sh
