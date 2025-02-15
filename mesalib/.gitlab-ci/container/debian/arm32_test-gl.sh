#!/usr/bin/env bash

DEBIAN_ARCH="armhf" \
. .gitlab-ci/container/debian/test-gl.sh

. .gitlab-ci/container/strip-rootfs.sh
