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

typedef struct
{
   const ac_nir_lower_ngg_options *options;

   nir_function_impl *impl;
   int const_out_vtxcnt[4];
   int const_out_prmcnt[4];
   unsigned max_num_waves;
   unsigned num_vertices_per_primitive;
   nir_def *lds_addr_gs_out_vtx;
   nir_def *lds_addr_gs_scratch;
   unsigned lds_bytes_per_gs_out_vertex;
   unsigned lds_offs_primflags;
   bool output_compile_time_known;
   bool streamout_enabled;
   /* Outputs */
   ac_nir_prerast_out out;
   /* Count per stream. */
   nir_def *vertex_count[4];
   nir_def *primitive_count[4];
} lower_ngg_gs_state;

/**
 * Return the address of the LDS storage reserved for the N'th vertex,
 * where N is in emit order, meaning:
 * - during the finale, N is the invocation_index (within the workgroup)
 * - during vertex emit, i.e. while the API GS shader invocation is running,
 *   N = invocation_index * gs_max_out_vertices + emit_idx
 *   where emit_idx is the vertex index in the current API GS invocation.
 *
 * Goals of the LDS memory layout:
 * 1. Eliminate bank conflicts on write for geometry shaders that have all emits
 *    in uniform control flow
 * 2. Eliminate bank conflicts on read for export if, additionally, there is no
 *    culling
 * 3. Agnostic to the number of waves (since we don't know it before compiling)
 * 4. Allow coalescing of LDS instructions (ds_write_b128 etc.)
 * 5. Avoid wasting memory.
 *
 * We use an AoS layout due to point 4 (this also helps point 3). In an AoS
 * layout, elimination of bank conflicts requires that each vertex occupy an
 * odd number of dwords. We use the additional dword to store the output stream
 * index as well as a flag to indicate whether this vertex ends a primitive
 * for rasterization.
 *
 * Swizzling is required to satisfy points 1 and 2 simultaneously.
 *
 * Vertices are stored in export order (gsthread * gs_max_out_vertices + emitidx).
 * Indices are swizzled in groups of 32, which ensures point 1 without
 * disturbing point 2.
 *
 * \return an LDS pointer to type {[N x i32], [4 x i8]}
 */
static nir_def *
ngg_gs_out_vertex_addr(nir_builder *b, nir_def *out_vtx_idx, lower_ngg_gs_state *s)
{
   unsigned write_stride_2exp = ffs(MAX2(b->shader->info.gs.vertices_out, 1)) - 1;

   /* gs_max_out_vertices = 2^(write_stride_2exp) * some odd number */
   if (write_stride_2exp) {
      nir_def *row = nir_ushr_imm(b, out_vtx_idx, 5);
      nir_def *swizzle = nir_iand_imm(b, row, (1u << write_stride_2exp) - 1u);
      out_vtx_idx = nir_ixor(b, out_vtx_idx, swizzle);
   }

   nir_def *out_vtx_offs = nir_imul_imm(b, out_vtx_idx, s->lds_bytes_per_gs_out_vertex);
   return nir_iadd_nuw(b, out_vtx_offs, s->lds_addr_gs_out_vtx);
}

static nir_def *
ngg_gs_emit_vertex_addr(nir_builder *b, nir_def *gs_vtx_idx, lower_ngg_gs_state *s)
{
   nir_def *tid_in_tg = nir_load_local_invocation_index(b);
   nir_def *gs_out_vtx_base = nir_imul_imm(b, tid_in_tg, b->shader->info.gs.vertices_out);
   nir_def *out_vtx_idx = nir_iadd_nuw(b, gs_out_vtx_base, gs_vtx_idx);

   return ngg_gs_out_vertex_addr(b, out_vtx_idx, s);
}

static void
ngg_gs_clear_primflags(nir_builder *b, nir_def *num_vertices, unsigned stream, lower_ngg_gs_state *s)
{
   char name[32];
   snprintf(name, sizeof(name), "clear_primflag_idx_%u", stream);
   nir_variable *clear_primflag_idx_var = nir_local_variable_create(b->impl, glsl_uint_type(), name);

   nir_def *zero_u8 = nir_imm_zero(b, 1, 8);
   nir_store_var(b, clear_primflag_idx_var, num_vertices, 0x1u);

   nir_loop *loop = nir_push_loop(b);
   {
      nir_def *clear_primflag_idx = nir_load_var(b, clear_primflag_idx_var);
      nir_if *if_break = nir_push_if(b, nir_uge_imm(b, clear_primflag_idx, b->shader->info.gs.vertices_out));
      {
         nir_jump(b, nir_jump_break);
      }
      nir_push_else(b, if_break);
      {
         nir_def *emit_vtx_addr = ngg_gs_emit_vertex_addr(b, clear_primflag_idx, s);
         nir_store_shared(b, zero_u8, emit_vtx_addr, .base = s->lds_offs_primflags + stream);
         nir_store_var(b, clear_primflag_idx_var, nir_iadd_imm_nuw(b, clear_primflag_idx, 1), 0x1u);
      }
      nir_pop_if(b, if_break);
   }
   nir_pop_loop(b, loop);
}

static bool
lower_ngg_gs_store_output(nir_builder *b, nir_intrinsic_instr *intrin, lower_ngg_gs_state *s)
{
   ac_nir_gather_prerast_store_output_info(b, intrin, &s->out);
   nir_instr_remove(&intrin->instr);
   return true;
}

static unsigned
gs_output_component_mask_with_stream(ac_nir_prerast_per_output_info *info, unsigned stream)
{
   unsigned mask = info->components_mask;
   if (!mask)
      return 0;

   /* clear component when not requested stream */
   for (int i = 0; i < 4; i++) {
      if (((info->stream >> (i * 2)) & 3) != stream)
         mask &= ~(1 << i);
   }

   return mask;
}

