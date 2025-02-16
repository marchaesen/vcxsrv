/*
 * Copyright © 2021 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "ac_nir.h"
#include "ac_nir_helpers.h"
#include "ac_gpu_info.h"
#include "amdgfxregs.h"
#include "nir_builder.h"
#include "nir_xfb_info.h"
#include "util/u_math.h"
#include "util/u_vector.h"

enum {
   nggc_passflag_used_by_pos = 1,
   nggc_passflag_used_by_other = 2,
   nggc_passflag_used_by_both = nggc_passflag_used_by_pos | nggc_passflag_used_by_other,
};

typedef struct
{
   nir_def *ssa;
   nir_variable *var;
} reusable_nondeferred_variable;

typedef struct
{
   const ac_nir_lower_ngg_options *options;

   nir_variable *position_value_var;
   nir_variable *prim_exp_arg_var;

   /**
    * Whether the current invocation's vertex (if any) is accepted by the culling algorithm.
    * Only used when culling is enabled.
    */
   nir_variable *es_accepted_var;

   /**
    * hether the current invocation's primitive (if any) is accepted by the culling algorithm.
    * Only used when culling is enabled.
    */
   nir_variable *gs_accepted_var;

   /**
    * Whether the current invocation's primitive (if any) should be exported.
    * Initially set to whether the invocation has a vertex, then set to false by the culling
    * algorithm if the primitive is rejected.
    */
   nir_variable *gs_exported_var;

   nir_variable *gs_vtx_indices_vars[3];

   nir_def *vtx_addr[3];

   struct u_vector reusable_nondeferred_variables;

   bool early_prim_export;
   bool streamout_enabled;
   bool has_user_edgeflags;
   bool skip_primitive_id;
   unsigned max_num_waves;

   /* LDS params */
   unsigned pervertex_lds_bytes;

   uint64_t inputs_needed_by_pos;
   uint64_t inputs_needed_by_others;

   nir_instr *compact_arg_stores[4];
   nir_intrinsic_instr *overwrite_args;
   nir_variable *repacked_rel_patch_id;

   /* clip distance */
   nir_variable *clip_vertex_var;
   nir_variable *clipdist_neg_mask_var;
   bool has_clipdist;

   /* outputs */
   ac_nir_prerast_out out;
} lower_ngg_nogs_state;

/* Per-vertex LDS layout of culling shaders */
enum {
   /* Position of the ES vertex (at the beginning for alignment reasons) */
   lds_es_pos_x = 0,
   lds_es_pos_y = 4,
   lds_es_pos_z = 8,
   lds_es_pos_w = 12,

   /* 1 when the vertex is accepted, 0 if it should be culled */
   lds_es_vertex_accepted = 16,
   /* ID of the thread which will export the current thread's vertex */
   lds_es_exporter_tid = 17,
   /* bit i is set when the i'th clip distance of a vertex is negative */
   lds_es_clipdist_neg_mask = 18,
   /* TES only, relative patch ID, less than max workgroup size */
   lds_es_tes_rel_patch_id = 19,

   /* Repacked arguments - also listed separately for VS and TES */
   lds_es_arg_0 = 20,
};

static nir_def *
pervertex_lds_addr(nir_builder *b, nir_def *vertex_idx, unsigned per_vtx_bytes)
{
   return nir_imul_imm(b, vertex_idx, per_vtx_bytes);
}

static void
ngg_nogs_init_vertex_indices_vars(nir_builder *b, nir_function_impl *impl, lower_ngg_nogs_state *s)
{
   for (unsigned v = 0; v < s->options->num_vertices_per_primitive; ++v) {
      s->gs_vtx_indices_vars[v] = nir_local_variable_create(impl, glsl_uint_type(), "gs_vtx_addr");

      nir_def *vtx;

      if (s->options->hw_info->gfx_level >= GFX12) {
         vtx = nir_ubfe_imm(b, nir_load_packed_passthrough_primitive_amd(b), 9 * v, 8);
      } else if (s->options->passthrough) {
         vtx = nir_ubfe_imm(b, nir_load_packed_passthrough_primitive_amd(b), 10 * v, 9);
      } else {
         vtx = nir_ubfe_imm(b, nir_load_gs_vertex_offset_amd(b, .base = v / 2u),
                            (v & 1u) * 16u, 16u);
      }

      nir_store_var(b, s->gs_vtx_indices_vars[v], vtx, 0x1);
   }
}

static nir_def *
emit_ngg_nogs_prim_exp_arg(nir_builder *b, lower_ngg_nogs_state *s)
{
   if (s->options->hw_info->gfx_level >= GFX12 || s->options->passthrough) {
      return nir_load_packed_passthrough_primitive_amd(b);
   } else {
      nir_def *vtx_idx[3] = {0};

      for (unsigned v = 0; v < s->options->num_vertices_per_primitive; ++v)
         vtx_idx[v] = nir_load_var(b, s->gs_vtx_indices_vars[v]);

      return ac_nir_pack_ngg_prim_exp_arg(b, s->options->num_vertices_per_primitive, vtx_idx, NULL,
                                        s->options->hw_info->gfx_level);
   }
}

static nir_def *
has_input_vertex(nir_builder *b)
{
   return nir_is_subgroup_invocation_lt_amd(b, nir_load_merged_wave_info_amd(b));
}

static nir_def *
has_input_primitive(nir_builder *b)
{
   return nir_is_subgroup_invocation_lt_amd(b, nir_load_merged_wave_info_amd(b), .base = 8);
}

static void
nogs_prim_gen_query(nir_builder *b, lower_ngg_nogs_state *s)
{
   if (!s->options->has_gen_prim_query)
      return;

   nir_if *if_shader_query = nir_push_if(b, nir_load_prim_gen_query_enabled_amd(b));
   {
      /* Activate only 1 lane and add the number of primitives to query result. */
      nir_if *if_elected = nir_push_if(b, nir_elect(b, 1));
      {
         /* Number of input primitives in the current wave. */
         nir_def *num_input_prims = nir_ubfe_imm(b, nir_load_merged_wave_info_amd(b),
                                                     8, 8);

         /* Add to stream 0 primitive generated counter. */
         nir_atomic_add_gen_prim_count_amd(b, num_input_prims, .stream_id = 0);
      }
      nir_pop_if(b, if_elected);
   }
   nir_pop_if(b, if_shader_query);
}

static nir_if *
emit_ngg_nogs_prim_export(nir_builder *b, lower_ngg_nogs_state *s, nir_def *arg)
{
   nir_if *if_gs_thread = nir_push_if(b, nir_load_var(b, s->gs_exported_var));
   {
      if (!arg)
         arg = emit_ngg_nogs_prim_exp_arg(b, s);

      /* pack user edge flag info into arg */
      if (s->has_user_edgeflags) {
         /* Workgroup barrier: wait for ES threads store user edge flags to LDS */
         nir_barrier(b, .execution_scope = SCOPE_WORKGROUP,
                            .memory_scope = SCOPE_WORKGROUP,
                            .memory_semantics = NIR_MEMORY_ACQ_REL,
                            .memory_modes = nir_var_mem_shared);

         unsigned edge_flag_bits = ac_get_all_edge_flag_bits(s->options->hw_info->gfx_level);
         nir_def *mask = nir_imm_intN_t(b, ~edge_flag_bits, 32);

         unsigned edge_flag_offset = 0;
         if (s->streamout_enabled) {
            unsigned packed_location =
               util_bitcount64(b->shader->info.outputs_written &
                               BITFIELD64_MASK(VARYING_SLOT_EDGE));
            edge_flag_offset = packed_location * 16;
         }

         for (int i = 0; i < s->options->num_vertices_per_primitive; i++) {
            nir_def *vtx_idx = nir_load_var(b, s->gs_vtx_indices_vars[i]);
            nir_def *addr = pervertex_lds_addr(b, vtx_idx, s->pervertex_lds_bytes);
            nir_def *edge = nir_load_shared(b, 1, 32, addr, .base = edge_flag_offset);

            if (s->options->hw_info->gfx_level >= GFX12)
               mask = nir_ior(b, mask, nir_ishl_imm(b, edge, 8 + i * 9));
            else
               mask = nir_ior(b, mask, nir_ishl_imm(b, edge, 9 + i * 10));
         }
         arg = nir_iand(b, arg, mask);
      }

      ac_nir_export_primitive(b, arg, NULL);

      /* Store implicit primitive ID when configured as a per-primitive output on
       * GPUs without an attribute ring.
       * Because this uses the export space, do it together with the primitive export.
       */
      if (!s->options->hw_info->has_attr_ring && s->options->export_primitive_id_per_prim) {
         const uint8_t offset = s->options->vs_output_param_offset[VARYING_SLOT_PRIMITIVE_ID];
         nir_def *prim_id = nir_load_primitive_id(b);
         nir_def *undef = nir_undef(b, 1, 32);
         ac_nir_prerast_out out = {
            .infos = {{.components_mask = 1, .as_varying_mask = 1}},
            .outputs = {{prim_id, undef, undef, undef}}
         };

         ac_nir_export_parameters(b, &offset, 1, 0, &out);
      }
   }
   nir_pop_if(b, if_gs_thread);
   return if_gs_thread;
}

static void
emit_ngg_nogs_prim_id_store_shared(nir_builder *b, lower_ngg_nogs_state *s)
{
   nir_def *gs_thread =
      s->gs_accepted_var ? nir_load_var(b, s->gs_accepted_var) : has_input_primitive(b);

   nir_if *if_gs_thread = nir_push_if(b, gs_thread);
   {
      /* Copy Primitive IDs from GS threads to the LDS address
       * corresponding to the ES thread of the provoking vertex.
       * It will be exported as a per-vertex attribute.
       */
      nir_def *gs_vtx_indices[3];
      for (unsigned i = 0; i < s->options->num_vertices_per_primitive; i++)
         gs_vtx_indices[i] = nir_load_var(b, s->gs_vtx_indices_vars[i]);

      nir_def *provoking_vertex = nir_load_provoking_vtx_in_prim_amd(b);
      nir_def *provoking_vtx_idx = nir_select_from_ssa_def_array(
         b, gs_vtx_indices, s->options->num_vertices_per_primitive, provoking_vertex);

      nir_def *prim_id = nir_load_primitive_id(b);
      nir_def *addr = pervertex_lds_addr(b, provoking_vtx_idx, s->pervertex_lds_bytes);

      /* primitive id is always at last of a vertex */
      nir_store_shared(b, prim_id, addr, .base = s->pervertex_lds_bytes - 4);
   }
   nir_pop_if(b, if_gs_thread);
}

/* Store implicit primitive ID when configured as a per-primitive output
 * on GPUs with an attribute ring.
 * This is done separately from the primitive export in order to
 * optimize attribute ring access.
 */
