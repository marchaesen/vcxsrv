/*
 * Copyright (c) 2025 Lima Project
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 */

#include "ppir.h"

static bool instr_has_single_slot(ppir_instr *instr)
{
   int count = 0;
   for (int i = 0; i < PPIR_INSTR_SLOT_NUM; i++) {
      if (instr->slots[i])
         count++;
   }

   if (instr->constant[0].num || instr->constant[1].num)
      count++;

   return count == 1;
}

bool ppir_compact_prog(ppir_compiler *comp)
{
   list_for_each_entry(ppir_block, block, &comp->block_list, list) {
      /* Walk block in reverse order.
       * If current instruction have a single instruction in combiner,
       * try moving it into previous instruction.
       */
      ppir_instr *next_instr = NULL;
      ppir_node *node = NULL;
      list_for_each_entry_safe_rev(ppir_instr, instr, &block->instr_list, list) {
         if (node && !instr->slots[PPIR_INSTR_SLOT_ALU_COMBINE]) {
            instr->slots[node->instr_pos] = node;
            list_del(&next_instr->list);
            comp->cur_instr_index--;
         }
         node = NULL;
         if (instr_has_single_slot(instr)) {
            if (instr->slots[PPIR_INSTR_SLOT_ALU_COMBINE]) {
               node = instr->slots[PPIR_INSTR_SLOT_ALU_COMBINE];
               break;
            }
         }
         next_instr = instr;
      }
   }

   return true;
}
