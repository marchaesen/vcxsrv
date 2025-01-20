# Copyright 2024 Valve Corporation
# SPDX-License-Identifier: MIT

libagx_shader_files = files(
  'compression.cl',
  'draws.cl',
  'geometry.cl',
  'query.cl',
  'tessellation.cl',
  'tessellator.cl',
  'texture.cl',
  'helper.cl',
)

prepended_input_args = []
foreach input_arg : libagx_shader_files
  prepended_input_args += ['--in', input_arg]
endforeach

# We need to set -fmacro-prefix-map properly for reproducability.
fs = import('fs')
relative_dir = fs.relative_to(meson.global_source_root(), meson.global_build_root()) + '/'

libagx_spv = custom_target(
  'libagx.spv',
  input : libagx_shader_files,
  output : 'libagx.spv',
  command : [
    prog_mesa_clc,
    prepended_input_args, '-o', '@OUTPUT@', '--depfile', '@DEPFILE@', '--',
    '-cl-std=cl2.0', '-D__OPENCL_VERSION__=200',
    '-I' + join_paths(meson.current_source_dir(), '.'),
    '-I' + join_paths(meson.current_source_dir(), '../../'),
    '-I' + join_paths(meson.current_source_dir(), 'shaders'),
    '-I' + join_paths(meson.current_build_dir(), '../genxml'),
    '-fmacro-prefix-map=@0@='.format(relative_dir),
    ],
  env: ['MESA_SHADER_CACHE_DISABLE=true'],
  depends : [prog_mesa_clc, agx_pack],
  depfile : 'libagx_shaders.h.d',
)

libagx_h = custom_target(
  'libagx.h',
  input : libagx_spv,
  output : 'libagx.h',
  command : [prog_vtn_bindgen, libagx_spv, '@OUTPUT@'],
  depends : [prog_vtn_bindgen],
)

idep_libagx = declare_dependency(
  sources : [libagx_h],
  include_directories : include_directories('.'),
)
