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
   '.', 'include', 'shared', 'midgard', 'bifrost', 'lib'
])

subdir('shared')
subdir('util')
subdir('midgard')
subdir('bifrost')
subdir('lib')

files_bifrost = files(
  'bifrost/cmdline.c',
  'bifrost/test/bi_submit.c',
  'bifrost/test/bi_interpret.c',
  'bifrost/test/bi_test_pack.c',
)

bifrost_compiler = executable(
  'bifrost_compiler',
  [files_bifrost, midgard_pack],
  include_directories : [
    inc_mapi,
    inc_mesa,
    inc_gallium,
    inc_gallium_aux,
    inc_include,
    inc_src,
    inc_panfrost,
    inc_panfrost_hw,
 ],
  dependencies : [
    idep_nir,
    idep_mesautil,
    dep_libdrm,
  ],
  link_with : [
    libglsl_standalone,
    libpanfrost_bifrost,
    libpanfrost_decode,
    libpanfrost_lib,
    libpanfrost_midgard_disasm, # references disassemble_midgard...
  ],
  build_by_default : with_tools.contains('panfrost')
)
