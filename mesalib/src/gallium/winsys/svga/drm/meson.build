# Copyright © 2017 Intel Corporation
# SPDX-License-Identifier: MIT

files_svgadrm = files(
  'pb_buffer_simple_fenced.c',
  'vmw_buffer.c',
  'vmw_context.c',
  'vmw_fence.c',
  'vmw_msg.c',
  'vmw_screen.c',
  'vmw_screen_dri.c',
  'vmw_screen_ioctl.c',
  'vmw_screen_pools.c',
  'vmw_screen_svga.c',
  'vmw_surface.c',
  'vmw_shader.c',
  'vmw_query.c',
)

libsvgadrm = static_library(
  'svgadrm',
  files_svgadrm,
  c_args : [c_msvc_compat_args],
  gnu_symbol_visibility : 'hidden',
  include_directories : [
    inc_src, inc_include, inc_gallium, inc_gallium_aux,
    include_directories('../../../drivers/svga', '../../../drivers/svga/include'),
  ],
  dependencies : [dep_libdrm, idep_mesautil],
)
