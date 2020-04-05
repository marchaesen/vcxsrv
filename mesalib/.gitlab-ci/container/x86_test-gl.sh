#!/bin/bash

set -e
set -o xtrace

export DEBIAN_FRONTEND=noninteractive

apt-get install -y \
      ca-certificates \
      gnupg

# Upstream LLVM package repository
apt-key add .gitlab-ci/container/llvm-snapshot.gpg.key
echo "deb https://apt.llvm.org/buster/ llvm-toolchain-buster-9 main" >/etc/apt/sources.list.d/llvm9.list

sed -i -e 's/http:\/\/deb/https:\/\/deb/g' /etc/apt/sources.list
echo 'deb https://deb.debian.org/debian buster-backports main' >/etc/apt/sources.list.d/backports.list

apt-get update

# Use newer packages from backports by default
cat >/etc/apt/preferences <<EOF
Package: *
Pin: release a=buster-backports
Pin-Priority: 500
EOF

apt-get dist-upgrade -y

apt-get install -y --no-remove \
      autoconf \
      automake \
      ccache \
      cmake \
      g++ \
      gcc \
      git \
      libexpat1 \
      libgbm-dev \
      libgles2-mesa-dev \
      libllvm9 \
      libpcre3-dev \
      libpcre32-3 \
      libpng-dev \
      libpng16-16 \
      libpython3.7 \
      libvulkan-dev \
      libvulkan1 \
      libwaffle-dev \
      libwayland-server0 \
      libxcb-keysyms1 \
      libxcb-keysyms1-dev \
      libxcb-xfixes0 \
      libxkbcommon-dev \
      libxkbcommon0 \
      libxrender-dev \
      libxrender1 \
      make \
      meson \
      patch \
      pkg-config \
      python \
      python3-distutils \
      python3-mako \
      python3-numpy \
      python3-pil \
      python3-requests \
      python3-six \
      python3-yaml \
      python3.7 \
      python3.7-dev \
      qt5-default \
      qt5-qmake \
      waffle-utils \
      xauth \
      xvfb \
      zlib1g

. .gitlab-ci/container/container_pre_build.sh

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
      autoconf \
      automake \
      ccache \
      cmake \
      g++ \
      gcc \
      gnupg \
      libc6-dev \
      libgbm-dev \
      libgles2-mesa-dev \
      libpcre3-dev \
      libpng-dev \
      libwaffle-dev \
      libxcb-keysyms1-dev \
      libxkbcommon-dev \
      libxrender-dev \
      make \
      meson \
      patch \
      pkg-config \
      python3-distutils \
      python3.7-dev

apt-get autoremove -y --purge
