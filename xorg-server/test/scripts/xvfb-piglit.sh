#!/bin/sh

set -e

if test "x$XTEST_DIR" = "x"; then
    echo "XTEST_DIR must be set to the directory of the xtest repository."
    # Exit as a "skip" so make check works even without piglit.
    exit 77
fi

if test "x$PIGLIT_DIR" = "x"; then
    echo "PIGLIT_DIR must be set to the directory of the piglit repository."
    # Exit as a "skip" so make check works even without piglit.
    exit 77
fi

if test "x$XSERVER_DIR" = "x"; then
    echo "XSERVER_DIR must be set to the directory of the xserver repository."
    # Exit as a real failure because it should always be set.
    exit 1
fi

export PIGLIT_RESULTS_DIR=$PIGLIT_DIR/results/xvfb

startx \
    $XSERVER_DIR/test/scripts/xinit-piglit-session.sh \
    -- \
    $XSERVER_DIR/hw/vfb/Xvfb \
        -noreset \
        -screen scrn 1280x1024x24

# Write out piglit-summaries.
SHORT_SUMMARY=$PIGLIT_RESULTS_DIR/summary
LONG_SUMMARY=$PIGLIT_RESULTS_DIR/long-summary
$PIGLIT_DIR/piglit-summary.py -s $PIGLIT_RESULTS_DIR > $SHORT_SUMMARY
$PIGLIT_DIR/piglit-summary.py $PIGLIT_RESULTS_DIR > $LONG_SUMMARY

# Write the short summary to make check's log file.
cat $SHORT_SUMMARY

# Parse the piglit summary to decide on our exit status.
status=0
# "pass: 0" would mean no tests actually ran.
if grep "pass:.*0" $SHORT_SUMMARY > /dev/null; then
    status=1
fi
# Fails or crashes should be failures from make check's perspective.
if ! grep "fail:.*0" $SHORT_SUMMARY > /dev/null; then
    status=1
fi
if ! grep "crash:.*0" $SHORT_SUMMARY > /dev/null; then
    status=1
fi

if test $status != 0; then
    $PIGLIT_DIR/piglit-summary-html.py \
	--overwrite \
	$PIGLIT_RESULTS_DIR/html \
	$PIGLIT_RESULTS_DIR

    echo "Some piglit tests failed."
    echo "The list of failing tests can be found in $LONG_SUMMARY."
    echo "An html page of the failing tests can be found at $PIGLIT_RESULTS_DIR/html/problems.html"
fi

exit $status
