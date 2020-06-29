#!/bin/bash

set -e
set -o xtrace

export DEBIAN_FRONTEND=noninteractive

# Ephemeral packages (installed for this script and removed again at the end)
STABLE_EPHEMERAL=" \
        libpciaccess-dev:$arch \
        wget \
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
        libtinfo-dev:$arch

apt-get install -y --no-remove -t buster-backports \
        llvm-8-dev:$arch

. .gitlab-ci/create-cross-file.sh $arch


. .gitlab-ci/container/container_pre_build.sh


# dependencies where we want a specific version
export LIBDRM_VERSION=libdrm-2.4.100

wget https://dri.freedesktop.org/libdrm/$LIBDRM_VERSION.tar.bz2
tar -xvf $LIBDRM_VERSION.tar.bz2 && rm $LIBDRM_VERSION.tar.bz2
cd $LIBDRM_VERSION
meson --cross-file=/cross_file-${arch}.txt build -D libdir=lib/$(dpkg-architecture -A $arch -qDEB_TARGET_MULTIARCH)
ninja -C build install
cd ..
rm -rf $LIBDRM_VERSION


apt-get purge -y \
        $STABLE_EPHEMERAL

. .gitlab-ci/container/container_post_build.sh
