#!/bin/sh

set -ex

INSTALL=$(realpath -s "$PWD"/install)

RESULTS=$(realpath -s "$PWD"/results)
mkdir -p "$RESULTS"

# Set up the driver environment.
# Modifiying here directly LD_LIBRARY_PATH may cause problems when
# using a command wrapper. Hence, we will just set it when running the
# command.
export __LD_LIBRARY_PATH="$LD_LIBRARY_PATH:$INSTALL/lib/"

# Sanity check to ensure that our environment is sufficient to make our tests
# run against the Mesa built by CI, rather than any installed distro version.
MESA_VERSION=$(cat "$INSTALL/VERSION" | sed 's/\./\\./g')

if [ "$VK_DRIVER" ]; then

    ### VULKAN ###

    # Set the Vulkan driver to use.
    export VK_ICD_FILENAMES="$INSTALL/share/vulkan/icd.d/${VK_DRIVER}_icd.x86_64.json"

    if [ "x$PIGLIT_PROFILES" = "xreplay" ]; then
        # Set environment for Wine.
        export WINEDEBUG="-all"
        export WINEPREFIX="/dxvk-wine64"
        export WINEESYNC=1

        # Set environment for DXVK.
        export DXVK_LOG_LEVEL="none"
        export DXVK_STATE_CACHE=0

        # Set environment for gfxreconstruct executables.
        export PATH="/gfxreconstruct/build/bin:$PATH"
    fi

    SANITY_MESA_VERSION_CMD="vulkaninfo"


    # Set up the Window System Interface (WSI)

    # IMPORTANT:
    #
    # Nothing to do here.
    #
    # Run vulkan against the host's running X server (xvfb doesn't
    # have DRI3 support).
    # Set the DISPLAY env variable in each gitlab-runner's
    # configuration file:
    # https://docs.gitlab.com/runner/configuration/advanced-configuration.html#the-runners-section
else

    ### GL/ES ###

    if [ "x$PIGLIT_PROFILES" = "xreplay" ]; then
        # Set environment for apitrace executable.
        export PATH="/apitrace/build:$PATH"

        # Our rootfs may not have "less", which apitrace uses during
        # apitrace dump
        export PAGER=cat
    fi

    SANITY_MESA_VERSION_CMD="wflinfo"


    # Set up the platform windowing system.

    if [ "x$EGL_PLATFORM" = "xsurfaceless" ]; then

        # Use the surfaceless EGL platform.
        export DISPLAY=
        export WAFFLE_PLATFORM="surfaceless_egl"

        SANITY_MESA_VERSION_CMD="$SANITY_MESA_VERSION_CMD --platform surfaceless_egl --api gles2"

        if [ "x$GALLIUM_DRIVER" = "xvirpipe" ]; then
            # piglit is to use virpipe, and virgl_test_server llvmpipe
            export GALLIUM_DRIVER="$GALLIUM_DRIVER"

            LD_LIBRARY_PATH="$__LD_LIBRARY_PATH" \
            GALLIUM_DRIVER=llvmpipe \
            GALLIVM_PERF="nopt,no_filter_hacks" \
            VTEST_USE_EGL_SURFACELESS=1 \
            VTEST_USE_GLES=1 \
            virgl_test_server >"$RESULTS"/vtest-log.txt 2>&1 &

            sleep 1
        fi
    elif [ "x$PIGLIT_PLATFORM" = "xgbm" ]; then
        SANITY_MESA_VERSION_CMD="$SANITY_MESA_VERSION_CMD --platform gbm --api gl"
    elif [ "x$PIGLIT_PLATFORM" = "xmixed_glx_egl" ]; then
        # It is assumed that you have already brought up your X server before
        # calling this script.
        SANITY_MESA_VERSION_CMD="$SANITY_MESA_VERSION_CMD --platform glx --api gl"
    else
        SANITY_MESA_VERSION_CMD="$SANITY_MESA_VERSION_CMD --platform glx --api gl --profile core"
        RUN_CMD_WRAPPER="xvfb-run --server-args=\"-noreset\" sh -c"
    fi
fi

if [ "$ZINK_USE_LAVAPIPE" ]; then
    export VK_ICD_FILENAMES="$INSTALL/share/vulkan/icd.d/lvp_icd.x86_64.json"
fi

# If the job is parallel at the  gitlab job level, will take the corresponding
# fraction of the caselist.
if [ -n "$CI_NODE_INDEX" ]; then

    if [ "$PIGLIT_PROFILES" != "${PIGLIT_PROFILES% *}" ]; then
        FAILURE_MESSAGE=$(printf "%s" "Can't parallelize piglit with multiple profiles")
        quiet print_red printf "%s\n" "$FAILURE_MESSAGE"
        exit 1
    fi

    USE_CASELIST=1
