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
 * Authors:
 *    Timur Kristóf
 *
 */

#include "nir.h"
#include "nir_builder.h"

typedef struct
{
   struct hash_table *range_ht;
} opt_offsets_state;

static nir_ssa_def *
try_extract_const_addition(nir_builder *b, nir_instr *instr, opt_offsets_state *state, unsigned *out_const)
{
   if (instr->type != nir_instr_type_alu)
      return NULL;

   nir_alu_instr *alu = nir_instr_as_alu(instr);
   if (alu->op != nir_op_iadd ||
       !nir_alu_src_is_trivial_ssa(alu, 0) ||
       !nir_alu_src_is_trivial_ssa(alu, 1))
      return NULL;

   if (!alu->no_unsigned_wrap) {
      if (!state->range_ht) {
         /* Cache for nir_unsigned_upper_bound */
         state->range_ht = _mesa_pointer_hash_table_create(NULL);
      }

      /* Check if there can really be an unsigned wrap. */
      nir_ssa_scalar src0 = {alu->src[0].src.ssa, 0};
      nir_ssa_scalar src1 = {alu->src[1].src.ssa, 0};
      uint32_t ub0 = nir_unsigned_upper_bound(b->shader, state->range_ht, src0, NULL);
      uint32_t ub1 = nir_unsigned_upper_bound(b->shader, state->range_ht, src1, NULL);

      if ((UINT32_MAX - ub0) < ub1)
         return NULL;

      /* We proved that unsigned wrap won't be possible, so we can set the flag too. */
      alu->no_unsigned_wrap = true;
   }

   for (unsigned i = 0; i < 2; ++i) {
      if (nir_src_is_const(alu->src[i].src)) {
         *out_const += nir_src_as_uint(alu->src[i].src);
         return alu->src[1 - i].src.ssa;
      }

      nir_ssa_def *replace_src = try_extract_const_addition(b, alu->src[0].src.ssa->parent_instr, state, out_const);
      if (replace_src) {
         b->cursor = nir_before_instr(&alu->instr);
         return nir_iadd(b, replace_src, alu->src[1 - i].src.ssa);
      }
   }

   return NULL;
}

static bool
try_fold_load_store(nir_builder *b,
                    nir_intrinsic_instr *intrin,
                    opt_offsets_state *state,
                    unsigned offset_src_idx)
{
   /* Assume that BASE is the constant offset of a load/store.
    * Try to constant-fold additions to the offset source
    * into the actual const offset of the instruction.
    */

   unsigned off_const = nir_intrinsic_base(intrin);
   nir_src *off_src = &intrin->src[offset_src_idx];
   nir_ssa_def *replace_src = NULL;

   if (!off_src->is_ssa || off_src->ssa->bit_size != 32)
      return false;

   if (!nir_src_is_const(*off_src)) {
      nir_ssa_def *r = off_src->ssa;
      while ((r = try_extract_const_addition(b, r->parent_instr, state, &off_const)))
         replace_src = r;
   } else if (nir_src_as_uint(*off_src)) {
      off_const += nir_src_as_uint(*off_src);
      b->cursor = nir_before_instr(&intrin->instr);
      replace_src = nir_imm_zero(b, off_src->ssa->num_components, off_src->ssa->bit_size);
   }

   if (!replace_src)
      return false;

   nir_instr_rewrite_src(&intrin->instr, &intrin->src[offset_src_idx], nir_src_for_ssa(replace_src));
   nir_intrinsic_set_base(intrin, off_const);
   return true;
}

static bool
process_instr(nir_builder *b, nir_instr *instr, void *s)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   opt_offsets_state *state = (opt_offsets_state *) s;
   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

   switch (intrin->intrinsic) {
   case nir_intrinsic_load_shared:
      return try_fold_load_store(b, intrin, state, 0);
   case nir_intrinsic_store_shared:
      return try_fold_load_store(b, intrin, state, 1);
   case nir_intrinsic_load_buffer_amd:
      return try_fold_load_store(b, intrin, state, 1);
   case nir_intrinsic_store_buffer_amd:
      return try_fold_load_store(b, intrin, state, 2);
   default:
      return false;
   }

   unreachable("Can't reach here.");
}

bool
nir_opt_offsets(nir_shader *shader)
{
   opt_offsets_state state;
   state.range_ht = NULL;

   bool p = nir_shader_instructions_pass(shader, process_instr,
                                         nir_metadata_block_index |
                                         nir_metadata_dominance,
                                         &state);

   if (state.range_ht)
      _mesa_hash_table_destroy(state.range_ht, NULL);


   return p;
}
