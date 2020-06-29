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

/*
 * Helpers to figure out the necessary delay slots between instructions.  Used
 * both in scheduling pass(es) and the final pass to insert any required nop's
 * so that the shader program is valid.
 *
 * Note that this needs to work both pre and post RA, so we can't assume ssa
 * src iterators work.
 */

/* generally don't count false dependencies, since this can just be
 * something like a barrier, or SSBO store.  The exception is array
 * dependencies if the assigner is an array write and the consumer
 * reads the same array.
 */
static bool
ignore_dep(struct ir3_instruction *assigner,
		struct ir3_instruction *consumer, unsigned n)
{
	if (!__is_false_dep(consumer, n))
		return false;

	if (assigner->barrier_class & IR3_BARRIER_ARRAY_W) {
		struct ir3_register *dst = assigner->regs[0];

		debug_assert(dst->flags & IR3_REG_ARRAY);

		foreach_src (src, consumer) {
			if ((src->flags & IR3_REG_ARRAY) &&
					(dst->array.id == src->array.id)) {
				return false;
			}
		}
	}

	return true;
}

/* calculate required # of delay slots between the instruction that
 * assigns a value and the one that consumes
 */
int
ir3_delayslots(struct ir3_instruction *assigner,
		struct ir3_instruction *consumer, unsigned n, bool soft)
{
	if (ignore_dep(assigner, consumer, n))
		return 0;

	/* worst case is cat1-3 (alu) -> cat4/5 needing 6 cycles, normal
	 * alu -> alu needs 3 cycles, cat4 -> alu and texture fetch
	 * handled with sync bits
	 */

	if (is_meta(assigner) || is_meta(consumer))
		return 0;

	if (writes_addr0(assigner) || writes_addr1(assigner))
		return 6;

	/* On a6xx, it takes the number of delay slots to get a SFU result
	 * back (ie. using nop's instead of (ss) is:
	 *
	 *     8 - single warp
	 *     9 - two warps
	 *    10 - four warps
	 *
	 * and so on.  Not quite sure where it tapers out (ie. how many
	 * warps share an SFU unit).  But 10 seems like a reasonable #
	 * to choose:
	 */
	if (soft && is_sfu(assigner))
		return 10;

	/* handled via sync flags: */
	if (is_sfu(assigner) || is_tex(assigner) || is_mem(assigner))
		return 0;

	/* assigner must be alu: */
	if (is_flow(consumer) || is_sfu(consumer) || is_tex(consumer) ||
			is_mem(consumer)) {
		return 6;
	} else if ((is_mad(consumer->opc) || is_madsh(consumer->opc)) &&
			(n == 3)) {
		/* special case, 3rd src to cat3 not required on first cycle */
		return 1;
	} else {
		return 3;
	}
}

static bool
count_instruction(struct ir3_instruction *n)
{
	/* NOTE: don't count branch/jump since we don't know yet if they will
	 * be eliminated later in resolve_jumps().. really should do that
	 * earlier so we don't have this constraint.
	 */
	return is_alu(n) || (is_flow(n) && (n->opc != OPC_JUMP) && (n->opc != OPC_B));
}

/**
 * @block: the block to search in, starting from end; in first pass,
 *    this will be the block the instruction would be inserted into
 *    (but has not yet, ie. it only contains already scheduled
 *    instructions).  For intra-block scheduling (second pass), this
 *    would be one of the predecessor blocks.
 * @instr: the instruction to search for
 * @maxd:  max distance, bail after searching this # of instruction
 *    slots, since it means the instruction we are looking for is
 *    far enough away
 * @pred:  if true, recursively search into predecessor blocks to
 *    find the worst case (shortest) distance (only possible after
 *    individual blocks are all scheduled)
 */
static unsigned
distance(struct ir3_block *block, struct ir3_instruction *instr,
		unsigned maxd, bool pred)
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

	/* if coming from a predecessor block, assume it is assigned far
	 * enough away.. we'll fix up later.
	 */
	if (!pred)
		return maxd;

	if (pred && (block->data != block)) {
		/* Search into predecessor blocks, finding the one with the
		 * shortest distance, since that will be the worst case
		 */
		unsigned min = maxd - d;

		/* (ab)use block->data to prevent recursion: */
		block->data = block;

		set_foreach (block->predecessors, entry) {
			struct ir3_block *pred = (struct ir3_block *)entry->key;
			unsigned n;

			n = distance(pred, instr, min, pred);

			min = MIN2(min, n);
		}

		block->data = NULL;
		d += min;
	}

	return d;
}

