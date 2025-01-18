# Copyright © 2017-2018 Intel Corporation
# SPDX-License-Identifier: MIT

libswkmsdri = static_library(
  'swkmsdri',
  files('kms_dri_sw_winsys.c', 'kms_dri_sw_winsys.h'),
  gnu_symbol_visibility : 'hidden',
  include_directories : [inc_gallium, inc_include, inc_src, inc_gallium_aux],
  dependencies : [dep_libdrm, idep_mesautil],
)
