/*
 * Copyright 2017 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "nir_builder.h"
#include "si_pipe.h"


static bool si_alu_to_scalar_filter(const nir_instr *instr, const void *data)
{
   struct si_screen *sscreen = (struct si_screen *)data;

   if (sscreen->options.fp16 &&
       instr->type == nir_instr_type_alu) {
      nir_alu_instr *alu = nir_instr_as_alu(instr);

      if (alu->dest.dest.is_ssa &&
          alu->dest.dest.ssa.bit_size == 16 &&
          alu->dest.dest.ssa.num_components == 2)
         return false;
   }

   return true;
}

void si_nir_opts(struct si_screen *sscreen, struct nir_shader *nir, bool first)
{
   bool progress;

   do {
      progress = false;
      bool lower_alu_to_scalar = false;
      bool lower_phis_to_scalar = false;

      NIR_PASS(progress, nir, nir_lower_vars_to_ssa);
      NIR_PASS(progress, nir, nir_lower_alu_to_scalar, si_alu_to_scalar_filter, sscreen);
      NIR_PASS(progress, nir, nir_lower_phis_to_scalar, false);

      if (first) {
         NIR_PASS(progress, nir, nir_split_array_vars, nir_var_function_temp);
         NIR_PASS(lower_alu_to_scalar, nir, nir_shrink_vec_array_vars, nir_var_function_temp);
         NIR_PASS(progress, nir, nir_opt_find_array_copies);
      }
      NIR_PASS(progress, nir, nir_opt_copy_prop_vars);
      NIR_PASS(progress, nir, nir_opt_dead_write_vars);

      NIR_PASS(lower_alu_to_scalar, nir, nir_opt_trivial_continues);
      /* (Constant) copy propagation is needed for txf with offsets. */
      NIR_PASS(progress, nir, nir_copy_prop);
      NIR_PASS(progress, nir, nir_opt_remove_phis);
      NIR_PASS(progress, nir, nir_opt_dce);
      NIR_PASS(lower_phis_to_scalar, nir, nir_opt_if, true);
      NIR_PASS(progress, nir, nir_opt_dead_cf);

      if (lower_alu_to_scalar)
         NIR_PASS_V(nir, nir_lower_alu_to_scalar, si_alu_to_scalar_filter, sscreen);
      if (lower_phis_to_scalar)
         NIR_PASS_V(nir, nir_lower_phis_to_scalar, false);
      progress |= lower_alu_to_scalar | lower_phis_to_scalar;

      NIR_PASS(progress, nir, nir_opt_cse);
      NIR_PASS(progress, nir, nir_opt_peephole_select, 8, true, true);

      /* Needed for algebraic lowering */
      NIR_PASS(progress, nir, nir_opt_algebraic);
      NIR_PASS(progress, nir, nir_opt_constant_folding);

      if (!nir->info.flrp_lowered) {
         unsigned lower_flrp = (nir->options->lower_flrp16 ? 16 : 0) |
                               (nir->options->lower_flrp32 ? 32 : 0) |
                               (nir->options->lower_flrp64 ? 64 : 0);
         assert(lower_flrp);
         bool lower_flrp_progress = false;

         NIR_PASS(lower_flrp_progress, nir, nir_lower_flrp, lower_flrp, false /* always_precise */);
         if (lower_flrp_progress) {
            NIR_PASS(progress, nir, nir_opt_constant_folding);
            progress = true;
         }

         /* Nothing should rematerialize any flrps, so we only
          * need to do this lowering once.
          */
         nir->info.flrp_lowered = true;
      }

      NIR_PASS(progress, nir, nir_opt_undef);
      NIR_PASS(progress, nir, nir_opt_conditional_discard);
      if (nir->options->max_unroll_iterations) {
         NIR_PASS(progress, nir, nir_opt_loop_unroll);
      }

      if (nir->info.stage == MESA_SHADER_FRAGMENT)
         NIR_PASS_V(nir, nir_opt_move_discards_to_top);

      if (sscreen->options.fp16)
         NIR_PASS(progress, nir, nir_opt_vectorize, NULL, NULL);
   } while (progress);

   NIR_PASS_V(nir, nir_lower_var_copies);
}

void si_nir_late_opts(nir_shader *nir)
{
   bool more_late_algebraic = true;
   while (more_late_algebraic) {
      more_late_algebraic = false;
      NIR_PASS(more_late_algebraic, nir, nir_opt_algebraic_late);
      NIR_PASS_V(nir, nir_opt_constant_folding);
      NIR_PASS_V(nir, nir_copy_prop);
      NIR_PASS_V(nir, nir_opt_dce);
      NIR_PASS_V(nir, nir_opt_cse);
   }
}

