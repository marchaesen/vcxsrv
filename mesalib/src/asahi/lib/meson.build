# Copyright 2018 Rob Clark
# Copyright 2019 Collabora, Ltd.
# Copyright 2017 Intel Corporation
# SPDX-License-Identifier: MIT

dep_iokit = dependency('IOKit', required : false)

libasahi_lib_files = files(
  'agx_bo.c',
  'agx_border.c',
  'agx_device.c',
  'agx_device_virtio.c',
  'agx_linker.c',
  'agx_bg_eot.c',
  'agx_tilebuffer.c',
  'agx_nir_lower_alpha.c',
  'agx_nir_lower_gs.c',
  'agx_nir_lower_ia.c',
  'agx_nir_lower_msaa.c',
  'agx_nir_lower_sample_intrinsics.c',
  'agx_nir_lower_tess.c',
  'agx_nir_lower_tilebuffer.c',
  'agx_nir_lower_uvs.c',
  'agx_nir_lower_vbo.c',
  'agx_nir_prolog_epilog.c',
  'agx_ppp.h',
  'agx_scratch.c',
  'agx_va.c',
  'pool.c',
)

libasahi_decode_files = files(
  'decode.c',
)

libasahi_decode = static_library(
  'asahi_decode',
  [libasahi_decode_files, agx_pack],
  dependencies : [dep_valgrind, idep_mesautil],
  c_args : [no_override_init_args, '-Wno-c2x-extensions'],
  gnu_symbol_visibility : 'hidden',
  build_by_default : false,
)

libasahi_decode_shared = shared_library(
  'asahi_decode',
  [libasahi_decode_files, agx_pack],
  dependencies : [dep_valgrind, idep_mesautil],
  c_args : [no_override_init_args, '-Wno-c2x-extensions'],
  build_by_default : with_tools.contains('asahi'),
)

libagx_shaders = custom_target(
  'libagx_shaders',
  input : libagx_spv,
  output : ['libagx_shaders.h', 'libagx_shaders.c'],
  command : [prog_asahi_clc, libagx_spv, '@OUTPUT0@', '@OUTPUT1@'],
  env: ['MESA_SHADER_CACHE_DISABLE=true'],
  depends : [prog_asahi_clc],
)

idep_libagx_shaders_h = declare_dependency(
  sources : [libagx_shaders],
  include_directories : include_directories('.'),
)

libasahi_lib = static_library(
  'asahi_lib',
  [libasahi_lib_files, libagx_shaders, agx_pack, sha1_h],
  include_directories : [inc_asahi, inc_virtio_gpu, inc_virtio_vdrm],
  c_args : [no_override_init_args, '-Wno-c2x-extensions'],
  gnu_symbol_visibility : 'hidden',
  link_with: [libasahi_decode, libvdrm],
  dependencies: [dep_libdrm, dep_valgrind, idep_nir, idep_mesautil, idep_libagx],
  build_by_default : false,
)

if with_tests
  test(
    'libasahi_tests',
    executable(
      'libasahi_tests',
      files(
        'tests/test-packing.cpp',
        'tests/test-tilebuffer.cpp',
      ),
      c_args : [c_msvc_compat_args, no_override_init_args],
      gnu_symbol_visibility : 'hidden',
      dependencies: [idep_gtest, idep_agx_pack, idep_mesautil],
      link_with : [libasahi_lib, libasahi_layout],
    ),
    suite : ['asahi'],
    protocol : 'gtest',
  )
endif

if dep_iokit.found()
  libasahi_wrap = shared_library(
    'wrap',
    'wrap.c',
    include_directories : [inc_src, inc_asahi],
    dependencies : [idep_mesautil, dep_iokit],
    c_args : [no_override_init_args, '-Wno-missing-prototypes'],
    gnu_symbol_visibility : 'hidden',
    build_by_default : with_tools.contains('asahi'),
    link_with: libasahi_decode,
  )
endif
