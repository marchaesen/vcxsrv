/*
 * Copyright (C) 2019 Google.
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

#include "ir3.h"

static bool
is_fp16_conv(struct ir3_instruction *instr)
{
	if (instr->opc != OPC_MOV)
		return false;

	struct ir3_register *dst = instr->regs[0];
	struct ir3_register *src = instr->regs[1];

	/* disallow conversions that cannot be folded into
	 * alu instructions:
	 */
	if (dst->flags & (IR3_REG_EVEN | IR3_REG_POS_INF))
		return false;

	if (dst->flags & (IR3_REG_RELATIV | IR3_REG_ARRAY))
		return false;
	if (src->flags & (IR3_REG_RELATIV | IR3_REG_ARRAY))
		return false;

	if (instr->cat1.src_type == TYPE_F32 &&
			instr->cat1.dst_type == TYPE_F16)
		return true;

	if (instr->cat1.src_type == TYPE_F16 &&
			instr->cat1.dst_type == TYPE_F32)
		return true;

	return false;
}

static bool
all_uses_fp16_conv(struct ir3_instruction *conv_src)
{
	foreach_ssa_use (use, conv_src)
		if (!is_fp16_conv(use))
			return false;
	return true;
}

/* For an instruction which has a conversion folded in, re-write the
 * uses of *all* conv's that used that src to be a simple mov that
 * cp can eliminate.  This avoids invalidating the SSA uses, it just
 * shifts the use to a simple mov.
 */
static void
rewrite_src_uses(struct ir3_instruction *src)
{
	foreach_ssa_use (use, src) {
		assert(is_fp16_conv(use));

		if (is_half(src)) {
			use->regs[1]->flags |= IR3_REG_HALF;
		} else {
			use->regs[1]->flags &= ~IR3_REG_HALF;
		}

		use->cat1.src_type = use->cat1.dst_type;
	}
}

static bool
try_conversion_folding(struct ir3_instruction *conv)
{
	struct ir3_instruction *src;

	if (!is_fp16_conv(conv))
		return false;

	/* NOTE: we can have non-ssa srcs after copy propagation: */
	src = ssa(conv->regs[1]);
	if (!src)
		return false;

	if (!is_alu(src))
		return false;

	/* avoid folding f2f32(f2f16) together, in cases where this is legal to
	 * do (glsl) nir should have handled that for us already:
	 */
	if (is_fp16_conv(src))
		return false;

	switch (src->opc) {
	case OPC_SEL_B32:
	case OPC_SEL_B16:
	case OPC_MAX_F:
	case OPC_MIN_F:
	case OPC_SIGN_F:
	case OPC_ABSNEG_F:
		return false;
	case OPC_MOV:
		/* if src is a "cov" and type doesn't match, then it can't be folded
		 * for example cov.u32u16+cov.f16f32 can't be folded to cov.u32f32
		 */
		if (src->cat1.dst_type != src->cat1.src_type &&
			conv->cat1.src_type != src->cat1.dst_type)
			return false;
		break;
	default:
		break;
	}

	if (!all_uses_fp16_conv(src))
		return false;

	if (src->opc == OPC_MOV) {
		if (src->cat1.dst_type == src->cat1.src_type) {
			/* If we're folding a conversion into a bitwise move, we need to
			 * change the dst type to F32 to get the right behavior, since we
			 * could be moving a float with a u32.u32 move.
			 */
			src->cat1.dst_type = conv->cat1.dst_type;
			src->cat1.src_type = conv->cat1.src_type;
		} else {
			/* Otherwise, for typechanging movs, we can just change the dst
			 * type to F16 to collaps the two conversions.  For example
			 * cov.s32f32 follwed by cov.f32f16 becomes cov.s32f16.
			 */
			src->cat1.dst_type = conv->cat1.dst_type;
		}
	}

	ir3_set_dst_type(src, is_half(conv));
	rewrite_src_uses(src);

	return true;
}

bool
ir3_cf(struct ir3 *ir)
{
	void *mem_ctx = ralloc_context(NULL);
	bool progress = false;

	ir3_find_ssa_uses(ir, mem_ctx, false);

	foreach_block (block, &ir->block_list) {
		foreach_instr (instr, &block->instr_list) {
			progress |= try_conversion_folding(instr);
		}
	}

	ralloc_free(mem_ctx);

	return progress;
}
