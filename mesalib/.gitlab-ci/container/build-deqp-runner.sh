#!/bin/bash

set -ex

cargo install --locked deqp-runner \
  -j ${FDO_CI_CONCURRENT:-4} \
  --version 0.8.2 \
  --root /usr/local \
  $EXTRA_CARGO_ARGS
