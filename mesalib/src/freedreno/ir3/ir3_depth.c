/*
 * Copyright (C) 2014 Rob Clark <robclark@freedesktop.org>
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

#include "util/u_math.h"

#include "ir3.h"

/*
 * Instruction Depth:
 *
 * Calculates weighted instruction depth, ie. the sum of # of needed
 * instructions plus delay slots back to original input (ie INPUT or
 * CONST).  That is to say, an instructions depth is:
 *
 *   depth(instr) {
 *     d = 0;
 *     // for each src register:
 *     foreach (src in instr->regs[1..n])
 *       d = max(d, delayslots(src->instr, n) + depth(src->instr));
 *     return d + 1;
 *   }
 *
 * After an instruction's depth is calculated, it is inserted into the
 * blocks depth sorted list, which is used by the scheduling pass.
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
		struct ir3_register *src;

		debug_assert(dst->flags & IR3_REG_ARRAY);

		foreach_src(src, consumer) {
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
int ir3_delayslots(struct ir3_instruction *assigner,
		struct ir3_instruction *consumer, unsigned n)
{
	if (ignore_dep(assigner, consumer, n))
		return 0;

	/* worst case is cat1-3 (alu) -> cat4/5 needing 6 cycles, normal
	 * alu -> alu needs 3 cycles, cat4 -> alu and texture fetch
	 * handled with sync bits
	 */

	if (is_meta(assigner))
		return 0;

	if (writes_addr(assigner))
		return 6;

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

void
ir3_insert_by_depth(struct ir3_instruction *instr, struct list_head *list)
{
	/* remove from existing spot in list: */
	list_delinit(&instr->node);

	/* find where to re-insert instruction: */
	list_for_each_entry (struct ir3_instruction, pos, list, node) {
		if (pos->depth > instr->depth) {
			list_add(&instr->node, &pos->node);
			return;
		}
	}
	/* if we get here, we didn't find an insertion spot: */
	list_addtail(&instr->node, list);
}

static void
ir3_instr_depth(struct ir3_instruction *instr, unsigned boost, bool falsedep)
{
	struct ir3_instruction *src;

	/* don't mark falsedep's as used, but otherwise process them normally: */
	if (!falsedep)
		instr->flags &= ~IR3_INSTR_UNUSED;

	if (ir3_instr_check_mark(instr))
		return;

	instr->depth = 0;

	foreach_ssa_src_n(src, i, instr) {
		unsigned sd;

		/* visit child to compute it's depth: */
		ir3_instr_depth(src, boost, __is_false_dep(instr, i));

		/* for array writes, no need to delay on previous write: */
		if (i == 0)
			continue;

		sd = ir3_delayslots(src, instr, i) + src->depth;
		sd += boost;

		instr->depth = MAX2(instr->depth, sd);
	}

	if (!is_meta(instr))
		instr->depth++;

	ir3_insert_by_depth(instr, &instr->block->instr_list);
}

static bool
remove_unused_by_block(struct ir3_block *block)
{
	bool progress = false;
	list_for_each_entry_safe (struct ir3_instruction, instr, &block->instr_list, node) {
		if (instr->opc == OPC_END)
			continue;
		if (instr->flags & IR3_INSTR_UNUSED) {
			list_delinit(&instr->node);
			progress = true;
		}
	}
	return progress;
}

static bool
compute_depth_and_remove_unused(struct ir3 *ir)
{
	unsigned i;
	bool progress = false;

	ir3_clear_mark(ir);

	/* initially mark everything as unused, we'll clear the flag as we
	 * visit the instructions:
	 */
	list_for_each_entry (struct ir3_block, block, &ir->block_list, node) {
		list_for_each_entry (struct ir3_instruction, instr, &block->instr_list, node) {
			instr->flags |= IR3_INSTR_UNUSED;
		}
	}

	for (i = 0; i < ir->noutputs; i++)
		if (ir->outputs[i])
			ir3_instr_depth(ir->outputs[i], 0, false);

	list_for_each_entry (struct ir3_block, block, &ir->block_list, node) {
		for (i = 0; i < block->keeps_count; i++)
			ir3_instr_depth(block->keeps[i], 0, false);

		/* We also need to account for if-condition: */
		if (block->condition)
			ir3_instr_depth(block->condition, 6, false);
	}

	/* mark un-used instructions: */
	list_for_each_entry (struct ir3_block, block, &ir->block_list, node) {
		progress |= remove_unused_by_block(block);
	}

	/* note that we can end up with unused indirects, but we should
	 * not end up with unused predicates.
	 */
	for (i = 0; i < ir->indirects_count; i++) {
		struct ir3_instruction *instr = ir->indirects[i];
		if (instr && (instr->flags & IR3_INSTR_UNUSED))
			ir->indirects[i] = NULL;
	}

	/* cleanup unused inputs: */
	for (i = 0; i < ir->ninputs; i++) {
		struct ir3_instruction *in = ir->inputs[i];
		if (in && (in->flags & IR3_INSTR_UNUSED))
			ir->inputs[i] = NULL;
	}

	return progress;
}

void
ir3_depth(struct ir3 *ir)
{
	bool progress;
	do {
		progress = compute_depth_and_remove_unused(ir);
	} while (progress);
}