static bool
lower_ngg_gs_emit_vertex_with_counter(nir_builder *b, nir_intrinsic_instr *intrin, lower_ngg_gs_state *s)
{
   b->cursor = nir_before_instr(&intrin->instr);

   unsigned stream = nir_intrinsic_stream_id(intrin);
   if (!(b->shader->info.gs.active_stream_mask & (1 << stream))) {
      nir_instr_remove(&intrin->instr);
      return true;
   }

   nir_def *gs_emit_vtx_idx = intrin->src[0].ssa;
   nir_def *current_vtx_per_prim = intrin->src[1].ssa;
   nir_def *gs_emit_vtx_addr = ngg_gs_emit_vertex_addr(b, gs_emit_vtx_idx, s);

   /* Store generic 32-bit outputs to LDS.
    * In case of packed 16-bit, we assume that has been already packed into 32 bit slots by now.
    */
   u_foreach_bit64(slot, b->shader->info.outputs_written) {
      const unsigned packed_location = util_bitcount64((b->shader->info.outputs_written & BITFIELD64_MASK(slot)));
      unsigned mask = gs_output_component_mask_with_stream(&s->out.infos[slot], stream);

      nir_def **output = s->out.outputs[slot];
      nir_def *undef = nir_undef(b, 1, 32);

      while (mask) {
         int start, count;
         u_bit_scan_consecutive_range(&mask, &start, &count);
         nir_def *values[4] = {0};
         for (int c = start; c < start + count; ++c) {
            if (!output[c]) {
               /* The shader hasn't written this output. */
               values[c - start] = undef;
            } else {
               assert(output[c]->bit_size == 32);
               values[c - start] = output[c];
            }
         }

         nir_def *store_val = nir_vec(b, values, (unsigned)count);
         nir_store_shared(b, store_val, gs_emit_vtx_addr,
                          .base = packed_location * 16 + start * 4,
                          .align_mul = 4);
      }

      /* Clear all outputs (they are undefined after emit_vertex) */
      memset(s->out.outputs[slot], 0, sizeof(s->out.outputs[slot]));
   }

   const unsigned num_32bit_outputs = util_bitcount64(b->shader->info.outputs_written);

   /* Store dedicated 16-bit outputs to LDS. */
   u_foreach_bit(slot, b->shader->info.outputs_written_16bit) {
      const unsigned packed_location = num_32bit_outputs +
         util_bitcount(b->shader->info.outputs_written_16bit & BITFIELD_MASK(slot));

      const unsigned mask_lo = gs_output_component_mask_with_stream(s->out.infos_16bit_lo + slot, stream);
      const unsigned mask_hi = gs_output_component_mask_with_stream(s->out.infos_16bit_hi + slot, stream);
      unsigned mask = mask_lo | mask_hi;

      nir_def **output_lo = s->out.outputs_16bit_lo[slot];
      nir_def **output_hi = s->out.outputs_16bit_hi[slot];
      nir_def *undef = nir_undef(b, 1, 16);

      while (mask) {
         int start, count;
         u_bit_scan_consecutive_range(&mask, &start, &count);
         nir_def *values[4] = {0};
         for (int c = start; c < start + count; ++c) {
            nir_def *lo = output_lo[c] ? output_lo[c] : undef;
            nir_def *hi = output_hi[c] ? output_hi[c] : undef;

            values[c - start] = nir_pack_32_2x16_split(b, lo, hi);
         }

         nir_def *store_val = nir_vec(b, values, (unsigned)count);
         nir_store_shared(b, store_val, gs_emit_vtx_addr,
                          .base = packed_location * 16 + start * 4,
                          .align_mul = 4);
      }

      /* Clear all outputs (they are undefined after emit_vertex) */
      memset(s->out.outputs_16bit_lo[slot], 0, sizeof(s->out.outputs_16bit_lo[slot]));
      memset(s->out.outputs_16bit_hi[slot], 0, sizeof(s->out.outputs_16bit_hi[slot]));
   }

   /* Calculate and store per-vertex primitive flags based on vertex counts:
    * - bit 0: whether this vertex finishes a primitive (a real primitive, not the strip)
    * - bit 1: whether the primitive index is odd (if we are emitting triangle strips, otherwise always 0)
    *          only set when the vertex also finishes the primitive
    * - bit 2: whether vertex is live (if culling is enabled: set after culling, otherwise always 1)
    */

   nir_def *vertex_live_flag =
      !stream && s->options->can_cull
         ? nir_ishl_imm(b, nir_b2i32(b, nir_inot(b, nir_load_cull_any_enabled_amd(b))), 2)
         : nir_imm_int(b, 0b100);

   nir_def *completes_prim = nir_ige_imm(b, current_vtx_per_prim, s->num_vertices_per_primitive - 1);
   nir_def *complete_flag = nir_b2i32(b, completes_prim);

   nir_def *prim_flag = nir_ior(b, vertex_live_flag, complete_flag);
   if (s->num_vertices_per_primitive == 3) {
      nir_def *odd = nir_iand(b, current_vtx_per_prim, complete_flag);
      nir_def *odd_flag = nir_ishl_imm(b, odd, 1);
      prim_flag = nir_ior(b, prim_flag, odd_flag);
   }

   nir_store_shared(b, nir_u2u8(b, prim_flag), gs_emit_vtx_addr,
                    .base = s->lds_offs_primflags + stream,
                    .align_mul = 4, .align_offset = stream);

   nir_instr_remove(&intrin->instr);
   return true;
}

