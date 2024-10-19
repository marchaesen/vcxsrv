# Copyright © 2017 Dylan Baker
# SPDX-License-Identifier: MIT

libwsw = static_library(
  'wsw',
  files('wrapper_sw_winsys.c', 'wrapper_sw_winsys.h'),
  gnu_symbol_visibility : 'hidden',
  include_directories : [inc_gallium, inc_include, inc_src, inc_gallium_aux],
  dependencies : idep_mesautil,
  build_by_default : false,
)
