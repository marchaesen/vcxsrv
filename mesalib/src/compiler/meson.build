# Copyright © 2017 Intel Corporation
# SPDX-License-Identifier: MIT

inc_compiler = include_directories('.')
inc_glsl = include_directories('glsl')
inc_spirv = include_directories('spirv')

builtin_types_h = custom_target(
  'builtin_types.h',
  input : 'builtin_types_h.py',
  output : 'builtin_types.h',
  command : [prog_python, '@INPUT@', '@OUTPUT@'],
  depend_files : files('builtin_types.py'),
  depends : u_format_gen_h,
)

builtin_types_c = custom_target(
  'builtin_types.c',
  input : 'builtin_types_c.py',
  output : 'builtin_types.c',
  command : [prog_python, '@INPUT@', '@OUTPUT@'],
  depend_files : files('builtin_types.py'),
)

float64_glsl_file = files('glsl/float64.glsl')

astc_decoder_glsl_file = files('glsl/astc_decoder.glsl')

files_libcompiler = files(
  'glsl_types.c',
  'glsl_types.h',
  'shader_enums.c',
  'shader_enums.h',
  'shader_info.h',
)

ir_expression_operation_h = custom_target(
  'ir_expression_operation.h',
  input : 'glsl/ir_expression_operation.py',
  output : 'ir_expression_operation.h',
  command : [prog_python, '@INPUT@', 'enum'],
  capture : true,
)

libcompiler = static_library(
  'compiler',
  [files_libcompiler, ir_expression_operation_h, builtin_types_h, builtin_types_c],
  include_directories : [inc_compiler, inc_include, inc_src],
  c_args : [c_msvc_compat_args, no_override_init_args],
  cpp_args : [cpp_msvc_compat_args],
  gnu_symbol_visibility : 'hidden',
  dependencies : [dep_valgrind],
  build_by_default : false,
)

idep_compiler = declare_dependency(
  sources : [ir_expression_operation_h, builtin_types_h],
  include_directories : inc_compiler,
  link_with : libcompiler,
)

subdir('nir')

subdir('spirv')

if with_clc
  subdir('clc')
endif
if with_gallium
  subdir('glsl')
endif
subdir('isaspec')

if with_nouveau_vk
  subdir('rust')
endif
