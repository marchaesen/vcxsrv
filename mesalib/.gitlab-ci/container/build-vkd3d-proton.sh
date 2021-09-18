#!/bin/bash

set -ex

VKD3D_PROTON_VERSION="2.3.1"
VKD3D_PROTON_COMMIT="3ed3526332f53d7d35cf1b685fa8096b01f26ff0"

VKD3D_PROTON_DST_DIR="/vkd3d-proton-tests"
VKD3D_PROTON_SRC_DIR="/vkd3d-proton-src"
VKD3D_PROTON_BUILD_DIR="/vkd3d-proton-$VKD3D_PROTON_VERSION"

function build_arch {
  local arch="$1"
  shift

  meson "$@"                               \
        -Denable_tests=true                \
        --buildtype release                \
        --prefix "$VKD3D_PROTON_BUILD_DIR" \
        --strip                            \
        --bindir "x${arch}"                \
        --libdir "x${arch}"                \
        "$VKD3D_PROTON_BUILD_DIR/build.${arch}"

  ninja -C "$VKD3D_PROTON_BUILD_DIR/build.${arch}" install

  install -D -m755 -t "${VKD3D_PROTON_DST_DIR}/x${arch}/bin" "$VKD3D_PROTON_BUILD_DIR/build.${arch}/tests/"*.exe
}

git clone https://github.com/HansKristian-Work/vkd3d-proton.git --single-branch -b "v$VKD3D_PROTON_VERSION" --no-checkout "$VKD3D_PROTON_SRC_DIR"
pushd "$VKD3D_PROTON_SRC_DIR"
git checkout "$VKD3D_PROTON_COMMIT"
git submodule update --init --recursive
git submodule update --recursive
build_arch 64 --cross-file build-win64.txt
build_arch 86 --cross-file build-win32.txt
cp "setup_vkd3d_proton.sh" "$VKD3D_PROTON_BUILD_DIR/setup_vkd3d_proton.sh"
chmod +x "$VKD3D_PROTON_BUILD_DIR/setup_vkd3d_proton.sh"
popd

"$VKD3D_PROTON_BUILD_DIR"/setup_vkd3d_proton.sh install
rm -rf "$VKD3D_PROTON_BUILD_DIR"
rm -rf "$VKD3D_PROTON_SRC_DIR"
