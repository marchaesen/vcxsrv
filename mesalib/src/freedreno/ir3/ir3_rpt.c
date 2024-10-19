/*
 * Copyright 2024 Igalia S.L.
 * SPDX-License-Identifier: MIT
 */

#include "ir3_nir.h"

bool
ir3_supports_vectorized_nir_op(nir_op op)
{
   switch (op) {
      /* TODO: emitted as absneg which can often be folded away (e.g., into
       * (neg)). This seems to often fail when repeated.
       */
   case nir_op_b2b1:

      /* dp2acc/dp4acc don't seem to support repeat. */
   case nir_op_udot_4x8_uadd:
   case nir_op_udot_4x8_uadd_sat:
   case nir_op_sudot_4x8_iadd:
   case nir_op_sudot_4x8_iadd_sat:

      /* Among SFU instructions, only rcp doesn't seem to support repeat. */
   case nir_op_frcp:
      return false;

   default:
      return true;
   }
}

uint8_t
ir3_nir_vectorize_filter(const nir_instr *instr, const void *data)
{
   if (instr->type == nir_instr_type_phi)
      return 4;
   if (instr->type != nir_instr_type_alu)
      return 0;

   struct nir_alu_instr *alu = nir_instr_as_alu(instr);

   if (!ir3_supports_vectorized_nir_op(alu->op))
      return 0;

   return 4;
}

static void
rpt_list_split(struct list_head *list, struct list_head *at)
{
   struct list_head *new_last = at->prev;
   new_last->next = list;
   at->prev = list->prev;
   list->prev->next = at;
   list->prev = new_last;
}

static enum ir3_register_flags
rpt_compatible_src_flags(struct ir3_register *src)
{
   return src->flags &
          (IR3_REG_SABS | IR3_REG_SNEG | IR3_REG_FABS | IR3_REG_FNEG |
           IR3_REG_BNOT | IR3_REG_CONST | IR3_REG_IMMED | IR3_REG_SSA |
           IR3_REG_HALF | IR3_REG_SHARED);
}

static enum ir3_register_flags
rpt_compatible_dst_flags(struct ir3_instruction *instr)
{
   return instr->dsts[0]->flags & (IR3_REG_SSA | IR3_REG_HALF | IR3_REG_SHARED);
}

static enum ir3_register_flags
rpt_illegal_src_flags(struct ir3_register *src)
{
   return src->flags & (IR3_REG_ARRAY | IR3_REG_RELATIV);
}

static enum ir3_instruction_flags
rpt_compatible_instr_flags(struct ir3_instruction *instr)
{
   return instr->flags & IR3_INSTR_SAT;
}

static bool
supports_imm_r(unsigned opc)
{
   return opc == OPC_BARY_F || opc == OPC_FLAT_B;
}

static bool
srcs_can_rpt(struct ir3_instruction *instr, struct ir3_register *src,
             struct ir3_register *rpt_src, unsigned rpt_n)
{
   if (rpt_illegal_src_flags(src) != 0 || rpt_illegal_src_flags(rpt_src) != 0)
      return false;
   if (rpt_compatible_src_flags(src) != rpt_compatible_src_flags(rpt_src))
      return false;
   if (src->flags & IR3_REG_IMMED) {
      uint32_t val = src->uim_val;
      uint32_t rpt_val = rpt_src->uim_val;

      if (rpt_val == val)
         return true;
      if (supports_imm_r(instr->opc))
         return rpt_val == val + rpt_n;
      return false;
   }

   return true;
}

static bool
can_rpt(struct ir3_instruction *instr, struct ir3_instruction *rpt,
        unsigned rpt_n)
{
   if (rpt_n >= 4)
      return false;
   if (rpt->ip != instr->ip + rpt_n)
      return false;
   if (rpt->opc != instr->opc)
      return false;
   if (!ir3_supports_rpt(instr->block->shader->compiler, instr->opc))
      return false;
   if (rpt_compatible_instr_flags(rpt) != rpt_compatible_instr_flags(instr))
      return false;
   if (rpt_compatible_dst_flags(rpt) != rpt_compatible_dst_flags(instr))
      return false;
   if (instr->srcs_count != rpt->srcs_count)
      return false;

   foreach_src_n (src, src_n, instr) {
      if (!srcs_can_rpt(instr, src, rpt->srcs[src_n], rpt_n))
         return false;
   }

   return true;
}

static bool
cleanup_rpt_instr(struct ir3_instruction *instr)
{
   if (!ir3_instr_is_first_rpt(instr))
      return false;

   unsigned rpt_n = 1;
   foreach_instr_rpt_excl (rpt, instr) {
      if (!can_rpt(instr, rpt, rpt_n++)) {
         rpt_list_split(&instr->rpt_node, &rpt->rpt_node);

         /* We have to do this recursively since later repetitions might come
          * before the first in the instruction list.
          */
         cleanup_rpt_instr(rpt);
         return true;
      }
   }

   return false;
}

/* Pre-RA pass to clean up repetition groups that can never be merged into a rpt
 * instruction. This ensures we don't needlessly allocate merge sets for them.
 */
