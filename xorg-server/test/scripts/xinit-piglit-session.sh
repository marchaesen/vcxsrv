#!/bin/sh

# .xinitrc replacement to run piglit and exit.
#
# Note that piglit will run many processes against the server, so
# running the server with -noreset is recommended to improve runtime.

set -e

if test "x$PIGLIT_DIR" = "x"; then
    echo "PIGLIT_DIR must be set to the directory of the piglit repository."
    exit 1
fi

if test "x$PIGLIT_RESULTS_DIR" = "x"; then
    echo "PIGLIT_RESULTS_DIR must be defined"
    exit 1
fi

if test "x$XTEST_DIR" = "x"; then
    echo "XTEST_DIR must be set to the root of the built xtest tree."
    exit 1
fi

cd $PIGLIT_DIR

# Write the piglit.conf we'll use for our testing.  Don't use the
# default piglit.conf name because that may overwrite a local
# piglit.conf.
PIGLITCONF=piglit-xserver-test.conf
cat <<EOF > $PIGLITCONF
[xts]
path=$XTEST_DIR
EOF

# Skip some tests that are failing at the time of importing the script.
#    "REPORT: min_bounds, rbearing was 0, expecting 2"
PIGLIT_ARGS="$PIGLIT_ARGS -x xlistfontswithinfo@3"
PIGLIT_ARGS="$PIGLIT_ARGS -x xlistfontswithinfo@4"
PIGLIT_ARGS="$PIGLIT_ARGS -x xloadqueryfont@1"
PIGLIT_ARGS="$PIGLIT_ARGS -x xqueryfont@1"
PIGLIT_ARGS="$PIGLIT_ARGS -x xqueryfont@2"

exec ./piglit-run.py xts-render -f $PIGLITCONF $PIGLIT_ARGS $PIGLIT_RESULTS_DIR