static bool
lower_ngg_gs_end_primitive_with_counter(nir_builder *b, nir_intrinsic_instr *intrin, UNUSED lower_ngg_gs_state *s)
{
   b->cursor = nir_before_instr(&intrin->instr);

   /* These are not needed, we can simply remove them */
   nir_instr_remove(&intrin->instr);
   return true;
}

static bool
lower_ngg_gs_set_vertex_and_primitive_count(nir_builder *b, nir_intrinsic_instr *intrin, lower_ngg_gs_state *s)
{
   b->cursor = nir_before_instr(&intrin->instr);

   unsigned stream = nir_intrinsic_stream_id(intrin);
   if (stream > 0 && !(b->shader->info.gs.active_stream_mask & (1 << stream))) {
      nir_instr_remove(&intrin->instr);
      return true;
   }

   s->vertex_count[stream] = intrin->src[0].ssa;
   s->primitive_count[stream] = intrin->src[1].ssa;

   /* Clear the primitive flags of non-emitted vertices */
   if (!nir_src_is_const(intrin->src[0]) || nir_src_as_uint(intrin->src[0]) < b->shader->info.gs.vertices_out)
      ngg_gs_clear_primflags(b, intrin->src[0].ssa, stream, s);

   nir_instr_remove(&intrin->instr);
   return true;
}

static bool
lower_ngg_gs_intrinsic(nir_builder *b, nir_intrinsic_instr *intrin, void *state)
{
   lower_ngg_gs_state *s = (lower_ngg_gs_state *) state;

   if (intrin->intrinsic == nir_intrinsic_store_output)
      return lower_ngg_gs_store_output(b, intrin, s);
   else if (intrin->intrinsic == nir_intrinsic_emit_vertex_with_counter)
      return lower_ngg_gs_emit_vertex_with_counter(b, intrin, s);
   else if (intrin->intrinsic == nir_intrinsic_end_primitive_with_counter)
      return lower_ngg_gs_end_primitive_with_counter(b, intrin, s);
   else if (intrin->intrinsic == nir_intrinsic_set_vertex_and_primitive_count)
      return lower_ngg_gs_set_vertex_and_primitive_count(b, intrin, s);

   return false;
}

static void
lower_ngg_gs_intrinsics(nir_shader *shader, lower_ngg_gs_state *s)
{
   nir_shader_intrinsics_pass(shader, lower_ngg_gs_intrinsic, nir_metadata_none, s);
}

static nir_def *
ngg_gs_process_out_primitive(nir_builder *b,
                             nir_def *exporter_tid_in_tg, nir_def *primflag_0,
                             lower_ngg_gs_state *s)
{
   /* Only bit 0 matters here - set it to 1 when the primitive should be null */
   nir_def *is_null_prim = nir_ixor(b, primflag_0, nir_imm_int(b, -1u));

   nir_def *vtx_indices[3] = {0};
   vtx_indices[s->num_vertices_per_primitive - 1] = exporter_tid_in_tg;
   if (s->num_vertices_per_primitive >= 2)
      vtx_indices[s->num_vertices_per_primitive - 2] = nir_iadd_imm(b, exporter_tid_in_tg, -1);
   if (s->num_vertices_per_primitive == 3)
      vtx_indices[s->num_vertices_per_primitive - 3] = nir_iadd_imm(b, exporter_tid_in_tg, -2);

   if (s->num_vertices_per_primitive == 3) {
      /* API GS outputs triangle strips, but NGG HW understands triangles.
       * We already know the triangles due to how we set the primitive flags, but we need to
       * make sure the vertex order is so that the front/back is correct, and the provoking vertex is kept.
       */

      nir_def *is_odd = nir_ubfe_imm(b, primflag_0, 1, 1);
      nir_def *provoking_vertex_index = nir_load_provoking_vtx_in_prim_amd(b);
      nir_def *provoking_vertex_first = nir_ieq_imm(b, provoking_vertex_index, 0);

      vtx_indices[0] = nir_bcsel(b, provoking_vertex_first, vtx_indices[0],
                                 nir_iadd(b, vtx_indices[0], is_odd));
      vtx_indices[1] = nir_bcsel(b, provoking_vertex_first,
                                 nir_iadd(b, vtx_indices[1], is_odd),
                                 nir_isub(b, vtx_indices[1], is_odd));
      vtx_indices[2] = nir_bcsel(b, provoking_vertex_first,
                                 nir_isub(b, vtx_indices[2], is_odd), vtx_indices[2]);
   }

   return ac_nir_pack_ngg_prim_exp_arg(b, s->num_vertices_per_primitive, vtx_indices,
                                             is_null_prim, s->options->hw_info->gfx_level);
}

