#!/bin/bash

set -e
set -o xtrace

export DEBIAN_FRONTEND=noninteractive

# Ephemeral packages (installed for this script and removed again at the end)
STABLE_EPHEMERAL=" \
        libpciaccess-dev:$arch
        "

dpkg --add-architecture $arch
apt-get update

apt-get install -y --no-remove \
        $STABLE_EPHEMERAL \
        crossbuild-essential-$arch \
        libelf-dev:$arch \
        libexpat1-dev:$arch \
        libffi-dev:$arch \
        libstdc++6:$arch \
        libtinfo-dev:$arch \
        libvulkan-dev:$arch \
        libx11-dev:$arch \
        libx11-xcb-dev:$arch \
        libxcb-dri2-0-dev:$arch \
        libxcb-dri3-dev:$arch \
        libxcb-glx0-dev:$arch \
        libxcb-present-dev:$arch \
        libxcb-randr0-dev:$arch \
        libxcb-shm0-dev:$arch \
        libxcb-xfixes0-dev:$arch \
        libxdamage-dev:$arch \
        libxext-dev:$arch \
        libxrandr-dev:$arch \
        libxshmfence-dev:$arch \
        libxxf86vm-dev:$arch \
        wget

if [[ $arch == "armhf" ]]; then
        LLVM=llvm-7-dev
else
        LLVM=llvm-8-dev
fi

apt-get install -y --no-remove -t buster-backports \
        $LLVM:$arch

. .gitlab-ci/container/create-cross-file.sh $arch


. .gitlab-ci/container/container_pre_build.sh


# dependencies where we want a specific version
EXTRA_MESON_ARGS="--cross-file=/cross_file-${arch}.txt -D libdir=lib/$(dpkg-architecture -A $arch -qDEB_TARGET_MULTIARCH)"
. .gitlab-ci/container/build-libdrm.sh

apt-get purge -y \
        $STABLE_EPHEMERAL

. .gitlab-ci/container/container_post_build.sh
