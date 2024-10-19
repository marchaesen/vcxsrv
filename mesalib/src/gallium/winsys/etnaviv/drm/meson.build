# Copyright © 2017 Intel Corporation
# SPDX-License-Identifier: MIT

libetnavivdrm = static_library(
  'etnavivdrm',
  'etnaviv_drm_winsys.c',
  include_directories : [
    inc_include, inc_src, inc_gallium, inc_gallium_aux, inc_gallium_drivers,
    inc_etnaviv,
  ],
  link_with: libetnaviv_drm,
  dependencies : [dep_libdrm, idep_nir_headers, idep_mesautil, idep_etna_common, idep_mesautil],
)
