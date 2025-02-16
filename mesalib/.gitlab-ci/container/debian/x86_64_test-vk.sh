#!/usr/bin/env bash

DEBIAN_ARCH="amd64" \
. .gitlab-ci/container/debian/test-vk.sh

. .gitlab-ci/container/strip-rootfs.sh
