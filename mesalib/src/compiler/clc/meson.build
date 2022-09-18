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

clang_resource_dir = join_paths(llvm_libdir, 'clang', dep_llvm.version(), 'include')

if dep_llvm.version().version_compare('< 15.0')
  opencl_c_h = custom_target(
    'opencl-c.h',
    input : [files_xxd, join_paths(clang_resource_dir, 'opencl-c.h')],
    output : 'opencl-c.h.h',
    command : [prog_python, '@INPUT@', '@OUTPUT@', '-n', 'opencl_c_source'],
  )
endif

opencl_c_base_h = custom_target(
  'opencl-c-base.h',
  input : [files_xxd, join_paths(clang_resource_dir, 'opencl-c-base.h')],
  output : 'opencl-c-base.h.h',
  command : [prog_python, '@INPUT@', '@OUTPUT@', '-n', 'opencl_c_base_source'],
)

_libclc_sources = [opencl_c_base_h]
if dep_llvm.version().version_compare('< 15.0')
  _libclc_sources += [opencl_c_h]
endif

files_libclc = files(
  'clc.c',
  'clc_helpers.cpp',
)

_libclc_cpp_args = ['-DCLANG_RESOURCE_DIR="@0@"'.format(clang_resource_dir)]
if with_microsoft_clc
  _libclc_cpp_args += ['-DUSE_STATIC_OPENCL_C_H=1']
endif

# Supported added for SPIRV 1.4 in a version that required LLVM 14.
if dep_llvm.version().version_compare('>= 14.0')
  _libclc_cpp_args += ['-DHAS_SPIRV_1_4=1']
endif

_libclc = static_library(
  'libclc',
  files_libclc,
  sources: _libclc_sources,
  include_directories : [inc_include, inc_src, inc_mapi, inc_mesa, inc_compiler, inc_spirv],
  cpp_args : _libclc_cpp_args,
  dependencies: [idep_nir_headers, dep_clang, dep_llvm, dep_llvmspirvlib,
                 idep_mesautil, idep_nir, dep_spirv_tools]
)

idep_clc = declare_dependency(
  link_with : _libclc,
  include_directories : include_directories('.'),
)
