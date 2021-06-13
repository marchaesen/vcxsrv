#!/bin/bash

set -e
set -o xtrace

export DEBIAN_FRONTEND=noninteractive

# Ephemeral packages (installed for this script and removed again at the end)
STABLE_EPHEMERAL=" \
      autoconf \
      automake \
      ccache \
      clang-11 \
      cmake \
      g++ \
      libclang-cpp11-dev \
      libgbm-dev \
      libgles2-mesa-dev \
      libllvmspirvlib-dev \
      libpciaccess-dev \
      libudev-dev \
      libvulkan-dev \
      libwaffle-dev \
      libwayland-dev \
      libx11-xcb-dev \
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
      wget \
      xz-utils \
      "

apt-get install -y --no-remove \
      $STABLE_EPHEMERAL \
      apitrace \
      clinfo \
      libclang-common-11-dev \
      libclang-cpp11 \
      libegl1 \
      libllvmspirvlib11 \
      libxcb-shm0 \
      ocl-icd-libopencl1 \
      python3-lxml \
      python3-renderdoc \
      python3-simplejson \
      spirv-tools


. .gitlab-ci/container/container_pre_build.sh


############### Build libdrm

. .gitlab-ci/container/build-libdrm.sh

############### Build libclc

. .gitlab-ci/container/build-libclc.sh

############### Build virglrenderer

. .gitlab-ci/container/build-virglrenderer.sh

############### Build piglit

PIGLIT_OPTS="-DPIGLIT_BUILD_CL_TESTS=ON -DPIGLIT_BUILD_VK_TESTS=OFF" . .gitlab-ci/container/build-piglit.sh

############### Build dEQP GL

DEQP_TARGET=surfaceless . .gitlab-ci/container/build-deqp.sh


############### Uninstall the build software

ccache --show-stats

apt-get purge -y \
      $STABLE_EPHEMERAL

apt-get autoremove -y --purge
