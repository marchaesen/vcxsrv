# Copyright © 2018 Rob Clark
# Copyright © 2019 Collabora
# SPDX-License-Identifier: MIT

libpanfrost_shared_files = files(
  'pan_minmax_cache.c',
  'pan_tiling.c',

  'pan_minmax_cache.h',
  'pan_tiling.h',
)

libpanfrost_shared = static_library(
  'panfrost_shared',
  [libpanfrost_shared_files],
  include_directories : [inc_include, inc_src],
  dependencies : idep_mesautil,
  c_args : [no_override_init_args, '-O3'],
  gnu_symbol_visibility : 'hidden',
  build_by_default : false,
)

if with_tests
  test(
    'panfrost_tiling',
    executable(
      'panfrost_tiling',
      files(
        'test/test-tiling.cpp',
      ),
      c_args : [c_msvc_compat_args, no_override_init_args],
      gnu_symbol_visibility : 'hidden',
      include_directories : [inc_include, inc_src, inc_panfrost],
      dependencies: [idep_gtest, idep_mesautil],
      link_with : [libpanfrost_shared],
    ),
    suite : ['panfrost'],
    protocol : 'gtest',
  )
endif
