# Copyright © Microsoft Corporation

# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:

# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.

# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
# IN THE SOFTWARE.

clang_ver_dir = dep_clang.version()
if clang_ver_dir.endswith('git')
  clang_ver_dir = clang_ver_dir.substring(0, -3)
endif

clang_resource_dir = join_paths(
  dep_clang.get_variable(cmake: 'CLANG_INCLUDE_DIRS'), '..',
  'lib', 'clang', clang_ver_dir, 'include'
)

opencl_c_h = custom_target(
  'opencl-c.h',
  input : [files_xxd, join_paths(clang_resource_dir, 'opencl-c.h')],
  output : 'opencl-c.h.h',
  command : [prog_python, '@INPUT@', '@OUTPUT@', '-n', 'opencl_c_source'],
)
opencl_c_base_h = custom_target(
  'opencl-c-base.h',
  input : [files_xxd, join_paths(clang_resource_dir, 'opencl-c-base.h')],
  output : 'opencl-c-base.h.h',
  command : [prog_python, '@INPUT@', '@OUTPUT@', '-n', 'opencl_c_base_source'],
)

files_libclc_compiler = files(
  'clc_compiler.c',
  'clc_nir.c',
  'clc_helpers.cpp'
)

libclc_compiler = shared_library(
  'clglon12compiler',
  [files_libclc_compiler, sha1_h],
  opencl_c_h,
  opencl_c_base_h,
  vs_module_defs : 'clglon12compiler.def',
  include_directories : [inc_include, inc_src, inc_mapi, inc_mesa, inc_compiler, inc_gallium, inc_spirv],
  dependencies: [idep_nir_headers, dep_clang, dep_llvm, cc.find_library('version'),
    dep_llvmspirvlib, idep_mesautil, idep_libdxil_compiler, idep_nir, dep_spirv_tools]
)

if dep_dxheaders.found()
  clc_compiler_test = executable('clc_compiler_test',
    ['clc_compiler_test.cpp', 'compute_test.cpp'],
    link_with : [libclc_compiler],
    dependencies : [idep_gtest, idep_mesautil, idep_libdxil_compiler, dep_dxheaders],
    include_directories : [inc_include, inc_src])

  test('clc_compiler_test', clc_compiler_test, timeout: 180)
endif
