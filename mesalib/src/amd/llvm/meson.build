# Copyright © 2019 Valve Corporation
# SPDX-License-Identifier: MIT

amd_common_llvm_files = files(
  'ac_llvm_build.c',
  'ac_llvm_build.h',
  'ac_llvm_helper.cpp',
  'ac_llvm_util.c',
  'ac_llvm_util.h',
  'ac_nir_to_llvm.c',
  'ac_nir_to_llvm.h',
  'ac_shader_abi.h',
)

libamd_common_llvm = static_library(
  'amd_common_llvm',
  [amd_common_llvm_files],
  include_directories : [
    inc_include, inc_src, inc_amd, inc_amd_common
  ],
  link_with: [
    libamd_common
  ],
  dependencies : [
    dep_llvm, dep_thread, dep_elf, dep_libdrm_amdgpu, dep_valgrind,
    idep_nir_headers, idep_amdgfxregs_h,
  ],
  gnu_symbol_visibility : 'hidden',
)

