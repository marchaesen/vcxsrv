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

nir_depends = files('nir_opcodes.py')

nir_builder_opcodes_h = custom_target(
  'nir_builder_opcodes.h',
  input : 'nir_builder_opcodes_h.py',
  output : 'nir_builder_opcodes.h',
  command : [prog_python2, '@INPUT@'],
  capture : true,
  depend_files : nir_depends,
)

nir_constant_expressions_c = custom_target(
  'nir_constant_expressions.c',
  input : 'nir_constant_expressions.py',
  output : 'nir_constant_expressions.c',
  command : [prog_python2, '@INPUT@'],
  capture : true,
  depend_files : nir_depends,
)

nir_opcodes_h = custom_target(
  'nir_opcodes.h',
  input : 'nir_opcodes_h.py',
  output : 'nir_opcodes.h',
  command : [prog_python2, '@INPUT@'],
  capture : true,
  depend_files : nir_depends,
)

nir_opcodes_c = custom_target(
  'nir_opcodes.c',
  input : 'nir_opcodes_c.py',
  output : 'nir_opcodes.c',
  command : [prog_python2, '@INPUT@'],
  capture : true,
  depend_files : nir_depends,
)

nir_opt_algebraic_c = custom_target(
  'nir_opt_algebraic.c',
  input : 'nir_opt_algebraic.py',
  output : 'nir_opt_algebraic.c',
  command : [prog_python2, '@INPUT@'],
  capture : true,
  depend_files : files('nir_algebraic.py'),
)

spirv_info_c = custom_target(
  'spirv_info.c',
  input : files('../spirv/spirv_info_c.py', '../spirv/spirv.core.grammar.json'),
  output : 'spirv_info.c',
  command : [prog_python2, '@INPUT0@', '@INPUT1@', '@OUTPUT@'],
)

vtn_gather_types_c = custom_target(
  'vtn_gather_types.c',
  input : files('../spirv/vtn_gather_types_c.py',
                '../spirv/spirv.core.grammar.json'),
  output : 'vtn_gather_types.c',
  command : [prog_python2, '@INPUT0@', '@INPUT1@', '@OUTPUT@'],
)

