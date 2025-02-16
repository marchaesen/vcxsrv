#!/usr/bin/env bash

DEBIAN_ARCH="armhf" \
. .gitlab-ci/container/debian/test-vk.sh

. .gitlab-ci/container/strip-rootfs.sh
