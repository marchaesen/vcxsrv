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
artifacts/ci-common/generate-env.sh > results/job-rootfs-overlay/set-job-env-vars.sh

tar zcf job-rootfs-overlay.tar.gz -C results/job-rootfs-overlay/ .
ci-fairy minio login --token-file "${CI_JOB_JWT_FILE}"
ci-fairy minio cp job-rootfs-overlay.tar.gz "minio://${JOB_ROOTFS_OVERLAY_PATH}"

touch results/lava.log
tail -f results/lava.log &
artifacts/lava/lava_job_submitter.py \
	--dump-yaml \
	--pipeline-info "$CI_JOB_NAME: $CI_PIPELINE_URL on $CI_COMMIT_REF_NAME ${CI_NODE_INDEX}/${CI_NODE_TOTAL}" \
	--base-system-url-prefix "https://${BASE_SYSTEM_HOST_PATH}" \
	--mesa-build-url "${FDO_HTTP_CACHE_URI:-}https://${MESA_BUILD_PATH}" \
	--job-rootfs-overlay-url "${FDO_HTTP_CACHE_URI:-}https://${JOB_ROOTFS_OVERLAY_PATH}" \
	--job-artifacts-base ${JOB_ARTIFACTS_BASE} \
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
	--lava-tags "${LAVA_TAGS}" >> results/lava.log
