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

#include <stdlib.h>

#include "util/ralloc.h"

#include "ir3.h"

struct ir3_validate_ctx {
	struct ir3 *ir;

	/* Current instruction being validated: */
	struct ir3_instruction *current_instr;

	/* Set of instructions found so far, used to validate that we
	 * don't have SSA uses that occure before def's
	 */
	struct set *defs;
};

static void
validate_error(struct ir3_validate_ctx *ctx, const char *condstr)
{
	fprintf(stderr, "validation fail: %s\n", condstr);
	fprintf(stderr, "  -> for instruction: ");
	ir3_print_instr(ctx->current_instr);
	abort();
}

#define validate_assert(ctx, cond) do { \
	if (!(cond)) { \
		validate_error(ctx, #cond); \
	} } while (0)

static unsigned
reg_class_flags(struct ir3_register *reg)
{
	return reg->flags & (IR3_REG_HALF | IR3_REG_HIGH);
}

static void
validate_src(struct ir3_validate_ctx *ctx, struct ir3_register *reg)
{
	struct ir3_instruction *src = ssa(reg);

	if (!src)
		return;

	validate_assert(ctx, _mesa_set_search(ctx->defs, src));
	validate_assert(ctx, src->regs[0]->wrmask == reg->wrmask);
	validate_assert(ctx, reg_class_flags(src->regs[0]) == reg_class_flags(reg));
}

static void
validate_instr(struct ir3_validate_ctx *ctx, struct ir3_instruction *instr)
{
	struct ir3_register *last_reg = NULL;

	if (writes_gpr(instr)) {
		if (instr->regs[0]->flags & IR3_REG_RELATIV) {
			validate_assert(ctx, instr->address);
		}
	}

	foreach_src_n (reg, n, instr) {
		if (reg->flags & IR3_REG_RELATIV)
			validate_assert(ctx, instr->address);

		validate_src(ctx, reg);

		/* Validate that all src's are either half of full.
		 *
		 * Note: tex instructions w/ .s2en are a bit special in that the
		 * tex/samp src reg is half-reg for non-bindless and full for
		 * bindless, irrespective of the precision of other srcs. The
		 * tex/samp src is the first src reg when .s2en is set
		 */
		if ((instr->flags & IR3_INSTR_S2EN) && (n < 2)) {
			if (n == 0) {
				if (instr->flags & IR3_INSTR_B)
					validate_assert(ctx, !(reg->flags & IR3_REG_HALF));
				else
					validate_assert(ctx, reg->flags & IR3_REG_HALF);
			}
		} else if (n > 0) {
			validate_assert(ctx, (last_reg->flags & IR3_REG_HALF) == (reg->flags & IR3_REG_HALF));
		}

		last_reg = reg;
	}

	_mesa_set_add(ctx->defs, instr);

	/* Check that src/dst types match the register types, and for
	 * instructions that have different opcodes depending on type,
	 * that the opcodes are correct.
	 */
	switch (opc_cat(instr->opc)) {
	case 1: /* move instructions */
		if (instr->regs[0]->flags & IR3_REG_HALF) {
			validate_assert(ctx, instr->cat1.dst_type == half_type(instr->cat1.dst_type));
		} else {
			validate_assert(ctx, instr->cat1.dst_type == full_type(instr->cat1.dst_type));
		}
		if (instr->regs[1]->flags & IR3_REG_HALF) {
			validate_assert(ctx, instr->cat1.src_type == half_type(instr->cat1.src_type));
		} else {
			validate_assert(ctx, instr->cat1.src_type == full_type(instr->cat1.src_type));
		}
		break;
	case 3:
		/* Validate that cat3 opc matches the src type.  We've already checked that all
		 * the src regs are same type
		 */
		if (instr->regs[1]->flags & IR3_REG_HALF) {
			validate_assert(ctx, instr->opc == cat3_half_opc(instr->opc));
		} else {
			validate_assert(ctx, instr->opc == cat3_full_opc(instr->opc));
		}
		break;
	case 4:
		/* Validate that cat4 opc matches the dst type: */
		if (instr->regs[0]->flags & IR3_REG_HALF) {
			validate_assert(ctx, instr->opc == cat4_half_opc(instr->opc));
		} else {
			validate_assert(ctx, instr->opc == cat4_full_opc(instr->opc));
		}
		break;
	case 5:
		if (instr->regs[0]->flags & IR3_REG_HALF) {
			validate_assert(ctx, instr->cat5.type == half_type(instr->cat5.type));
		} else {
			validate_assert(ctx, instr->cat5.type == full_type(instr->cat5.type));
		}
		break;
	}
}

void
ir3_validate(struct ir3 *ir)
{
#ifdef NDEBUG
#  define VALIDATE 0
#else
#  define VALIDATE 1
#endif

	if (!VALIDATE)
		return;

	struct ir3_validate_ctx *ctx = ralloc_size(NULL, sizeof(*ctx));

	ctx->ir = ir;
	ctx->defs = _mesa_pointer_set_create(ctx);

	foreach_block (block, &ir->block_list) {
		foreach_instr (instr, &block->instr_list) {
			ctx->current_instr = instr;
			validate_instr(ctx, instr);
		}
	}

	ralloc_free(ctx);
}
