#!/bin/sh

set -ex

INSTALL="$(pwd)/install"

# Set up the driver environment.
export LD_LIBRARY_PATH="$LD_LIBRARY_PATH:$(pwd)/install/lib/"

# Set environment for renderdoc libraries.
export PYTHONPATH="$PYTHONPATH:/renderdoc/build/lib"
export LD_LIBRARY_PATH="$LD_LIBRARY_PATH:/renderdoc/build/lib"

# Set environment for the waffle library.
export LD_LIBRARY_PATH="/waffle/build/lib:$LD_LIBRARY_PATH"

# Set environment for apitrace executable.
export PATH="/apitrace/build:$PATH"

# Use the surfaceless EGL platform.
export EGL_PLATFORM="surfaceless"
export DISPLAY=
export WAFFLE_PLATFORM="surfaceless_egl"

RESULTS=`pwd`/results
mkdir -p $RESULTS

# Perform a self-test to ensure tracie is working properly.
"$INSTALL/tracie/tests/test.sh"

if [ "$GALLIUM_DRIVER" = "virpipe" ]; then
    # tracie is to use virpipe, and virgl_test_server llvmpipe
    export GALLIUM_DRIVER="$GALLIUM_DRIVER"

    GALLIUM_DRIVER=llvmpipe \
    GALLIVM_PERF="nopt,no_filter_hacks" \
    VTEST_USE_EGL_SURFACELESS=1 \
    VTEST_USE_GLES=1 \
    virgl_test_server >$RESULTS/vtest-log.txt 2>&1 &

    sleep 1
fi

# Sanity check to ensure that our environment is sufficient to make our tests
# run against the Mesa built by CI, rather than any installed distro version.
MESA_VERSION=$(cat "$INSTALL/VERSION" | sed 's/\./\\./g')
wflinfo --platform surfaceless_egl --api gles2 | grep "Mesa $MESA_VERSION\(\s\|$\)"

python3 "$INSTALL/tracie/tracie.py" --file "$INSTALL/traces.yml" --device-name "$DEVICE_NAME"
