# Copyright © 2019 Intel Corporation
# SPDX-License-Identifier: MIT

overlay_shaders = [
  'overlay.frag',
  'overlay.vert',
]
overlay_spv = []
foreach s : ['overlay.frag', 'overlay.vert']
  _name = f'@s@.spv.h'
  overlay_spv += custom_target(
    _name,
    input : s,
    output : _name,
    command : [
      prog_glslang, '-V', '-x', '-o', '@OUTPUT@', '@INPUT@', glslang_quiet,
      glslang_depfile,
    ],
    depfile : f'@_name@.d',
  )
endforeach

vklayer_files = files(
  'overlay.cpp',
  'overlay_params.c',
)

vklayer_mesa_overlay = shared_library(
  'VkLayer_MESA_overlay',
  vklayer_files, overlay_spv, sha1_h,
  c_args : [no_override_init_args],
  gnu_symbol_visibility : 'hidden',
  dependencies : [idep_vulkan_util, idep_mesautil, vulkan_wsi_deps, libimgui_core_dep, dep_dl],
  include_directories : [inc_include, inc_src],
  link_args : cc.get_supported_link_arguments(['-Wl,-Bsymbolic-functions', '-Wl,-z,relro']),
  install : true
)

install_data(
  files('VkLayer_MESA_overlay.json'),
  install_dir : join_paths(get_option('datadir'), 'vulkan', 'explicit_layer.d'),
  install_tag : 'runtime',
)

install_data(
  'mesa-overlay-control.py',
  install_dir : get_option('bindir'),
  install_mode : 'r-xr-xr-x',
)
