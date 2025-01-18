# Copyright © 2020 Google, Inc
# SPDX-License-Identifier: MIT

libfreedreno_ir2 = static_library(
  'freedreno_ir2',
  [
    'disasm-a2xx.c',
    'instr-a2xx.h',
    freedreno_xml_header_files,
  ],
  include_directories : [
    inc_freedreno,
    inc_include,
    inc_src,
  ],
  c_args : [no_override_init_args],
  gnu_symbol_visibility : 'hidden',
  dependencies : [],
  build_by_default : false,
)
