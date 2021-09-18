#!/bin/bash

set -e
set -o xtrace

export DEBIAN_FRONTEND=noninteractive

apt-get install -y ca-certificates

sed -i -e 's/http:\/\/deb/https:\/\/deb/g' /etc/apt/sources.list

# Ephemeral packages (installed for this script and removed again at
# the end)
STABLE_EPHEMERAL=" \
      cargo \
      python3-dev \
      python3-pip \
      python3-setuptools \
      python3-wheel \
      "

apt-get update
apt-get dist-upgrade -y

apt-get install -y --no-remove \
      git \
      git-lfs \
      libasan6 \
      libexpat1 \
      libllvm11 \
      libllvm9 \
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
      vulkan-tools \
      waffle-utils \
      xauth \
      xvfb \
      zlib1g

apt-get install -y --no-install-recommends \
      $STABLE_EPHEMERAL

# Needed for ci-fairy, this revision is able to upload files to MinIO
# and doesn't depend on git
pip3 install git+http://gitlab.freedesktop.org/freedesktop/ci-templates@0f1abc24c043e63894085a6bd12f14263e8b29eb

############### Build dEQP runner
. .gitlab-ci/container/build-deqp-runner.sh
rm -rf ~/.cargo

apt-get purge -y $STABLE_EPHEMERAL

apt-get autoremove -y --purge
