/*
 * Copyright (C) 2019 Google, Inc.
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
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#include "ir3.h"

/* The maximum number of nop's we may need to insert between two instructions.
 */
#define MAX_NOPS 6

/* The soft delay for approximating the cost of (ss). On a6xx, it takes the
 * number of delay slots to get a SFU result back (ie. using nop's instead of
 * (ss) is:
 *
 *     8 - single warp
 *     9 - two warps
 *    10 - four warps
 *
 * and so on. Not quite sure where it tapers out (ie. how many warps share an
 * SFU unit). But 10 seems like a reasonable # to choose:
 */
#define SOFT_SS_NOPS 10

/*
 * Helpers to figure out the necessary delay slots between instructions.  Used
 * both in scheduling pass(es) and the final pass to insert any required nop's
 * so that the shader program is valid.
 *
 * Note that this needs to work both pre and post RA, so we can't assume ssa
 * src iterators work.
 */

/* calculate required # of delay slots between the instruction that
 * assigns a value and the one that consumes
 */
int
ir3_delayslots(struct ir3_instruction *assigner,
               struct ir3_instruction *consumer, unsigned n, bool soft)
{
   /* generally don't count false dependencies, since this can just be
    * something like a barrier, or SSBO store.
    */
   if (__is_false_dep(consumer, n))
      return 0;

   /* worst case is cat1-3 (alu) -> cat4/5 needing 6 cycles, normal
    * alu -> alu needs 3 cycles, cat4 -> alu and texture fetch
    * handled with sync bits
    */

   if (is_meta(assigner) || is_meta(consumer))
      return 0;

   if (writes_addr0(assigner) || writes_addr1(assigner))
      return 6;

   if (soft && is_sfu(assigner))
      return SOFT_SS_NOPS;

   /* handled via sync flags: */
   if (is_sfu(assigner) || is_tex(assigner) || is_mem(assigner))
      return 0;

   /* As far as we know, shader outputs don't need any delay. */
   if (consumer->opc == OPC_END || consumer->opc == OPC_CHMASK)
      return 0;

   /* assigner must be alu: */
   if (is_flow(consumer) || is_sfu(consumer) || is_tex(consumer) ||
       is_mem(consumer) || (assigner->dsts[0]->flags & IR3_REG_SHARED)) {
      return 6;
   } else {
      /* In mergedregs mode, there is an extra 2-cycle penalty when half of
       * a full-reg is read as a half-reg or when a half-reg is read as a
       * full-reg.
       */
      bool mismatched_half = (assigner->dsts[0]->flags & IR3_REG_HALF) !=
                             (consumer->srcs[n]->flags & IR3_REG_HALF);
      unsigned penalty = mismatched_half ? 2 : 0;
      if ((is_mad(consumer->opc) || is_madsh(consumer->opc)) && (n == 2)) {
         /* special case, 3rd src to cat3 not required on first cycle */
         return 1 + penalty;
      } else {
         return 3 + penalty;
      }
   }
}

static bool
count_instruction(struct ir3_instruction *n)
{
   /* NOTE: don't count branch/jump since we don't know yet if they will
    * be eliminated later in resolve_jumps().. really should do that
    * earlier so we don't have this constraint.
    */
   return is_alu(n) ||
          (is_flow(n) && (n->opc != OPC_JUMP) && (n->opc != OPC_B));
}

static unsigned
distance(struct ir3_block *block, struct ir3_instruction *instr, unsigned maxd)
{
   unsigned d = 0;

   /* Note that this relies on incrementally building up the block's
    * instruction list.. but this is how scheduling and nopsched
    * work.
    */
   foreach_instr_rev (n, &block->instr_list) {
      if ((n == instr) || (d >= maxd))
         return MIN2(maxd, d + n->nop);
      if (count_instruction(n))
         d = MIN2(maxd, d + 1 + n->repeat + n->nop);
   }

   return maxd;
}

static unsigned
delay_calc_srcn_prera(struct ir3_block *block, struct ir3_instruction *assigner,
                      struct ir3_instruction *consumer, unsigned srcn)
{
   unsigned delay = 0;

   if (assigner->opc == OPC_META_PHI)
      return 0;

   if (is_meta(assigner)) {
      foreach_src_n (src, n, assigner) {
         unsigned d;

         if (!src->def)
            continue;

         d = delay_calc_srcn_prera(block, src->def->instr, consumer, srcn);
         delay = MAX2(delay, d);
      }
   } else {
      delay = ir3_delayslots(assigner, consumer, srcn, false);
      delay -= distance(block, assigner, delay);
   }

