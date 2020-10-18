# Copyright © 2017 Intel Corporation

# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:

# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

inc_compiler = include_directories('.')
inc_glsl = include_directories('glsl')
inc_spirv = include_directories('spirv')

files_libcompiler = files(
  'builtin_type_macros.h',
  'glsl_types.cpp',
  'glsl_types.h',
  'nir_types.cpp',
  'nir_types.h',
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
  [files_libcompiler, ir_expression_operation_h],
  include_directories : [inc_mapi, inc_mesa, inc_compiler, inc_include, inc_src, inc_gallium, inc_gallium_aux],
  c_args : [c_msvc_compat_args, no_override_init_args],
  cpp_args : [cpp_msvc_compat_args],
  gnu_symbol_visibility : 'hidden',
  dependencies : [dep_valgrind],
  build_by_default : false,
)

subdir('spirv')

subdir('nir')

# These are here because putting it in spirv/meson.build would create a circular
# dependency with nir/meson.build.
spirv2nir = executable(
  'spirv2nir',
  files('spirv/spirv2nir.c'),
  dependencies : [dep_m, idep_nir, idep_mesautil],
  include_directories : [inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_gallium_aux, include_directories('spirv')],
  c_args : [c_msvc_compat_args, no_override_init_args],
  gnu_symbol_visibility : 'hidden',
  build_by_default : with_tools.contains('nir'),
  install : with_tools.contains('nir'),
)

if with_tests
  test(
    'avail_vis',
    executable(
      'avail_vis',
      files('spirv/tests/avail_vis.cpp'),
      c_args : [c_msvc_compat_args, no_override_init_args],
      gnu_symbol_visibility : 'hidden',
      include_directories : [inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_gallium_aux],
      dependencies : [dep_thread, idep_gtest, idep_nir, idep_mesautil],
    ),
    suite : ['compiler', 'spirv'],
  )

  test(
    'volatile',
    executable(
      'volatile',
      files('spirv/tests/volatile.cpp'),
      c_args : [c_msvc_compat_args, no_override_init_args],
      gnu_symbol_visibility : 'hidden',
      include_directories : [inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_gallium_aux],
      dependencies : [dep_thread, idep_gtest, idep_nir, idep_mesautil],
    ),
    suite : ['compiler', 'spirv'],
  )
endif

subdir('glsl')
