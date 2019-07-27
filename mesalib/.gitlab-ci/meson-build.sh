#!/bin/bash

set -e
set -o xtrace

# We need to control the version of llvm-config we're using, so we'll
# generate a native file to do so. This requires meson >=0.49
if test -n "$LLVM_VERSION"; then
    LLVM_CONFIG="llvm-config-${LLVM_VERSION}"
    echo -e "[binaries]\nllvm-config = '`which $LLVM_CONFIG`'" > native.file
    $LLVM_CONFIG --version
else
    rm -f native.file
    touch native.file
fi

rm -rf _build
meson _build --native-file=native.file \
      ${CROSS} \
      -D libdir=lib \
      -D buildtype=debug \
      -D build-tests=true \
      -D libunwind=${UNWIND} \
      ${DRI_LOADERS} \
      -D dri-drivers=${DRI_DRIVERS:-[]} \
      ${GALLIUM_ST} \
      -D gallium-drivers=${GALLIUM_DRIVERS:-[]} \
      -D vulkan-drivers=${VULKAN_DRIVERS:-[]} \
      -D I-love-half-baked-turnips=true \
      ${EXTRA_OPTION}
cd _build
meson configure
ninja -j4
LC_ALL=C.UTF-8 ninja test
DESTDIR=$PWD/../install ninja install
cd ..

if test -n "$MESON_SHADERDB"; then
    ./.gitlab-ci/run-shader-db.sh;
fi
