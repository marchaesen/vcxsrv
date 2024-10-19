#!/usr/bin/env bash

set -e

arch=armhf . .gitlab-ci/container/debian/baremetal_arm_test.sh
