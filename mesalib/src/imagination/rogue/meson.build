# Copyright © 2022 Imagination Technologies Ltd.
# SPDX-License-Identifier: MIT

with_imagination_tools = with_tools.contains('imagination')

inc_rogue = include_directories([
  '.',
])

libpowervr_rogue_files = files(
  'nir/rogue_nir_lower_io.c',
  'nir/rogue_nir_pfo.c',

  'rogue.c',
  'rogue_build_data.c',
  'rogue_builder.c',
  'rogue_compile.c',
  'rogue_constreg.c',
  'rogue_debug.c',
  'rogue_encode.c',
  'rogue_info.c',
  'rogue_nir.c',
  'rogue_print.c',
  'rogue_validate.c',

  'passes/rogue_constreg.c',
  'passes/rogue_copy_prop.c',
  'passes/rogue_dce.c',
  'passes/rogue_lower_late_ops.c',
  'passes/rogue_lower_pseudo_ops.c',
  'passes/rogue_regalloc.c',
  'passes/rogue_schedule_instr_groups.c',
  'passes/rogue_schedule_uvsw.c',
  'passes/rogue_schedule_wdf.c',
  'passes/rogue_trim.c',
)

# FIXME: Remove idep_vulkan_runtime once
# the compiler/driver interface is finalised.
libpowervr_rogue = shared_library(
  'powervr_rogue',
  libpowervr_rogue_files,
  include_directories : [
    inc_imagination,
    inc_include,
    inc_src,
  ],
  c_args : [imagination_c_args, no_override_init_args],
  gnu_symbol_visibility : 'hidden',
  dependencies : [idep_mesautil, idep_nir, idep_vulkan_runtime, dep_csbgen],
  install : true,
)

subdir('tools')
