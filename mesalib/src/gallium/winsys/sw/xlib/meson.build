# Copyright © 2017 Intel Corporation
# SPDX-License-Identifier: MIT

libws_xlib = static_library(
  'ws_xlib',
  files('xlib_sw_winsys.c'),
  gnu_symbol_visibility : 'hidden',
  include_directories : [inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_gallium_aux],
  dependencies : [dep_x11, dep_xext, dep_xcb, idep_mesautil],
)
