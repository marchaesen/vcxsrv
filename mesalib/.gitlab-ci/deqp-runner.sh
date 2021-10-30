#!/bin/sh

set -ex

if [ -z "$GPU_VERSION" ]; then
   echo 'GPU_VERSION must be set to something like "llvmpipe" or "freedreno-a630" (the name used in .gitlab-ci/gpu-version-*.txt)'
   exit 1
fi

INSTALL=`pwd`/install

# Set up the driver environment.
export LD_LIBRARY_PATH=`pwd`/install/lib/
export EGL_PLATFORM=surfaceless
export VK_ICD_FILENAMES=`pwd`/install/share/vulkan/icd.d/"$VK_DRIVER"_icd.${VK_CPU:-`uname -m`}.json

RESULTS=`pwd`/${DEQP_RESULTS_DIR:-results}
mkdir -p $RESULTS

HANG_DETECTION_CMD=""

if [ -z "$DEQP_SUITE" ]; then
    if [ -z "$DEQP_VER" ]; then
        echo 'DEQP_SUITE must be set to the name of your deqp-gpu_version.toml, or DEQP_VER must be set to something like "gles2", "gles31-khr" or "vk" for the test run'
        exit 1
    fi

    DEQP_WIDTH=${DEQP_WIDTH:-256}
    DEQP_HEIGHT=${DEQP_HEIGHT:-256}
    DEQP_CONFIG=${DEQP_CONFIG:-rgba8888d24s8ms0}
    DEQP_VARIANT=${DEQP_VARIANT:-master}

    DEQP_OPTIONS="$DEQP_OPTIONS --deqp-surface-width=$DEQP_WIDTH --deqp-surface-height=$DEQP_HEIGHT"
    DEQP_OPTIONS="$DEQP_OPTIONS --deqp-surface-type=${DEQP_SURFACE_TYPE:-pbuffer}"
    DEQP_OPTIONS="$DEQP_OPTIONS --deqp-gl-config-name=$DEQP_CONFIG"
    DEQP_OPTIONS="$DEQP_OPTIONS --deqp-visibility=hidden"

    if [ "$DEQP_VER" = "vk" -a -z "$VK_DRIVER" ]; then
        echo 'VK_DRIVER must be to something like "radeon" or "intel" for the test run'
        exit 1
    fi

    # Generate test case list file.
    if [ "$DEQP_VER" = "vk" ]; then
       MUSTPASS=/deqp/mustpass/vk-$DEQP_VARIANT.txt
       DEQP=/deqp/external/vulkancts/modules/vulkan/deqp-vk
       HANG_DETECTION_CMD="/parallel-deqp-runner/build/bin/hang-detection"
    elif [ "$DEQP_VER" = "gles2" -o "$DEQP_VER" = "gles3" -o "$DEQP_VER" = "gles31" -o "$DEQP_VER" = "egl" ]; then
       MUSTPASS=/deqp/mustpass/$DEQP_VER-$DEQP_VARIANT.txt
       DEQP=/deqp/modules/$DEQP_VER/deqp-$DEQP_VER
    elif [ "$DEQP_VER" = "gles2-khr" -o "$DEQP_VER" = "gles3-khr" -o "$DEQP_VER" = "gles31-khr" -o "$DEQP_VER" = "gles32-khr" ]; then
       MUSTPASS=/deqp/mustpass/$DEQP_VER-$DEQP_VARIANT.txt
       DEQP=/deqp/external/openglcts/modules/glcts
    else
       MUSTPASS=/deqp/mustpass/$DEQP_VER-$DEQP_VARIANT.txt
       DEQP=/deqp/external/openglcts/modules/glcts
    fi

    cp $MUSTPASS /tmp/case-list.txt

    # If the caselist is too long to run in a reasonable amount of time, let the job
    # specify what fraction (1/n) of the caselist we should run.  Note: N~M is a gnu
    # sed extension to match every nth line (first line is #1).
    if [ -n "$DEQP_FRACTION" ]; then
       sed -ni 1~$DEQP_FRACTION"p" /tmp/case-list.txt
    fi

    # If the job is parallel at the gitab job level, take the corresponding fraction
    # of the caselist.
    if [ -n "$CI_NODE_INDEX" ]; then
       sed -ni $CI_NODE_INDEX~$CI_NODE_TOTAL"p" /tmp/case-list.txt
    fi

    if [ -n "$DEQP_CASELIST_FILTER" ]; then
        sed -ni "/$DEQP_CASELIST_FILTER/p" /tmp/case-list.txt
    fi

    if [ -n "$DEQP_CASELIST_INV_FILTER" ]; then
        sed -ni "/$DEQP_CASELIST_INV_FILTER/!p" /tmp/case-list.txt
    fi

    if [ ! -s /tmp/case-list.txt ]; then
        echo "Caselist generation failed"
        exit 1
    fi
fi

if [ -e "$INSTALL/$GPU_VERSION-fails.txt" ]; then
    DEQP_RUNNER_OPTIONS="$DEQP_RUNNER_OPTIONS --baseline $INSTALL/$GPU_VERSION-fails.txt"
fi

# Default to an empty known flakes file if it doesn't exist.
touch $INSTALL/$GPU_VERSION-flakes.txt


if [ -n "$VK_DRIVER" ] && [ -e "$INSTALL/$VK_DRIVER-skips.txt" ]; then
    DEQP_SKIPS="$DEQP_SKIPS $INSTALL/$VK_DRIVER-skips.txt"
