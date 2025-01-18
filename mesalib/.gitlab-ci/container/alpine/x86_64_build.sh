#!/usr/bin/env bash
# shellcheck disable=SC1091

# When changing this file, you need to bump the following
# .gitlab-ci/image-tags.yml tags:
# ALPINE_X86_64_BUILD_TAG

set -e

. .gitlab-ci/setup-test-env.sh

set -o xtrace

EPHEMERAL=(
)


DEPS=(
    bash
    bison
    ccache
    "clang${LLVM_VERSION}-dev"
    cmake
    clang-dev
    coreutils
    curl
    flex
    gcc
    g++
    git
    gettext
    glslang
    graphviz
    linux-headers
    "llvm${LLVM_VERSION}-static"
    "llvm${LLVM_VERSION}-dev"
    meson
    mold
    musl-dev
    expat-dev
    elfutils-dev
    libclc-dev
    libdrm-dev
    libva-dev
    libpciaccess-dev
    zlib-dev
    python3-dev
    py3-clang
    py3-cparser
    py3-mako
    py3-packaging
    py3-pip
    py3-ply
    py3-yaml
    vulkan-headers
    spirv-tools-dev
    spirv-llvm-translator-dev
    util-macros
    wayland-dev
    wayland-protocols
)

apk --no-cache add "${DEPS[@]}" "${EPHEMERAL[@]}"

pip3 install --break-system-packages sphinx===5.1.1 hawkmoth===0.16.0

. .gitlab-ci/container/container_pre_build.sh


############### Uninstall the build software

# too many vendor binarise, just keep the ones we need
find /usr/share/clc \
  \( -type f -o -type l \) \
  ! -name 'spirv-mesa3d-.spv' \
  ! -name 'spirv64-mesa3d-.spv' \
  -delete

apk del "${EPHEMERAL[@]}"

. .gitlab-ci/container/container_post_build.sh
