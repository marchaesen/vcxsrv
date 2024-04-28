/*
 * Copyright (C) 2023 Valve Corporation.
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "ir3.h"
#include "util/ralloc.h"

/* RA cannot handle phis of shared registers where there are extra physical
 * sources, or the sources have extra physical destinations, because these edges
 * are critical edges that we cannot resolve copies along.  Here's a contrived
 * example:
 *
 * loop {
 *    if non-uniform {
 *       if uniform {
 *          x_1 = ...;
 *          continue;
 *       }
 *       x_2 = ...;
 *    } else {
 *       break;
 *    }
 *    // continue block
 *    x_3 = phi(x_1, x_2)
 * }
 *
 * Assuming x_1 and x_2 are uniform, x_3 will also be uniform, because all
 * threads that stay in the loop take the same branch to the continue block,
 * however execution may fall through from the assignment to x_2 to the
 * break statement because the outer if is non-uniform, and then it will fall
 * through again to the continue block. In cases like this we have to demote the
 * phi to normal registers and insert movs around it (which will probably be
 * coalesced).
 */

static void
lower_phi(void *ctx, struct ir3_instruction *phi)
{
   struct ir3_block *block = phi->block;
   for (unsigned i = 0; i < block->predecessors_count; i++) {
      struct ir3_block *pred = block->predecessors[i];
      if (phi->srcs[i]->def) {
         struct ir3_instruction *pred_mov = ir3_instr_create(pred, OPC_MOV, 1, 1);
         pred_mov->uses = _mesa_pointer_set_create(ctx);
         __ssa_dst(pred_mov)->flags |= (phi->srcs[i]->flags & IR3_REG_HALF);
         unsigned src_flags = IR3_REG_SSA | IR3_REG_SHARED |
            (phi->srcs[i]->flags & IR3_REG_HALF);
         ir3_src_create(pred_mov, INVALID_REG, src_flags)->def =
            phi->srcs[i]->def;
         pred_mov->cat1.src_type = pred_mov->cat1.dst_type =
            (src_flags & IR3_REG_HALF) ? TYPE_U16 : TYPE_U32;

         _mesa_set_remove_key(phi->srcs[i]->def->instr->uses, phi);
         _mesa_set_add(phi->srcs[i]->def->instr->uses, pred_mov);
         phi->srcs[i]->def = pred_mov->dsts[0];
      }
      phi->srcs[i]->flags &= ~IR3_REG_SHARED;
   }

   phi->dsts[0]->flags &= ~IR3_REG_SHARED;

   struct ir3_instruction *shared_mov =
      ir3_MOV(block, phi,
              (phi->dsts[0]->flags & IR3_REG_HALF) ? TYPE_U16 : TYPE_U32);
   shared_mov->uses = _mesa_pointer_set_create(ctx);
   shared_mov->dsts[0]->flags |= IR3_REG_SHARED;
   ir3_instr_move_after_phis(shared_mov, block);

   foreach_ssa_use (use, phi) {
      for (unsigned i = 0; i < use->srcs_count; i++) {
         if (use->srcs[i]->def == phi->dsts[0])
            use->srcs[i]->def = shared_mov->dsts[0];
      }
   }
}

bool
ir3_lower_shared_phis(struct ir3 *ir)
{
   void *mem_ctx = ralloc_context(NULL);
   bool progress = false;

   ir3_find_ssa_uses(ir, mem_ctx, false);

   foreach_block (block, &ir->block_list) {
      bool pred_physical_edge = false;
      for (unsigned i = 0; i < block->predecessors_count; i++) {
         unsigned successors_count =
            block->predecessors[i]->successors[1] ? 2 : 1;
         if (block->predecessors[i]->physical_successors_count > successors_count) {
            pred_physical_edge = true;
            break;
         }
      }

      if (!pred_physical_edge &&
          block->physical_predecessors_count == block->predecessors_count)
         continue;

      foreach_instr_safe (phi, &block->instr_list) {
         if (phi->opc != OPC_META_PHI)
            break;

         if (!(phi->dsts[0]->flags & IR3_REG_SHARED))
            continue;

         lower_phi(mem_ctx, phi);
         progress = true;
      }
   }

   ralloc_free(mem_ctx);
   return progress;
}

