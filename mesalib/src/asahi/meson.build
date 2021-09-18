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

inc_asahi = include_directories([
   '.', 'lib', 'compiler'
])

subdir('lib')
subdir('compiler')

files_agx = files(
  'compiler/cmdline.c',
)

agx_compiler = executable(
  'agx_compiler',
  [files_agx],
  include_directories : [
    inc_include,
    inc_src,
    inc_gallium,
    inc_gallium_aux,
    inc_mapi,
    inc_mesa,
 ],
  dependencies : [
    idep_nir,
    idep_mesautil,
    idep_agx_pack,
    dep_libdrm,
  ],
  link_with : [
    libglsl_standalone,
    libasahi_compiler,
  ],
  build_by_default : with_tools.contains('asahi')
)
