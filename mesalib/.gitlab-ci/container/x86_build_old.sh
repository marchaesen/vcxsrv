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
      llvm-3.9-dev \
      libclang-3.9-dev \
      llvm-4.0-dev \
      libclang-4.0-dev \
      llvm-5.0-dev \
      libclang-5.0-dev \
      g++ \
      bzip2 \
      ccache \
      zlib1g-dev \
      pkg-config \
      gcc \
      git \
      libepoxy-dev \
      libclc-dev \
      xz-utils \
      libdrm-dev \
      libexpat1-dev \
      libelf-dev \
      libunwind-dev \
      libpng-dev \
      python-mako \
      python3-mako \
      bison \
      flex \
      gettext \
      scons \
      meson


############### Uninstall unused packages

apt-get autoremove -y --purge
