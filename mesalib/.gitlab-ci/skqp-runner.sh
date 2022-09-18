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

# Args:
# $1: section id
# $2: section header
gitlab_section_start() {
    echo -e "\e[0Ksection_start:$(date +%s):$1[collapsed=${GL_COLLAPSED:-false}]\r\e[0K\e[32;1m$2\e[0m"
}

# Args:
# $1: section id
gitlab_section_end() {
        echo -e "\e[0Ksection_end:$(date +%s):$1\r\e[0K"
}


# sponge allows piping to files that are being used as input.
# E.g.: sort file.txt | sponge file.txt
# In order to avoid installing moreutils just to have the sponge binary, we can
# use a bash function for it
# Source https://unix.stackexchange.com/a/561346/310927
sponge () (
    set +x
    append=false

    while getopts 'a' opt; do
        case $opt in
            a) append=true ;;
            *) echo error; exit 1
        esac
    done
    shift "$(( OPTIND - 1 ))"

    outfile=$1

    tmpfile=$(mktemp "$(dirname "$outfile")/tmp-sponge.XXXXXXXX") &&
    cat >"$tmpfile" &&
    if "$append"; then
        cat "$tmpfile" >>"$outfile"
    else
        if [ -f "$outfile" ]; then
            chmod --reference="$outfile" "$tmpfile"
        fi
        if [ -f "$outfile" ]; then
            mv "$tmpfile" "$outfile"
        elif [ -n "$outfile" ] && [ ! -e "$outfile" ]; then
            cat "$tmpfile" >"$outfile"
        else
            cat "$tmpfile"
        fi
    fi &&
    rm -f "$tmpfile"
)

remove_comments_from_files() (
    INPUT_FILES="$*"
    for INPUT_FILE in ${INPUT_FILES}
    do
        [ -f "${INPUT_FILE}" ] || continue
        sed -i '/#/d' "${INPUT_FILE}"
        sed -i '/^\s*$/d' "${INPUT_FILE}"
    done
)

subtract_test_lists() (
    MINUEND=$1
    sort "${MINUEND}" | sponge "${MINUEND}"
    shift
    for SUBTRAHEND in "$@"
    do
        sort "${SUBTRAHEND}" | sponge "${SUBTRAHEND}"
        join -v 1 "${MINUEND}" "${SUBTRAHEND}" |
            sponge "${MINUEND}"
    done
)

merge_rendertests_files() {
    BASE_FILE=$1
    shift
    FILES="$*"
    # shellcheck disable=SC2086
    cat $FILES "$BASE_FILE" |
        sort --unique --stable --field-separator=, --key=1,1 |
        sponge "$BASE_FILE"
}

assure_files() (
    for CASELIST_FILE in $*
    do
        >&2 echo "Looking for ${CASELIST_FILE}..."
        [ -f ${CASELIST_FILE} ] || (
            >&2 echo "Not found. Creating empty."
            touch ${CASELIST_FILE}
        )
    done
)

# Generate rendertests from scratch, customizing with fails/flakes/crashes files
generate_rendertests() (
    set -e
    GENERATED_FILE=$(mktemp)
    TESTS_FILE_PREFIX="${SKQP_FILE_PREFIX}-${SKQP_BACKEND}_rendertests"
    FLAKES_FILE="${TESTS_FILE_PREFIX}-flakes.txt"
    FAILS_FILE="${TESTS_FILE_PREFIX}-fails.txt"
    CRASHES_FILE="${TESTS_FILE_PREFIX}-crashes.txt"
    RENDER_TESTS_FILE="${TESTS_FILE_PREFIX}.txt"

    # Default to an empty known flakes file if it doesn't exist.
    assure_files ${FLAKES_FILE} ${FAILS_FILE} ${CRASHES_FILE}

    # skqp does not support comments in rendertests.txt file
    remove_comments_from_files "${FLAKES_FILE}" "${FAILS_FILE}" "${CRASHES_FILE}"

    # create an exhaustive rendertest list
    "${SKQP_BIN_DIR}"/list_gms | sort > "$GENERATED_FILE"

    # Remove undesirable tests from the list
    subtract_test_lists "${GENERATED_FILE}" "${CRASHES_FILE}" "${FLAKES_FILE}"

    # Add ",0" to each test to set the expected diff sum to zero
    sed -i 's/$/,0/g' "$GENERATED_FILE"

    merge_rendertests_files "$GENERATED_FILE" "${FAILS_FILE}"

    mv "${GENERATED_FILE}" "${RENDER_TESTS_FILE}"

    echo "${RENDER_TESTS_FILE}"
)