fi

if [ -n "$GALLIUM_DRIVER" ] && [ -e "$INSTALL/$GALLIUM_DRIVER-skips.txt" ]; then
    DEQP_SKIPS="$DEQP_SKIPS $INSTALL/$GALLIUM_DRIVER-skips.txt"
fi

if [ -n "$DRIVER_NAME" ] && [ -e "$INSTALL/$DRIVER_NAME-skips.txt" ]; then
    DEQP_SKIPS="$DEQP_SKIPS $INSTALL/$DRIVER_NAME-skips.txt"
fi

if [ -e "$INSTALL/$GPU_VERSION-skips.txt" ]; then
    DEQP_SKIPS="$DEQP_SKIPS $INSTALL/$GPU_VERSION-skips.txt"
fi

set +e

report_load() {
    echo "System load: $(cut -d' ' -f1-3 < /proc/loadavg)"
    echo "# of CPU cores: $(cat /proc/cpuinfo | grep processor | wc -l)"
}

# wrapper to supress +x to avoid spamming the log
quiet() {
    set +x
    "$@"
    set -x
}

if [ "$GALLIUM_DRIVER" = "virpipe" ]; then
    # deqp is to use virpipe, and virgl_test_server llvmpipe
    export GALLIUM_DRIVER="$GALLIUM_DRIVER"

    VTEST_ARGS="--use-egl-surfaceless"
    if [ "$VIRGL_HOST_API" = "GLES" ]; then
        VTEST_ARGS="$VTEST_ARGS --use-gles"
    fi

    GALLIUM_DRIVER=llvmpipe \
    GALLIVM_PERF="nopt" \
    virgl_test_server $VTEST_ARGS >$RESULTS/vtest-log.txt 2>&1 &

    sleep 1
fi

if [ -z "$DEQP_SUITE" ]; then
    if [ -n "$DEQP_EXPECTED_RENDERER" ]; then
        export DEQP_RUNNER_OPTIONS="$DEQP_RUNNER_OPTIONS --renderer-check "$DEQP_EXPECTED_RENDERER""
    fi
    if [ $DEQP_VER != vk -a $DEQP_VER != egl ]; then
        export DEQP_RUNNER_OPTIONS="$DEQP_RUNNER_OPTIONS --version-check `cat $INSTALL/VERSION | sed 's/[() ]/./g'`"
    fi

    deqp-runner \
        run \
        --deqp $DEQP \
        --output $RESULTS \
        --caselist /tmp/case-list.txt \
        --skips $INSTALL/all-skips.txt $DEQP_SKIPS \
        --flakes $INSTALL/$GPU_VERSION-flakes.txt \
        --testlog-to-xml /deqp/executor/testlog-to-xml \
        --jobs ${FDO_CI_CONCURRENT:-4} \
	$DEQP_RUNNER_OPTIONS \
        -- \
        $DEQP_OPTIONS
else
    deqp-runner \
        suite \
        --suite $INSTALL/deqp-$DEQP_SUITE.toml \
        --output $RESULTS \
        --skips $INSTALL/all-skips.txt $DEQP_SKIPS \
        --flakes $INSTALL/$GPU_VERSION-flakes.txt \
        --testlog-to-xml /deqp/executor/testlog-to-xml \
        --fraction-start $CI_NODE_INDEX \
        --fraction $CI_NODE_TOTAL \
        --jobs ${FDO_CI_CONCURRENT:-4} \
	$DEQP_RUNNER_OPTIONS
fi

DEQP_EXITCODE=$?

quiet report_load

# Remove all but the first 50 individual XML files uploaded as artifacts, to
# save fd.o space when you break everything.
find $RESULTS -name \*.xml | \
    sort -n |
    sed -n '1,+49!p' | \
    xargs rm -f

# If any QPA XMLs are there, then include the XSL/CSS in our artifacts.
find $RESULTS -name \*.xml \
    -exec cp /deqp/testlog.css /deqp/testlog.xsl "$RESULTS/" ";" \
    -quit

deqp-runner junit \
   --testsuite dEQP \
   --results $RESULTS/failures.csv \
   --output $RESULTS/junit.xml \
   --limit 50 \
   --template "See https://$CI_PROJECT_ROOT_NAMESPACE.pages.freedesktop.org/-/$CI_PROJECT_NAME/-/jobs/$CI_JOB_ID/artifacts/results/{{testcase}}.xml"

# Report the flakes to the IRC channel for monitoring (if configured):
if [ -n "$FLAKES_CHANNEL" ]; then
  python3 $INSTALL/report-flakes.py \
         --host irc.oftc.net \
         --port 6667 \
         --results $RESULTS/results.csv \
         --known-flakes $INSTALL/$GPU_VERSION-flakes.txt \
         --channel "$FLAKES_CHANNEL" \
         --runner "$CI_RUNNER_DESCRIPTION" \
         --job "$CI_JOB_ID" \
         --url "$CI_JOB_URL" \
         --branch "${CI_MERGE_REQUEST_SOURCE_BRANCH_NAME:-$CI_COMMIT_BRANCH}" \
         --branch-title "${CI_MERGE_REQUEST_TITLE:-$CI_COMMIT_TITLE}"
fi

exit $DEQP_EXITCODE
