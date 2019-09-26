/*
 * Copyright © 2018 Valve Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */

#include "nir.h"

/* This pass computes for each ssa definition if it is uniform.
 * That is, the variable has the same value for all invocations
 * of the group.
 *
 * This divergence analysis pass expects the shader to be in LCSSA-form.
 *
 * This algorithm implements "The Simple Divergence Analysis" from
 * Diogo Sampaio, Rafael De Souza, Sylvain Collange, Fernando Magno Quintão Pereira.
 * Divergence Analysis.  ACM Transactions on Programming Languages and Systems (TOPLAS),
 * ACM, 2013, 35 (4), pp.13:1-13:36. <10.1145/2523815>. <hal-00909072v2>
 */

static bool
visit_cf_list(bool *divergent, struct exec_list *list,
              nir_divergence_options options, gl_shader_stage stage);

static bool
visit_alu(bool *divergent, nir_alu_instr *instr)
{
   if (divergent[instr->dest.dest.ssa.index])
      return false;

   unsigned num_src = nir_op_infos[instr->op].num_inputs;

   for (unsigned i = 0; i < num_src; i++) {
      if (divergent[instr->src[i].src.ssa->index]) {
         divergent[instr->dest.dest.ssa.index] = true;
         return true;
      }
   }

   return false;
}

