#!/usr/bin/env bash

set -e

arch=arm64 . .gitlab-ci/container/debian/baremetal_arm_test.sh