static void
emit_ngg_nogs_prim_id_store_per_prim_to_attr_ring(nir_builder *b, lower_ngg_nogs_state *s)
{
   assert(s->options->hw_info->has_attr_ring);

   nir_def *is_gs_thread = nir_load_var(b, s->gs_exported_var);
   nir_def *highest_gs_thread = nir_ufind_msb(b, nir_ballot(b, 1, s->options->wave_size, is_gs_thread));
   nir_def *max_num_gs_threads = nir_iadd_imm_nuw(b, highest_gs_thread, 1);

   const uint8_t offset = s->options->vs_output_param_offset[VARYING_SLOT_PRIMITIVE_ID];
   ac_nir_prerast_out out = {
      .infos = {{.components_mask = 1, .as_varying_mask = 1}},
      .outputs = {{nir_load_primitive_id(b), NULL, NULL, NULL}}
   };

   ac_nir_store_parameters_to_attr_ring(b, &offset, 1, 0, &out, max_num_gs_threads);
}

static void
emit_store_ngg_nogs_es_primitive_id(nir_builder *b, lower_ngg_nogs_state *s)
{
   nir_def *prim_id = NULL;

   if (b->shader->info.stage == MESA_SHADER_VERTEX) {
      /* LDS address where the primitive ID is stored */
      nir_def *thread_id_in_threadgroup = nir_load_local_invocation_index(b);
      nir_def *addr =
         pervertex_lds_addr(b, thread_id_in_threadgroup, s->pervertex_lds_bytes);

      /* Load primitive ID from LDS */
      prim_id = nir_load_shared(b, 1, 32, addr, .base = s->pervertex_lds_bytes - 4);
   } else if (b->shader->info.stage == MESA_SHADER_TESS_EVAL) {
      /* Just use tess eval primitive ID, which is the same as the patch ID. */
      prim_id = nir_load_primitive_id(b);
   }

   s->out.outputs[VARYING_SLOT_PRIMITIVE_ID][0] = prim_id;
   s->out.infos[VARYING_SLOT_PRIMITIVE_ID].as_varying_mask |= 1;

   /* Update outputs_written to reflect that the pass added a new output. */
   b->shader->info.outputs_written |= VARYING_BIT_PRIMITIVE_ID;
}

static void
add_clipdist_bit(nir_builder *b, nir_def *dist, unsigned index, nir_variable *mask)
{
   nir_def *is_neg = nir_flt_imm(b, dist, 0);
   nir_def *neg_mask = nir_ishl_imm(b, nir_b2i32(b, is_neg), index);
   neg_mask = nir_ior(b, neg_mask, nir_load_var(b, mask));
   nir_store_var(b, mask, neg_mask, 1);
}

static bool
remove_culling_shader_output(nir_builder *b, nir_intrinsic_instr *intrin, void *state)
{
   lower_ngg_nogs_state *s = (lower_ngg_nogs_state *) state;

   /* These are not allowed in VS / TES */
   assert(intrin->intrinsic != nir_intrinsic_store_per_vertex_output &&
          intrin->intrinsic != nir_intrinsic_load_per_vertex_input);

   /* We are only interested in output stores now */
   if (intrin->intrinsic != nir_intrinsic_store_output)
      return false;

   b->cursor = nir_before_instr(&intrin->instr);

   /* no indirect output */
   assert(nir_src_is_const(intrin->src[1]) && nir_src_as_uint(intrin->src[1]) == 0);

   unsigned writemask = nir_intrinsic_write_mask(intrin);
   unsigned component = nir_intrinsic_component(intrin);
   nir_def *store_val = intrin->src[0].ssa;

   /* Position output - store the value to a variable, remove output store */
   nir_io_semantics io_sem = nir_intrinsic_io_semantics(intrin);
   switch (io_sem.location) {
   case VARYING_SLOT_POS:
      ac_nir_store_var_components(b, s->position_value_var, store_val, component, writemask);
      break;
   case VARYING_SLOT_CLIP_DIST0:
   case VARYING_SLOT_CLIP_DIST1: {
      unsigned base = io_sem.location == VARYING_SLOT_CLIP_DIST1 ? 4 : 0;
      base += component;

      /* valid clipdist component mask */
      unsigned mask = (s->options->clip_cull_dist_mask >> base) & writemask;
      u_foreach_bit(i, mask) {
         add_clipdist_bit(b, nir_channel(b, store_val, i), base + i,
                          s->clipdist_neg_mask_var);
         s->has_clipdist = true;
      }
      break;
   }
   case VARYING_SLOT_CLIP_VERTEX:
      ac_nir_store_var_components(b, s->clip_vertex_var, store_val, component, writemask);
      break;
   default:
      break;
   }

   /* Remove all output stores */
   nir_instr_remove(&intrin->instr);
   return true;
}

static void
remove_culling_shader_outputs(nir_shader *culling_shader, lower_ngg_nogs_state *s)
{
   nir_shader_intrinsics_pass(culling_shader, remove_culling_shader_output,
                              nir_metadata_control_flow, s);

   /* Remove dead code resulting from the deleted outputs. */
   bool progress;
   do {
      progress = false;
      NIR_PASS(progress, culling_shader, nir_opt_dead_write_vars);
      NIR_PASS(progress, culling_shader, nir_opt_dce);
      NIR_PASS(progress, culling_shader, nir_opt_dead_cf);
   } while (progress);
}

static void
rewrite_uses_to_var(nir_builder *b, nir_def *old_def, nir_variable *replacement_var, unsigned replacement_var_channel)
{
   if (old_def->parent_instr->type == nir_instr_type_load_const)
      return;

   b->cursor = nir_after_instr(old_def->parent_instr);
   if (b->cursor.instr->type == nir_instr_type_phi)
      b->cursor = nir_after_phis(old_def->parent_instr->block);

   nir_def *pos_val_rep = nir_load_var(b, replacement_var);
   nir_def *replacement = nir_channel(b, pos_val_rep, replacement_var_channel);

   if (old_def->num_components > 1) {
      /* old_def uses a swizzled vector component.
       * There is no way to replace the uses of just a single vector component,
       * so instead create a new vector and replace all uses of the old vector.
       */
      nir_def *old_def_elements[NIR_MAX_VEC_COMPONENTS] = {0};
      for (unsigned j = 0; j < old_def->num_components; ++j)
         old_def_elements[j] = nir_channel(b, old_def, j);
      replacement = nir_vec(b, old_def_elements, old_def->num_components);
   }

   nir_def_rewrite_uses_after(old_def, replacement, replacement->parent_instr);
}

static bool
remove_extra_pos_output(nir_builder *b, nir_intrinsic_instr *intrin, void *state)
{
   lower_ngg_nogs_state *s = (lower_ngg_nogs_state *) state;

   /* These are not allowed in VS / TES */
   assert(intrin->intrinsic != nir_intrinsic_store_per_vertex_output &&
          intrin->intrinsic != nir_intrinsic_load_per_vertex_input);

   /* We are only interested in output stores now */
   if (intrin->intrinsic != nir_intrinsic_store_output)
      return false;

   nir_io_semantics io_sem = nir_intrinsic_io_semantics(intrin);
   if (io_sem.location != VARYING_SLOT_POS)
      return false;

   b->cursor = nir_before_instr(&intrin->instr);

   /* In case other outputs use what we calculated for pos,
    * try to avoid calculating it again by rewriting the usages
    * of the store components here.
    */
   nir_def *store_val = intrin->src[0].ssa;
   unsigned store_pos_component = nir_intrinsic_component(intrin);

   nir_instr_remove(&intrin->instr);

   if (store_val->parent_instr->type == nir_instr_type_alu) {
      nir_alu_instr *alu = nir_instr_as_alu(store_val->parent_instr);
      if (nir_op_is_vec_or_mov(alu->op)) {
         /* Output store uses a vector, we can easily rewrite uses of each vector element. */

         unsigned num_vec_src = 0;
         if (alu->op == nir_op_mov)
            num_vec_src = 1;
         else if (alu->op == nir_op_vec2)
            num_vec_src = 2;
         else if (alu->op == nir_op_vec3)
            num_vec_src = 3;
         else if (alu->op == nir_op_vec4)
            num_vec_src = 4;
         assert(num_vec_src);

         /* Remember the current components whose uses we wish to replace.
          * This is needed because rewriting one source can affect the others too.
          */
         nir_def *vec_comps[NIR_MAX_VEC_COMPONENTS] = {0};
         for (unsigned i = 0; i < num_vec_src; i++)
            vec_comps[i] = alu->src[i].src.ssa;

         for (unsigned i = 0; i < num_vec_src; i++)
            rewrite_uses_to_var(b, vec_comps[i], s->position_value_var, store_pos_component + i);
      } else {
         rewrite_uses_to_var(b, store_val, s->position_value_var, store_pos_component);
      }
   } else {
      rewrite_uses_to_var(b, store_val, s->position_value_var, store_pos_component);
   }

   return true;
}

static void
remove_extra_pos_outputs(nir_shader *shader, lower_ngg_nogs_state *s)
{
   nir_shader_intrinsics_pass(shader, remove_extra_pos_output,
                              nir_metadata_control_flow, s);
}

static bool
remove_compacted_arg(lower_ngg_nogs_state *s, nir_builder *b, unsigned idx)
{
   nir_instr *store_instr = s->compact_arg_stores[idx];
   if (!store_instr)
      return false;

   /* Simply remove the store. */
   nir_instr_remove(store_instr);

   /* Find the intrinsic that overwrites the shader arguments,
    * and change its corresponding source.
    * This will cause NIR's DCE to recognize the load and its phis as dead.
    */
   b->cursor = nir_before_instr(&s->overwrite_args->instr);
   nir_def *undef_arg = nir_undef(b, 1, 32);
   nir_def_rewrite_uses(s->overwrite_args->src[idx].ssa, undef_arg);

   s->compact_arg_stores[idx] = NULL;
   return true;
}

