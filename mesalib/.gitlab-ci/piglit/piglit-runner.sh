#!/bin/sh

set -ex

if [ -z "$GPU_VERSION" ]; then
   echo 'GPU_VERSION must be set to something like "llvmpipe" or "freedreno-a630" (the name used in your ci/gpu-version-*.txt)'
   exit 1
fi

INSTALL=`pwd`/install

# Set up the driver environment.
export LD_LIBRARY_PATH=`pwd`/install/lib/
export EGL_PLATFORM=surfaceless
export VK_ICD_FILENAMES=`pwd`/install/share/vulkan/icd.d/"$VK_DRIVER"_icd.${VK_CPU:-`uname -m`}.json

RESULTS=`pwd`/${PIGLIT_RESULTS_DIR:-results}
mkdir -p $RESULTS

if [ -n "$PIGLIT_FRACTION" -o -n "$CI_NODE_INDEX" ]; then
   FRACTION=`expr ${PIGLIT_FRACTION:-1} \* ${CI_NODE_TOTAL:-1}`
PIGLIT_RUNNER_OPTIONS="$PIGLIT_RUNNER_OPTIONS --fraction $FRACTION"
fi

# If the job is parallel at the gitab job level, take the corresponding fraction
# of the caselist.
if [ -n "$CI_NODE_INDEX" ]; then
   PIGLIT_RUNNER_OPTIONS="$PIGLIT_RUNNER_OPTIONS --fraction-start ${CI_NODE_INDEX}"
fi

if [ -e "$INSTALL/$GPU_VERSION-fails.txt" ]; then
    PIGLIT_RUNNER_OPTIONS="$PIGLIT_RUNNER_OPTIONS --baseline $INSTALL/$GPU_VERSION-fails.txt"
fi

# Default to an empty known flakes file if it doesn't exist.
touch $INSTALL/$GPU_VERSION-flakes.txt

if [ -n "$VK_DRIVER" ] && [ -e "$INSTALL/$VK_DRIVER-skips.txt" ]; then
    PIGLIT_SKIPS="$PIGLIT_SKIPS $INSTALL/$VK_DRIVER-skips.txt"
fi

if [ -n "$GALLIUM_DRIVER" ] && [ -e "$INSTALL/$GALLIUM_DRIVER-skips.txt" ]; then
    PIGLIT_SKIPS="$PIGLIT_SKIPS $INSTALL/$GALLIUM_DRIVER-skips.txt"
fi

if [ -n "$DRIVER_NAME" ] && [ -e "$INSTALL/$DRIVER_NAME-skips.txt" ]; then
    PIGLIT_SKIPS="$PIGLIT_SKIPS $INSTALL/$DRIVER_NAME-skips.txt"
fi

if [ -e "$INSTALL/$GPU_VERSION-skips.txt" ]; then
    PIGLIT_SKIPS="$PIGLIT_SKIPS $INSTALL/$GPU_VERSION-skips.txt"
fi

set +e

piglit-runner \
    run \
    --piglit-folder /piglit \
    --output $RESULTS \
    --jobs ${FDO_CI_CONCURRENT:-4} \
    --skips $INSTALL/all-skips.txt $PIGLIT_SKIPS \
    --flakes $INSTALL/$GPU_VERSION-flakes.txt \
    --profile $PIGLIT_PROFILES \
    --process-isolation \
    $PIGLIT_RUNNER_OPTIONS \
    -v -v

PIGLIT_EXITCODE=$?

deqp-runner junit \
   --testsuite $PIGLIT_PROFILES \
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

exit $PIGLIT_EXITCODE
