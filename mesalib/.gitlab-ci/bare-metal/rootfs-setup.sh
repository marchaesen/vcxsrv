#!/bin/bash

rootfs_dst=$1

mkdir -p $rootfs_dst/results

# Set up the init script that brings up the system.
cp $BM/init.sh $rootfs_dst/init

cp $BM/capture-devcoredump.sh $rootfs_dst/

set +x
# Pass through relevant env vars from the gitlab job to the baremetal init script
touch $rootfs_dst/set-job-env-vars.sh
chmod +x $rootfs_dst/set-job-env-vars.sh
for var in \
    BARE_METAL_TEST_SCRIPT \
    BM_KERNEL_MODULES \
    CI_COMMIT_BRANCH \
    CI_COMMIT_TITLE \
    CI_JOB_JWT \
    CI_JOB_ID \
    CI_JOB_URL \
    CI_MERGE_REQUEST_SOURCE_BRANCH_NAME \
    CI_MERGE_REQUEST_TITLE \
    CI_NODE_INDEX \
    CI_NODE_TOTAL \
    CI_PIPELINE_ID \
    CI_PROJECT_PATH \
    CI_RUNNER_DESCRIPTION \
    CI_SERVER_URL \
    DEQP_CASELIST_FILTER \
    DEQP_CONFIG \
    DEQP_EXPECTED_RENDERER \
    DEQP_FRACTION \
    DEQP_HEIGHT \
    DEQP_NO_SAVE_RESULTS \
    DEQP_PARALLEL \
    DEQP_RESULTS_DIR \
    DEQP_VARIANT \
    DEQP_VER \
    DEQP_WIDTH \
    DEVICE_NAME \
    DRIVER_NAME \
    EGL_PLATFORM \
    FDO_CI_CONCURRENT \
    FDO_UPSTREAM_REPO \
    FD_MESA_DEBUG \
    FLAKES_CHANNEL \
    GPU_VERSION \
    IR3_SHADER_DEBUG \
    MESA_GL_VERSION_OVERRIDE \
    MESA_GLSL_VERSION_OVERRIDE \
    MESA_GLES_VERSION_OVERRIDE \
    MINIO_HOST \
    NIR_VALIDATE \
    PIGLIT_HTML_SUMMARY \
    PIGLIT_JUNIT_RESULTS \
    PIGLIT_OPTIONS \
    PIGLIT_PLATFORM \
    PIGLIT_PROFILES \
    PIGLIT_REPLAY_ARTIFACTS_BASE_URL \
    PIGLIT_REPLAY_DESCRIPTION_FILE \
    PIGLIT_REPLAY_DEVICE_NAME \
    PIGLIT_REPLAY_EXTRA_ARGS \
    PIGLIT_REPLAY_REFERENCE_IMAGES_BASE_URL \
    PIGLIT_REPLAY_UPLOAD_TO_MINIO \
    PIGLIT_RESULTS \
    TU_DEBUG \
    VK_DRIVER \
    ; do
  if [ -n "${!var+x}" ]; then
    echo "export $var=${!var@Q}" >> $rootfs_dst/set-job-env-vars.sh
  fi
done
echo "Variables passed through:"
cat $rootfs_dst/set-job-env-vars.sh
set -x

# Add the Mesa drivers we built, and make a consistent symlink to them.
mkdir -p $rootfs_dst/$CI_PROJECT_DIR
tar -C $rootfs_dst/$CI_PROJECT_DIR/ -xf $CI_PROJECT_DIR/artifacts/install.tar
ln -sf $CI_PROJECT_DIR/install $rootfs_dst/install
