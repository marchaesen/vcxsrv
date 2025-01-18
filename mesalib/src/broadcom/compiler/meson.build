# Copyright © 2017 Broadcom
# SPDX-License-Identifier: MIT

libbroadcom_compiler_files = files(
  'nir_to_vir.c',
  'vir.c',
  'vir_dump.c',
  'vir_live_variables.c',
  'vir_opt_constant_alu.c',
  'vir_opt_copy_propagate.c',
  'vir_opt_dead_code.c',
  'vir_opt_redundant_flags.c',
  'vir_opt_small_immediates.c',
  'vir_register_allocate.c',
  'vir_to_qpu.c',
  'qpu_schedule.c',
  'qpu_validate.c',
  'v3d_tex.c',
  'v3d_compiler.h',
  'v3d_nir_lower_io.c',
  'v3d_nir_lower_image_load_store.c',
  'v3d_nir_lower_line_smooth.c',
  'v3d_nir_lower_load_store_bitsize.c',
  'v3d_nir_lower_logic_ops.c',
  'v3d_nir_lower_scratch.c',
  'v3d_nir_lower_txf_ms.c',
  'v3d_packing.c',
)

v3d_nir_lower_algebraic_c = custom_target(
  'v3d_nir_lower_algebraic.c',
  input : 'v3d_nir_lower_algebraic.py',
  output : 'v3d_nir_lower_algebraic.c',
  command : [
    prog_python, '@INPUT@', '-p', dir_compiler_nir,
  ],
  capture : true,
  depend_files : nir_algebraic_depends,
)

libbroadcom_compiler = static_library(
  'broadcom_compiler',
  [libbroadcom_compiler_files, v3d_xml_pack, v3d_nir_lower_algebraic_c],
  include_directories : [inc_include, inc_src, inc_gallium, inc_gallium_aux, inc_broadcom],
  c_args : [no_override_init_args],
  gnu_symbol_visibility : 'hidden',
  dependencies : [dep_libdrm, dep_valgrind, idep_nir_headers, idep_mesautil],
  build_by_default : false,
)

v3d_libs += libbroadcom_compiler
