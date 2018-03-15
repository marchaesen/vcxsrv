#!/bin/sh

export PREFIX=/usr
export TRAVIS_BUILD_DIR=/root
export PIGLIT_DIR=$TRAVIS_BUILD_DIR/piglit
export XTEST_DIR=$TRAVIS_BUILD_DIR/xtest

set -e
set -x

meson setup build/
meson configure -Dprefix=$PREFIX build/
ninja -C build/ install
ninja -C build/ test
