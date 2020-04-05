#!/bin/bash

set -ex

# https://github.com/LunarG/gfxreconstruct/issues/328
GFXRECONSTRUCT_VERSION=b66cd392a84b226cb60ad9d4130ddeb58a1559cb

git clone https://github.com/LunarG/gfxreconstruct.git --single-branch --no-checkout /gfxreconstruct
pushd /gfxreconstruct
git checkout "$GFXRECONSTRUCT_VERSION"
git submodule update --init
git submodule update
cmake -G Ninja -B_build -H. -DCMAKE_BUILD_TYPE=Release
ninja -C _build gfxrecon-replay
mkdir -p build/bin
install _build/tools/replay/gfxrecon-replay build/bin
strip build/bin/*
find . -not -path './build' -not -path './build/*' -delete
popd
