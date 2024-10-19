# Copyright © 2018 Rob Clark
# Copyright © 2019 Collabora
# SPDX-License-Identifier: MIT

libpanfrost_util_files = files(
  'lcra.c',
  'lcra.h',
  'pan_collect_varyings.c',
  'pan_ir.c',
  'pan_ir.h',
  'pan_liveness.c',
  'pan_lower_framebuffer.c',
  'pan_lower_helper_invocation.c',
  'pan_lower_image_ms.c',
  'pan_lower_image_index.c',
  'pan_lower_sample_position.c',
  'pan_lower_store_component.c',
  'pan_lower_writeout.c',
  'pan_lower_xfb.c',
)

libpanfrost_util = static_library(
  'panfrost_util',
  [libpanfrost_util_files],
  include_directories : [inc_include, inc_src, inc_panfrost_hw],
  dependencies: [idep_nir, idep_mesautil],
  c_args : [no_override_init_args],
  gnu_symbol_visibility : 'hidden',
  build_by_default : false,
)
