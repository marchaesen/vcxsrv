# Copyright © 2020 Igalia S.L
# SPDX-License-Identifier: MIT

freedreno_devices_h = custom_target(
  'freedreno_devices.h',
  input: 'freedreno_devices.py',
  output: 'freedreno_devices.h',
  command: [
    prog_python, '@INPUT@',
    '-p', join_paths(meson.project_build_root(), 'src/freedreno/registers/adreno/')
  ],
  depends: freedreno_py_header_files,
  capture: true,
)

libfreedreno_common = static_library(
  'freedreno_common',
  [
    'disasm.h',
    'fd6_pack.h',
    'freedreno_dev_info.c',
    'freedreno_dev_info.h',
    'freedreno_pm4.h',
    'freedreno_rd_output.c',
    'freedreno_rd_output.h',
    'freedreno_uuid.c',
    'freedreno_uuid.h',
    'freedreno_guardband.h',
    freedreno_devices_h,
    sha1_h,
    'redump.h'
  ],
  include_directories : [inc_freedreno, inc_include, inc_src],
  c_args : [no_override_init_args],
  build_by_default : true,
  dependencies: [idep_mesautil],
  gnu_symbol_visibility : 'hidden',
)

idep_libfreedreno_common = declare_dependency(
  link_with: [libfreedreno_common],
  include_directories : [inc_freedreno, inc_include, inc_src],
)
