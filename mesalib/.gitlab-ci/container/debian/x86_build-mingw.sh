#!/bin/bash

set -e
set -o xtrace

apt-get update
apt-get install -y --no-remove \
        zstd \
        g++-mingw-w64-i686 \
        g++-mingw-w64-x86-64

. .gitlab-ci/container/debian/x86_build-mingw-patch.sh
. .gitlab-ci/container/debian/x86_build-mingw-source-deps.sh
