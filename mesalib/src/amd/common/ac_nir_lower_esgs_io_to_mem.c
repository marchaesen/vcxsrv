/*
 * Copyright © 2021 Valve Corporation
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

#include "ac_nir.h"
#include "nir_builder.h"

/*
 * Lower NIR cross-stage I/O intrinsics into the memory accesses that actually happen on the HW.
 *
 * These HW stages are used only when a Geometry Shader is used.
 * Export Shader (ES) runs the SW stage before GS, can be either VS or TES.
 *
 * * GFX6-8:
 *   ES and GS are separate HW stages.
 *   I/O is passed between them through VRAM.
 * * GFX9+:
 *   ES and GS are merged into a single HW stage.
 *   I/O is passed between them through LDS.
 *
 */

typedef struct {
   /* Which hardware generation we're dealing with */
   enum chip_class chip_class;

   /* Number of ES outputs for which memory should be reserved.
    * When compacted, this should be the number of linked ES outputs.
    */
   unsigned num_reserved_es_outputs;
} lower_esgs_io_state;

static nir_ssa_def *
emit_split_buffer_load(nir_builder *b, nir_ssa_def *desc, nir_ssa_def *v_off, nir_ssa_def *s_off,
                       unsigned component_stride, unsigned num_components, unsigned bit_size)
{
   unsigned total_bytes = num_components * bit_size / 8u;
   unsigned full_dwords = total_bytes / 4u;
   unsigned remaining_bytes = total_bytes - full_dwords * 4u;

   /* Accomodate max number of split 64-bit loads */
   nir_ssa_def *comps[NIR_MAX_VEC_COMPONENTS * 2u];

   /* Assume that 1x32-bit load is better than 1x16-bit + 1x8-bit */
   if (remaining_bytes == 3) {
      remaining_bytes = 0;
      full_dwords++;
   }

   for (unsigned i = 0; i < full_dwords; ++i)
      comps[i] = nir_build_load_buffer_amd(b, 1, 32, desc, v_off, s_off,
                                           .base = component_stride * i, .memory_modes = nir_var_shader_in);

   if (remaining_bytes)
      comps[full_dwords] = nir_build_load_buffer_amd(b, 1, remaining_bytes * 8, desc, v_off, s_off,
                                                     .base = component_stride * full_dwords, .memory_modes = nir_var_shader_in);

   return nir_extract_bits(b, comps, full_dwords + !!remaining_bytes, 0, num_components, bit_size);
}

static void
emit_split_buffer_store(nir_builder *b, nir_ssa_def *d, nir_ssa_def *desc, nir_ssa_def *v_off, nir_ssa_def *s_off,
                        unsigned component_stride, unsigned num_components, unsigned bit_size,
                        unsigned writemask, bool swizzled, bool slc)
{
   while (writemask) {
      int start, count;
      u_bit_scan_consecutive_range(&writemask, &start, &count);
      assert(start >= 0 && count >= 0);

      unsigned bytes = count * bit_size / 8u;
      unsigned start_byte = start * bit_size / 8u;

      while (bytes) {
         unsigned store_bytes = MIN2(bytes, 4u);
         if ((start_byte % 4) == 1 || (start_byte % 4) == 3)
            store_bytes = MIN2(store_bytes, 1);
         else if ((start_byte % 4) == 2)
            store_bytes = MIN2(store_bytes, 2);

         nir_ssa_def *store_val = nir_extract_bits(b, &d, 1, start_byte * 8u, 1, store_bytes * 8u);
         nir_build_store_buffer_amd(b, store_val, desc, v_off, s_off, .is_swizzled = swizzled, .slc_amd = slc,
                                    .base = start_byte, .memory_modes = nir_var_shader_out);

         start_byte += store_bytes;
         bytes -= store_bytes;
      }
   }
}

static bool
lower_es_output_store(nir_builder *b,
                      nir_instr *instr,
                      void *state)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

   if (intrin->intrinsic != nir_intrinsic_store_output)
      return false;

   lower_esgs_io_state *st = (lower_esgs_io_state *) state;
   unsigned write_mask = nir_intrinsic_write_mask(intrin);

   b->cursor = nir_before_instr(instr);
   nir_ssa_def *io_off = nir_build_calc_io_offset(b, intrin, nir_imm_int(b, 16u), 4u);

   if (st->chip_class <= GFX8) {
      /* GFX6-8: ES is a separate HW stage, data is passed from ES to GS in VRAM. */
      nir_ssa_def *ring = nir_build_load_ring_esgs_amd(b);
      nir_ssa_def *es2gs_off = nir_build_load_ring_es2gs_offset_amd(b);
      emit_split_buffer_store(b, intrin->src[0].ssa, ring, io_off, es2gs_off, 4u,
                              intrin->src[0].ssa->num_components, intrin->src[0].ssa->bit_size,
                              write_mask, true, true);
   } else {
      /* GFX9+: ES is merged into GS, data is passed through LDS. */
      unsigned esgs_itemsize = st->num_reserved_es_outputs * 16u;
      nir_ssa_def *vertex_idx = nir_build_load_local_invocation_index(b);
      nir_ssa_def *off = nir_iadd(b, nir_imul_imm(b, vertex_idx, esgs_itemsize), io_off);
      nir_build_store_shared(b, intrin->src[0].ssa, off, .write_mask = write_mask,
                             .align_mul = 16u, .align_offset = (nir_intrinsic_component(intrin) * 4u) % 16u);
   }

   nir_instr_remove(instr);
   return true;
}

