# Copyright © 2020 Google, Inc
# SPDX-License-Identifier: MIT

libfreedreno_rnn_files = files(
  'colors.c',
  'colors.h',
  'path.c',
  'rnn.c',
  'rnn.h',
  'rnndec.c',
  'rnndec.h',
  'util.h',
)

libfreedreno_rnn = static_library(
  'freedreno_rnn',
  libfreedreno_rnn_files,
  include_directories: [
    inc_src,
    inc_include,
  ],
  c_args : [
    no_override_init_args,
    '-DRNN_DEF_PATH="' + rnn_path + '"',
  ],
  gnu_symbol_visibility: 'hidden',
  dependencies: [ dep_libxml2, idep_mesautil ],
  build_by_default: false,
)
