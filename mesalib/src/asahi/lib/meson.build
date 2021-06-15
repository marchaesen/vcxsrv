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

dep_iokit = dependency('IOKit', required : false)

libasahi_lib_files = files(
  'agx_device.c',
  'agx_formats.c',
  'pool.c',
  'tiling.c',
)

libasahi_decode_files = files(
  'decode.c',
)

agx_pack = custom_target(
  'agx_pack.h',
  input : ['gen_pack.py', 'cmdbuf.xml'],
  output : 'agx_pack.h',
  command : [prog_python, '@INPUT@'],
  capture : true,
)

idep_agx_pack = declare_dependency(
  sources : [agx_pack],
  include_directories : include_directories('.'),
)

libasahi_lib = static_library(
  'asahi_lib',
  [libasahi_lib_files, agx_pack],
  include_directories : [inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_gallium_aux, inc_asahi],
  c_args : [no_override_init_args],
  gnu_symbol_visibility : 'hidden',
  dependencies: [dep_libdrm, idep_nir, dep_iokit],
  build_by_default : false,
)

libasahi_decode = static_library(
  'asahi_decode',
  [libasahi_decode_files, agx_pack],
  include_directories : [inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_gallium_aux, inc_asahi],
  dependencies : idep_mesautil,
  c_args : [no_override_init_args],
  gnu_symbol_visibility : 'hidden',
  build_by_default : false,
)
