# Copyright © 2017 Intel Corporation
# Copyright © 2018 Alyssa Rosenzweig
# SPDX-License-Identifier: MIT

files_panfrost = files(
  'driinfo_panfrost.h',
  'pan_afbc_cso.c',
  'pan_bo.c',
  'pan_device.c',
  'pan_disk_cache.c',
  'pan_fence.c',
  'pan_helpers.c',
  'pan_public.h',
  'pan_screen.c',
  'pan_screen.h',
  'pan_resource.c',
  'pan_resource.h',
  'pan_context.c',
  'pan_blit.c',
  'pan_job.c',
  'pan_shader.c',
  'pan_mempool.c',
  'pan_mempool.h',
  'pan_nir_remove_fragcolor_stores.c',
  'pan_nir_lower_sysvals.c',
  'pan_nir_lower_res_indices.c',
)

panfrost_includes = [
  inc_mapi,
  inc_mesa,
  inc_gallium,
  inc_gallium_aux,
  inc_include,
  inc_src,
  inc_panfrost,
]

compile_args_panfrost = [
  '-DGALLIUM_PANFROST',
  '-Wno-pointer-arith'
]

panfrost_versions = ['4', '5', '6', '7', '9', '10']
libpanfrost_versions = []

foreach ver : panfrost_versions
  files_panfrost_vx = [
    'pan_cmdstream.c',
    'pan_fb_preload.c',
    pan_packers,
  ]
  if ver in ['4', '5', '6', '7', '9']
    files_panfrost_vx += ['pan_jm.c']
  elif ver in ['10']
    files_panfrost_vx += ['pan_csf.c']
  endif
  libpanfrost_versions += static_library(
    'panfrost-v' + ver, files_panfrost_vx,
    include_directories : panfrost_includes,
    c_args : ['-DPAN_ARCH=' + ver],
    gnu_symbol_visibility : 'hidden',
    dependencies : [idep_pan_packers, idep_nir, dep_libdrm, idep_mesautil],
)
endforeach

libpanfrost = static_library(
  'panfrost',
  files_panfrost,
  dependencies: [
    dep_thread,
    dep_libdrm,
    idep_mesautil,
    idep_nir,
    idep_pan_packers
  ],
  include_directories : panfrost_includes,
  c_args : [c_msvc_compat_args, compile_args_panfrost],
  gnu_symbol_visibility : 'hidden',
  link_with: [libpanfrost_versions],
)

driver_panfrost = declare_dependency(
  compile_args : compile_args_panfrost,
  link_with : [libpanfrost, libpanfrostwinsys, libpanfrost_shared, libpanfrost_midgard, libpanfrost_bifrost, libpanfrost_decode, libpanfrost_lib],
)
