#!/bin/bash
# shellcheck disable=SC2086 # we want word splitting
set -uex

uncollapsed_section_start piglit "Building piglit"

# When changing this file, you need to bump the following
# .gitlab-ci/image-tags.yml tags:
# DEBIAN_TEST_GL_TAG
# DEBIAN_TEST_VK_TAG
# KERNEL_ROOTFS_TAG

REV="fc8179d319046f45346bcbcc5aaeabebdf151f03"

git clone https://gitlab.freedesktop.org/mesa/piglit.git --single-branch --no-checkout /piglit
pushd /piglit
git checkout "$REV"
patch -p1 <$OLDPWD/.gitlab-ci/piglit/disable-vs_in.diff
cmake -S . -B . -G Ninja -DCMAKE_BUILD_TYPE=Release $PIGLIT_OPTS ${EXTRA_CMAKE_ARGS:-}
ninja ${PIGLIT_BUILD_TARGETS:-}
find . -depth \( -name .git -o -name '*ninja*' -o -iname '*cmake*' -o -name '*.[chao]' \) \
       ! -name 'include_test.h' -exec rm -rf {} \;
rm -rf target_api
if [ "${PIGLIT_BUILD_TARGETS:-}" = "piglit_replayer" ]; then
    find . -depth \
         ! -regex "^\.$" \
         ! -regex "^\.\/piglit.*" \
         ! -regex "^\.\/framework.*" \
         ! -regex "^\.\/bin$" \
         ! -regex "^\.\/bin\/replayer\.py" \
         ! -regex "^\.\/templates.*" \
         ! -regex "^\.\/tests$" \
         ! -regex "^\.\/tests\/replay\.py" \
         -exec rm -rf {} \; 2>/dev/null
fi
popd

section_end piglit
