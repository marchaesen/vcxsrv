# Copyright © 2018 Rob Clark

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

ir3_nir_trig_c = custom_target(
  'ir3_nir_trig.c',
  input : 'ir3_nir_trig.py',
  output : 'ir3_nir_trig.c',
  command : [
    prog_python, '@INPUT@',
    '-p', join_paths(meson.source_root(), 'src/compiler/nir/'),
  ],
  capture : true,
  depend_files : nir_algebraic_py,
)

libfreedreno_ir3_files = files(
  'disasm-a3xx.c',
  'instr-a3xx.h',
  'ir3.c',
  'ir3_compiler_nir.c',
  'ir3_compiler.c',
  'ir3_compiler.h',
  'ir3_cp.c',
  'ir3_depth.c',
  'ir3_group.c',
  'ir3.h',
  'ir3_legalize.c',
  'ir3_nir.c',
  'ir3_nir.h',
  'ir3_nir_lower_tg4_to_tex.c',
  'ir3_print.c',
  'ir3_ra.c',
  'ir3_sched.c',
  'ir3_shader.c',
  'ir3_shader.h',
)

libfreedreno_ir3 = static_library(
  'freedreno_ir3',
  [libfreedreno_ir3_files, ir3_nir_trig_c],
  include_directories : [inc_freedreno, inc_common],
  c_args : [c_vis_args, no_override_init_args],
  cpp_args : [cpp_vis_args],
  dependencies : idep_nir_headers,
  build_by_default : false,
)

