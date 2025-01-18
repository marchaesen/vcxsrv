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
    'lbvh_generate_ir.comp',
    'lbvh_generate_ir',
    [],
  ],
  [
    'lbvh_main.comp',
    'lbvh_main',
    [],
  ],
  [
    'leaf.comp',
    'leaf',
    ['ALWAYS_ACTIVE=0'],
  ],
  [
    'leaf.comp',
    'leaf_always_active',
    ['ALWAYS_ACTIVE=1'],
  ],
  [
    'morton.comp',
    'morton',
    [],
  ],
  [
    'ploc_internal.comp',
    'ploc_internal',
    [],
  ],
  [
    'update.comp',
    'update',
    [],
  ],
]

bvh_include_dir = dir_source_root + '/src/amd/vulkan/bvh'

bvh_includes = files(
  'build_helpers.h',
  'build_interface.h',
  'bvh.h',
)

bvh_spv = []
foreach s : bvh_shaders
  command = [
    prog_glslang, '-V', '-I' + bvh_include_dir, '--target-env', 'spirv1.5',
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
