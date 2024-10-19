#!/usr/bin/env bash

# When changing this file, you need to bump the following
# .gitlab-ci/image-tags.yml tags:
# DEBIAN_TEST_GL_TAG
# KERNEL_ROOTFS_TAG

set -uex

VALIDATION_TAG="snapshot-2024wk39"

git clone -b "$VALIDATION_TAG" --single-branch --depth 1 https://github.com/KhronosGroup/Vulkan-ValidationLayers.git
pushd Vulkan-ValidationLayers
python3 scripts/update_deps.py --dir external --config release --generator Ninja
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DBUILD_TESTS=OFF -DBUILD_WERROR=OFF -C external/helper.cmake -S . -B build
ninja -C build
cmake --install build --strip
popd
rm -rf Vulkan-ValidationLayers
