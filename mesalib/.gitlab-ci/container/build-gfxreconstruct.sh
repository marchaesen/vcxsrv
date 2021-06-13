#!/bin/bash

set -ex

GFXRECONSTRUCT_VERSION=3738decc2f4f9ff183818e5ab213a75a79fb7ab1

git clone https://github.com/LunarG/gfxreconstruct.git --single-branch -b master --no-checkout /gfxreconstruct
pushd /gfxreconstruct
git checkout "$GFXRECONSTRUCT_VERSION"
git submodule update --init
git submodule update
cmake -S . -B _build -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C _build gfxrecon-replay gfxrecon-info
mkdir -p build/bin
install _build/tools/replay/gfxrecon-replay build/bin
install _build/tools/info/gfxrecon-info build/bin
strip build/bin/*
find . -not -path './build' -not -path './build/*' -delete
popd
