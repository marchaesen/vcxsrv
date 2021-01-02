/*
 * Copyright (c) 2019 Lima Project
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

/* Propagates liveness from a liveness set to another by performing the
 * union between sets. */
static void
ppir_liveness_propagate(ppir_compiler *comp,
                        struct ppir_liveness *dest, struct ppir_liveness *src,
                        struct set *dest_set, struct set *src_set)
{
   set_foreach(src_set, entry_src) {
      const struct ppir_liveness *s = entry_src->key;
      assert(s);

      unsigned int regalloc_index = s->reg->regalloc_index;

      dest[regalloc_index].reg = src[regalloc_index].reg;
      dest[regalloc_index].mask |= src[regalloc_index].mask;
      _mesa_set_add(dest_set, &dest[regalloc_index]);
   }
}

/* Clone a liveness set (without propagation) */
static void
ppir_liveness_set_clone(ppir_compiler *comp,
                        struct ppir_liveness *dest, struct ppir_liveness *src,
                        struct set *dest_set, struct set *src_set)
{
   _mesa_set_clear(dest_set, NULL);
   memset(dest, 0, list_length(&comp->reg_list) * sizeof(struct ppir_liveness));
   memcpy(dest, src,
          list_length(&comp->reg_list) * sizeof(struct ppir_liveness));

   set_foreach(src_set, entry_src) {
      const struct ppir_liveness *s = entry_src->key;
      assert(s);

      unsigned int regalloc_index = s->reg->regalloc_index;
      dest[regalloc_index].reg = src[regalloc_index].reg;
      dest[regalloc_index].mask = src[regalloc_index].mask;
      _mesa_set_add(dest_set, &dest[regalloc_index]);
   }
}

/* Check whether two liveness sets are equal. */
static bool
ppir_liveness_set_equal(ppir_compiler *comp,
                        struct ppir_liveness *l1, struct ppir_liveness *l2,
                        struct set *set1, struct set *set2)
{
   set_foreach(set1, entry1) {
      const struct ppir_liveness *k1 = entry1->key;
      unsigned int regalloc_index = k1->reg->regalloc_index;

      struct set_entry *entry2 = _mesa_set_search(set2, &l2[regalloc_index]);
      if (!entry2)
         return false;

      const struct ppir_liveness *k2 = entry2->key;

      if (k1->mask != k2->mask)
         return false;
   }
   set_foreach(set2, entry2) {
      const struct ppir_liveness *k2 = entry2->key;
      unsigned int regalloc_index = k2->reg->regalloc_index;

      struct set_entry *entry1 = _mesa_set_search(set1, &l1[regalloc_index]);
      if (!entry1)
         return false;

      const struct ppir_liveness *k1 = entry1->key;

      if (k2->mask != k1->mask)
         return false;
   }
   return true;
}

/* Update the liveness information of the instruction by adding its srcs
 * as live registers to the live_in set. */
static void
ppir_liveness_instr_srcs(ppir_compiler *comp, ppir_instr *instr)
{
   for (int i = PPIR_INSTR_SLOT_NUM-1; i >= 0; i--) {
      ppir_node *node = instr->slots[i];
      if (!node)
         continue;

      switch(node->op) {
         case ppir_op_const:
         case ppir_op_undef:
            continue;
         default:
            break;
      }

      for (int i = 0; i < ppir_node_get_src_num(node); i++) {
         ppir_src *src = ppir_node_get_src(node, i);
         if (!src || src->type == ppir_target_pipeline)
            continue;

         ppir_reg *reg = ppir_src_get_reg(src);
         if (!reg || reg->undef)
            continue;

         /* if some other op on this same instruction is writing,
          * we just need to reserve a register for this particular
          * instruction. */
         if (src->node && src->node->instr == instr) {
            instr->live_internal[reg->regalloc_index].reg = reg;
            _mesa_set_add(instr->live_internal_set, &instr->live_internal[reg->regalloc_index]);
            continue;
         }

         struct set_entry *live = _mesa_set_search(instr->live_in_set,
                                                   &instr->live_in[reg->regalloc_index]);
         if (src->type == ppir_target_ssa) {
            /* reg is read, needs to be live before instr */
            if (live)
               continue;

            instr->live_in[reg->regalloc_index].reg = reg;
            _mesa_set_add(instr->live_in_set, &instr->live_in[reg->regalloc_index]);
         }
         else {
            unsigned int mask = ppir_src_get_mask(src);

            /* read reg is type register, need to check if this sets
             * any additional bits in the current mask */
            if (live && (instr->live_in[reg->regalloc_index].mask ==
                        (instr->live_in[reg->regalloc_index].mask | mask)))
               continue;

            /* some new components */
            instr->live_in[reg->regalloc_index].reg = reg;
            instr->live_in[reg->regalloc_index].mask |= mask;
            _mesa_set_add(instr->live_in_set, &instr->live_in[reg->regalloc_index]);
         }
      }
   }
}


/* Update the liveness information of the instruction by removing its
 * dests from the live_in set. */
