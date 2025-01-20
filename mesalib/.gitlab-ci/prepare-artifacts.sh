#!/usr/bin/env bash
# shellcheck disable=SC2038 # TODO: rewrite the find
# shellcheck disable=SC2086 # we want word splitting
# shellcheck disable=SC1091 # paths only become valid at runtime

. "${SCRIPTS_DIR}/setup-test-env.sh"

section_switch prepare-artifacts "artifacts: prepare"

set -e
set -o xtrace

CROSS_FILE=/cross_file-"$CROSS".txt

# Delete unused bin and includes from artifacts to save space.
rm -rf install/bin install/include
rm -f install/lib/*.a

# Strip the drivers in the artifacts to cut 80% of the artifacts size.
if [ -n "$CROSS" ]; then
    STRIP=$(sed -n -E "s/strip\s*=\s*\[?'(.*)'\]?/\1/p" "$CROSS_FILE")
    if [ -z "$STRIP" ]; then
        echo "Failed to find strip command in cross file"
        exit 1
    fi
else
    STRIP="strip"
fi
if [ -z "$ARTIFACTS_DEBUG_SYMBOLS" ]; then
    find install -name \*.so -exec $STRIP --strip-debug {} \;
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
cp -Rp .gitlab-ci/setup-test-env.sh install/
cp -Rp .gitlab-ci/*-runner.sh install/
cp -Rp .gitlab-ci/bin/structured_logger.py install/
cp -Rp .gitlab-ci/bin/custom_logger.py install/

mapfile -t duplicate_files < <(
  find src/ -path '*/ci/*' \
    \( \
      -name '*.txt' \
      -o -name '*.toml' \
      -o -name '*traces*.yml' \
    \) \
    -exec basename -a {} + | sort | uniq -d
)
if [ ${#duplicate_files[@]} -gt 0 ]; then
  echo 'Several files with the same name in various ci/ folders:'
  printf -- '  %s\n' "${duplicate_files[@]}"
  exit 1
fi

find src/ -path '*/ci/*' \
  \( \
    -name '*.txt' \
    -o -name '*.toml' \
    -o -name '*traces*.yml' \
  \) \
  -exec cp -p {} install/ \;

# Tar up the install dir so that symlinks and hardlinks aren't each
# packed separately in the zip file.
mkdir -p artifacts/
tar -cf artifacts/install.tar install
cp -Rp .gitlab-ci/common artifacts/ci-common
cp -Rp .gitlab-ci/lava artifacts/
cp -Rp .gitlab-ci/b2c artifacts/
cp bin/ci/structured_logger.py artifacts/

if [ -n "$S3_ARTIFACT_NAME" ]; then
    # Pass needed files to the test stage
    S3_ARTIFACT_NAME="$S3_ARTIFACT_NAME.tar.zst"
    zstd --quiet --threads ${FDO_CI_CONCURRENT:-0} artifacts/install.tar -o ${S3_ARTIFACT_NAME}
    ci-fairy s3cp --token-file "${S3_JWT_FILE}" ${S3_ARTIFACT_NAME} https://${PIPELINE_ARTIFACTS_BASE}/${S3_ARTIFACT_NAME}
fi

section_end prepare-artifacts
