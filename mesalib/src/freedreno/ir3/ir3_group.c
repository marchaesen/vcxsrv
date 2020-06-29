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

#include "ir3.h"

/*
 * Find/group instruction neighbors:
 */

static void
insert_mov(struct ir3_instruction *collect, int idx)
{
	struct ir3_instruction *src = ssa(collect->regs[idx+1]);
	struct ir3_instruction *mov = ir3_MOV(src->block, src,
		(collect->regs[idx+1]->flags & IR3_REG_HALF) ? TYPE_U16 : TYPE_U32);

	collect->regs[idx+1]->instr = mov;

	/* if collect and src are in the same block, move the inserted mov
	 * to just before the collect to avoid a use-before-def.  Otherwise
	 * it should be safe to leave at the end of the block it is in:
	 */
	if (src->block == collect->block) {
		ir3_instr_move_before(mov, collect);
	}
}

/* verify that cur != instr, but cur is also not in instr's neighbor-list: */
static bool
in_neighbor_list(struct ir3_instruction *instr, struct ir3_instruction *cur, int pos)
{
	int idx = 0;

	if (!instr)
		return false;

	if (instr == cur)
		return true;

	for (instr = ir3_neighbor_first(instr); instr; instr = instr->cp.right)
		if ((idx++ != pos) && (instr == cur))
			return true;

	return false;
}

static void
group_collect(struct ir3_instruction *collect)
{
	struct ir3_register **regs = &collect->regs[1];
	unsigned n = collect->regs_count - 1;

	/* first pass, figure out what has conflicts and needs a mov
	 * inserted.  Do this up front, before starting to setup
	 * left/right neighbor pointers.  Trying to do it in a single
	 * pass could result in a situation where we can't even setup
	 * the mov's right neighbor ptr if the next instr also needs
	 * a mov.
	 */
restart:
	for (unsigned i = 0; i < n; i++) {
		struct ir3_instruction *instr = ssa(regs[i]);
		if (instr) {
			struct ir3_instruction *left = (i > 0) ? ssa(regs[i - 1]) : NULL;
			struct ir3_instruction *right = (i < (n-1)) ? ssa(regs[i + 1]) : NULL;
			bool conflict;

			/* check for left/right neighbor conflicts: */
			conflict = conflicts(instr->cp.left, left) ||
				conflicts(instr->cp.right, right);

			/* Mixing array elements and higher register classes
			 * (ie. groups) doesn't really work out in RA.  See:
			 *
			 * https://trello.com/c/DqeDkeVf/156-bug-with-stk-70frag
			 */
			if (instr->regs[0]->flags & IR3_REG_ARRAY)
				conflict = true;

			/* we also can't have an instr twice in the group: */
			for (unsigned j = i + 1; (j < n) && !conflict; j++)
				if (in_neighbor_list(ssa(regs[j]), instr, i))
					conflict = true;

			if (conflict) {
				insert_mov(collect, i);
				/* inserting the mov may have caused a conflict
				 * against the previous:
				 */
				goto restart;
			}
		}
	}

	/* second pass, now that we've inserted mov's, fixup left/right
	 * neighbors.  This is guaranteed to succeed, since by definition
	 * the newly inserted mov's cannot conflict with anything.
	 */
	for (unsigned i = 0; i < n; i++) {
		struct ir3_instruction *instr = ssa(regs[i]);
		if (instr) {
			struct ir3_instruction *left = (i > 0) ? ssa(regs[i - 1]) : NULL;
			struct ir3_instruction *right = (i < (n-1)) ? ssa(regs[i + 1]) : NULL;

			debug_assert(!conflicts(instr->cp.left, left));
			if (left) {
				instr->cp.left_cnt++;
				instr->cp.left = left;
			}

			debug_assert(!conflicts(instr->cp.right, right));
			if (right) {
				instr->cp.right_cnt++;
				instr->cp.right = right;
			}
		}
	}
}

static bool
instr_find_neighbors(struct ir3_instruction *instr)
{
	bool progress = false;

	if (ir3_instr_check_mark(instr))
		return false;

	if (instr->opc == OPC_META_COLLECT) {
		group_collect(instr);
		progress = true;
	}

	foreach_ssa_src (src, instr)
		progress |= instr_find_neighbors(src);

	return progress;
}

static bool
find_neighbors(struct ir3 *ir)
{
	bool progress = false;
	unsigned i;

	foreach_output (out, ir)
		progress |= instr_find_neighbors(out);

	foreach_block (block, &ir->block_list) {
		for (i = 0; i < block->keeps_count; i++) {
			struct ir3_instruction *instr = block->keeps[i];
			progress |= instr_find_neighbors(instr);
		}

		/* We also need to account for if-condition: */
		if (block->condition)
			progress |= instr_find_neighbors(block->condition);
	}

	return progress;
}

bool
ir3_group(struct ir3 *ir)
{
	ir3_clear_mark(ir);
	return find_neighbors(ir);
}
