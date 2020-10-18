# Copyright © 2018 Rob Clark
# Copyright © 2019 Collabora

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

libpanfrost_bifrost_files = files(
  'bi_layout.c',
  'bi_liveness.c',
  'bi_lower_combine.c',
  'bi_print.c',
  'bi_opt_dce.c',
  'bi_pack.c',
  'bi_ra.c',
  'bi_schedule.c',
  'bi_special.c',
  'bi_tables.c',
  'bir.c',
  'bifrost_compile.c',
)

bifrost_nir_algebraic_c = custom_target(
  'bifrost_nir_algebraic.c',
  input : 'bifrost_nir_algebraic.py',
  output : 'bifrost_nir_algebraic.c',
  command : [
    prog_python, '@INPUT@',
    '-p', join_paths(meson.source_root(), 'src/compiler/nir/'),
  ],
  capture : true,
  depend_files : nir_algebraic_py,
)

bifrost_gen_disasm_c = custom_target(
  'bifrost_gen_disasm.c',
  input : ['gen_disasm.py', 'ISA.xml'],
  output : 'bifrost_gen_disasm.c',
  command : [prog_python, '@INPUT@'],
  capture : true,
)

bi_generated_pack_h = custom_target(
  'bi_generated_pack.h',
  input : ['gen_pack.py', 'ISA.xml'],
  output : 'bi_generated_pack.h',
  command : [prog_python, '@INPUT@'],
  capture : true,
)

idep_bi_generated_pack_h = declare_dependency(
  sources : [bi_generated_pack_h],
  include_directories : include_directories('.'),
)

libpanfrost_bifrost_disasm = static_library(
  'panfrost_bifrost_disasm',
  ['disassemble.c', 'bi_print_common.c', bifrost_gen_disasm_c],
  include_directories : [inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_gallium_aux, inc_panfrost_hw],
  dependencies: [idep_nir],
  link_with: [libpanfrost_util],
  c_args : [no_override_init_args],
  gnu_symbol_visibility : 'hidden',
  build_by_default : false,
)

libpanfrost_bifrost = static_library(
  'panfrost_bifrost',
  [libpanfrost_bifrost_files, bifrost_nir_algebraic_c],
  include_directories : [inc_include, inc_src, inc_mapi, inc_mesa, inc_gallium, inc_gallium_aux, inc_panfrost_hw],
  dependencies: [idep_nir, idep_bi_generated_pack_h],
  link_with: [libpanfrost_util, libpanfrost_bifrost_disasm],
  c_args : [no_override_init_args],
  gnu_symbol_visibility : 'hidden',
  build_by_default : false,
)
