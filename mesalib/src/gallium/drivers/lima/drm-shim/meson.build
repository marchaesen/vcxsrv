# Copyright (C) 2021 Icecream95
# SPDX-License-Identifier: MIT

liblima_noop_drm_shim = shared_library(
  'lima_noop_drm_shim',
  'lima_noop.c',
  include_directories: [inc_include, inc_src],
  dependencies: [dep_drm_shim, idep_mesautil],
  gnu_symbol_visibility : 'hidden',
  install : true,
)
