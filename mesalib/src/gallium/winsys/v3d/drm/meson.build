# Copyright © 2017 Broadcom
# SPDX-License-Identifier: MIT

libv3dwinsys = static_library(
  'v3dwinsys',
  files('v3d_drm_winsys.c'),
  include_directories : [
    inc_src, inc_include,
    inc_gallium, inc_gallium_aux, inc_gallium_drivers,
  ],
  gnu_symbol_visibility : 'hidden',
  dependencies : idep_mesautil,
)
