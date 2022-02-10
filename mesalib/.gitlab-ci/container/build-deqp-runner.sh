#!/bin/sh

set -ex

if [ -n "${DEQP_RUNNER_GIT_TAG}${DEQP_RUNNER_GIT_REV}" ]; then
    # Build and install from source
    EXTRA_CARGO_ARGS="--git ${DEQP_RUNNER_GIT_URL:-https://gitlab.freedesktop.org/anholt/deqp-runner.git} ${EXTRA_CARGO_ARGS}"

    if [ -n "${DEQP_RUNNER_GIT_TAG}" ]; then
        EXTRA_CARGO_ARGS="--tag ${DEQP_RUNNER_GIT_TAG} ${EXTRA_CARGO_ARGS}"
    else
        EXTRA_CARGO_ARGS="--rev ${DEQP_RUNNER_GIT_REV} ${EXTRA_CARGO_ARGS}"
    fi
else
    # Install from package registry
    EXTRA_CARGO_ARGS="--version 0.12.0 ${EXTRA_CARGO_ARGS} -- deqp-runner"
fi

cargo install --locked  \
    -j ${FDO_CI_CONCURRENT:-4} \
    --root /usr/local \
    ${EXTRA_CARGO_ARGS}
