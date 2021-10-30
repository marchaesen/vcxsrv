#!/bin/bash

set -ex

git config --global user.email "mesa@example.com"
git config --global user.name "Mesa CI"
git clone \
    https://github.com/intel/libva-utils.git \
    -b 2.13.0 \
    --depth 1 \
    /va-utils

pushd /va-utils
meson build -D tests=true  -Dprefix=/va $EXTRA_MESON_ARGS
ninja -C build install
popd
rm -rf /va-utils
