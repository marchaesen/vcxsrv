#!/usr/bin/env bash

DEBIAN_ARCH="arm64" \
. .gitlab-ci/container/debian/test-gl.sh

. .gitlab-ci/container/strip-rootfs.sh
