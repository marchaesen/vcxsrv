/*
 * Copyright © 2021 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "ac_nir.h"
#include "ac_nir_helpers.h"

#include "nir_builder.h"

typedef struct {
   nir_def *outputs[64][4];
   nir_def *outputs_16bit_lo[16][4];
   nir_def *outputs_16bit_hi[16][4];

   ac_nir_gs_output_info *info;

   nir_def *vertex_count[4];
   nir_def *primitive_count[4];
} lower_legacy_gs_state;

static bool
lower_legacy_gs_store_output(nir_builder *b, nir_intrinsic_instr *intrin,
                             lower_legacy_gs_state *s)
{
   /* Assume:
    * - the shader used nir_lower_io_to_temporaries
    * - 64-bit outputs are lowered
    * - no indirect indexing is present
    */
   assert(nir_src_is_const(intrin->src[1]) && !nir_src_as_uint(intrin->src[1]));

   b->cursor = nir_before_instr(&intrin->instr);

   unsigned component = nir_intrinsic_component(intrin);
   unsigned write_mask = nir_intrinsic_write_mask(intrin);
   nir_io_semantics sem = nir_intrinsic_io_semantics(intrin);

   nir_def **outputs;
   if (sem.location < VARYING_SLOT_VAR0_16BIT) {
      outputs = s->outputs[sem.location];
   } else {
      unsigned index = sem.location - VARYING_SLOT_VAR0_16BIT;
      if (sem.high_16bits)
         outputs = s->outputs_16bit_hi[index];
      else
         outputs = s->outputs_16bit_lo[index];
   }

   nir_def *store_val = intrin->src[0].ssa;
   /* 64bit output has been lowered to 32bit */
   assert(store_val->bit_size <= 32);

   /* 16-bit output stored in a normal varying slot that isn't a dedicated 16-bit slot. */
   const bool non_dedicated_16bit = sem.location < VARYING_SLOT_VAR0_16BIT && store_val->bit_size == 16;

   u_foreach_bit (i, write_mask) {
      unsigned comp = component + i;
      nir_def *store_component = nir_channel(b, store_val, i);

      if (non_dedicated_16bit) {
         if (sem.high_16bits) {
            nir_def *lo = outputs[comp] ? nir_unpack_32_2x16_split_x(b, outputs[comp]) : nir_imm_intN_t(b, 0, 16);
            outputs[comp] = nir_pack_32_2x16_split(b, lo, store_component);
         } else {
            nir_def *hi = outputs[comp] ? nir_unpack_32_2x16_split_y(b, outputs[comp]) : nir_imm_intN_t(b, 0, 16);
            outputs[comp] = nir_pack_32_2x16_split(b, store_component, hi);
         }
      } else {
         outputs[comp] = store_component;
      }
   }

   nir_instr_remove(&intrin->instr);
   return true;
}

static bool
lower_legacy_gs_emit_vertex_with_counter(nir_builder *b, nir_intrinsic_instr *intrin,
                                         lower_legacy_gs_state *s)
{
   b->cursor = nir_before_instr(&intrin->instr);

   unsigned stream = nir_intrinsic_stream_id(intrin);
   nir_def *vtxidx = intrin->src[0].ssa;

   nir_def *gsvs_ring = nir_load_ring_gsvs_amd(b, .stream_id = stream);
   nir_def *soffset = nir_load_ring_gs2vs_offset_amd(b);

   unsigned offset = 0;
   u_foreach_bit64 (i, b->shader->info.outputs_written) {
      for (unsigned j = 0; j < 4; j++) {
         nir_def *output = s->outputs[i][j];
         /* Next vertex emit need a new value, reset all outputs. */
         s->outputs[i][j] = NULL;

         const uint8_t usage_mask = s->info->varying_mask[i] | s->info->sysval_mask[i];

         if (!(usage_mask & (1 << j)) ||
             ((s->info->streams[i] >> (j * 2)) & 0x3) != stream)
            continue;

         unsigned base = offset * b->shader->info.gs.vertices_out * 4;
         offset++;

         /* no one set this output, skip the buffer store */
         if (!output)
            continue;

         nir_def *voffset = nir_ishl_imm(b, vtxidx, 2);

         /* extend 8/16 bit to 32 bit, 64 bit has been lowered */
         nir_def *data = nir_u2uN(b, output, 32);

         nir_store_buffer_amd(b, data, gsvs_ring, voffset, soffset, nir_imm_int(b, 0),
                              .access = ACCESS_COHERENT | ACCESS_NON_TEMPORAL |
                                        ACCESS_IS_SWIZZLED_AMD,
                              .base = base,
                              /* For ACO to not reorder this store around EmitVertex/EndPrimitve */
                              .memory_modes = nir_var_shader_out);
      }
   }

   u_foreach_bit (i, b->shader->info.outputs_written_16bit) {
      for (unsigned j = 0; j < 4; j++) {
         nir_def *output_lo = s->outputs_16bit_lo[i][j];
         nir_def *output_hi = s->outputs_16bit_hi[i][j];
         /* Next vertex emit need a new value, reset all outputs. */
         s->outputs_16bit_lo[i][j] = NULL;
         s->outputs_16bit_hi[i][j] = NULL;

         bool has_lo_16bit = (s->info->varying_mask_16bit_lo[i] & (1 << j)) &&
            ((s->info->streams_16bit_lo[i] >> (j * 2)) & 0x3) == stream;
         bool has_hi_16bit = (s->info->varying_mask_16bit_hi[i] & (1 << j)) &&
            ((s->info->streams_16bit_hi[i] >> (j * 2)) & 0x3) == stream;
         if (!has_lo_16bit && !has_hi_16bit)
            continue;

         unsigned base = offset * b->shader->info.gs.vertices_out;
         offset++;

         bool has_lo_16bit_out = has_lo_16bit && output_lo;
         bool has_hi_16bit_out = has_hi_16bit && output_hi;

         /* no one set needed output, skip the buffer store */
         if (!has_lo_16bit_out && !has_hi_16bit_out)
            continue;

         if (!has_lo_16bit_out)
            output_lo = nir_undef(b, 1, 16);

         if (!has_hi_16bit_out)
            output_hi = nir_undef(b, 1, 16);

         nir_def *voffset = nir_iadd_imm(b, vtxidx, base);
         voffset = nir_ishl_imm(b, voffset, 2);

         nir_store_buffer_amd(b, nir_pack_32_2x16_split(b, output_lo, output_hi),
                              gsvs_ring, voffset, soffset, nir_imm_int(b, 0),
                              .access = ACCESS_COHERENT | ACCESS_NON_TEMPORAL |
                                        ACCESS_IS_SWIZZLED_AMD,
                              /* For ACO to not reorder this store around EmitVertex/EndPrimitve */
                              .memory_modes = nir_var_shader_out);
      }
   }

   /* Signal vertex emission. */
   nir_sendmsg_amd(b, nir_load_gs_wave_id_amd(b),
                   .base = AC_SENDMSG_GS_OP_EMIT | AC_SENDMSG_GS | (stream << 8));

   nir_instr_remove(&intrin->instr);
   return true;
}

