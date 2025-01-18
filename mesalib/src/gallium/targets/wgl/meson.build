# Copyright © Microsoft Corporation
# SPDX-License-Identifier: MIT

gallium_wgl_link_args = []

wgl_def = custom_target(
  'gallium_wgl.def',
  input: 'gallium_wgl.def.in',
  output : 'gallium_wgl.def',
  command : gen_vs_module_defs_normal_command,
)

gallium_wgl_name = get_option('gallium-wgl-dll-name')
libgallium_wgl = shared_library(
  gallium_wgl_name,
  ['wgl.c'],
  vs_module_defs : wgl_def,
  include_directories : [
    inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_gallium_aux, inc_wgl, inc_gallium_winsys, inc_gallium_winsys_sw, inc_gallium_drivers,
  ],
  link_whole : [libwgl],
  link_with : [
    libgallium, libglsl, libmesa, libwsgdi, libglapi_static, libglapi, libgalliumvl_stub,
  ],
  dependencies : [
    dep_ws2_32, idep_nir, idep_mesautil, driver_swrast,
    driver_d3d12, driver_zink, winsys_d3d12_wgl
  ],
  link_args : gallium_wgl_link_args,
  name_prefix : '',
  install : true,
)
libgallium_wgl_build_dir = meson.current_build_dir()
