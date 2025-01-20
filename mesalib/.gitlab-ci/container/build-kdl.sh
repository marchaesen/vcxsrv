#!/usr/bin/env bash
# shellcheck disable=SC1091  # the path is created by the script

set -ex

uncollapsed_section_start kdl "Building kdl"

KDL_REVISION="cbbe5fd54505fd03ee34f35bfd16794f0c30074f"
KDL_CHECKOUT_DIR="/tmp/ci-kdl.git"

mkdir -p ${KDL_CHECKOUT_DIR}
pushd ${KDL_CHECKOUT_DIR}
git init
git remote add origin https://gitlab.freedesktop.org/gfx-ci/ci-kdl.git
git fetch --depth 1 origin ${KDL_REVISION}
git checkout FETCH_HEAD
popd

# Run venv in a subshell, so we don't accidentally leak the venv state into
# calling scripts
(
	python3 -m venv /ci-kdl
	source /ci-kdl/bin/activate &&
	pushd ${KDL_CHECKOUT_DIR} &&
	pip install -r requirements.txt &&
	pip install . &&
	popd
)

rm -rf ${KDL_CHECKOUT_DIR}

section_end kdl