generate_unittests() (
    set -e
    GENERATED_FILE=$(mktemp)
    TESTS_FILE_PREFIX="${SKQP_FILE_PREFIX}_unittests"
    FLAKES_FILE="${TESTS_FILE_PREFIX}-flakes.txt"
    FAILS_FILE="${TESTS_FILE_PREFIX}-fails.txt"
    CRASHES_FILE="${TESTS_FILE_PREFIX}-crashes.txt"
    UNIT_TESTS_FILE="${TESTS_FILE_PREFIX}.txt"

    # Default to an empty known flakes file if it doesn't exist.
    assure_files ${FLAKES_FILE} ${FAILS_FILE} ${CRASHES_FILE}

    # Remove unitTest_ prefix
    for UT_FILE in "${FAILS_FILE}" "${CRASHES_FILE}" "${FLAKES_FILE}"; do
        sed -i 's/^unitTest_//g' "${UT_FILE}"
    done

    # create an exhaustive unittests list
    "${SKQP_BIN_DIR}"/list_gpu_unit_tests > "${GENERATED_FILE}"

    # Remove undesirable tests from the list
    subtract_test_lists "${GENERATED_FILE}" "${CRASHES_FILE}" "${FLAKES_FILE}" "${FAILS_FILE}"

    remove_comments_from_files "${GENERATED_FILE}"
    mv "${GENERATED_FILE}" "${UNIT_TESTS_FILE}"

    echo "${UNIT_TESTS_FILE}"
)

run_all_tests() {
    rm -f "${SKQP_ASSETS_DIR}"/skqp/*.txt
}

copy_tests_files() (
    # Copy either unit test or render test files from a specific driver given by
    # GPU VERSION variable.
    # If there is no test file at the expected location, this function will
    # return error_code 1
    SKQP_BACKEND="${1}"
    SKQP_FILE_PREFIX="${INSTALL}/${GPU_VERSION}-skqp"

    if echo "${SKQP_BACKEND}" | grep -qE 'vk|gl(es)?'
    then
        echo "Generating rendertests.txt file"
        GENERATED_RENDERTESTS=$(generate_rendertests)
        cp "${GENERATED_RENDERTESTS}" "${SKQP_ASSETS_DIR}"/skqp/rendertests.txt
        mkdir -p "${SKQP_RESULTS_DIR}/${SKQP_BACKEND}"
        cp "${GENERATED_RENDERTESTS}" "${SKQP_RESULTS_DIR}/${SKQP_BACKEND}/generated_rendertests.txt"
        return 0
    fi

    # The unittests.txt path is hardcoded inside assets directory,
    # that is why it needs to be a special case.
    if echo "${SKQP_BACKEND}" | grep -qE "unitTest"
    then
        echo "Generating unittests.txt file"
        GENERATED_UNITTESTS=$(generate_unittests)
        cp "${GENERATED_UNITTESTS}" "${SKQP_ASSETS_DIR}"/skqp/unittests.txt
        mkdir -p "${SKQP_RESULTS_DIR}/${SKQP_BACKEND}"
        cp "${GENERATED_UNITTESTS}" "${SKQP_RESULTS_DIR}/${SKQP_BACKEND}/generated_unittests.txt"
    fi
)

resolve_tests_files() {
    if [ -n "${RUN_ALL_TESTS}" ]
    then
        run_all_tests
        return
    fi

    SKQP_BACKEND=${1}
    if ! copy_tests_files "${SKQP_BACKEND}"
    then
        echo "No override test file found for ${SKQP_BACKEND}. Using the default one."
    fi
}

test_vk_backend() {
    if echo "${SKQP_BACKENDS:?}" | grep -qE 'vk'
    then
        if [ -n "$VK_DRIVER" ]; then
            return 0
        fi

        echo "VK_DRIVER environment variable is missing."
        # shellcheck disable=SC2012
        VK_DRIVERS=$(ls "$INSTALL"/share/vulkan/icd.d/ | cut -f 1 -d '_')
        if [ -n "${VK_DRIVERS}" ]
        then
            echo "Please set VK_DRIVER to the correct driver from the list:"
            echo "${VK_DRIVERS}"
        fi
        echo "No Vulkan tests will be executed, but it was requested in SKQP_BACKENDS variable. Exiting."
        exit 2
    fi

    # Vulkan environment is not configured, but it was not requested by the job
    return 1
}

setup_backends() {
    if test_vk_backend
    then
        export VK_ICD_FILENAMES="$INSTALL"/share/vulkan/icd.d/"$VK_DRIVER"_icd."${VK_CPU:-$(uname -m)}".json
    fi
}

show_reports() (
    set +xe

    # Unit tests produce empty HTML reports, guide the user to check the TXT file.
    if echo "${SKQP_BACKENDS}" | grep -qE "unitTest"
    then
        # Remove the empty HTML report to avoid confusion
        rm -f "${SKQP_RESULTS_DIR}"/unitTest/report.html

        echo "See skqp unit test results at:"
        echo "https://$CI_PROJECT_ROOT_NAMESPACE.pages.freedesktop.org/-/$CI_PROJECT_NAME/-/jobs/$CI_JOB_ID/artifacts${SKQP_RESULTS_DIR}/unitTest/unit_tests.txt"
    fi

    REPORT_FILES=$(mktemp)
    find "${SKQP_RESULTS_DIR}"/**/report.html -type f > "${REPORT_FILES}"
    while read -r REPORT
    do
        # shellcheck disable=SC2001
        BACKEND_NAME=$(echo "${REPORT}" | sed  's@.*/\([^/]*\)/report.html@\1@')
        echo "See skqp ${BACKEND_NAME} render tests report at:"
        echo "https://$CI_PROJECT_ROOT_NAMESPACE.pages.freedesktop.org/-/$CI_PROJECT_NAME/-/jobs/$CI_JOB_ID/artifacts${REPORT}"
    done < "${REPORT_FILES}"

    # If there is no report available, tell the user that something is wrong.
    if [ ! -s "${REPORT_FILES}" ]
    then
        echo "No skqp report available. Probably some fatal error has occured during the skqp execution."
    fi
)