static bool
visit_intrinsic(bool *divergent, nir_intrinsic_instr *instr,
                nir_divergence_options options, gl_shader_stage stage)
{
   if (!nir_intrinsic_infos[instr->intrinsic].has_dest)
      return false;

   if (divergent[instr->dest.ssa.index])
      return false;

   bool is_divergent = false;
   switch (instr->intrinsic) {
   /* Intrinsics which are always uniform */
   case nir_intrinsic_shader_clock:
   case nir_intrinsic_ballot:
   case nir_intrinsic_read_invocation:
   case nir_intrinsic_read_first_invocation:
   case nir_intrinsic_vote_any:
   case nir_intrinsic_vote_all:
   case nir_intrinsic_vote_feq:
   case nir_intrinsic_vote_ieq:
   case nir_intrinsic_load_work_dim:
   case nir_intrinsic_load_work_group_id:
   case nir_intrinsic_load_num_work_groups:
   case nir_intrinsic_load_local_group_size:
   case nir_intrinsic_load_subgroup_id:
   case nir_intrinsic_load_num_subgroups:
   case nir_intrinsic_load_subgroup_size:
   case nir_intrinsic_load_subgroup_eq_mask:
   case nir_intrinsic_load_subgroup_ge_mask:
   case nir_intrinsic_load_subgroup_gt_mask:
   case nir_intrinsic_load_subgroup_le_mask:
   case nir_intrinsic_load_subgroup_lt_mask:
   case nir_intrinsic_first_invocation:
   case nir_intrinsic_load_base_instance:
   case nir_intrinsic_load_base_vertex:
   case nir_intrinsic_load_first_vertex:
   case nir_intrinsic_load_draw_id:
   case nir_intrinsic_load_is_indexed_draw:
   case nir_intrinsic_load_viewport_scale:
   case nir_intrinsic_load_alpha_ref_float:
   case nir_intrinsic_load_user_clip_plane:
   case nir_intrinsic_load_viewport_x_scale:
   case nir_intrinsic_load_viewport_y_scale:
   case nir_intrinsic_load_viewport_z_scale:
   case nir_intrinsic_load_viewport_offset:
   case nir_intrinsic_load_viewport_z_offset:
   case nir_intrinsic_load_blend_const_color_a_float:
   case nir_intrinsic_load_blend_const_color_b_float:
   case nir_intrinsic_load_blend_const_color_g_float:
   case nir_intrinsic_load_blend_const_color_r_float:
   case nir_intrinsic_load_blend_const_color_rgba:
   case nir_intrinsic_load_blend_const_color_aaaa8888_unorm:
   case nir_intrinsic_load_blend_const_color_rgba8888_unorm:
      is_divergent = false;
      break;

   /* Intrinsics with divergence depending on shader stage and hardware */
   case nir_intrinsic_load_input:
      is_divergent = divergent[instr->src[0].ssa->index];
      if (stage == MESA_SHADER_FRAGMENT)
         is_divergent |= !(options & nir_divergence_single_prim_per_subgroup);
      else if (stage == MESA_SHADER_TESS_EVAL)
         is_divergent |= !(options & nir_divergence_single_patch_per_tes_subgroup);
      else
         is_divergent = true;
      break;
   case nir_intrinsic_load_output:
      assert(stage == MESA_SHADER_TESS_CTRL || stage == MESA_SHADER_FRAGMENT);
      is_divergent = divergent[instr->src[0].ssa->index];
      if (stage == MESA_SHADER_TESS_CTRL)
         is_divergent |= !(options & nir_divergence_single_patch_per_tcs_subgroup);
      else
         is_divergent = true;
      break;
   case nir_intrinsic_load_layer_id:
   case nir_intrinsic_load_front_face:
      assert(stage == MESA_SHADER_FRAGMENT);
      is_divergent = !(options & nir_divergence_single_prim_per_subgroup);
      break;
   case nir_intrinsic_load_view_index:
      assert(stage != MESA_SHADER_COMPUTE && stage != MESA_SHADER_KERNEL);
      if (options & nir_divergence_view_index_uniform)
         is_divergent = false;
      else if (stage == MESA_SHADER_FRAGMENT)
         is_divergent = !(options & nir_divergence_single_prim_per_subgroup);
      break;
   case nir_intrinsic_load_fs_input_interp_deltas:
      assert(stage == MESA_SHADER_FRAGMENT);
      is_divergent = divergent[instr->src[0].ssa->index];
      is_divergent |= !(options & nir_divergence_single_prim_per_subgroup);
      break;
   case nir_intrinsic_load_primitive_id:
      if (stage == MESA_SHADER_FRAGMENT)
         is_divergent = !(options & nir_divergence_single_prim_per_subgroup);
      else if (stage == MESA_SHADER_TESS_CTRL)
         is_divergent = !(options & nir_divergence_single_patch_per_tcs_subgroup);
      else if (stage == MESA_SHADER_TESS_EVAL)
         is_divergent = !(options & nir_divergence_single_patch_per_tes_subgroup);
      else
         unreachable("Invalid stage for load_primitive_id");
      break;
   case nir_intrinsic_load_tess_level_inner:
   case nir_intrinsic_load_tess_level_outer:
      if (stage == MESA_SHADER_TESS_CTRL)
         is_divergent = !(options & nir_divergence_single_patch_per_tcs_subgroup);
      else if (stage == MESA_SHADER_TESS_EVAL)
         is_divergent = !(options & nir_divergence_single_patch_per_tes_subgroup);
      else
         unreachable("Invalid stage for load_primitive_tess_level_*");
      break;
   case nir_intrinsic_load_patch_vertices_in:
      if (stage == MESA_SHADER_TESS_EVAL)
         is_divergent = !(options & nir_divergence_single_patch_per_tes_subgroup);
      else
         assert(stage == MESA_SHADER_TESS_CTRL);
      break;

   /* Clustered reductions are uniform if cluster_size == subgroup_size or
    * the source is uniform and the operation is invariant.
    * Inclusive scans are uniform if
    * the source is uniform and the operation is invariant
    */
   case nir_intrinsic_reduce:
      if (nir_intrinsic_cluster_size(instr) == 0)
         return false;
      /* fallthrough */
   case nir_intrinsic_inclusive_scan: {
      nir_op op = nir_intrinsic_reduction_op(instr);
      is_divergent = divergent[instr->src[0].ssa->index];
      if (op != nir_op_umin && op != nir_op_imin && op != nir_op_fmin &&
          op != nir_op_umax && op != nir_op_imax && op != nir_op_fmax &&
          op != nir_op_iand && op != nir_op_ior)
         is_divergent = true;
      break;
   }

   /* Intrinsics with divergence depending on sources */
   case nir_intrinsic_ballot_bitfield_extract:
   case nir_intrinsic_ballot_find_lsb:
   case nir_intrinsic_ballot_find_msb:
   case nir_intrinsic_ballot_bit_count_reduce:
   case nir_intrinsic_shuffle:
   case nir_intrinsic_shuffle_xor:
   case nir_intrinsic_shuffle_up:
   case nir_intrinsic_shuffle_down:
   case nir_intrinsic_quad_broadcast:
   case nir_intrinsic_quad_swap_horizontal:
   case nir_intrinsic_quad_swap_vertical:
   case nir_intrinsic_quad_swap_diagonal:
   case nir_intrinsic_load_deref:
   case nir_intrinsic_load_ubo:
   case nir_intrinsic_load_ssbo:
   case nir_intrinsic_load_shared:
   case nir_intrinsic_load_global:
   case nir_intrinsic_load_uniform:
   case nir_intrinsic_load_push_constant:
   case nir_intrinsic_load_constant:
   case nir_intrinsic_load_sample_pos_from_id:
   case nir_intrinsic_load_kernel_input:
   case nir_intrinsic_image_load:
   case nir_intrinsic_image_deref_load:
   case nir_intrinsic_bindless_image_load:
   case nir_intrinsic_image_samples:
   case nir_intrinsic_image_deref_samples:
   case nir_intrinsic_bindless_image_samples:
   case nir_intrinsic_get_buffer_size:
   case nir_intrinsic_image_size:
   case nir_intrinsic_image_deref_size:
   case nir_intrinsic_bindless_image_size:
   case nir_intrinsic_copy_deref:
   case nir_intrinsic_deref_buffer_array_length:
   case nir_intrinsic_vulkan_resource_index:
   case nir_intrinsic_vulkan_resource_reindex:
   case nir_intrinsic_load_vulkan_descriptor:
   case nir_intrinsic_atomic_counter_read:
   case nir_intrinsic_atomic_counter_read_deref:
   case nir_intrinsic_quad_swizzle_amd:
   case nir_intrinsic_masked_swizzle_amd: {
      unsigned num_srcs = nir_intrinsic_infos[instr->intrinsic].num_srcs;
      for (unsigned i = 0; i < num_srcs; i++) {
         if (divergent[instr->src[i].ssa->index]) {
            is_divergent = true;
            break;
         }
      }
      break;
   }

   /* Intrinsics which are always divergent */
   case nir_intrinsic_load_color0:
   case nir_intrinsic_load_color1:
   case nir_intrinsic_load_param:
   case nir_intrinsic_load_sample_id:
   case nir_intrinsic_load_sample_id_no_per_sample:
   case nir_intrinsic_load_sample_mask_in:
   case nir_intrinsic_load_interpolated_input:
   case nir_intrinsic_load_barycentric_pixel:
   case nir_intrinsic_load_barycentric_centroid:
   case nir_intrinsic_load_barycentric_sample:
   case nir_intrinsic_load_barycentric_at_sample:
   case nir_intrinsic_load_barycentric_at_offset:
   case nir_intrinsic_interp_deref_at_offset:
   case nir_intrinsic_interp_deref_at_sample:
   case nir_intrinsic_interp_deref_at_centroid:
   case nir_intrinsic_load_tess_coord:
   case nir_intrinsic_load_point_coord:
   case nir_intrinsic_load_frag_coord:
   case nir_intrinsic_load_sample_pos:
   case nir_intrinsic_load_vertex_id_zero_base:
   case nir_intrinsic_load_vertex_id:
   case nir_intrinsic_load_per_vertex_input:
   case nir_intrinsic_load_per_vertex_output:
   case nir_intrinsic_load_instance_id:
   case nir_intrinsic_load_invocation_id:
   case nir_intrinsic_load_local_invocation_id:
   case nir_intrinsic_load_local_invocation_index:
   case nir_intrinsic_load_global_invocation_id:
   case nir_intrinsic_load_global_invocation_index:
   case nir_intrinsic_load_subgroup_invocation:
   case nir_intrinsic_load_helper_invocation:
   case nir_intrinsic_is_helper_invocation:
   case nir_intrinsic_load_scratch:
   case nir_intrinsic_deref_atomic_add:
   case nir_intrinsic_deref_atomic_imin:
   case nir_intrinsic_deref_atomic_umin:
   case nir_intrinsic_deref_atomic_imax:
   case nir_intrinsic_deref_atomic_umax:
   case nir_intrinsic_deref_atomic_and:
   case nir_intrinsic_deref_atomic_or:
   case nir_intrinsic_deref_atomic_xor:
   case nir_intrinsic_deref_atomic_exchange:
   case nir_intrinsic_deref_atomic_comp_swap:
   case nir_intrinsic_deref_atomic_fadd:
   case nir_intrinsic_deref_atomic_fmin:
   case nir_intrinsic_deref_atomic_fmax:
   case nir_intrinsic_deref_atomic_fcomp_swap:
   case nir_intrinsic_ssbo_atomic_add:
   case nir_intrinsic_ssbo_atomic_imin:
   case nir_intrinsic_ssbo_atomic_umin:
   case nir_intrinsic_ssbo_atomic_imax:
   case nir_intrinsic_ssbo_atomic_umax:
   case nir_intrinsic_ssbo_atomic_and:
   case nir_intrinsic_ssbo_atomic_or:
   case nir_intrinsic_ssbo_atomic_xor:
   case nir_intrinsic_ssbo_atomic_exchange:
   case nir_intrinsic_ssbo_atomic_comp_swap:
   case nir_intrinsic_ssbo_atomic_fadd:
   case nir_intrinsic_ssbo_atomic_fmax:
   case nir_intrinsic_ssbo_atomic_fmin:
   case nir_intrinsic_ssbo_atomic_fcomp_swap:
   case nir_intrinsic_image_deref_atomic_add:
   case nir_intrinsic_image_deref_atomic_imin:
   case nir_intrinsic_image_deref_atomic_umin:
   case nir_intrinsic_image_deref_atomic_imax:
   case nir_intrinsic_image_deref_atomic_umax:
   case nir_intrinsic_image_deref_atomic_and:
   case nir_intrinsic_image_deref_atomic_or:
   case nir_intrinsic_image_deref_atomic_xor:
   case nir_intrinsic_image_deref_atomic_exchange:
   case nir_intrinsic_image_deref_atomic_comp_swap:
   case nir_intrinsic_image_deref_atomic_fadd:
   case nir_intrinsic_image_atomic_add:
   case nir_intrinsic_image_atomic_imin:
   case nir_intrinsic_image_atomic_umin:
   case nir_intrinsic_image_atomic_imax:
   case nir_intrinsic_image_atomic_umax:
   case nir_intrinsic_image_atomic_and:
   case nir_intrinsic_image_atomic_or:
   case nir_intrinsic_image_atomic_xor:
   case nir_intrinsic_image_atomic_exchange:
   case nir_intrinsic_image_atomic_comp_swap:
   case nir_intrinsic_image_atomic_fadd:
   case nir_intrinsic_bindless_image_atomic_add:
   case nir_intrinsic_bindless_image_atomic_imin:
   case nir_intrinsic_bindless_image_atomic_umin:
   case nir_intrinsic_bindless_image_atomic_imax:
   case nir_intrinsic_bindless_image_atomic_umax:
   case nir_intrinsic_bindless_image_atomic_and:
   case nir_intrinsic_bindless_image_atomic_or:
   case nir_intrinsic_bindless_image_atomic_xor:
   case nir_intrinsic_bindless_image_atomic_exchange:
   case nir_intrinsic_bindless_image_atomic_comp_swap:
   case nir_intrinsic_bindless_image_atomic_fadd:
   case nir_intrinsic_shared_atomic_add:
   case nir_intrinsic_shared_atomic_imin:
   case nir_intrinsic_shared_atomic_umin:
   case nir_intrinsic_shared_atomic_imax:
   case nir_intrinsic_shared_atomic_umax:
   case nir_intrinsic_shared_atomic_and:
   case nir_intrinsic_shared_atomic_or:
   case nir_intrinsic_shared_atomic_xor:
   case nir_intrinsic_shared_atomic_exchange:
   case nir_intrinsic_shared_atomic_comp_swap:
   case nir_intrinsic_shared_atomic_fadd:
   case nir_intrinsic_shared_atomic_fmin:
   case nir_intrinsic_shared_atomic_fmax:
   case nir_intrinsic_shared_atomic_fcomp_swap:
   case nir_intrinsic_global_atomic_add:
   case nir_intrinsic_global_atomic_imin:
   case nir_intrinsic_global_atomic_umin:
   case nir_intrinsic_global_atomic_imax:
   case nir_intrinsic_global_atomic_umax:
   case nir_intrinsic_global_atomic_and:
   case nir_intrinsic_global_atomic_or:
   case nir_intrinsic_global_atomic_xor:
   case nir_intrinsic_global_atomic_exchange:
   case nir_intrinsic_global_atomic_comp_swap:
   case nir_intrinsic_global_atomic_fadd:
   case nir_intrinsic_global_atomic_fmin:
   case nir_intrinsic_global_atomic_fmax:
   case nir_intrinsic_global_atomic_fcomp_swap:
   case nir_intrinsic_atomic_counter_add:
   case nir_intrinsic_atomic_counter_min:
   case nir_intrinsic_atomic_counter_max:
   case nir_intrinsic_atomic_counter_and:
   case nir_intrinsic_atomic_counter_or:
   case nir_intrinsic_atomic_counter_xor:
   case nir_intrinsic_atomic_counter_inc:
   case nir_intrinsic_atomic_counter_pre_dec:
   case nir_intrinsic_atomic_counter_post_dec:
   case nir_intrinsic_atomic_counter_exchange:
   case nir_intrinsic_atomic_counter_comp_swap:
   case nir_intrinsic_atomic_counter_add_deref:
   case nir_intrinsic_atomic_counter_min_deref:
   case nir_intrinsic_atomic_counter_max_deref:
   case nir_intrinsic_atomic_counter_and_deref:
   case nir_intrinsic_atomic_counter_or_deref:
   case nir_intrinsic_atomic_counter_xor_deref:
   case nir_intrinsic_atomic_counter_inc_deref:
   case nir_intrinsic_atomic_counter_pre_dec_deref:
   case nir_intrinsic_atomic_counter_post_dec_deref:
   case nir_intrinsic_atomic_counter_exchange_deref:
   case nir_intrinsic_atomic_counter_comp_swap_deref:
   case nir_intrinsic_exclusive_scan:
   case nir_intrinsic_ballot_bit_count_exclusive:
   case nir_intrinsic_ballot_bit_count_inclusive:
   case nir_intrinsic_write_invocation_amd:
   case nir_intrinsic_mbcnt_amd:
      is_divergent = true;
      break;

   default:
#ifdef NDEBUG
      is_divergent = true;
      break;
#else
      nir_print_instr(&instr->instr, stderr);
      unreachable("\nNIR divergence analysis: Unhandled intrinsic.");
#endif
   }

   divergent[instr->dest.ssa.index] = is_divergent;
   return is_divergent;
}

