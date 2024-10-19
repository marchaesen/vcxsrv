# Copyright © Microsoft Corporation
# SPDX-License-Identifier: MIT

files_libd3d12winsys = files('d3d12_wgl_winsys.c')
if target_machine.system().startswith('Gaming.Xbox')
  files_libd3d12winsys += files('d3d12_wgl_framebuffer_xbox.cpp')
else
  files_libd3d12winsys += files('d3d12_wgl_framebuffer.cpp')
endif

libd3d12winsys = static_library(
  'd3d12winsys',
  files_libd3d12winsys,
  include_directories : [inc_src, inc_wgl, inc_include, inc_gallium, inc_gallium_aux, inc_gallium_drivers],
  dependencies : [dep_dxheaders, idep_nir_headers, idep_libdxil_compiler, idep_mesautil],
  gnu_symbol_visibility : 'hidden',
)

winsys_d3d12_wgl = declare_dependency(
  link_with : [libd3d12winsys],
)
