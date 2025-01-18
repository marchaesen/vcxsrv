#!/bin/bash

# When changing this file, you need to bump the following
# .gitlab-ci/image-tags.yml tags:
# DEBIAN_TEST_VK_TAG
# KERNEL_ROOTFS_TAG
set -ex

uncollapsed_section_start vkd3d-proton "Building vkd3d-proton"

VKD3D_PROTON_COMMIT="b121e6d746341e0aaba7663e3d85f3194e8e20e1"

VKD3D_PROTON_DST_DIR="/vkd3d-proton-tests"
VKD3D_PROTON_SRC_DIR="/vkd3d-proton-src"
VKD3D_PROTON_BUILD_DIR="/vkd3d-proton-build"

function build_arch {
  local arch="$1"

  meson setup                              \
        -Denable_tests=true                \
        --buildtype release                \
        --prefix "$VKD3D_PROTON_DST_DIR"   \
        --strip                            \
        --bindir "x${arch}"                \
        --libdir "x${arch}"                \
        "$VKD3D_PROTON_BUILD_DIR/build.${arch}"

  ninja -C "$VKD3D_PROTON_BUILD_DIR/build.${arch}" install

  install -D -m755 -t "${VKD3D_PROTON_DST_DIR}/x${arch}/bin" "$VKD3D_PROTON_BUILD_DIR/build.${arch}/tests/d3d12"
}

git clone https://github.com/HansKristian-Work/vkd3d-proton.git --single-branch -b master --no-checkout "$VKD3D_PROTON_SRC_DIR"
pushd "$VKD3D_PROTON_SRC_DIR"
git checkout "$VKD3D_PROTON_COMMIT"
git submodule update --init --recursive
git submodule update --recursive
build_arch 64
build_arch 86
mkdir "$VKD3D_PROTON_DST_DIR/tests"
cp \
  "tests/test-runner.sh" \
  "tests/d3d12_tests.h" \
  "$VKD3D_PROTON_DST_DIR/tests/"
popd

rm -rf "$VKD3D_PROTON_BUILD_DIR"
rm -rf "$VKD3D_PROTON_SRC_DIR"

section_end vkd3d-proton
