#!/bin/sh

set -ex

DEQP_WIDTH=${DEQP_WIDTH:-256}
DEQP_HEIGHT=${DEQP_HEIGHT:-256}
DEQP_CONFIG=${DEQP_CONFIG:-rgba8888d24s8ms0}
DEQP_VARIANT=${DEQP_VARIANT:-master}

DEQP_OPTIONS="$DEQP_OPTIONS --deqp-surface-width=$DEQP_WIDTH --deqp-surface-height=$DEQP_HEIGHT"
DEQP_OPTIONS="$DEQP_OPTIONS --deqp-surface-type=pbuffer"
DEQP_OPTIONS="$DEQP_OPTIONS --deqp-gl-config-name=$DEQP_CONFIG"
DEQP_OPTIONS="$DEQP_OPTIONS --deqp-visibility=hidden"

# deqp's shader cache (for vulkan) is not multiprocess safe for a common
# filename, see:
# https://gitlab.freedesktop.org/mesa/parallel-deqp-runner/-/merge_requests/13
DEQP_OPTIONS="$DEQP_OPTIONS --deqp-shadercache=disable"

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

INSTALL=`pwd`/install

# Set up the driver environment.
export LD_LIBRARY_PATH=`pwd`/install/lib/
export EGL_PLATFORM=surfaceless
export VK_ICD_FILENAMES=`pwd`/install/share/vulkan/icd.d/"$VK_DRIVER"_icd.`uname -m`.json

# the runner was failing to look for libkms in /usr/local/lib for some reason
# I never figured out.
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/lib

RESULTS=`pwd`/results
mkdir -p $RESULTS

# Generate test case list file.
if [ "$DEQP_VER" = "vk" ]; then
   cp /deqp/mustpass/vk-$DEQP_VARIANT.txt /tmp/case-list.txt
   DEQP=/deqp/external/vulkancts/modules/vulkan/deqp-vk
elif [ "$DEQP_VER" = "gles2" -o "$DEQP_VER" = "gles3" -o "$DEQP_VER" = "gles31" ]; then
   cp /deqp/mustpass/$DEQP_VER-$DEQP_VARIANT.txt /tmp/case-list.txt
   DEQP=/deqp/modules/$DEQP_VER/deqp-$DEQP_VER
   SUITE=dEQP
else
   cp /deqp/mustpass/$DEQP_VER-$DEQP_VARIANT.txt /tmp/case-list.txt
   DEQP=/deqp/external/openglcts/modules/glcts
   SUITE=KHR
fi

# If the job is parallel, take the corresponding fraction of the caselist.
# Note: N~M is a gnu sed extension to match every nth line (first line is #1).
if [ -n "$CI_NODE_INDEX" ]; then
   sed -ni $CI_NODE_INDEX~$CI_NODE_TOTAL"p" /tmp/case-list.txt
fi

if [ -n "$DEQP_CASELIST_FILTER" ]; then
    sed -ni "/$DEQP_CASELIST_FILTER/p" /tmp/case-list.txt
fi

if [ ! -s /tmp/case-list.txt ]; then
    echo "Caselist generation failed"
    exit 1
fi

if [ -n "$DEQP_EXPECTED_FAILS" ]; then
    XFAIL="--xfail-list $INSTALL/$DEQP_EXPECTED_FAILS"
fi

set +e

if [ -n "$DEQP_PARALLEL" ]; then
   JOB="--job $DEQP_PARALLEL"
elif [ -n "$FDO_CI_CONCURRENT" ]; then
   JOB="--job $FDO_CI_CONCURRENT"
else
   JOB="--job 4"
fi

