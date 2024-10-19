# Copyright © 2017 Intel Corporation
# SPDX-License-Identifier: MIT

libcrocuswinsys = static_library(
  'crocuswinsys',
  files('crocus_drm_winsys.c'),
  include_directories : [
    inc_src, inc_include,
    inc_gallium, inc_gallium_aux, inc_gallium_drivers,
  ],
  gnu_symbol_visibility : 'hidden',
  dependencies : [dep_valgrind, idep_intel_dev, idep_mesautil],
)
