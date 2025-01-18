# Copyright © 2017 Broadcom
# SPDX-License-Identifier: MIT

libbroadcom_qpu_files = files(
  'qpu_disasm.c',
  'qpu_instr.c',
  'qpu_pack.c',
)

libbroadcom_qpu = static_library(
  'broadcom_qpu',
  [libbroadcom_qpu_files, v3d_xml_pack],
  include_directories : [inc_include, inc_src, inc_broadcom],
  c_args : [no_override_init_args],
  gnu_symbol_visibility : 'hidden',
  dependencies : [dep_libdrm, dep_valgrind],
  build_by_default : false,
)

v3d_libs += libbroadcom_qpu

test(
  'qpu_disasm',
  executable(
    'qpu_disasm', 'tests/qpu_disasm.c',
    link_with: libbroadcom_qpu,
    dependencies : idep_mesautil,
    include_directories: [inc_include, inc_src]
  ),
  suite : ['broadcom'],
)
