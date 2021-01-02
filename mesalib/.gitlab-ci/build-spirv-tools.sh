#!/bin/bash

set -ex

git clone --depth 1 https://github.com/KhronosGroup/SPIRV-Tools SPIRV-Tools
pushd SPIRV-Tools
pushd external
git clone --depth 1 https://github.com/KhronosGroup/SPIRV-Headers
popd
cmake -S . -B _build -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C _build
ninja -C _build install
popd
rm -rf SPIRV-Tools
