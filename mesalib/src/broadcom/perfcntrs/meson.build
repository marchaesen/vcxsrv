# Copyright © 2024 Raspberry Pi Ltd
# SPDX-License-Identifier: MIT

libbroadcom_perfcntrs_files = files(
  'v3d_perfcntrs.c',
)

files_per_version = files(
    'v3dx_counter.c',
)

per_version_libs = []
foreach ver : v3d_versions
  per_version_libs += static_library(
    'v3d-perfcntrs-v' + ver,
    [files_per_version, v3d_xml_pack],
    include_directories : [
      inc_src, inc_include, inc_broadcom,
    ],
    c_args : [v3d_simulator_arg, '-DV3D_VERSION=' + ver],
    gnu_symbol_visibility: 'hidden',
    dependencies : [dep_v3d_hw, dep_libdrm, dep_valgrind, idep_mesautil],
)
endforeach

libbroadcom_perfcntrs = static_library(
  'broadcom_perfcntrs',
  [libbroadcom_perfcntrs_files],
  include_directories : [inc_src, inc_include, inc_broadcom],
  c_args : [no_override_init_args],
  gnu_symbol_visibility : 'hidden',
  dependencies : [dep_v3d_hw, dep_libdrm, dep_valgrind, idep_mesautil],
  link_with : [per_version_libs],
  build_by_default : false,
)

idep_broadcom_perfcntrs = declare_dependency(
  link_with: [
    libbroadcom_perfcntrs,
  ],
  include_directories: [
    inc_src,
  ],
)

v3d_libs += libbroadcom_perfcntrs