static void
ngg_gs_process_out_vertex(nir_builder *b, nir_def *out_vtx_lds_addr, lower_ngg_gs_state *s)
{
   nir_def *exported_out_vtx_lds_addr = out_vtx_lds_addr;

   if (!s->output_compile_time_known) {
      /* Vertex compaction.
       * The current thread will export a vertex that was live in another invocation.
       * Load the index of the vertex that the current thread will have to export.
       */
      nir_def *exported_vtx_idx = nir_load_shared(b, 1, 8, out_vtx_lds_addr, .base = s->lds_offs_primflags + 1);
      exported_out_vtx_lds_addr = ngg_gs_out_vertex_addr(b, nir_u2u32(b, exported_vtx_idx), s);
   }

   u_foreach_bit64(slot, b->shader->info.outputs_written) {
      const unsigned packed_location =
         util_bitcount64((b->shader->info.outputs_written & BITFIELD64_MASK(slot)));

      unsigned mask = gs_output_component_mask_with_stream(&s->out.infos[slot], 0);

      while (mask) {
         int start, count;
         u_bit_scan_consecutive_range(&mask, &start, &count);
         nir_def *load =
            nir_load_shared(b, count, 32, exported_out_vtx_lds_addr,
                            .base = packed_location * 16 + start * 4,
                            .align_mul = 4);

         for (int i = 0; i < count; i++)
            s->out.outputs[slot][start + i] = nir_channel(b, load, i);
      }
   }

   const unsigned num_32bit_outputs = util_bitcount64(b->shader->info.outputs_written);

   /* Dedicated 16-bit outputs. */
   u_foreach_bit(i, b->shader->info.outputs_written_16bit) {
      const unsigned packed_location = num_32bit_outputs +
         util_bitcount(b->shader->info.outputs_written_16bit & BITFIELD_MASK(i));

      const unsigned mask_lo = gs_output_component_mask_with_stream(&s->out.infos_16bit_lo[i], 0);
      const unsigned mask_hi = gs_output_component_mask_with_stream(&s->out.infos_16bit_hi[i], 0);
      unsigned mask = mask_lo | mask_hi;

      while (mask) {
         int start, count;
         u_bit_scan_consecutive_range(&mask, &start, &count);
         nir_def *load =
            nir_load_shared(b, count, 32, exported_out_vtx_lds_addr,
                            .base = packed_location * 16 + start * 4,
                            .align_mul = 4);

         for (int j = 0; j < count; j++) {
            nir_def *val = nir_channel(b, load, j);
            unsigned comp = start + j;

            if (mask_lo & BITFIELD_BIT(comp))
               s->out.outputs_16bit_lo[i][comp] = nir_unpack_32_2x16_split_x(b, val);

            if (mask_hi & BITFIELD_BIT(comp))
               s->out.outputs_16bit_hi[i][comp] = nir_unpack_32_2x16_split_y(b, val);
         }
      }
   }

   /* This should be after streamout and before exports. */
   ac_nir_clamp_vertex_color_outputs(b, &s->out);
}

/**
 * Emit NGG GS output, including vertex and primitive exports and attribute ring stores (if any).
 * The exact sequence emitted, depends on the current GPU and its workarounds.
 *
 * The order mainly depends on whether the current GPU has an attribute ring, and
 * whether it has the bug that requires us to emit a wait for the attribute ring stores.
 *
 * The basic structure looks like this:
 *
 * if (has primitive) {
 *    <per-primitive processing: calculation of the primitive export argument>
 *
 *    if (!(wait for attr ring)) {
 *       <primitive export>
 *    }
 * }
 * if (has vertex) {
 *    <per-vertex processing: load each output from LDS, and perform necessary adjustments>
 *
 *    if (!(wait for attr ring)) {
 *       <vertex position exports>
 *       <vertex parameter exports>
 *    }
 * }
 * <per-vertex attribute ring stores, if the current GPU has an attribute ring>
 * if (wait for attr ring) {
 *    <barrier to wait for attribute ring stores>
 *    if (has primitive) {
 *       <primitive export>
 *    }
 *    if (has vertex) {
 *       <vertex position exports>
 *       <vertex parameter exports>
 *    }
 * }
 *
 */
static void
ngg_gs_emit_output(nir_builder *b, nir_def *max_num_out_vtx, nir_def *max_num_out_prims,
                   nir_def *tid_in_tg, nir_def *out_vtx_lds_addr, nir_def *prim_exporter_tid_in_tg,
                   nir_def *primflag_0, lower_ngg_gs_state *s)
{
   nir_def *undef = nir_undef(b, 1, 32);

   /* Primitive processing */
   nir_def *prim_exp_arg = NULL;
   nir_if *if_process_primitive = nir_push_if(b, nir_ilt(b, tid_in_tg, max_num_out_prims));
   {
      prim_exp_arg = ngg_gs_process_out_primitive(b, prim_exporter_tid_in_tg, primflag_0, s);
   }
   nir_pop_if(b, if_process_primitive);
   prim_exp_arg = nir_if_phi(b, prim_exp_arg, undef);

   /* Vertex processing */
   nir_if *if_process_vertex = nir_push_if(b, nir_ilt(b, tid_in_tg, max_num_out_vtx));
   {
      ngg_gs_process_out_vertex(b, out_vtx_lds_addr, s);
   }
   nir_pop_if(b, if_process_vertex);
   ac_nir_create_output_phis(b, b->shader->info.outputs_written, b->shader->info.outputs_written_16bit, &s->out);

   nir_if *if_export_primitive = nir_push_if(b, if_process_primitive->condition.ssa);
   {
      ac_nir_export_primitive(b, prim_exp_arg, NULL);
   }
   nir_pop_if(b, if_export_primitive);

   nir_if *if_export_vertex = nir_push_if(b, if_process_vertex->condition.ssa);
   {
      uint64_t export_outputs = b->shader->info.outputs_written | VARYING_BIT_POS;
      if (s->options->kill_pointsize)
         export_outputs &= ~VARYING_BIT_PSIZ;
      if (s->options->kill_layer)
         export_outputs &= ~VARYING_BIT_LAYER;

      ac_nir_export_position(b, s->options->hw_info->gfx_level,
                             s->options->clip_cull_dist_mask,
                             !s->options->has_param_exports,
                             s->options->force_vrs, true,
                             export_outputs, &s->out, NULL);

      if (s->options->has_param_exports && !s->options->hw_info->has_attr_ring)
         ac_nir_export_parameters(b, s->options->vs_output_param_offset,
                                  b->shader->info.outputs_written,
                                  b->shader->info.outputs_written_16bit,
                                  &s->out);
   }
   nir_pop_if(b, if_export_vertex);

   if (s->options->has_param_exports && s->options->hw_info->has_attr_ring) {
      if (s->options->hw_info->has_attr_ring_wait_bug)
         b->cursor = nir_after_cf_node_and_phis(&if_export_primitive->cf_node);

      nir_def *vertices_in_wave = nir_bit_count(b, nir_ballot(b, 1, s->options->wave_size, if_process_vertex->condition.ssa));

      ac_nir_store_parameters_to_attr_ring(b, s->options->vs_output_param_offset,
                                           b->shader->info.outputs_written,
                                           b->shader->info.outputs_written_16bit,
                                           &s->out, vertices_in_wave);

      if (s->options->hw_info->has_attr_ring_wait_bug) {
         /* Wait for attribute ring stores to finish. */
         nir_barrier(b, .execution_scope = SCOPE_SUBGROUP,
                        .memory_scope = SCOPE_DEVICE,
                        .memory_semantics = NIR_MEMORY_RELEASE,
                        .memory_modes = nir_var_mem_ssbo | nir_var_shader_out | nir_var_mem_global | nir_var_image);
      }
   }
}

