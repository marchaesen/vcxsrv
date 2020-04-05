#!/bin/bash

set -ex

git clone https://gitlab.freedesktop.org/mesa/parallel-deqp-runner.git --depth 1 -b mesa-ci-2019-12-17 /parallel-deqp-runner
pushd /parallel-deqp-runner
meson build/ $EXTRA_MESON_ARGS
ninja -C build install
popd
rm -rf /parallel-deqp-runner
