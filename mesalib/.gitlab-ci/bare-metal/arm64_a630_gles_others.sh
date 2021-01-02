#!/bin/sh

# This test script groups together a bunch of fast dEQP variant runs
# to amortize the cost of rebooting the board.

set -ex

EXIT=0

# Test rendering with the gmem path forced when possible (~1 minute)
if ! env \
  DEQP_RESULTS_DIR=results/gmem \
  DEQP_VER=gles31 \
  DEQP_FRACTION=5 \
  FD_MESA_DEBUG=nobypass \
  /install/deqp-runner.sh; then
    EXIT=1
fi

# Test rendering with the bypass path forced when possible (~1 minute)
if ! env \
  DEQP_RESULTS_DIR=results/bypass \
  DEQP_VER=gles31 \
  DEQP_FRACTION=5 \
  FD_MESA_DEBUG=nogmem \
  GPU_VERSION=freedreno-a630-bypass \
  /install/deqp-runner.sh; then
    EXIT=1
fi

# Test rendering with the UBO-to-constants optimization disabled (~1 minute)
if ! env \
  DEQP_RESULTS_DIR=results/nouboopt \
  DEQP_VER=gles31 \
  IR3_SHADER_DEBUG=nouboopt \
  DEQP_CASELIST_FILTER="functional.*ubo" \
  /install/deqp-runner.sh; then
    EXIT=1
fi

# gles3-565nozs mustpass (~20s)
if ! env \
  DEQP_RESULTS_DIR=results/gles3-565nozs \
  DEQP_VER=gles3 \
  DEQP_CONFIG="rgb565d0s0ms0" \
  DEQP_VARIANT="565-no-depth-no-stencil" \
  /install/deqp-runner.sh; then
    EXIT=1
fi

# gles31-565nozs mustpass (~1s)
if ! env \
  DEQP_RESULTS_DIR=results/gles31-565nozs \
  DEQP_VER=gles31 \
  DEQP_CONFIG="rgb565d0s0ms0" \
  DEQP_VARIANT="565-no-depth-no-stencil" \
  /install/deqp-runner.sh; then
    EXIT=1
fi

# gles3-multisample mustpass -- disabled pending https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/1859
# if ! env \
#   DEQP_RESULTS_DIR=results/gles3-multisample \
#   DEQP_VER=gles3 \
#   DEQP_CONFIG="rgba8888d24s8ms4" \
#   DEQP_VARIANT="multisample" \
#   /install/deqp-runner.sh; then
#     EXIT=1
# fi

# gles31-multisample mustpass -- disabled pending https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/1859
# if ! env \
#   DEQP_RESULTS_DIR=results/gles31-multisample \
#   DEQP_VER=gles31 \
#   DEQP_CONFIG="rgba8888d24s8ms4" \
#   DEQP_VARIANT="multisample" \
#   /install/deqp-runner.sh; then
#     EXIT=1
# fi

exit $EXIT
