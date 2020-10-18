#!/bin/bash

set -ex

if [ -n "$INCLUDE_OPENCL_TESTS" ]; then
    PIGLIT_OPTS="-DPIGLIT_BUILD_CL_TESTS=ON"
fi

git clone https://gitlab.freedesktop.org/mesa/piglit.git --single-branch --no-checkout /piglit
pushd /piglit
git checkout 404862743cf8a7b37a4e3a93b4ba1858d59cd4ab
patch -p1 <$OLDPWD/.gitlab-ci/piglit/disable-vs_in.diff
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release $PIGLIT_OPTS
ninja
find -name .git -o -name '*ninja*' -o -iname '*cmake*' -o -name '*.[chao]' | xargs rm -rf
rm -rf target_api
popd
