# Copyright © 2023 Igalia S.L.
# SPDX-License-Identifier: MIT

isa_depend_files = [
  'etnaviv.xml',
  isaspec_py_deps,
]

etnaviv_isa = custom_target(
  'etnaviv-isa',
  input: ['etnaviv.xml'],
  output: ['etnaviv-isa.c', 'etnaviv-isa.h'],
  command: [
    prog_isaspec_decode, '--xml', '@INPUT@',
    '--out-c', '@OUTPUT0@', '--out-h', '@OUTPUT1@',
  ],
  depend_files: isa_depend_files,
)

decode_files = [
  etnaviv_isa,
  'isa.h',
]

_libetnaviv_decode = static_library(
  'etnaviv_decode',
  decode_files,
  dependencies: [idep_mesautil, idep_isaspec_decode],
  link_with: [libisaspec],
  include_directories: [
    inc_include,
    inc_src,
  ],
  gnu_symbol_visibility: 'hidden',
)

idep_libetnaviv_decode = declare_dependency(
  include_directories : [
    include_directories('.'),
    inc_include,
    inc_src
  ],
  link_with : _libetnaviv_decode,
  dependencies: [idep_isaspec_decode],
  sources : [etnaviv_isa],
)

prog_enums_h = files('enums.h.py')
etnaviv_enums_h = custom_target(
  'enums.h',
  input: 'etnaviv.xml',
  output: 'enums.h',
  command: [prog_python, prog_enums_h, '--xml', '@INPUT@', '--output', '@OUTPUT@'],
  depend_files: [isa_depend_files, prog_enums_h],
)

idep_etnaviv_enums_h = declare_dependency(
  sources : [etnaviv_enums_h],
  include_directories : include_directories('.'),
)

encode_h = custom_target(
  'encode.h',
  input: ['etnaviv.xml'],
  output: 'encode.h',
  command: [
    prog_isaspec_encode, '--xml', '@INPUT@', '--out-h', '@OUTPUT@'
  ],
  depend_files: isa_depend_files,
)

encode_files = [
  etnaviv_enums_h,
  encode_h,
  'encode.c',
]

libetnaviv_encode = static_library(
  'etnaviv_encode',
  encode_files,
  gnu_symbol_visibility: 'hidden',
  include_directories: [
    inc_src,
  ],
)

disasm = executable(
  'etnaviv-disasm',
  ['disasm.c'],
  dependencies: idep_libetnaviv_decode,
  build_by_default: with_tools.contains('etnaviv'),
  include_directories: [
    inc_src,
  ],
  install: false,
)

if with_tools.contains('etnaviv')
  isa_bindings_rs = rust.bindgen(
    input : 'bindings.h',
    output : 'isa_bindings.rs',
    include_directories : [inc_src],
    args : [
      '--disable-header-comment',
      '--ignore-functions',
      '--allowlist-type', 'etna_asm_result',
      '--allowlist-type', 'etna_inst',
      '--allowlist-type', 'isa_swiz',
      '--default-enum-style', 'rust',
      '--with-derive-default',
    ],
  )

  _libetnaviv_isa_bindings_gen = static_library(
    'isa_bindings',
    isa_bindings_rs,
    gnu_symbol_visibility : 'hidden',
    rust_abi : 'rust',
    rust_args: [
      '-Anon_camel_case_types',
    ],
  )

  dep_indexmap = dependency('indexmap',
    version : '>= 2.2.6',
    fallback : ['indexmap', 'dep_indexmap'],
    required : true,
  )

  dep_roxmltree = dependency('roxmltree',
    version : '>= 0.20.roxmltree',
    fallback : ['roxmltree', 'dep_roxmltree'],
    required : true,
  )

  dep_syn = dependency('syn',
    version : '>= 2.0.15',
    fallback : ['syn', 'dep_syn'],
    required : true,
  )

  dep_pest_generator = dependency('pest_generator',
    version: '>= 2.7.6',
    fallback: ['pest_generator', 'dep_pest_generator'],
    required: true,
  )

  dep_pest = dependency('pest',
    version: '>= 2.7.6',
    fallback: ['pest', 'dep_pest'],
    required: true,
  )

  _libetnaviv_isa_proc_rs = rust.proc_macro(
    'etnaviv_isa_proc',
    files('isa_proc.rs', 'isa.rs'),
    dependencies : [dep_indexmap, dep_roxmltree, dep_syn, dep_pest_generator],
  )

  if with_tests
    # triggers a bug in meson
    # rust.test('isa', _libetnaviv_isa_proc_rs, suite : ['etnaviv'])
  endif

  libetnaviv_isa_rs = static_library(
    'etnaviv_isa_rs',
    files('lib.rs'),
    gnu_symbol_visibility : 'hidden',
    rust_abi : 'c',
    dependencies: [dep_indexmap, dep_roxmltree, dep_pest],
    link_with: [_libetnaviv_isa_bindings_gen, _libetnaviv_isa_proc_rs],
  )

  etnaviv_assembler = executable(
    'etnaviv-assembler',
    ['assembler.c'],
    gnu_symbol_visibility: 'hidden',
    dependencies: [ idep_libetnaviv_decode ],
    link_with: [libetnaviv_encode, libetnaviv_isa_rs],
  )
else
  pre_args += '-DHAVE_ETNAVIV_NO_PEST'

  # dummy library
  libetnaviv_isa_rs = static_library(
    'etnaviv_isa_rs',
    sources: [],
  )
endif

subdir('tests')
