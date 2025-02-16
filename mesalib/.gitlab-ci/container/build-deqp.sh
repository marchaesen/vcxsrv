#!/usr/bin/env bash
# shellcheck disable=SC2086 # we want word splitting

# When changing this file, you need to bump the following
# .gitlab-ci/image-tags.yml tags:
# DEBIAN_TEST_ANDROID_TAG
# DEBIAN_TEST_GL_TAG
# DEBIAN_TEST_VK_TAG
# KERNEL_ROOTFS_TAG

set -ue -o pipefail

# shellcheck disable=SC2153
deqp_api=${DEQP_API,,}

uncollapsed_section_start deqp-$deqp_api "Building dEQP $DEQP_API"

set -x

# See `deqp_build_targets` below for which release is used to produce which
# binary. Unless this comment has bitrotten:
# - the commit from the main branch produces the deqp tools and `deqp-vk`,
# - the VK release produces `deqp-vk`,
# - the GL release produces `glcts`, and
# - the GLES release produces `deqp-gles*` and `deqp-egl`

DEQP_MAIN_COMMIT=a9988483c0864d7190e5e6264ccead95423dfd00
DEQP_VK_VERSION=1.4.1.1
DEQP_GL_VERSION=4.6.5.0
DEQP_GLES_VERSION=3.2.11.0

# Patches to VulkanCTS may come from commits in their repo (listed in
# cts_commits_to_backport) or patch files stored in our repo (in the patch
# directory `$OLDPWD/.gitlab-ci/container/patches/` listed in cts_patch_files).
# Both list variables would have comments explaining the reasons behind the
# patches.

# shellcheck disable=SC2034
main_cts_commits_to_backport=(
    # If you find yourself wanting to add something in here, consider whether
    # bumping DEQP_MAIN_COMMIT is not a better solution :)
)

# shellcheck disable=SC2034
main_cts_patch_files=(
)

# shellcheck disable=SC2034
vk_cts_commits_to_backport=(
)

# shellcheck disable=SC2034
vk_cts_patch_files=(
)

# shellcheck disable=SC2034
gl_cts_commits_to_backport=(
  # Add #include <cmath> in deMath.h when being compiled by C++
  71808fe7d0a640dfd703e845d93ba1c5ab751055
  # Revert "Add #include <cmath> in deMath.h when being compiled by C++ compiler"
  # This also adds an alternative fix along with the revert.
  6164879a0acce258637d261592a9c395e564b361
)

# shellcheck disable=SC2034
gl_cts_patch_files=(
  build-deqp-gl_Build-Don-t-build-Vulkan-utilities-for-GL-builds.patch
)

# shellcheck disable=SC2034
# GLES builds also EGL
gles_cts_commits_to_backport=(
  # Add #include <cmath> in deMath.h when being compiled by C++
  71808fe7d0a640dfd703e845d93ba1c5ab751055
  # Revert "Add #include <cmath> in deMath.h when being compiled by C++ compiler"
  # This also adds an alternative fix along with the revert.
  6164879a0acce258637d261592a9c395e564b361
)

# shellcheck disable=SC2034
gles_cts_patch_files=(
  build-deqp-gl_Build-Don-t-build-Vulkan-utilities-for-GL-builds.patch
)

if [ "${DEQP_TARGET}" = 'android' ]; then
  gles_cts_patch_files+=(
    build-deqp-gles_Allow-running-on-Android-from-the-command-line.patch
    build-deqp-gles_Android-prints-to-stdout-instead-of-logcat.patch
  )
fi


### Careful editing anything below this line


git config --global user.email "mesa@example.com"
git config --global user.name "Mesa CI"

# shellcheck disable=SC2153
case "${DEQP_API}" in
  tools) DEQP_VERSION="$DEQP_MAIN_COMMIT";;
  *-main) DEQP_VERSION="$DEQP_MAIN_COMMIT";;
  VK) DEQP_VERSION="vulkan-cts-$DEQP_VK_VERSION";;
  GL) DEQP_VERSION="opengl-cts-$DEQP_GL_VERSION";;
  GLES) DEQP_VERSION="opengl-es-cts-$DEQP_GLES_VERSION";;
  *) echo "Unexpected DEQP_API value: $DEQP_API"; exit 1;;
esac

mkdir -p /VK-GL-CTS
pushd /VK-GL-CTS
[ -e .git ] || {
  git init
  git remote add origin https://github.com/KhronosGroup/VK-GL-CTS.git
}
git fetch --depth 1 origin "$DEQP_VERSION"
git checkout FETCH_HEAD
DEQP_COMMIT=$(git rev-parse FETCH_HEAD)

