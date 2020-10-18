#!/bin/bash

set -ex

git clone https://gitlab.freedesktop.org/mesa/piglit.git --single-branch --no-checkout /piglit
pushd /piglit
git checkout 6982bd10a9f71a36fbf441608aa0830d6b3fcf5b
patch -p1 <$OLDPWD/.gitlab-ci/piglit/disable-vs_in.diff
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja
find -name .git -o -name '*ninja*' -o -iname '*cmake*' -o -name '*.[chao]' | xargs rm -rf
rm -rf target_api
popd