static void
ngg_gs_setup_vertex_compaction(nir_builder *b, nir_def *vertex_live, nir_def *tid_in_tg,
                               nir_def *exporter_tid_in_tg, lower_ngg_gs_state *s)
{
   assert(vertex_live->bit_size == 1);
   nir_if *if_vertex_live = nir_push_if(b, vertex_live);
   {
      /* Setup the vertex compaction.
       * Save the current thread's id for the thread which will export the current vertex.
       * We reuse stream 1 of the primitive flag of the other thread's vertex for storing this.
       */

      nir_def *exporter_lds_addr = ngg_gs_out_vertex_addr(b, exporter_tid_in_tg, s);
      nir_def *tid_in_tg_u8 = nir_u2u8(b, tid_in_tg);
      nir_store_shared(b, tid_in_tg_u8, exporter_lds_addr, .base = s->lds_offs_primflags + 1);
   }
   nir_pop_if(b, if_vertex_live);
}

static nir_def *
ngg_gs_load_out_vtx_primflag(nir_builder *b, unsigned stream, nir_def *tid_in_tg,
                             nir_def *vtx_lds_addr, nir_def *max_num_out_vtx,
                             lower_ngg_gs_state *s)
{
   nir_def *zero = nir_imm_int(b, 0);

   nir_if *if_outvtx_thread = nir_push_if(b, nir_ilt(b, tid_in_tg, max_num_out_vtx));
   nir_def *primflag = nir_load_shared(b, 1, 8, vtx_lds_addr,
                                           .base = s->lds_offs_primflags + stream);
   primflag = nir_u2u32(b, primflag);
   nir_pop_if(b, if_outvtx_thread);

   return nir_if_phi(b, primflag, zero);
}

static void
ngg_gs_out_prim_all_vtxptr(nir_builder *b, nir_def *last_vtxidx, nir_def *last_vtxptr,
                           nir_def *last_vtx_primflag, lower_ngg_gs_state *s,
                           nir_def *vtxptr[3])
{
   unsigned last_vtx = s->num_vertices_per_primitive - 1;
   vtxptr[last_vtx]= last_vtxptr;

   bool primitive_is_triangle = s->num_vertices_per_primitive == 3;
   nir_def *is_odd = primitive_is_triangle ?
      nir_ubfe_imm(b, last_vtx_primflag, 1, 1) : NULL;

   for (unsigned i = 0; i < s->num_vertices_per_primitive - 1; i++) {
      nir_def *vtxidx = nir_iadd_imm(b, last_vtxidx, -(last_vtx - i));

      /* Need to swap vertex 0 and vertex 1 when vertex 2 index is odd to keep
       * CW/CCW order for correct front/back face culling.
       */
      if (primitive_is_triangle)
         vtxidx = i == 0 ? nir_iadd(b, vtxidx, is_odd) : nir_isub(b, vtxidx, is_odd);

      vtxptr[i] = ngg_gs_out_vertex_addr(b, vtxidx, s);
   }
}

