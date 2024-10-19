# Copyright © 2017 Broadcom
# SPDX-License-Identifier: MIT

dep_simpenrose = dependency('simpenrose', required : false)

vc4_winsys_c_args = []
if with_gallium_kmsro
  vc4_winsys_c_args += '-DGALLIUM_KMSRO'
endif

if dep_simpenrose.found()
  vc4_winsys_c_args += '-DUSE_VC4_SIMULATOR'
endif

libvc4winsys = static_library(
  'vc4winsys',
  files('vc4_drm_winsys.c'),
  dependencies : idep_mesautil,
  include_directories : [
    inc_src, inc_include,
    inc_gallium, inc_gallium_aux, inc_gallium_drivers, inc_gallium_winsys,
  ],
  c_args : [vc4_winsys_c_args],
  gnu_symbol_visibility : 'hidden',
)
