# Copyright © 2019 Broadcom
# Copyright © 2020 Intel Corporation
# SPDX-License-Identifier: MIT

drm_shim = static_library(
  'drm_shim',
  [
    'device.c',
    'drm_shim.c',
  ],
  include_directories: [inc_include, inc_src],
  dependencies: [dep_libdrm, idep_mesautil, dep_dl],
  gnu_symbol_visibility : 'hidden',
)
dep_drm_shim = declare_dependency(
  link_with: drm_shim,
  dependencies: dep_libdrm,
)