static bool
visit_tex(bool *divergent, nir_tex_instr *instr)
{
   if (divergent[instr->dest.ssa.index])
      return false;

   bool is_divergent = false;

   for (unsigned i = 0; i < instr->num_srcs; i++) {
      switch (instr->src[i].src_type) {
      case nir_tex_src_sampler_deref:
      case nir_tex_src_sampler_handle:
      case nir_tex_src_sampler_offset:
         is_divergent |= divergent[instr->src[i].src.ssa->index] &&
                         instr->sampler_non_uniform;
         break;
      case nir_tex_src_texture_deref:
      case nir_tex_src_texture_handle:
      case nir_tex_src_texture_offset:
         is_divergent |= divergent[instr->src[i].src.ssa->index] &&
                         instr->texture_non_uniform;
         break;
      default:
         is_divergent |= divergent[instr->src[i].src.ssa->index];
         break;
      }
   }

   divergent[instr->dest.ssa.index] = is_divergent;
   return is_divergent;
}

static bool
visit_phi(bool *divergent, nir_phi_instr *instr)
{
   /* There are 3 types of phi instructions:
    * (1) gamma: represent the joining point of different paths
    *     created by an “if-then-else” branch.
    *     The resulting value is divergent if the branch condition
    *     or any of the source values is divergent.
    *
    * (2) mu: which only exist at loop headers,
    *     merge initial and loop-carried values.
    *     The resulting value is divergent if any source value
    *     is divergent or a divergent loop continue condition
    *     is associated with a different ssa-def.
    *
    * (3) eta: represent values that leave a loop.
    *     The resulting value is divergent if the source value is divergent
    *     or any loop exit condition is divergent for a value which is
    *     not loop-invariant.
    *     (note: there should be no phi for loop-invariant variables.)
    */

   if (divergent[instr->dest.ssa.index])
      return false;

   nir_foreach_phi_src(src, instr) {
      /* if any source value is divergent, the resulting value is divergent */
      if (divergent[src->src.ssa->index]) {
         divergent[instr->dest.ssa.index] = true;
         return true;
      }
   }

   nir_cf_node *prev = nir_cf_node_prev(&instr->instr.block->cf_node);

   if (!prev) {
      /* mu: if no predecessor node exists, the phi must be at a loop header */
      nir_loop *loop = nir_cf_node_as_loop(instr->instr.block->cf_node.parent);
      prev = nir_cf_node_prev(&loop->cf_node);
      nir_ssa_def* same = NULL;
      bool all_same = true;

      /* first, check if all loop-carried values are from the same ssa-def */
      nir_foreach_phi_src(src, instr) {
         if (src->pred == nir_cf_node_as_block(prev))
            continue;
         if (src->src.ssa->parent_instr->type == nir_instr_type_ssa_undef)
            continue;
         if (!same)
            same = src->src.ssa;
         else if (same != src->src.ssa)
            all_same = false;
      }

      /* if all loop-carried values are the same, the resulting value is uniform */
      if (all_same)
         return false;

      /* check if the loop-carried values come from different ssa-defs
       * and the corresponding condition is divergent. */
      nir_foreach_phi_src(src, instr) {
         /* skip the loop preheader */
         if (src->pred == nir_cf_node_as_block(prev))
            continue;

         /* skip the unconditional back-edge */
         if (src->pred == nir_loop_last_block(loop))
            continue;

         /* if the value is undef, we don't need to check the condition */
         if (src->src.ssa->parent_instr->type == nir_instr_type_ssa_undef)
            continue;

         nir_cf_node *current = src->pred->cf_node.parent;
         /* check recursively the conditions if any is divergent */
         while (current->type != nir_cf_node_loop) {
            assert (current->type == nir_cf_node_if);
            nir_if *if_node = nir_cf_node_as_if(current);
            if (divergent[if_node->condition.ssa->index]) {
               divergent[instr->dest.ssa.index] = true;
               return true;
            }
            current = current->parent;
         }
         assert(current == &loop->cf_node);
      }

   } else if (prev->type == nir_cf_node_if) {
      /* if only one of the incoming values is defined, the resulting value is uniform */
      unsigned defined_srcs = 0;
      nir_foreach_phi_src(src, instr) {
         if (src->src.ssa->parent_instr->type != nir_instr_type_ssa_undef)
            defined_srcs++;
      }
      if (defined_srcs <= 1)
         return false;

      /* gamma: check if the condition is divergent */
      nir_if *if_node = nir_cf_node_as_if(prev);
      if (divergent[if_node->condition.ssa->index]) {
         divergent[instr->dest.ssa.index] = true;
         return true;
      }

   } else {
      /* eta: the predecessor must be a loop */
      assert(prev->type == nir_cf_node_loop);

      /* Check if any loop exit condition is divergent:
       * That is any break happens under divergent condition or
       * a break is preceeded by a divergent continue
       */
      nir_foreach_phi_src(src, instr) {
         nir_cf_node *current = src->pred->cf_node.parent;

         /* check recursively the conditions if any is divergent */
         while (current->type != nir_cf_node_loop) {
            assert(current->type == nir_cf_node_if);
            nir_if *if_node = nir_cf_node_as_if(current);
            if (divergent[if_node->condition.ssa->index]) {
               divergent[instr->dest.ssa.index] = true;
               return true;
            }
            current = current->parent;
         }
         assert(current == prev);

         /* check if any divergent continue happened before the break */
         nir_foreach_block_in_cf_node(block, prev) {
            if (block == src->pred)
               break;
            if (!nir_block_ends_in_jump(block))
               continue;

            nir_jump_instr *jump = nir_instr_as_jump(nir_block_last_instr(block));
            if (jump->type != nir_jump_continue)
               continue;

            current = block->cf_node.parent;
            bool is_divergent = false;
            while (current != prev) {
               /* the continue belongs to an inner loop */
               if (current->type == nir_cf_node_loop) {
                  is_divergent = false;
                  break;
               }
               assert(current->type == nir_cf_node_if);
               nir_if *if_node = nir_cf_node_as_if(current);
               is_divergent |= divergent[if_node->condition.ssa->index];
               current = current->parent;
            }

            if (is_divergent) {
               divergent[instr->dest.ssa.index] = true;
               return true;
            }
         }
      }
   }

   return false;
}

