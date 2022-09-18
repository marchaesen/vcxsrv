#!/bin/bash

set -e
set -o xtrace

CROSS_FILE=/cross_file-"$CROSS".txt

# We need to control the version of llvm-config we're using, so we'll
# tweak the cross file or generate a native file to do so.
if test -n "$LLVM_VERSION"; then
    LLVM_CONFIG="llvm-config-${LLVM_VERSION}"
    echo -e "[binaries]\nllvm-config = '`which $LLVM_CONFIG`'" > native.file
    if [ -n "$CROSS" ]; then
        sed -i -e '/\[binaries\]/a\' -e "llvm-config = '`which $LLVM_CONFIG`'" $CROSS_FILE
    fi
    $LLVM_CONFIG --version
else
    rm -f native.file
    touch native.file
fi

# cross-xfail-$CROSS, if it exists, contains a list of tests that are expected
# to fail for the $CROSS configuration, one per line. you can then mark those
# tests in their meson.build with:
#
# test(...,
#      should_fail: meson.get_cross_property('xfail', '').contains(t),
#     )
#
# where t is the name of the test, and the '' is the string to search when
# not cross-compiling (which is empty, because for amd64 everything is
# expected to pass).
if [ -n "$CROSS" ]; then
    CROSS_XFAIL=.gitlab-ci/cross-xfail-"$CROSS"
    if [ -s "$CROSS_XFAIL" ]; then
        sed -i \
            -e '/\[properties\]/a\' \
            -e "xfail = '$(tr '\n' , < $CROSS_XFAIL)'" \
            "$CROSS_FILE"
    fi
fi

# Only use GNU time if available, not any shell built-in command
case $CI_JOB_NAME in
    # strace and wine don't seem to mix well
    # ASAN leak detection is incompatible with strace
    debian-mingw32-x86_64|*-asan*)
        if test -f /usr/bin/time; then
            MESON_TEST_ARGS+=--wrapper=$PWD/.gitlab-ci/meson/time.sh
        fi
        Xvfb :0 -screen 0 1024x768x16 &
        export DISPLAY=:0.0
        ;;
    *)
        if test -f /usr/bin/time -a -f /usr/bin/strace; then
            MESON_TEST_ARGS+=--wrapper=$PWD/.gitlab-ci/meson/time-strace.sh
        fi
        ;;
esac

rm -rf _build
meson _build --native-file=native.file \
      --wrap-mode=nofallback \
      ${CROSS+--cross "$CROSS_FILE"} \
      -D prefix=`pwd`/install \
      -D libdir=lib \
      -D buildtype=${BUILDTYPE:-debug} \
      -D build-tests=true \
      -D c_args="$(echo -n $C_ARGS)" \
      -D cpp_args="$(echo -n $CPP_ARGS)" \
      -D enable-glcpp-tests=false \
      -D libunwind=${UNWIND} \
      ${DRI_LOADERS} \
      ${GALLIUM_ST} \
      -D gallium-drivers=${GALLIUM_DRIVERS:-[]} \
      -D vulkan-drivers=${VULKAN_DRIVERS:-[]} \
      -D video-codecs=h264dec,h264enc,h265dec,h265enc,vc1dec \
      -D werror=true \
      ${EXTRA_OPTION}
cd _build
meson configure
if command -V mold &> /dev/null ; then
    mold --run ninja
else
    ninja
fi
LC_ALL=C.UTF-8 meson test --num-processes ${FDO_CI_CONCURRENT:-4} --print-errorlogs ${MESON_TEST_ARGS}
if command -V mold &> /dev/null ; then
    mold --run ninja install
else
    ninja install
fi
cd ..
