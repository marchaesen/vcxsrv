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
#include "nir.h"
#include "nir_builder.h"

static nir_ssa_def *
try_extract_additions(nir_builder *b, nir_ssa_scalar scalar, uint64_t *out_const,
                      nir_ssa_def **out_offset)
{
   if (!nir_ssa_scalar_is_alu(scalar) || nir_ssa_scalar_alu_op(scalar) != nir_op_iadd)
      return NULL;

   nir_alu_instr *alu = nir_instr_as_alu(scalar.def->parent_instr);
   nir_ssa_scalar src0 = nir_ssa_scalar_chase_alu_src(scalar, 0);
   nir_ssa_scalar src1 = nir_ssa_scalar_chase_alu_src(scalar, 1);

   for (unsigned i = 0; i < 2; ++i) {
      nir_ssa_scalar src = i ? src1 : src0;
      if (nir_ssa_scalar_is_const(src)) {
         *out_const += nir_ssa_scalar_as_uint(src);
      } else if (nir_ssa_scalar_is_alu(src) && nir_ssa_scalar_alu_op(src) == nir_op_u2u64) {
         nir_ssa_scalar offset_scalar = nir_ssa_scalar_chase_alu_src(src, 0);
         nir_ssa_def *offset = nir_channel(b, offset_scalar.def, offset_scalar.comp);
         if (*out_offset)
            *out_offset = nir_iadd(b, *out_offset, offset);
         else
            *out_offset = offset;
      } else {
         continue;
      }

      nir_ssa_def *replace_src =
         try_extract_additions(b, i == 1 ? src0 : src1, out_const, out_offset);
      return replace_src ? replace_src : nir_ssa_for_alu_src(b, alu, 1 - i);
   }

   nir_ssa_def *replace_src0 = try_extract_additions(b, src0, out_const, out_offset);
   nir_ssa_def *replace_src1 = try_extract_additions(b, src1, out_const, out_offset);
   if (!replace_src0 && !replace_src1)
      return NULL;

   replace_src0 = replace_src0 ? replace_src0 : nir_channel(b, src0.def, src0.comp);
   replace_src1 = replace_src1 ? replace_src1 : nir_channel(b, src1.def, src1.comp);
   return nir_iadd(b, replace_src0, replace_src1);
}

static bool
process_instr(nir_builder *b, nir_instr *instr, void *_)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

   nir_intrinsic_op op;
   switch (intrin->intrinsic) {
   case nir_intrinsic_load_global:
   case nir_intrinsic_load_global_constant:
      op = nir_intrinsic_load_global_amd;
      break;
   case nir_intrinsic_global_atomic_add:
      op = nir_intrinsic_global_atomic_add_amd;
      break;
   case nir_intrinsic_global_atomic_imin:
      op = nir_intrinsic_global_atomic_imin_amd;
      break;
   case nir_intrinsic_global_atomic_umin:
      op = nir_intrinsic_global_atomic_umin_amd;
      break;
   case nir_intrinsic_global_atomic_imax:
      op = nir_intrinsic_global_atomic_imax_amd;
      break;
   case nir_intrinsic_global_atomic_umax:
      op = nir_intrinsic_global_atomic_umax_amd;
      break;
   case nir_intrinsic_global_atomic_and:
      op = nir_intrinsic_global_atomic_and_amd;
      break;
   case nir_intrinsic_global_atomic_or:
      op = nir_intrinsic_global_atomic_or_amd;
      break;
   case nir_intrinsic_global_atomic_xor:
      op = nir_intrinsic_global_atomic_xor_amd;
      break;
   case nir_intrinsic_global_atomic_exchange:
      op = nir_intrinsic_global_atomic_exchange_amd;
      break;
   case nir_intrinsic_global_atomic_fadd:
      op = nir_intrinsic_global_atomic_fadd_amd;
      break;
   case nir_intrinsic_global_atomic_fmin:
      op = nir_intrinsic_global_atomic_fmin_amd;
      break;
   case nir_intrinsic_global_atomic_fmax:
      op = nir_intrinsic_global_atomic_fmax_amd;
      break;
   case nir_intrinsic_global_atomic_comp_swap:
      op = nir_intrinsic_global_atomic_comp_swap_amd;
      break;
   case nir_intrinsic_global_atomic_fcomp_swap:
      op = nir_intrinsic_global_atomic_fcomp_swap_amd;
      break;
   case nir_intrinsic_store_global:
      op = nir_intrinsic_store_global_amd;
      break;
   default:
      return false;
   }
   unsigned addr_src_idx = op == nir_intrinsic_store_global_amd ? 1 : 0;

   nir_src *addr_src = &intrin->src[addr_src_idx];

   uint64_t off_const = 0;
   nir_ssa_def *offset = NULL;
   nir_ssa_scalar src = {addr_src->ssa, 0};
   b->cursor = nir_after_instr(addr_src->ssa->parent_instr);
   nir_ssa_def *addr = try_extract_additions(b, src, &off_const, &offset);
   addr = addr ? addr : addr_src->ssa;

   b->cursor = nir_before_instr(&intrin->instr);

   if (off_const > UINT32_MAX) {
      addr = nir_iadd_imm(b, addr, off_const);
      off_const = 0;
   }

   nir_intrinsic_instr *new_intrin = nir_intrinsic_instr_create(b->shader, op);

   new_intrin->num_components = intrin->num_components;

   if (op != nir_intrinsic_store_global_amd)
      nir_ssa_dest_init(&new_intrin->instr, &new_intrin->dest, intrin->dest.ssa.num_components,
                        intrin->dest.ssa.bit_size, NULL);

   unsigned num_src = nir_intrinsic_infos[intrin->intrinsic].num_srcs;
   for (unsigned i = 0; i < num_src; i++)
      new_intrin->src[i] = nir_src_for_ssa(intrin->src[i].ssa);
   new_intrin->src[num_src] = nir_src_for_ssa(offset ? offset : nir_imm_zero(b, 1, 32));
   new_intrin->src[addr_src_idx] = nir_src_for_ssa(addr);

   if (nir_intrinsic_has_access(intrin))
      nir_intrinsic_set_access(new_intrin, nir_intrinsic_access(intrin));
   if (nir_intrinsic_has_align_mul(intrin))
      nir_intrinsic_set_align_mul(new_intrin, nir_intrinsic_align_mul(intrin));
   if (nir_intrinsic_has_align_offset(intrin))
      nir_intrinsic_set_align_offset(new_intrin, nir_intrinsic_align_offset(intrin));
   if (nir_intrinsic_has_write_mask(intrin))
      nir_intrinsic_set_write_mask(new_intrin, nir_intrinsic_write_mask(intrin));
   nir_intrinsic_set_base(new_intrin, off_const);

   nir_builder_instr_insert(b, &new_intrin->instr);
   if (op != nir_intrinsic_store_global_amd)
      nir_ssa_def_rewrite_uses(&intrin->dest.ssa, &new_intrin->dest.ssa);
   nir_instr_remove(&intrin->instr);

   return true;
}

bool
ac_nir_lower_global_access(nir_shader *shader)
{
   return nir_shader_instructions_pass(shader, process_instr,
                                       nir_metadata_block_index | nir_metadata_dominance, NULL);
}
