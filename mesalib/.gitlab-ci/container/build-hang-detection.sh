#!/bin/bash

set -ex

PARALLEL_DEQP_RUNNER_VERSION=6596b71cf37a7efb4d54acd48c770ed2d4ad6b7e

git clone https://gitlab.freedesktop.org/mesa/parallel-deqp-runner --single-branch -b master --no-checkout /parallel-deqp-runner
pushd /parallel-deqp-runner
git checkout "$PARALLEL_DEQP_RUNNER_VERSION"
meson . _build
ninja -C _build hang-detection
mkdir -p build/bin
install _build/hang-detection build/bin
strip build/bin/*
find . -not -path './build' -not -path './build/*' -delete
popd