static bool
lower_legacy_gs_set_vertex_and_primitive_count(nir_builder *b, nir_intrinsic_instr *intrin,
                                               lower_legacy_gs_state *s)
{
   b->cursor = nir_before_instr(&intrin->instr);

   unsigned stream = nir_intrinsic_stream_id(intrin);

   s->vertex_count[stream] = intrin->src[0].ssa;
   s->primitive_count[stream] = intrin->src[1].ssa;

   nir_instr_remove(&intrin->instr);
   return true;
}

static bool
lower_legacy_gs_end_primitive_with_counter(nir_builder *b, nir_intrinsic_instr *intrin,
                                               lower_legacy_gs_state *s)
{
   b->cursor = nir_before_instr(&intrin->instr);
   const unsigned stream = nir_intrinsic_stream_id(intrin);

   /* Signal primitive emission. */
   nir_sendmsg_amd(b, nir_load_gs_wave_id_amd(b),
                   .base = AC_SENDMSG_GS_OP_CUT | AC_SENDMSG_GS | (stream << 8));

   nir_instr_remove(&intrin->instr);
   return true;
}

static bool
lower_legacy_gs_intrinsic(nir_builder *b, nir_intrinsic_instr *intrin, void *state)
{
   lower_legacy_gs_state *s = (lower_legacy_gs_state *) state;

   if (intrin->intrinsic == nir_intrinsic_store_output)
      return lower_legacy_gs_store_output(b, intrin, s);
   else if (intrin->intrinsic == nir_intrinsic_emit_vertex_with_counter)
      return lower_legacy_gs_emit_vertex_with_counter(b, intrin, s);
   else if (intrin->intrinsic == nir_intrinsic_end_primitive_with_counter)
      return lower_legacy_gs_end_primitive_with_counter(b, intrin, s);
   else if (intrin->intrinsic == nir_intrinsic_set_vertex_and_primitive_count)
      return lower_legacy_gs_set_vertex_and_primitive_count(b, intrin, s);

   return false;
}

void
ac_nir_lower_legacy_gs(nir_shader *nir,
                       bool has_gen_prim_query,
                       bool has_pipeline_stats_query,
                       ac_nir_gs_output_info *output_info)
{
   lower_legacy_gs_state s = {
      .info = output_info,
   };

   unsigned num_vertices_per_primitive = 0;
   switch (nir->info.gs.output_primitive) {
   case MESA_PRIM_POINTS:
      num_vertices_per_primitive = 1;
      break;
   case MESA_PRIM_LINE_STRIP:
      num_vertices_per_primitive = 2;
      break;
   case MESA_PRIM_TRIANGLE_STRIP:
      num_vertices_per_primitive = 3;
      break;
   default:
      unreachable("Invalid GS output primitive.");
      break;
   }

   nir_shader_intrinsics_pass(nir, lower_legacy_gs_intrinsic,
                              nir_metadata_control_flow, &s);

   nir_function_impl *impl = nir_shader_get_entrypoint(nir);

   nir_builder builder = nir_builder_at(nir_after_impl(impl));
   nir_builder *b = &builder;

   /* Emit shader query for mix use legacy/NGG GS */
   bool progress = ac_nir_gs_shader_query(b,
                                          has_gen_prim_query,
                                          has_pipeline_stats_query,
                                          has_pipeline_stats_query,
                                          num_vertices_per_primitive,
                                          64,
                                          s.vertex_count,
                                          s.primitive_count);

   /* Wait for all stores to finish. */
   nir_barrier(b, .execution_scope = SCOPE_INVOCATION,
                      .memory_scope = SCOPE_DEVICE,
                      .memory_semantics = NIR_MEMORY_RELEASE,
                      .memory_modes = nir_var_shader_out | nir_var_mem_ssbo |
                                      nir_var_mem_global | nir_var_image);

   /* Signal that the GS is done. */
   nir_sendmsg_amd(b, nir_load_gs_wave_id_amd(b),
                   .base = AC_SENDMSG_GS_OP_NOP | AC_SENDMSG_GS_DONE);

   if (progress)
      nir_metadata_preserve(impl, nir_metadata_none);
}
