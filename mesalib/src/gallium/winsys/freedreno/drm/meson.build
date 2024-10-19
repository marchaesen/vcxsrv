# Copyright © 2017 Rob Clark
# SPDX-License-Identifier: MIT

libfreedrenowinsys = static_library(
  'freedrenowinsys',
  [
    files('freedreno_drm_public.h', 'freedreno_drm_winsys.c'),
    freedreno_xml_header_files,
  ],
  include_directories : [
    inc_src, inc_include, inc_gallium, inc_gallium_aux, inc_gallium_drivers,
    inc_freedreno,
  ],
  gnu_symbol_visibility : 'hidden',
  dependencies : [
    dep_libdrm,
    idep_mesautil,
  ],
)