static bool
cleanup_culling_shader_after_dce(nir_shader *shader,
                                 nir_function_impl *function_impl,
                                 lower_ngg_nogs_state *s)
{
   bool uses_vs_vertex_id = false;
   bool uses_vs_instance_id = false;
   bool uses_tes_u = false;
   bool uses_tes_v = false;
   bool uses_tes_rel_patch_id = false;
   bool uses_tes_patch_id = false;

   bool progress = false;
   nir_builder b = nir_builder_create(function_impl);

   nir_foreach_block_reverse_safe(block, function_impl) {
      nir_foreach_instr_reverse_safe(instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

         switch (intrin->intrinsic) {
         case nir_intrinsic_sendmsg_amd:
            goto cleanup_culling_shader_after_dce_done;
         case nir_intrinsic_load_vertex_id:
         case nir_intrinsic_load_vertex_id_zero_base:
            uses_vs_vertex_id = true;
            break;
         case nir_intrinsic_load_instance_id:
            uses_vs_instance_id = true;
            break;
         case nir_intrinsic_load_input: {
            const nir_io_semantics io_sem = nir_intrinsic_io_semantics(intrin);
            if (s->options->instance_rate_inputs & BITFIELD_BIT(io_sem.location))
               uses_vs_instance_id = true;
            else
               uses_vs_vertex_id = true;
            break;
         }
         case nir_intrinsic_load_tess_coord:
            uses_tes_u = uses_tes_v = true;
            break;
         case nir_intrinsic_load_tess_rel_patch_id_amd:
            uses_tes_rel_patch_id = true;
            break;
         case nir_intrinsic_load_primitive_id:
            if (shader->info.stage == MESA_SHADER_TESS_EVAL)
               uses_tes_patch_id = true;
            break;
         default:
            break;
         }
      }
   }

   cleanup_culling_shader_after_dce_done:

   if (shader->info.stage == MESA_SHADER_VERTEX) {
      if (!uses_vs_vertex_id)
         progress |= remove_compacted_arg(s, &b, 0);
      if (!uses_vs_instance_id)
         progress |= remove_compacted_arg(s, &b, 1);
   } else if (shader->info.stage == MESA_SHADER_TESS_EVAL) {
      if (!uses_tes_u)
         progress |= remove_compacted_arg(s, &b, 0);
      if (!uses_tes_v)
         progress |= remove_compacted_arg(s, &b, 1);
      if (!uses_tes_rel_patch_id)
         progress |= remove_compacted_arg(s, &b, 3);
      if (!uses_tes_patch_id)
         progress |= remove_compacted_arg(s, &b, 2);
   }

   return progress;
}

/**
 * Perform vertex compaction after culling.
 *
 * 1. Repack surviving ES invocations (this determines which lane will export which vertex)
 * 2. Surviving ES vertex invocations store their data to LDS
 * 3. Emit GS_ALLOC_REQ
 * 4. Repacked invocations load the vertex data from LDS
 * 5. GS threads update their vertex indices
 * 6. Optionally, do the same for primitives.
 */
static void
compact_vertices_after_culling(nir_builder *b,
                               lower_ngg_nogs_state *s,
                               nir_variable **repacked_variables,
                               nir_variable **gs_vtxaddr_vars,
                               nir_def *invocation_index,
                               nir_def *es_vertex_lds_addr,
                               nir_def *es_exporter_tid,
                               nir_def *num_live_vertices_in_workgroup,
                               nir_def *gs_exporter_tid,
                               nir_def *num_live_primitives_in_workgroup,
                               unsigned pervertex_lds_bytes,
                               unsigned num_repacked_variables)
{
   nir_if *if_es_accepted = nir_push_if(b, nir_load_var(b, s->es_accepted_var));
   {
      nir_def *exporter_addr = pervertex_lds_addr(b, es_exporter_tid, pervertex_lds_bytes);

      /* Store the exporter thread's index to the LDS space of the current thread so GS threads can load it */
      nir_store_shared(b, nir_u2u8(b, es_exporter_tid), es_vertex_lds_addr, .base = lds_es_exporter_tid);

      /* Store the current thread's position output to the exporter thread's LDS space */
      nir_def *pos = nir_load_var(b, s->position_value_var);
      nir_store_shared(b, pos, exporter_addr, .base = lds_es_pos_x);

      /* Store the current thread's repackable arguments to the exporter thread's LDS space */
      for (unsigned i = 0; i < num_repacked_variables; ++i) {
         nir_def *arg_val = nir_load_var(b, repacked_variables[i]);
         nir_intrinsic_instr *store = nir_store_shared(b, arg_val, exporter_addr, .base = lds_es_arg_0 + 4u * i);

         s->compact_arg_stores[i] = &store->instr;
      }

      /* TES rel patch id does not cost extra dword */
      if (b->shader->info.stage == MESA_SHADER_TESS_EVAL) {
         nir_def *arg_val = nir_load_var(b, s->repacked_rel_patch_id);
         nir_intrinsic_instr *store =
            nir_store_shared(b, nir_u2u8(b, arg_val), exporter_addr,
                             .base = lds_es_tes_rel_patch_id);

         s->compact_arg_stores[3] = &store->instr;
      }
   }
   nir_pop_if(b, if_es_accepted);

   /* TODO: Consider adding a shortcut exit.
    * Waves that have no vertices and primitives left can s_endpgm right here.
    */

   nir_barrier(b, .execution_scope=SCOPE_WORKGROUP, .memory_scope=SCOPE_WORKGROUP,
                         .memory_semantics=NIR_MEMORY_ACQ_REL, .memory_modes=nir_var_mem_shared);

   nir_def *es_survived = nir_ilt(b, invocation_index, num_live_vertices_in_workgroup);
   nir_if *if_packed_es_thread = nir_push_if(b, es_survived);
   {
      /* Read position from the current ES thread's LDS space (written by the exported vertex's ES thread) */
      nir_def *exported_pos = nir_load_shared(b, 4, 32, es_vertex_lds_addr, .base = lds_es_pos_x);
      nir_store_var(b, s->position_value_var, exported_pos, 0xfu);

      /* Read the repacked arguments */
      for (unsigned i = 0; i < num_repacked_variables; ++i) {
         nir_def *arg_val = nir_load_shared(b, 1, 32, es_vertex_lds_addr, .base = lds_es_arg_0 + 4u * i);
         nir_store_var(b, repacked_variables[i], arg_val, 0x1u);
      }

      if (b->shader->info.stage == MESA_SHADER_TESS_EVAL) {
         nir_def *arg_val = nir_load_shared(b, 1, 8, es_vertex_lds_addr,
                                                .base = lds_es_tes_rel_patch_id);
         nir_store_var(b, s->repacked_rel_patch_id, nir_u2u32(b, arg_val), 0x1u);
      }
   }
   nir_push_else(b, if_packed_es_thread);
   {
      nir_store_var(b, s->position_value_var, nir_undef(b, 4, 32), 0xfu);
      for (unsigned i = 0; i < num_repacked_variables; ++i)
         nir_store_var(b, repacked_variables[i], nir_undef(b, 1, 32), 0x1u);
   }
   nir_pop_if(b, if_packed_es_thread);

   nir_def *gs_accepted = nir_load_var(b, s->gs_accepted_var);
   nir_if *if_gs_accepted = nir_push_if(b, gs_accepted);
   {
      nir_def *exporter_vtx_indices[3] = {0};

      /* Load the index of the ES threads that will export the current GS thread's vertices */
      for (unsigned v = 0; v < s->options->num_vertices_per_primitive; ++v) {
         nir_def *vtx_addr = nir_load_var(b, gs_vtxaddr_vars[v]);
         nir_def *exporter_vtx_idx = nir_load_shared(b, 1, 8, vtx_addr, .base = lds_es_exporter_tid);
         exporter_vtx_indices[v] = nir_u2u32(b, exporter_vtx_idx);
         nir_store_var(b, s->gs_vtx_indices_vars[v], exporter_vtx_indices[v], 0x1);
      }

      nir_def *prim_exp_arg =
         ac_nir_pack_ngg_prim_exp_arg(b, s->options->num_vertices_per_primitive,
                                    exporter_vtx_indices, NULL, s->options->hw_info->gfx_level);
      nir_store_var(b, s->prim_exp_arg_var, prim_exp_arg, 0x1u);
   }
   nir_pop_if(b, if_gs_accepted);

   nir_store_var(b, s->es_accepted_var, es_survived, 0x1u);

   if (s->options->compact_primitives) {
      /* For primitive compaction, re-use the same LDS space that we used for
       * vertex compaction, so we need to wait until vertex threads are finished reading it.
       * Considering we only need 1 DWORD per primitive, let's assume we always have enough space,
       * since vertex compaction requires at least 5 DWORDs per vertex.
       */
      nir_barrier(b, .execution_scope=SCOPE_WORKGROUP, .memory_scope=SCOPE_WORKGROUP,
                     .memory_semantics=NIR_MEMORY_ACQ_REL, .memory_modes=nir_var_mem_shared);

      if_gs_accepted = nir_push_if(b, gs_accepted);
      {
         nir_def *exporter_addr = pervertex_lds_addr(b, gs_exporter_tid, pervertex_lds_bytes);
         nir_def *prim_exp_arg = nir_load_var(b, s->prim_exp_arg_var);

         /* Store the primitive export argument into the address of the exporter thread. */
         nir_store_shared(b, prim_exp_arg, exporter_addr, .base = lds_es_pos_x);
      }
      nir_pop_if(b, if_gs_accepted);

      nir_barrier(b, .execution_scope=SCOPE_WORKGROUP, .memory_scope=SCOPE_WORKGROUP,
                     .memory_semantics=NIR_MEMORY_ACQ_REL, .memory_modes=nir_var_mem_shared);

      nir_def *gs_survived = nir_ilt(b, invocation_index, num_live_primitives_in_workgroup);
      nir_if *if_packed_gs_thread = nir_push_if(b, gs_survived);
      {
         /* Load the primitive export argument that the current thread will export. */
         nir_def *prim_exp_arg = nir_load_shared(b, 1, 32, es_vertex_lds_addr, .base = lds_es_pos_x);

         nir_store_var(b, s->prim_exp_arg_var, prim_exp_arg, 0x1u);
      }
      nir_push_else(b, if_packed_gs_thread);
      {
         nir_store_var(b, s->prim_exp_arg_var, nir_undef(b, 1, 32), 0x1u);
      }
      nir_pop_if(b, if_packed_gs_thread);

      nir_store_var(b, s->gs_accepted_var, gs_survived, 0x1u);
      nir_store_var(b, s->gs_exported_var, gs_survived, 0x1u);
   }
}

