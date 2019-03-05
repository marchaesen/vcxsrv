/*
 * Copyright (C) 2018 Rob Clark <robclark@freedesktop.org>
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
 * A simple pass to do Sethi–Ullman numbering, as described in "Generalizations
 * of the Sethi-Ullman algorithm for register allocation"[1].  This is used by
 * the scheduler pass.
 *
 * TODO this could probably be more clever about flow control, ie. if a src
 * is computed in multiple paths into a block, I think we should only have to
 * consider the worst-case.
 *
 * [1] https://pdfs.semanticscholar.org/ae53/6010b214612c2571f483354c264b0b39c545.pdf
 */

static unsigned
number_instr(struct ir3_instruction *instr)
{
	if (ir3_instr_check_mark(instr))
		return instr->sun;

	struct ir3_instruction *src;
	const unsigned n = __ssa_src_cnt(instr);
	unsigned a[n];
	unsigned b[n];
	unsigned i = 0;

	/* TODO I think including false-deps in the calculation is the right
	 * thing to do:
	 */
	foreach_ssa_src_n(src, n, instr) {
		if (__is_false_dep(instr, n))
			continue;
		if (src->block != instr->block) {
			a[i] = 1;
		} else {
			a[i] = number_instr(src);
		}
		b[i] = dest_regs(src);
		i++;
	}

	/*
	 * Rπ = max(aπ(1), bπ(1) + max(aπ(2), bπ(2) + max(..., bπ(k−1) + max(aπ(k), bπ(k)))...):
	 */
	unsigned last_r = 0;

	for (int k = i - 1; k >= 0; k--) {
		unsigned r = MAX2(a[k], b[k] + last_r);

		if (k > 0)
			r += b[k-1];

		last_r = r;
	}

	last_r = MAX2(last_r, dest_regs(instr));

	instr->sun = last_r;

	return instr->sun;
}

void
ir3_sun(struct ir3 *ir)
{
	unsigned max = 0;

	ir3_clear_mark(ir);

	for (unsigned i = 0; i < ir->noutputs; i++)
		if (ir->outputs[i])
			max = MAX2(max, number_instr(ir->outputs[i]));

	list_for_each_entry (struct ir3_block, block, &ir->block_list, node) {
		for (unsigned i = 0; i < block->keeps_count; i++)
			max = MAX2(max, number_instr(block->keeps[i]));
		if (block->condition)
			max = MAX2(max, number_instr(block->condition));
	}

	ir->max_sun = max;
}
