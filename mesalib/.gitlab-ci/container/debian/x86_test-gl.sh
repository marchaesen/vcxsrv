#!/bin/bash
# shellcheck disable=SC2086 # we want word splitting

set -e
set -o xtrace

export DEBIAN_FRONTEND=noninteractive

# Ephemeral packages (installed for this script and removed again at the end)
STABLE_EPHEMERAL=" \
      bzip2 \
      ccache \
      clang-13 \
      clang-11 \
      cmake \
      g++ \
      glslang-tools \
      libasound2-dev \
      libcap-dev \
      libclang-cpp13-dev \
      libclang-cpp11-dev \
      libgles2-mesa-dev \
      libllvmspirvlib-dev \
      libpciaccess-dev \
      libpng-dev \
      libudev-dev \
      libvulkan-dev \
      libwaffle-dev \
      libx11-xcb-dev \
      libxcb-dri2-0-dev \
      libxkbcommon-dev \
      libxrender-dev \
      llvm-13-dev \
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

apt-get update

apt-get install -y --no-remove \
      $STABLE_EPHEMERAL \
      clinfo \
      iptables \
      libclang-common-13-dev \
      libclang-common-11-dev \
      libclang-cpp13 \
      libclang-cpp11 \
      libcap2 \
      libegl1 \
      libepoxy0 \
      libfdt1 \
      libllvmspirvlib11 \
      libxcb-shm0 \
      ocl-icd-libopencl1 \
      python3-lxml \
      python3-renderdoc \
      python3-simplejson \
      spirv-tools


. .gitlab-ci/container/container_pre_build.sh

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
