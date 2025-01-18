# Copyright © 2017 Intel Corporation
# SPDX-License-Identifier: MIT

bison_command = []
if yacc_is_bison
  bison_command = [
    prog_bison, '-o', '@OUTPUT0@', '-p', 'glcpp_parser_',
    '--defines=@OUTPUT1@', '@INPUT@',
  ]
else
  bison_command = [
    prog_bison, '-o', '@OUTPUT0@', '-p', 'glcpp_parser_',
    '-H', '@OUTPUT1@', '@INPUT@',
  ]
endif

glcpp_parse = custom_target(
  'glcpp-parse.[ch]',
  input : 'glcpp-parse.y',
  output : ['glcpp-parse.c', 'glcpp-parse.h'],
  command : bison_command
)

glcpp_lex = custom_target(
  'glcpp-lex.c',
  input : 'glcpp-lex.l',
  output : 'glcpp-lex.c',
  command : [prog_flex, '-o', '@OUTPUT@', '@INPUT@'],
)

libglcpp = static_library(
  'glcpp',
  [glcpp_lex, glcpp_parse, files('glcpp.h', 'pp.c')],
  dependencies : idep_mesautil,
  include_directories : [inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_gallium_aux],
  c_args : [no_override_init_args, c_msvc_compat_args],
  cpp_args : [cpp_msvc_compat_args],
  gnu_symbol_visibility : 'hidden',
  build_by_default : false,
)

libglcpp_standalone = static_library(
  'glcpp_standalone',
  'pp_standalone_scaffolding.c',
  link_with : libglcpp,
  dependencies : idep_mesautil,
  include_directories : [inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_gallium_aux],
  c_args : [no_override_init_args, c_msvc_compat_args],
  cpp_args : [cpp_msvc_compat_args],
  gnu_symbol_visibility : 'hidden',
  build_by_default : false,
)

glcpp = executable(
  'glcpp',
  'glcpp.c',
  dependencies : [dep_m, idep_getopt, idep_mesautil],
  include_directories : [inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_gallium_aux],
  link_with : [libglcpp_standalone, libglsl_util],
  c_args : [no_override_init_args, c_msvc_compat_args],
  gnu_symbol_visibility : 'hidden',
  build_by_default : false,
)

# Meson can't auto-skip these on cross builds because of the python wrapper
if with_any_opengl and with_tests and meson.can_run_host_binaries() and \
   with_glcpp_tests
  modes = ['unix', 'windows', 'oldmac', 'bizarro']

  foreach m : modes
    test(
      'glcpp test (@0@)'.format(m),
      prog_python,
      args : [
        files('tests/glcpp_test.py'),
        glcpp, join_paths(meson.current_source_dir(), 'tests'),
        '--@0@'.format(m),
      ],
      suite : ['compiler', 'glcpp'],
      timeout: 60,
    )
  endforeach
endif