static void si_late_optimize_16bit_samplers(struct si_screen *sscreen, nir_shader *nir)
{
   /* Optimize and fix types of image_sample sources and destinations.
    *
    * The image_sample constraints are:
    *   nir_tex_src_coord:       has_a16 ? select 16 or 32 : 32
    *   nir_tex_src_comparator:  32
    *   nir_tex_src_offset:      32
    *   nir_tex_src_bias:        32
    *   nir_tex_src_lod:         match coord
    *   nir_tex_src_min_lod:     match coord
    *   nir_tex_src_ms_index:    match coord
    *   nir_tex_src_ddx:         has_g16 && coord == 32 ? select 16 or 32 : match coord
    *   nir_tex_src_ddy:         match ddy
    *
    * coord and ddx are selected optimally. The types of the rest are legalized
    * based on those two.
    */
   /* TODO: The constraints can't represent the ddx constraint. */
   /*bool has_g16 = sscreen->info.chip_class >= GFX10 && LLVM_VERSION_MAJOR >= 12;*/
   bool has_g16 = false;
   nir_tex_src_type_constraints tex_constraints = {
      [nir_tex_src_comparator]   = {true, 32},
      [nir_tex_src_offset]       = {true, 32},
      [nir_tex_src_bias]         = {true, 32},
      [nir_tex_src_lod]          = {true, 0, nir_tex_src_coord},
      [nir_tex_src_min_lod]      = {true, 0, nir_tex_src_coord},
      [nir_tex_src_ms_index]     = {true, 0, nir_tex_src_coord},
      [nir_tex_src_ddx]          = {!has_g16, 0, nir_tex_src_coord},
      [nir_tex_src_ddy]          = {true, 0, has_g16 ? nir_tex_src_ddx : nir_tex_src_coord},
   };
   bool changed = false;

   NIR_PASS(changed, nir, nir_fold_16bit_sampler_conversions,
            (1 << nir_tex_src_coord) |
            (has_g16 ? 1 << nir_tex_src_ddx : 0));
   NIR_PASS(changed, nir, nir_legalize_16bit_sampler_srcs, tex_constraints);

   if (changed) {
      si_nir_opts(sscreen, nir, false);
      si_nir_late_opts(nir);
   }
}

static int type_size_vec4(const struct glsl_type *type, bool bindless)
{
   return glsl_count_attribute_slots(type, false);
}

static void si_nir_lower_color(nir_shader *nir)
{
   nir_function_impl *entrypoint = nir_shader_get_entrypoint(nir);

   nir_builder b;
   nir_builder_init(&b, entrypoint);

   nir_foreach_block (block, entrypoint) {
      nir_foreach_instr_safe (instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

         if (intrin->intrinsic != nir_intrinsic_load_deref)
            continue;

         nir_deref_instr *deref = nir_src_as_deref(intrin->src[0]);
         if (!nir_deref_mode_is(deref, nir_var_shader_in))
            continue;

         b.cursor = nir_before_instr(instr);
         nir_variable *var = nir_deref_instr_get_variable(deref);
         nir_ssa_def *def;

         if (var->data.location == VARYING_SLOT_COL0) {
            def = nir_load_color0(&b);
            nir->info.fs.color0_interp = var->data.interpolation;
            nir->info.fs.color0_sample = var->data.sample;
            nir->info.fs.color0_centroid = var->data.centroid;
         } else if (var->data.location == VARYING_SLOT_COL1) {
            def = nir_load_color1(&b);
            nir->info.fs.color1_interp = var->data.interpolation;
            nir->info.fs.color1_sample = var->data.sample;
            nir->info.fs.color1_centroid = var->data.centroid;
         } else {
            continue;
         }

         nir_ssa_def_rewrite_uses(&intrin->dest.ssa, def);
         nir_instr_remove(instr);
      }
   }
}

