# Copyright © 2018 Rob Clark
# SPDX-License-Identifier: MIT

libfreedreno_layout = static_library(
  'freedreno_layout',
  [
    'fd5_layout.c',
    'fd6_format_table.c',
    'fd6_layout.c',
    'fd6_tiled_memcpy.cc',
    'fd6_view.c',
    'freedreno_layout.c',
    freedreno_xml_header_files,
  ],
  include_directories : [inc_freedreno, inc_include, inc_src],
  c_args : [no_override_init_args],
  gnu_symbol_visibility : 'hidden',
  dependencies : [idep_nir_headers, idep_mesautil],
  build_by_default : false,
)

foreach major : ['5', '6']
  test(
    'fd' + major + '_layout',
    executable(
      'fd' + major + '_layout',
      [
        'fd_layout_test.c',
        'fd' + major + '_layout_test.c',
        freedreno_xml_header_files,
      ],
      link_with: libfreedreno_layout,
      dependencies : idep_mesautil,
      include_directories: [
        inc_include,
        inc_src,
        inc_freedreno],
    ),
    suite : ['freedreno'],
  )
endforeach
