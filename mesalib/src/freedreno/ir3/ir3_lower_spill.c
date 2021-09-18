/*
 * Copyright (C) 2021 Valve Corporation
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

#include "ir3_ra.h"

/* The spilling pass leaves out a few details required to successfully operate
 * ldp/stp:
 *
 * 1. ldp/stp can only load/store 4 components at a time, but spilling ignores
 *    that and just spills/restores entire values, including arrays and values
 *    created for texture setup which can be more than 4 components.
 * 2. The spiller doesn't add barrier dependencies needed for post-RA
 *    scheduling.
 *
 * The first one, in particular, is much easier to handle after RA because
 * arrays and normal values can be treated the same way. Therefore this pass
 * runs after RA, and handles both issues. This keeps the complexity out of the
 * spiller.
 */

static void
split_spill(struct ir3_instruction *spill)
{
   unsigned orig_components = spill->srcs[2]->uim_val;

   /* We don't handle splitting dependencies. */
   assert(spill->deps_count == 0);

   if (orig_components <= 4) {
      if (spill->srcs[1]->flags & IR3_REG_ARRAY) {
         spill->srcs[1]->wrmask = MASK(orig_components);
         spill->srcs[1]->num = spill->srcs[1]->array.base;
         spill->srcs[1]->flags &= ~IR3_REG_ARRAY;
      }
      return;
   }

   for (unsigned comp = 0; comp < orig_components; comp += 4) {
      unsigned components = MIN2(orig_components - comp, 4);
      struct ir3_instruction *clone = ir3_instr_clone(spill);
      ir3_instr_move_before(clone, spill);

      clone->srcs[1]->wrmask = MASK(components);
      if (clone->srcs[1]->flags & IR3_REG_ARRAY) {
         clone->srcs[1]->num = clone->srcs[1]->array.base + comp;
         clone->srcs[1]->flags &= ~IR3_REG_ARRAY;
      }

      clone->srcs[2]->uim_val = components;
      clone->cat6.dst_offset +=
         comp * ((spill->srcs[1]->flags & IR3_REG_HALF) ? 2 : 4);
   }

   list_delinit(&spill->node);
}

static void
split_reload(struct ir3_instruction *reload)
{
   unsigned orig_components = reload->srcs[2]->uim_val;

   assert(reload->deps_count == 0);

   if (orig_components <= 4) {
      if (reload->dsts[0]->flags & IR3_REG_ARRAY) {
         reload->dsts[0]->wrmask = MASK(orig_components);
         reload->dsts[0]->num = reload->dsts[0]->array.base;
         reload->dsts[0]->flags &= ~IR3_REG_ARRAY;
      }
      return;
   }

   for (unsigned comp = 0; comp < orig_components; comp += 4) {
      unsigned components = MIN2(orig_components - comp, 4);
      struct ir3_instruction *clone = ir3_instr_clone(reload);
      ir3_instr_move_before(clone, reload);

      clone->dsts[0]->wrmask = MASK(components);
      if (clone->dsts[0]->flags & IR3_REG_ARRAY) {
         clone->dsts[0]->num = clone->dsts[0]->array.base + comp;
         clone->dsts[0]->flags &= ~IR3_REG_ARRAY;
      }

      clone->srcs[2]->uim_val = components;
      clone->srcs[1]->uim_val +=
         comp * ((reload->dsts[0]->flags & IR3_REG_HALF) ? 2 : 4);
   }

   list_delinit(&reload->node);
}

static void
add_spill_reload_deps(struct ir3_block *block)
{
   struct ir3_instruction *last_spill = NULL;

   foreach_instr (instr, &block->instr_list) {
      if ((instr->opc == OPC_SPILL_MACRO || instr->opc == OPC_RELOAD_MACRO) &&
          last_spill) {
         ir3_instr_add_dep(instr, last_spill);
      }

      if (instr->opc == OPC_SPILL_MACRO)
         last_spill = instr;
   }


   last_spill = NULL;

   foreach_instr_rev (instr, &block->instr_list) {
      if ((instr->opc == OPC_SPILL_MACRO || instr->opc == OPC_RELOAD_MACRO) &&
          last_spill) {
         ir3_instr_add_dep(last_spill, instr);
      }

      if (instr->opc == OPC_SPILL_MACRO)
         last_spill = instr;
   }
}

bool
ir3_lower_spill(struct ir3 *ir)
{
   foreach_block (block, &ir->block_list) {
      foreach_instr_safe (instr, &block->instr_list) {
         if (instr->opc == OPC_SPILL_MACRO)
            split_spill(instr);
         else if (instr->opc == OPC_RELOAD_MACRO)
            split_reload(instr);
      }

      add_spill_reload_deps(block);

      foreach_instr (instr, &block->instr_list) {
         if (instr->opc == OPC_SPILL_MACRO)
            instr->opc = OPC_STP;
         else if (instr->opc == OPC_RELOAD_MACRO)
            instr->opc = OPC_LDP;
      }
   }

   return true;
}
