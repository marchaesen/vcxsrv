# Copyright © 2020 Google, Inc
# SPDX-License-Identifier: MIT

computerator_files = [
  'a4xx.cc',
  'a6xx.cc',
  'ir3_asm.cc',
  'main.cc',
  freedreno_xml_header_files,
  ir3_parser[1],
]

computerator_cpp_args = cpp.get_supported_arguments([
  '-Wno-sign-compare',
  '-Wno-array-bounds',
])

if meson.is_cross_build()
  computerator_cpp_args += '-Wno-array-bounds'
endif

computerator = executable(
  'computerator',
  computerator_files,
  include_directories : [
    inc_freedreno,
    inc_include,
    inc_src,
  ],
  link_with : [
    libfreedreno_drm,
    libfreedreno_ir3,
    libfreedreno_perfcntrs,
    libfreedreno_common,
  ],
  dependencies : [
    dep_libdrm,
    idep_mesautil,
    # We don't actually use nir, but ir3 wants some nir headers:
    idep_nir,
  ],
  cpp_args : [computerator_cpp_args],
  build_by_default : with_tools.contains('freedreno'),
  install : with_tools.contains('freedreno'),
)
