# Copyright © 2020 Google, Inc

# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:

# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

afuc_parser = custom_target(
  'parser.[ch]',
  input: 'parser.y',
  output: ['parser.c', 'parser.h'],
  command: [
    prog_bison, '@INPUT@', '--defines=@OUTPUT1@', '--output=@OUTPUT0@'
  ]
)

afuc_lexer = custom_target(
  'lexer.c',
  input: 'lexer.l',
  output: 'lexer.c',
  command: [
    prog_flex, '-o', '@OUTPUT@', '@INPUT@'
  ]
)

asm = executable(
  'afuc-asm',
  [
    'asm.c',
    afuc_lexer,
    afuc_parser,
  ],
  include_directories: [
    inc_freedreno_rnn,
  ],
  link_with: [
    libfreedreno_rnn,
  ],
  dependencies: [],
  build_by_default : with_tools.contains('freedreno'),
  install: install_fd_decode_tools,
)

disasm = executable(
  'afuc-disasm',
  'disasm.c',
  include_directories: [
    inc_freedreno_rnn,
  ],
  link_with: [
    libfreedreno_rnn,
  ],
  dependencies: [],
  build_by_default : with_tools.contains('freedreno'),
  install: install_fd_decode_tools,
)