files_libnir = files(
  'nir.c',
  'nir.h',
  'nir_builder.h',
  'nir_clone.c',
  'nir_constant_expressions.h',
  'nir_control_flow.c',
  'nir_control_flow.h',
  'nir_control_flow_private.h',
  'nir_dominance.c',
  'nir_from_ssa.c',
  'nir_gather_info.c',
  'nir_gs_count_vertices.c',
  'nir_inline_functions.c',
  'nir_instr_set.c',
  'nir_instr_set.h',
  'nir_intrinsics.c',
  'nir_intrinsics.h',
  'nir_linking_helpers.c',
  'nir_liveness.c',
  'nir_loop_analyze.c',
  'nir_loop_analyze.h',
  'nir_lower_64bit_packing.c',
  'nir_lower_alu_to_scalar.c',
  'nir_lower_alpha_test.c',
  'nir_lower_atomics.c',
  'nir_lower_atomics_to_ssbo.c',
  'nir_lower_bitmap.c',
  'nir_lower_clamp_color_outputs.c',
  'nir_lower_clip.c',
  'nir_lower_clip_cull_distance_arrays.c',
  'nir_lower_constant_initializers.c',
  'nir_lower_double_ops.c',
  'nir_lower_drawpixels.c',
  'nir_lower_global_vars_to_local.c',
  'nir_lower_gs_intrinsics.c',
  'nir_lower_load_const_to_scalar.c',
  'nir_lower_locals_to_regs.c',
  'nir_lower_idiv.c',
  'nir_lower_indirect_derefs.c',
  'nir_lower_int64.c',
  'nir_lower_io.c',
  'nir_lower_io_arrays_to_elements.c',
  'nir_lower_io_to_temporaries.c',
  'nir_lower_io_to_scalar.c',
  'nir_lower_io_types.c',
  'nir_lower_passthrough_edgeflags.c',
  'nir_lower_patch_vertices.c',
  'nir_lower_phis_to_scalar.c',
  'nir_lower_regs_to_ssa.c',
  'nir_lower_returns.c',
  'nir_lower_samplers.c',
  'nir_lower_samplers_as_deref.c',
  'nir_lower_subgroups.c',
  'nir_lower_system_values.c',
  'nir_lower_tex.c',
  'nir_lower_to_source_mods.c',
  'nir_lower_two_sided_color.c',
  'nir_lower_uniforms_to_ubo.c',
  'nir_lower_vars_to_ssa.c',
  'nir_lower_var_copies.c',
  'nir_lower_vec_to_movs.c',
  'nir_lower_wpos_center.c',
  'nir_lower_wpos_ytransform.c',
  'nir_metadata.c',
  'nir_move_vec_src_uses_to_dest.c',
  'nir_normalize_cubemap_coords.c',
  'nir_opt_conditional_discard.c',
  'nir_opt_constant_folding.c',
  'nir_opt_copy_prop_vars.c',
  'nir_opt_copy_propagate.c',
  'nir_opt_cse.c',
  'nir_opt_dce.c',
  'nir_opt_dead_cf.c',
  'nir_opt_gcm.c',
  'nir_opt_global_to_local.c',
  'nir_opt_if.c',
  'nir_opt_intrinsics.c',
  'nir_opt_loop_unroll.c',
  'nir_opt_move_comparisons.c',
  'nir_opt_move_load_ubo.c',
  'nir_opt_peephole_select.c',
  'nir_opt_remove_phis.c',
  'nir_opt_shrink_load.c',
  'nir_opt_trivial_continues.c',
  'nir_opt_undef.c',
  'nir_phi_builder.c',
  'nir_phi_builder.h',
  'nir_print.c',
  'nir_propagate_invariant.c',
  'nir_remove_dead_variables.c',
  'nir_repair_ssa.c',
  'nir_search.c',
  'nir_search.h',
  'nir_search_helpers.h',
  'nir_serialize.c',
  'nir_serialize.h',
  'nir_split_var_copies.c',
  'nir_sweep.c',
  'nir_to_lcssa.c',
  'nir_validate.c',
  'nir_vla.h',
  'nir_worklist.c',
  'nir_worklist.h',
  '../spirv/GLSL.std.450.h',
  '../spirv/nir_spirv.h',
  '../spirv/spirv.h',
  '../spirv/spirv_info.h',
  '../spirv/spirv_to_nir.c',
  '../spirv/vtn_alu.c',
  '../spirv/vtn_amd.c',
  '../spirv/vtn_cfg.c',
  '../spirv/vtn_glsl450.c',
  '../spirv/vtn_private.h',
  '../spirv/vtn_subgroup.c',
  '../spirv/vtn_variables.c',
)

libnir = static_library(
  'nir',
  [files_libnir, spirv_info_c, nir_opt_algebraic_c, nir_opcodes_c,
   nir_opcodes_h, nir_constant_expressions_c, nir_builder_opcodes_h,
   vtn_gather_types_c],
  include_directories : [inc_common, inc_compiler, include_directories('../spirv')],
  c_args : [c_vis_args, c_msvc_compat_args, no_override_init_args],
  link_with : libcompiler,
  build_by_default : false,
)

# Headers-only dependency
idep_nir_headers = declare_dependency(
  sources : [nir_opcodes_h, nir_builder_opcodes_h],
  include_directories : include_directories('.'),
)

# Also link with nir
idep_nir = declare_dependency(
  dependencies : idep_nir_headers,
  link_with : libnir,
)

nir_algebraic_py = files('nir_algebraic.py')

if with_tests
  test(
    'nir_control_flow',
    executable(
      'nir_control_flow_test',
      files('tests/control_flow_tests.cpp'),
      c_args : [c_vis_args, c_msvc_compat_args, no_override_init_args],
      include_directories : [inc_common],
      dependencies : [dep_thread, idep_gtest, idep_nir],
      link_with : libmesa_util,
    )
  )
endif