static void
analyze_shader_before_culling_walk(nir_def *ssa,
                                   uint8_t flag,
                                   lower_ngg_nogs_state *s)
{
   nir_instr *instr = ssa->parent_instr;
   uint8_t old_pass_flags = instr->pass_flags;
   instr->pass_flags |= flag;

   if (instr->pass_flags == old_pass_flags)
      return; /* Already visited. */

   switch (instr->type) {
   case nir_instr_type_intrinsic: {
      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

      /* VS input loads and SSBO loads are actually VRAM reads on AMD HW. */
      switch (intrin->intrinsic) {
      case nir_intrinsic_load_input: {
         nir_io_semantics in_io_sem = nir_intrinsic_io_semantics(intrin);
         uint64_t in_mask = UINT64_C(1) << (uint64_t) in_io_sem.location;
         if (instr->pass_flags & nggc_passflag_used_by_pos)
            s->inputs_needed_by_pos |= in_mask;
         else if (instr->pass_flags & nggc_passflag_used_by_other)
            s->inputs_needed_by_others |= in_mask;
         break;
      }
      default:
         break;
      }

      break;
   }
   case nir_instr_type_alu: {
      nir_alu_instr *alu = nir_instr_as_alu(instr);
      unsigned num_srcs = nir_op_infos[alu->op].num_inputs;

      for (unsigned i = 0; i < num_srcs; ++i) {
         analyze_shader_before_culling_walk(alu->src[i].src.ssa, flag, s);
      }

      break;
   }
   case nir_instr_type_tex: {
      nir_tex_instr *tex = nir_instr_as_tex(instr);
      unsigned num_srcs = tex->num_srcs;

      for (unsigned i = 0; i < num_srcs; ++i) {
         analyze_shader_before_culling_walk(tex->src[i].src.ssa, flag, s);
      }

      break;
   }
   case nir_instr_type_phi: {
      nir_phi_instr *phi = nir_instr_as_phi(instr);
      nir_foreach_phi_src_safe(phi_src, phi) {
         analyze_shader_before_culling_walk(phi_src->src.ssa, flag, s);
      }

      break;
   }
   default:
      break;
   }
}

static void
analyze_shader_before_culling(nir_shader *shader, lower_ngg_nogs_state *s)
{
   nir_foreach_function_impl(impl, shader) {
      /* We need divergence info for culling shaders. */
      nir_metadata_require(impl, nir_metadata_divergence);

      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            instr->pass_flags = 0;

            if (instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
            if (intrin->intrinsic != nir_intrinsic_store_output)
               continue;

            nir_io_semantics io_sem = nir_intrinsic_io_semantics(intrin);
            nir_def *store_val = intrin->src[0].ssa;
            uint8_t flag = io_sem.location == VARYING_SLOT_POS ? nggc_passflag_used_by_pos : nggc_passflag_used_by_other;
            analyze_shader_before_culling_walk(store_val, flag, s);
         }
      }

      nir_metadata_preserve(impl, nir_metadata_all);
   }
}

static nir_def *
find_reusable_ssa_def(nir_instr *instr)
{
   /* Find instructions whose SSA definitions are used by both
    * the top and bottom parts of the shader (before and after culling).
    * Only in this case, it makes sense for the bottom part
    * to try to reuse these from the top part.
    */
   if ((instr->pass_flags & nggc_passflag_used_by_both) != nggc_passflag_used_by_both)
      return NULL;

   switch (instr->type) {
   case nir_instr_type_alu: {
      nir_alu_instr *alu = nir_instr_as_alu(instr);
      if (alu->def.divergent)
         return NULL;
      /* Ignore uniform floats because they regress VGPR usage too much */
      if (nir_op_infos[alu->op].output_type & nir_type_float)
         return NULL;
      return &alu->def;
   }
   case nir_instr_type_intrinsic: {
      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
      if (!nir_intrinsic_can_reorder(intrin) ||
            !nir_intrinsic_infos[intrin->intrinsic].has_dest ||
            intrin->def.divergent)
         return NULL;
      return &intrin->def;
   }
   case nir_instr_type_phi: {
      nir_phi_instr *phi = nir_instr_as_phi(instr);
      if (phi->def.divergent)
         return NULL;
      return &phi->def;
   }
   default:
      return NULL;
   }
}

static const struct glsl_type *
glsl_uint_type_for_ssa(nir_def *ssa)
{
   enum glsl_base_type base_type = GLSL_TYPE_UINT;
   switch (ssa->bit_size) {
   case 8: base_type = GLSL_TYPE_UINT8; break;
   case 16: base_type = GLSL_TYPE_UINT16; break;
   case 32: base_type = GLSL_TYPE_UINT; break;
   case 64: base_type = GLSL_TYPE_UINT64; break;
   default: return NULL;
   }

   return ssa->num_components == 1
          ? glsl_scalar_type(base_type)
          : glsl_vector_type(base_type, ssa->num_components);
}

/**
 * Save the reusable SSA definitions to variables so that the
 * bottom shader part can reuse them from the top part.
 *
 * 1. We create a new function temporary variable for reusables,
 *    and insert a store+load.
 * 2. The shader is cloned (the top part is created), then the
 *    control flow is reinserted (for the bottom part.)
 * 3. For reusables, we delete the variable stores from the
 *    bottom part. This will make them use the variables from
 *    the top part and DCE the redundant instructions.
 */
static void
save_reusable_variables(nir_builder *b, lower_ngg_nogs_state *s)
{
   ASSERTED int vec_ok = u_vector_init(&s->reusable_nondeferred_variables, 4, sizeof(reusable_nondeferred_variable));
   assert(vec_ok);

   /* Upper limit on reusable uniforms in order to reduce SGPR spilling. */
   unsigned remaining_reusable_uniforms = 48;

   nir_block *block = nir_start_block(b->impl);
   while (block) {
      /* Process the instructions in the current block. */
      nir_foreach_instr_safe(instr, block) {
         /* Determine if we can reuse the current SSA value.
          * When vertex compaction is used, it is possible that the same shader invocation
          * processes a different vertex in the top and bottom part of the shader.
          * Therefore, we only reuse uniform values.
          */
         nir_def *ssa = find_reusable_ssa_def(instr);
         if (!ssa)
            continue;

         /* Determine a suitable type for the SSA value. */
         const struct glsl_type *t = glsl_uint_type_for_ssa(ssa);
         if (!t)
            continue;

         if (!ssa->divergent) {
            if (remaining_reusable_uniforms < ssa->num_components)
               continue;

            remaining_reusable_uniforms -= ssa->num_components;
         }

         reusable_nondeferred_variable *saved = (reusable_nondeferred_variable *) u_vector_add(&s->reusable_nondeferred_variables);
         assert(saved);

         /* Create a new NIR variable where we store the reusable value.
          * Then, we reload the variable and replace the uses of the value
          * with the reloaded variable.
          */
         saved->var = nir_local_variable_create(b->impl, t, NULL);
         saved->ssa = ssa;

         b->cursor = instr->type == nir_instr_type_phi
                     ? nir_after_instr_and_phis(instr)
                     : nir_after_instr(instr);
         nir_store_var(b, saved->var, saved->ssa, BITFIELD_MASK(ssa->num_components));
         nir_def *reloaded = nir_load_var(b, saved->var);
         nir_def_rewrite_uses_after(ssa, reloaded, reloaded->parent_instr);
      }

      /* Look at the next CF node. */
      nir_cf_node *next_cf_node = nir_cf_node_next(&block->cf_node);
      if (next_cf_node) {
         /* It makes no sense to try to reuse things from within loops. */
         bool next_is_loop = next_cf_node->type == nir_cf_node_loop;

         /* Don't reuse if we're in divergent control flow.
          *
          * Thanks to vertex repacking, the same shader invocation may process a different vertex
          * in the top and bottom part, and it's even possible that this different vertex was initially
          * processed in a different wave. So the two parts may take a different divergent code path.
          * Therefore, these variables in divergent control flow may stay undefined.
          *
          * Note that this problem doesn't exist if vertices are not repacked or if the
          * workgroup only has a single wave.
          */
         bool next_is_divergent_if =
            next_cf_node->type == nir_cf_node_if &&
            nir_src_is_divergent(&nir_cf_node_as_if(next_cf_node)->condition);

         if (next_is_loop || next_is_divergent_if) {
            block = nir_cf_node_cf_tree_next(next_cf_node);
            continue;
         }
      }

      /* Go to the next block. */
      block = nir_block_cf_tree_next(block);
   }
}

/**
 * Reuses suitable variables from the top part of the shader,
 * by deleting their stores from the bottom part.
 */
static void
apply_reusable_variables(nir_builder *b, lower_ngg_nogs_state *s)
{
   if (!u_vector_length(&s->reusable_nondeferred_variables)) {
      u_vector_finish(&s->reusable_nondeferred_variables);
      return;
   }

   nir_foreach_block_reverse_safe(block, b->impl) {
      nir_foreach_instr_reverse_safe(instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;
         nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

         /* When we found any of these intrinsics, it means
          * we reached the top part and we must stop.
          */
         if (intrin->intrinsic == nir_intrinsic_sendmsg_amd)
            goto done;

         if (intrin->intrinsic != nir_intrinsic_store_deref)
            continue;
         nir_deref_instr *deref = nir_src_as_deref(intrin->src[0]);
         if (deref->deref_type != nir_deref_type_var)
            continue;

         reusable_nondeferred_variable *saved;
         u_vector_foreach(saved, &s->reusable_nondeferred_variables) {
            if (saved->var == deref->var) {
               nir_instr_remove(instr);
            }
         }
      }
   }

   done:
   u_vector_finish(&s->reusable_nondeferred_variables);
}

static void
cull_primitive_accepted(nir_builder *b, void *state)
{
   lower_ngg_nogs_state *s = (lower_ngg_nogs_state *)state;

   nir_store_var(b, s->gs_accepted_var, nir_imm_true(b), 0x1u);

   /* Store the accepted state to LDS for ES threads */
   for (unsigned vtx = 0; vtx < s->options->num_vertices_per_primitive; ++vtx)
      nir_store_shared(b, nir_imm_intN_t(b, 1, 8), s->vtx_addr[vtx], .base = lds_es_vertex_accepted);
}

static void
clipdist_culling_es_part(nir_builder *b, lower_ngg_nogs_state *s,
                         nir_def *es_vertex_lds_addr)
{
   /* no gl_ClipDistance used but we have user defined clip plane */
   if (s->options->user_clip_plane_enable_mask && !s->has_clipdist) {
      /* use gl_ClipVertex if defined */
      nir_variable *clip_vertex_var =
         b->shader->info.outputs_written & BITFIELD64_BIT(VARYING_SLOT_CLIP_VERTEX) ?
         s->clip_vertex_var : s->position_value_var;
      nir_def *clip_vertex = nir_load_var(b, clip_vertex_var);

      /* clip against user defined clip planes */
      for (unsigned i = 0; i < 8; i++) {
         if (!(s->options->user_clip_plane_enable_mask & BITFIELD_BIT(i)))
            continue;

         nir_def *plane = nir_load_user_clip_plane(b, .ucp_id = i);
         nir_def *dist = nir_fdot(b, clip_vertex, plane);
         add_clipdist_bit(b, dist, i, s->clipdist_neg_mask_var);
      }

      s->has_clipdist = true;
   }

   /* store clipdist_neg_mask to LDS for culling latter in gs thread */
   if (s->has_clipdist) {
      nir_def *mask = nir_load_var(b, s->clipdist_neg_mask_var);
      nir_store_shared(b, nir_u2u8(b, mask), es_vertex_lds_addr,
                       .base = lds_es_clipdist_neg_mask);
   }
}