   return delay;
}

/**
 * Calculate delay for instruction before register allocation, using SSA
 * source pointers. This can't handle inter-block dependencies.
 */
unsigned
ir3_delay_calc_prera(struct ir3_block *block, struct ir3_instruction *instr)
{
   unsigned delay = 0;

   foreach_src_n (src, i, instr) {
      unsigned d = 0;

      if (src->def && src->def->instr->block == block) {
         d = delay_calc_srcn_prera(block, src->def->instr, instr, i);
      }

      delay = MAX2(delay, d);
   }

   return delay;
}

/* Post-RA, we don't have arrays any more, so we have to be a bit careful here
 * and have to handle relative accesses specially.
 */

static unsigned
post_ra_reg_elems(struct ir3_register *reg)
{
   if (reg->flags & IR3_REG_RELATIV)
      return reg->size;
   return reg_elems(reg);
}

static unsigned
post_ra_reg_num(struct ir3_register *reg)
{
   if (reg->flags & IR3_REG_RELATIV)
      return reg->array.base;
   return reg->num;
}

static unsigned
delay_calc_srcn_postra(struct ir3_instruction *assigner,
                       struct ir3_instruction *consumer, unsigned assigner_n,
                       unsigned consumer_n, bool soft, bool mergedregs)
{
   struct ir3_register *src = consumer->srcs[consumer_n];
   struct ir3_register *dst = assigner->dsts[assigner_n];
   bool mismatched_half =
      (src->flags & IR3_REG_HALF) != (dst->flags & IR3_REG_HALF);

   /* In the mergedregs case or when the register is a special register,
    * half-registers do not alias with full registers.
    */
   if ((!mergedregs || is_reg_special(src) || is_reg_special(dst)) &&
       mismatched_half)
      return 0;

   unsigned src_start = post_ra_reg_num(src) * reg_elem_size(src);
   unsigned src_end = src_start + post_ra_reg_elems(src) * reg_elem_size(src);
   unsigned dst_start = post_ra_reg_num(dst) * reg_elem_size(dst);
   unsigned dst_end = dst_start + post_ra_reg_elems(dst) * reg_elem_size(dst);

   if (dst_start >= src_end || src_start >= dst_end)
      return 0;

   unsigned delay = ir3_delayslots(assigner, consumer, consumer_n, soft);

   if (assigner->repeat == 0 && consumer->repeat == 0)
      return delay;

   /* If either side is a relative access, we can't really apply most of the
    * reasoning below because we don't know which component aliases which.
    * Just bail in this case.
    */
   if ((src->flags & IR3_REG_RELATIV) || (dst->flags & IR3_REG_RELATIV))
      return delay;

   /* MOVMSK seems to require that all users wait until the entire
    * instruction is finished, so just bail here.
    */
   if (assigner->opc == OPC_MOVMSK)
      return delay;

   /* TODO: Handle the combination of (rpt) and different component sizes
    * better like below. This complicates things significantly because the
    * components don't line up.
    */
   if (mismatched_half)
      return delay;

   /* If an instruction has a (rpt), then it acts as a sequence of
    * instructions, reading its non-(r) sources at each cycle. First, get the
    * register num for the first instruction where they interfere:
    */

   unsigned first_num = MAX2(src_start, dst_start) / reg_elem_size(dst);

   /* Now, for that first conflicting half/full register, figure out the
    * sub-instruction within assigner/consumer it corresponds to. For (r)
    * sources, this should already return the correct answer of 0. However we
    * have to special-case the multi-mov instructions, where the
    * sub-instructions sometimes come from the src/dst indices instead.
    */
   unsigned first_src_instr;
   if (consumer->opc == OPC_SWZ || consumer->opc == OPC_GAT)
      first_src_instr = consumer_n;
   else
      first_src_instr = first_num - src->num;

   unsigned first_dst_instr;
   if (assigner->opc == OPC_SWZ || assigner->opc == OPC_SCT)
      first_dst_instr = assigner_n;
   else
      first_dst_instr = first_num - dst->num;

   /* The delay we return is relative to the *end* of assigner and the
    * *beginning* of consumer, because it's the number of nops (or other
    * things) needed between them. Any instructions after first_dst_instr
    * subtract from the delay, and so do any instructions before
    * first_src_instr. Calculate an offset to subtract from the non-rpt-aware
    * delay to account for that.
    *
    * Now, a priori, we need to go through this process for every
    * conflicting regnum and take the minimum of the offsets to make sure
    * that the appropriate number of nop's is inserted for every conflicting
    * pair of sub-instructions. However, as we go to the next conflicting
    * regnum (if any), the number of instructions after first_dst_instr
    * decreases by 1 and the number of source instructions before
    * first_src_instr correspondingly increases by 1, so the offset stays the
    * same for all conflicting registers.
    */
   unsigned offset = first_src_instr + (assigner->repeat - first_dst_instr);
   return offset > delay ? 0 : delay - offset;
}