static bool
visit_load_const(bool *divergent, nir_load_const_instr *instr)
{
   return false;
}

static bool
visit_ssa_undef(bool *divergent, nir_ssa_undef_instr *instr)
{
   return false;
}

static bool
nir_variable_mode_is_uniform(nir_variable_mode mode) {
   switch (mode) {
   case nir_var_uniform:
   case nir_var_mem_ubo:
   case nir_var_mem_ssbo:
   case nir_var_mem_shared:
   case nir_var_mem_global:
      return true;
   default:
      return false;
   }
}

static bool
nir_variable_is_uniform(nir_variable *var, nir_divergence_options options,
                        gl_shader_stage stage)
{
   if (nir_variable_mode_is_uniform(var->data.mode))
      return true;

   if (stage == MESA_SHADER_FRAGMENT &&
       (options & nir_divergence_single_prim_per_subgroup) &&
       var->data.mode == nir_var_shader_in &&
       var->data.interpolation == INTERP_MODE_FLAT)
      return true;

   if (stage == MESA_SHADER_TESS_CTRL &&
       (options & nir_divergence_single_patch_per_tcs_subgroup) &&
       var->data.mode == nir_var_shader_out && var->data.patch)
      return true;

   if (stage == MESA_SHADER_TESS_EVAL &&
       (options & nir_divergence_single_patch_per_tes_subgroup) &&
       var->data.mode == nir_var_shader_in && var->data.patch)
      return true;

   return false;
}

