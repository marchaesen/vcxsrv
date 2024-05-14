/*
 * Copyright 2021 Alyssa Rosenzweig
 * SPDX-License-Identifier: MIT
 */

#include "agx_compiler.h"

/**
 * SSA-based scalar dead code elimination.
 * This pass assumes that no loop header phis are dead.
 */
void
agx_dce(agx_context *ctx, bool partial)
{
   BITSET_WORD *seen = calloc(BITSET_WORDS(ctx->alloc), sizeof(BITSET_WORD));

   agx_foreach_block(ctx, block) {
      if (block->loop_header) {
         agx_foreach_phi_in_block(block, I) {
            agx_foreach_ssa_src(I, s) {
               BITSET_SET(seen, I->src[s].value);
            }
         }
      }
   }

   agx_foreach_block_rev(ctx, block) {
      agx_foreach_instr_in_block_safe_rev(block, I) {
         if (block->loop_header && I->op == AGX_OPCODE_PHI)
            break;

         bool needed = false;

         agx_foreach_ssa_dest(I, d) {
            /* Eliminate destinations that are never read, as RA needs to
             * handle them specially. Visible only for instructions that write
             * multiple destinations (splits) or that write a destination but
             * cannot be DCE'd (atomics).
             */
            if (BITSET_TEST(seen, I->dest[d].value)) {
               needed = true;
            } else if (partial) {
               I->dest[d] = agx_null();
            }
         }

         if (!needed && agx_opcodes_info[I->op].can_eliminate) {
            agx_remove_instruction(I);
         } else {
            agx_foreach_ssa_src(I, s) {
               BITSET_SET(seen, I->src[s].value);
            }
         }
      }
   }

   free(seen);
}
