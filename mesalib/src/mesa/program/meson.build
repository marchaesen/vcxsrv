# Copyright © 2017 Intel Corporation
# SPDX-License-Identifier: MIT

bison_command = []
if yacc_is_bison
  bison_command = [prog_bison, '-o', '@OUTPUT0@', '--defines=@OUTPUT1@', '@INPUT@']
else
  bison_command = [prog_bison, '-o', '@OUTPUT0@', '-H', '@OUTPUT1@', '@INPUT@']
endif

mesa_lex = custom_target(
  'mesa_lex',
  input : 'program_lexer.l',
  output : 'lex.yy.c',
  command : [prog_flex, '-o', '@OUTPUT@', '@INPUT@'],
)

program_parse_tab = custom_target(
  'program_parse_tab.[ch]',
  input : 'program_parse.y',
  output : ['program_parse.tab.c', 'program_parse.tab.h'],
  command : bison_command,
)
