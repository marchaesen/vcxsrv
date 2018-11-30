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

/* bleh.. we need to do the same group_n() thing for both inputs/outputs
 * (where we have a simple instr[] array), and fanin nodes (where we have
 * an extra indirection via reg->instr).
 */
struct group_ops {
	struct ir3_instruction *(*get)(void *arr, int idx);
	void (*insert_mov)(void *arr, int idx, struct ir3_instruction *instr);
};

static struct ir3_instruction *arr_get(void *arr, int idx)
{
	return ((struct ir3_instruction **)arr)[idx];
}
static void arr_insert_mov_out(void *arr, int idx, struct ir3_instruction *instr)
{
	((struct ir3_instruction **)arr)[idx] =
			ir3_MOV(instr->block, instr, TYPE_F32);
}
static void arr_insert_mov_in(void *arr, int idx, struct ir3_instruction *instr)
{
	/* so, we can't insert a mov in front of a meta:in.. and the downstream
	 * instruction already has a pointer to 'instr'.  So we cheat a bit and
	 * morph the meta:in instruction into a mov and insert a new meta:in
	 * in front.
	 */
	struct ir3_instruction *in;

	debug_assert(instr->regs_count == 1);

	in = ir3_instr_create(instr->block, OPC_META_INPUT);
	in->inout.block = instr->block;
	ir3_reg_create(in, instr->regs[0]->num, 0);

	/* create src reg for meta:in and fixup to now be a mov: */
	ir3_reg_create(instr, 0, IR3_REG_SSA)->instr = in;
	instr->opc = OPC_MOV;
	instr->cat1.src_type = TYPE_F32;
	instr->cat1.dst_type = TYPE_F32;

	((struct ir3_instruction **)arr)[idx] = in;
}
static struct group_ops arr_ops_out = { arr_get, arr_insert_mov_out };
static struct group_ops arr_ops_in = { arr_get, arr_insert_mov_in };

static struct ir3_instruction *instr_get(void *arr, int idx)
{
	return ssa(((struct ir3_instruction *)arr)->regs[idx+1]);
}
static void
instr_insert_mov(void *arr, int idx, struct ir3_instruction *instr)
{
	((struct ir3_instruction *)arr)->regs[idx+1]->instr =
			ir3_MOV(instr->block, instr, TYPE_F32);
}
static struct group_ops instr_ops = { instr_get, instr_insert_mov };

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
group_n(struct group_ops *ops, void *arr, unsigned n)
{
	unsigned i, j;

	/* first pass, figure out what has conflicts and needs a mov
	 * inserted.  Do this up front, before starting to setup
	 * left/right neighbor pointers.  Trying to do it in a single
	 * pass could result in a situation where we can't even setup
	 * the mov's right neighbor ptr if the next instr also needs
	 * a mov.
	 */
restart:
	for (i = 0; i < n; i++) {
		struct ir3_instruction *instr = ops->get(arr, i);
		if (instr) {
			struct ir3_instruction *left = (i > 0) ? ops->get(arr, i - 1) : NULL;
			struct ir3_instruction *right = (i < (n-1)) ? ops->get(arr, i + 1) : NULL;
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
			for (j = i + 1; (j < n) && !conflict; j++)
				if (in_neighbor_list(ops->get(arr, j), instr, i))
					conflict = true;

			if (conflict) {
				ops->insert_mov(arr, i, instr);
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
	for (i = 0; i < n; i++) {
		struct ir3_instruction *instr = ops->get(arr, i);
		if (instr) {
			struct ir3_instruction *left = (i > 0) ? ops->get(arr, i - 1) : NULL;
			struct ir3_instruction *right = (i < (n-1)) ? ops->get(arr, i + 1) : NULL;

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

static void
instr_find_neighbors(struct ir3_instruction *instr)
{
	struct ir3_instruction *src;

	if (ir3_instr_check_mark(instr))
		return;

	if (instr->opc == OPC_META_FI)
		group_n(&instr_ops, instr, instr->regs_count - 1);

	foreach_ssa_src(src, instr)
		instr_find_neighbors(src);
}

/* a bit of sadness.. we can't have "holes" in inputs from PoV of
 * register assignment, they still need to be grouped together.  So
 * we need to insert dummy/padding instruction for grouping, and
 * then take it back out again before anyone notices.
 */
static void
pad_and_group_input(struct ir3_instruction **input, unsigned n)
{
	int i, mask = 0;
	struct ir3_block *block = NULL;

	for (i = n - 1; i >= 0; i--) {
		struct ir3_instruction *instr = input[i];
		if (instr) {
			block = instr->block;
		} else if (block) {
			instr = ir3_NOP(block);
			ir3_reg_create(instr, 0, IR3_REG_SSA);    /* dummy dst */
			input[i] = instr;
			mask |= (1 << i);
		}
	}

	group_n(&arr_ops_in, input, n);

	for (i = 0; i < n; i++) {
		if (mask & (1 << i))
			input[i] = NULL;
	}
}

static void
find_neighbors(struct ir3 *ir)
{
	unsigned i;

	/* shader inputs/outputs themselves must be contiguous as well:
	 *
	 * NOTE: group inputs first, since we only insert mov's
	 * *before* the conflicted instr (and that would go badly
	 * for inputs).  By doing inputs first, we should never
	 * have a conflict on inputs.. pushing any conflict to
	 * resolve to the outputs, for stuff like:
	 *
	 *     MOV OUT[n], IN[m].wzyx
	 *
	 * NOTE: we assume here inputs/outputs are grouped in vec4.
	 * This logic won't quite cut it if we don't align smaller
	 * on vec4 boundaries
	 */
	for (i = 0; i < ir->ninputs; i += 4)
		pad_and_group_input(&ir->inputs[i], 4);
	for (i = 0; i < ir->noutputs; i += 4)
		group_n(&arr_ops_out, &ir->outputs[i], 4);

	for (i = 0; i < ir->noutputs; i++) {
		if (ir->outputs[i]) {
			struct ir3_instruction *instr = ir->outputs[i];
			instr_find_neighbors(instr);
		}
	}

	list_for_each_entry (struct ir3_block, block, &ir->block_list, node) {
		for (i = 0; i < block->keeps_count; i++) {
			struct ir3_instruction *instr = block->keeps[i];
			instr_find_neighbors(instr);
		}

		/* We also need to account for if-condition: */
		if (block->condition)
			instr_find_neighbors(block->condition);
	}
}

void
ir3_group(struct ir3 *ir)
{
	ir3_clear_mark(ir);
	find_neighbors(ir);
}
