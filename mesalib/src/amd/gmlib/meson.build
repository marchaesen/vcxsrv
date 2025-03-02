# Copyright 2022 Advanced Micro Devices, Inc.
# SPDX-License-Identifier: MIT

c_args_gm = cc.get_supported_arguments([
  '-Wall',
  '-Wextra',
  '-Wno-unused',
  '-Wno-unused-parameter',
  '-Wno-unused-command-line-argument',
  '-Wno-ignored-qualifiers',
  '-Wno-missing-field-initializers',
  '-Wno-self-assign',
  '-Wno-implicit-fallthrough',
  '-Werror=comment',
  '-Werror=missing-braces',
  '-Werror=override-init',
  '-Werror=enum-conversion',
  '-Werror=enum-compare',
  '-Werror=maybe-uninitialized',
])

c_args_gm += [
  '-DGM_SIM',
]

gm_files = files(
  'tonemap_adaptor.h',
  'tonemap_adaptor.c',
  'gm/csc_api_funcs.c',
  'gm/csc_api_funcs.h',
  'gm/csc_funcs.c',
  'gm/csc_funcs.h',
  'gm/cs_funcs.c',
  'gm/cs_funcs.h',
  'gm/cvd_api_funcs.c',
  'gm/cvd_api_funcs.h',
  'gm/cvd_funcs.c',
  'gm/cvd_funcs.h',
  'gm/gm_api_funcs.c',
  'gm/gm_api_funcs.h',
  'gm/gm_funcs.c',
  'gm/gm_funcs.h',
  'gm/mat_funcs.c',
  'gm/mat_funcs.h',
  'ToneMapGenerator/inc/ToneMapGenerator.h',
  'ToneMapGenerator/inc/ToneMapTypes.h',
  'ToneMapGenerator/src/inc/AGMGenerator.h',
  'ToneMapGenerator/src/inc/CSCGenerator.h',
  'ToneMapGenerator/src/src/AGMGenerator.c',
  'ToneMapGenerator/src/src/ToneMapGenerator.c',
)

inc_amd_gm = include_directories(
  'gm',
  'ToneMapGenerator/inc',
  'ToneMapGenerator/src/inc',
)

libgm = static_library(
  'libgm.a',
  gm_files,
  install : false,
  c_args : c_args_gm,
  include_directories : inc_amd_gm
)
