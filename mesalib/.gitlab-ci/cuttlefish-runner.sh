#!/usr/bin/env bash
# shellcheck disable=SC2086 # we want word splitting
# shellcheck disable=SC1091 # paths only become valid at runtime

. "${SCRIPTS_DIR}/setup-test-env.sh"

section_start cuttlefish_setup "cuttlefish: setup"
set -xe

export HOME=/cuttlefish
export PATH=/cuttlefish/bin:$PATH
export LD_LIBRARY_PATH=/cuttlefish/lib64:${CI_PROJECT_DIR}/install/lib:$LD_LIBRARY_PATH
export EGL_PLATFORM=surfaceless

# Pick up a vulkan driver
ARCH=$(uname -m)
export VK_DRIVER_FILES=${CI_PROJECT_DIR}/install/share/vulkan/icd.d/${VK_DRIVER:-}_icd.$ARCH.json

syslogd

chown root:kvm /dev/kvm

pushd /cuttlefish

# Add a function to perform some tasks when exiting the script
function my_atexit()
{
  # shellcheck disable=SC2317
  cp /cuttlefish/cuttlefish/instances/cvd-1/logs/logcat $RESULTS_DIR || true
  # shellcheck disable=SC2317
  cp /cuttlefish/cuttlefish/instances/cvd-1/kernel.log $RESULTS_DIR || true

  # shellcheck disable=SC2317
  cp /cuttlefish/cuttlefish/instances/cvd-1/logs/launcher.log $RESULTS_DIR || true

  # shellcheck disable=SC2317
  /cuttlefish/bin/stop_cvd -wait_for_launcher=10
}

# stop cuttlefish if the script ends prematurely or is interrupted
trap 'my_atexit' EXIT
trap 'exit 2' HUP INT PIPE TERM

ulimit -S -n 32768

# Clean up state of previous run
rm -rf  /cuttlefish/cuttlefish
rm -rf  /cuttlefish/.cache
rm -rf  /cuttlefish/.cuttlefish_config.json

launch_cvd \
  -daemon \
  -verbosity=VERBOSE \
  -file_verbosity=VERBOSE \
  -use_overlay=false \
  -enable_bootanimation=false \
  -enable_minimal_mode=true \
  -guest_enforce_security=false \
  -report_anonymous_usage_stats=no \
  -gpu_mode="$ANDROID_GPU_MODE" \
  -cpus=${FDO_CI_CONCURRENT:-4} \
  -memory_mb 8192 \
  -kernel_path="$HOME/bzImage" \
  -initramfs_path="$HOME/initramfs.img"

sleep 1

popd

ADB=adb

$ADB wait-for-device root
sleep 1
$ADB shell echo Hi from Android
# shellcheck disable=SC2035
$ADB logcat dEQP:D *:S &

# overlay vendor

OV_TMPFS="/data/overlay-remount"
$ADB shell mkdir -p "$OV_TMPFS"
$ADB shell mount -t tmpfs none "$OV_TMPFS"

$ADB shell mkdir -p "$OV_TMPFS/vendor-upper"
$ADB shell mkdir -p "$OV_TMPFS/vendor-work"

opts="lowerdir=/vendor,upperdir=$OV_TMPFS/vendor-upper,workdir=$OV_TMPFS/vendor-work"
$ADB shell mount -t overlay -o "$opts" none /vendor

$ADB shell setenforce 0

# deqp

