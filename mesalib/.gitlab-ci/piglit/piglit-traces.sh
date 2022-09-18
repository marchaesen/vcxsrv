#!/bin/sh

set -ex

INSTALL=$(realpath -s "$PWD"/install)
MINIO_ARGS="--credentials=/tmp/.minio_credentials"

RESULTS=$(realpath -s "$PWD"/results)
mkdir -p "$RESULTS"

if [ "$PIGLIT_REPLAY_SUBCOMMAND" = "profile" ]; then
    # workaround for older Debian Bullseye libyaml 0.2.2
    sed -i "/^%YAML 1\.2$/d" "$PIGLIT_REPLAY_DESCRIPTION_FILE"

    yq -i -Y '. | del(.traces[][] | select(.label[0,1,2,3,4,5,6,7,8,9] == "no-perf"))' \
      "$PIGLIT_REPLAY_DESCRIPTION_FILE"  # label positions are a bit hack
fi

# WINE
PATH="/opt/wine-stable/bin/:$PATH" # WineHQ path
export WINEPREFIX="/dxvk-wine64" # hardcode DXVK for now

# Set environment for DXVK.
export DXVK_LOG_LEVEL="info"
export DXVK_LOG="$RESULTS/dxvk"
[ -d "$DXVK_LOG" ] || mkdir -pv "$DXVK_LOG"
export DXVK_STATE_CACHE=0


# Set up the driver environment.
# Modifiying here directly LD_LIBRARY_PATH may cause problems when
# using a command wrapper. Hence, we will just set it when running the
# command.
export __LD_LIBRARY_PATH="$LD_LIBRARY_PATH:$INSTALL/lib/"
export VK_ICD_FILENAMES="$INSTALL/share/vulkan/icd.d/${VK_DRIVER}_icd.${VK_CPU:-`uname -m`}.json"

# Sanity check to ensure that our environment is sufficient to make our tests
# run against the Mesa built by CI, rather than any installed distro version.
MESA_VERSION=$(head -1 "$INSTALL/VERSION" | sed 's/\./\\./g')

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

# Set environment for apitrace executable.
export PATH="/apitrace/build:$PATH"

export PIGLIT_REPLAY_WINE_BINARY=wine64
export PIGLIT_REPLAY_WINE_APITRACE_BINARY="/apitrace-msvc-win64/bin/apitrace.exe"
export PIGLIT_REPLAY_WINE_D3DRETRACE_BINARY="/apitrace-msvc-win64/bin/d3dretrace.exe"

# Our rootfs may not have "less", which apitrace uses during
# apitrace dump
export PAGER=cat

SANITY_MESA_VERSION_CMD="wflinfo"

HANG_DETECTION_CMD=""


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

# If the job is parallel at the  gitlab job level, will take the corresponding
# fraction of the caselist.
if [ -n "$CI_NODE_INDEX" ]; then
    USE_CASELIST=1
fi

replay_minio_upload_images() {
    find "$RESULTS/$__PREFIX" -type f -name "*.png" -printf "%P\n" \
        | while read -r line; do

        __TRACE="${line%-*-*}"
        if grep -q "^$__PREFIX/$__TRACE: pass$" ".gitlab-ci/piglit/$PIGLIT_RESULTS.txt.orig"; then
            if [ "x$CI_PROJECT_PATH" != "x$FDO_UPSTREAM_REPO" ]; then
                continue
            fi
            __MINIO_PATH="$PIGLIT_REPLAY_REFERENCE_IMAGES_BASE"
            __DESTINATION_FILE_PATH="${line##*-}"
            if wget -q --method=HEAD "https://${__MINIO_PATH}/${__DESTINATION_FILE_PATH}" 2>/dev/null; then
                continue
            fi
        else
            __MINIO_PATH="$JOB_ARTIFACTS_BASE"
            __DESTINATION_FILE_PATH="$__MINIO_TRACES_PREFIX/${line##*-}"
        fi

        ci-fairy minio cp $MINIO_ARGS "$RESULTS/$__PREFIX/$line" \
            "minio://${__MINIO_PATH}/${__DESTINATION_FILE_PATH}"
    done
}

SANITY_MESA_VERSION_CMD="$SANITY_MESA_VERSION_CMD | tee /tmp/version.txt | grep \"Mesa $MESA_VERSION\(\s\|$\)\""

