# Copyright (c) 2019 Etnaviv Project
# Copyright (c) 2019 Zodiac Inflight Innovations
# SPDX-License-Identifier: MIT

libetnaviv_noop_drm_shim = shared_library(
  'etnaviv_noop_drm_shim',
  'etnaviv_noop.c',
  include_directories: [inc_include, inc_src],
  dependencies: [dep_drm_shim, idep_mesautil],
  gnu_symbol_visibility : 'hidden',
  install : true,
)