static void si_lower_io(struct nir_shader *nir)
{
   /* HW supports indirect indexing for: | Enabled in driver
    * -------------------------------------------------------
    * TCS inputs                         | Yes
    * TES inputs                         | Yes
    * GS inputs                          | No
    * -------------------------------------------------------
    * VS outputs before TCS              | No
    * TCS outputs                        | Yes
    * VS/TES outputs before GS           | No
    */
   bool has_indirect_inputs = nir->info.stage == MESA_SHADER_TESS_CTRL ||
                              nir->info.stage == MESA_SHADER_TESS_EVAL;
   bool has_indirect_outputs = nir->info.stage == MESA_SHADER_TESS_CTRL;

   if (!has_indirect_inputs || !has_indirect_outputs) {
      NIR_PASS_V(nir, nir_lower_io_to_temporaries, nir_shader_get_entrypoint(nir),
                 !has_indirect_outputs, !has_indirect_inputs);

      /* Since we're doing nir_lower_io_to_temporaries late, we need
       * to lower all the copy_deref's introduced by
       * lower_io_to_temporaries before calling nir_lower_io.
       */
      NIR_PASS_V(nir, nir_split_var_copies);
      NIR_PASS_V(nir, nir_lower_var_copies);
      NIR_PASS_V(nir, nir_lower_global_vars_to_local);
   }

   /* The vectorization must be done after nir_lower_io_to_temporaries, because
    * nir_lower_io_to_temporaries after vectorization breaks:
    *    piglit/bin/arb_gpu_shader5-interpolateAtOffset -auto -fbo
    * TODO: It's probably a bug in nir_lower_io_to_temporaries.
    *
    * The vectorizer can only vectorize this:
    *    op src0.x, src1.x
    *    op src0.y, src1.y
    *
    * So it requires that inputs are already vectors and it must be the same
    * vector between instructions. The vectorizer doesn't create vectors
    * from independent scalar sources, so vectorize inputs.
    *
    * TODO: The pass fails this for VS: assert(b.shader->info.stage != MESA_SHADER_VERTEX);
    */
   if (nir->info.stage != MESA_SHADER_VERTEX)
      NIR_PASS_V(nir, nir_lower_io_to_vector, nir_var_shader_in);

   /* Vectorize outputs, so that we don't split vectors before storing outputs. */
   /* TODO: The pass fails an assertion for other shader stages. */
   if (nir->info.stage == MESA_SHADER_TESS_CTRL ||
       nir->info.stage == MESA_SHADER_FRAGMENT)
      NIR_PASS_V(nir, nir_lower_io_to_vector, nir_var_shader_out);

   if (nir->info.stage == MESA_SHADER_FRAGMENT)
      si_nir_lower_color(nir);

   NIR_PASS_V(nir, nir_lower_io, nir_var_shader_out | nir_var_shader_in,
              type_size_vec4, nir_lower_io_lower_64bit_to_32);
   nir->info.io_lowered = true;

   /* This pass needs actual constants */
   NIR_PASS_V(nir, nir_opt_constant_folding);
   NIR_PASS_V(nir, nir_io_add_const_offset_to_base, nir_var_shader_in |
                                                    nir_var_shader_out);

   /* Remove dead derefs, so that nir_validate doesn't fail. */
   NIR_PASS_V(nir, nir_opt_dce);

   /* Remove input and output nir_variables, because we don't need them
    * anymore. Also remove uniforms, because those should have been lowered
    * to UBOs already.
    */
   unsigned modes = nir_var_shader_in | nir_var_shader_out | nir_var_uniform;
   nir_foreach_variable_with_modes_safe(var, nir, modes) {
      if (var->data.mode == nir_var_uniform &&
          (glsl_type_get_image_count(var->type) ||
           glsl_type_get_sampler_count(var->type)))
         continue;

      exec_node_remove(&var->node);
   }
}

static bool
lower_intrinsic_filter(const nir_instr *instr, const void *dummy)
{
   return instr->type == nir_instr_type_intrinsic;
}

static nir_ssa_def *
lower_intrinsic_instr(nir_builder *b, nir_instr *instr, void *dummy)
{
   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

   switch (intrin->intrinsic) {
   case nir_intrinsic_is_sparse_texels_resident:
      /* code==0 means sparse texels are resident */
      return nir_ieq_imm(b, intrin->src[0].ssa, 0);
   default:
      return NULL;
   }
}

static bool si_lower_intrinsics(nir_shader *nir)
{
   return nir_shader_lower_instructions(nir,
                                        lower_intrinsic_filter,
                                        lower_intrinsic_instr,
                                        NULL);
}

/**
 * Perform "lowering" operations on the NIR that are run once when the shader
 * selector is created.
 */
