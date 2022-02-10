#!/bin/sh

copy_tests_files() (
    SKQP_BACKEND="${1}"
    SKQP_FILE_PREFIX="${INSTALL}/${GPU_VERSION}-skqp"

    if echo "${SKQP_BACKEND}" | grep -qE 'gl(es)?'
    then
        SKQP_RENDER_TESTS_FILE="${SKQP_FILE_PREFIX}-${SKQP_BACKEND}_rendertests.txt"
        cp "${SKQP_RENDER_TESTS_FILE}" "${SKQP_ASSETS_DIR}"/skqp/rendertests.txt
        return 0
    fi

    # The unittests.txt path is hardcoded inside assets directory,
    # that is why it needs to be a special case.
    if echo "${SKQP_BACKEND}" | grep -qE "unitTest"
    then
        cp "${SKQP_FILE_PREFIX}_unittests.txt" "${SKQP_ASSETS_DIR}"/skqp/unittests.txt
    fi
)

set -ex

# Needed so configuration files can contain paths to files in /install
ln -sf "$CI_PROJECT_DIR"/install /install

INSTALL=${PWD}/install

if [ -z "$GPU_VERSION" ]; then
   echo 'GPU_VERSION must be set to something like "llvmpipe" or "freedreno-a630" (the name used in .gitlab-ci/gpu-version-*.txt)'
   exit 1
fi

SKQP_ASSETS_DIR=/skqp/assets
SKQP_RESULTS_DIR="${SKQP_RESULTS_DIR:-results}"

mkdir "${SKQP_ASSETS_DIR}"/skqp

SKQP_EXITCODE=0
for SKQP_BACKEND in ${SKQP_BACKENDS}
do
    set -e
    copy_tests_files "${SKQP_BACKEND}"

    set +e
    SKQP_BACKEND_RESULTS_DIR="${SKQP_RESULTS_DIR}"/"${SKQP_BACKEND}"
    mkdir -p "${SKQP_BACKEND_RESULTS_DIR}"
    /skqp/skqp "${SKQP_ASSETS_DIR}" '' "${SKQP_BACKEND_RESULTS_DIR}" "${SKQP_BACKEND}_"
    BACKEND_EXITCODE=$?

    if [ ! $BACKEND_EXITCODE -eq 0 ]
    then
        echo "skqp failed on ${SKQP_BACKEND} tests with ${BACKEND_EXITCODE} exit code."
    fi

    # Propagate error codes to leverage the final job result
    SKQP_EXITCODE=$(( SKQP_EXITCODE | BACKEND_EXITCODE ))
done

set +x

# Unit tests produce empty HTML reports, guide the user to check the TXT file.
if echo "${SKQP_BACKENDS}" | grep -qE "unitTest"
then
    # Remove the empty HTML report to avoid confusion
    rm -f "${SKQP_RESULTS_DIR}"/unitTest/report.html

    echo "See skqp unit test results at:"
    echo "https://$CI_PROJECT_ROOT_NAMESPACE.pages.freedesktop.org/-/$CI_PROJECT_NAME/-/jobs/$CI_JOB_ID/artifacts/${SKQP_RESULTS_DIR}/unitTest/unit_tests.txt"
fi

for REPORT in "${SKQP_RESULTS_DIR}"/**/report.html
do
    BACKEND_NAME=$(echo "${REPORT}" | sed  's@.*/\([^/]*\)/report.html@\1@')
    echo "See skqp ${BACKEND_NAME} render tests report at:"
    echo "https://$CI_PROJECT_ROOT_NAMESPACE.pages.freedesktop.org/-/$CI_PROJECT_NAME/-/jobs/$CI_JOB_ID/artifacts/${REPORT}"
done

# If there is no report available, tell the user that something is wrong.
if [ ! -f "${REPORT}" ]
then
    echo "No skqp report available. Probably some fatal error has occured during the skqp execution."
fi

exit $SKQP_EXITCODE
