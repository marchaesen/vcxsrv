#!/bin/sh

set -ex

INSTALL="$(pwd)/install"

RESULTS="$(pwd)/results"
mkdir -p "$RESULTS"

# Set up the driver environment.
# Modifiying here directly LD_LIBRARY_PATH may cause problems when
# using a command wrapper. Hence, we will just set it when running the
# command.
export __LD_LIBRARY_PATH="$LD_LIBRARY_PATH:$INSTALL/lib/"

# Sanity check to ensure that our environment is sufficient to make our tests
# run against the Mesa built by CI, rather than any installed distro version.
MESA_VERSION=$(cat "$INSTALL/VERSION" | sed 's/\./\\./g')

if [ "$VK_DRIVER" ]; then

    ### VULKAN ###

    # Set the Vulkan driver to use.
    export VK_ICD_FILENAMES="$INSTALL/share/vulkan/icd.d/${VK_DRIVER}_icd.x86_64.json"

    if [ "x$PIGLIT_PROFILES" = "xreplay" ]; then
        # Set environment for VulkanTools' VK_LAYER_LUNARG_screenshot layer.
        export VK_LAYER_PATH="$VK_LAYER_PATH:/VulkanTools/build/etc/vulkan/explicit_layer.d"
        export __LD_LIBRARY_PATH="$__LD_LIBRARY_PATH:/VulkanTools/build/lib"

        # Set environment for Wine.
        export WINEDEBUG="-all"
        export WINEPREFIX="/dxvk-wine64"
        export WINEESYNC=1

        # Set environment for DXVK.
        export DXVK_LOG_LEVEL="none"
        export DXVK_STATE_CACHE=0

        # Set environment for gfxreconstruct executables.
        export PATH="/gfxreconstruct/build/bin:$PATH"
    fi

    SANITY_MESA_VERSION_CMD="vulkaninfo"


    # Set up the Window System Interface (WSI)

    # IMPORTANT:
    #
    # Nothing to do here.
    #
    # Run vulkan against the host's running X server (xvfb doesn't
    # have DRI3 support).
    # Set the DISPLAY env variable in each gitlab-runner's
    # configuration file:
    # https://docs.gitlab.com/runner/configuration/advanced-configuration.html#the-runners-section
else

    ### GL/ES ###

    if [ "x$PIGLIT_PROFILES" = "xreplay" ]; then
        # Set environment for renderdoc libraries.
        export PYTHONPATH="$PYTHONPATH:/renderdoc/build/lib"
        export __LD_LIBRARY_PATH="$__LD_LIBRARY_PATH:/renderdoc/build/lib"

        # Set environment for apitrace executable.
        export PATH="/apitrace/build:$PATH"

        # Our rootfs may not have "less", which apitrace uses during
        # apitrace dump
        export PAGER=cat
    fi

    SANITY_MESA_VERSION_CMD="wflinfo"


    # Set up the platform windowing system.

    # Set environment for the waffle library.
    export __LD_LIBRARY_PATH="/waffle/build/lib:$__LD_LIBRARY_PATH"

    # Set environment for wflinfo executable.
    export PATH="/waffle/build/bin:$PATH"

    if [ "x$EGL_PLATFORM" = "xsurfaceless" ]; then

        # Use the surfaceless EGL platform.
        export DISPLAY=
        export WAFFLE_PLATFORM="surfaceless_egl"

        SANITY_MESA_VERSION_CMD="$SANITY_MESA_VERSION_CMD --platform surfaceless_egl --api gles2"

        if [ "x$GALLIUM_DRIVER" = "xvirpipe" ]; then
            # piglit is to use virpipe, and virgl_test_server llvmpipe
            export GALLIUM_DRIVER="$GALLIUM_DRIVER"

            LD_LIBRARY_PATH="$__LD_LIBRARY_PATH" \
            GALLIUM_DRIVER=llvmpipe \
            GALLIVM_PERF="nopt,no_filter_hacks" \
            VTEST_USE_EGL_SURFACELESS=1 \
            VTEST_USE_GLES=1 \
            virgl_test_server >"$RESULTS"/vtest-log.txt 2>&1 &

            sleep 1
        fi
    elif [ "x$PIGLIT_PLATFORM" = "xgbm" ]; then
        SANITY_MESA_VERSION_CMD="$SANITY_MESA_VERSION_CMD --platform gbm --api gl"
    else
        SANITY_MESA_VERSION_CMD="$SANITY_MESA_VERSION_CMD --platform glx --api gl --profile core"
        RUN_CMD_WRAPPER="xvfb-run --server-args=\"-noreset\" sh -c"
    fi
fi

SANITY_MESA_VERSION_CMD="$SANITY_MESA_VERSION_CMD | tee /tmp/version.txt | grep \"Mesa $MESA_VERSION\(\s\|$\)\""

rm -rf results
cd /piglit

PIGLIT_OPTIONS=$(printf "%s" "$PIGLIT_OPTIONS")

