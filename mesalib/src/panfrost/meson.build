# Copyright © 2018 Rob Clark
# Copyright © 2019 Collabora

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

inc_panfrost_hw = include_directories([
   'include'
])

inc_panfrost = include_directories([
   '.', 'include', 'shared', 'midgard', 'bifrost'
])

subdir('shared')
subdir('midgard')
subdir('bifrost')
subdir('pandecode')

files_pandecode = files(
  'pandecode/cmdline.c',
  'pandecode/common.c',
  'pandecode/decode.c',
  'pandecode/pan_pretty_print.c',

  'midgard/disassemble.c',
  'midgard/midgard_ops.c',
  'bifrost/disassemble.c',
)

pandecode = executable(
  'pandecoder',
  files_pandecode,
  include_directories : [inc_common, inc_include, inc_src, inc_panfrost],
  dependencies : [
    dep_thread,
  ],
  link_with : [
    libmesa_util
  ],
  build_by_default : true
)

files_bifrost = files(
  'bifrost/cmdline.c',
)

bifrost_compiler = executable(
  'bifrost_compiler',
  [files_bifrost],
  include_directories : [
    inc_common,
    inc_include,
    inc_src,
    inc_panfrost,
 ],
  dependencies : [
    dep_thread,
    idep_nir
  ],
  link_with : [
    libglsl_standalone,
    libmesa_util,
    libpanfrost_bifrost
  ],
  build_by_default : true
)
