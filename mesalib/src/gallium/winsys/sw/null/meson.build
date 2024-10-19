# Copyright © 2017 Dylan Baker
# SPDX-License-Identifier: MIT

libws_null = static_library(
  'ws_null',
  files('null_sw_winsys.c', 'null_sw_winsys.h'),
  gnu_symbol_visibility : 'hidden',
  include_directories : [inc_gallium, inc_src, inc_gallium_aux, inc_include],
  dependencies : idep_mesautil,
  build_by_default : false,
)
