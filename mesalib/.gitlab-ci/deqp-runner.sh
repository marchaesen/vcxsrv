#!/bin/sh

set -ex

DEQP_OPTIONS="--deqp-surface-width=256 --deqp-surface-height=256"
DEQP_OPTIONS="$DEQP_OPTIONS --deqp-surface-type=pbuffer"
DEQP_OPTIONS="$DEQP_OPTIONS --deqp-gl-config-name=rgba8888d24s8ms0"
DEQP_OPTIONS="$DEQP_OPTIONS --deqp-visibility=hidden"

# It would be nice to be able to enable the watchdog, so that hangs in a test
# don't need to wait the full hour for the run to time out.  However, some
# shaders end up taking long enough to compile
# (dEQP-GLES31.functional.ubo.random.all_per_block_buffers.20 for example)
# that they'll sporadically trigger the watchdog.
#DEQP_OPTIONS="$DEQP_OPTIONS --deqp-watchdog=enable"

if [ -z "$DEQP_VER" ]; then
   echo 'DEQP_VER must be set to something like "gles2", "gles31" or "vk" for the test run'
   exit 1
fi

if [ "$DEQP_VER" = "vk" ]; then
   if [ -z "$VK_DRIVER" ]; then
      echo 'VK_DRIVER must be to something like "radeon" or "intel" for the test run'
      exit 1
   fi
fi

if [ -z "$DEQP_SKIPS" ]; then
   echo 'DEQP_SKIPS must be set to something like "deqp-default-skips.txt"'
   exit 1
fi

ARTIFACTS=`pwd`/artifacts

# Set up the driver environment.
export LD_LIBRARY_PATH=`pwd`/install/lib/
export EGL_PLATFORM=surfaceless
export VK_ICD_FILENAMES=`pwd`/install/share/vulkan/icd.d/"$VK_DRIVER"_icd.x86_64.json

# the runner was failing to look for libkms in /usr/local/lib for some reason
# I never figured out.
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/lib

RESULTS=`pwd`/results
mkdir -p $RESULTS

# Generate test case list file.
if [ "$DEQP_VER" = "vk" ]; then
   cp /deqp/mustpass/vk-master.txt /tmp/case-list.txt
   DEQP=/deqp/external/vulkancts/modules/vulkan/deqp-vk
else
   cp /deqp/mustpass/$DEQP_VER-master.txt /tmp/case-list.txt
   DEQP=/deqp/modules/$DEQP_VER/deqp-$DEQP_VER
fi

# If the job is parallel, take the corresponding fraction of the caselist.
# Note: N~M is a gnu sed extension to match every nth line (first line is #1).
if [ -n "$CI_NODE_INDEX" ]; then
   sed -ni $CI_NODE_INDEX~$CI_NODE_TOTAL"p" /tmp/case-list.txt
fi

if [ ! -s /tmp/case-list.txt ]; then
    echo "Caselist generation failed"
    exit 1
fi

if [ -n "$DEQP_EXPECTED_FAILS" ]; then
    XFAIL="--xfail-list $ARTIFACTS/$DEQP_EXPECTED_FAILS"
fi

set +e

run_cts() {
    deqp=$1
    caselist=$2
    output=$3
    deqp-runner \
        --deqp $deqp \
        --output $output \
        --caselist $caselist \
        --exclude-list $ARTIFACTS/$DEQP_SKIPS \
        $XFAIL \
        --job ${DEQP_PARALLEL:-1} \
	--allow-flakes true \
	$DEQP_RUNNER_OPTIONS \
        -- \
        $DEQP_OPTIONS
}

report_flakes() {
    if [ -z "$FLAKES_CHANNEL" ]; then
        return 0
    fi
    flakes=$1
    bot="$CI_RUNNER_DESCRIPTION-$CI_PIPELINE_ID"
    channel="$FLAKES_CHANNEL"
    (
    echo NICK $bot
    echo USER $bot unused unused :Gitlab CI Notifier
    sleep 10
    echo "JOIN $channel"
    sleep 1
    desc="Flakes detected in job: $CI_JOB_URL on $CI_RUNNER_DESCRIPTION"
    if [ -n "CI_MERGE_REQUEST_SOURCE_BRANCH_NAME" ]; then
        desc="$desc on branch $CI_MERGE_REQUEST_SOURCE_BRANCH_NAME ($CI_MERGE_REQUEST_TITLE)"
    fi
    echo "PRIVMSG $channel :$desc"
    for flake in `cat $flakes`; do
        echo "PRIVMSG $channel :$flake"
    done
    echo "PRIVMSG $channel :See $CI_JOB_URL/artifacts/browse/results/"
    echo "QUIT"
    ) | nc irc.freenode.net 6667 > /dev/null

}

