# Copyright © 2017 Broadcom
# SPDX-License-Identifier: MIT

libkmsrowinsys = static_library(
  'kmsrowinsys',
  files('kmsro_drm_winsys.c'),
  include_directories : [
    inc_src, inc_include, inc_loader,
    inc_gallium, inc_gallium_aux, inc_gallium_winsys,
  ],
  link_with : libloader,
  c_args : [renderonly_drivers_c_args],
  gnu_symbol_visibility : 'hidden',
  dependencies: [dep_libdrm, idep_mesautil],
)

driver_kmsro = declare_dependency(
  compile_args : '-DGALLIUM_KMSRO',
  link_with : libkmsrowinsys,
)