static bool
visit_deref(bool *divergent, nir_deref_instr *deref,
            nir_divergence_options options, gl_shader_stage stage)
{
   if (divergent[deref->dest.ssa.index])
      return false;

   bool is_divergent = false;
   switch (deref->deref_type) {
   case nir_deref_type_var:
      is_divergent = !nir_variable_is_uniform(deref->var, options, stage);
      break;
   case nir_deref_type_array:
   case nir_deref_type_ptr_as_array:
      is_divergent = divergent[deref->arr.index.ssa->index];
      /* fallthrough */
   case nir_deref_type_struct:
   case nir_deref_type_array_wildcard:
      is_divergent |= divergent[deref->parent.ssa->index];
      break;
   case nir_deref_type_cast:
      is_divergent = !nir_variable_mode_is_uniform(deref->var->data.mode) ||
                     divergent[deref->parent.ssa->index];
      break;
   }

   divergent[deref->dest.ssa.index] = is_divergent;
   return is_divergent;
}

static bool
visit_block(bool *divergent, nir_block *block, nir_divergence_options options,
            gl_shader_stage stage)
{
   bool has_changed = false;

   nir_foreach_instr(instr, block) {
      switch (instr->type) {
      case nir_instr_type_alu:
         has_changed |= visit_alu(divergent, nir_instr_as_alu(instr));
         break;
      case nir_instr_type_intrinsic:
         has_changed |= visit_intrinsic(divergent, nir_instr_as_intrinsic(instr),
                                        options, stage);
         break;
      case nir_instr_type_tex:
         has_changed |= visit_tex(divergent, nir_instr_as_tex(instr));
         break;
      case nir_instr_type_phi:
         has_changed |= visit_phi(divergent, nir_instr_as_phi(instr));
         break;
      case nir_instr_type_load_const:
         has_changed |= visit_load_const(divergent, nir_instr_as_load_const(instr));
         break;
      case nir_instr_type_ssa_undef:
         has_changed |= visit_ssa_undef(divergent, nir_instr_as_ssa_undef(instr));
         break;
      case nir_instr_type_deref:
         has_changed |= visit_deref(divergent, nir_instr_as_deref(instr),
                                    options, stage);
         break;
      case nir_instr_type_jump:
         break;
      case nir_instr_type_call:
      case nir_instr_type_parallel_copy:
         unreachable("NIR divergence analysis: Unsupported instruction type.");
      }
   }

   return has_changed;
}

