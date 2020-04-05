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
export PATH=/apitrace/build:$PATH

# Use the surfaceless EGL platform.
export EGL_PLATFORM=surfaceless
export DISPLAY=
export WAFFLE_PLATFORM=surfaceless_egl

# Perform a self-test to ensure tracie is working properly.
"$INSTALL/tracie/tests/test.sh"

python3 $INSTALL/tracie/tracie.py --file $INSTALL/traces.yml --device-name $DEVICE_NAME