static unsigned
ngg_nogs_get_culling_pervertex_lds_size(gl_shader_stage stage,
                                        bool uses_instance_id,
                                        bool uses_primitive_id,
                                        unsigned *num_repacked_variables)
{
   /* Culling shaders must repack some variables because
    * the same shader invocation may process different vertices
    * before and after the culling algorithm.
    */

   unsigned num_repacked;
   if (stage == MESA_SHADER_VERTEX) {
      /* Vertex shaders repack:
       * - Vertex ID
       * - Instance ID (only if used)
       */
      num_repacked = uses_instance_id ? 2 : 1;
   } else {
      /* Tess eval shaders repack:
       * - U, V coordinates
       * - primitive ID (aka. patch id, only if used)
       * - relative patch id (not included here because doesn't need a dword)
       */
      assert(stage == MESA_SHADER_TESS_EVAL);
      num_repacked = uses_primitive_id ? 3 : 2;
   }

   if (num_repacked_variables)
      *num_repacked_variables = num_repacked;

   /* one odd dword to reduce LDS bank conflict */
   return (lds_es_arg_0 + num_repacked * 4u) | 4u;
}

static void
add_deferred_attribute_culling(nir_builder *b, nir_cf_list *original_extracted_cf, lower_ngg_nogs_state *s)
{
   bool uses_instance_id = BITSET_TEST(b->shader->info.system_values_read, SYSTEM_VALUE_INSTANCE_ID);
   bool uses_tess_primitive_id = BITSET_TEST(b->shader->info.system_values_read, SYSTEM_VALUE_PRIMITIVE_ID);

   unsigned num_repacked_variables;
   unsigned pervertex_lds_bytes =
      ngg_nogs_get_culling_pervertex_lds_size(b->shader->info.stage,
                                              uses_instance_id,
                                              uses_tess_primitive_id,
                                              &num_repacked_variables);

   nir_function_impl *impl = nir_shader_get_entrypoint(b->shader);

   /* Create some helper variables. */
   nir_variable *gs_vtxaddr_vars[3] = {
      nir_local_variable_create(impl, glsl_uint_type(), "gs_vtx0_addr"),
      nir_local_variable_create(impl, glsl_uint_type(), "gs_vtx1_addr"),
      nir_local_variable_create(impl, glsl_uint_type(), "gs_vtx2_addr"),
   };

   nir_variable *repacked_variables[3] = {
      nir_local_variable_create(impl, glsl_uint_type(), "repacked_var_0"),
      nir_local_variable_create(impl, glsl_uint_type(), "repacked_var_1"),
      nir_local_variable_create(impl, glsl_uint_type(), "repacked_var_2"),
   };

   /* Relative patch ID is a special case because it doesn't need an extra dword, repack separately. */
   s->repacked_rel_patch_id = nir_local_variable_create(impl, glsl_uint_type(), "repacked_rel_patch_id");

   if (s->options->clip_cull_dist_mask ||
       s->options->user_clip_plane_enable_mask) {
      s->clip_vertex_var =
         nir_local_variable_create(impl, glsl_vec4_type(), "clip_vertex");
      s->clipdist_neg_mask_var =
         nir_local_variable_create(impl, glsl_uint_type(), "clipdist_neg_mask");

      /* init mask to 0 */
      nir_store_var(b, s->clipdist_neg_mask_var, nir_imm_int(b, 0), 1);
   }

   /* Top part of the culling shader (aka. position shader part)
    *
    * We clone the full ES shader and emit it here, but we only really care
    * about its position output, so we delete every other output from this part.
    * The position output is stored into a temporary variable, and reloaded later.
    */

   nir_def *es_thread = has_input_vertex(b);
   nir_if *if_es_thread = nir_push_if(b, es_thread);
   {
      /* Initialize the position output variable to zeroes, in case not all VS/TES invocations store the output.
       * The spec doesn't require it, but we use (0, 0, 0, 1) because some games rely on that.
       */
      nir_store_var(b, s->position_value_var, nir_imm_vec4(b, 0.0f, 0.0f, 0.0f, 1.0f), 0xfu);

      /* Now reinsert a clone of the shader code */
      struct hash_table *remap_table = _mesa_pointer_hash_table_create(NULL);
      nir_cf_list_clone_and_reinsert(original_extracted_cf, &if_es_thread->cf_node, b->cursor, remap_table);
      _mesa_hash_table_destroy(remap_table, NULL);
      b->cursor = nir_after_cf_list(&if_es_thread->then_list);

      /* Remember the current thread's shader arguments */
      if (b->shader->info.stage == MESA_SHADER_VERTEX) {
         nir_store_var(b, repacked_variables[0], nir_load_vertex_id_zero_base(b), 0x1u);
         if (uses_instance_id)
            nir_store_var(b, repacked_variables[1], nir_load_instance_id(b), 0x1u);
      } else if (b->shader->info.stage == MESA_SHADER_TESS_EVAL) {
         nir_store_var(b, s->repacked_rel_patch_id, nir_load_tess_rel_patch_id_amd(b), 0x1u);
         nir_def *tess_coord = nir_load_tess_coord(b);
         nir_store_var(b, repacked_variables[0], nir_channel(b, tess_coord, 0), 0x1u);
         nir_store_var(b, repacked_variables[1], nir_channel(b, tess_coord, 1), 0x1u);
         if (uses_tess_primitive_id)
            nir_store_var(b, repacked_variables[2], nir_load_primitive_id(b), 0x1u);
      } else {
         unreachable("Should be VS or TES.");
      }
   }
   nir_pop_if(b, if_es_thread);

   nir_store_var(b, s->es_accepted_var, es_thread, 0x1u);
   nir_def *gs_thread = has_input_primitive(b);
   nir_store_var(b, s->gs_accepted_var, gs_thread, 0x1u);

   /* Remove all non-position outputs, and put the position output into the variable. */
   nir_metadata_preserve(impl, nir_metadata_none);
   remove_culling_shader_outputs(b->shader, s);
   b->cursor = nir_after_impl(impl);

   nir_def *lds_scratch_base = nir_load_lds_ngg_scratch_base_amd(b);

   /* Run culling algorithms if culling is enabled.
    *
    * NGG culling can be enabled or disabled in runtime.
    * This is determined by a SGPR shader argument which is accessed
    * by the following NIR intrinsic.
    */

   nir_if *if_cull_en = nir_push_if(b, nir_load_cull_any_enabled_amd(b));
   {
      nir_def *invocation_index = nir_load_local_invocation_index(b);
      nir_def *es_vertex_lds_addr = pervertex_lds_addr(b, invocation_index, pervertex_lds_bytes);

      /* ES invocations store their vertex data to LDS for GS threads to read. */
      if_es_thread = nir_push_if(b, es_thread);
      if_es_thread->control = nir_selection_control_divergent_always_taken;
      {
         /* Store position components that are relevant to culling in LDS */
         nir_def *pre_cull_pos = nir_load_var(b, s->position_value_var);
         nir_def *pre_cull_w = nir_channel(b, pre_cull_pos, 3);
         nir_store_shared(b, pre_cull_w, es_vertex_lds_addr, .base = lds_es_pos_w);
         nir_def *pre_cull_x_div_w = nir_fdiv(b, nir_channel(b, pre_cull_pos, 0), pre_cull_w);
         nir_def *pre_cull_y_div_w = nir_fdiv(b, nir_channel(b, pre_cull_pos, 1), pre_cull_w);
         nir_store_shared(b, nir_vec2(b, pre_cull_x_div_w, pre_cull_y_div_w), es_vertex_lds_addr, .base = lds_es_pos_x);

         /* Clear out the ES accepted flag in LDS */
         nir_store_shared(b, nir_imm_zero(b, 1, 8), es_vertex_lds_addr, .align_mul = 4, .base = lds_es_vertex_accepted);

         /* For clipdist culling */
         clipdist_culling_es_part(b, s, es_vertex_lds_addr);
      }
      nir_pop_if(b, if_es_thread);

      nir_barrier(b, .execution_scope=SCOPE_WORKGROUP, .memory_scope=SCOPE_WORKGROUP,
                            .memory_semantics=NIR_MEMORY_ACQ_REL, .memory_modes=nir_var_mem_shared);

      nir_store_var(b, s->gs_accepted_var, nir_imm_false(b), 0x1u);
      nir_store_var(b, s->prim_exp_arg_var, nir_imm_int(b, 1u << 31), 0x1u);

      /* GS invocations load the vertex data and perform the culling. */
      nir_if *if_gs_thread = nir_push_if(b, gs_thread);
      {
         /* Load vertex indices from input VGPRs */
         nir_def *vtx_idx[3] = {0};
         for (unsigned vertex = 0; vertex < s->options->num_vertices_per_primitive;
              ++vertex)
            vtx_idx[vertex] = nir_load_var(b, s->gs_vtx_indices_vars[vertex]);

         nir_def *pos[3][4] = {0};

         /* Load W positions of vertices first because the culling code will use these first */
         for (unsigned vtx = 0; vtx < s->options->num_vertices_per_primitive; ++vtx) {
            s->vtx_addr[vtx] = pervertex_lds_addr(b, vtx_idx[vtx], pervertex_lds_bytes);
            pos[vtx][3] = nir_load_shared(b, 1, 32, s->vtx_addr[vtx], .base = lds_es_pos_w);
            nir_store_var(b, gs_vtxaddr_vars[vtx], s->vtx_addr[vtx], 0x1u);
         }

         /* Load the X/W, Y/W positions of vertices */
         for (unsigned vtx = 0; vtx < s->options->num_vertices_per_primitive; ++vtx) {
            nir_def *xy = nir_load_shared(b, 2, 32, s->vtx_addr[vtx], .base = lds_es_pos_x);
            pos[vtx][0] = nir_channel(b, xy, 0);
            pos[vtx][1] = nir_channel(b, xy, 1);
         }

         nir_def *accepted_by_clipdist;
         if (s->has_clipdist) {
            nir_def *clipdist_neg_mask = nir_imm_intN_t(b, 0xff, 8);
            for (unsigned vtx = 0; vtx < s->options->num_vertices_per_primitive; ++vtx) {
               nir_def *mask =
                  nir_load_shared(b, 1, 8, s->vtx_addr[vtx],
                                  .base = lds_es_clipdist_neg_mask);
               clipdist_neg_mask = nir_iand(b, clipdist_neg_mask, mask);
            }
            /* primitive is culled if any plane's clipdist of all vertices are negative */
            accepted_by_clipdist = nir_ieq_imm(b, clipdist_neg_mask, 0);
         } else {
            accepted_by_clipdist = nir_imm_true(b);
         }

         /* See if the current primitive is accepted */
         ac_nir_cull_primitive(b, accepted_by_clipdist, pos,
                               s->options->num_vertices_per_primitive,
                               cull_primitive_accepted, s);
      }
      nir_pop_if(b, if_gs_thread);

      nir_barrier(b, .execution_scope=SCOPE_WORKGROUP, .memory_scope=SCOPE_WORKGROUP,
                            .memory_semantics=NIR_MEMORY_ACQ_REL, .memory_modes=nir_var_mem_shared);

      nir_store_var(b, s->es_accepted_var, nir_imm_false(b), 0x1u);

      /* ES invocations load their accepted flag from LDS. */
      if_es_thread = nir_push_if(b, es_thread);
      if_es_thread->control = nir_selection_control_divergent_always_taken;
      {
         nir_def *accepted = nir_load_shared(b, 1, 8u, es_vertex_lds_addr, .base = lds_es_vertex_accepted, .align_mul = 4u);
         nir_def *accepted_bool = nir_ine_imm(b, nir_u2u32(b, accepted), 0);
         nir_store_var(b, s->es_accepted_var, accepted_bool, 0x1u);
      }
      nir_pop_if(b, if_es_thread);

      nir_def *es_accepted = nir_load_var(b, s->es_accepted_var);
      nir_def *gs_accepted = nir_load_var(b, s->gs_accepted_var);

      /* Repack the vertices (always) and primitives (optional) that survived the culling. */
      nir_def *accepted[] = { es_accepted, gs_accepted };
      ac_nir_wg_repack_result rep[2] = {0};
      const unsigned num_rep = s->options->compact_primitives ? 2 : 1;
      ac_nir_repack_invocations_in_workgroup(b, accepted, rep, num_rep, lds_scratch_base,
                                      s->max_num_waves, s->options->wave_size);
      nir_def *num_live_vertices_in_workgroup = rep[0].num_repacked_invocations;
      nir_def *es_exporter_tid = rep[0].repacked_invocation_index;
      nir_def *num_exported_prims = NULL;
      nir_def *gs_exporter_tid = NULL;

      if (s->options->compact_primitives) {
         num_exported_prims = rep[1].num_repacked_invocations;
         gs_exporter_tid = rep[1].repacked_invocation_index;
      } else {
         /* If all vertices are culled, set primitive count to 0 as well. */
         nir_def *fully_culled = nir_ieq_imm(b, num_live_vertices_in_workgroup, 0u);
         num_exported_prims = nir_bcsel(b, fully_culled, nir_imm_int(b, 0u), nir_load_workgroup_num_input_primitives_amd(b));
         nir_store_var(b, s->gs_exported_var, nir_iand(b, nir_inot(b, fully_culled), has_input_primitive(b)), 0x1u);
      }

      nir_if *if_wave_0 = nir_push_if(b, nir_ieq_imm(b, nir_load_subgroup_id(b), 0));
      {
         ac_nir_ngg_alloc_vertices_and_primitives(b, num_live_vertices_in_workgroup, num_exported_prims, s->options->hw_info->has_ngg_fully_culled_bug);
      }
      nir_pop_if(b, if_wave_0);

      /* Vertex compaction. */
      compact_vertices_after_culling(b, s,
                                     repacked_variables, gs_vtxaddr_vars,
                                     invocation_index, es_vertex_lds_addr,
                                     es_exporter_tid, num_live_vertices_in_workgroup,
                                     gs_exporter_tid, num_exported_prims,
                                     pervertex_lds_bytes, num_repacked_variables);
   }
   nir_push_else(b, if_cull_en);
   {
      /* When culling is disabled, we do the same as we would without culling. */
      nir_if *if_wave_0 = nir_push_if(b, nir_ieq_imm(b, nir_load_subgroup_id(b), 0));
      {
         nir_def *vtx_cnt = nir_load_workgroup_num_input_vertices_amd(b);
         nir_def *prim_cnt = nir_load_workgroup_num_input_primitives_amd(b);
         ac_nir_ngg_alloc_vertices_and_primitives(b, vtx_cnt, prim_cnt, false);
      }
      nir_pop_if(b, if_wave_0);
      nir_store_var(b, s->prim_exp_arg_var, emit_ngg_nogs_prim_exp_arg(b, s), 0x1u);
   }
   nir_pop_if(b, if_cull_en);

   /* Update shader arguments.
    *
    * The registers which hold information about the subgroup's
    * vertices and primitives are updated here, so the rest of the shader
    * doesn't need to worry about the culling.
    *
    * These "overwrite" intrinsics must be at top level control flow,
    * otherwise they can mess up the backend (eg. ACO's SSA).
    *
    * TODO:
    * A cleaner solution would be to simply replace all usages of these args
    * with the load of the variables.
    * However, this wouldn't work right now because the backend uses the arguments
    * for purposes not expressed in NIR, eg. VS input loads, etc.
    * This can change if VS input loads and other stuff are lowered to eg. load_buffer_amd.
    */

   if (b->shader->info.stage == MESA_SHADER_VERTEX)
      s->overwrite_args =
         nir_overwrite_vs_arguments_amd(b,
            nir_load_var(b, repacked_variables[0]), nir_load_var(b, repacked_variables[1]));
   else if (b->shader->info.stage == MESA_SHADER_TESS_EVAL)
      s->overwrite_args =
         nir_overwrite_tes_arguments_amd(b,
            nir_load_var(b, repacked_variables[0]), nir_load_var(b, repacked_variables[1]),
            nir_load_var(b, repacked_variables[2]), nir_load_var(b, s->repacked_rel_patch_id));
   else
      unreachable("Should be VS or TES.");
}

