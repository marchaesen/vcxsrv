# Copyright © 2017 Intel Corporation
# SPDX-License-Identifier: MIT

libvirglvtest = static_library(
  'virglvtest',
  ['virgl_vtest_socket.c', 'virgl_vtest_winsys.c'],
  include_directories : [inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_gallium_aux, inc_gallium_drivers, inc_virtio],
  dependencies : [dep_libvirglcommon, idep_mesautil],
  gnu_symbol_visibility : 'hidden',
)
