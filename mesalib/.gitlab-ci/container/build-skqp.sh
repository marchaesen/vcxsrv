#!/bin/bash

create_gn_args() {
    cp "${BASE_ARGS_GN_FILE}" "${SKQP_OUT_DIR}"/args.gn
    echo "target_cpu = \"${SKQP_ARCH}\"" >> "${SKQP_OUT_DIR}"/args.gn
}

download_skqp_models() (
    # The download_model.py script needs a checksum file to know what models
    # version to download.

    # This is the most recent commit available in the skia repository with a
    # valid files.checksum
    SKIA_LAST_SKQP_CUT_COMMIT_SHA=ccf5f0d75b6a6b54756f2c62d57e3730eed8aa45
    git fetch origin "${SKIA_LAST_SKQP_CUT_COMMIT_SHA}:refs/remotes/origin/${SKIA_LAST_SKQP_CUT_COMMIT_SHA}"
    git checkout "${SKIA_LAST_SKQP_CUT_COMMIT_SHA}" -- \
        platform_tools/android/apps/skqp/src/main/assets/files.checksum

    # The following patch transforms download_model.py from python2 to python3.
    git apply "${DOWNLOAD_MODEL_PATCH_FILE}"
    python3 tools/skqp/download_model.py

    # Copy resources from skia to skqp directory
    python3 tools/skqp/setup_resources
)

set -ex

SCRIPT_DIR=$(realpath "$(dirname "$0")")
FETCH_GN_PATCH_FILE="${SCRIPT_DIR}/build-skqp_fetch-gn.patch"
BASE_ARGS_GN_FILE="${SCRIPT_DIR}/build-skqp_base.gn"
DOWNLOAD_MODEL_PATCH_FILE="${SCRIPT_DIR}/build-skqp_download_model.patch"

SKQP_ARCH=${SKQP_ARCH:-x64}
SKIA_DIR=${SKIA_DIR:-$(mktemp -d)}
SKQP_DIR=${SKQP_DIR:-$(mktemp -d)}
SKQP_OUT_DIR=${SKIA_DIR}/out/${SKQP_ARCH}
SKQP_INSTALL_DIR=/skqp
SKQP_ASSETS_DIR="${SKQP_INSTALL_DIR}/assets"
# Build list_gpu_unit_tests to update the unittests.txt file properly to the
# target hardware.
SKQP_BINARIES=(skqp list_gpu_unit_tests)

# Using a recent release version to mitigate instability during test phase
SKIA_COMMIT_SHA="canvaskit/0.32.0"

git clone 'https://skia.googlesource.com/skia/' \
    --single-branch \
    -b "${SKIA_COMMIT_SHA}" \
    "${SKIA_DIR}"

pushd "${SKIA_DIR}"

git apply "${FETCH_GN_PATCH_FILE}"
# Fetch some needed build tools needed to build skia/skqp
# Basically, it clones repositories with commits SHAs from
# ${SKIA_DIR}/DEPS directory
python3 tools/git-sync-deps

mkdir -p "${SKQP_OUT_DIR}"
mkdir -p "${SKQP_INSTALL_DIR}"

create_gn_args

# Build and install skqp binaries
bin/gn gen "${SKQP_OUT_DIR}"

for BINARY in "${SKQP_BINARIES[@]}"
do
    /usr/bin/ninja -C "${SKQP_OUT_DIR}" "${BINARY}"
    install -m 0755 "${SKQP_OUT_DIR}/${BINARY}" "${SKQP_INSTALL_DIR}"
done

# Acquire assets and move them to the target directory.
download_skqp_models
mv platform_tools/android/apps/skqp/src/main/assets/ "${SKQP_ASSETS_DIR}"

popd
rm -Rf "${SKQP_DIR}"
rm -Rf "${SKIA_DIR}"

set +ex
