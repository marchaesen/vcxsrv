#!/bin/bash -e

if test "x$XTEST_DIR" = "x"; then
    echo "XTEST_DIR must be set to the directory of the xtest repository."
    # Exit as a "skip" so make check works even without xtest.
    exit 77
fi

if test "x$PIGLIT_DIR" = "x"; then
    echo "PIGLIT_DIR must be set to the directory of the piglit repository."
    # Exit as a "skip" so make check works even without piglit.
    exit 77
fi

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

# Need to kill weston before exiting, or meson will time out waiting for it to terminate
# We rely on bash's behaviour, which executes the EXIT trap handler even if the shell is
# terminated due to receiving a signal
trap 'kill $WESTON_PID' EXIT

# Wait for weston to initialize before starting Xwayland
if ! timeout 5s bash -c "while ! $XSERVER_BUILDDIR/hw/xwayland/Xwayland -pogo -displayfd 1 &>/dev/null; do sleep 1; done"; then
    # Try running Xwayland one more time, so we can propagate its stdout/stderr
    # output and exit status
    $XSERVER_BUILDDIR/hw/xwayland/Xwayland -pogo -displayfd 1
fi

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
PIGLIT_ARGS="$PIGLIT_ARGS -x xsetfontpath@1"
PIGLIT_ARGS="$PIGLIT_ARGS -x xsetfontpath@2"

export PIGLIT_ARGS

$XSERVER_DIR/test/scripts/run-piglit.sh
