# Mesa 3-D graphics library
#
# Copyright (C) 2022 Roman Stratiienko (r.stratiienko@gmail.com)
# SPDX-License-Identifier: MIT

c_args_for_u_gralloc = []
cpp_args_for_u_gralloc = []
options_for_u_gralloc = []
deps_for_u_gralloc = [dep_android]

files_u_gralloc = files(
  'u_gralloc.c',
  'u_gralloc_internal.c',
  'u_gralloc_fallback.c',
  'u_gralloc_cros_api.c',
  'u_gralloc_libdrm.c',
  'u_gralloc_qcom.c',
)

if dep_android_ui.found()
  files_u_gralloc += files('u_gralloc_imapper5_api.cpp')
  c_args_for_u_gralloc += '-DUSE_IMAPPER4_METADATA_API'
  cpp_args_for_u_gralloc += '-DUSE_IMAPPER4_METADATA_API'
  options_for_u_gralloc += 'cpp_std=c++17'
elif dep_android_mapper4.found()
  files_u_gralloc += files('u_gralloc_imapper4_api.cpp')
  c_args_for_u_gralloc += '-DUSE_IMAPPER4_METADATA_API'
  cpp_args_for_u_gralloc += '-DUSE_IMAPPER4_METADATA_API'
  options_for_u_gralloc += 'cpp_std=c++17'
  deps_for_u_gralloc += dep_android_mapper4
endif

if with_freedreno_vk or with_gallium_freedreno
  c_args_for_u_gralloc += '-DHAS_FREEDRENO'
endif

_libmesa_u_gralloc = static_library(
  '_mesa_u_gralloc',
  [files_u_gralloc],
  include_directories : [inc_include, inc_util],
  c_args : c_args_for_u_gralloc,
  cpp_args : cpp_args_for_u_gralloc,
  override_options : options_for_u_gralloc,
  gnu_symbol_visibility : 'hidden',
  build_by_default : false,
  dependencies: deps_for_u_gralloc,
)

idep_u_gralloc = declare_dependency(link_with: _libmesa_u_gralloc)