bool
ir3_cleanup_rpt(struct ir3 *ir, struct ir3_shader_variant *v)
{
   ir3_count_instructions(ir);
   bool progress = false;

   foreach_block (block, &ir->block_list) {
      foreach_instr (instr, &block->instr_list)
         progress |= cleanup_rpt_instr(instr);
   }

   return progress;
}

enum rpt_src_type {
   RPT_INCOMPATIBLE, /* Incompatible sources. */
   RPT_SET,          /* Compatible sources that need (r) set. */
   RPT_DONT_SET,     /* Compatible sources that don't need (r) set. */
};

static enum rpt_src_type
srcs_rpt_compatible(struct ir3_instruction *instr, struct ir3_register *src,
                    struct ir3_register *rpt_src)
{
   /* Shared RA may have demoted some sources from shared to non-shared. When
    * this happened for some but not all instructions in a repeat group, the
    * assert below would trigger. Detect this here.
    */
   if ((src->flags & IR3_REG_SHARED) != (rpt_src->flags & IR3_REG_SHARED))
      return RPT_INCOMPATIBLE;

   assert(srcs_can_rpt(instr, src, rpt_src, instr->repeat + 1));

   if (src->flags & IR3_REG_IMMED) {
      if (supports_imm_r(instr->opc) &&
          rpt_src->uim_val == src->uim_val + instr->repeat + 1) {
         return RPT_SET;
      }

      assert(rpt_src->uim_val == src->uim_val);
      return RPT_DONT_SET;
   }

   if (rpt_src->num == src->num + instr->repeat + 1) {
      if ((src->flags & IR3_REG_R) || instr->repeat == 0)
         return RPT_SET;
      return RPT_INCOMPATIBLE;
   }

   if (rpt_src->num == src->num && !(src->flags & IR3_REG_R))
      return RPT_DONT_SET;
   return RPT_INCOMPATIBLE;
}

static unsigned
inc_wrmask(unsigned wrmask)
{
   return (wrmask << 1) | 0x1;
}

static bool
try_merge(struct ir3_instruction *instr, struct ir3_instruction *rpt,
          unsigned rpt_n)
{
   assert(rpt_n > 0 && rpt_n < 4);
   assert(instr->opc == rpt->opc);
   assert(instr->dsts_count == 1 && rpt->dsts_count == 1);
   assert(instr->srcs_count == rpt->srcs_count);
   assert(rpt_compatible_instr_flags(instr) == rpt_compatible_instr_flags(rpt));

   struct ir3_register *dst = instr->dsts[0];
   struct ir3_register *rpt_dst = rpt->dsts[0];

   if (rpt->ip != instr->ip + rpt_n)
      return false;
   if (rpt_dst->num != dst->num + rpt_n)
      return false;

   enum rpt_src_type srcs_rpt[instr->srcs_count];

   foreach_src_n (src, src_n, instr) {
      srcs_rpt[src_n] = srcs_rpt_compatible(instr, src, rpt->srcs[src_n]);

      if (srcs_rpt[src_n] == RPT_INCOMPATIBLE)
         return false;
   }

   foreach_src_n (src, src_n, instr) {
      assert((src->flags & ~(IR3_REG_R | IR3_REG_KILL | IR3_REG_FIRST_KILL)) ==
             (rpt->srcs[src_n]->flags & ~(IR3_REG_KILL | IR3_REG_FIRST_KILL)));

      if (srcs_rpt[src_n] == RPT_SET) {
         src->flags |= IR3_REG_R;
         src->wrmask = inc_wrmask(src->wrmask);
      }
   }

   dst->wrmask = inc_wrmask(dst->wrmask);
   return true;
}

static bool
merge_instr(struct ir3_instruction *instr)
{
   if (!ir3_instr_is_first_rpt(instr))
      return false;

   bool progress = false;

   unsigned rpt_n = 1;

   foreach_instr_rpt_excl_safe (rpt, instr) {
      /* When rpt cannot be merged, stop immediately. We will try to merge rpt
       * with the following instructions (if any) once we encounter it in
       * ir3_combine_rpt.
       */
      if (!try_merge(instr, rpt, rpt_n))
         break;

      instr->repeat++;

      /* We cannot remove the rpt immediately since when it is the instruction
       * after instr, foreach_instr_safe will fail. So mark it instead and
       * remove it in ir3_combine_rpt when we encounter it.
       */
      rpt->flags |= IR3_INSTR_MARK;
      list_delinit(&rpt->rpt_node);
      ++rpt_n;
      progress = true;
   }

   list_delinit(&instr->rpt_node);
   return progress;
}

/* Merge compatible instructions in a repetition group into one or more rpt
 * instructions.
 */
bool
ir3_merge_rpt(struct ir3 *ir, struct ir3_shader_variant *v)
{
   ir3_clear_mark(ir);
   ir3_count_instructions(ir);
   bool progress = false;

   foreach_block (block, &ir->block_list) {
      foreach_instr_safe (instr, &block->instr_list) {
         if (instr->flags & IR3_INSTR_MARK) {
            list_delinit(&instr->node);
            continue;
         }

         progress |= merge_instr(instr);
      }
   }

   return progress;
}
