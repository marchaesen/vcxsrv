# Copyright © 2017 Intel Corporation

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

glcpp_parse = custom_target(
  'glcpp-parse.[ch]',
  input : 'glcpp-parse.y',
  output : ['glcpp-parse.c', 'glcpp-parse.h'],
  command : [
    prog_bison, '-o', '@OUTPUT0@', '-p', 'glcpp_parser_',
    '--defines=@OUTPUT1@', '@INPUT@',
  ],
)

glcpp_lex = custom_target(
  'glcpp-lex.c',
  input : 'glcpp-lex.l',
  output : 'glcpp-lex.c',
  command : [prog_flex, '-o', '@OUTPUT@', '@INPUT@'],
)

_extra_args = []
if cpp.get_id() == 'msvc'
  # Flex relies on __STDC_VERSION__>=199901L to decide when to include C99
  # inttypes.h.  We always have inttypes.h available with MSVC (either the one
  # bundled with MSVC 2013, or the one we bundle ourselves), but we can't just
  # define __STDC_VERSION__ without breaking stuff, as MSVC doesn't fully
  # support C99.  There's also no way to premptively include stdint.
  _extra_args += '-FIinttypes.h'
endif

libglcpp = static_library(
  'glcpp',
  [glcpp_lex, glcpp_parse, files('glcpp.h', 'pp.c')],
  dependencies : idep_mesautil,
  include_directories : [inc_common],
  c_args : [c_vis_args, no_override_init_args, c_msvc_compat_args, _extra_args],
  cpp_args : [cpp_vis_args, cpp_msvc_compat_args, _extra_args],
  build_by_default : false,
)

glcpp = executable(
  'glcpp',
  'glcpp.c',
  dependencies : [dep_m, idep_getopt],
  include_directories : [inc_common],
  link_with : [libglcpp, libglsl_util],
  c_args : [c_vis_args, no_override_init_args, c_msvc_compat_args],
  build_by_default : false,
)

# FIXME: these fail on windows due to whitespace differences
if with_any_opengl and with_tests and host_machine.system() != 'windows'
  modes = ['unix', 'windows', 'oldmac', 'bizarro']
  if dep_valgrind.found()
    modes += ['valgrind']
  endif

  foreach m : modes
    test(
      'glcpp test (@0@)'.format(m),
      prog_python,
      args : [
        join_paths(meson.current_source_dir(), 'tests/glcpp_test.py'),
        glcpp, join_paths(meson.current_source_dir(), 'tests'),
        '--@0@'.format(m),
      ],
      suite : ['compiler', 'glcpp'],
    )
  endforeach
endif
