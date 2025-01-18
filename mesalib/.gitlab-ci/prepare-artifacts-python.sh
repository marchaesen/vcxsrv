#!/usr/bin/env bash
# shellcheck disable=SC2038 # TODO: rewrite the find
# shellcheck disable=SC2086 # we want word splitting
# shellcheck disable=SC1091 # relative paths only become valid at runtime

. "${SCRIPTS_DIR}/setup-test-env.sh"

section_switch prepare-artifacts "artifacts: prepare"

set -e
set -o xtrace

mkdir -p artifacts/

# Test runs don't pull down the git tree, so put the dEQP helper
# script and associated bits there.
echo "$(cat VERSION) (git-$(git rev-parse HEAD | cut -b -10))" > artifacts/VERSION
cp -Rp .gitlab-ci/report-flakes.py artifacts/
cp -Rp .gitlab-ci/setup-test-env.sh artifacts/
cp -Rp .gitlab-ci/common artifacts/ci-common
cp -Rp .gitlab-ci/b2c artifacts/
cp -Rp .gitlab-ci/bare-metal artifacts/
cp -Rp .gitlab-ci/lava artifacts/
cp -Rp .gitlab-ci/bin/*_logger.py artifacts/

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

if [ -d "src/" ]; then
  find src/ -path '*/ci/*' \
    \( \
      -name '*.txt' \
      -o -name '*.toml' \
      -o -name '*traces*.yml' \
    \) \
    -exec cp -p {} artifacts/ \;
fi
cp -Rp .gitlab-ci/*.txt artifacts/

if [ -n "$S3_ARTIFACT_NAME" ]; then
    # Pass needed files to the test stage
    S3_ARTIFACT_TAR="$S3_ARTIFACT_NAME.tar.zst"
    tar cv artifacts/ | zstd -o "${S3_ARTIFACT_TAR}"
    ci-fairy s3cp --token-file "${S3_JWT_FILE}" "${S3_ARTIFACT_TAR}" "https://${PIPELINE_ARTIFACTS_BASE}/${S3_ARTIFACT_TAR}"
    rm "${S3_ARTIFACT_TAR}"
fi

section_end prepare-artifacts