static nir_def *
ngg_gs_cull_primitive(nir_builder *b, nir_def *tid_in_tg, nir_def *max_vtxcnt,
                      nir_def *out_vtx_lds_addr, nir_def *out_vtx_primflag_0,
                      lower_ngg_gs_state *s)
{
   /* we haven't enabled point culling, if enabled this function could be further optimized */
   assert(s->num_vertices_per_primitive > 1);

   /* save the primflag so that we don't need to load it from LDS again */
   nir_variable *primflag_var = nir_local_variable_create(s->impl, glsl_uint_type(), "primflag");
   nir_store_var(b, primflag_var, out_vtx_primflag_0, 1);

   /* last bit of primflag indicate if this is the final vertex of a primitive */
   nir_def *is_end_prim_vtx = nir_i2b(b, nir_iand_imm(b, out_vtx_primflag_0, 1));
   nir_def *has_output_vertex = nir_ilt(b, tid_in_tg, max_vtxcnt);
   nir_def *prim_enable = nir_iand(b, is_end_prim_vtx, has_output_vertex);

   nir_if *if_prim_enable = nir_push_if(b, prim_enable);
   {
      /* Calculate the LDS address of every vertex in the current primitive. */
      nir_def *vtxptr[3];
      ngg_gs_out_prim_all_vtxptr(b, tid_in_tg, out_vtx_lds_addr, out_vtx_primflag_0, s, vtxptr);

      /* Load the positions from LDS. */
      nir_def *pos[3][4];
      for (unsigned i = 0; i < s->num_vertices_per_primitive; i++) {
         /* VARYING_SLOT_POS == 0, so base won't count packed location */
         pos[i][3] = nir_load_shared(b, 1, 32, vtxptr[i], .base = 12); /* W */
         nir_def *xy = nir_load_shared(b, 2, 32, vtxptr[i], .base = 0, .align_mul = 4);
         pos[i][0] = nir_channel(b, xy, 0);
         pos[i][1] = nir_channel(b, xy, 1);

         pos[i][0] = nir_fdiv(b, pos[i][0], pos[i][3]);
         pos[i][1] = nir_fdiv(b, pos[i][1], pos[i][3]);
      }

      /* TODO: support clipdist culling in GS */
      nir_def *accepted_by_clipdist = nir_imm_true(b);

      nir_def *accepted = ac_nir_cull_primitive(
         b, accepted_by_clipdist, pos, s->num_vertices_per_primitive, NULL, NULL);

      nir_if *if_rejected = nir_push_if(b, nir_inot(b, accepted));
      {
         /* clear the primflag if rejected */
         nir_store_shared(b, nir_imm_zero(b, 1, 8), out_vtx_lds_addr,
                          .base = s->lds_offs_primflags);

         nir_store_var(b, primflag_var, nir_imm_int(b, 0), 1);
      }
      nir_pop_if(b, if_rejected);
   }
   nir_pop_if(b, if_prim_enable);

   /* Wait for LDS primflag access done. */
   nir_barrier(b, .execution_scope = SCOPE_WORKGROUP,
                         .memory_scope = SCOPE_WORKGROUP,
                         .memory_semantics = NIR_MEMORY_ACQ_REL,
                         .memory_modes = nir_var_mem_shared);

   /* only dead vertex need a chance to relive */
   nir_def *vtx_is_dead = nir_ieq_imm(b, nir_load_var(b, primflag_var), 0);
   nir_def *vtx_update_primflag = nir_iand(b, vtx_is_dead, has_output_vertex);
   nir_if *if_update_primflag = nir_push_if(b, vtx_update_primflag);
   {
      /* get succeeding vertices' primflag to detect this vertex's liveness */
      for (unsigned i = 1; i < s->num_vertices_per_primitive; i++) {
         nir_def *vtxidx = nir_iadd_imm(b, tid_in_tg, i);
         nir_def *not_overflow = nir_ilt(b, vtxidx, max_vtxcnt);
         nir_if *if_not_overflow = nir_push_if(b, not_overflow);
         {
            nir_def *vtxptr = ngg_gs_out_vertex_addr(b, vtxidx, s);
            nir_def *vtx_primflag =
               nir_load_shared(b, 1, 8, vtxptr, .base = s->lds_offs_primflags);
            vtx_primflag = nir_u2u32(b, vtx_primflag);

            /* if succeeding vertex is alive end of primitive vertex, need to set current
             * thread vertex's liveness flag (bit 2)
             */
            nir_def *has_prim = nir_i2b(b, nir_iand_imm(b, vtx_primflag, 1));
            nir_def *vtx_live_flag =
               nir_bcsel(b, has_prim, nir_imm_int(b, 0b100), nir_imm_int(b, 0));

            /* update this vertex's primflag */
            nir_def *primflag = nir_load_var(b, primflag_var);
            primflag = nir_ior(b, primflag, vtx_live_flag);
            nir_store_var(b, primflag_var, primflag, 1);
         }
         nir_pop_if(b, if_not_overflow);
      }
   }
   nir_pop_if(b, if_update_primflag);

   return nir_load_var(b, primflag_var);
}