fi

print_red() {
    RED='\033[0;31m'
    NC='\033[0m' # No Color
    printf "${RED}"
    "$@"
    printf "${NC}"
}

# wrapper to supress +x to avoid spamming the log
quiet() {
    set +x
    "$@"
    set -x
}

replay_minio_upload_images() {
    find "$RESULTS/$__PREFIX" -type f -name "*.png" -printf "%P\n" \
        | while read -r line; do

        __TRACE="${line%-*-*}"
        if grep -q "^$__PREFIX/$__TRACE: pass$" ".gitlab-ci/piglit/$PIGLIT_RESULTS.txt.orig"; then
            if [ "x$CI_PROJECT_PATH" != "x$FDO_UPSTREAM_REPO" ]; then
                continue
            fi
            __MINIO_PATH="$PIGLIT_REPLAY_REFERENCE_IMAGES_BASE_URL"
            __DESTINATION_FILE_PATH="${line##*-}"
            if ci-fairy minio ls "minio://${MINIO_HOST}${__MINIO_PATH}/${__DESTINATION_FILE_PATH}" 2>/dev/null; then
                continue
            fi
        else
            __MINIO_PATH="$PIGLIT_REPLAY_ARTIFACTS_BASE_URL"
            __DESTINATION_FILE_PATH="$__MINIO_TRACES_PREFIX/${line##*-}"
            # Adding to the JUnit the direct link to the diff page in
            # the dashboard
            __PIGLIT_TESTCASE_CLASSNAME="piglit\.trace\.$PIGLIT_REPLAY_DEVICE_NAME\.$(dirname $__TRACE | sed 's%/%\\.%g;s@%@\\%@')"
            __PIGLIT_TESTCASE_NAME="$(basename $__TRACE | sed 's%\.%_%g;s@%@\\%@')"
            __DASHBOARD_URL="https://tracie.freedesktop.org/dashboard/imagediff/${CI_PROJECT_PATH}/${CI_JOB_ID}/${__TRACE}"
            __START_TEST_PATTERN='<testcase classname="'"${__PIGLIT_TESTCASE_CLASSNAME}"'" name="'"${__PIGLIT_TESTCASE_NAME}"'" status="fail"'
            __REPLACE_TEST_PATTERN='</system-out><failure type="fail"/></testcase>'
            # Replace in the range between __START_TEST_PATTERN and
            # __REPLACE_TEST_PATTERN leaving __START_TEST_PATTERN out
            # from the substitution
            sed '\%'"${__START_TEST_PATTERN}"'%,\%'"${__REPLACE_TEST_PATTERN}"'%{\%'"${__START_TEST_PATTERN}"'%b;s%'"${__REPLACE_TEST_PATTERN}"'%</system-out><failure type="fail">To view the image differences visit: '"${__DASHBOARD_URL}"'</failure></testcase>%}' \
                -i "$RESULTS"/junit.xml
        fi

        ci-fairy minio cp "$RESULTS/$__PREFIX/$line" \
            "minio://${MINIO_HOST}${__MINIO_PATH}/${__DESTINATION_FILE_PATH}"
    done
}

SANITY_MESA_VERSION_CMD="$SANITY_MESA_VERSION_CMD | tee /tmp/version.txt | grep \"Mesa $MESA_VERSION\(\s\|$\)\""

rm -rf results
cd /piglit

if [ -n "$USE_CASELIST" ]; then
    PIGLIT_TESTS=$(printf "%s" "$PIGLIT_TESTS")
    PIGLIT_GENTESTS="./piglit print-cmd $PIGLIT_TESTS $PIGLIT_PROFILES --format \"{name}\" > /tmp/case-list.txt"
    RUN_GENTESTS="export LD_LIBRARY_PATH=$__LD_LIBRARY_PATH; $PIGLIT_GENTESTS"

    eval $RUN_GENTESTS

    sed -ni $CI_NODE_INDEX~$CI_NODE_TOTAL"p" /tmp/case-list.txt

    PIGLIT_TESTS="--test-list /tmp/case-list.txt"
fi

PIGLIT_OPTIONS=$(printf "%s" "$PIGLIT_OPTIONS")

PIGLIT_TESTS=$(printf "%s" "$PIGLIT_TESTS")

PIGLIT_CMD="./piglit run --timeout 300 -j${FDO_CI_CONCURRENT:-4} $PIGLIT_OPTIONS $PIGLIT_TESTS $PIGLIT_PROFILES "$(/usr/bin/printf "%q" "$RESULTS")

