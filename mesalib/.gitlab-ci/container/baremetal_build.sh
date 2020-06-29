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
KERNEL_URL=https://gitlab.freedesktop.org/drm/msm/-/archive/drm-msm-fixes-2020-06-25/msm-drm-msm-fixes-2020-06-25.tar.gz

DEBIAN_ARCH=$arch INCLUDE_VK_CTS=1 . .gitlab-ci/container/lava_arm.sh

############### Store traces
# Clone the traces-db at container build time so we don't have to pull traces
# per run (too much egress cost for fd.o).
git clone \
    --depth 1 \
    -b mesa-ci-2020-06-08 \
    https://gitlab.freedesktop.org/gfx-ci/tracie/traces-db.git \
    $ROOTFS/traces-db
rm -rf $ROOTFS/traces-db/.git
find $ROOTFS/traces-db -type f \
     -a -not -name '*.trace' \
     -a -not -name '*.rdc' \
     -delete

ccache --show-stats

. .gitlab-ci/container/container_post_build.sh

apt-get purge -y $BAREMETAL_EPHEMERAL
