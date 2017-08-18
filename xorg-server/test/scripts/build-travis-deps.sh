#!/bin/sh

export PREFIX=/usr
export TRAVIS_BUILD_DIR=/root
export PIGLIT_DIR=$TRAVIS_BUILD_DIR/piglit
export XTEST_DIR=$TRAVIS_BUILD_DIR/xtest

set -e

meson --prefix=/usr build/
ninja -C build/ install
ninja -C build/ test
