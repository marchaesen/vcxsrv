#!/bin/bash

set -ex

export LIBDRM_VERSION=libdrm-2.4.102

wget https://dri.freedesktop.org/libdrm/$LIBDRM_VERSION.tar.xz
tar -xvf $LIBDRM_VERSION.tar.xz && rm $LIBDRM_VERSION.tar.xz
cd $LIBDRM_VERSION
meson build -D vc4=false -D freedreno=false -D etnaviv=false $EXTRA_MESON_ARGS
ninja -C build install
cd ..
rm -rf $LIBDRM_VERSION

