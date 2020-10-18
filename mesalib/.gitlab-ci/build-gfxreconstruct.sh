#!/bin/bash

set -ex

GFXRECONSTRUCT_VERSION=57c588c04af631d1d6d381a48e2b9283f9d9d528

# Using the "dev" branch by now because it solves a crash and will allow us to
# use the gfxreconstruct-info tool
git clone https://github.com/LunarG/gfxreconstruct.git --single-branch -b dev --no-checkout /gfxreconstruct
pushd /gfxreconstruct
git checkout "$GFXRECONSTRUCT_VERSION"
git submodule update --init
git submodule update
cmake -G Ninja -B_build -H. -DCMAKE_BUILD_TYPE=Release
ninja -C _build gfxrecon-replay gfxrecon-info
mkdir -p build/bin
install _build/tools/replay/gfxrecon-replay build/bin
install _build/tools/info/gfxrecon-info build/bin
strip build/bin/*
find . -not -path './build' -not -path './build/*' -delete
popd
