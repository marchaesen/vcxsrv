# Copyright © 2017 Dylan Baker
# Copyright © 2018 Intel Corporation
# SPDX-License-Identifier: MIT

libswdri = static_library(
  'swdri',
  files('dri_sw_winsys.c', 'dri_sw_winsys.h'),
  gnu_symbol_visibility : 'hidden',
  include_directories : [inc_gallium, inc_include, inc_src, inc_gallium_aux],
  dependencies : idep_mesautil,
)