if [ -d results ]; then
    cd results && rm -rf ..?* .[!.]* *
fi
cd /piglit

if [ -n "$USE_CASELIST" ]; then
    PIGLIT_TESTS=$(printf "%s" "$PIGLIT_TESTS")
    PIGLIT_GENTESTS="./piglit print-cmd $PIGLIT_TESTS replay --format \"{name}\" > /tmp/case-list.txt"
    RUN_GENTESTS="export LD_LIBRARY_PATH=$__LD_LIBRARY_PATH; $PIGLIT_GENTESTS"

    eval $RUN_GENTESTS

    sed -ni $CI_NODE_INDEX~$CI_NODE_TOTAL"p" /tmp/case-list.txt

    PIGLIT_TESTS="--test-list /tmp/case-list.txt"
fi

PIGLIT_OPTIONS=$(printf "%s" "$PIGLIT_OPTIONS")

PIGLIT_TESTS=$(printf "%s" "$PIGLIT_TESTS")

PIGLIT_CMD="./piglit run -l verbose --timeout 300 -j${FDO_CI_CONCURRENT:-4} $PIGLIT_OPTIONS $PIGLIT_TESTS replay "$(/usr/bin/printf "%q" "$RESULTS")

RUN_CMD="export LD_LIBRARY_PATH=$__LD_LIBRARY_PATH; $SANITY_MESA_VERSION_CMD && $HANG_DETECTION_CMD $PIGLIT_CMD"

if [ "$RUN_CMD_WRAPPER" ]; then
    RUN_CMD="set +e; $RUN_CMD_WRAPPER "$(/usr/bin/printf "%q" "$RUN_CMD")"; set -e"
fi

ci-fairy minio login $MINIO_ARGS --token-file "${CI_JOB_JWT_FILE}"

# The replayer doesn't do any size or checksum verification for the traces in
# the replayer db, so if we had to restart the system due to intermittent device
# errors (or tried to cache replayer-db between runs, which would be nice to
# have), you could get a corrupted local trace that would spuriously fail the
# run.
rm -rf replayer-db

eval $RUN_CMD

if [ $? -ne 0 ]; then
    printf "%s\n" "Found $(cat /tmp/version.txt), expected $MESA_VERSION"
fi

ARTIFACTS_BASE_URL="https://${CI_PROJECT_ROOT_NAMESPACE}.${CI_PAGES_DOMAIN}/-/${CI_PROJECT_NAME}/-/jobs/${CI_JOB_ID}/artifacts"

./piglit summary aggregate "$RESULTS" -o junit.xml

PIGLIT_RESULTS="${PIGLIT_RESULTS:-replay}"
RESULTSFILE="$RESULTS/$PIGLIT_RESULTS.txt"
mkdir -p .gitlab-ci/piglit
./piglit summary console "$RESULTS"/results.json.bz2 \
    | tee ".gitlab-ci/piglit/$PIGLIT_RESULTS.txt.orig" \
    | head -n -1 | grep -v ": pass" \
    | sed '/^summary:/Q' \
    > $RESULTSFILE

__PREFIX="trace/$PIGLIT_REPLAY_DEVICE_NAME"
__MINIO_PATH="$PIGLIT_REPLAY_ARTIFACTS_BASE_URL"
__MINIO_TRACES_PREFIX="traces"

if [ "$PIGLIT_REPLAY_SUBCOMMAND" != "profile" ]; then
    quiet replay_minio_upload_images
fi


if [ ! -s $RESULTSFILE ]; then
    exit 0
fi

./piglit summary html --exclude-details=pass \
"$RESULTS"/summary "$RESULTS"/results.json.bz2

find "$RESULTS"/summary -type f -name "*.html" -print0 \
        | xargs -0 sed -i 's%<img src="file://'"${RESULTS}"'.*-\([0-9a-f]*\)\.png%<img src="https://'"${JOB_ARTIFACTS_BASE}"'/traces/\1.png%g'
find "$RESULTS"/summary -type f -name "*.html" -print0 \
        | xargs -0 sed -i 's%<img src="file://%<img src="https://'"${PIGLIT_REPLAY_REFERENCE_IMAGES_BASE}"'/%g'

quiet print_red echo "Failures in traces:"
cat $RESULTSFILE
quiet print_red echo "Review the image changes and get the new checksums at: ${ARTIFACTS_BASE_URL}/results/summary/problems.html"
exit 1