if [ "$DEQP_VERSION" = "$DEQP_MAIN_COMMIT" ]; then
  merge_base="$(curl -s https://api.github.com/repos/KhronosGroup/VK-GL-CTS/compare/main...$DEQP_MAIN_COMMIT | jq -r .merge_base_commit.sha)"
  if [[ "$merge_base" != "$DEQP_MAIN_COMMIT" ]]; then
    echo "VK-GL-CTS commit $DEQP_MAIN_COMMIT is not a commit from the main branch."
    exit 1
  fi
fi

mkdir -p /deqp-$deqp_api

if [ "$DEQP_VERSION" = "$DEQP_MAIN_COMMIT" ]; then
  prefix="main"
else
  prefix="$deqp_api"
fi

cts_commits_to_backport="${prefix}_cts_commits_to_backport[@]"
for commit in "${!cts_commits_to_backport}"
do
  PATCH_URL="https://github.com/KhronosGroup/VK-GL-CTS/commit/$commit.patch"
  echo "Apply patch to ${DEQP_API} CTS from $PATCH_URL"
  curl -L --retry 4 -f --retry-all-errors --retry-delay 60 $PATCH_URL | \
    GIT_COMMITTER_DATE=$(LC_TIME=C date -d@0) git am -
done

cts_patch_files="${prefix}_cts_patch_files[@]"
for patch in "${!cts_patch_files}"
do
  echo "Apply patch to ${DEQP_API} CTS from $patch"
  GIT_COMMITTER_DATE=$(LC_TIME=C date -d@0) git am < $OLDPWD/.gitlab-ci/container/patches/$patch
done

{
  if [ "$DEQP_VERSION" = "$DEQP_MAIN_COMMIT" ]; then
    commit_desc=$(git show --no-patch --format='commit %h on %ci' --abbrev=10 "$DEQP_COMMIT")
    echo "dEQP $DEQP_API at $commit_desc"
  else
    echo "dEQP $DEQP_API version $DEQP_VERSION"
  fi
  if [ "$(git rev-parse HEAD)" != "$DEQP_COMMIT" ]; then
    echo "The following local patches are applied on top:"
    git log --reverse --oneline "$DEQP_COMMIT".. --format='- %s'
  fi
} > /deqp-$deqp_api/deqp-$deqp_api-version

# --insecure is due to SSL cert failures hitting sourceforge for zlib and
# libpng (sigh).  The archives get their checksums checked anyway, and git
# always goes through ssh or https.
python3 external/fetch_sources.py --insecure

if [[ "$DEQP_API" = tools ]]; then
  # Save the testlog stylesheets:
  cp doc/testlog-stylesheet/testlog.{css,xsl} /deqp-$deqp_api
fi

popd

deqp_build_targets=()
case "${DEQP_API}" in
  VK|VK-main)
    deqp_build_targets+=(deqp-vk)
    ;;
  GL)
    deqp_build_targets+=(glcts)
    ;;
  GLES)
    deqp_build_targets+=(deqp-gles{2,3,31})
    deqp_build_targets+=(glcts)  # needed for gles*-khr tests
    # deqp-egl also comes from this build, but it is handled separately below.
    ;;
  tools)
    deqp_build_targets+=(testlog-to-xml)
    deqp_build_targets+=(testlog-to-csv)
    deqp_build_targets+=(testlog-to-junit)
    ;;
esac

OLD_IFS="$IFS"
IFS=";"
CMAKE_SBT="${deqp_build_targets[*]}"
IFS="$OLD_IFS"

pushd /deqp-$deqp_api

if [ "${DEQP_API}" = 'GLES' ]; then
  if [ "${DEQP_TARGET}" = 'android' ]; then
    cmake -S /VK-GL-CTS -B . -G Ninja \
        -DDEQP_TARGET=android \
        -DCMAKE_BUILD_TYPE=Release \
        -DSELECTED_BUILD_TARGETS="deqp-egl" \
        ${EXTRA_CMAKE_ARGS:-}
    ninja modules/egl/deqp-egl
    mv modules/egl/deqp-egl{,-android}
  else
    # When including EGL/X11 testing, do that build first and save off its
    # deqp-egl binary.
    cmake -S /VK-GL-CTS -B . -G Ninja \
        -DDEQP_TARGET=x11_egl_glx \
        -DCMAKE_BUILD_TYPE=Release \
        -DSELECTED_BUILD_TARGETS="deqp-egl" \
        ${EXTRA_CMAKE_ARGS:-}
    ninja modules/egl/deqp-egl
    mv modules/egl/deqp-egl{,-x11}

    cmake -S /VK-GL-CTS -B . -G Ninja \
        -DDEQP_TARGET=wayland \
        -DCMAKE_BUILD_TYPE=Release \
        -DSELECTED_BUILD_TARGETS="deqp-egl" \
        ${EXTRA_CMAKE_ARGS:-}
    ninja modules/egl/deqp-egl
    mv modules/egl/deqp-egl{,-wayland}
  fi
