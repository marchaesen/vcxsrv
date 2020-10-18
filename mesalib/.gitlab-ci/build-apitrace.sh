#!/bin/bash

set -ex

# Need an unreleased version of Waffle for surfaceless support in apitrace
# Replace this build with the Debian package once that's possible

WAFFLE_VERSION="e3c995d9a2693b687501715b6550619922346089"
git clone https://gitlab.freedesktop.org/mesa/waffle.git --single-branch --no-checkout /waffle
pushd /waffle
git checkout "$WAFFLE_VERSION"
cmake -B_build -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_BUILD_TYPE=Release $EXTRA_CMAKE_ARGS .
make -C _build install
mkdir -p build/lib build/bin
cp _build/lib/libwaffle-1.so build/lib/libwaffle-1.so.0
cp _build/bin/wflinfo build/bin/wflinfo
${STRIP_CMD:-strip} build/lib/* build/bin/*
find . -not -path './build' -not -path './build/*' -delete
popd

APITRACE_VERSION="9.0"

git clone https://github.com/apitrace/apitrace.git --single-branch --no-checkout /apitrace
pushd /apitrace
git checkout "$APITRACE_VERSION"
# Note: The cmake stuff for waffle in apitrace fails to use waffle's library
# directory.  Just force the issue here.
env LDFLAGS="-L/usr/local/lib" \
    cmake -G Ninja -B_build -H. -DCMAKE_BUILD_TYPE=Release -DENABLE_GUI=False -DENABLE_WAFFLE=on -DWaffle_DIR=/usr/local/lib/cmake/Waffle/ $EXTRA_CMAKE_ARGS
ninja -C _build
mkdir build
cp _build/apitrace build
cp _build/eglretrace build
${STRIP_CMD:-strip} build/*
find . -not -path './build' -not -path './build/*' -delete
popd