static void
ngg_nogs_store_edgeflag_to_lds(nir_builder *b, lower_ngg_nogs_state *s)
{
   if (!s->out.outputs[VARYING_SLOT_EDGE][0])
      return;

   /* clamp user edge flag to 1 for latter bit operations */
   nir_def *edgeflag = s->out.outputs[VARYING_SLOT_EDGE][0];
   edgeflag = nir_umin(b, edgeflag, nir_imm_int(b, 1));

   /* user edge flag is stored at the beginning of a vertex if streamout is not enabled */
   unsigned offset = 0;
   if (s->streamout_enabled) {
      unsigned packed_location =
         util_bitcount64(b->shader->info.outputs_written & BITFIELD64_MASK(VARYING_SLOT_EDGE));
      offset = packed_location * 16;
   }

   nir_def *tid = nir_load_local_invocation_index(b);
   nir_def *addr = pervertex_lds_addr(b, tid, s->pervertex_lds_bytes);

   nir_store_shared(b, edgeflag, addr, .base = offset);
}

static void
ngg_nogs_store_xfb_outputs_to_lds(nir_builder *b, lower_ngg_nogs_state *s)
{
   nir_xfb_info *info = ac_nir_get_sorted_xfb_info(b->shader);

   uint64_t xfb_outputs = 0;
   unsigned xfb_outputs_16bit = 0;
   uint8_t xfb_mask[VARYING_SLOT_MAX] = {0};
   uint8_t xfb_mask_16bit_lo[16] = {0};
   uint8_t xfb_mask_16bit_hi[16] = {0};

   /* Get XFB output mask for each slot. */
   for (int i = 0; i < info->output_count; i++) {
      nir_xfb_output_info *out = info->outputs + i;

      if (out->location < VARYING_SLOT_VAR0_16BIT) {
         xfb_outputs |= BITFIELD64_BIT(out->location);
         xfb_mask[out->location] |= out->component_mask;
      } else {
         unsigned index = out->location - VARYING_SLOT_VAR0_16BIT;
         xfb_outputs_16bit |= BITFIELD_BIT(index);

         if (out->high_16bits)
            xfb_mask_16bit_hi[index] |= out->component_mask;
         else
            xfb_mask_16bit_lo[index] |= out->component_mask;
      }
   }

   nir_def *tid = nir_load_local_invocation_index(b);
   nir_def *addr = pervertex_lds_addr(b, tid, s->pervertex_lds_bytes);

   u_foreach_bit64(slot, xfb_outputs) {
      uint64_t outputs_written = b->shader->info.outputs_written;
      if (s->skip_primitive_id)
         outputs_written &= ~VARYING_BIT_PRIMITIVE_ID;
      unsigned packed_location =
         util_bitcount64(outputs_written & BITFIELD64_MASK(slot));

      unsigned mask = xfb_mask[slot];

      /* Clear unused components. */
      for (unsigned i = 0; i < 4; i++) {
         if (!s->out.outputs[slot][i])
            mask &= ~BITFIELD_BIT(i);
      }

      while (mask) {
         int start, count;
         u_bit_scan_consecutive_range(&mask, &start, &count);
         /* Outputs here are sure to be 32bit.
          *
          * 64bit outputs have been lowered to two 32bit. As 16bit outputs:
          *   Vulkan does not allow streamout outputs less than 32bit.
          *   OpenGL puts 16bit outputs in VARYING_SLOT_VAR0_16BIT.
          */
         nir_def *store_val = nir_vec(b, &s->out.outputs[slot][start], (unsigned)count);
         nir_store_shared(b, store_val, addr, .base = packed_location * 16 + start * 4);
      }
   }

   unsigned num_32bit_outputs = util_bitcount64(b->shader->info.outputs_written);
   u_foreach_bit64(slot, xfb_outputs_16bit) {
      unsigned packed_location = num_32bit_outputs +
         util_bitcount(b->shader->info.outputs_written_16bit & BITFIELD_MASK(slot));

      unsigned mask_lo = xfb_mask_16bit_lo[slot];
      unsigned mask_hi = xfb_mask_16bit_hi[slot];

      /* Clear unused components. */
      for (unsigned i = 0; i < 4; i++) {
         if (!s->out.outputs_16bit_lo[slot][i])
            mask_lo &= ~BITFIELD_BIT(i);
         if (!s->out.outputs_16bit_hi[slot][i])
            mask_hi &= ~BITFIELD_BIT(i);
      }

      nir_def **outputs_lo = s->out.outputs_16bit_lo[slot];
      nir_def **outputs_hi = s->out.outputs_16bit_hi[slot];
      nir_def *undef = nir_undef(b, 1, 16);

      unsigned mask = mask_lo | mask_hi;
      while (mask) {
         int start, count;
         u_bit_scan_consecutive_range(&mask, &start, &count);

         nir_def *values[4] = {0};
         for (int c = start; c < start + count; ++c) {
            nir_def *lo = mask_lo & BITFIELD_BIT(c) ? outputs_lo[c] : undef;
            nir_def *hi = mask_hi & BITFIELD_BIT(c) ? outputs_hi[c] : undef;

            /* extend 8/16 bit to 32 bit, 64 bit has been lowered */
            values[c - start] = nir_pack_32_2x16_split(b, lo, hi);
         }

         nir_def *store_val = nir_vec(b, values, (unsigned)count);
         nir_store_shared(b, store_val, addr, .base = packed_location * 16 + start * 4);
      }
   }
}

