#!/bin/bash

set -ex

DEQP_OPTIONS=(--deqp-surface-width=256 --deqp-surface-height=256)
DEQP_OPTIONS+=(--deqp-surface-type=pbuffer)
DEQP_OPTIONS+=(--deqp-gl-config-name=rgba8888d24s8ms0)
DEQP_OPTIONS+=(--deqp-visibility=hidden)
DEQP_OPTIONS+=(--deqp-log-images=disable)
DEQP_OPTIONS+=(--deqp-crashhandler=enable)

# It would be nice to be able to enable the watchdog, so that hangs in a test
# don't need to wait the full hour for the run to time out.  However, some
# shaders end up taking long enough to compile
# (dEQP-GLES31.functional.ubo.random.all_per_block_buffers.20 for example)
# that they'll sporadically trigger the watchdog.
#DEQP_OPTIONS+=(--deqp-watchdog=enable)

if [ -z "$DEQP_VER" ]; then
   echo 'DEQP_VER must be set to something like "gles2" or "gles31" for the test run'
   exit 1
fi

if [ -z "$DEQP_SKIPS" ]; then
   echo 'DEQP_SKIPS must be set to something like "deqp-default-skips.txt"'
   exit 1
fi

# Prep the expected failure list
if [ -n "$DEQP_EXPECTED_FAILS" ]; then
   export DEQP_EXPECTED_FAILS=`pwd`/artifacts/$DEQP_EXPECTED_FAILS
else
   export DEQP_EXPECTED_FAILS=/tmp/expect-no-failures.txt
   touch $DEQP_EXPECTED_FAILS
fi
sort < $DEQP_EXPECTED_FAILS > /tmp/expected-fails.txt

# Fix relative paths on inputs.
export DEQP_SKIPS=`pwd`/artifacts/$DEQP_SKIPS

# Be a good citizen on the shared runners.
export LP_NUM_THREADS=4

# Set up the driver environment.
export LD_LIBRARY_PATH=`pwd`/install/lib/
export EGL_PLATFORM=surfaceless

# the runner was failing to look for libkms in /usr/local/lib for some reason
# I never figured out.
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/lib

RESULTS=`pwd`/results
mkdir -p $RESULTS

cd /deqp/modules/$DEQP_VER

# Generate test case list file
cp /deqp/mustpass/$DEQP_VER-master.txt /tmp/case-list.txt

# Note: not using sorted input and comm, becuase I want to run the tests in
# the same order that dEQP would.
while read -r line; do
   if echo "$line" | grep -q '^[^#]'; then
       sed -i "/$line/d" /tmp/case-list.txt
   fi
done < $DEQP_SKIPS

# If the job is parallel, take the corresponding fraction of the caselist.
# Note: N~M is a gnu sed extension to match every nth line (first line is #1).
if [ -n "$CI_NODE_INDEX" ]; then
   sed -ni $CI_NODE_INDEX~$CI_NODE_TOTAL"p" /tmp/case-list.txt
fi

if [ ! -s /tmp/case-list.txt ]; then
    echo "Caselist generation failed"
    exit 1
fi

# Cannot use tee because dash doesn't have pipefail
touch /tmp/result.txt
tail -f /tmp/result.txt &

./deqp-$DEQP_VER "${DEQP_OPTIONS[@]}" --deqp-log-filename=$RESULTS/results.qpa --deqp-caselist-file=/tmp/case-list.txt >> /tmp/result.txt
DEQP_EXITCODE=$?

sed -ne \
    '/StatusCode="Fail"/{x;p}; s/#beginTestCaseResult //; T; h' \
    $RESULTS/results.qpa \
    > /tmp/unsorted-fails.txt

# Scrape out the renderer that the test run used, so we can validate that the
# right driver was used.
if grep -q "dEQP-.*.info.renderer" /tmp/case-list.txt; then
    # This is an ugly dependency on the .qpa format: Print 3 lines after the
    # match, which happens to contain the result.
    RENDERER=`sed -n '/#beginTestCaseResult dEQP-.*.info.renderer/{n;n;n;p}' $RESULTS/results.qpa | sed -n -E "s|<Text>(.*)</Text>|\1|p"`

    echo "GL_RENDERER for this test run: $RENDERER"

    if [ -n "$DEQP_RENDERER_MATCH" ]; then
        echo $RENDERER | grep -q $DEQP_RENDERER_MATCH > /dev/null
    fi
fi

if grep -q "dEQP-.*.info.version" /tmp/case-list.txt; then
    # This is an ugly dependency on the .qpa format: Print 3 lines after the
    # match, which happens to contain the result.
    VERSION=`sed -n '/#beginTestCaseResult dEQP-.*.info.version/{n;n;n;p}' $RESULTS/results.qpa | sed -n -E "s|<Text>(.*)</Text>|\1|p"`
    echo "Driver version tested: $VERSION"
fi

if [ $DEQP_EXITCODE -ne 0 ]; then
   exit $DEQP_EXITCODE
fi

sort < /tmp/unsorted-fails.txt > $RESULTS/fails.txt

comm -23 $RESULTS/fails.txt /tmp/expected-fails.txt > /tmp/new-fails.txt
if [ -s /tmp/new-fails.txt ]; then
    echo "Unexpected failures:"
    cat /tmp/new-fails.txt
    exit 1
else
    echo "No new failures"
fi

sort /tmp/case-list.txt > /tmp/sorted-case-list.txt
comm -12 /tmp/sorted-case-list.txt /tmp/expected-fails.txt > /tmp/expected-fails-in-caselist.txt
comm -13 $RESULTS/fails.txt /tmp/expected-fails-in-caselist.txt > /tmp/new-passes.txt
if [ -s /tmp/new-passes.txt ]; then
    echo "Unexpected passes, please update $DEQP_EXPECTED_FAILS (or add flaky tests to $DEQP_SKIPS):"
    cat /tmp/new-passes.txt
    exit 1
else
    echo "No new passes"
fi
