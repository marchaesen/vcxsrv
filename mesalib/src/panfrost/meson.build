# Copyright © 2018 Rob Clark
# Copyright © 2019 Collabora
# SPDX-License-Identifier: MIT

inc_panfrost_hw = include_directories([
   'include'
])

inc_panfrost = include_directories([
   '.', 'include', 'shared', 'midgard', 'compiler', 'lib'
])

compile_args_panfrost = [
  '-Wno-pointer-arith'
]

subdir('shared')
subdir('util')
subdir('midgard')
subdir('compiler')

if with_gallium_panfrost or with_panfrost_vk
   subdir('lib')
   subdir('perf')
endif

files_bifrost = files(
  'compiler/cmdline.c',
)

bifrost_compiler = executable(
  'bifrost_compiler',
  [files_bifrost],
  include_directories : [
    inc_include,
    inc_src,
    inc_panfrost,
    inc_panfrost_hw,
 ],
  dependencies : [
    idep_nir,
    idep_mesautil,
    idep_bi_opcodes_h,
    dep_libdrm,
  ],
  link_with : [
    libpanfrost_bifrost,
  ],
  build_by_default : with_tools.contains('panfrost')
)

if with_panfrost_vk
  subdir('vulkan')
endif

if with_perfetto and (with_datasources.contains('panfrost') or with_datasources.contains('auto'))
  subdir('ds')
endif

if with_tools.contains('drm-shim')
  subdir('drm-shim')
endif

if with_tools.contains('panfrost')
   subdir('tools')
endif
