#!/usr/bin/env bash

set -ex

uncollapsed_section_start llvm-spirv "Building LLVM-SPIRV-Translator"

if [ "${LLVM_VERSION:?llvm version not set}" -ge 18 ]; then
  VER="${LLVM_VERSION}.1.0"
else
  VER="${LLVM_VERSION}.0.0"
fi

curl -L --retry 4 -f --retry-all-errors --retry-delay 60 \
    -O "https://github.com/KhronosGroup/SPIRV-LLVM-Translator/archive/refs/tags/v${VER}.tar.gz"
tar -xvf "v${VER}.tar.gz" && rm "v${VER}.tar.gz"

mkdir "SPIRV-LLVM-Translator-${VER}/build"
pushd "SPIRV-LLVM-Translator-${VER}/build"
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
ninja
ninja install
# For some reason llvm-spirv is not installed by default
ninja llvm-spirv
cp tools/llvm-spirv/llvm-spirv /usr/bin/
popd

du -sh "SPIRV-LLVM-Translator-${VER}"
rm -rf "SPIRV-LLVM-Translator-${VER}"

section_end llvm-spirv