static void si_lower_nir(struct si_screen *sscreen, struct nir_shader *nir)
{
   /* Perform lowerings (and optimizations) of code.
    *
    * Performance considerations aside, we must:
    * - lower certain ALU operations
    * - ensure constant offsets for texture instructions are folded
    *   and copy-propagated
    */

   static const struct nir_lower_tex_options lower_tex_options = {
      .lower_txp = ~0u,
      .lower_txs_cube_array = true,
   };
   NIR_PASS_V(nir, nir_lower_tex, &lower_tex_options);

   static const struct nir_lower_image_options lower_image_options = {
      .lower_cube_size = true,
   };
   NIR_PASS_V(nir, nir_lower_image, &lower_image_options);

   NIR_PASS_V(nir, si_lower_intrinsics);

   const nir_lower_subgroups_options subgroups_options = {
      .subgroup_size = 64,
      .ballot_bit_size = 64,
      .ballot_components = 1,
      .lower_to_scalar = true,
      .lower_subgroup_masks = true,
      .lower_vote_trivial = false,
      .lower_vote_eq = true,
   };
   NIR_PASS_V(nir, nir_lower_subgroups, &subgroups_options);

   NIR_PASS_V(nir, nir_lower_discard_or_demote,
              (sscreen->debug_flags & DBG(FS_CORRECT_DERIVS_AFTER_KILL)) ||
               nir->info.is_arb_asm);

   /* Lower load constants to scalar and then clean up the mess */
   NIR_PASS_V(nir, nir_lower_load_const_to_scalar);
   NIR_PASS_V(nir, nir_lower_var_copies);
   NIR_PASS_V(nir, nir_opt_intrinsics);
   NIR_PASS_V(nir, nir_lower_system_values);
   NIR_PASS_V(nir, nir_lower_compute_system_values, NULL);

   if (nir->info.stage == MESA_SHADER_COMPUTE) {
      if (nir->info.cs.derivative_group == DERIVATIVE_GROUP_QUADS) {
         /* If we are shuffling local_invocation_id for quad derivatives, we
          * need to derive local_invocation_index from local_invocation_id
          * first, so that the value corresponds to the shuffled
          * local_invocation_id.
          */
         nir_lower_compute_system_values_options options = {0};
         options.lower_local_invocation_index = true;
         NIR_PASS_V(nir, nir_lower_compute_system_values, &options);
      }

      nir_opt_cse(nir); /* CSE load_local_invocation_id */
      nir_lower_compute_system_values_options options = {0};
      options.shuffle_local_ids_for_quad_derivatives = true;
      NIR_PASS_V(nir, nir_lower_compute_system_values, &options);
   }

   if (sscreen->b.get_shader_param(&sscreen->b, PIPE_SHADER_FRAGMENT, PIPE_SHADER_CAP_FP16)) {
      NIR_PASS_V(nir, nir_lower_mediump_io,
                 /* TODO: LLVM fails to compile this test if VS inputs are 16-bit:
                  * dEQP-GLES31.functional.shaders.builtin_functions.integer.bitfieldinsert.uvec3_lowp_geometry
                  */
                 (nir->info.stage != MESA_SHADER_VERTEX ? nir_var_shader_in : 0) | nir_var_shader_out,
                 BITFIELD64_BIT(VARYING_SLOT_PNTC) | BITFIELD64_RANGE(VARYING_SLOT_VAR0, 32),
                 true);
   }

   si_nir_opts(sscreen, nir, true);
   /* Run late optimizations to fuse ffma and eliminate 16-bit conversions. */
   si_nir_late_opts(nir);

   if (sscreen->b.get_shader_param(&sscreen->b, PIPE_SHADER_FRAGMENT, PIPE_SHADER_CAP_FP16))
      si_late_optimize_16bit_samplers(sscreen, nir);

   NIR_PASS_V(nir, nir_remove_dead_variables, nir_var_function_temp, NULL);
}

char *si_finalize_nir(struct pipe_screen *screen, void *nirptr)
{
   struct si_screen *sscreen = (struct si_screen *)screen;
   struct nir_shader *nir = (struct nir_shader *)nirptr;

   si_lower_io(nir);
   si_lower_nir(sscreen, nir);
   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));

   if (sscreen->options.inline_uniforms)
      nir_find_inlinable_uniforms(nir);

   NIR_PASS_V(nir, nir_convert_to_lcssa, true, true); /* required by divergence analysis */
   NIR_PASS_V(nir, nir_divergence_analysis); /* to find divergent loops */

   return NULL;
}
