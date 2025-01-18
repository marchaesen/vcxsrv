/*
 * Copyright © 2018-2019 Igalia S.L.
 * SPDX-License-Identifier: MIT
 */

#include "compiler/nir/nir_builder.h"
#include "ir3_nir.h"

/**
 * This pass moves to NIR certain offset computations for different I/O
 * ops that are currently implemented on the IR3 backend compiler, to
 * give NIR a chance to optimize them:
 *
 * - Dword-offset for SSBO load, store and atomics: A new, similar intrinsic
 *   is emitted that replaces the original one, adding a new source that
 *   holds the result of the original byte-offset source divided by 4.
 */

/* Returns the ir3-specific intrinsic opcode corresponding to an SSBO
 * instruction that is handled by this pass. It also conveniently returns
 * the offset source index in @offset_src_idx.
 *
 * If @intrinsic is not SSBO, or it is not handled by the pass, -1 is
 * returned.
 */
static int
get_ir3_intrinsic_for_ssbo_intrinsic(unsigned intrinsic,
                                     uint8_t *offset_src_idx)
{
   assert(offset_src_idx);

   *offset_src_idx = 1;

   switch (intrinsic) {
   case nir_intrinsic_store_ssbo:
      *offset_src_idx = 2;
      return nir_intrinsic_store_ssbo_ir3;
   case nir_intrinsic_load_ssbo:
      return nir_intrinsic_load_ssbo_ir3;
   case nir_intrinsic_ssbo_atomic:
      return nir_intrinsic_ssbo_atomic_ir3;
   case nir_intrinsic_ssbo_atomic_swap:
      return nir_intrinsic_ssbo_atomic_swap_ir3;
   default:
      break;
   }

   return -1;
}

static nir_def *
check_and_propagate_bit_shift32(nir_builder *b, nir_alu_instr *alu_instr,
                                int32_t direction, int32_t shift)
{
   nir_def *shift_ssa = alu_instr->src[1].src.ssa;

   /* Only propagate if the shift is a const value so we can check value range
    * statically.
    */
   nir_const_value *const_val = nir_src_as_const_value(alu_instr->src[1].src);
   if (!const_val)
      return NULL;

   int32_t current_shift = const_val[0].i32 * direction;
   int32_t new_shift = current_shift + shift;

   /* If the merge would reverse the direction, bail out.
    * e.g, 'x << 2' then 'x >> 4' is not 'x >> 2'.
    */
   if (current_shift * new_shift < 0)
      return NULL;

   /* If the propagation would overflow an int32_t, bail out too to be on the
    * safe side.
    */
   if (new_shift < -31 || new_shift > 31)
      return NULL;

   /* Add or substract shift depending on the final direction (SHR vs. SHL). */
   if (shift * direction < 0)
      shift_ssa = nir_iadd_imm(b, shift_ssa, -abs(shift));
   else
      shift_ssa = nir_iadd_imm(b, shift_ssa, abs(shift));

   return shift_ssa;
}

nir_def *
ir3_nir_try_propagate_bit_shift(nir_builder *b, nir_def *offset,
                                int32_t shift)
{
   nir_instr *offset_instr = offset->parent_instr;
   if (offset_instr->type != nir_instr_type_alu)
      return NULL;

   nir_alu_instr *alu = nir_instr_as_alu(offset_instr);
   nir_def *shift_ssa;
   nir_def *new_offset = NULL;

   /* the first src could be something like ssa_18.x, but we only want
    * the single component.  Otherwise the ishl/ishr/ushr could turn
    * into a vec4 operation:
    */
   nir_def *src0 = nir_mov_alu(b, alu->src[0], 1);

   switch (alu->op) {
   case nir_op_ishl:
      shift_ssa = check_and_propagate_bit_shift32(b, alu, 1, shift);
      if (shift_ssa)
         new_offset = nir_ishl(b, src0, shift_ssa);
      break;
   case nir_op_ishr:
      shift_ssa = check_and_propagate_bit_shift32(b, alu, -1, shift);
      if (shift_ssa)
         new_offset = nir_ishr(b, src0, shift_ssa);
      break;
   case nir_op_ushr:
      shift_ssa = check_and_propagate_bit_shift32(b, alu, -1, shift);
      if (shift_ssa)
         new_offset = nir_ushr(b, src0, shift_ssa);
      break;
   default:
      return NULL;
   }

   return new_offset;
}

static nir_def *
create_shift(nir_builder *b, nir_def *offset, int shift)
{
   /* If the offset to be shifted has the form "iadd constant, foo" don't shift
    * the result but transform it to "iadd constant>>shift, (ushr foo, shift)".
    * This ensures nir_opt_offsets (which only looks for iadds) can fold the
    * constant into the immediate offset.
    */
   if (offset->parent_instr->type == nir_instr_type_alu) {
      nir_alu_instr *offset_instr = nir_instr_as_alu(offset->parent_instr);

      if (offset_instr->op == nir_op_iadd &&
          nir_src_is_const(offset_instr->src[0].src)) {
         nir_def *new_shift = ir3_nir_try_propagate_bit_shift(
            b, offset_instr->src[1].src.ssa, -shift);

         if (!new_shift)
            new_shift = nir_ushr_imm(b, offset_instr->src[1].src.ssa, shift);

         return nir_iadd_imm(
            b, new_shift,
            nir_src_as_const_value(offset_instr->src[0].src)->u32 >> shift);
      }
   }

   return nir_ushr_imm(b, offset, shift);
}

