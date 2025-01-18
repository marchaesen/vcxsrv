/*
 * Copyright 2022 Alyssa Rosenzweig
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include "compiler/nir/nir_builder.h"
#include "agx_nir.h"
#include "nir.h"
#include "nir_intrinsics.h"
#include "nir_opcodes.h"

struct match {
   nir_scalar base, offset;
   bool sign_extend;
   uint8_t shift;
};

static enum pipe_format
format_for_bitsize(unsigned bitsize)
{
   switch (bitsize) {
   case 8:
      return PIPE_FORMAT_R8_UINT;
   case 16:
      return PIPE_FORMAT_R16_UINT;
   case 32:
      return PIPE_FORMAT_R32_UINT;
   default:
      unreachable("should have been lowered");
   }
}

static bool
pass(struct nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   if (intr->intrinsic != nir_intrinsic_load_global &&
       intr->intrinsic != nir_intrinsic_load_global_constant &&
       intr->intrinsic != nir_intrinsic_global_atomic &&
       intr->intrinsic != nir_intrinsic_global_atomic_swap &&
       intr->intrinsic != nir_intrinsic_store_global)
      return false;

   b->cursor = nir_before_instr(&intr->instr);

   unsigned bitsize = intr->intrinsic == nir_intrinsic_store_global
                         ? nir_src_bit_size(intr->src[0])
                         : intr->def.bit_size;
   enum pipe_format format = format_for_bitsize(bitsize);
   unsigned format_shift = util_logbase2(util_format_get_blocksize(format));

   nir_src *orig_offset = nir_get_io_offset_src(intr);
   nir_scalar base = nir_scalar_resolved(orig_offset->ssa, 0);
   struct match match = {.base = base};
   bool shift_must_match =
      (intr->intrinsic == nir_intrinsic_global_atomic) ||
      (intr->intrinsic == nir_intrinsic_global_atomic_swap);
   unsigned max_shift = format_shift + (shift_must_match ? 0 : 2);

   if (nir_scalar_is_alu(base)) {
      nir_op op = nir_scalar_alu_op(base);
      if (op == nir_op_ulea_agx || op == nir_op_ilea_agx) {
         unsigned shift = nir_scalar_as_uint(nir_scalar_chase_alu_src(base, 2));
         if (shift >= format_shift && shift <= max_shift) {
            match = (struct match){
               .base = nir_scalar_chase_alu_src(base, 0),
               .offset = nir_scalar_chase_alu_src(base, 1),
               .shift = shift - format_shift,
               .sign_extend = (op == nir_op_ilea_agx),
            };
         }
      } else if (op == nir_op_iadd) {
         for (unsigned i = 0; i < 2; ++i) {
            nir_scalar const_scalar = nir_scalar_chase_alu_src(base, i);
            if (!nir_scalar_is_const(const_scalar))
               continue;

            /* Put scalar into form (k*2^n), clamping n at the maximum hardware
             * shift.
             */
            int64_t raw_scalar = nir_scalar_as_uint(const_scalar);
            uint32_t shift = MIN2(__builtin_ctz(raw_scalar), max_shift);
            int64_t k = raw_scalar >> shift;

            /* See if the reduced scalar is from a sign extension. */
            if (k > INT32_MAX || k < INT32_MIN)
               break;

            /* Match the constant */
            match = (struct match){
               .base = nir_scalar_chase_alu_src(base, 1 - i),
               .offset = nir_get_scalar(nir_imm_int(b, k), 0),
               .shift = shift - format_shift,
               .sign_extend = true,
            };

            break;
         }
      }
   }

   nir_def *offset = match.offset.def != NULL
                        ? nir_channel(b, match.offset.def, match.offset.comp)
                        : nir_imm_int(b, 0);

   nir_def *new_base = nir_channel(b, match.base.def, match.base.comp);

   nir_def *repl = NULL;
   bool has_dest = (intr->intrinsic != nir_intrinsic_store_global);
   unsigned num_components = has_dest ? intr->def.num_components : 0;
   unsigned bit_size = has_dest ? intr->def.bit_size : 0;

   if (intr->intrinsic == nir_intrinsic_load_global) {
      repl =
         nir_load_agx(b, num_components, bit_size, new_base, offset,
                      .access = nir_intrinsic_access(intr), .base = match.shift,
                      .format = format, .sign_extend = match.sign_extend);

   } else if (intr->intrinsic == nir_intrinsic_load_global_constant) {
      repl = nir_load_constant_agx(b, num_components, bit_size, new_base,
                                   offset, .access = nir_intrinsic_access(intr),
                                   .base = match.shift, .format = format,
                                   .sign_extend = match.sign_extend);
   } else if (intr->intrinsic == nir_intrinsic_global_atomic) {
      repl =
         nir_global_atomic_agx(b, bit_size, new_base, offset, intr->src[1].ssa,
                               .atomic_op = nir_intrinsic_atomic_op(intr),
                               .sign_extend = match.sign_extend);
   } else if (intr->intrinsic == nir_intrinsic_global_atomic_swap) {
      repl = nir_global_atomic_swap_agx(
         b, bit_size, new_base, offset, intr->src[1].ssa, intr->src[2].ssa,
         .atomic_op = nir_intrinsic_atomic_op(intr),
         .sign_extend = match.sign_extend);
   } else {
      nir_store_agx(b, intr->src[0].ssa, new_base, offset,
                    .access = nir_intrinsic_access(intr), .base = match.shift,
                    .format = format, .sign_extend = match.sign_extend);
   }

   if (repl)
      nir_def_rewrite_uses(&intr->def, repl);

   nir_instr_remove(&intr->instr);
   return true;
}

bool
agx_nir_lower_address(nir_shader *nir)
{
   bool progress = false;

   /* First, clean up as much as possible. This will make fusing more effective.
    */
   do {
      progress = false;
      NIR_PASS(progress, nir, agx_nir_cleanup_amul);
      NIR_PASS(progress, nir, nir_opt_constant_folding);
      NIR_PASS(progress, nir, nir_opt_dce);
   } while (progress);

   /* Then, fuse as many lea as possible */
   NIR_PASS(progress, nir, agx_nir_fuse_lea);

   /* Next, lower load/store using the lea's */
   NIR_PASS(progress, nir, nir_shader_intrinsics_pass, pass,
            nir_metadata_control_flow, NULL);

   /* Finally, lower any leftover lea instructions back to ALU to let
    * nir_opt_algebraic simplify them from here.
    */
   NIR_PASS(progress, nir, agx_nir_lower_lea);
   NIR_PASS(progress, nir, nir_opt_dce);

   return progress;
}
