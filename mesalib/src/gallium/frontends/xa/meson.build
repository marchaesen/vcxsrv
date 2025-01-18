# Copyright © 2017 Intel Corporation
# SPDX-License-Identifier: MIT

xa_version = ['2', '5', '0']

xa_conf = configuration_data()
xa_conf.set('XA_MAJOR', xa_version[0])
xa_conf.set('XA_MINOR', xa_version[1])
xa_conf.set('XA_PATCH', xa_version[2])

xa_tracker_h = configure_file(
  configuration : xa_conf,
  input : 'xa_tracker.h.in',
  output : 'xa_tracker.h',
  install_dir : get_option('includedir'),
)

libxa_st = static_library(
  'xa_st',
  [xa_tracker_h, files(
    'xa_composite.c', 'xa_context.c', 'xa_renderer.c', 'xa_tgsi.c',
    'xa_tracker.c', 'xa_yuv.c',
  )],
  c_args : ['-pedantic'],
  gnu_symbol_visibility : 'hidden',
  dependencies : idep_mesautil,
  include_directories : [inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_gallium_aux],
)

install_headers('xa_composite.h', 'xa_context.h')