static bool
lower_offset_for_ssbo(nir_intrinsic_instr *intrinsic, nir_builder *b,
                      unsigned ir3_ssbo_opcode, uint8_t offset_src_idx)
{
   unsigned num_srcs = nir_intrinsic_infos[intrinsic->intrinsic].num_srcs;
   int shift = 2;

   bool has_dest = nir_intrinsic_infos[intrinsic->intrinsic].has_dest;
   nir_def *new_dest = NULL;

   /* for 16-bit ssbo access, offset is in 16-bit words instead of dwords */
   if ((has_dest && intrinsic->def.bit_size == 16) ||
       (!has_dest && intrinsic->src[0].ssa->bit_size == 16))
      shift = 1;

   /* for 8-bit ssbo access, offset is in 8-bit words instead of dwords */
   if ((has_dest && intrinsic->def.bit_size == 8) ||
       (!has_dest && intrinsic->src[0].ssa->bit_size == 8))
      shift = 0;

   if ((has_dest && intrinsic->def.bit_size == 64) ||
       (!has_dest && intrinsic->src[0].ssa->bit_size == 64)) {
      shift = 1;
   }

   /* Here we create a new intrinsic and copy over all contents from the old
    * one. */

   nir_intrinsic_instr *new_intrinsic;
   nir_src *target_src;

   b->cursor = nir_before_instr(&intrinsic->instr);

   /* 'offset_src_idx' holds the index of the source that represent the offset. */
   new_intrinsic = nir_intrinsic_instr_create(b->shader, ir3_ssbo_opcode);

   nir_def *offset = intrinsic->src[offset_src_idx].ssa;

   /* Since we don't have value range checking, we first try to propagate
    * the division by 4 ('offset >> 2') into another bit-shift instruction that
    * possibly defines the offset. If that's the case, we emit a similar
    * instructions adjusting (merging) the shift value.
    *
    * Here we use the convention that shifting right is negative while shifting
    * left is positive. So 'x / 4' ~ 'x >> 2' or 'x << -2'.
    */
   nir_def *new_offset = ir3_nir_try_propagate_bit_shift(b, offset, -shift);

   /* The new source that will hold the dword-offset is always the last
    * one for every intrinsic.
    */
   target_src = &new_intrinsic->src[num_srcs];
   *target_src = nir_src_for_ssa(offset);

   if (has_dest) {
      nir_def *dest = &intrinsic->def;
      nir_def_init(&new_intrinsic->instr, &new_intrinsic->def,
                   dest->num_components, dest->bit_size);
      new_dest = &new_intrinsic->def;
   }

   for (unsigned i = 0; i < num_srcs; i++)
      new_intrinsic->src[i] = nir_src_for_ssa(intrinsic->src[i].ssa);

   nir_intrinsic_copy_const_indices(new_intrinsic, intrinsic);

   new_intrinsic->num_components = intrinsic->num_components;

   /* If we managed to propagate the division by 4, just use the new offset
    * register and don't emit the SHR.
    */
   if (new_offset)
      offset = new_offset;
   else
      offset = create_shift(b, offset, shift);

   /* Insert the new intrinsic right before the old one. */
   nir_builder_instr_insert(b, &new_intrinsic->instr);

   /* Replace the last source of the new intrinsic by the result of
    * the offset divided by 4.
    */
   nir_src_rewrite(target_src, offset);

   if (has_dest) {
      /* Replace the uses of the original destination by that
       * of the new intrinsic.
       */
      nir_def_rewrite_uses(&intrinsic->def, new_dest);
   }

   /* Finally remove the original intrinsic. */
   nir_instr_remove(&intrinsic->instr);

   return true;
}

static bool
lower_io_offsets_block(nir_block *block, nir_builder *b, void *mem_ctx)
{
   bool progress = false;

   nir_foreach_instr_safe (instr, block) {
      if (instr->type != nir_instr_type_intrinsic)
         continue;

      nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

      /* SSBO */
      int ir3_intrinsic;
      uint8_t offset_src_idx;
      ir3_intrinsic =
         get_ir3_intrinsic_for_ssbo_intrinsic(intr->intrinsic, &offset_src_idx);
      if (ir3_intrinsic != -1) {
         progress |= lower_offset_for_ssbo(intr, b, (unsigned)ir3_intrinsic,
                                           offset_src_idx);
      }
   }

   return progress;
}

static bool
lower_io_offsets_func(nir_function_impl *impl)
{
   void *mem_ctx = ralloc_parent(impl);
   nir_builder b = nir_builder_create(impl);

   bool progress = false;
   nir_foreach_block_safe (block, impl) {
      progress |= lower_io_offsets_block(block, &b, mem_ctx);
   }

   if (progress) {
      nir_metadata_preserve(impl,
                            nir_metadata_control_flow);
   }

   return progress;
}

bool
ir3_nir_lower_io_offsets(nir_shader *shader)
{
   bool progress = false;

   nir_foreach_function (function, shader) {
      if (function->impl)
         progress |= lower_io_offsets_func(function->impl);
   }

   return progress;
}

uint32_t
ir3_nir_max_imm_offset(nir_intrinsic_instr *intrin, const void *data)
{
   const struct ir3_compiler *compiler = data;

   if (!compiler->has_ssbo_imm_offsets)
      return 0;

   switch (intrin->intrinsic) {
   case nir_intrinsic_load_ssbo_ir3:
      if ((nir_intrinsic_access(intrin) & ACCESS_CAN_REORDER) &&
          !(compiler->options.storage_8bit && intrin->def.bit_size == 8))
         return 255; /* isam.v */
      return 127;    /* ldib.b */
   case nir_intrinsic_store_ssbo_ir3:
      return 127; /* stib.b */
   default:
      return 0;
   }
}
