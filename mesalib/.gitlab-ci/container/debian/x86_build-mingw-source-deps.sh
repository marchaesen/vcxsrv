#!/bin/bash
# shellcheck disable=SC2086 # we want word splitting

set -e

wd=$PWD
CMAKE_TOOLCHAIN_MINGW_PATH=$wd/.gitlab-ci/container/debian/x86_mingw-toolchain.cmake
mkdir -p ~/tmp
pushd ~/tmp

# Building DirectX-Headers
git clone https://github.com/microsoft/DirectX-Headers -b v1.606.3 --depth 1
mkdir -p DirectX-Headers/build
pushd DirectX-Headers/build
meson .. \
--backend=ninja \
--buildtype=release -Dbuild-test=false \
-Dprefix=/usr/x86_64-w64-mingw32/ \
--cross-file=$wd/.gitlab-ci/x86_64-w64-mingw32

ninja install
popd

export VULKAN_SDK_VERSION=1.3.211.0

# Building SPIRV Tools
git clone -b sdk-$VULKAN_SDK_VERSION --depth=1 \
https://github.com/KhronosGroup/SPIRV-Tools SPIRV-Tools

git clone -b sdk-$VULKAN_SDK_VERSION --depth=1 \
https://github.com/KhronosGroup/SPIRV-Headers SPIRV-Tools/external/SPIRV-Headers

mkdir -p SPIRV-Tools/build
pushd SPIRV-Tools/build
cmake .. \
-DCMAKE_TOOLCHAIN_FILE=$CMAKE_TOOLCHAIN_MINGW_PATH \
-DCMAKE_INSTALL_PREFIX=/usr/x86_64-w64-mingw32/ \
-GNinja -DCMAKE_BUILD_TYPE=Release \
-DCMAKE_CROSSCOMPILING=1 \
-DCMAKE_POLICY_DEFAULT_CMP0091=NEW

ninja install
popd

# Building LLVM
git clone -b release/14.x --depth=1 \
https://github.com/llvm/llvm-project llvm-project

git clone -b v14.0.0 --depth=1 \
https://github.com/KhronosGroup/SPIRV-LLVM-Translator llvm-project/llvm/projects/SPIRV-LLVM-Translator

mkdir llvm-project/build
pushd llvm-project/build
cmake ../llvm \
-DCMAKE_TOOLCHAIN_FILE=$CMAKE_TOOLCHAIN_MINGW_PATH \
-DCMAKE_INSTALL_PREFIX=/usr/x86_64-w64-mingw32/ \
-GNinja -DCMAKE_BUILD_TYPE=Release \
-DCMAKE_CROSSCOMPILING=1 \
-DLLVM_ENABLE_RTTI=ON \
-DCROSS_TOOLCHAIN_FLAGS_NATIVE=-DLLVM_EXTERNAL_SPIRV_HEADERS_SOURCE_DIR=$PWD/../../SPIRV-Tools/external/SPIRV-Headers \
-DLLVM_EXTERNAL_SPIRV_HEADERS_SOURCE_DIR=$PWD/../../SPIRV-Tools/external/SPIRV-Headers \
-DLLVM_ENABLE_PROJECTS="clang" \
-DLLVM_TARGETS_TO_BUILD="AMDGPU;X86" \
-DLLVM_OPTIMIZED_TABLEGEN=TRUE \
-DLLVM_ENABLE_ASSERTIONS=TRUE \
-DLLVM_INCLUDE_UTILS=OFF \
-DLLVM_INCLUDE_RUNTIMES=OFF \
-DLLVM_INCLUDE_TESTS=OFF \
-DLLVM_INCLUDE_EXAMPLES=OFF \
-DLLVM_INCLUDE_GO_TESTS=OFF \
-DLLVM_INCLUDE_BENCHMARKS=OFF \
-DLLVM_BUILD_LLVM_C_DYLIB=OFF \
-DLLVM_ENABLE_DIA_SDK=OFF \
-DCLANG_BUILD_TOOLS=ON \
-DLLVM_SPIRV_INCLUDE_TESTS=OFF

ninja install
popd

# Building libclc
mkdir llvm-project/build-libclc
pushd llvm-project/build-libclc
cmake ../libclc \
-DCMAKE_TOOLCHAIN_FILE=$CMAKE_TOOLCHAIN_MINGW_PATH \
-DCMAKE_INSTALL_PREFIX=/usr/x86_64-w64-mingw32/ \
-GNinja -DCMAKE_BUILD_TYPE=Release \
-DCMAKE_CROSSCOMPILING=1 \
-DCMAKE_POLICY_DEFAULT_CMP0091=NEW \
-DCMAKE_CXX_FLAGS="-m64" \
-DLLVM_CONFIG="/usr/x86_64-w64-mingw32/bin/llvm-config" \
-DLLVM_CLANG="/usr/x86_64-w64-mingw32/bin/clang" \
-DLLVM_AS="/usr/x86_64-w64-mingw32/bin/llvm-as" \
-DLLVM_LINK="/usr/x86_64-w64-mingw32/bin/llvm-link" \
-DLLVM_OPT="/usr/x86_64-w64-mingw32/bin/opt" \
-DLLVM_SPIRV="/usr/x86_64-w64-mingw32/bin/llvm-spirv" \
-DLIBCLC_TARGETS_TO_BUILD="spirv-mesa3d-;spirv64-mesa3d-"

ninja install
popd

popd # ~/tmp

# Cleanup ~/tmp
rm -rf ~/tmp
