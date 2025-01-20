#!/usr/bin/env bash
# The relative paths in this file only become valid at runtime.
# shellcheck disable=SC1091
# shellcheck disable=SC2086 # we want word splitting

set -e

. .gitlab-ci/setup-test-env.sh

set -o xtrace

uncollapsed_section_start debian_setup "Base Debian system setup"

export DEBIAN_FRONTEND=noninteractive

apt-get install -y libelogind0  # this interfere with systemd deps, install separately

# Ephemeral packages (installed for this script and removed again at the end)
EPHEMERAL=(
    ccache
    cmake
    dpkg-dev
    g++
    glslang-tools
    libexpat1-dev
    gnupg2
    libdrm-dev
    libgbm-dev
    libgles2-mesa-dev
    liblz4-dev
    libpciaccess-dev
    libudev-dev
    libvulkan-dev
    libwaffle-dev
    libx11-xcb-dev
    libxcb-dri2-0-dev
    libxcb-ewmh-dev
    libxcb-keysyms1-dev
    libxkbcommon-dev
    libxrandr-dev
    libxrender-dev
    libzstd-dev
    meson
    p7zip
    patch
    pkgconf
    python3-dev
    python3-distutils
    python3-pip
    python3-setuptools
    python3-wheel
    software-properties-common
    wine64-tools
    xz-utils
)

DEPS=(
    curl
    libepoxy0
    libxcb-shm0
    pciutils
    python3-lxml
    python3-simplejson
    sysvinit-core
    weston
    xwayland
    wine
    wine64
    xinit
    xserver-xorg-video-amdgpu
    xserver-xorg-video-ati
)

apt-get update

apt-get install -y --no-remove --no-install-recommends \
      "${DEPS[@]}" "${EPHEMERAL[@]}"

############### Building ...

. .gitlab-ci/container/container_pre_build.sh

section_end debian_setup

############### Build piglit replayer

# We don't run any _piglit_ Vulkan tests in the containers.
PIGLIT_OPTS="-DPIGLIT_USE_WAFFLE=ON
	     -DPIGLIT_USE_GBM=OFF
	     -DPIGLIT_USE_WAYLAND=OFF
	     -DPIGLIT_USE_X11=OFF
	     -DPIGLIT_BUILD_GLX_TESTS=OFF
	     -DPIGLIT_BUILD_EGL_TESTS=OFF
	     -DPIGLIT_BUILD_WGL_TESTS=OFF
	     -DPIGLIT_BUILD_GL_TESTS=OFF
	     -DPIGLIT_BUILD_GLES1_TESTS=OFF
	     -DPIGLIT_BUILD_GLES2_TESTS=OFF
	     -DPIGLIT_BUILD_GLES3_TESTS=OFF
	     -DPIGLIT_BUILD_CL_TESTS=OFF
	     -DPIGLIT_BUILD_VK_TESTS=OFF
	     -DPIGLIT_BUILD_DMA_BUF_TESTS=OFF" \
  PIGLIT_BUILD_TARGETS="piglit_replayer" \
  . .gitlab-ci/container/build-piglit.sh

############### Build dEQP VK

DEQP_API=tools \
DEQP_TARGET=default \
. .gitlab-ci/container/build-deqp.sh

DEQP_API=VK-main \
DEQP_TARGET=default \
. .gitlab-ci/container/build-deqp.sh

DEQP_API=VK \
DEQP_TARGET=default \
. .gitlab-ci/container/build-deqp.sh

rm -rf /VK-GL-CTS

############### Build apitrace

. .gitlab-ci/container/build-apitrace.sh

############### Build Fossilize

. .gitlab-ci/container/build-fossilize.sh

############### Build gfxreconstruct

. .gitlab-ci/container/build-gfxreconstruct.sh

############### Build VKD3D-Proton

. .gitlab-ci/container/setup-wine.sh "/vkd3d-proton-wine64"

. .gitlab-ci/container/build-vkd3d-proton.sh

############### Uninstall the build software

uncollapsed_section_switch debian_cleanup "Cleaning up base Debian system"

apt-get purge -y "${EPHEMERAL[@]}"

. .gitlab-ci/container/container_post_build.sh

section_end debian_cleanup
