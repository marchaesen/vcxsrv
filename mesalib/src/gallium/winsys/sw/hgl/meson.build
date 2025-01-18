# Copyright © 2017 Dylan Baker
# SPDX-License-Identifier: MIT

libswhgl = static_library(
  'swhgl',
  files('hgl_sw_winsys.cpp'),
  gnu_symbol_visibility : 'hidden',
  include_directories : [inc_gallium, inc_include, inc_src, inc_gallium_aux,
    include_directories('../../../frontends/hgl')
  ],
  dependencies: idep_mesautil,
  build_by_default : false,
)
