# Copyright © 2019 Broadcom
# SPDX-License-Identifier: MIT

libvc4_noop_drm_shim = shared_library(
  'vc4_noop_drm_shim',
  'vc4_noop.c',
  include_directories: [inc_include, inc_src],
  dependencies: dep_drm_shim,
  gnu_symbol_visibility : 'hidden',
  install : true,
)

libv3d_noop_drm_shim = shared_library(
  'v3d_noop_drm_shim',
  'v3d_noop.c',
  include_directories: [inc_include, inc_src],
  dependencies: dep_drm_shim,
  gnu_symbol_visibility : 'hidden',
  install : true,
)