static unsigned
delay_calc_postra(struct ir3_block *block, struct ir3_instruction *start,
                  struct ir3_instruction *consumer, unsigned distance,
                  bool soft, bool pred, bool mergedregs)
{
   unsigned delay = 0;
   /* Search backwards starting at the instruction before start, unless it's
    * NULL then search backwards from the block end.
    */
   struct list_head *start_list =
      start ? start->node.prev : block->instr_list.prev;
   list_for_each_entry_from_rev (struct ir3_instruction, assigner, start_list,
                                 &block->instr_list, node) {
      if (count_instruction(assigner))
         distance += assigner->nop;

      if (distance + delay >= (soft ? SOFT_SS_NOPS : MAX_NOPS))
         return delay;

      if (is_meta(assigner))
         continue;

      unsigned new_delay = 0;

      foreach_dst_n (dst, dst_n, assigner) {
         if (dst->wrmask == 0)
            continue;
         foreach_src_n (src, src_n, consumer) {
            if (src->flags & (IR3_REG_IMMED | IR3_REG_CONST))
               continue;

            unsigned src_delay = delay_calc_srcn_postra(
               assigner, consumer, dst_n, src_n, soft, mergedregs);
            new_delay = MAX2(new_delay, src_delay);
         }
      }

      new_delay = new_delay > distance ? new_delay - distance : 0;
      delay = MAX2(delay, new_delay);

      if (count_instruction(assigner))
         distance += 1 + assigner->repeat;
   }

   /* Note: this allows recursion into "block" if it has already been
    * visited, but *not* recursion into its predecessors. We may have to
    * visit the original block twice, for the loop case where we have to
    * consider definititons in an earlier iterations of the same loop:
    *
    * while (...) {
    *		mov.u32u32 ..., r0.x
    *		...
    *		mov.u32u32 r0.x, ...
    * }
    *
    * However any other recursion would be unnecessary.
    */

   if (pred && block->data != block) {
      block->data = block;

      for (unsigned i = 0; i < block->predecessors_count; i++) {
         struct ir3_block *pred = block->predecessors[i];
         unsigned pred_delay = delay_calc_postra(pred, NULL, consumer, distance,
                                                 soft, pred, mergedregs);
         delay = MAX2(delay, pred_delay);
      }

      block->data = NULL;
   }

   return delay;
}

/**
 * Calculate delay for post-RA scheduling based on physical registers but not
 * exact (i.e. don't recurse into predecessors, and make it possible to
 * estimate impact of sync flags).
 *
 * @soft:  If true, add additional delay for situations where they
 *    would not be strictly required because a sync flag would be
 *    used (but scheduler would prefer to schedule some other
 *    instructions first to avoid stalling on sync flag)
 * @mergedregs: True if mergedregs is enabled.
 */
unsigned
ir3_delay_calc_postra(struct ir3_block *block, struct ir3_instruction *instr,
                      bool soft, bool mergedregs)
{
   return delay_calc_postra(block, NULL, instr, 0, soft, false, mergedregs);
}

/**
 * Calculate delay for nop insertion. This must exactly match hardware
 * requirements, including recursing into predecessor blocks.
 */
unsigned
ir3_delay_calc_exact(struct ir3_block *block, struct ir3_instruction *instr,
                     bool mergedregs)
{
   return delay_calc_postra(block, NULL, instr, 0, false, true, mergedregs);
}

/**
 * Remove nop instructions.  The scheduler can insert placeholder nop's
 * so that ir3_delay_calc() can account for nop's that won't be needed
 * due to nop's triggered by a previous instruction.  However, before
 * legalize, we want to remove these.  The legalize pass can insert
 * some nop's if needed to hold (for example) sync flags.  This final
 * remaining nops are inserted by legalize after this.
 */
void
ir3_remove_nops(struct ir3 *ir)
{
   foreach_block (block, &ir->block_list) {
      foreach_instr_safe (instr, &block->instr_list) {
         if (instr->opc == OPC_NOP) {
            list_del(&instr->node);
         }
      }
   }
}
