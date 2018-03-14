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

sid_tables_h = custom_target(
  'sid_tables_h',
  input : ['sid_tables.py', 'sid.h', 'gfx9d.h'],
  output : 'sid_tables.h',
  command : [prog_python2, '@INPUT@'],
  capture : true,
)

amd_common_files = files(
  'ac_binary.c',
  'ac_binary.h',
  'ac_exp_param.h',
  'ac_llvm_build.c',
  'ac_llvm_build.h',
  'ac_llvm_helper.cpp',
  'ac_llvm_util.c',
  'ac_llvm_util.h',
  'ac_shader_abi.h',
  'ac_shader_util.c',
  'ac_shader_util.h',
  'ac_nir_to_llvm.c',
  'ac_nir_to_llvm.h',
  'ac_gpu_info.c',
  'ac_gpu_info.h',
  'ac_surface.c',
  'ac_surface.h',
  'ac_debug.c',
  'ac_debug.h',
)

libamd_common = static_library(
  'amd_common',
  [amd_common_files, sid_tables_h],
  include_directories : [
    inc_common, inc_compiler, inc_mesa, inc_mapi, inc_amd,
  ],
  dependencies : [
    dep_llvm, dep_thread, dep_elf, dep_libdrm_amdgpu, dep_valgrind,
    idep_nir_headers,
  ],
  c_args : [c_vis_args],
  cpp_args : [cpp_vis_args],
)
