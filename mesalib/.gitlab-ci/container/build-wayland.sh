#!/usr/bin/env bash
# shellcheck disable=SC2086 # we want word splitting

set -uex

uncollapsed_section_start wayland "Building Wayland"

# When changing this file, you need to bump the following
# .gitlab-ci/image-tags.yml tags:
# ALPINE_X86_64_BUILD_TAG
# DEBIAN_BASE_TAG
# DEBIAN_BUILD_TAG
# DEBIAN_TEST_ANDROID_TAG
# DEBIAN_TEST_GL_TAG
# DEBIAN_TEST_VK_TAG
# FEDORA_X86_64_BUILD_TAG
# KERNEL_ROOTFS_TAG

export LIBWAYLAND_VERSION="1.21.0"
export WAYLAND_PROTOCOLS_VERSION="1.41"

git clone https://gitlab.freedesktop.org/wayland/wayland
cd wayland
git checkout "$LIBWAYLAND_VERSION"
meson setup -Ddocumentation=false -Ddtd_validation=false -Dlibraries=true _build ${EXTRA_MESON_ARGS:-}
meson install -C _build
cd ..
rm -rf wayland

git clone https://gitlab.freedesktop.org/wayland/wayland-protocols
cd wayland-protocols
git checkout "$WAYLAND_PROTOCOLS_VERSION"
meson setup -Dtests=false _build ${EXTRA_MESON_ARGS:-}
meson install -C _build
cd ..
rm -rf wayland-protocols

section_end wayland
