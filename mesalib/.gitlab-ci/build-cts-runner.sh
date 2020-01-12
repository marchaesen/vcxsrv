#!/bin/bash

set -ex

git clone https://gitlab.freedesktop.org/mesa/parallel-deqp-runner.git --depth 1 -b mesa-ci-2019-12-17
cd parallel-deqp-runner
meson build/ $EXTRA_MESON_ARGS
ninja -C build -j4 install
cd ..
rm -rf parallel-deqp-runner