run_cts() {
    deqp=$1
    caselist=$2
    output=$3
    deqp-runner \
        --deqp $deqp \
        --output $output \
        --caselist $caselist \
        --exclude-list $INSTALL/$DEQP_SKIPS \
        --compact-display false \
        $XFAIL \
        $JOB \
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

    # Pick the first QPA mentioning our testcase
    qpa=`grep -l "$start" $qpas | head -n 1`

    # If we found one, go extract just that testcase's contents from the QPA
    # to a new QPA, then do testlog-to-xml on that.
    if [ -n "$qpa" ]; then
        while IFS= read -r line; do
            if [ "$line" = "$start" ]; then
                dst="$testcase.qpa"
                echo "#beginSession" > $dst
                echo "$line" >> $dst
                while IFS= read -r line; do
                    if [ "$line" = "#endTestCaseResult" ]; then
                        echo "$line" >> $dst
                        echo "#endSession" >> $dst
                        /deqp/executor/testlog-to-xml $dst "$RESULTS/$testcase$DEQP_RUN_SUFFIX.xml"
                        # copy the stylesheets here so they only end up in artifacts
                        # if we have one or more result xml in artifacts
                        cp /deqp/testlog.css "$RESULTS/"
                        cp /deqp/testlog.xsl "$RESULTS/"
                        return 0
                    fi
                    echo "$line" >> $dst
                done
                return 1
            fi
        done < $qpa
    fi
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

parse_renderer() {
    RENDERER=`grep -A1 TestCaseResult.\*info.renderer $RESULTS/deqp-info.qpa | grep '<Text' | sed 's|.*<Text>||g' | sed 's|</Text>||g'`
    VERSION=`grep -A1 TestCaseResult.\*info.version $RESULTS/deqp-info.qpa | grep '<Text' | sed 's|.*<Text>||g' | sed 's|</Text>||g'`
    echo "Renderer: $RENDERER"
    echo "Version: $VERSION "

    if ! echo $RENDERER | grep -q $DEQP_EXPECTED_RENDERER; then
        echo "Expected GL_RENDERER $DEQP_EXPECTED_RENDERER"
        exit 1
    fi
}

check_renderer() {
    echo "Capturing renderer info for GLES driver sanity checks"
    # If you're having trouble loading your driver, uncommenting this may help
    # debug.
    # export EGL_LOG_LEVEL=debug
    VERSION=`echo $DEQP_VER | tr '[a-z]' '[A-Z]'`
    $DEQP $DEQP_OPTIONS --deqp-case=$SUITE-$VERSION.info.\* --deqp-log-filename=$RESULTS/deqp-info.qpa
    parse_renderer
}

check_vk_device_name() {
    echo "Capturing device info for VK driver sanity checks"
    $DEQP $DEQP_OPTIONS --deqp-case=dEQP-VK.info.device --deqp-log-filename=$RESULTS/deqp-info.qpa
    DEVICENAME=`grep deviceName $RESULTS/deqp-info.qpa | sed 's|deviceName: ||g'`
    echo "deviceName: $DEVICENAME"
    if [ -n "$DEQP_EXPECTED_RENDERER" -a "x$DEVICENAME" != "x$DEQP_EXPECTED_RENDERER" ]; then
        echo "Expected deviceName $DEQP_EXPECTED_RENDERER"
        exit 1
    fi
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
    GALLIVM_PERF="nopt,no_filter_hacks" \
    virgl_test_server $VTEST_ARGS >$RESULTS/vtest-log.txt 2>&1 &

    sleep 1
fi

if [ $DEQP_VER = vk ]; then
    quiet check_vk_device_name
else
    quiet check_renderer
fi

RESULTSFILE=$RESULTS/cts-runner-results$DEQP_RUN_SUFFIX.txt
UNEXPECTED_RESULTSFILE=$RESULTS/cts-runner-unexpected-results$DEQP_RUN_SUFFIX.txt
FLAKESFILE=$RESULTS/cts-runner-flakes$DEQP_RUN_SUFFIX.txt

run_cts $DEQP /tmp/case-list.txt $RESULTSFILE
DEQP_EXITCODE=$?

echo "System load: $(cut -d' ' -f1-3 < /proc/loadavg)"
echo "# of CPU cores: $(cat /proc/cpuinfo | grep processor | wc -l)"

# junit is disabled, because it overloads gitlab.freedesktop.org to parse it.
#quiet generate_junit $RESULTSFILE > $RESULTS/results.xml

if [ $DEQP_EXITCODE -ne 0 ]; then
    # preserve caselist files in case of failures:
    cp /tmp/deqp_runner.*.txt $RESULTS/
    egrep -v ",Pass|,Skip|,ExpectedFail" $RESULTSFILE > $UNEXPECTED_RESULTSFILE

    # deqp-runner's flake detection won't perfectly detect all flakes, so
    # allow the driver to list some known flakes that won't intermittently
    # fail people's pipelines (while still allowing them to run and be
    # reported to IRC in the usual flake detection path).  If we had some
    # fails listed (so this wasn't a total runner failure), then filter out
    # the known flakes and see if there are any issues left.
    if [ -n "$DEQP_FLAKES" -a -s $UNEXPECTED_RESULTSFILE ]; then
        set +x
        while read line; do
            line=`echo $line | sed 's|#.*||g'`
            if [ -n "$line" ]; then
                sed -i "/$line/d" $UNEXPECTED_RESULTSFILE
            fi
        done < $INSTALL/$DEQP_FLAKES
        set -x

        if [ ! -s $UNEXPECTED_RESULTSFILE ]; then
            exit 0
        fi
    fi

    if [ -z "$DEQP_NO_SAVE_RESULTS" ]; then
        echo "Some unexpected results found (see cts-runner-results.txt in artifacts for full results):"
        head -n 50 $UNEXPECTED_RESULTSFILE

        # Save the logs for up to the first 50 unexpected results:
        head -n 50 $UNEXPECTED_RESULTSFILE | quiet extract_xml_results /tmp/*.qpa
    else
        echo "Unexpected results found:"
        cat $UNEXPECTED_RESULTSFILE
    fi
else
    grep ",Flake" $RESULTSFILE > $FLAKESFILE

    count=`cat $FLAKESFILE | wc -l`
    if [ $count -gt 0 ]; then
        echo "Some flakes found (see cts-runner-flakes.txt in artifacts for full results):"
        head -n 50 $FLAKESFILE

        if [ -z "$DEQP_NO_SAVE_RESULTS" ]; then
            # Save the logs for up to the first 50 flakes:
            head -n 50 $FLAKESFILE | quiet extract_xml_results /tmp/*.qpa
        fi

        # Report the flakes to IRC channel for monitoring (if configured):
        quiet report_flakes $FLAKESFILE
    else
        # no flakes, so clean-up:
        rm $FLAKESFILE
    fi
fi

exit $DEQP_EXITCODE
