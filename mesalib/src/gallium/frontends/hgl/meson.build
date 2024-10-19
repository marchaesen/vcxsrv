# Copyright © 2017 Dylan Baker
# SPDX-License-Identifier: MIT

files_libsthgl = files(
  'hgl_context.h',
  'hgl.c',
)

libsthgl = static_library(
  'sthgl',
  files_libsthgl,
  include_directories : [
    inc_include, inc_util, inc_mesa, inc_mapi, inc_src,
    inc_gallium, inc_gallium_aux
  ],
  gnu_symbol_visibility : 'hidden',
)
