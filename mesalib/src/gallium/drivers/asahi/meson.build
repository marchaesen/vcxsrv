# Copyright © 2017 Intel Corporation

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

files_asahi = files(
  'agx_blit.c',
  'agx_pipe.c',
  'agx_state.c',
  'agx_uniforms.c',
  'magic.c',
)

libasahi = static_library(
  'asahi',
  files_asahi,
  include_directories : [inc_gallium_aux, inc_gallium, inc_include, inc_src],
  c_args : [c_msvc_compat_args],
  gnu_symbol_visibility : 'hidden',
  dependencies : [idep_nir, idep_agx_pack],
)

driver_asahi = declare_dependency(
  compile_args : '-DGALLIUM_ASAHI',
  link_with : [libasahi, libasahi_compiler, libasahi_lib, libasahi_decode]
)