static void
ngg_gs_build_streamout(nir_builder *b, lower_ngg_gs_state *s)
{
   nir_xfb_info *info = ac_nir_get_sorted_xfb_info(b->shader);

   nir_def *tid_in_tg = nir_load_local_invocation_index(b);
   nir_def *max_vtxcnt = nir_load_workgroup_num_input_vertices_amd(b);
   nir_def *out_vtx_lds_addr = ngg_gs_out_vertex_addr(b, tid_in_tg, s);
   nir_def *prim_live[4] = {0};
   nir_def *gen_prim[4] = {0};
   nir_def *export_seq[4] = {0};
   nir_def *out_vtx_primflag[4] = {0};
   for (unsigned stream = 0; stream < 4; stream++) {
      if (!(info->streams_written & BITFIELD_BIT(stream)))
         continue;

      out_vtx_primflag[stream] =
         ngg_gs_load_out_vtx_primflag(b, stream, tid_in_tg, out_vtx_lds_addr, max_vtxcnt, s);

      /* Check bit 0 of primflag for primitive alive, it's set for every last
       * vertex of a primitive.
       */
      prim_live[stream] = nir_i2b(b, nir_iand_imm(b, out_vtx_primflag[stream], 1));

      unsigned scratch_stride = ALIGN(s->max_num_waves, 4);
      nir_def *scratch_base =
         nir_iadd_imm(b, s->lds_addr_gs_scratch, stream * scratch_stride);

      /* We want to export primitives to streamout buffer in sequence,
       * but not all vertices are alive or mark end of a primitive, so
       * there're "holes". We don't need continuous invocations to write
       * primitives to streamout buffer like final vertex export, so
       * just repack to get the sequence (export_seq) is enough, no need
       * to do compaction.
       *
       * Use separate scratch space for each stream to avoid barrier.
       * TODO: we may further reduce barriers by writing to all stream
       * LDS at once, then we only need one barrier instead of one each
       * stream..
       */
      ac_nir_wg_repack_result rep = {0};
      ac_nir_repack_invocations_in_workgroup(b, &prim_live[stream], &rep, 1, scratch_base,
                                      s->max_num_waves, s->options->wave_size);

      /* nir_intrinsic_set_vertex_and_primitive_count can also get primitive count of
       * current wave, but still need LDS to sum all wave's count to get workgroup count.
       * And we need repack to export primitive to streamout buffer anyway, so do here.
       */
      gen_prim[stream] = rep.num_repacked_invocations;
      export_seq[stream] = rep.repacked_invocation_index;
   }

   /* Workgroup barrier: wait for LDS scratch reads finish. */
   nir_barrier(b, .execution_scope = SCOPE_WORKGROUP,
                      .memory_scope = SCOPE_WORKGROUP,
                      .memory_semantics = NIR_MEMORY_ACQ_REL,
                      .memory_modes = nir_var_mem_shared);

   /* Get global buffer offset where this workgroup will stream out data to. */
   nir_def *emit_prim[4] = {0};
   nir_def *buffer_offsets[4] = {0};
   nir_def *so_buffer[4] = {0};
   ac_nir_ngg_build_streamout_buffer_info(b, info, s->options->hw_info->gfx_level, s->options->has_xfb_prim_query,
                                   s->options->use_gfx12_xfb_intrinsic, s->lds_addr_gs_scratch, tid_in_tg,
                                   gen_prim, so_buffer, buffer_offsets, emit_prim);

   for (unsigned stream = 0; stream < 4; stream++) {
      if (!(info->streams_written & BITFIELD_BIT(stream)))
         continue;

      nir_def *can_emit = nir_ilt(b, export_seq[stream], emit_prim[stream]);
      nir_if *if_emit = nir_push_if(b, nir_iand(b, can_emit, prim_live[stream]));
      {
         /* Get streamout buffer vertex index for the first vertex of this primitive. */
         nir_def *first_vertex_idx =
            nir_imul_imm(b, export_seq[stream], s->num_vertices_per_primitive);
         nir_def *stream_buffer_offsets[NIR_MAX_XFB_BUFFERS];

         u_foreach_bit(buffer, info->buffers_written) {
            stream_buffer_offsets[buffer] = nir_iadd(b, buffer_offsets[buffer],
                                                     nir_imul_imm(b, first_vertex_idx,
                                                                  info->buffers[buffer].stride));
         }

         /* Get all vertices' lds address of this primitive. */
         nir_def *exported_vtx_lds_addr[3];
         ngg_gs_out_prim_all_vtxptr(b, tid_in_tg, out_vtx_lds_addr,
                                    out_vtx_primflag[stream], s,
                                    exported_vtx_lds_addr);

         /* Write all vertices of this primitive to streamout buffer. */
         for (unsigned i = 0; i < s->num_vertices_per_primitive; i++) {
            ac_nir_ngg_build_streamout_vertex(b, info, stream, so_buffer,
                                       stream_buffer_offsets, i,
                                       exported_vtx_lds_addr[i],
                                       &s->out, false);
         }
      }
      nir_pop_if(b, if_emit);
   }
}

static void
ngg_gs_finale(nir_builder *b, lower_ngg_gs_state *s)
{
   nir_def *tid_in_tg = nir_load_local_invocation_index(b);
   nir_def *max_vtxcnt = nir_load_workgroup_num_input_vertices_amd(b);
   nir_def *max_prmcnt = max_vtxcnt; /* They are currently practically the same; both RADV and RadeonSI do this. */
   nir_def *out_vtx_lds_addr = ngg_gs_out_vertex_addr(b, tid_in_tg, s);

   if (s->output_compile_time_known) {
      /* When the output is compile-time known, the GS writes all possible vertices and primitives it can.
       * The gs_alloc_req needs to happen on one wave only, otherwise the HW hangs.
       */
      nir_if *if_wave_0 = nir_push_if(b, nir_ieq_imm(b, nir_load_subgroup_id(b), 0));
      {
         /* When the GS outputs 0 vertices, make the vertex and primitive count compile-time zero. */
         if (b->shader->info.gs.vertices_out == 0)
            max_vtxcnt = max_prmcnt = nir_imm_int(b, 0);

         ac_nir_ngg_alloc_vertices_and_primitives(b, max_vtxcnt, max_prmcnt,
                                                  b->shader->info.gs.vertices_out == 0 &&
                                                  s->options->hw_info->has_ngg_fully_culled_bug);
      }
      nir_pop_if(b, if_wave_0);
   }

   /* Workgroup barrier already emitted, we can assume all GS output stores are done by now. */

   nir_def *out_vtx_primflag_0 = ngg_gs_load_out_vtx_primflag(b, 0, tid_in_tg, out_vtx_lds_addr, max_vtxcnt, s);

   if (s->output_compile_time_known && b->shader->info.gs.vertices_out) {
      ngg_gs_emit_output(b, max_vtxcnt, max_prmcnt, tid_in_tg, out_vtx_lds_addr, tid_in_tg, out_vtx_primflag_0, s);
      return;
   }

   /* cull primitives */
   if (s->options->can_cull) {
      nir_if *if_cull_en = nir_push_if(b, nir_load_cull_any_enabled_amd(b));

      /* culling code will update the primflag */
      nir_def *updated_primflag =
         ngg_gs_cull_primitive(b, tid_in_tg, max_vtxcnt, out_vtx_lds_addr,
                               out_vtx_primflag_0, s);

      nir_pop_if(b, if_cull_en);

      out_vtx_primflag_0 = nir_if_phi(b, updated_primflag, out_vtx_primflag_0);
   }

   /* When the output vertex count is not known at compile time:
    * There may be gaps between invocations that have live vertices, but NGG hardware
    * requires that the invocations that export vertices are packed (ie. compact).
    * To ensure this, we need to repack invocations that have a live vertex.
    */
   nir_def *vertex_live = nir_ine_imm(b, out_vtx_primflag_0, 0);
   ac_nir_wg_repack_result rep = {0};

   ac_nir_repack_invocations_in_workgroup(b, &vertex_live, &rep, 1, s->lds_addr_gs_scratch,
                                   s->max_num_waves, s->options->wave_size);

   nir_def *workgroup_num_vertices = rep.num_repacked_invocations;
   nir_def *exporter_tid_in_tg = rep.repacked_invocation_index;

   /* When the workgroup emits 0 total vertices, we also must export 0 primitives (otherwise the HW can hang). */
   nir_def *any_output = nir_ine_imm(b, workgroup_num_vertices, 0);
   max_prmcnt = nir_bcsel(b, any_output, max_prmcnt, nir_imm_int(b, 0));

   /* Allocate export space. We currently don't compact primitives, just use the maximum number. */
   nir_if *if_wave_0 = nir_push_if(b, nir_ieq_imm(b, nir_load_subgroup_id(b), 0));
   {
      ac_nir_ngg_alloc_vertices_and_primitives(b, workgroup_num_vertices, max_prmcnt, s->options->hw_info->has_ngg_fully_culled_bug);
   }
   nir_pop_if(b, if_wave_0);

   /* Vertex compaction. This makes sure there are no gaps between threads that export vertices. */
   ngg_gs_setup_vertex_compaction(b, vertex_live, tid_in_tg, exporter_tid_in_tg, s);

   /* Workgroup barrier: wait for all LDS stores to finish. */
   nir_barrier(b, .execution_scope=SCOPE_WORKGROUP, .memory_scope=SCOPE_WORKGROUP,
                        .memory_semantics=NIR_MEMORY_ACQ_REL, .memory_modes=nir_var_mem_shared);

   ngg_gs_emit_output(b, workgroup_num_vertices, max_prmcnt, tid_in_tg, out_vtx_lds_addr, exporter_tid_in_tg, out_vtx_primflag_0, s);
}