static bool
visit_if(bool *divergent, nir_if *if_stmt, nir_divergence_options options, gl_shader_stage stage)
{
   return visit_cf_list(divergent, &if_stmt->then_list, options, stage) |
          visit_cf_list(divergent, &if_stmt->else_list, options, stage);
}

static bool
visit_loop(bool *divergent, nir_loop *loop, nir_divergence_options options, gl_shader_stage stage)
{
   bool has_changed = false;
   bool repeat = true;

   /* TODO: restructure this and the phi handling more efficiently */
   while (repeat) {
      repeat = visit_cf_list(divergent, &loop->body, options, stage);
      has_changed |= repeat;
   }

   return has_changed;
}

static bool
visit_cf_list(bool *divergent, struct exec_list *list,
              nir_divergence_options options, gl_shader_stage stage)
{
   bool has_changed = false;

   foreach_list_typed(nir_cf_node, node, node, list) {
      switch (node->type) {
      case nir_cf_node_block:
         has_changed |= visit_block(divergent, nir_cf_node_as_block(node),
                                    options, stage);
         break;
      case nir_cf_node_if:
         has_changed |= visit_if(divergent, nir_cf_node_as_if(node),
                                 options, stage);
         break;
      case nir_cf_node_loop:
         has_changed |= visit_loop(divergent, nir_cf_node_as_loop(node),
                                   options, stage);
         break;
      case nir_cf_node_function:
         unreachable("NIR divergence analysis: Unsupported cf_node type.");
      }
   }

   return has_changed;
}


bool*
nir_divergence_analysis(nir_shader *shader, nir_divergence_options options)
{
   nir_function_impl *impl = nir_shader_get_entrypoint(shader);
   bool *t = rzalloc_array(shader, bool, impl->ssa_alloc);

   visit_cf_list(t, &impl->body, options, shader->info.stage);

   return t;
}
