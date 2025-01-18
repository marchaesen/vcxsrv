# Copyright © 2019 Purism SPC
# SPDX-License-Identifier: MIT

libetnaviv_drm_files = files(
  'etnaviv_device.c',
  'etnaviv_gpu.c',
  'etnaviv_bo.c',
  'etnaviv_bo_cache.c',
  'etnaviv_perfmon.c',
  'etnaviv_pipe.c',
  'etnaviv_cmd_stream.c',
  'etnaviv_drmif.h',
  'etnaviv_priv.h',
)

libetnaviv_drm = static_library(
  'etnaviv_drm',
  libetnaviv_drm_files,
  include_directories : [
    inc_etnaviv,
    inc_include,
    inc_src,
  ],
  c_args : [no_override_init_args],
  gnu_symbol_visibility : 'hidden',
  dependencies : [
    dep_libdrm,
    dep_valgrind,
    idep_mesautil,
    idep_etna_common,
  ],
  link_with : [
    libetnaviv_hwdb
  ],
  build_by_default : false,
)

if with_tools.contains('etnaviv')
  subdir('tests')
endif
