#!/bin/bash
#
# Copyright (C) 2022 Collabora Limited
# Author: Guilherme Gallo <guilherme.gallo@collabora.com>
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.


create_gn_args() {
    # gn can be configured to cross-compile skia and its tools
    # It is important to set the target_cpu to guarantee the intended target
    # machine
    cp "${BASE_ARGS_GN_FILE}" "${SKQP_OUT_DIR}"/args.gn
    echo "target_cpu = \"${SKQP_ARCH}\"" >> "${SKQP_OUT_DIR}"/args.gn
}


download_skia_source() {
    if [ -z ${SKIA_DIR+x} ]
    then
        return 1
    fi

    # Skia cloned from https://android.googlesource.com/platform/external/skqp
    # has all needed assets tracked on git-fs
    SKQP_REPO=https://android.googlesource.com/platform/external/skqp
    SKQP_BRANCH=android-cts-11.0_r7

    git clone --branch "${SKQP_BRANCH}" --depth 1 "${SKQP_REPO}" "${SKIA_DIR}"
}

set -ex

SCRIPT_DIR=$(realpath "$(dirname "$0")")
SKQP_PATCH_DIR="${SCRIPT_DIR}"
BASE_ARGS_GN_FILE="${SCRIPT_DIR}/build-skqp_base.gn"

SKQP_ARCH=${SKQP_ARCH:-x64}
SKIA_DIR=${SKIA_DIR:-$(mktemp -d)}
SKQP_OUT_DIR=${SKIA_DIR}/out/${SKQP_ARCH}
SKQP_INSTALL_DIR=${SKQP_INSTALL_DIR:-/skqp}
SKQP_ASSETS_DIR="${SKQP_INSTALL_DIR}/assets"
SKQP_BINARIES=(skqp list_gpu_unit_tests list_gms)

download_skia_source

pushd "${SKIA_DIR}"

# Apply all skqp patches for Mesa CI
cat "${SKQP_PATCH_DIR}"/build-skqp_*.patch |
    patch -p1

# Fetch some needed build tools needed to build skia/skqp.
# Basically, it clones repositories with commits SHAs from ${SKIA_DIR}/DEPS
# directory.
python tools/git-sync-deps

mkdir -p "${SKQP_OUT_DIR}"
mkdir -p "${SKQP_INSTALL_DIR}"

create_gn_args

# Build and install skqp binaries
bin/gn gen "${SKQP_OUT_DIR}"

for BINARY in "${SKQP_BINARIES[@]}"
do
    /usr/bin/ninja -C "${SKQP_OUT_DIR}" "${BINARY}"
    # Strip binary, since gn is not stripping it even when `is_debug == false`
    ${STRIP_CMD:-strip} "${SKQP_OUT_DIR}/${BINARY}"
    install -m 0755 "${SKQP_OUT_DIR}/${BINARY}" "${SKQP_INSTALL_DIR}"
done

# Move assets to the target directory, which will reside in rootfs.
mv platform_tools/android/apps/skqp/src/main/assets/ "${SKQP_ASSETS_DIR}"

popd
rm -Rf "${SKIA_DIR}"

set +ex
