# Copyright © Microsoft Corporation
# SPDX-License-Identifier: MIT

libspirv_to_dxil = both_libraries(
  'spirv_to_dxil',
  [
    files(
      'dxil_spirv_nir.c',
      'dxil_spirv_nir.h',
      'dxil_spirv_nir_lower_bindless.c',
      'spirv_to_dxil.c',
      'spirv_to_dxil.h',
    ),
    sha1_h,
  ],
  vs_module_defs : 'spirv_to_dxil.def',
  dependencies : [idep_nir, idep_vtn, idep_libdxil_compiler],
  include_directories : [inc_include, inc_src],
  install : true,
)

idep_libspirv_to_dxil = declare_dependency(
   link_with : libspirv_to_dxil.get_static_lib(),
   include_directories : include_directories('.')
)

spirv_to_dxil = executable(
  'spirv2dxil',
  files(
    'spirv2dxil.c',
  ),
  dependencies : [idep_nir, idep_libdxil_compiler, idep_getopt, idep_libspirv_to_dxil],
  include_directories : [inc_include, inc_src],
  build_by_default : true,
  install : true,
)
