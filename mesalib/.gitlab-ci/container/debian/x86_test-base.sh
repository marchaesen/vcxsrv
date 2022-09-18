#!/bin/bash
# shellcheck disable=SC2086 # we want word splitting

set -e
set -o xtrace

export DEBIAN_FRONTEND=noninteractive

apt-get install -y ca-certificates gnupg2 software-properties-common

sed -i -e 's/http:\/\/deb/https:\/\/deb/g' /etc/apt/sources.list

# Ephemeral packages (installed for this script and removed again at
# the end)
STABLE_EPHEMERAL=" \
      autoconf \
      automake \
      bc \
      bison \
      bzip2 \
      ccache \
      clang-11 \
      flex \
      glslang-tools \
      g++ \
      libasound2-dev \
      libcap-dev \
      libclang-cpp11-dev \
      libegl-dev \
      libelf-dev \
      libepoxy-dev \
      libgbm-dev \
      libpciaccess-dev \
      libvulkan-dev \
      libwayland-dev \
      libx11-xcb-dev \
      libxext-dev \
      make \
      meson \
      patch \
      pkg-config \
      python3-dev \
      python3-pip \
      python3-setuptools \
      python3-wheel \
      wayland-protocols \
      xz-utils \
      "

# Add llvm 13 to the build image
apt-key add .gitlab-ci/container/debian/llvm-snapshot.gpg.key
add-apt-repository "deb https://apt.llvm.org/bullseye/ llvm-toolchain-bullseye-13 main"

apt-get update
apt-get dist-upgrade -y

apt-get install -y \
      sysvinit-core

apt-get install -y --no-remove \
      git \
      git-lfs \
      inetutils-syslogd \
      iptables \
      jq \
      libasan6 \
      libexpat1 \
      libllvm13 \
      libllvm11 \
      liblz4-1 \
      libpng16-16 \
      libpython3.9 \
      libvulkan1 \
      libwayland-client0 \
      libwayland-server0 \
      libxcb-ewmh2 \
      libxcb-randr0 \
      libxcb-xfixes0 \
      libxkbcommon0 \
      libxrandr2 \
      libxrender1 \
      python3-mako \
      python3-numpy \
      python3-packaging \
      python3-pil \
      python3-requests \
      python3-six \
      python3-yaml \
      socat \
      vulkan-tools \
      waffle-utils \
      wget \
      xauth \
      xvfb \
      zlib1g \
      zstd

apt-get install -y --no-install-recommends \
      $STABLE_EPHEMERAL


. .gitlab-ci/container/container_pre_build.sh

############### Build kernel

export DEFCONFIG="arch/x86/configs/x86_64_defconfig"
export KERNEL_IMAGE_NAME=bzImage
export KERNEL_ARCH=x86_64
export DEBIAN_ARCH=amd64

mkdir -p /lava-files/
. .gitlab-ci/container/build-kernel.sh

# Needed for ci-fairy, this revision is able to upload files to MinIO
# and doesn't depend on git
pip3 install git+http://gitlab.freedesktop.org/freedesktop/ci-templates@34f4ade99434043f88e164933f570301fd18b125

# Needed for manipulation with traces yaml files.
pip3 install yq

############### Build libdrm

. .gitlab-ci/container/build-libdrm.sh

############### Build Wayland

. .gitlab-ci/container/build-wayland.sh

############### Build Crosvm

. .gitlab-ci/container/build-rust.sh
. .gitlab-ci/container/build-crosvm.sh

############### Build dEQP runner
. .gitlab-ci/container/build-deqp-runner.sh

rm -rf /root/.cargo
rm -rf /root/.rustup

ccache --show-stats

apt-get purge -y $STABLE_EPHEMERAL

apt-get autoremove -y --purge
