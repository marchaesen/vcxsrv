#!/bin/sh -e

# this times out on Travis, because the tests take too long.
if test "x$TRAVIS_BUILD_DIR" != "x"; then
    exit 77
fi

# Weston requires XDG_RUNTIME_DIR
if test "x$XDG_RUNTIME_DIR" = "x"; then
    export XDG_RUNTIME_DIR=$(mktemp -d)
fi

# Skip if weston isn't available
weston --version >/dev/null || exit 77

weston --no-config --backend=headless-backend.so --socket=wayland-$$ &
WESTON_PID=$!
export WAYLAND_DISPLAY=wayland-$$

# Wait for weston to initialize before starting Xwayland
timeout --preserve-status 60s bash -c 'while ! weston-info &>/dev/null; do sleep 1; done'

# Start an Xwayland server
export PIGLIT_RESULTS_DIR=$XSERVER_BUILDDIR/test/piglit-results/xwayland
export SERVER_COMMAND="$XSERVER_BUILDDIR/hw/xwayland/Xwayland -noreset"

# Make sure glamor doesn't use HW acceleration
export GBM_ALWAYS_SOFTWARE=1

# Tests that currently fail on llvmpipe on CI
PIGLIT_ARGS="$PIGLIT_ARGS -x xcleararea@6"
PIGLIT_ARGS="$PIGLIT_ARGS -x xcleararea@7"
PIGLIT_ARGS="$PIGLIT_ARGS -x xclearwindow@4"
PIGLIT_ARGS="$PIGLIT_ARGS -x xclearwindow@5"
PIGLIT_ARGS="$PIGLIT_ARGS -x xcopyarea@1"

export PIGLIT_ARGS

# Do not let run-piglit.sh exit status terminate this script prematurely
set +e
$XSERVER_DIR/test/scripts/run-piglit.sh
PIGLIT_STATUS=$?

kill $WESTON_PID
exit $PIGLIT_STATUS