PIGLIT_CMD="./piglit run -j${FDO_CI_CONCURRENT:-4} $PIGLIT_OPTIONS $PIGLIT_PROFILES "$(/usr/bin/printf "%q" "$RESULTS")

RUN_CMD="export LD_LIBRARY_PATH=$__LD_LIBRARY_PATH; $SANITY_MESA_VERSION_CMD && $PIGLIT_CMD"

if [ "$RUN_CMD_WRAPPER" ]; then
    RUN_CMD="set +e; $RUN_CMD_WRAPPER "$(/usr/bin/printf "%q" "$RUN_CMD")"; set -e"
fi

eval $RUN_CMD

if [ $? -ne 0 ]; then
    printf "%s\n" "Found $(cat /tmp/version.txt), expected $MESA_VERSION"
fi

if [ ${PIGLIT_JUNIT_RESULTS:-0} -eq 1 ]; then
    ./piglit summary aggregate "$RESULTS" -o junit.xml
fi

PIGLIT_RESULTS="${PIGLIT_RESULTS:-$PIGLIT_PROFILES}"
RESULTSFILE="$RESULTS/$PIGLIT_RESULTS.txt"
mkdir -p .gitlab-ci/piglit
./piglit summary console "$RESULTS"/results.json.bz2 \
    | tee ".gitlab-ci/piglit/$PIGLIT_RESULTS.txt.orig" \
    | head -n -1 | grep -v ": pass" > $RESULTSFILE

if [ "x$PIGLIT_PROFILES" = "xreplay" ] \
       && [ ${PIGLIT_REPLAY_UPLOAD_TO_MINIO:-0} -eq 1 ]; then

    ci-fairy minio login $CI_JOB_JWT

    __PREFIX="trace/$PIGLIT_REPLAY_DEVICE_NAME"
    __MINIO_PATH="$PIGLIT_REPLAY_ARTIFACTS_BASE_URL"
    __MINIO_TRACES_PREFIX="traces"

    ci-fairy minio cp "$RESULTS"/junit.xml \
        "minio://${MINIO_HOST}${__MINIO_PATH}/${__MINIO_TRACES_PREFIX}/junit.xml"
    ci-fairy minio cp "$RESULTS"/results.json.bz2 \
        "minio://${MINIO_HOST}${__MINIO_PATH}/${__MINIO_TRACES_PREFIX}/results.json.bz2"

    find "$RESULTS/$__PREFIX" -type f -name "*.png" -printf "%P\n" \
        | while read -r line; do

        __TRACE="${line%-*-*}"
        if grep -q "^$__PREFIX/$__TRACE: pass$" ".gitlab-ci/piglit/$PIGLIT_RESULTS.txt.orig"; then
            if [ "x$CI_PROJECT_PATH" != "x$FDO_UPSTREAM_REPO" ]; then
                continue
            fi
            __MINIO_PATH="$PIGLIT_REPLAY_REFERENCE_IMAGES_BASE_URL"
            __DESTINATION_FILE_PATH="${line##*-}"
            if ci-fairy minio ls "minio://${MINIO_HOST}${__MINIO_PATH}/${__DESTINATION_FILE_PATH}" 2>/dev/null; then
                continue
            fi
        else
            __MINIO_PATH="$PIGLIT_REPLAY_ARTIFACTS_BASE_URL"
            __DESTINATION_FILE_PATH="$__MINIO_TRACES_PREFIX/${line##*-}"
        fi

        ci-fairy minio cp "$RESULTS/$__PREFIX/$line" \
            "minio://${MINIO_HOST}${__MINIO_PATH}/${__DESTINATION_FILE_PATH}"
    done
fi

cp "$INSTALL/piglit/$PIGLIT_RESULTS.txt" \
   ".gitlab-ci/piglit/$PIGLIT_RESULTS.txt.baseline"
if diff -q ".gitlab-ci/piglit/$PIGLIT_RESULTS.txt.baseline" $RESULTSFILE; then
    exit 0
fi

if [ ${PIGLIT_HTML_SUMMARY:-1} -eq 1 ]; then
    ./piglit summary html --exclude-details=pass \
        "$OLDPWD"/summary "$RESULTS"/results.json.bz2

    if [ "x$PIGLIT_PROFILES" = "xreplay" ]; then
        find "$OLDPWD"/summary -type f -name "*.html" -print0 \
            | xargs -0 sed -i 's@<img src="file://'"${RESULTS}"'@<img src="https://'"${MINIO_HOST}${PIGLIT_REPLAY_ARTIFACTS_BASE_URL}"'@g'
        find "$OLDPWD"/summary -type f -name "*.html" -print0 \
            | xargs -0 sed -i 's@<img src="file://@<img src="https://'"${MINIO_HOST}${PIGLIT_REPLAY_REFERENCE_IMAGES_BASE_URL}"'/@g'
    fi
fi

printf "%s\n" "Unexpected change in results:"
diff -u ".gitlab-ci/piglit/$PIGLIT_RESULTS.txt.baseline" $RESULTSFILE
exit 1
