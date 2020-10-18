/*
 * Copyright © 2020 Google, Inc.
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

#include "util/ralloc.h"
#include "util/u_dynarray.h"

#include "ir3.h"

/**
 * A bit more extra cleanup after sched pass.  In particular, prior to
 * instruction scheduling, we can't easily eliminate unneeded mov's
 * from "arrays", because we don't yet know if there is an intervening
 * array-write scheduled before the use of the array-read.
 *
 * NOTE array is equivalent to nir "registers".. ie. it can be length of
 * one.  It is basically anything that is not SSA.
 */


/**
 * Check if any instruction before `use` and after `src` writes to the
 * specified array.  If `offset` is negative, it is a relative (a0.x)
 * access and we care about all writes to the array (as we don't know
 * which array element is read).  Otherwise in the case of non-relative
 * access, we only have to care about the write to the specified (>= 0)
 * offset.
 */
static bool
has_conflicting_write(struct ir3_instruction *src,
		struct ir3_instruction *use,
		unsigned id, int offset)
{
	assert(src->block == use->block);

	/* NOTE that since src and use are in the same block, src by
	 * definition appears in the block's instr_list before use:
	 */
	foreach_instr_rev (instr, &use->node) {
		if (instr == src)
			break;

		/* if we are looking at a RELATIV read, we can't move
		 * it past an a0.x write:
		 */
		if ((offset < 0) && (dest_regs(instr) > 0) &&
				(instr->regs[0]->num == regid(REG_A0, 0)))
			return true;

		if (!writes_gpr(instr))
			continue;

		struct ir3_register *dst = instr->regs[0];
		if (!(dst->flags & IR3_REG_ARRAY))
			continue;

		if (dst->array.id != id)
			continue;

		/*
		 * At this point, we have narrowed down an instruction
		 * that writes to the same array.. check if it the write
		 * is to an array element that we care about:
		 */

		/* is write to an unknown array element? */
		if (dst->flags & IR3_REG_RELATIV)
			return true;

		/* is read from an unknown array element? */
		if (offset < 0)
			return true;

		/* is write to same array element? */
		if (dst->array.offset == offset)
			return true;
	}

	return false;
}

/* Can we fold the mov src into use without invalid flags? */
static bool
valid_flags(struct ir3_instruction *use, struct ir3_instruction *mov)
{
	struct ir3_register *src = mov->regs[1];

	foreach_src_n (reg, n, use) {
		if (ssa(reg) != mov)
			continue;

		if (!ir3_valid_flags(use, n, reg->flags | src->flags))
			return false;
	}

	return true;
}

static bool
instr_cp_postsched(struct ir3_instruction *mov)
{
	struct ir3_register *src = mov->regs[1];

	/* only consider mov's from "arrays", other cases we have
	 * already considered already:
	 */
	if (!(src->flags & IR3_REG_ARRAY))
		return false;

	int offset = (src->flags & IR3_REG_RELATIV) ? -1 : src->array.offset;

	/* Once we move the array read directly into the consuming
	 * instruction(s), we will also need to update instructions
	 * that had a false-dep on the original mov to have deps
	 * on the consuming instructions:
	 */
	struct util_dynarray newdeps;
	util_dynarray_init(&newdeps, mov->uses);

	foreach_ssa_use (use, mov) {
		if (use->block != mov->block)
			continue;

		if (is_meta(use))
			continue;

		if (has_conflicting_write(mov, use, src->array.id, offset))
			continue;

		if (conflicts(mov->address, use->address))
			continue;

		if (!valid_flags(use, mov))
			continue;

		/* Ok, we've established that it is safe to remove this copy: */

		bool removed = false;
		foreach_src_n (reg, n, use) {
			if (ssa(reg) != mov)
				continue;

			use->regs[n + 1] = ir3_reg_clone(mov->block->shader, src);

			/* preserve (abs)/etc modifiers: */
			use->regs[n + 1]-> flags |= reg->flags;

			removed = true;
		}

		/* the use could have been only a false-dep, only add to
		 * the newdeps array if we've actually updated a real
		 * src reg for the use:
		 */
		if (removed) {
			util_dynarray_append(&newdeps, struct ir3_instruction *, use);

			/* Remove the use from the src instruction: */
			_mesa_set_remove_key(mov->uses, use);
		}
	}

	/* Once we have the complete set of instruction(s) that are are now
	 * directly reading from the array, update any false-dep uses to
	 * now depend on these instructions.  The only remaining uses at
	 * this point should be false-deps:
	 */
	foreach_ssa_use (use, mov) {
		util_dynarray_foreach(&newdeps, struct ir3_instruction *, instrp) {
			struct ir3_instruction *newdep = *instrp;
			ir3_instr_add_dep(use, newdep);
		}
	}

	return util_dynarray_num_elements(&newdeps, struct ir3_instruction **) > 0;
}

bool
ir3_cp_postsched(struct ir3 *ir)
{
	void *mem_ctx = ralloc_context(NULL);
	bool progress = false;

	ir3_find_ssa_uses(ir, mem_ctx, false);

	foreach_block (block, &ir->block_list) {
		foreach_instr_safe (instr, &block->instr_list) {
			if (is_same_type_mov(instr))
				progress |= instr_cp_postsched(instr);
		}
	}

	ralloc_free(mem_ctx);

	return progress;
}
