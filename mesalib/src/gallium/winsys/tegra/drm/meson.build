# Copyright © 2018 NVIDIA CORPORATION
# SPDX-License-Identifier: MIT

libtegradrm = static_library(
  'tegradrm',
  'tegra_drm_winsys.c',
  include_directories : [
    inc_include, inc_src, inc_gallium, inc_gallium_aux, inc_gallium_drivers,
    inc_gallium_winsys
  ],
  dependencies : idep_mesautil,
)

driver_tegra = declare_dependency(
  compile_args : '-DGALLIUM_TEGRA',
  link_with : libtegradrm,
)
