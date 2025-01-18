#!/usr/bin/env bash
# shellcheck disable=SC2086 # we want word splitting

BINDGEN_VER=0.65.1
CBINDGEN_VER=0.26.0

# bindgen
RUSTFLAGS='-L native=/usr/local/lib' cargo install \
  bindgen-cli --version ${BINDGEN_VER} \
  --locked \
  -j ${FDO_CI_CONCURRENT:-4} \
  --root /usr/local

# cbindgen
RUSTFLAGS='-L native=/usr/local/lib' cargo install \
  cbindgen --version ${CBINDGEN_VER} \
  --locked \
  -j ${FDO_CI_CONCURRENT:-4} \
  --root /usr/local

