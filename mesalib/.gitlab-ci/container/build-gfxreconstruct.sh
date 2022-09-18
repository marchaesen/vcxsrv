#!/bin/bash

set -ex

GFXRECONSTRUCT_VERSION=5ed3caeecc46e976c4df31e263df8451ae176c26

git clone https://github.com/LunarG/gfxreconstruct.git \
    --single-branch \
    -b master \
    --no-checkout \
    /gfxreconstruct
pushd /gfxreconstruct
git checkout "$GFXRECONSTRUCT_VERSION"
git submodule update --init
git submodule update
cmake -S . -B _build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX:PATH=/gfxreconstruct/build -DBUILD_WERROR=OFF
cmake --build _build --parallel --target tools/{replay,info}/install/strip
find . -not -path './build' -not -path './build/*' -delete
popd
