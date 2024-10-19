# Copyright © 2018 NVIDIA CORPORATION
# SPDX-License-Identifier: MIT

files_tegra = files(
  'tegra_context.c',
  'tegra_context.h',
  'tegra_resource.h',
  'tegra_screen.c',
)

libtegra = static_library(
  'tegra',
  files_tegra,
  gnu_symbol_visibility : 'hidden',
  include_directories : [
    inc_include, inc_src, inc_gallium, inc_gallium_aux, inc_gallium_drivers,
    inc_gallium_winsys,
  ],
  dependencies : [dep_libdrm, idep_mesautil],
)

driver_tegra = declare_dependency(
  compile_args : '-DGALLIUM_TEGRA',
  link_with : [libtegra, libtegradrm],
)