RUN_CMD="export LD_LIBRARY_PATH=$__LD_LIBRARY_PATH; $SANITY_MESA_VERSION_CMD && $PIGLIT_CMD"

if [ "$RUN_CMD_WRAPPER" ]; then
    RUN_CMD="set +e; $RUN_CMD_WRAPPER "$(/usr/bin/printf "%q" "$RUN_CMD")"; set -e"
fi

FAILURE_MESSAGE=$(printf "%s" "Unexpected change in results:")

eval $RUN_CMD

if [ $? -ne 0 ]; then
    printf "%s\n" "Found $(cat /tmp/version.txt), expected $MESA_VERSION"
fi

ARTIFACTS_BASE_URL="https://${CI_PROJECT_ROOT_NAMESPACE}.${CI_PAGES_DOMAIN}/-/${CI_PROJECT_NAME}/-/jobs/${CI_JOB_ID}/artifacts"

if [ ${PIGLIT_JUNIT_RESULTS:-0} -eq 1 ]; then
    ./piglit summary aggregate "$RESULTS" -o junit.xml
    FAILURE_MESSAGE=$(printf "${FAILURE_MESSAGE}\n%s" "Check the JUnit report for failures at: ${ARTIFACTS_BASE_URL}/results/junit.xml")
fi

PIGLIT_RESULTS="${PIGLIT_RESULTS:-$PIGLIT_PROFILES}"
RESULTSFILE="$RESULTS/$PIGLIT_RESULTS.txt"
mkdir -p .gitlab-ci/piglit
./piglit summary console "$RESULTS"/results.json.bz2 \
    | tee ".gitlab-ci/piglit/$PIGLIT_RESULTS.txt.orig" \
    | head -n -1 | grep -v ": pass" \
    | sed '/^summary:/Q' \
    > $RESULTSFILE

if [ "x$PIGLIT_PROFILES" = "xreplay" ] \
       && [ ${PIGLIT_REPLAY_UPLOAD_TO_MINIO:-0} -eq 1 ]; then

    ci-fairy minio login $CI_JOB_JWT

    __PREFIX="trace/$PIGLIT_REPLAY_DEVICE_NAME"
    __MINIO_PATH="$PIGLIT_REPLAY_ARTIFACTS_BASE_URL"
    __MINIO_TRACES_PREFIX="traces"

    quiet replay_minio_upload_images
fi

if [ -n "$USE_CASELIST" ]; then
    # Just filter the expected results based on the tests that were actually
    # executed, and switch to the version with no summary
    cat ".gitlab-ci/piglit/$PIGLIT_RESULTS.txt.orig" | sed '/^summary:/Q' | rev \
        | cut -f2- -d: | rev | sed "s/$/:/g" > /tmp/executed.txt

    grep -F -f /tmp/executed.txt "$INSTALL/$PIGLIT_RESULTS.txt" \
       > ".gitlab-ci/piglit/$PIGLIT_RESULTS.txt.baseline" || true
else
    cp "$INSTALL/$PIGLIT_RESULTS.txt" \
       ".gitlab-ci/piglit/$PIGLIT_RESULTS.txt.baseline"
fi

if diff -q ".gitlab-ci/piglit/$PIGLIT_RESULTS.txt.baseline" $RESULTSFILE; then
    exit 0
fi

./piglit summary html --exclude-details=pass \
"$RESULTS"/summary "$RESULTS"/results.json.bz2

if [ "x$PIGLIT_PROFILES" = "xreplay" ]; then
find "$RESULTS"/summary -type f -name "*.html" -print0 \
        | xargs -0 sed -i 's%<img src="file://'"${RESULTS}"'.*-\([0-9a-f]*\)\.png%<img src="https://'"${MINIO_HOST}${PIGLIT_REPLAY_ARTIFACTS_BASE_URL}"'/traces/\1.png%g'
find "$RESULTS"/summary -type f -name "*.html" -print0 \
        | xargs -0 sed -i 's%<img src="file://%<img src="https://'"${MINIO_HOST}${PIGLIT_REPLAY_REFERENCE_IMAGES_BASE_URL}"'/%g'
fi

FAILURE_MESSAGE=$(printf "${FAILURE_MESSAGE}\n%s" "Check the HTML summary for problems at: ${ARTIFACTS_BASE_URL}/results/summary/problems.html")

quiet print_red printf "%s\n" "$FAILURE_MESSAGE"
quiet diff --color=always -u ".gitlab-ci/piglit/$PIGLIT_RESULTS.txt.baseline" $RESULTSFILE
exit 1
