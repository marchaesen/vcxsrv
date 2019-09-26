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

libpanfrost_midgard_files = files(
  'midgard_compile.c',
  'mir.c',
  'midgard_print.c',
  'midgard_schedule.c',
  'midgard_derivatives.c',
  'midgard_emit.c',
  'midgard_ra.c',
  'midgard_ra_pipeline.c',
  'midgard_liveness.c',
  'midgard_ops.c',
  'mir_promote_uniforms.c',
  'midgard_opt_copy_prop.c',
  'midgard_opt_dce.c',
  'midgard_opt_invert.c',
  'midgard_opt_float.c',
  'midgard_opt_perspective.c',
  'disassemble.c',
)

midgard_nir_algebraic_c = custom_target(
  'midgard_nir_algebraic.c',
  input : 'midgard_nir_algebraic.py',
  output : 'midgard_nir_algebraic.c',
  command : [
    prog_python, '@INPUT@',
    '-p', join_paths(meson.source_root(), 'src/compiler/nir/'),
  ],
  capture : true,
  depend_files : nir_algebraic_py,
)

libpanfrost_midgard = static_library(
  'panfrost_midgard',
  [libpanfrost_midgard_files, midgard_nir_algebraic_c],
  include_directories : [
   inc_common,
   inc_include,
   inc_src,
   inc_panfrost_hw,
  ],
  dependencies: [
   idep_nir
  ],
  c_args : [c_vis_args, no_override_init_args],
  cpp_args : [cpp_vis_args],
  build_by_default : false,
)
