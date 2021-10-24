#!/bin/sh

# This test script groups together a bunch of fast dEQP variant runs
# to amortize the cost of rebooting the board.

set -ex

EXIT=0

# Run reset tests without parallelism:
if ! env \
  DEQP_RESULTS_DIR=results/reset \
  FDO_CI_CONCURRENT=1 \
  DEQP_CASELIST_FILTER='.*reset.*' \
  /install/deqp-runner.sh; then
    EXIT=1
fi

# Then run everything else with parallelism:
if ! env \
  DEQP_RESULTS_DIR=results/nonrobustness \
  DEQP_CASELIST_INV_FILTER='.*reset.*' \
  /install/deqp-runner.sh; then
    EXIT=1
fi

