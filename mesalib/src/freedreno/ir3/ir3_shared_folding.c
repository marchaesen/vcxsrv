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

/* Try to fold a shared -> non-shared mov into the instruction producing the
 * shared src. We do this aggresively, even if there are other uses of the
 * source, on the assumption that the "default" state should be non-shared and
 * we should be able to fold the other sources eventually.
 */

#include "util/ralloc.h"

#include "ir3.h"

static bool
try_shared_folding(struct ir3_instruction *mov, void *mem_ctx)
{
   if (mov->opc != OPC_MOV)
      return false;

   if ((mov->dsts[0]->flags & IR3_REG_SHARED) ||
       !(mov->srcs[0]->flags & IR3_REG_SHARED))
      return false;

   struct ir3_instruction *src = ssa(mov->srcs[0]);
   if (!src)
      return false;

   if (mov->cat1.dst_type != mov->cat1.src_type) {
      /* Check if the conversion can be folded into the source by ir3_cf */
      bool can_fold;
      type_t output_type = ir3_output_conv_type(src, &can_fold);
      if (!can_fold || output_type != TYPE_U32)
         return false;
      foreach_ssa_use (use, src) {
         if (use->opc != OPC_MOV ||
             use->cat1.src_type != mov->cat1.src_type ||
             use->cat1.dst_type != mov->cat1.dst_type)
            return false;
      }
   }

   if (src->opc == OPC_META_PHI) {
      struct ir3_block *block = src->block;
      for (unsigned i = 0; i < block->predecessors_count; i++) {
         struct ir3_block *pred = block->predecessors[i];
         if (src->srcs[i]->def) {
            struct ir3_instruction *pred_mov = ir3_instr_create(pred, OPC_MOV, 1, 1);
            __ssa_dst(pred_mov)->flags |= (src->srcs[i]->flags & IR3_REG_HALF);
            unsigned src_flags = IR3_REG_SSA | IR3_REG_SHARED |
               (src->srcs[i]->flags & IR3_REG_HALF);
            ir3_src_create(pred_mov, INVALID_REG, src_flags)->def =
               src->srcs[i]->def;
            pred_mov->cat1.src_type = pred_mov->cat1.dst_type =
               (src_flags & IR3_REG_HALF) ? TYPE_U16 : TYPE_U32;

            _mesa_set_remove_key(src->srcs[i]->def->instr->uses, src);
            _mesa_set_add(src->srcs[i]->def->instr->uses, pred_mov);
            src->srcs[i]->def = pred_mov->dsts[0];
         }
         src->srcs[i]->flags &= ~IR3_REG_SHARED;
      }
   } else if (opc_cat(src->opc) == 2 && src->srcs_count >= 2) {
      /* cat2 vector ALU instructions cannot have both shared sources */
      if ((src->srcs[0]->flags & (IR3_REG_SHARED | IR3_REG_CONST)) &&
          (src->srcs[1]->flags & (IR3_REG_SHARED | IR3_REG_CONST)))
         return false;
   } else if (opc_cat(src->opc) == 3) {
      /* cat3 vector ALU instructions cannot have src1 shared */
      if (src->srcs[1]->flags & IR3_REG_SHARED)
         return false;
   } else if (src->opc == OPC_LDC) {
      src->flags &= ~IR3_INSTR_U;
   } else {
      return false;
   }

   /* Remove IR3_REG_SHARED from the original destination, which should make the
    * mov trivial so that it can be cleaned up later by copy prop.
    */
   src->dsts[0]->flags &= ~IR3_REG_SHARED;
   mov->srcs[0]->flags &= ~IR3_REG_SHARED;

   /* Insert a copy to shared for uses other than this move instruction. */
   struct ir3_instruction *shared_mov = NULL;
   foreach_ssa_use (use, src) {
      if (use == mov)
         continue;

      if (!shared_mov) {
         shared_mov = ir3_MOV(src->block, src, mov->cat1.src_type);
         shared_mov->dsts[0]->flags |= IR3_REG_SHARED;
         if (src->opc == OPC_META_PHI)
            ir3_instr_move_after_phis(shared_mov, src->block);
         else
            ir3_instr_move_after(shared_mov, src);
         shared_mov->uses = _mesa_pointer_set_create(mem_ctx);
      }

      for (unsigned i = 0; i < use->srcs_count; i++) {
         if (use->srcs[i]->def == src->dsts[0])
            use->srcs[i]->def = shared_mov->dsts[0];
      }
      _mesa_set_add(shared_mov->uses, use);
   }

   return true;
}

bool
ir3_shared_fold(struct ir3 *ir)
{
   void *mem_ctx = ralloc_context(NULL);
   bool progress = false;

   ir3_find_ssa_uses(ir, mem_ctx, false);

   /* Folding a phi can push the mov up to its sources, so iterate blocks in
    * reverse to try and convert an entire phi-web in one go.
    */
   foreach_block_rev (block, &ir->block_list) {
      foreach_instr (instr, &block->instr_list) {
         progress |= try_shared_folding(instr, mem_ctx);
      }
   }

   ralloc_free(mem_ctx);

   return progress;
}

