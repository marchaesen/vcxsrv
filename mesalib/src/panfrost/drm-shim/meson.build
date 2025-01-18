# Copyright (C) 2021 Icecream95
# SPDX-License-Identifier: MIT

libpanfrost_noop_drm_shim = shared_library(
  'panfrost_noop_drm_shim',
  'panfrost_noop.c',
  include_directories: [inc_include, inc_src],
  dependencies: dep_drm_shim,
  gnu_symbol_visibility : 'hidden',
  install : true,
)