static void
ngg_nogs_build_streamout(nir_builder *b, lower_ngg_nogs_state *s)
{
   nir_xfb_info *info = ac_nir_get_sorted_xfb_info(b->shader);

   nir_def *lds_scratch_base = nir_load_lds_ngg_scratch_base_amd(b);

   /* Get global buffer offset where this workgroup will stream out data to. */
   nir_def *generated_prim = nir_load_workgroup_num_input_primitives_amd(b);
   nir_def *gen_prim_per_stream[4] = {generated_prim, 0, 0, 0};
   nir_def *emit_prim_per_stream[4] = {0};
   nir_def *buffer_offsets[4] = {0};
   nir_def *so_buffer[4] = {0};
   nir_def *tid_in_tg = nir_load_local_invocation_index(b);
   ac_nir_ngg_build_streamout_buffer_info(b, info, s->options->hw_info->gfx_level, s->options->has_xfb_prim_query,
                                   s->options->use_gfx12_xfb_intrinsic, lds_scratch_base, tid_in_tg,
                                   gen_prim_per_stream,
                                   so_buffer, buffer_offsets,
                                   emit_prim_per_stream);

   /* Write out primitive data */
   nir_if *if_emit = nir_push_if(b, nir_ilt(b, tid_in_tg, emit_prim_per_stream[0]));
   {
      unsigned vtx_lds_stride = (b->shader->num_outputs * 4 + 1) * 4;
      nir_def *num_vert_per_prim = nir_load_num_vertices_per_primitive_amd(b);
      nir_def *first_vertex_idx = nir_imul(b, tid_in_tg, num_vert_per_prim);

      u_foreach_bit(buffer, info->buffers_written) {
         buffer_offsets[buffer] = nir_iadd(b, buffer_offsets[buffer],
                                           nir_imul_imm(b, first_vertex_idx,
                                                        info->buffers[buffer].stride));
      }

      for (unsigned i = 0; i < s->options->num_vertices_per_primitive; i++) {
         nir_if *if_valid_vertex =
            nir_push_if(b, nir_igt_imm(b, num_vert_per_prim, i));
         {
            nir_def *vtx_lds_idx = nir_load_var(b, s->gs_vtx_indices_vars[i]);
            nir_def *vtx_lds_addr = pervertex_lds_addr(b, vtx_lds_idx, vtx_lds_stride);
            ac_nir_ngg_build_streamout_vertex(b, info, 0, so_buffer, buffer_offsets, i,
                                       vtx_lds_addr, &s->out, s->skip_primitive_id);
         }
         nir_pop_if(b, if_valid_vertex);
      }
   }
   nir_pop_if(b, if_emit);

   /* Wait streamout memory ops done before export primitive, otherwise it
    * may not finish when shader ends.
    *
    * If a shader has no param exports, rasterization can start before
    * the shader finishes and thus memory stores might not finish before
    * the pixel shader starts.
    *
    * TODO: we only need this when no param exports.
    *
    * TODO: not sure if we need this barrier when late prim export, as I
    *       can't observe test fail without this barrier.
    */
   nir_scoped_memory_barrier(b, SCOPE_DEVICE, NIR_MEMORY_RELEASE, nir_var_mem_ssbo);
}

static unsigned
ngg_nogs_get_pervertex_lds_size(gl_shader_stage stage,
                                unsigned shader_num_outputs,
                                bool streamout_enabled,
                                bool export_prim_id,
                                bool has_user_edgeflags)
{
   unsigned pervertex_lds_bytes = 0;

   if (streamout_enabled) {
      /* The extra dword is used to avoid LDS bank conflicts and store the primitive id.
       * TODO: only alloc space for outputs that really need streamout.
       */
      pervertex_lds_bytes = (shader_num_outputs * 4 + 1) * 4;
   }

   bool need_prim_id_store_shared = export_prim_id && stage == MESA_SHADER_VERTEX;
   if (need_prim_id_store_shared || has_user_edgeflags) {
      unsigned size = 0;
      if (need_prim_id_store_shared)
         size += 4;
      if (has_user_edgeflags)
         size += 4;

      /* pad to odd dwords to avoid LDS bank conflict */
      size |= 4;

      pervertex_lds_bytes = MAX2(pervertex_lds_bytes, size);
   }

   return pervertex_lds_bytes;
}

static void
ngg_nogs_gather_outputs(nir_builder *b, struct exec_list *cf_list, lower_ngg_nogs_state *s)
{
   /* Assume:
    * - the shader used nir_lower_io_to_temporaries
    * - 64-bit outputs are lowered
    * - no indirect indexing is present
    */
   struct nir_cf_node *first_node =
      exec_node_data(nir_cf_node, exec_list_get_head(cf_list), node);

   for (nir_block *block = nir_cf_node_cf_tree_first(first_node); block != NULL;
        block = nir_block_cf_tree_next(block)) {
      nir_foreach_instr_safe (instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
         if (intrin->intrinsic != nir_intrinsic_store_output)
            continue;

         ac_nir_gather_prerast_store_output_info(b, intrin, &s->out);
         nir_instr_remove(instr);
      }
   }
}

