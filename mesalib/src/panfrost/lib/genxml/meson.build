# Copyright © 2018 Rob Clark
# Copyright © 2019 Collabora
# SPDX-License-Identifier: MIT

pan_packers = []
foreach packer : ['common', 'v4', 'v5', 'v6', 'v7', 'v9', 'v10']
  pan_packers += custom_target(
    packer + '_pack.h',
    input : ['gen_pack.py', packer + '.xml'],
    output : packer + '_pack.h',
    command : [prog_python, '@INPUT@'],
    capture : true,
  )
endforeach

idep_pan_packers = declare_dependency(
  sources : [pan_packers],
  include_directories : include_directories('.'),
)

libpanfrost_decode_per_arch = []

foreach ver : ['4', '5', '6', '7', '9', '10']
  libpanfrost_decode_per_arch += static_library(
    'pandecode-arch-v' + ver,
    ['decode.c', 'decode_jm.c', 'decode_csf.c', pan_packers],
    include_directories : [inc_include, inc_src, inc_panfrost],
    dependencies : [dep_libdrm, idep_pan_packers, idep_nir, idep_mesautil],
    c_args : [no_override_init_args, '-DPAN_ARCH=' + ver],
    gnu_symbol_visibility : 'hidden',
    build_by_default : false,
  )
endforeach

libpanfrost_decode = static_library(
  'panfrost_decode',
  [
    'decode_common.c',
    pan_packers
  ],
  include_directories : [inc_include, inc_src, inc_panfrost],
  dependencies : idep_mesautil,
  c_args : [no_override_init_args],
  gnu_symbol_visibility : 'hidden',
  build_by_default : false,
  link_with: [libpanfrost_decode_per_arch],
)
