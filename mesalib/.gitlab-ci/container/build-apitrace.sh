#!/bin/bash

set -ex

APITRACE_VERSION="170424754bb46002ba706e16ee5404b61988d74a"

git clone https://github.com/apitrace/apitrace.git --single-branch --no-checkout /apitrace
pushd /apitrace
git checkout "$APITRACE_VERSION"
git submodule update --init --depth 1 --recursive
cmake -S . -B _build -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_GUI=False -DENABLE_WAFFLE=on $EXTRA_CMAKE_ARGS
ninja -C _build
mkdir build
cp _build/apitrace build
cp _build/eglretrace build
${STRIP_CMD:-strip} build/*
find . -not -path './build' -not -path './build/*' -delete
popd
