# Copyright © 2018 Lima Project
# SPDX-License-Identifier: MIT

liblimawinsys = static_library(
  'limawinsys',
  files('lima_drm_winsys.c'),
  include_directories : [
    inc_src, inc_include,
    inc_gallium, inc_gallium_aux, inc_gallium_drivers,
  ],
  dependencies : idep_mesautil,
)
