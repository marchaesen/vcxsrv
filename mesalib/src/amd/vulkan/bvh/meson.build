# Copyright © 2022 Konstantin Seurer
# SPDX-License-Identifier: MIT

# source file, output name, defines
bvh_shaders = [
  [
    'copy.comp',
    'copy',
    [],
  ],
  [
    'encode.comp',
    'encode',
    ['COMPACT=0'],
  ],
  [
    'encode.comp',
    'encode_compact',
    ['COMPACT=1'],
  ],
  [
    'header.comp',
    'header',
    [],
  ],
  [
    'update.comp',
    'update',
    [],
  ],
  [
    'leaf.comp',
    'radv_leaf',
    ['ALWAYS_ACTIVE=0'],
  ],
  [
    'leaf.comp',
    'radv_leaf_always_active',
    ['ALWAYS_ACTIVE=1'],
  ],
]

bvh_include_dir = dir_source_root + '/src/amd/vulkan/bvh'
vk_bvh_include_dir = dir_source_root + '/src/vulkan/runtime/bvh'

bvh_includes = files(
  'build_helpers.h',
  'build_interface.h',
  'bvh.h',
  vk_bvh_include_dir + '/vk_build_helpers.h',
  vk_bvh_include_dir + '/vk_bvh.h',
)

bvh_spv = []
foreach s : bvh_shaders
  command = [
    prog_glslang, '-V', '-I' + bvh_include_dir, '-I' + vk_bvh_include_dir, '--target-env', 'spirv1.5',
    '-x', '-o', '@OUTPUT@', '@INPUT@', glslang_depfile, glslang_quiet,
  ]

  foreach define : s[2]
    command += '-D' + define
  endforeach

  _bvh_name = '@0@.spv.h'.format(s[1])
  bvh_spv += custom_target(
    _bvh_name,
    input : s[0],
    output : _bvh_name,
    command : command,
    depfile : f'@_bvh_name@.d',
    depend_files: bvh_includes
  )
endforeach
