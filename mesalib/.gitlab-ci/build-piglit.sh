#!/bin/bash

set -ex

git clone https://gitlab.freedesktop.org/mesa/piglit.git --single-branch --no-checkout /piglit
pushd /piglit
git checkout d5aa2cf85503c5339cd8dff04b709acb8085ad93
patch -p1 <$OLDPWD/.gitlab-ci/piglit/disable-vs_in.diff
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -j4
find -name .git -o -name '*ninja*' -o -iname '*cmake*' -o -name '*.[chao]' | xargs rm -rf
rm -rf target_api
popd
