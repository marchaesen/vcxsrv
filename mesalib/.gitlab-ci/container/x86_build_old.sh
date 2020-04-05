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
      gettext \
      git \
      libclang-3.9-dev \
      libclang-4.0-dev \
      libclang-5.0-dev \
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
      meson \
      pkg-config \
      python-mako \
      python3-mako \
      scons \
      xz-utils \
      zlib1g-dev

. .gitlab-ci/container/container_pre_build.sh

############### Uninstall unused packages

. .gitlab-ci/container/container_post_build.sh
