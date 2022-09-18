#!/bin/bash
# shellcheck disable=SC2086 # we want word splitting

set -e
set -o xtrace

export DEBIAN_FRONTEND=noninteractive

# Ephemeral packages (installed for this script and removed again at the end)
STABLE_EPHEMERAL=" \
      autoconf \
      automake \
      autotools-dev \
      bzip2 \
      libtool \
      libssl-dev \
      python3-pip \
      "

apt-get update

apt-get install -y --no-remove \
      $STABLE_EPHEMERAL \
      check \
      clang \
      libasan6 \
      libarchive-dev \
      libclang-cpp13-dev \
      libclang-cpp11-dev \
      libgbm-dev \
      libglvnd-dev \
      libllvmspirvlib-dev \
      liblua5.3-dev \
      libxcb-dri2-0-dev \
      libxcb-dri3-dev \
      libxcb-glx0-dev \
      libxcb-present-dev \
      libxcb-randr0-dev \
      libxcb-shm0-dev \
      libxcb-sync-dev \
      libxcb-xfixes0-dev \
      libxcb1-dev \
      libxml2-dev \
      llvm-13-dev \
      llvm-11-dev \
      ocl-icd-opencl-dev \
      python3-freezegun \
      python3-pytest \
      procps \
      spirv-tools \
      shellcheck \
      strace \
      time \
      yamllint \
      zstd


. .gitlab-ci/container/container_pre_build.sh

# dependencies where we want a specific version
export              XORG_RELEASES=https://xorg.freedesktop.org/releases/individual

export         XORGMACROS_VERSION=util-macros-1.19.0

. .gitlab-ci/container/build-mold.sh

wget $XORG_RELEASES/util/$XORGMACROS_VERSION.tar.bz2
tar -xvf $XORGMACROS_VERSION.tar.bz2 && rm $XORGMACROS_VERSION.tar.bz2
cd $XORGMACROS_VERSION; ./configure; make install; cd ..
rm -rf $XORGMACROS_VERSION

. .gitlab-ci/container/build-libdrm.sh

. .gitlab-ci/container/build-wayland.sh

pushd /usr/local
git clone https://gitlab.freedesktop.org/mesa/shader-db.git --depth 1
rm -rf shader-db/.git
cd shader-db
make
popd

git clone https://github.com/microsoft/DirectX-Headers -b v1.606.3 --depth 1
mkdir -p DirectX-Headers/build
pushd DirectX-Headers/build
meson .. --backend=ninja --buildtype=release -Dbuild-test=false
ninja
ninja install
popd
rm -rf DirectX-Headers

pip3 install git+https://git.lavasoftware.org/lava/lavacli@3db3ddc45e5358908bc6a17448059ea2340492b7

# install bindgen
RUSTFLAGS='-L native=/usr/local/lib' cargo install \
  bindgen --version 0.59.2 \
  -j ${FDO_CI_CONCURRENT:-4} \
  --root /usr/local

############### Uninstall the build software

apt-get purge -y \
      $STABLE_EPHEMERAL

. .gitlab-ci/container/container_post_build.sh
