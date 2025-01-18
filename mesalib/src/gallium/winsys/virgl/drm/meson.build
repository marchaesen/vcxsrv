# Copyright © 2017 Intel Corporation
# SPDX-License-Identifier: MIT

libvirgldrm = static_library(
  'virgldrm',
  'virgl_drm_winsys.c',
  include_directories : [inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_gallium_aux, inc_gallium_drivers, inc_virtio],
  dependencies : [dep_libdrm, dep_libvirglcommon, idep_mesautil, idep_nir_headers],
  gnu_symbol_visibility : 'hidden',
)
