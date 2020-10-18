#!/bin/bash

set -ex

export LLVM_CONFIG="llvm-config-10"

$LLVM_CONFIG --version

git clone https://github.com/KhronosGroup/SPIRV-LLVM-Translator -b llvm_release_100 --depth 1 /SPIRV-LLVM-Translator
pushd /SPIRV-LLVM-Translator
cmake -G Ninja -DLLVM_BUILD_TOOLS=ON -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS=-fPIC -DCMAKE_CXX_FLAGS=-fPIC -DCMAKE_INSTALL_PREFIX=`$LLVM_CONFIG --prefix`
ninja
ninja install
popd


git config --global user.email "mesa@example.com"
git config --global user.name "Mesa CI"
git clone \
    https://github.com/llvm/llvm-project \
    --depth 1 \
    /llvm-project

mkdir /libclc
pushd /libclc
cmake -G Ninja -DLLVM_CONFIG=$LLVM_CONFIG -DLIBCLC_TARGETS_TO_BUILD="spirv-mesa3d-;spirv64-mesa3d-" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr /llvm-project/libclc
ninja
ninja install
popd

# workaroud cmake vs debian packaging.
mkdir -p /usr/lib/clc
ln -s /usr/share/clc/spirv64-mesa3d-.spv /usr/lib/clc/
ln -s /usr/share/clc/spirv-mesa3d-.spv /usr/lib/clc/

du -sh *
rm -rf /libclc /llvm-project /SPIRV-LLVM-Translator