void
ac_nir_lower_ngg_gs(nir_shader *shader, const ac_nir_lower_ngg_options *options)
{
   nir_function_impl *impl = nir_shader_get_entrypoint(shader);
   assert(impl);

   lower_ngg_gs_state state = {
      .options = options,
      .impl = impl,
      .max_num_waves = DIV_ROUND_UP(options->max_workgroup_size, options->wave_size),
      .lds_offs_primflags = options->gs_out_vtx_bytes,
      .lds_bytes_per_gs_out_vertex = options->gs_out_vtx_bytes + 4u,
      .streamout_enabled = shader->xfb_info && !options->disable_streamout,
   };

   if (!options->can_cull) {
      nir_gs_count_vertices_and_primitives(shader, state.const_out_vtxcnt,
                                           state.const_out_prmcnt, NULL, 4u);
      state.output_compile_time_known = false;
   }

   if (shader->info.gs.output_primitive == MESA_PRIM_POINTS)
      state.num_vertices_per_primitive = 1;
   else if (shader->info.gs.output_primitive == MESA_PRIM_LINE_STRIP)
      state.num_vertices_per_primitive = 2;
   else if (shader->info.gs.output_primitive == MESA_PRIM_TRIANGLE_STRIP)
      state.num_vertices_per_primitive = 3;
   else
      unreachable("Invalid GS output primitive.");

   /* Extract the full control flow. It is going to be wrapped in an if statement. */
   nir_cf_list extracted;
   nir_cf_extract(&extracted, nir_before_impl(impl),
                  nir_after_impl(impl));

   nir_builder builder = nir_builder_at(nir_before_impl(impl));
   nir_builder *b = &builder; /* This is to avoid the & */

   /* Workgroup barrier: wait for ES threads */
   nir_barrier(b, .execution_scope=SCOPE_WORKGROUP, .memory_scope=SCOPE_WORKGROUP,
                         .memory_semantics=NIR_MEMORY_ACQ_REL, .memory_modes=nir_var_mem_shared);

   state.lds_addr_gs_out_vtx = nir_load_lds_ngg_gs_out_vertex_base_amd(b);
   state.lds_addr_gs_scratch = nir_load_lds_ngg_scratch_base_amd(b);

   /* Wrap the GS control flow. */
   nir_if *if_gs_thread = nir_push_if(b, nir_is_subgroup_invocation_lt_amd(b, nir_load_merged_wave_info_amd(b), .base = 8));

   nir_cf_reinsert(&extracted, b->cursor);
   b->cursor = nir_after_cf_list(&if_gs_thread->then_list);
   nir_pop_if(b, if_gs_thread);

   /* Workgroup barrier: wait for all GS threads to finish */
   nir_barrier(b, .execution_scope=SCOPE_WORKGROUP, .memory_scope=SCOPE_WORKGROUP,
                         .memory_semantics=NIR_MEMORY_ACQ_REL, .memory_modes=nir_var_mem_shared);

   if (state.streamout_enabled)
      ngg_gs_build_streamout(b, &state);

   /* Lower the GS intrinsics */
   lower_ngg_gs_intrinsics(shader, &state);

   if (!state.vertex_count[0]) {
      fprintf(stderr, "Could not find set_vertex_and_primitive_count for stream 0. This would hang your GPU.");
      abort();
   }

   /* Emit shader queries */
   b->cursor = nir_after_cf_list(&if_gs_thread->then_list);
   ac_nir_gs_shader_query(b,
                          state.options->has_gen_prim_query,
                          state.options->has_gs_invocations_query,
                          state.options->has_gs_primitives_query,
                          state.num_vertices_per_primitive,
                          state.options->wave_size,
                          state.vertex_count,
                          state.primitive_count);

   b->cursor = nir_after_impl(impl);

   /* Emit the finale sequence */
   ngg_gs_finale(b, &state);
   nir_validate_shader(shader, "after emitting NGG GS");

   /* Cleanup */
   nir_lower_vars_to_ssa(shader);
   nir_remove_dead_variables(shader, nir_var_function_temp, NULL);
   nir_metadata_preserve(impl, nir_metadata_none);
}
