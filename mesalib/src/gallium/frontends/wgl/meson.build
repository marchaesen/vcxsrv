# Copyright © 2018 Intel Corporation
# SPDX-License-Identifier: MIT

inc_wgl = include_directories('.')
_c_args_wgl = []
if not with_shared_glapi
  # prevent _glapi_* from being declared __declspec(dllimport)
  _c_args_wgl += '-D_GLAPI_NO_EXPORTS'
endif

_wgl_deps = [idep_xmlconfig, driver_zink, idep_mesautilformat]
if with_gallium_zink
  _wgl_deps += idep_vulkan_wsi_defines
endif

files_libwgl = files(
  'stw_context.c',
  'stw_device.c',
  'stw_ext_context.c',
  'stw_ext_extensionsstring.c',
  'stw_ext_interop.c',
  'stw_ext_pbuffer.c',
  'stw_ext_pixelformat.c',
  'stw_ext_rendertexture.c',
  'stw_ext_swapinterval.c',
  'stw_framebuffer.c',
  'stw_getprocaddress.c',
  'stw_image.c',
  'stw_nopfuncs.c',
  'stw_nopfuncs.h',
  'stw_pixelformat.c',
  'stw_st.c',
  'stw_tls.c',
)

if target_machine.system().startswith('Gaming.Xbox')
  files_libwgl += files('stw_gdishim.c')
endif

libwgl = static_library(
  'wgl',
  files_libwgl,
  c_args : [
    '-D_GDI32_',             # prevent wgl* being declared __declspec(dllimport)
    _c_args_wgl
  ],
  include_directories : [
    inc_include, inc_src, inc_gallium, inc_gallium_aux, inc_mapi, inc_mesa,
  ],
  dependencies : _wgl_deps,
)
