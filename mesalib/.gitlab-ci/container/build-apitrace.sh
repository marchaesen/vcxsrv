#!/bin/bash
# shellcheck disable=SC2086 # we want word splitting

set -ex

APITRACE_VERSION="790380e05854d5c9d315555444ffcc7acb8f4037"

git clone https://github.com/apitrace/apitrace.git --single-branch --no-checkout /apitrace
pushd /apitrace
git checkout "$APITRACE_VERSION"
git submodule update --init --depth 1 --recursive
cmake -S . -B _build -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_GUI=False -DENABLE_WAFFLE=on $EXTRA_CMAKE_ARGS
cmake --build _build --parallel --target apitrace eglretrace
mkdir build
cp _build/apitrace build
cp _build/eglretrace build
${STRIP_CMD:-strip} build/*
find . -not -path './build' -not -path './build/*' -delete
popd
