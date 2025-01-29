# Copyright © 2022 Konstantin Seurer

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

# source file, output name, defines
bvh_shaders = [
  [
    'encode.comp',
    'encode',
    [],
  ],
  [
    'header.comp',
    'header',
    [],
  ],
  [
    'copy.comp',
    'copy',
    []
  ],
]

tu_bvh_include_dir = dir_source_root + '/src/freedreno/vulkan/bvh'

tu_bvh_includes = files(
  'tu_build_helpers.h',
  'tu_build_interface.h',
  'tu_bvh.h',
)

bvh_spv = []
foreach s : bvh_shaders
  command = [
    prog_glslang, '-V', '-I' + vk_bvh_include_dir, '-I' + tu_bvh_include_dir, '--target-env', 'spirv1.5', '-x', '-o', '@OUTPUT@', '@INPUT@'
  ]
  command += glslang_quiet

  foreach define : s[2]
    command += '-D' + define
  endforeach

  bvh_spv += custom_target(
    s[1] + '.spv.h',
    input : s[0],
    output : s[1] + '.spv.h',
    command : command,
    depend_files: [vk_bvh_includes, tu_bvh_includes], 
  )
endforeach