fi

cmake -S /VK-GL-CTS -B . -G Ninja \
      -DDEQP_TARGET=${DEQP_TARGET} \
      -DCMAKE_BUILD_TYPE=Release \
      -DSELECTED_BUILD_TARGETS="${CMAKE_SBT}" \
      ${EXTRA_CMAKE_ARGS:-}

# Make sure `default` doesn't silently stop detecting one of the platforms we care about
if [ "${DEQP_TARGET}" = 'default' ]; then
  grep -q DEQP_SUPPORT_WAYLAND=1 build.ninja
  grep -q DEQP_SUPPORT_X11=1 build.ninja
  grep -q DEQP_SUPPORT_XCB=1 build.ninja
fi

ninja "${deqp_build_targets[@]}"

if [ "$DEQP_API" != tools ]; then
    # Copy out the mustpass lists we want.
    mkdir -p mustpass

    if [ "${DEQP_API}" = 'VK' ] || [ "${DEQP_API}" = 'VK-main' ]; then
        for mustpass in $(< /VK-GL-CTS/external/vulkancts/mustpass/main/vk-default.txt) ; do
            cat /VK-GL-CTS/external/vulkancts/mustpass/main/$mustpass \
                >> mustpass/vk-main.txt
        done
    fi

    if [ "${DEQP_API}" = 'GL' ]; then
        cp \
            /VK-GL-CTS/external/openglcts/data/gl_cts/data/mustpass/gl/khronos_mustpass/main/*-main.txt \
            mustpass/
        cp \
            /VK-GL-CTS/external/openglcts/data/gl_cts/data/mustpass/gl/khronos_mustpass_single/main/*-single.txt \
            mustpass/
    fi

    if [ "${DEQP_API}" = 'GLES' ]; then
        cp \
            /VK-GL-CTS/external/openglcts/data/gl_cts/data/mustpass/gles/aosp_mustpass/main/*.txt \
            mustpass/
        cp \
            /VK-GL-CTS/external/openglcts/data/gl_cts/data/mustpass/egl/aosp_mustpass/main/egl-main.txt \
            mustpass/
        cp \
            /VK-GL-CTS/external/openglcts/data/gl_cts/data/mustpass/gles/khronos_mustpass/main/*-main.txt \
            mustpass/
    fi

    # Compress the caselists, since Vulkan's in particular are gigantic; higher
    # compression levels provide no real measurable benefit.
    zstd -1 --rm mustpass/*.txt
fi

if [ "$DEQP_API" = tools ]; then
    # Save *some* executor utils, but otherwise strip things down
    # to reduct deqp build size:
    mv executor/testlog-to-* .
    rm -rf executor
fi

# Remove other mustpass files, since we saved off the ones we wanted to conventient locations above.
rm -rf external/**/mustpass/
rm -rf external/vulkancts/modules/vulkan/vk-main*
rm -rf external/vulkancts/modules/vulkan/vk-default

rm -rf external/openglcts/modules/cts-runner
rm -rf modules/internal
rm -rf execserver
rm -rf framework
find . -depth \( -iname '*cmake*' -o -name '*ninja*' -o -name '*.o' -o -name '*.a' \) -exec rm -rf {} \;
if [ "${DEQP_API}" = 'VK' ] || [ "${DEQP_API}" = 'VK-main' ]; then
  ${STRIP_CMD:-strip} external/vulkancts/modules/vulkan/deqp-vk
fi
if [ "${DEQP_API}" = 'GL' ] || [ "${DEQP_API}" = 'GLES' ]; then
  ${STRIP_CMD:-strip} external/openglcts/modules/glcts
fi
if [ "${DEQP_API}" = 'GLES' ]; then
  ${STRIP_CMD:-strip} modules/*/deqp-*
fi
du -sh ./*
popd

section_end deqp-$deqp_api