void
ac_nir_lower_ngg_nogs(nir_shader *shader, const ac_nir_lower_ngg_options *options)
{
   nir_function_impl *impl = nir_shader_get_entrypoint(shader);
   assert(impl);
   assert(options->max_workgroup_size && options->wave_size);
   assert(!(options->can_cull && options->passthrough));

   nir_variable *position_value_var = nir_local_variable_create(impl, glsl_vec4_type(), "position_value");
   nir_variable *prim_exp_arg_var = nir_local_variable_create(impl, glsl_uint_type(), "prim_exp_arg");
   nir_variable *es_accepted_var =
      options->can_cull ? nir_local_variable_create(impl, glsl_bool_type(), "es_accepted") : NULL;
   nir_variable *gs_accepted_var =
      options->can_cull ? nir_local_variable_create(impl, glsl_bool_type(), "gs_accepted") : NULL;
   nir_variable *gs_exported_var = nir_local_variable_create(impl, glsl_bool_type(), "gs_exported");

   const bool wait_attr_ring = options->has_param_exports && options->hw_info->has_attr_ring_wait_bug;
   bool streamout_enabled = shader->xfb_info && !options->disable_streamout;
   bool has_user_edgeflags =
      options->use_edgeflags && (shader->info.outputs_written & VARYING_BIT_EDGE);
   /* streamout need to be done before either prim or vertex export. Because when no
    * param export, rasterization can start right after prim and vertex export,
    * which left streamout buffer writes un-finished.
    *
    * Always use late prim export when user edge flags are enabled.
    * This is because edge flags are written by ES threads but they
    * are exported by GS threads as part of th primitive export.
    *
    * When the primitive ID output is configured as a per-primitive,
    * and the shader must wait for attribute ring waits before exports,
    * we must always use late primitive export.
    */
   const bool early_prim_export =
      options->early_prim_export && !(streamout_enabled || has_user_edgeflags) &&
      !(wait_attr_ring && options->export_primitive_id_per_prim);

   lower_ngg_nogs_state state = {
      .options = options,
      .early_prim_export = early_prim_export,
      .streamout_enabled = streamout_enabled,
      .position_value_var = position_value_var,
      .prim_exp_arg_var = prim_exp_arg_var,
      .es_accepted_var = es_accepted_var,
      .gs_accepted_var = gs_accepted_var,
      .gs_exported_var = gs_exported_var,
      .max_num_waves = DIV_ROUND_UP(options->max_workgroup_size, options->wave_size),
      .has_user_edgeflags = has_user_edgeflags,
      .skip_primitive_id = streamout_enabled && (options->export_primitive_id || options->export_primitive_id_per_prim),
   };

   /* Can't export the primitive ID both as per-vertex and per-primitive. */
   assert(!options->export_primitive_id || !options->export_primitive_id_per_prim);

   const bool need_prim_id_store_shared =
      options->export_primitive_id && shader->info.stage == MESA_SHADER_VERTEX;

   if (options->export_primitive_id) {
      shader->info.outputs_written |= VARYING_BIT_PRIMITIVE_ID;
   }

   if (options->export_primitive_id_per_prim) {
      /* The HW preloads the primitive ID to VGPRs of GS threads for VS, but not for TES. */
      assert(shader->info.stage == MESA_SHADER_VERTEX);
      assert(options->hw_info->gfx_level >= GFX10_3);
   }

   nir_builder builder = nir_builder_create(impl);
   nir_builder *b = &builder; /* This is to avoid the & */

   if (options->can_cull) {
      analyze_shader_before_culling(shader, &state);
      save_reusable_variables(b, &state);
   }

   nir_cf_list extracted;
   nir_cf_extract(&extracted, nir_before_impl(impl),
                  nir_after_impl(impl));
   b->cursor = nir_before_impl(impl);

   ngg_nogs_init_vertex_indices_vars(b, impl, &state);

   /* Emit primitives generated query code here, so that
    * it executes before culling and isn't in the extracted CF.
    */
   nogs_prim_gen_query(b, &state);

   /* Whether a shader invocation should export a primitive,
    * initialize to all invocations that have an input primitive.
    */
   nir_store_var(b, gs_exported_var, has_input_primitive(b), 0x1u);

   if (!options->can_cull) {
      /* Newer chips can use PRIMGEN_PASSTHRU_NO_MSG to skip gs_alloc_req for NGG passthrough. */
      if (!(options->passthrough && options->hw_info->has_ngg_passthru_no_msg)) {
         /* Allocate export space on wave 0 - confirm to the HW that we want to use all possible space */
         nir_if *if_wave_0 = nir_push_if(b, nir_ieq_imm(b, nir_load_subgroup_id(b), 0));
         {
            nir_def *vtx_cnt = nir_load_workgroup_num_input_vertices_amd(b);
            nir_def *prim_cnt = nir_load_workgroup_num_input_primitives_amd(b);
            ac_nir_ngg_alloc_vertices_and_primitives(b, vtx_cnt, prim_cnt, false);
         }
         nir_pop_if(b, if_wave_0);
      }

      /* Take care of early primitive export, otherwise just pack the primitive export argument */
      if (state.early_prim_export)
         emit_ngg_nogs_prim_export(b, &state, NULL);
      else
         nir_store_var(b, prim_exp_arg_var, emit_ngg_nogs_prim_exp_arg(b, &state), 0x1u);
   } else {
      add_deferred_attribute_culling(b, &extracted, &state);
      b->cursor = nir_after_impl(impl);

      if (state.early_prim_export)
         emit_ngg_nogs_prim_export(b, &state, nir_load_var(b, state.prim_exp_arg_var));

      /* Wait for culling to finish using LDS. */
      if (need_prim_id_store_shared || has_user_edgeflags) {
         nir_barrier(b, .execution_scope = SCOPE_WORKGROUP,
                               .memory_scope = SCOPE_WORKGROUP,
                               .memory_semantics = NIR_MEMORY_ACQ_REL,
                               .memory_modes = nir_var_mem_shared);
      }
   }

   /* determine the LDS vertex stride */
   state.pervertex_lds_bytes =
      ngg_nogs_get_pervertex_lds_size(shader->info.stage,
                                      shader->num_outputs,
                                      state.streamout_enabled,
                                      options->export_primitive_id,
                                      state.has_user_edgeflags);

   if (need_prim_id_store_shared) {
      emit_ngg_nogs_prim_id_store_shared(b, &state);

      /* Wait for GS threads to store primitive ID in LDS. */
      nir_barrier(b, .execution_scope = SCOPE_WORKGROUP, .memory_scope = SCOPE_WORKGROUP,
                            .memory_semantics = NIR_MEMORY_ACQ_REL, .memory_modes = nir_var_mem_shared);
   } else if (options->export_primitive_id_per_prim && options->hw_info->has_attr_ring) {
      emit_ngg_nogs_prim_id_store_per_prim_to_attr_ring(b, &state);
   }

   nir_def *es_thread =
      options->can_cull ? nir_load_var(b, es_accepted_var) : has_input_vertex(b);

   /* Calculate the bit count here instead of below for lower SGPR usage and better ALU
    * scheduling.
    */
   nir_def *num_es_threads = NULL;
   if (options->hw_info->has_attr_ring && options->can_cull) {
      nir_def *es_accepted_mask =
         nir_ballot(b, 1, options->wave_size, nir_load_var(b, es_accepted_var));
      num_es_threads = nir_bit_count(b, es_accepted_mask);
   }

   nir_if *if_es_thread = nir_push_if(b, es_thread);
   {
      /* Run the actual shader */
      nir_cf_reinsert(&extracted, b->cursor);
      b->cursor = nir_after_cf_list(&if_es_thread->then_list);

      if (options->export_primitive_id)
         emit_store_ngg_nogs_es_primitive_id(b, &state);
   }
   nir_pop_if(b, if_es_thread);

   if (options->can_cull) {
      /* Replace uniforms. */
      apply_reusable_variables(b, &state);

      /* Remove the redundant position output. */
      remove_extra_pos_outputs(shader, &state);

      /* After looking at the performance in apps eg. Doom Eternal, and The Witcher 3,
       * it seems that it's best to put the position export always at the end, and
       * then let ACO schedule it up (slightly) only when early prim export is used.
       */
      b->cursor = nir_after_cf_list(&if_es_thread->then_list);

      nir_def *pos_val = nir_load_var(b, state.position_value_var);
      for (int i = 0; i < 4; i++)
         state.out.outputs[VARYING_SLOT_POS][i] = nir_channel(b, pos_val, i);
   }

   /* Gather outputs data and types */
   ngg_nogs_gather_outputs(b, &if_es_thread->then_list, &state);
   b->cursor = nir_after_cf_list(&if_es_thread->then_list);

   /* This should be after streamout and before exports. */
   ac_nir_clamp_vertex_color_outputs(b, &state.out);

   if (state.has_user_edgeflags)
      ngg_nogs_store_edgeflag_to_lds(b, &state);

   if (state.streamout_enabled) {
      /* TODO: support culling after streamout. */
      assert(!options->can_cull);

      ngg_nogs_store_xfb_outputs_to_lds(b, &state);

      b->cursor = nir_after_impl(impl);
      ngg_nogs_build_streamout(b, &state);
   }

   /* Take care of late primitive export */
   nir_if *if_late_prim_export = NULL;
   if (!state.early_prim_export) {
      b->cursor = nir_after_impl(impl);

      if (wait_attr_ring && options->export_primitive_id_per_prim) {
         /* Wait for the per-primitive primitive ID store to finish. */
         nir_barrier(b, .execution_scope = SCOPE_SUBGROUP,
                        .memory_scope = SCOPE_DEVICE,
                        .memory_semantics = NIR_MEMORY_RELEASE,
                        .memory_modes = nir_var_mem_ssbo | nir_var_shader_out | nir_var_mem_global | nir_var_image);
      }

      if_late_prim_export = emit_ngg_nogs_prim_export(b, &state, nir_load_var(b, prim_exp_arg_var));
   }

   uint64_t export_outputs = shader->info.outputs_written | VARYING_BIT_POS;
   if (options->kill_pointsize)
      export_outputs &= ~VARYING_BIT_PSIZ;
   if (options->kill_layer)
      export_outputs &= ~VARYING_BIT_LAYER;

   /* If streamout is enabled, export positions after streamout. This increases streamout performance
    * for up to 4 vec4 xfb outputs on GFX12 because the streamout code doesn't have go through
    * the export allocation bottleneck. Adding more xfb outputs starts to be limited by the memory
    * bandwidth.
    */
   const bool pos_exports_in_cf = state.streamout_enabled || wait_attr_ring;

   nir_if *if_pos_exports = NULL;
   if (pos_exports_in_cf) {
      b->cursor = nir_after_cf_node(&if_es_thread->cf_node);
      ac_nir_create_output_phis(b, b->shader->info.outputs_written, b->shader->info.outputs_written_16bit, &state.out);

      b->cursor = nir_after_impl(impl);
      if_pos_exports = nir_push_if(b, es_thread);
   } else {
      b->cursor = nir_after_cf_list(&if_es_thread->then_list);
   }

   ac_nir_export_position(b, options->hw_info->gfx_level,
                          options->clip_cull_dist_mask,
                          !options->has_param_exports,
                          options->force_vrs, true,
                          export_outputs, &state.out, NULL);

   if (options->has_param_exports && !options->hw_info->has_attr_ring) {
      ac_nir_export_parameters(b, options->vs_output_param_offset,
                               b->shader->info.outputs_written,
                               b->shader->info.outputs_written_16bit,
                               &state.out);
   }

   if (if_pos_exports)
      nir_pop_if(b, if_pos_exports);

   if (options->has_param_exports && options->hw_info->has_attr_ring) {
      if (!pos_exports_in_cf) {
         b->cursor = nir_after_cf_node(&if_es_thread->cf_node);
         ac_nir_create_output_phis(b, b->shader->info.outputs_written, b->shader->info.outputs_written_16bit, &state.out);
      }

      if (!wait_attr_ring)
         b->cursor = nir_after_impl(impl);
      else if (if_late_prim_export)
         b->cursor = nir_after_cf_node_and_phis(&if_late_prim_export->cf_node);
      else
         b->cursor = nir_after_cf_node_and_phis(&if_es_thread->cf_node);

      if (!num_es_threads)
         num_es_threads = nir_load_merged_wave_info_amd(b);

      ac_nir_store_parameters_to_attr_ring(b, options->vs_output_param_offset,
                                          b->shader->info.outputs_written,
                                          b->shader->info.outputs_written_16bit,
                                          &state.out, num_es_threads);

      if (wait_attr_ring) {
         /* Wait for attribute ring stores to finish. */
         nir_barrier(b, .execution_scope = SCOPE_SUBGROUP,
                        .memory_scope = SCOPE_DEVICE,
                        .memory_semantics = NIR_MEMORY_RELEASE,
                        .memory_modes = nir_var_mem_ssbo | nir_var_shader_out | nir_var_mem_global | nir_var_image);
      }
   }

   nir_metadata_preserve(impl, nir_metadata_none);
   nir_validate_shader(shader, "after emitting NGG VS/TES");

   /* Cleanup */
   nir_opt_dead_write_vars(shader);
   nir_lower_vars_to_ssa(shader);
   nir_remove_dead_variables(shader, nir_var_function_temp, NULL);
   nir_lower_alu_to_scalar(shader, NULL, NULL);
   nir_lower_phis_to_scalar(shader, true);

   if (options->can_cull) {
      /* It's beneficial to redo these opts after splitting the shader. */
      nir_opt_sink(shader, nir_move_load_input | nir_move_const_undef | nir_move_copies);
      nir_opt_move(shader, nir_move_load_input | nir_move_copies | nir_move_const_undef);
   }

   bool progress;
   do {
      progress = false;
      NIR_PASS(progress, shader, nir_opt_undef);
      NIR_PASS(progress, shader, nir_opt_dce);
      NIR_PASS(progress, shader, nir_opt_dead_cf);

      if (options->can_cull)
         progress |= cleanup_culling_shader_after_dce(shader, b->impl, &state);
   } while (progress);
}

unsigned
ac_ngg_nogs_get_pervertex_lds_size(gl_shader_stage stage,
                                   unsigned shader_num_outputs,
                                   bool streamout_enabled,
                                   bool export_prim_id,
                                   bool has_user_edgeflags,
                                   bool can_cull,
                                   bool uses_instance_id,
                                   bool uses_primitive_id)
{
   /* for culling time lds layout only */
   unsigned culling_pervertex_lds_bytes = can_cull ?
      ngg_nogs_get_culling_pervertex_lds_size(
         stage, uses_instance_id, uses_primitive_id, NULL) : 0;

   unsigned pervertex_lds_bytes =
      ngg_nogs_get_pervertex_lds_size(stage, shader_num_outputs, streamout_enabled,
                                      export_prim_id, has_user_edgeflags);

   return MAX2(culling_pervertex_lds_bytes, pervertex_lds_bytes);
}

unsigned
ac_ngg_get_scratch_lds_size(gl_shader_stage stage,
                            unsigned workgroup_size,
                            unsigned wave_size,
                            bool streamout_enabled,
                            bool can_cull,
                            bool compact_primitives)
{
   unsigned scratch_lds_size = 0;
   unsigned max_num_waves = DIV_ROUND_UP(workgroup_size, wave_size);

   if (stage == MESA_SHADER_VERTEX || stage == MESA_SHADER_TESS_EVAL) {
      if (streamout_enabled) {
         /* 4 dwords for 4 streamout buffer offset, 1 dword for emit prim count */
         scratch_lds_size = 20;
      } else if (can_cull) {
         /* 1 byte per wave per repack, max 8 waves */
         unsigned num_rep = compact_primitives ? 2 : 1;
         scratch_lds_size = ALIGN(max_num_waves, 4u) * num_rep;
      }
   } else {
      assert(stage == MESA_SHADER_GEOMETRY);

      scratch_lds_size = ALIGN(max_num_waves, 4u);
      /* streamout take 8 dwords for buffer offset and emit vertex per stream */
      if (streamout_enabled)
         scratch_lds_size = MAX2(scratch_lds_size, 32);
   }

   return scratch_lds_size;
}
