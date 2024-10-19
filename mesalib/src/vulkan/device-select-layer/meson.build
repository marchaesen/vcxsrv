# Copyright © 2019 Intel Corporation
# SPDX-License-Identifier: MIT

vklayer_files = files(
  'device_select_layer.c',
)

vklayer_deps = []
vklayer_flags = []

if with_platform_x11
   vklayer_files += files('device_select_x11.c')
   vklayer_deps += dep_xcb_dri3
endif

if with_platform_wayland
   vklayer_files += files('device_select_wayland.c')
   vklayer_files += [ wayland_drm_client_protocol_h, wayland_drm_protocol_c ]
   vklayer_deps += dep_wayland_client
endif

vklayer_mesa_device_select = shared_library(
  'VkLayer_MESA_device_select',
  vklayer_files,
  c_args : [no_override_init_args, vklayer_flags],
  gnu_symbol_visibility : 'hidden',
  dependencies : [
    idep_vulkan_util, idep_vulkan_wsi_headers, idep_mesautil, vklayer_deps,
    dep_libdrm, dep_dl
  ],
  include_directories : [inc_include, inc_util, inc_src],
  link_args : cc.get_supported_link_arguments(['-Wl,-Bsymbolic-functions', '-Wl,-z,relro']),
  install : true
)

install_data(
  files('VkLayer_MESA_device_select.json'),
  install_dir : join_paths(get_option('datadir'), 'vulkan', 'implicit_layer.d'),
  install_tag : 'runtime',
)
