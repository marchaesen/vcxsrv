#!/bin/bash

set -ex

RENDERDOC_VERSION=da02e88201dc3b64316fc33ce6ff69cc729689aa

git clone https://github.com/baldurk/renderdoc.git --single-branch --no-checkout /renderdoc
pushd /renderdoc
git checkout "$RENDERDOC_VERSION"
cmake -S . -B _build -G Ninja -DENABLE_QRENDERDOC=false -DCMAKE_BUILD_TYPE=Release $EXTRA_CMAKE_ARGS
ninja -C _build
mkdir -p build/lib
${STRIP_CMD:-strip} _build/lib/*.so
cp _build/lib/renderdoc.so build/lib
cp _build/lib/librenderdoc.so build/lib
find . -not -path './build' -not -path './build/*' -delete
popd
