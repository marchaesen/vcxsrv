#!/bin/sh

set -ex

if [ -z "$GPU_VERSION" ]; then
   echo 'GPU_VERSION must be set to something like "llvmpipe" or "freedreno-a630" (the name used in your ci/piglit-gpu-version-*.txt)'
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

if [ -e "$INSTALL/piglit-$GPU_VERSION-fails.txt" ]; then
    PIGLIT_RUNNER_OPTIONS="$PIGLIT_RUNNER_OPTIONS --baseline $INSTALL/piglit-$GPU_VERSION-fails.txt"
fi

if [ -e "$INSTALL/piglit-$GPU_VERSION-flakes.txt" ]; then
    PIGLIT_RUNNER_OPTIONS="$PIGLIT_RUNNER_OPTIONS --flakes $INSTALL/piglit-$GPU_VERSION-flakes.txt"
fi

if [ -e "$INSTALL/piglit-$GPU_VERSION-skips.txt" ]; then
    PIGLIT_RUNNER_OPTIONS="$PIGLIT_RUNNER_OPTIONS --skips $INSTALL/piglit-$GPU_VERSION-skips.txt"
fi

set +e

if [ -n "$PIGLIT_PARALLEL" ]; then
   PIGLIT_RUNNER_OPTIONS="$PIGLIT_RUNNER_OPTIONS --jobs $PIGLIT_PARALLEL"
elif [ -n "$FDO_CI_CONCURRENT" ]; then
   PIGLIT_RUNNER_OPTIONS="$PIGLIT_RUNNER_OPTIONS --jobs $FDO_CI_CONCURRENT"
else
   PIGLIT_RUNNER_OPTIONS="$PIGLIT_RUNNER_OPTIONS --jobs 4"
fi

report_flakes() {
    # Replace spaces in test names with _ to make the channel reporting not
    # split it across lines, even though it makes it so you can't copy and
    # paste from IRC into your flakes list.
    flakes=`grep ",Flake" $1 | sed 's|,Flake.*||g' | sed 's| |_|g'`
    if [ -z "$flakes" ]; then
        return 0
    fi

    if [ -z "$FLAKES_CHANNEL" ]; then
        return 0
    fi

    # The nick needs to be something unique so that multiple runners
    # connecting at the same time don't race for one nick and get blocked.
    # freenode has a 16-char limit on nicks (9 is the IETF standard, but
    # various servers extend that).  So, trim off the common prefixes of the
    # runner name, and append the job ID so that software runners with more
    # than one concurrent job (think swrast) don't collide.  For freedreno,
    # that gives us a nick as long as db410c-N-JJJJJJJJ, and it'll be a while
    # before we make it to 9-digit jobs (we're at 7 so far).
    runner=`echo $CI_RUNNER_DESCRIPTION | sed 's|mesa-||' | sed 's|google-freedreno-||g'`
    bot="$runner-$CI_JOB_ID"
    channel="$FLAKES_CHANNEL"
    (
    echo NICK $bot
    echo USER $bot unused unused :Gitlab CI Notifier
    sleep 10
    echo "JOIN $channel"
    sleep 1
    desc="Flakes detected in job: $CI_JOB_URL on $CI_RUNNER_DESCRIPTION"
    if [ -n "$CI_MERGE_REQUEST_SOURCE_BRANCH_NAME" ]; then
        desc="$desc on branch $CI_MERGE_REQUEST_SOURCE_BRANCH_NAME ($CI_MERGE_REQUEST_TITLE)"
    elif [ -n "$CI_COMMIT_BRANCH" ]; then
        desc="$desc on branch $CI_COMMIT_BRANCH ($CI_COMMIT_TITLE)"
    fi
    echo "PRIVMSG $channel :$desc"
    for flake in $flakes; do
        echo "PRIVMSG $channel :$flake"
    done
    echo "PRIVMSG $channel :See $CI_JOB_URL/artifacts/browse/results/"
    echo "QUIT"
    ) | nc irc.freenode.net 6667 > /dev/null

}

# wrapper to supress +x to avoid spamming the log
quiet() {
    set +x
    "$@"
    set -x
}

RESULTS_CSV=$RESULTS/results.csv
FAILURES_CSV=$RESULTS/failures.csv

export LD_PRELOAD=$TEST_LD_PRELOAD

    piglit-runner \
        run \
        --piglit-folder /piglit \
        --output $RESULTS \
        --profile $PIGLIT_PROFILES \
        --process-isolation \
	$PIGLIT_RUNNER_OPTIONS \
        -v -v

PIGLIT_EXITCODE=$?

export LD_PRELOAD=

deqp-runner junit \
   --testsuite $PIGLIT_PROFILES \
   --results $RESULTS/failures.csv \
   --output $RESULTS/junit.xml \
   --limit 50 \
   --template "See https://$CI_PROJECT_ROOT_NAMESPACE.pages.freedesktop.org/-/$CI_PROJECT_NAME/-/jobs/$CI_JOB_ID/artifacts/results/{{testcase}}.xml"

# Report the flakes to the IRC channel for monitoring (if configured):
quiet report_flakes $RESULTS_CSV

exit $PIGLIT_EXITCODE
