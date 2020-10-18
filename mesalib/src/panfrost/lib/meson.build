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

libpanfrost_lib_files = files(
  'pan_encoder.h',

  'pan_afbc.c',
  'pan_attributes.c',
  'pan_bo.c',
  'pan_blit.c',
  'pan_format.c',
  'pan_invocation.c',
  'pan_sampler.c',
  'pan_tiler.c',
  'pan_texture.c',
  'pan_scoreboard.c',
  'pan_scratch.c',
  'pan_pool.c',
  'pan_props.c',
)

libpanfrost_decode_files = files(
  'decode_common.c',
  'decode.c',
)

midgard_pack = custom_target(
  'midgard_pack.h',
  input : ['gen_pack.py', 'midgard.xml'],
  output : 'midgard_pack.h',
  command : [prog_python, '@INPUT@'],
  capture : true,
)

idep_midgard_pack = declare_dependency(
  sources : [midgard_pack],
  include_directories : include_directories('.'),
)

libpanfrost_lib = static_library(
  'panfrost_lib',
  [libpanfrost_lib_files, midgard_pack],
  include_directories : [inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_gallium_aux, inc_panfrost_hw],
  c_args : [no_override_init_args],
  gnu_symbol_visibility : 'hidden',
  dependencies: [dep_libdrm, idep_nir],
  build_by_default : false,
)

libpanfrost_decode = static_library(
  'panfrost_decode',
  [libpanfrost_decode_files, midgard_pack],
  include_directories : [inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_gallium_aux, inc_panfrost],
  c_args : [no_override_init_args],
  gnu_symbol_visibility : 'hidden',
  build_by_default : false,
)
