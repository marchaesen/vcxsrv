#!/bin/bash

set -e
set -o xtrace

if test -n "$LLVM_VERSION"; then
    export LLVM_CONFIG="llvm-config-${LLVM_VERSION}"
fi

rm -rf build
scons $SCONS_TARGET force_scons=on
eval $SCONS_CHECK_COMMAND 
