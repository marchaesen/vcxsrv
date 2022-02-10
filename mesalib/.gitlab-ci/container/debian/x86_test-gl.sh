#!/bin/bash

set -e
set -o xtrace

export DEBIAN_FRONTEND=noninteractive

# Ephemeral packages (installed for this script and removed again at the end)
STABLE_EPHEMERAL=" \
      autoconf \
      automake \
      bc \
      bison \
      bzip2 \
      ccache \
      clang-11 \
      cmake \
      flex \
      g++ \
      glslang-tools \
      libasound2-dev \
      libcap-dev \
      libclang-cpp11-dev \
      libelf-dev \
      libexpat1-dev \
      libfdt-dev \
      libgbm-dev \
      libgles2-mesa-dev \
      libllvmspirvlib-dev \
      libpciaccess-dev \
      libpng-dev \
      libudev-dev \
      libvulkan-dev \
      libwaffle-dev \
      libx11-xcb-dev \
      libxcb-dri2-0-dev \
      libxext-dev \
      libxkbcommon-dev \
      libxrender-dev \
      llvm-11-dev \
      llvm-spirv \
      make \
      meson \
      ocl-icd-opencl-dev \
      patch \
      pkg-config \
      python3-distutils \
      xz-utils \
      "

apt-get install -y --no-remove \
      $STABLE_EPHEMERAL \
      clinfo \
      iptables \
      libclang-common-11-dev \
      libclang-cpp11 \
      libcap2 \
      libegl1 \
      libepoxy-dev \
      libfdt1 \
      libllvmspirvlib11 \
      libxcb-shm0 \
      ocl-icd-libopencl1 \
      python3-lxml \
      python3-renderdoc \
      python3-simplejson \
      spirv-tools \
      sysvinit-core \
      wget


. .gitlab-ci/container/container_pre_build.sh

############### Build libdrm

. .gitlab-ci/container/build-libdrm.sh

############### Build Wayland

. .gitlab-ci/container/build-wayland.sh

############### Build Crosvm

. .gitlab-ci/container/build-rust.sh
. .gitlab-ci/container/build-crosvm.sh
rm -rf /root/.cargo
rm -rf /root/.rustup

############### Build kernel

export DEFCONFIG="arch/x86/configs/x86_64_defconfig"
export KERNEL_IMAGE_NAME=bzImage
export KERNEL_ARCH=x86_64
export DEBIAN_ARCH=amd64

mkdir -p /lava-files/
. .gitlab-ci/container/build-kernel.sh

############### Build libclc

. .gitlab-ci/container/build-libclc.sh

############### Build piglit

PIGLIT_OPTS="-DPIGLIT_BUILD_CL_TESTS=ON -DPIGLIT_BUILD_DMA_BUF_TESTS=ON" . .gitlab-ci/container/build-piglit.sh

############### Build dEQP GL

DEQP_TARGET=surfaceless . .gitlab-ci/container/build-deqp.sh

############### Build apitrace

. .gitlab-ci/container/build-apitrace.sh

############### Uninstall the build software

ccache --show-stats

apt-get purge -y \
      $STABLE_EPHEMERAL

apt-get autoremove -y --purge
