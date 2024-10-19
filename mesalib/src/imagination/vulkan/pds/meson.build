# Copyright © 2022 Imagination Technologies Ltd.
# SPDX-License-Identifier: MIT

libpowervr_pds_files = files(
  'pvr_pds.c',
  'pvr_pds_disasm.c',
  'pvr_pds_printer.c',
  'pvr_pipeline_pds.c',
)

libpowervr_pds_includes = include_directories(
  '..',
  '.',
  'pvr_pds_programs',
)

libpowervr_pds = static_library(
  'pvr_pds',
  [libpowervr_pds_files],
  include_directories : [
    libpowervr_pds_includes,
    inc_include,
    inc_src,
    inc_imagination,
  ],
  c_args : [
    imagination_c_args,
    no_override_init_args,
  ],
  gnu_symbol_visibility : 'hidden',
  pic : true,
)
