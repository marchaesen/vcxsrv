#!/bin/bash

set -ex

VULKANTOOLS_VERSION=1862c6a47b64cd09156205d7f7e6b3bfcea76390

git clone https://github.com/LunarG/VulkanTools.git --single-branch --no-checkout /VulkanTools
pushd /VulkanTools
git checkout "$VULKANTOOLS_VERSION"
./update_external_sources.sh
mkdir _build
./scripts/update_deps.py --dir=_build --config=release --generator=Ninja
cmake -G Ninja -B_build -H. \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/VulkanTools/build \
      -DBUILD_TESTS=OFF \
      -DBUILD_VLF=OFF \
      -DBUILD_VKTRACE=OFF \
      -DBUILD_VIA=OFF \
      -DBUILD_VKTRACE_REPLAY=OFF \
      -C_build/helper.cmake
ninja -C _build VkLayer_screenshot VkLayer_screenshot-staging-json
mkdir -p build/etc/vulkan/explicit_layer.d
mkdir build/lib
install _build/layersvt/staging-json/VkLayer_screenshot.json build/etc/vulkan/explicit_layer.d
install _build/layersvt/libVkLayer_screenshot.so build/lib
strip build/lib/*
find . -not -path './build' -not -path './build/*' -delete
popd