static nir_ssa_def *
gs_per_vertex_input_vertex_offset_gfx6(nir_builder *b, nir_src *vertex_src)
{
   if (nir_src_is_const(*vertex_src))
      return nir_build_load_gs_vertex_offset_amd(b, .base = nir_src_as_uint(*vertex_src));

   nir_ssa_def *vertex_offset = nir_build_load_gs_vertex_offset_amd(b, .base = 0);

   for (unsigned i = 1; i < b->shader->info.gs.vertices_in; ++i) {
      nir_ssa_def *cond = nir_ieq_imm(b, vertex_src->ssa, i);
      nir_ssa_def *elem = nir_build_load_gs_vertex_offset_amd(b, .base = i);
      vertex_offset = nir_bcsel(b, cond, elem, vertex_offset);
   }

   return vertex_offset;
}

static nir_ssa_def *
gs_per_vertex_input_vertex_offset_gfx9(nir_builder *b, nir_src *vertex_src)
{
   if (nir_src_is_const(*vertex_src)) {
      unsigned vertex = nir_src_as_uint(*vertex_src);
      return nir_ubfe(b, nir_build_load_gs_vertex_offset_amd(b, .base = vertex / 2u),
                      nir_imm_int(b, (vertex & 1u) * 16u), nir_imm_int(b, 16u));
   }

   nir_ssa_def *vertex_offset = nir_build_load_gs_vertex_offset_amd(b, .base = 0);

   for (unsigned i = 1; i < b->shader->info.gs.vertices_in; i++) {
      nir_ssa_def *cond = nir_ieq_imm(b, vertex_src->ssa, i);
      nir_ssa_def *elem = nir_build_load_gs_vertex_offset_amd(b, .base = i / 2u * 2u);
      if (i % 2u)
         elem = nir_ishr_imm(b, elem, 16u);

      vertex_offset = nir_bcsel(b, cond, elem, vertex_offset);
   }

   return nir_iand_imm(b, vertex_offset, 0xffffu);
}

static nir_ssa_def *
gs_per_vertex_input_offset(nir_builder *b,
                           lower_esgs_io_state *st,
                           nir_intrinsic_instr *instr)
{
   nir_src *vertex_src = nir_get_io_arrayed_index_src(instr);
   nir_ssa_def *vertex_offset = st->chip_class >= GFX9
                                ? gs_per_vertex_input_vertex_offset_gfx9(b, vertex_src)
                                : gs_per_vertex_input_vertex_offset_gfx6(b, vertex_src);

   unsigned base_stride = st->chip_class >= GFX9 ? 1 : 64 /* Wave size on GFX6-8 */;
   nir_ssa_def *io_off = nir_build_calc_io_offset(b, instr, nir_imm_int(b, base_stride * 4u), base_stride);
   nir_ssa_def *off = nir_iadd(b, io_off, vertex_offset);
   return nir_imul_imm(b, off, 4u);
}

static nir_ssa_def *
lower_gs_per_vertex_input_load(nir_builder *b,
                               nir_instr *instr,
                               void *state)
{
   lower_esgs_io_state *st = (lower_esgs_io_state *) state;
   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
   nir_ssa_def *off = gs_per_vertex_input_offset(b, st, intrin);

   if (st->chip_class >= GFX9)
      return nir_build_load_shared(b, intrin->dest.ssa.num_components, intrin->dest.ssa.bit_size, off,
                                   .align_mul = 16u, .align_offset = (nir_intrinsic_component(intrin) * 4u) % 16u);

   unsigned wave_size = 64u; /* GFX6-8 only support wave64 */
   nir_ssa_def *ring = nir_build_load_ring_esgs_amd(b);
   return emit_split_buffer_load(b, ring, off, nir_imm_zero(b, 1, 32), 4u * wave_size,
                                 intrin->dest.ssa.num_components, intrin->dest.ssa.bit_size);
}

static bool
filter_load_per_vertex_input(const nir_instr *instr, UNUSED const void *state)
{
   return instr->type == nir_instr_type_intrinsic && nir_instr_as_intrinsic(instr)->intrinsic == nir_intrinsic_load_per_vertex_input;
}

void
ac_nir_lower_es_outputs_to_mem(nir_shader *shader,
                               enum chip_class chip_class,
                               unsigned num_reserved_es_outputs)
{
   lower_esgs_io_state state = {
      .chip_class = chip_class,
      .num_reserved_es_outputs = num_reserved_es_outputs,
   };

   nir_shader_instructions_pass(shader,
                                lower_es_output_store,
                                nir_metadata_block_index | nir_metadata_dominance,
                                &state);
}

void
ac_nir_lower_gs_inputs_to_mem(nir_shader *shader,
                              enum chip_class chip_class,
                              unsigned num_reserved_es_outputs)
{
   lower_esgs_io_state state = {
      .chip_class = chip_class,
      .num_reserved_es_outputs = num_reserved_es_outputs,
   };

   nir_shader_lower_instructions(shader,
                                 filter_load_per_vertex_input,
                                 lower_gs_per_vertex_input_load,
                                 &state);
}
