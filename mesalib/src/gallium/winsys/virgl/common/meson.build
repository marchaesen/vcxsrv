# Copyright © 2019 Collabora Ltd
# SPDX-License-Identifier: MIT

libvirglcommon = static_library(
  'virglcommon',
  'virgl_resource_cache.c',
  include_directories : [inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_gallium_aux, inc_gallium_drivers],
  gnu_symbol_visibility : 'hidden',
  dependencies: idep_mesautil,
)

dep_libvirglcommon = declare_dependency(
  link_with : libvirglcommon,
  include_directories : include_directories('.'),
)
