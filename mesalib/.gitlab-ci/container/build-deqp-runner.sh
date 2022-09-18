#!/bin/sh
# shellcheck disable=SC2086 # we want word splitting

set -ex

if [ -n "${DEQP_RUNNER_GIT_TAG}${DEQP_RUNNER_GIT_REV}" ]; then
    # Build and install from source
    DEQP_RUNNER_CARGO_ARGS="--git ${DEQP_RUNNER_GIT_URL:-https://gitlab.freedesktop.org/anholt/deqp-runner.git}"

    if [ -n "${DEQP_RUNNER_GIT_TAG}" ]; then
        DEQP_RUNNER_CARGO_ARGS="--tag ${DEQP_RUNNER_GIT_TAG} ${DEQP_RUNNER_CARGO_ARGS}"
    else
        DEQP_RUNNER_CARGO_ARGS="--rev ${DEQP_RUNNER_GIT_REV} ${DEQP_RUNNER_CARGO_ARGS}"
    fi

    DEQP_RUNNER_CARGO_ARGS="${DEQP_RUNNER_CARGO_ARGS} ${EXTRA_CARGO_ARGS}"
else
    # Install from package registry
    DEQP_RUNNER_CARGO_ARGS="--version 0.15.0 ${EXTRA_CARGO_ARGS} -- deqp-runner"
fi

cargo install --locked  \
    -j ${FDO_CI_CONCURRENT:-4} \
    --root /usr/local \
    ${DEQP_RUNNER_CARGO_ARGS}

# remove unused test runners to shrink images for the Mesa CI build (not kernel,
# which chooses its own deqp branch)
if [ -z "${DEQP_RUNNER_GIT_TAG}${DEQP_RUNNER_GIT_REV}" ]; then
    rm -f /usr/local/bin/igt-runner
fi
