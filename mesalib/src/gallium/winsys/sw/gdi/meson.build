# Copyright © 2018 Intel Corporation
# SPDX-License-Identifier: MIT

libwsgdi = static_library(
  'wsgdi',
  'gdi_sw_winsys.c',
  include_directories : [
    inc_src, inc_include, inc_gallium, inc_gallium_aux, inc_gallium_drivers, inc_frontends
  ],
  dependencies: idep_mesautil,
)
