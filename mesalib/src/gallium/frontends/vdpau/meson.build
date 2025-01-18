# Copyright © 2017, 2019 Intel Corproration
# SPDX-License-Identifier: MIT

VDPAU_MAJOR = 1
VDPAU_MINOR = 0

libvdpau_st = static_library(
  'vdpau_st',
  files(
    'bitmap.c', 'decode.c', 'device.c', 'ftab.c', 'htab.c', 'mixer.c',
    'output.c', 'preemption.c', 'presentation.c', 'query.c', 'surface.c',
  ),
  c_args : [
    '-DVER_MAJOR=@0@'.format(VDPAU_MAJOR),
    '-DVER_MINOR=@0@'.format(VDPAU_MINOR),
  ],
  gnu_symbol_visibility : 'hidden',
  include_directories : [
    inc_include, inc_src, inc_util, inc_gallium, inc_gallium_aux,
  ],
  dependencies : [dep_vdpau, dep_xcb, dep_x11_xcb, dep_xcb_dri2, dep_libdrm, idep_mesautil],
)
