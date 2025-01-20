#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# © Collabora Limited
# Author: Guilherme Gallo <guilherme.gallo@collabora.com>

# This script runs unit/integration tests related with LAVA CI tools
# shellcheck disable=SC1091 # The relative paths in this file only become valid at runtime.
# shellcheck disable=SC2086 # quoting PYTEST_VERBOSE makes us pass an empty path

set -eu

PYTHON_BIN="python3.11"

if [ -z "${SCRIPTS_DIR:-}" ]; then
    SCRIPTS_DIR="$(dirname "${0}")"
fi

if [ -z "${CI_JOB_STARTED_AT:-}" ]; then
    CI_JOB_STARTED_AT=$(date -u +%Y-%m-%dT%H:%M:%SZ) # isoformat
fi

source "${SCRIPTS_DIR}/setup-test-env.sh"

if [ -z "${CI_PROJECT_DIR:-}" ]; then
    CI_PROJECT_DIR="$(dirname "${0}")/../"
fi

if [ -z "${CI_JOB_TIMEOUT:-}" ]; then
    # Export this default value, 1 hour in seconds, to test the lava job submitter
    export CI_JOB_TIMEOUT=3600
fi

# If running outside of the debian/x86_64_pyutils container,
# run in a virtual environment for isolation
# e.g. USE_VENV=true ./.gitlab-ci/run-pytest.sh
if [ "${USE_VENV:-}" == true ]; then
    echo "Setting up virtual environment for local testing."
    MESA_PYTEST_VENV="${CI_PROJECT_DIR}/.venv-pytest"
    ${PYTHON_BIN} -m venv "${MESA_PYTEST_VENV}"
    source "${MESA_PYTEST_VENV}"/bin/activate
    ${PYTHON_BIN} -m pip install --break-system-packages -r "${CI_PROJECT_DIR}/bin/ci/test/requirements.txt"
fi

LIB_TEST_DIR=${CI_PROJECT_DIR}/.gitlab-ci/tests
SCRIPT_TEST_DIR=${CI_PROJECT_DIR}/bin/ci

uncollapsed_section_start pytest "Running pytest"

PYTHONPATH="${LIB_TEST_DIR}:${SCRIPT_TEST_DIR}:${PYTHONPATH:-}" ${PYTHON_BIN} -m \
    pytest "${LIB_TEST_DIR}" "${SCRIPT_TEST_DIR}" \
            -W ignore::DeprecationWarning \
            --junitxml=artifacts/ci_scripts_report.xml \
            -m 'not slow' \
	    ${PYTEST_VERBOSE:-}

section_end pytest

section_start flake8 "flake8"
${PYTHON_BIN} -m flake8 \
--config "${CI_PROJECT_DIR}/.gitlab-ci/.flake8" \
"${LIB_TEST_DIR}" "${SCRIPT_TEST_DIR}"
section_end flake8
