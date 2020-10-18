#!/bin/bash

set -e
set -o xtrace

ROOTFS=/lava-files/rootfs-${arch}

dpkg --add-architecture $arch
apt-get update

# Cross-build test deps
BAREMETAL_EPHEMERAL=" \
        autoconf \
        automake \
        crossbuild-essential-$arch \
        git-lfs \
        libdrm-dev:$arch \
        libboost-dev:$arch \
        libegl1-mesa-dev:$arch \
        libelf-dev:$arch \
        libexpat1-dev:$arch \
        libffi-dev:$arch \
        libgbm-dev:$arch \
        libgles2-mesa-dev:$arch \
        libpciaccess-dev:$arch \
        libpcre3-dev:$arch \
        libpng-dev:$arch \
        libpython3-dev:$arch \
        libstdc++6:$arch \
        libtinfo-dev:$arch \
        libegl1-mesa-dev:$arch \
        libvulkan-dev:$arch \
        libxcb-keysyms1-dev:$arch \
        libpython3-dev:$arch \
        python3-dev \
        qt5-default \
        qt5-qmake \
        qtbase5-dev:$arch \
        "

apt-get install -y --no-remove $BAREMETAL_EPHEMERAL

mkdir /var/cache/apt/archives/$arch

############### Create cross-files

. .gitlab-ci/create-cross-file.sh $arch

. .gitlab-ci/container/container_pre_build.sh

############### Create rootfs
KERNEL_URL=https://github.com/anholt/linux/archive/cheza-pagetables-2020-09-04.tar.gz

DEBIAN_ARCH=$arch INCLUDE_VK_CTS=1 . .gitlab-ci/container/lava_build.sh

############### Uninstall the build software

apt-get purge -y $BAREMETAL_EPHEMERAL

. .gitlab-ci/container/container_post_build.sh
