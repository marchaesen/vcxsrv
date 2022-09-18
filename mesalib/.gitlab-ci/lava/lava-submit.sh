#!/bin/bash

set -e
set -x

# Try to use the kernel and rootfs built in mainline first, so we're more
# likely to hit cache
if wget -q --method=HEAD "https://${BASE_SYSTEM_MAINLINE_HOST_PATH}/done"; then
	BASE_SYSTEM_HOST_PATH="${BASE_SYSTEM_MAINLINE_HOST_PATH}"
else
	BASE_SYSTEM_HOST_PATH="${BASE_SYSTEM_FORK_HOST_PATH}"
fi

rm -rf results
mkdir -p results/job-rootfs-overlay/

cp artifacts/ci-common/capture-devcoredump.sh results/job-rootfs-overlay/
cp artifacts/ci-common/init-*.sh results/job-rootfs-overlay/
cp artifacts/ci-common/intel-gpu-freq.sh results/job-rootfs-overlay/

# Prepare env vars for upload.
KERNEL_IMAGE_BASE_URL="https://${BASE_SYSTEM_HOST_PATH}" \
	artifacts/ci-common/generate-env.sh > results/job-rootfs-overlay/set-job-env-vars.sh
echo -e "\e[0Ksection_start:$(date +%s):variables[collapsed=true]\r\e[0KVariables passed through:"
cat results/job-rootfs-overlay/set-job-env-vars.sh
echo -e "\e[0Ksection_end:$(date +%s):variables\r\e[0K"

tar zcf job-rootfs-overlay.tar.gz -C results/job-rootfs-overlay/ .
ci-fairy minio login --token-file "${CI_JOB_JWT_FILE}"
ci-fairy minio cp job-rootfs-overlay.tar.gz "minio://${JOB_ROOTFS_OVERLAY_PATH}"

touch results/lava.log
tail -f results/lava.log &
PYTHONPATH=artifacts/ artifacts/lava/lava_job_submitter.py \
	--dump-yaml \
	--pipeline-info "$CI_JOB_NAME: $CI_PIPELINE_URL on $CI_COMMIT_REF_NAME ${CI_NODE_INDEX}/${CI_NODE_TOTAL}" \
	--rootfs-url-prefix "https://${BASE_SYSTEM_HOST_PATH}" \
	--kernel-url-prefix "https://${BASE_SYSTEM_HOST_PATH}" \
	--build-url "${FDO_HTTP_CACHE_URI:-}https://${BUILD_PATH}" \
	--job-rootfs-overlay-url "${FDO_HTTP_CACHE_URI:-}https://${JOB_ROOTFS_OVERLAY_PATH}" \
	--job-timeout ${JOB_TIMEOUT:-30} \
	--first-stage-init artifacts/ci-common/init-stage1.sh \
	--ci-project-dir ${CI_PROJECT_DIR} \
	--device-type ${DEVICE_TYPE} \
	--dtb ${DTB} \
	--jwt-file "${CI_JOB_JWT_FILE}" \
	--kernel-image-name ${KERNEL_IMAGE_NAME} \
	--kernel-image-type "${KERNEL_IMAGE_TYPE}" \
	--boot-method ${BOOT_METHOD} \
	--visibility-group ${VISIBILITY_GROUP} \
	--lava-tags "${LAVA_TAGS}" \
	--mesa-job-name "$CI_JOB_NAME" \
	>> results/lava.log
