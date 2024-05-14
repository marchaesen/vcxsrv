/*
 * Copyright 2024 Alyssa Rosenzweig
 * SPDX-License-Identifier: MIT
 */

#include "agx_builder.h"
#include "agx_compiler.h"
#include "agx_opcodes.h"

static bool
is_shuffle(enum agx_opcode op)
{
   switch (op) {
   case AGX_OPCODE_SHUFFLE:
   case AGX_OPCODE_SHUFFLE_UP:
   case AGX_OPCODE_SHUFFLE_DOWN:
   case AGX_OPCODE_SHUFFLE_XOR:
   case AGX_OPCODE_QUAD_SHUFFLE:
   case AGX_OPCODE_QUAD_SHUFFLE_UP:
   case AGX_OPCODE_QUAD_SHUFFLE_DOWN:
   case AGX_OPCODE_QUAD_SHUFFLE_XOR:
      return true;
   default:
      return false;
   }
}

/*
 * AGX shuffle instructions read indices to shuffle with from the entire quad
 * and accumulate them. That means that an inactive thread anywhere in the quad
 * can make the whole shuffle undefined! To workaround, we reserve a scratch
 * register (r0h) which we keep zero throughout the program... except for when
 * actually shuffling, when we copy the shuffle index into r0h for the
 * operation. This ensures that inactive threads read 0 for their index and
 * hence do not contribute to the accumulated index.
 */
void
agx_lower_divergent_shuffle(agx_context *ctx)
{
   agx_builder b = agx_init_builder(ctx, agx_before_function(ctx));
   agx_index scratch = agx_register(1, AGX_SIZE_16);

   assert(ctx->any_quad_divergent_shuffle);
   agx_mov_imm_to(&b, scratch, 0);

   agx_foreach_block(ctx, block) {
      bool needs_zero = false;

      agx_foreach_instr_in_block_safe(block, I) {
         if (is_shuffle(I->op) && I->src[1].type == AGX_INDEX_REGISTER) {
            assert(I->dest[0].value != scratch.value);
            assert(I->src[0].value != scratch.value);
            assert(I->src[1].value != scratch.value);

            /* Use scratch register for our input, then zero it at the end of
             * the block so all inactive threads read zero.
             */
            b.cursor = agx_before_instr(I);
            agx_mov_to(&b, scratch, I->src[1]);
            needs_zero = true;

            I->src[1] = scratch;
         }
      }

      if (needs_zero) {
         b.cursor = agx_after_block_logical(block);
         agx_mov_imm_to(&b, scratch, 0);
      }
   }
}