usage() {
    cat <<EOF
    Usage: $(basename "$0") [-a]

    Arguments:
        -a: Run all unit tests and render tests, useful when introducing a new driver to skqp.
EOF
}

parse_args() {
    while getopts ':ah' opt; do
        case "$opt" in
            a)
            echo "Running all skqp tests"
            export RUN_ALL_TESTS=1
            shift
            ;;

            h)
            usage
            exit 0
            ;;

            ?)
            echo "Invalid command option."
            usage
            exit 1
            ;;
        esac
    done
}

set -e

parse_args "${@}"

# Needed so configuration files can contain paths to files in /install
INSTALL="$CI_PROJECT_DIR"/install

if [ -z "$GPU_VERSION" ]; then
    echo 'GPU_VERSION must be set to something like "llvmpipe" or
"freedreno-a630" (it will serve as a component to find the path for files
residing in src/**/ci/*.txt)'
    exit 1
fi

LD_LIBRARY_PATH=$INSTALL:$LD_LIBRARY_PATH
setup_backends

SKQP_BIN_DIR=${SKQP_BIN_DIR:-/skqp}
SKQP_ASSETS_DIR="${SKQP_BIN_DIR}"/assets
SKQP_RESULTS_DIR="${SKQP_RESULTS_DIR:-${PWD}/results}"

mkdir -p "${SKQP_ASSETS_DIR}"/skqp

# Show the reports on exit, even when a test crashes
trap show_reports INT TERM EXIT

SKQP_EXITCODE=0
for SKQP_BACKEND in ${SKQP_BACKENDS}
do
    resolve_tests_files "${SKQP_BACKEND}"
    SKQP_BACKEND_RESULTS_DIR="${SKQP_RESULTS_DIR}"/"${SKQP_BACKEND}"
    mkdir -p "${SKQP_BACKEND_RESULTS_DIR}"
    BACKEND_EXITCODE=0

    GL_COLLAPSED=true gitlab_section_start "skqp_${SKQP_BACKEND}" "skqp logs for ${SKQP_BACKEND}"
    "${SKQP_BIN_DIR}"/skqp "${SKQP_ASSETS_DIR}" "${SKQP_BACKEND_RESULTS_DIR}" "${SKQP_BACKEND}_" ||
        BACKEND_EXITCODE=$?
    gitlab_section_end "skqp_${SKQP_BACKEND}"

    if [ ! $BACKEND_EXITCODE -eq 0 ]
    then
        echo "skqp failed on ${SKQP_BACKEND} tests with exit code: ${BACKEND_EXITCODE}."
    else
        echo "skqp succeeded on ${SKQP_BACKEND}."
    fi

    # Propagate error codes to leverage the final job result
    SKQP_EXITCODE=$(( SKQP_EXITCODE | BACKEND_EXITCODE ))
done

exit $SKQP_EXITCODE
