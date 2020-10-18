#!/bin/bash

arch=ppc64el

. .gitlab-ci/container/cross_build.sh

apt-get install -y --no-remove \
        libvulkan-dev:$arch
