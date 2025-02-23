# Copyright 2017 Intel Corporation
# SPDX-License-Identifier: MIT

agx_pack = custom_target(
  input : ['gen_pack.py', 'cmdbuf.xml', 'agx_pack_header.h'],
  output : 'agx_pack.h',
  command : [prog_python, '@INPUT@'],
  capture : true,
)

idep_agx_pack = declare_dependency(
  sources : [agx_pack],
  dependencies : dep_valgrind,
  include_directories : include_directories('.'),
)


