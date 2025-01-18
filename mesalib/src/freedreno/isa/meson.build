# Copyright © 2020 Google, Inc
# SPDX-License-Identifier: MIT

isa_depend_files = [
  'ir3-common.xml',
  'ir3-cat0.xml',
  'ir3-cat1.xml',
  'ir3-cat2.xml',
  'ir3-cat3.xml',
  'ir3-cat4.xml',
  'ir3-cat5.xml',
  'ir3-cat6.xml',
  'ir3-cat7.xml',
  isaspec_py_deps
]

ir3_isa = custom_target(
  'ir3-isa',
  input: ['ir3.xml'],
  output: ['ir3-isa.c', 'ir3-isa.h'],
  command: [
    prog_isaspec_decode, '--xml', '@INPUT@',
    '--out-c', '@OUTPUT0@', '--out-h', '@OUTPUT1@',
  ],
  depend_files: isa_depend_files,
)

decode_files = [
  ir3_isa,
  'isa.h',
]

_libir3decode = static_library(
  'ir3decode',
  decode_files,
  dependencies: [idep_mesautil, idep_isaspec_decode],
  link_with: [libisaspec],
  include_directories: [
    inc_include,
    inc_src,
  ],
  gnu_symbol_visibility: 'hidden',
)

idep_libir3decode = declare_dependency(
  include_directories : [
    include_directories('.'),
    inc_include,
    inc_src
  ],
  link_with : _libir3decode,
  dependencies: [idep_isaspec_decode],
  sources : [ir3_isa],
)

ir3disasm = executable(
  'ir3-disasm',
  ['ir3-disasm.c'],
  dependencies: [idep_libir3decode, idep_libfreedreno_common],
  build_by_default: with_tools.contains('freedreno'),
  include_directories: [
    inc_src,
  ],
  install: false,
)

encode_h = custom_target(
  'encode.h',
  input: ['ir3.xml'],
  output: 'encode.h',
  command: [
    prog_isaspec_encode, '--xml', '@INPUT@', '--out-h', '@OUTPUT@'
  ],
  depend_files: isa_depend_files,
)

encode_files = [
  encode_h,
  'encode.c',
  'isa.h',
]

libir3encode = static_library(
  'ir3encode',
  encode_files,
  dependencies: [idep_mesautil, idep_nir],
  include_directories: [
    inc_src,
    inc_include,
    inc_freedreno,
  ],
  gnu_symbol_visibility: 'hidden',
)
