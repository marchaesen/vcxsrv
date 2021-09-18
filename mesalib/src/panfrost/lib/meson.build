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

pan_packers = custom_target(
  'midgard_pack.h',
  input : ['gen_pack.py', 'midgard.xml'],
  output : 'midgard_pack.h',
  command : [prog_python, '@INPUT@'],
  capture : true,
)

idep_pan_packers = declare_dependency(
  sources : [pan_packers],
  include_directories : include_directories('.'),
)

pixel_format_versions = ['6', '7']
libpanfrost_pixel_format = []

foreach ver : pixel_format_versions
  libpanfrost_pixel_format += static_library(
    'pan-format-v' + ver,
    ['pan_format.c', pan_packers],
    include_directories : [
      inc_src, inc_include, inc_gallium, inc_mesa, inc_panfrost_hw, inc_gallium_aux, inc_panfrost_hw
    ],
    c_args : ['-DPAN_ARCH=' + ver],
    gnu_symbol_visibility : 'hidden',
    dependencies : [idep_pan_packers, dep_libdrm],
)
endforeach

libpanfrost_per_arch = []

foreach ver : ['4', '5', '6', '7']
  libpanfrost_per_arch += static_library(
    'pan-arch-v' + ver,
    [
      'pan_indirect_dispatch.c',
      'pan_indirect_draw.c',
    ],
    include_directories : [inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_gallium_aux, inc_panfrost_hw],
    c_args : ['-DPAN_ARCH=' + ver],
    gnu_symbol_visibility : 'hidden',
    dependencies : [dep_libdrm, idep_pan_packers, idep_nir],
  )
endforeach

libpanfrost_lib_files = files(
  'pan_encoder.h',

  'pan_afbc.c',
  'pan_attributes.c',
  'pan_bo.c',
  'pan_blend.c',
  'pan_blitter.c',
  'pan_clear.c',
  'pan_cs.c',
  'pan_samples.c',
  'pan_tiler.c',
  'pan_texture.c',
  'pan_shader.c',
  'pan_scoreboard.c',
  'pan_scratch.c',
  'pan_props.c',
  'pan_util.c',
)

libpanfrost_decode_files = files(
  'decode_common.c',
  'decode.c',
)

libpanfrost_lib = static_library(
  'panfrost_lib',
  [libpanfrost_lib_files, pan_packers],
  include_directories : [inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_gallium_aux, inc_panfrost_hw],
  c_args : [no_override_init_args],
  gnu_symbol_visibility : 'hidden',
  dependencies: [dep_libdrm, idep_nir],
  build_by_default : false,
  link_with: [libpanfrost_pixel_format, libpanfrost_per_arch],
)

libpanfrost_decode = static_library(
  'panfrost_decode',
  [libpanfrost_decode_files, pan_packers],
  include_directories : [inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_gallium_aux, inc_panfrost],
  dependencies : idep_mesautil,
  c_args : [no_override_init_args],
  gnu_symbol_visibility : 'hidden',
  build_by_default : false,
)

libpanfrost_dep = declare_dependency(
  link_with: [libpanfrost_lib, libpanfrost_decode, libpanfrost_midgard, libpanfrost_bifrost, libpanfrost_pixel_format, libpanfrost_per_arch],
  include_directories: [inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_gallium_aux, inc_panfrost_hw, inc_panfrost],
  dependencies: [dep_libdrm, idep_nir, idep_pan_packers],
)

if with_tests
  test(
    'panfrost_blend',
    executable(
      'panfrost_blend_test',
      files('tests/test-blend.c'),
      c_args : [c_msvc_compat_args, no_override_init_args],
      gnu_symbol_visibility : 'hidden',
      include_directories : [inc_include, inc_src, inc_mesa],
      dependencies: [libpanfrost_dep],
    ),
    suite : ['panfrost'],
  )

  test(
    'panfrost_clear',
    executable(
      'panfrost_clear_test',
      files('tests/test-clear.c'),
      c_args : [c_msvc_compat_args, no_override_init_args],
      gnu_symbol_visibility : 'hidden',
      include_directories : [inc_include, inc_src, inc_mesa],
      dependencies: [libpanfrost_dep],
    ),
    suite : ['panfrost'],
  )
endif