extract_xml_result() {
    testcase=$1
    shift 1
    qpas=$*
    start="#beginTestCaseResult $testcase"
    for qpa in $qpas; do
        while IFS= read -r line; do
            if [ "$line" = "$start" ]; then
                dst="$testcase.qpa"
                echo "#beginSession" > $dst
                echo $line >> $dst
                while IFS= read -r line; do
                    if [ "$line" = "#endTestCaseResult" ]; then
                        echo $line >> $dst
                        echo "#endSession" >> $dst
                        /deqp/executor/testlog-to-xml $dst "$RESULTS/$testcase.xml"
                        # copy the stylesheets here so they only end up in artifacts
                        # if we have one or more result xml in artifacts
                        cp /deqp/testlog.css "$RESULTS/"
                        cp /deqp/testlog.xsl "$RESULTS/"
                        return 0
                    fi
                    echo $line >> $dst
                done
                return 1
            fi
        done < $qpa
    done
}

extract_xml_results() {
    qpas=$*
    while IFS= read -r testcase; do
        testcase=${testcase%,*}
        extract_xml_result $testcase $qpas
    done
}

# Generate junit results
generate_junit() {
    results=$1
    echo "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
    echo "<testsuites>"
    echo "<testsuite name=\"$DEQP_VER-$CI_NODE_INDEX\">"
    while read line; do
        testcase=${line%,*}
        result=${line#*,}
        # avoid counting Skip's in the # of tests:
        if [ "$result" = "Skip" ]; then
            continue;
        fi
        echo "<testcase name=\"$testcase\">"
        if [ "$result" != "Pass" ]; then
            echo "<failure type=\"$result\">"
            echo "$result: See $CI_JOB_URL/artifacts/results/$testcase.xml"
            echo "</failure>"
        fi
        echo "</testcase>"
    done < $results
    echo "</testsuite>"
    echo "</testsuites>"
}

# wrapper to supress +x to avoid spamming the log
quiet() {
    set +x
    "$@"
    set -x
}

run_cts $DEQP /tmp/case-list.txt $RESULTS/cts-runner-results.txt
DEQP_EXITCODE=$?

quiet generate_junit $RESULTS/cts-runner-results.txt > $RESULTS/results.xml

if [ $DEQP_EXITCODE -ne 0 ]; then
    # preserve caselist files in case of failures:
    cp /tmp/deqp_runner.*.txt $RESULTS/
    echo "Some unexpected results found (see cts-runner-results.txt in artifacts for full results):"
    cat $RESULTS/cts-runner-results.txt | \
        grep -v ",Pass" | \
        grep -v ",Skip" | \
        grep -v ",ExpectedFail" > \
        $RESULTS/cts-runner-unexpected-results.txt
    head -n 50 $RESULTS/cts-runner-unexpected-results.txt

    if [ -z "$DEQP_NO_SAVE_RESULTS" ]; then
        # Save the logs for up to the first 50 unexpected results:
        head -n 50 $RESULTS/cts-runner-unexpected-results.txt | quiet extract_xml_results /tmp/*.qpa
    fi

    count=`cat $RESULTS/cts-runner-unexpected-results.txt | wc -l`

    # Re-run fails to detect flakes.  But use a small threshold, if
    # something was fundamentally broken, we don't want to re-run
    # the entire caselist
else
    cat $RESULTS/cts-runner-results.txt | \
        grep ",Flake" > \
        $RESULTS/cts-runner-flakes.txt

    count=`cat $RESULTS/cts-runner-flakes.txt | wc -l`
    if [ $count -gt 0 ]; then
        echo "Some flakes found (see cts-runner-flakes.txt in artifacts for full results):"
        head -n 50 $RESULTS/cts-runner-flakes.txt

        if [ -z "$DEQP_NO_SAVE_RESULTS" ]; then
            # Save the logs for up to the first 50 flakes:
            head -n 50 $RESULTS/cts-runner-flakes.txt | quiet extract_xml_results /tmp/*.qpa
        fi

        # Report the flakes to IRC channel for monitoring (if configured):
        quiet report_flakes $RESULTS/cts-runner-flakes.txt
    else
        # no flakes, so clean-up:
        rm $RESULTS/cts-runner-flakes.txt
    fi
fi

exit $DEQP_EXITCODE
