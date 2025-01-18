# Copyright © 2024 Raspberry Pi Ltd
# Copyright © 2021 Collabora, Ltd.
# Copyright © 2021 Google, Inc
# SPDX-License-Identifier: MIT

pps_broadcom_lib = static_library(
  'broadcom-dds',
  sources: [
    'v3d_pps_driver.cc',
    'v3d_pps_driver.h',
  ],
  include_directories: [
    inc_tool,
    inc_broadcom,
    inc_include,
  ],
  dependencies: [
    dep_libdrm,
    dep_perfetto,
    idep_broadcom_perfcntrs,
  ],
  c_args: [no_override_init_args],
)

pps_broadcom_dep = declare_dependency(
  link_with: [
    pps_broadcom_lib,
    libbroadcom_v3d,
  ],
  include_directories: [
    inc_tool,
  ],
  compile_args: [
    '-DPPS_V3D',
  ],
)

pps_datasources += pps_broadcom_dep
pps_includes += [
  inc_include,
  inc_broadcom,
]
