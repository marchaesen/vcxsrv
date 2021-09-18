#!/bin/bash

rootfs_dst=$1

mkdir -p $rootfs_dst/results

# Set up the init script that brings up the system.
cp $BM/bm-init.sh $rootfs_dst/init
cp $CI_COMMON/init*.sh $rootfs_dst/

cp $CI_COMMON/capture-devcoredump.sh $rootfs_dst/

set +x
# Pass through relevant env vars from the gitlab job to the baremetal init script
"$CI_COMMON"/generate-env.sh > $rootfs_dst/set-job-env-vars.sh
chmod +x $rootfs_dst/set-job-env-vars.sh
echo "Variables passed through:"
cat $rootfs_dst/set-job-env-vars.sh
echo "export CI_JOB_JWT=${CI_JOB_JWT@Q}" >> $rootfs_dst/set-job-env-vars.sh
set -x

# Add the Mesa drivers we built, and make a consistent symlink to them.
mkdir -p $rootfs_dst/$CI_PROJECT_DIR
rsync -aH --delete $CI_PROJECT_DIR/install/ $rootfs_dst/$CI_PROJECT_DIR/install/
