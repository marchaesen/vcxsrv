# Copyright © 2020 Google, Inc

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

computerator_files = [
  'a6xx.c',
  'ir3_asm.c',
  'main.c',
  freedreno_xml_header_files,
  ir3_parser[1],
]

computerator = executable(
  'computerator',
  computerator_files,
  include_directories : [
    inc_freedreno,
    inc_include,
    inc_src,
    inc_mapi,
    inc_mesa,
    inc_gallium,
    inc_gallium_aux,
  ],
  link_with : [
    libfreedreno_drm,
    libfreedreno_ir3,
    libfreedreno_perfcntrs,
  ],
  dependencies : [
    dep_libdrm,
    idep_mesautil,
    # We don't actually use nir, but ir3 wants some nir headers:
    idep_nir,
  ],
  build_by_default : with_tools.contains('freedreno'),
  install : with_tools.contains('freedreno'),
)
