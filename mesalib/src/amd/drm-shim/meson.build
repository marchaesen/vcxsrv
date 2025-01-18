# Copyright © 2021 Emma Anholt
# SPDX-License-Identifier: MIT

libradeon_noop_drm_shim = shared_library(
  'radeon_noop_drm_shim',
  'radeon_noop_drm_shim.c',
  include_directories: [inc_include, inc_src, inc_amd],
  dependencies: dep_drm_shim,
  gnu_symbol_visibility : 'hidden',
  install : true,
)

libamdgpu_noop_drm_shim = shared_library(
  'amdgpu_noop_drm_shim',
  ['amdgpu_noop_drm_shim.c', 'amdgpu_devices.c'],
  include_directories: [inc_include, inc_src, inc_amd],
  dependencies: dep_drm_shim,
  gnu_symbol_visibility : 'hidden',
  install : true,
)

amdgpu_dump = executable(
  'amdgpu_dump_states',
  'amdgpu_dump_states.c',
  include_directories: [inc_include, inc_src, inc_amd],
  gnu_symbol_visibility: 'hidden',
  dependencies: [dep_libdrm],
  install: false,
)
