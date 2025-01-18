# Copyright © 2020 Google, Inc
# SPDX-License-Identifier: MIT

prog_isaspec_decode = find_program('decode.py')
idep_isaspec_decode = declare_dependency(include_directories : include_directories('.'))

prog_isaspec_encode = find_program('encode.py')

isaspec_py_deps = files('isa.py')

libisaspec = static_library(
  'isaspec',
  ['isaspec.c'],
  dependencies: [idep_mesautil],
  gnu_symbol_visibility: 'hidden',
)