$ADB shell mkdir -p /data/deqp
$ADB push /deqp-gles/modules/egl/deqp-egl-android /data/deqp
$ADB push /deqp-gles/assets/gl_cts/data/mustpass/egl/aosp_mustpass/3.2.6.x/egl-main.txt /data/deqp
$ADB push /deqp-vk/external/vulkancts/modules/vulkan/* /data/deqp
$ADB push /deqp-vk/mustpass/vk-main.txt.zst /data/deqp
$ADB push /deqp-tools/* /data/deqp
$ADB push /deqp-runner/deqp-runner /data/deqp

# download Android Mesa from S3
MESA_ANDROID_ARTIFACT_URL=https://${PIPELINE_ARTIFACTS_BASE}/${S3_ANDROID_ARTIFACT_NAME}.tar.zst
curl -L --retry 4 -f --retry-all-errors --retry-delay 60 -o ${S3_ANDROID_ARTIFACT_NAME}.tar.zst ${MESA_ANDROID_ARTIFACT_URL}
mkdir /mesa-android
tar -C /mesa-android -xvf ${S3_ANDROID_ARTIFACT_NAME}.tar.zst
rm "${S3_ANDROID_ARTIFACT_NAME}.tar.zst" &

INSTALL="/mesa-android/install"

$ADB push "$INSTALL/all-skips.txt" /data/deqp
$ADB push "$INSTALL/angle-skips.txt" /data/deqp
if [ -e "$INSTALL/$GPU_VERSION-flakes.txt" ]; then
  $ADB push "$INSTALL/$GPU_VERSION-flakes.txt" /data/deqp
fi
if [ -e "$INSTALL/$GPU_VERSION-fails.txt" ]; then
  $ADB push "$INSTALL/$GPU_VERSION-fails.txt" /data/deqp
fi
if [ -e "$INSTALL/$GPU_VERSION-skips.txt" ]; then
  $ADB push "$INSTALL/$GPU_VERSION-skips.txt" /data/deqp
fi
$ADB push "$INSTALL/deqp-$DEQP_SUITE.toml" /data/deqp

# remove 32 bits libs from /vendor/lib

$ADB shell rm -f /vendor/lib/egl/libGLES_mesa.so

$ADB shell rm -f /vendor/lib/egl/libEGL_angle.so
$ADB shell rm -f /vendor/lib/egl/libEGL_emulation.so
$ADB shell rm -f /vendor/lib/egl/libGLESv1_CM_angle.so
$ADB shell rm -f /vendor/lib/egl/libGLESv1_CM_emulation.so
$ADB shell rm -f /vendor/lib/egl/libGLESv2_angle.so
$ADB shell rm -f /vendor/lib/egl/libGLESv2_emulation.so

$ADB shell rm -f /vendor/lib/hw/vulkan.*

# replace on /vendor/lib64

$ADB push "$INSTALL/lib/libgallium_dri.so" /vendor/lib64/libgallium_dri.so
$ADB push "$INSTALL/lib/libEGL.so" /vendor/lib64/egl/libEGL_mesa.so
$ADB push "$INSTALL/lib/libGLESv1_CM.so" /vendor/lib64/egl/libGLESv1_CM_mesa.so
$ADB push "$INSTALL/lib/libGLESv2.so" /vendor/lib64/egl/libGLESv2_mesa.so

$ADB push "$INSTALL/lib/libvulkan_lvp.so" /vendor/lib64/hw/vulkan.lvp.so
$ADB push "$INSTALL/lib/libvulkan_virtio.so" /vendor/lib64/hw/vulkan.virtio.so

$ADB shell rm -f /vendor/lib64/egl/libEGL_emulation.so
$ADB shell rm -f /vendor/lib64/egl/libGLESv1_CM_emulation.so
$ADB shell rm -f /vendor/lib64/egl/libGLESv2_emulation.so

# Remove built-in ANGLE, we'll supply our own if needed
$ADB shell rm -f /vendor/lib64/egl/libEGL_angle.so
$ADB shell rm -f /vendor/lib64/egl/libGLESv1_CM_angle.so
$ADB shell rm -f /vendor/lib64/egl/libGLESv2_angle.so

if [ -n "$USE_ANGLE" ]; then
  $ADB push /angle/libEGL_angle.so /vendor/lib64/egl/libEGL_angle.so
  $ADB push /angle/libGLESv1_CM_angle.so /vendor/lib64/egl/libGLESv1_CM_angle.so
  $ADB push /angle/libGLESv2_angle.so /vendor/lib64/egl/libGLESv2_angle.so
fi

# Check what GLES implementation Surfaceflinger is using before copying the new mesa libraries
while [ "$($ADB shell dumpsys SurfaceFlinger | grep GLES:)" = "" ] ; do sleep 1; done
$ADB shell dumpsys SurfaceFlinger | grep GLES

# restart Android shell, so that surfaceflinger uses the new libraries
$ADB shell stop
$ADB shell start

# Check what GLES implementation Surfaceflinger is using after copying the new mesa libraries
while [ "$($ADB shell dumpsys SurfaceFlinger | grep GLES:)" = "" ] ; do sleep 1; done
MESA_RUNTIME_VERSION="$($ADB shell dumpsys SurfaceFlinger | grep GLES:)"

if [ "$USE_ANGLE" = 1 ]; then
  ANGLE_HASH=$(head -c 12 /angle/version)
  if ! printf "%s" "$MESA_RUNTIME_VERSION" | grep --quiet "${ANGLE_HASH}"; then
    echo "Fatal: Android is loading a wrong version of the ANGLE libs: ${ANGLE_HASH}" 1>&2
    exit 1
  fi
else
  MESA_BUILD_VERSION=$(cat "$INSTALL/VERSION")
  if ! printf "%s" "$MESA_RUNTIME_VERSION" | grep --quiet "${MESA_BUILD_VERSION}$"; then
     echo "Fatal: Android is loading a wrong version of the Mesa3D libs: ${MESA_RUNTIME_VERSION}" 1>&2
     exit 1
  fi
fi

BASELINE=""
if [ -e "$INSTALL/$GPU_VERSION-fails.txt" ]; then
    BASELINE="--baseline /data/deqp/$GPU_VERSION-fails.txt"
fi

# Default to an empty known flakes file if it doesn't exist.
$ADB shell "touch /data/deqp/$GPU_VERSION-flakes.txt"

if [ -e "$INSTALL/$GPU_VERSION-skips.txt" ]; then
    DEQP_SKIPS="$DEQP_SKIPS /data/deqp/$GPU_VERSION-skips.txt"
fi

if [ -n "$USE_ANGLE" ]; then
    DEQP_SKIPS="$DEQP_SKIPS /data/deqp/angle-skips.txt"
fi

AOSP_RESULTS=/data/deqp/results
uncollapsed_section_switch cuttlefish_test "cuttlefish: testing"

set +e
$ADB shell "mkdir ${AOSP_RESULTS}; cd ${AOSP_RESULTS}/..; \
  XDG_CACHE_HOME=/data/local/tmp \
  ./deqp-runner \
    suite \
    --suite /data/deqp/deqp-$DEQP_SUITE.toml \
    --output $AOSP_RESULTS \
    --skips /data/deqp/all-skips.txt $DEQP_SKIPS \
    --flakes /data/deqp/$GPU_VERSION-flakes.txt \
    --testlog-to-xml /data/deqp/testlog-to-xml \
    --shader-cache-dir /data/local/tmp \
    --fraction-start ${CI_NODE_INDEX:-1} \
    --fraction $(( CI_NODE_TOTAL * ${DEQP_FRACTION:-1})) \
    --jobs ${FDO_CI_CONCURRENT:-4} \
    $BASELINE \
    ${DEQP_RUNNER_MAX_FAILS:+--max-fails \"$DEQP_RUNNER_MAX_FAILS\"} \
    "

EXIT_CODE=$?
set -e
section_switch cuttlefish_results "cuttlefish: gathering the results"

$ADB pull "$AOSP_RESULTS/." "$RESULTS_DIR"

# Remove all but the first 50 individual XML files uploaded as artifacts, to
# save fd.o space when you break everything.
find $RESULTS_DIR -name \*.xml | \
    sort -n |
    sed -n '1,+49!p' | \
    xargs rm -f

# If any QPA XMLs are there, then include the XSL/CSS in our artifacts.
find $RESULTS_DIR -name \*.xml \
    -exec cp /deqp-tools/testlog.css /deqp-tools/testlog.xsl "$RESULTS_DIR/" ";" \
    -quit

$ADB shell "cd ${AOSP_RESULTS}/..; \
./deqp-runner junit \
   --testsuite dEQP \
   --results $AOSP_RESULTS/failures.csv \
   --output $AOSP_RESULTS/junit.xml \
   --limit 50 \
   --template \"See $ARTIFACTS_BASE_URL/results/{{testcase}}.xml\""

$ADB pull "$AOSP_RESULTS/junit.xml" "$RESULTS_DIR"

section_end cuttlefish_results
exit $EXIT_CODE
