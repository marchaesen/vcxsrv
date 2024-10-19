#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# © Collabora Limited
# Author: Guilherme Gallo <guilherme.gallo@collabora.com>

# This script runs unit/integration tests related with LAVA CI tools
# shellcheck disable=SC1091 # The relative paths in this file only become valid at runtime.
# shellcheck disable=SC2086 # quoting PYTEST_VERBOSE makes us pass an empty path

. "${SCRIPTS_DIR}/setup-test-env.sh"

section_start pytest_setup "Setting up pytest environment"

set -exu

if [ -z "${CI_PROJECT_DIR:-}" ]; then
    CI_PROJECT_DIR="$(dirname "${0}")/../"
fi

if [ -z "${MESA_PYTEST_VENV:-}" ]; then
    MESA_PYTEST_VENV="${CI_PROJECT_DIR}/.venv-pytest"
fi

# Use this script in a python virtualenv for isolation
python3 -m venv "${MESA_PYTEST_VENV}"
. "${MESA_PYTEST_VENV}"/bin/activate

python3 -m pip install --break-system-packages -r "${CI_PROJECT_DIR}/bin/ci/requirements.txt"
python3 -m pip install --break-system-packages -r "${CI_PROJECT_DIR}/bin/ci/test/requirements.txt"

LIB_TEST_DIR=${CI_PROJECT_DIR}/.gitlab-ci/tests
SCRIPT_TEST_DIR=${CI_PROJECT_DIR}/bin/ci

uncollapsed_section_switch pytest "Running pytest"

PYTHONPATH="${LIB_TEST_DIR}:${SCRIPT_TEST_DIR}:${PYTHONPATH:-}" python3 -m \
    pytest "${LIB_TEST_DIR}" "${SCRIPT_TEST_DIR}" \
            -W ignore::DeprecationWarning \
            --junitxml=artifacts/ci_scripts_report.xml \
            -m 'not slow' \
	    ${PYTEST_VERBOSE:-}

section_end pytest
