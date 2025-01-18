# Copyright © 2017 Dylan Baker
# SPDX-License-Identifier: MIT

libamdgpuwinsys = static_library(
  'amdgpuwinsys',
  files(
    'amdgpu_bo.c', 'amdgpu_bo.h', 'amdgpu_cs.cpp', 'amdgpu_cs.h',
    'amdgpu_surface.c', 'amdgpu_winsys.c',
    'amdgpu_winsys.h',
  ),
  include_directories : [
    inc_amd, inc_gallium, inc_gallium_aux, inc_include, inc_src,
    inc_amd_common, inc_amd_common_llvm,
  ],
  gnu_symbol_visibility : 'hidden',
  link_with : libamdgpu_addrlib,
  dependencies : [dep_llvm, dep_libdrm_amdgpu, idep_amdgfxregs_h, idep_mesautil],
)
