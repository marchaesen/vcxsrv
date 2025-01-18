# Copyright © 2017-2019 Intel Corporation
# SPDX-License-Identifier: MIT

libva_version = dep_va.version().split('.')

libva_st = static_library(
  'va_st',
  files(
    'buffer.c', 'config.c', 'context.c', 'display.c', 'image.c', 'picture.c',
    'picture_mpeg12.c', 'picture_mpeg4.c', 'picture_h264.c', 'picture_hevc.c',
    'picture_vc1.c', 'picture_mjpeg.c', 'picture_vp9.c','picture_av1.c','postproc.c',
    'subpicture.c', 'surface.c', 'picture_h264_enc.c', 'picture_hevc_enc.c',
    'picture_av1_enc.c',
  ),
  c_args : [
    '-DVA_DRIVER_INIT_FUNC=__vaDriverInit_@0@_@1@'.format(
      libva_version[0], libva_version[1]
    ),
  ],
  gnu_symbol_visibility : 'hidden',
  include_directories : [inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_gallium_aux],
  dependencies : [
    dep_va_headers, dep_x11_xcb, dep_xcb, dep_xcb_dri2, dep_xcb_dri3,
    dep_libdrm, idep_mesautil,
  ],
)
