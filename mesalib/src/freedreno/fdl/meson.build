# Copyright © 2018 Rob Clark

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

libfreedreno_layout = static_library(
  'freedreno_layout',
  [
    'fd6_layout.c',
    'freedreno_layout.c',
  ],
  include_directories : [inc_freedreno, inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_gallium_aux],
  c_args : [c_vis_args, no_override_init_args],
  cpp_args : [cpp_vis_args],
  dependencies : idep_nir_headers,
  build_by_default : false,
)


test(
  'fd6_layout',
  executable(
    'fd6_layout',
    [
      'fd6_layout_test.c',
      freedreno_xml_header_files,
    ],
    link_with: libfreedreno_layout,
    dependencies : idep_mesautil,
    include_directories: [
      inc_include,
      inc_src,
      inc_mapi,
      inc_mesa,
      inc_gallium,
      inc_gallium_aux,
      inc_freedreno],
  ),
  suite : ['freedreno'],
)
