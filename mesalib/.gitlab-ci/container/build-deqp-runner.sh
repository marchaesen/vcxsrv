#!/bin/bash

set -ex

cargo install --locked deqp-runner \
  -j ${FDO_CI_CONCURRENT:-4} \
  --version 0.10.0 \
  --root /usr/local \
  $EXTRA_CARGO_ARGS
