# Copyright © 2017 Broadcom
# SPDX-License-Identifier: MIT

inc_broadcom = include_directories('.', 'cle')

subdir('cle')

v3d_versions = ['42', '71']
v3d_libs = []

dep_v3d_hw = dependency('v3d_hw', required: false)
with_v3d_hw = dep_v3d_hw.found()
v3d_simulator_arg = '-DUSE_V3D_SIMULATOR=@0@'.format(with_v3d_hw.to_int())

if with_gallium_v3d or with_broadcom_vk
  subdir('compiler')
  subdir('qpu')
endif

if with_v3d_hw
  subdir('simulator')
endif

if with_tools.contains('drm-shim')
  subdir('drm-shim')
endif

per_version_libs = []
foreach ver : v3d_versions
  per_version_libs += static_library(
    'broadcom-v' + ver,
    [
      files('clif/v3dx_dump.c'),
      v3d_xml_pack
    ],
    include_directories : [inc_include, inc_src, inc_broadcom],
    c_args : [no_override_init_args, '-DV3D_VERSION=' + ver],
    gnu_symbol_visibility : 'hidden',
    dependencies: [dep_valgrind, dep_thread],
  )
endforeach

v3d_args = ['-DV3D_BUILD_NEON']

v3d_neon_c_args = []
if host_machine.cpu_family() == 'arm'
    v3d_neon_c_args = '-mfpu=neon'
endif

libv3d_neon = static_library(
  'v3d_neon',
  'common/v3d_tiling.c',
  include_directories : [
    inc_src, inc_include, inc_broadcom,
  ],
  c_args : [v3d_args, v3d_neon_c_args],
  gnu_symbol_visibility : 'hidden',
  dependencies : [dep_libdrm, dep_valgrind, idep_nir_headers, idep_mesautil],
)

libbroadcom_v3d = static_library(
  'broadcom_v3d',
  [
    files('common/v3d_debug.c', 'common/v3d_device_info.c', 'clif/clif_dump.c', 'common/v3d_util.c'),
    v3d_xml_pack,
  ],
  include_directories : [inc_include, inc_src, inc_broadcom],
  c_args : [no_override_init_args],
  gnu_symbol_visibility : 'hidden',
  link_whole : v3d_libs + per_version_libs,
  link_with: [libv3d_neon],
  build_by_default : false,
  dependencies: [dep_valgrind, dep_thread, idep_mesautil],
)

if with_broadcom_vk
  subdir('vulkan')
endif
