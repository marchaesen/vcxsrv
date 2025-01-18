# Copyright © 2017 Intel Corporation
# SPDX-License-Identifier: MIT

files_libvirgl = files(
  'virgl_buffer.c',
  'virgl_context.c',
  'virgl_encode.c',
  'virgl_query.c',
  'virgl_resource.c',
  'virgl_screen.c',
  'virgl_staging_mgr.c',
  'virgl_streamout.c',
  'virgl_transfer_queue.c',
  'virgl_texture.c',
  'virgl_tgsi.c',
  'virgl_video.c',
)

libvirgl = static_library(
  'virgl',
  [ files_libvirgl ],
  gnu_symbol_visibility : 'hidden',
  include_directories : [inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_gallium_aux, inc_virtio],
  dependencies : [dep_libdrm, idep_mesautil, idep_xmlconfig, idep_nir],
)

virgl_deps = [libvirgl]
if not with_platform_windows
  virgl_deps += libvirgldrm
  virgl_deps += libvirglvtest
endif

driver_virgl = declare_dependency(
  compile_args : '-DGALLIUM_VIRGL',
  link_with : virgl_deps,
)

if with_tests
  subdir('tests')
endif
