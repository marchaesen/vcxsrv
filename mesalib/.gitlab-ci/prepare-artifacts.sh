#!/bin/bash

set -e
set -o xtrace

CROSS_FILE=/cross_file-"$CROSS".txt

# Delete unused bin and includes from artifacts to save space.
rm -rf install/bin install/include

# Strip the drivers in the artifacts to cut 80% of the artifacts size.
if [ -n "$CROSS" ]; then
    STRIP=`sed -n -E "s/strip\s*=\s*'(.*)'/\1/p" "$CROSS_FILE"`
    if [ -z "$STRIP" ]; then
        echo "Failed to find strip command in cross file"
        exit 1
    fi
else
    STRIP="strip"
fi
if [ -z "$ARTIFACTS_DEBUG_SYMBOLS" ]; then
    find install -name \*.so -exec $STRIP {} \;
fi

# Test runs don't pull down the git tree, so put the dEQP helper
# script and associated bits there.
echo "$(cat VERSION) (git-$(git rev-parse HEAD | cut -b -10))" > install/VERSION
cp -Rp .gitlab-ci/bare-metal install/
cp -Rp .gitlab-ci/common install/
cp -Rp .gitlab-ci/piglit install/
cp -Rp .gitlab-ci/fossils.yml install/
cp -Rp .gitlab-ci/fossils install/
cp -Rp .gitlab-ci/fossilize-runner.sh install/
cp -Rp .gitlab-ci/crosvm-init.sh install/
cp -Rp .gitlab-ci/*.txt install/
cp -Rp .gitlab-ci/report-flakes.py install/
cp -Rp .gitlab-ci/vkd3d-proton install/
cp -Rp .gitlab-ci/*-runner.sh install/
find . -path \*/ci/\*.txt \
    -o -path \*/ci/\*.toml \
    -o -path \*/ci/\*traces\*.yml \
    | xargs -I '{}' cp -p '{}' install/

# Tar up the install dir so that symlinks and hardlinks aren't each
# packed separately in the zip file.
mkdir -p artifacts/
tar -cf artifacts/install.tar install
cp -Rp .gitlab-ci/common artifacts/ci-common
cp -Rp .gitlab-ci/lava artifacts/

if [ -n "$MINIO_ARTIFACT_NAME" ]; then
    # Pass needed files to the test stage
    MINIO_ARTIFACT_NAME="$MINIO_ARTIFACT_NAME.tar.gz"
    gzip -c artifacts/install.tar > ${MINIO_ARTIFACT_NAME}
    ci-fairy minio login $CI_JOB_JWT
    ci-fairy minio cp ${MINIO_ARTIFACT_NAME} minio://${PIPELINE_ARTIFACTS_BASE}/${MINIO_ARTIFACT_NAME}
fi
