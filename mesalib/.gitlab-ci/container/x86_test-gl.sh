#!/bin/bash

set -e
set -o xtrace

export DEBIAN_FRONTEND=noninteractive

# Ephemeral packages (installed for this script and removed again at the end)
STABLE_EPHEMERAL=" \
      autoconf \
      automake \
      ccache \
      cmake \
      g++ \
      libgbm-dev \
      libgles2-mesa-dev \
      libpcre3-dev \
      libpng-dev \
      libvulkan-dev \
      libwaffle-dev \
      libxcb-keysyms1-dev \
      libxkbcommon-dev \
      libxrender-dev \
      make \
      meson \
      patch \
      pkg-config \
      python3-distutils \
      python3.7-dev \
      wget \
      xz-utils \
      "

apt-get install -y --no-remove \
      $STABLE_EPHEMERAL


. .gitlab-ci/container/container_pre_build.sh

############### Build virglrenderer

. .gitlab-ci/build-virglrenderer.sh

############### Build piglit

. .gitlab-ci/build-piglit.sh

############### Build dEQP runner

. .gitlab-ci/build-cts-runner.sh

############### Build dEQP GL

. .gitlab-ci/build-deqp-gl.sh

############### Build apitrace

. .gitlab-ci/build-apitrace.sh

############### Build renderdoc

. .gitlab-ci/build-renderdoc.sh

############### Uninstall the build software

ccache --show-stats

apt-get purge -y \
      $STABLE_EPHEMERAL

apt-get autoremove -y --purge
