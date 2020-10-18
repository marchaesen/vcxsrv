#!/bin/bash

set -e
set -o xtrace

export DEBIAN_FRONTEND=noninteractive

apt-get install -y \
      apt-transport-https \
      ca-certificates

sed -i -e 's/http:\/\/deb/https:\/\/deb/g' /etc/apt/sources.list
echo 'deb https://deb.debian.org/debian stretch-backports main' >/etc/apt/sources.list.d/backports.list

apt-get update

# Use newer packages from backports by default
cat >/etc/apt/preferences <<EOF
Package: *
Pin: release a=stretch-backports
Pin-Priority: 500
EOF

apt-get dist-upgrade -y

apt-get install -y --no-remove \
      bison \
      bzip2 \
      ccache \
      flex \
      g++ \
      gcc \
      git \
      libclang-3.9-dev \
      libclang-4.0-dev \
      libclang-5.0-dev \
      libclang-6.0-dev \
      libclang-7-dev \
      libclc-dev \
      libdrm-dev \
      libelf-dev \
      libepoxy-dev \
      libexpat1-dev \
      libpng-dev \
      libunwind-dev \
      llvm-3.9-dev \
      llvm-4.0-dev \
      llvm-5.0-dev \
      llvm-6.0-dev \
      llvm-7-dev \
      ninja-build \
      pkg-config \
      python-mako \
      python3-mako \
      python3-pip \
      python3-setuptools \
      python3-wheel \
      scons \
      xz-utils \
      zlib1g-dev

# We need at least 0.52.0, which is not in stretch
python3 -m pip install meson>=0.52

. .gitlab-ci/container/container_pre_build.sh

############### Uninstall unused packages

. .gitlab-ci/container/container_post_build.sh