/* calculate delay for specified src: */
static unsigned
delay_calc_srcn(struct ir3_block *block,
		struct ir3_instruction *assigner,
		struct ir3_instruction *consumer,
		unsigned srcn, bool soft, bool pred)
{
	unsigned delay = 0;

	if (is_meta(assigner)) {
		foreach_src_n (src, n, assigner) {
			unsigned d;

			if (!src->instr)
				continue;

			d = delay_calc_srcn(block, src->instr, consumer, srcn, soft, pred);

			/* A (rptN) instruction executes in consecutive cycles so
			 * it's outputs are written in successive cycles.  And
			 * likewise for it's (r)'d (incremented) inputs, they are
			 * read on successive cycles.
			 *
			 * So we need to adjust the delay for (rptN)'s assigners
			 * and consumers accordingly.
			 *
			 * Note that the dst of a (rptN) instruction is implicitly
			 * (r) (the assigner case), although that is not the case
			 * for src registers.  There is exactly one case, bary.f,
			 * which has a vecN (collect) src that is not (r)'d.
			 */
			if ((assigner->opc == OPC_META_SPLIT) && src->instr->repeat) {
				/* (rptN) assigner case: */
				d -= MIN2(d, src->instr->repeat - assigner->split.off);
			} else if ((assigner->opc == OPC_META_COLLECT) && consumer->repeat &&
					(consumer->regs[srcn]->flags & IR3_REG_R)) {
				d -= MIN2(d, n);
			}

			delay = MAX2(delay, d);
		}
	} else {
		delay = ir3_delayslots(assigner, consumer, srcn, soft);
		delay -= distance(block, assigner, delay, pred);
	}

	return delay;
}

static struct ir3_instruction *
find_array_write(struct ir3_block *block, unsigned array_id, unsigned maxd)
{
	unsigned d = 0;

	/* Note that this relies on incrementally building up the block's
	 * instruction list.. but this is how scheduling and nopsched
	 * work.
	 */
	foreach_instr_rev (n, &block->instr_list) {
		if (d >= maxd)
			return NULL;
		if (count_instruction(n))
			d++;
		if (dest_regs(n) == 0)
			continue;

		/* note that a dest reg will never be an immediate */
		if (n->regs[0]->array.id == array_id)
			return n;
	}

	return NULL;
}

/* like list_length() but only counts instructions which count in the
 * delay determination:
 */
static unsigned
count_block_delay(struct ir3_block *block)
{
	unsigned delay = 0;
	foreach_instr (n, &block->instr_list) {
		if (!count_instruction(n))
			continue;
		delay++;
	}
	return delay;
}

static unsigned
delay_calc_array(struct ir3_block *block, unsigned array_id,
		struct ir3_instruction *consumer, unsigned srcn,
		bool soft, bool pred, unsigned maxd)
{
	struct ir3_instruction *assigner;

	assigner = find_array_write(block, array_id, maxd);
	if (assigner)
		return delay_calc_srcn(block, assigner, consumer, srcn, soft, pred);

	if (!pred)
		return 0;

	unsigned len = count_block_delay(block);
	if (maxd <= len)
		return 0;

	maxd -= len;

	if (block->data == block) {
		/* we have a loop, return worst case: */
		return maxd;
	}

	/* If we need to search into predecessors, find the one with the
	 * max delay.. the resulting delay is that minus the number of
	 * counted instructions in this block:
	 */
	unsigned max = 0;

	/* (ab)use block->data to prevent recursion: */
	block->data = block;

	set_foreach (block->predecessors, entry) {
		struct ir3_block *pred = (struct ir3_block *)entry->key;
		unsigned delay =
			delay_calc_array(pred, array_id, consumer, srcn, soft, pred, maxd);

		max = MAX2(max, delay);
	}

	block->data = NULL;

	if (max < len)
		return 0;

	return max - len;
}

/**
 * Calculate delay for instruction (maximum of delay for all srcs):
 *
 * @soft:  If true, add additional delay for situations where they
 *    would not be strictly required because a sync flag would be
 *    used (but scheduler would prefer to schedule some other
 *    instructions first to avoid stalling on sync flag)
 * @pred:  If true, recurse into predecessor blocks
 */
unsigned
ir3_delay_calc(struct ir3_block *block, struct ir3_instruction *instr,
		bool soft, bool pred)
{
	unsigned delay = 0;

	foreach_src_n (src, i, instr) {
		unsigned d = 0;

		if ((src->flags & IR3_REG_RELATIV) && !(src->flags & IR3_REG_CONST)) {
			d = delay_calc_array(block, src->array.id, instr, i+1, soft, pred, 6);
		} else if (src->instr) {
			d = delay_calc_srcn(block, src->instr, instr, i+1, soft, pred);
		}

		delay = MAX2(delay, d);
	}

	if (instr->address) {
		unsigned d = delay_calc_srcn(block, instr->address, instr, 0, soft, pred);
		delay = MAX2(delay, d);
	}

	return delay;
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