static void
ppir_liveness_instr_dest(ppir_compiler *comp, ppir_instr *instr)
{
   for (int i = PPIR_INSTR_SLOT_NUM-1; i >= 0; i--) {
      ppir_node *node = instr->slots[i];
      if (!node)
         continue;

      switch(node->op) {
         case ppir_op_const:
         case ppir_op_undef:
            continue;
         default:
            break;
      }

      ppir_dest *dest = ppir_node_get_dest(node);
      if (!dest || dest->type == ppir_target_pipeline)
         continue;
      ppir_reg *reg = ppir_dest_get_reg(dest);
      if (!reg || reg->undef)
         continue;

      struct set_entry *live = _mesa_set_search(instr->live_in_set,
                                                &instr->live_in[reg->regalloc_index]);

      /* If a register is written but wasn't read in a later instruction, it is
       * either dead code or a bug. For now, assign an interference to it to
       * ensure it doesn't get assigned a live register and overwrites it. */
      if (!live) {
         instr->live_internal[reg->regalloc_index].reg = reg;
         _mesa_set_add(instr->live_internal_set, &instr->live_internal[reg->regalloc_index]);
         continue;
      }

      if (dest->type == ppir_target_ssa) {
         /* reg is written and ssa, is not live before instr */
         _mesa_set_remove_key(instr->live_in_set, &instr->live_in[reg->regalloc_index]);
      }
      else {
         unsigned int mask = dest->write_mask;
         /* written reg is type register, need to check if this clears
          * the remaining mask to remove it from the live set */
         if (instr->live_in[reg->regalloc_index].mask ==
             (instr->live_in[reg->regalloc_index].mask & ~mask))
            continue;

         instr->live_in[reg->regalloc_index].mask &= ~mask;
         /* unset reg if all remaining bits were cleared */
         if (!instr->live_in[reg->regalloc_index].mask) {
            _mesa_set_remove_key(instr->live_in_set, &instr->live_in[reg->regalloc_index]);
         }
      }
   }
}

/* Main loop, iterate blocks/instructions/ops backwards, propagate
 * livenss and update liveness of each instruction. */
static bool
ppir_liveness_compute_live_sets(ppir_compiler *comp)
{
   bool cont = false;
   list_for_each_entry_rev(ppir_block, block, &comp->block_list, list) {
      ppir_instr *first = list_first_entry(&block->instr_list, ppir_instr, list);
      ppir_instr *last = list_last_entry(&block->instr_list, ppir_instr, list);

      /* inherit live_out from the other blocks live_in */
      for (int i = 0; i < 2; i++) {
         ppir_block *succ = block->successors[i];
         if (!succ)
            continue;

         ppir_liveness_propagate(comp, block->live_out, succ->live_in,
                                 block->live_out_set, succ->live_in_set);
      }

      list_for_each_entry_rev(ppir_instr, instr, &block->instr_list, list) {
         /* inherit (or-) live variables from next instr or block */
         if (instr == last) {
            ppir_liveness_set_clone(comp,
                                    instr->live_out, block->live_out,
                                    instr->live_out_set, block->live_out_set);
         }
         else {
            ppir_instr *next_instr = LIST_ENTRY(ppir_instr, instr->list.next, list);
            ppir_liveness_set_clone(comp,
                                    instr->live_out, next_instr->live_in,
                                    instr->live_out_set, next_instr->live_in_set);
         }
         /* initial copy to check for changes */
         struct set *temp_live_in_set = _mesa_set_create(comp,
                                                         _mesa_hash_pointer,
                                                         _mesa_key_pointer_equal);
         struct ppir_liveness temp_live_in[list_length(&comp->reg_list)];
         ppir_liveness_set_clone(comp,
               temp_live_in, instr->live_in,
               temp_live_in_set, instr->live_in_set);

         /* initialize live_in for potential changes */
         ppir_liveness_propagate(comp, instr->live_in, instr->live_out,
                                 instr->live_in_set, instr->live_out_set);

         ppir_liveness_instr_dest(comp, instr);
         ppir_liveness_instr_srcs(comp, instr);

         cont |= !ppir_liveness_set_equal(comp, temp_live_in, instr->live_in,
               temp_live_in_set, instr->live_in_set);
      }

      /* inherit live_in from the first instruction in the block,
       * or live_out if it is empty */
      if (!list_is_empty(&block->instr_list) && first && first->scheduled)
         ppir_liveness_set_clone(comp, block->live_in, first->live_in,
               block->live_in_set, first->live_in_set);
      else
         ppir_liveness_set_clone(comp, block->live_in, block->live_out,
               block->live_in_set, block->live_out_set);
   }

   return cont;
}

/*
 * Liveness analysis is based on https://en.wikipedia.org/wiki/Live_variable_analysis
 * This implementation calculates liveness before/after each
 * instruction. Aggregated block liveness information is stored
 * before/after blocks for conveniency (handle e.g. empty blocks).
 * Blocks/instructions/ops are iterated backwards so register reads are
 * propagated up to the instruction that writes it.
 *
 * 1) Before computing liveness for each instruction, propagate live_out
 *    from the next instruction. If it is the last instruction in a
 *    block, propagate liveness from all possible next instructions
 *    (in this case, this information comes from the live_out of the
 *    block itself).
 * 2) Calculate live_in for the each instruction. The initial live_in is
 *    a copy of its live_out so registers who aren't touched by this
 *    instruction are kept intact.
 *    - If a register is written by this instruction, it no longer needs
 *    to be live before the instruction, so it is removed from live_in.
 *    - If a register is read by this instruction, it needs to be live
 *    before its execution, so add it to live_in.
 *    - Non-ssa registers are a special case. For this, the algorithm
 *    keeps and updates the mask of live components following the same
 *    logic as above. The register is only removed from the live set
 *    when no live components are left.
 *    - If a non-ssa register is written and read in the same
 *    instruction, it stays in live_in.
 *    - Another special case is a ssa register that is written by an
 *    early op in the instruction, and read by a later op. In this case,
 *    the algorithm adds it to the live_out set so that the register
 *    allocator properly assigns an interference for it.
 * 3) The algorithm must run over the entire program until it converges,
 *    i.e. a full run happens without changes. This is because blocks
 *    are updated sequentially and updates in a block may need to be
 *    propagated to parent blocks that were already calculated in the
 *    current run.
 */
void
ppir_liveness_analysis(ppir_compiler *comp)
{
   while (ppir_liveness_compute_live_sets(comp))
      ;
}
