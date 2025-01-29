#!/usr/bin/env bash
# shellcheck disable=SC2086 # we want word splitting

set -uex -o pipefail

if [ -z "$GPU_VERSION" ]; then
   echo 'GPU_VERSION must be set to something like "radeonsi-raven" or "freedreno-a630" (the name used in your ci/gpu-version-*.txt)'
   exit 1
fi

if [ -z "$FLUSTER_CODECS" ]; then
   echo 'FLUSTER_CODECS must be set to a space sparated list of codecs like "VP8" or "H.265"'
   exit 1
fi

# Check which fluster vectors to get
FLUSTER_VECTORS_HOST_PATH="${STORAGE_MAINLINE_HOST_PATH}/fluster/${FLUSTER_VECTORS_VERSION}"
if [ "$CI_PROJECT_PATH" != "$FDO_UPSTREAM_REPO" ]; then
  if ! curl -s -X HEAD -L --retry 4 -f --retry-delay 60 "https://${FLUSTER_VECTORS_HOST_PATH}/done"; then
    echo "Using Fluster vectors from the fork, cached from mainline is unavailable."
    FLUSTER_VECTORS_HOST_PATH="${STORAGE_FORK_HOST_PATH}/fluster/${FLUSTER_VECTORS_VERSION}"
  else
    echo "Using the cached Fluster vectors."
  fi
fi

FLUSTER_VECTORS_HOST_PATH="${FDO_HTTP_CACHE_URI:-}https://${FLUSTER_VECTORS_HOST_PATH}/vectors.tar.zst"

curl -L --retry 4 -f --retry-all-errors --retry-delay 60 ${FLUSTER_VECTORS_HOST_PATH} | tar --zstd -x -C /usr/local/

INSTALL="$PWD/install"

# Set up the driver environment.
export LD_LIBRARY_PATH="$INSTALL/lib/"

export LIBVA_DRIVERS_PATH=$INSTALL/lib/dri/
# libva spams driver open info by default, and that happens per testcase.
export LIBVA_MESSAGING_LEVEL=1

RESULTS=$PWD/${FLUSTER_RESULTS_DIR:-results}
mkdir -p $RESULTS

if [ -n "${FLUSTER_FRACTION:-}" ] || [ -n "$CI_NODE_INDEX" ]; then
    FRACTION=$((${FLUSTER_FRACTION:-1} * ${CI_NODE_TOTAL:-1}))
    FLUSTER_RUNNER_OPTIONS="${FLUSTER_RUNNER_OPTIONS:-} --fraction $FRACTION"
fi

# If the job is parallel at the gitab job level, take the corresponding fraction
# of the caselist.
if [ -n "$CI_NODE_INDEX" ]; then
    FLUSTER_RUNNER_OPTIONS="${FLUSTER_RUNNER_OPTIONS:-} --fraction-start ${CI_NODE_INDEX}"
fi

for codec in ${FLUSTER_CODECS}; do
    DECODERS="${DECODERS:-} GStreamer-${codec}-VAAPI-Gst1.0"
done

# Default to an empty known flakes file if it doesn't exist.
FLUSTER_FLAKES=$INSTALL/$GPU_VERSION-fluster-flakes.txt
touch ${FLUSTER_FLAKES}

# Default to an empty known fails file if it doesn't exist.
FLUSTER_FAILS=$INSTALL/$GPU_VERSION-fluster-fails.txt
touch ${FLUSTER_FAILS}

# Default to an empty known skips file if it doesn't exist.
FLUSTER_SKIPS=$INSTALL/$GPU_VERSION-fluster-skips.txt
touch ${FLUSTER_SKIPS}

set +e

fluster-runner \
        run \
        --fluster /usr/local/fluster/fluster.py \
        --output ${RESULTS} \
        --jobs ${FDO_CI_CONCURRENT:-4} \
        --skips ${FLUSTER_SKIPS} \
        --flakes ${FLUSTER_FLAKES} \
        --baseline ${FLUSTER_FAILS} \
        --decoders ${DECODERS} \
        ${FLUSTER_RUNNER_OPTIONS} \
        -v -v

FLUSTER_EXITCODE=$?

set -e

# Report the flakes to the IRC channel for monitoring (if configured):
if [ -n "${FLAKES_CHANNEL:-}" ]; then
    python3 $INSTALL/report-flakes.py \
     --host irc.oftc.net \
     --port 6667 \
     --results $RESULTS/results.csv \
     --known-flakes ${FLUSTER_FLAKES} \
     --channel "$FLAKES_CHANNEL" \
     --runner "$CI_RUNNER_DESCRIPTION" \
     --job "$CI_JOB_ID" \
     --url "$CI_JOB_URL" \
     --branch "${CI_MERGE_REQUEST_SOURCE_BRANCH_NAME:-$CI_COMMIT_BRANCH}" \
     --branch-title "${CI_MERGE_REQUEST_TITLE:-$CI_COMMIT_TITLE}" || true
fi

deqp-runner junit \
   --testsuite "fluster-${FLUSTER_CODECS// /-}" \
   --results $RESULTS/results.csv \
   --output $RESULTS/junit.xml \
   --template "See https://$CI_PROJECT_ROOT_NAMESPACE.pages.freedesktop.org/-/$CI_PROJECT_NAME/-/jobs/$CI_JOB_ID/artifacts/results/{{testcase}}.xml"

# Compress results.csv to save on bandwidth during the upload of artifacts to
# GitLab.
zstd --rm -T0 -8qc $RESULTS/results.csv -o $RESULTS/results.csv.zst

exit $FLUSTER_EXITCODE
