#!/bin/bash
# shellcheck disable=SC2086 # we want word splitting

set -ex

SCRIPT_DIR="$(pwd)"

git config --global user.email "mesa@example.com"
git config --global user.name "Mesa CI"

CROSVM_VERSION=c7cd0e0114c8363b884ba56d8e12adee718dcc93
git clone --single-branch -b main --no-checkout https://chromium.googlesource.com/chromiumos/platform/crosvm /platform/crosvm
pushd /platform/crosvm
git checkout "$CROSVM_VERSION"
git submodule update --init
# Apply all crosvm patches for Mesa CI
git am "$SCRIPT_DIR"/.gitlab-ci/container/build-crosvm_*.patch

VIRGLRENDERER_VERSION=3c5a9bbb7464e0e91e446991055300f4f989f6a9
rm -rf third_party/virglrenderer
git clone --single-branch -b master --no-checkout https://gitlab.freedesktop.org/virgl/virglrenderer.git third_party/virglrenderer
pushd third_party/virglrenderer
git checkout "$VIRGLRENDERER_VERSION"
meson build/ -Dvenus-experimental=true $EXTRA_MESON_ARGS
ninja -C build install
popd

RUSTFLAGS='-L native=/usr/local/lib' cargo install \
  bindgen \
  -j ${FDO_CI_CONCURRENT:-4} \
  --root /usr/local \
  $EXTRA_CARGO_ARGS

RUSTFLAGS='-L native=/usr/local/lib' cargo install \
  -j ${FDO_CI_CONCURRENT:-4} \
  --locked \
  --features 'default-no-sandbox gpu x virgl_renderer virgl_renderer_next' \
  --path . \
  --root /usr/local \
  $EXTRA_CARGO_ARGS

popd

rm -rf /platform/crosvm
