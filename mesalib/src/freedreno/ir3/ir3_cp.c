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

#include <math.h>
#include "util/half_float.h"
#include "util/u_math.h"

#include "ir3.h"
#include "ir3_compiler.h"
#include "ir3_shader.h"

#define swap(a, b) \
	do { __typeof(a) __tmp = (a); (a) = (b); (b) = __tmp; } while (0)

/*
 * Copy Propagate:
 */

struct ir3_cp_ctx {
	struct ir3 *shader;
	struct ir3_shader_variant *so;
	bool progress;
};

/* is it a type preserving mov, with ok flags?
 *
 * @instr: the mov to consider removing
 * @dst_instr: the instruction consuming the mov (instr)
 *
 * TODO maybe drop allow_flags since this is only false when dst is
 * NULL (ie. outputs)
 */
static bool is_eligible_mov(struct ir3_instruction *instr,
		struct ir3_instruction *dst_instr, bool allow_flags)
{
	if (is_same_type_mov(instr)) {
		struct ir3_register *dst = instr->regs[0];
		struct ir3_register *src = instr->regs[1];
		struct ir3_instruction *src_instr = ssa(src);

		/* only if mov src is SSA (not const/immed): */
		if (!src_instr)
			return false;

		/* no indirect: */
		if (dst->flags & IR3_REG_RELATIV)
			return false;
		if (src->flags & IR3_REG_RELATIV)
			return false;

		if (src->flags & IR3_REG_ARRAY)
			return false;

		if (!allow_flags)
			if (src->flags & (IR3_REG_FABS | IR3_REG_FNEG |
					IR3_REG_SABS | IR3_REG_SNEG | IR3_REG_BNOT))
				return false;

		/* If src is coming from fanout/split (ie. one component of a
		 * texture fetch, etc) and we have constraints on swizzle of
		 * destination, then skip it.
		 *
		 * We could possibly do a bit better, and copy-propagation if
		 * we can CP all components that are being fanned out.
		 */
		if (src_instr->opc == OPC_META_SPLIT) {
			if (!dst_instr)
				return false;
			if (dst_instr->opc == OPC_META_COLLECT)
				return false;
			if (dst_instr->cp.left || dst_instr->cp.right)
				return false;
		}

		return true;
	}
	return false;
}

/* propagate register flags from src to dst.. negates need special
 * handling to cancel each other out.
 */
static void combine_flags(unsigned *dstflags, struct ir3_instruction *src)
{
	unsigned srcflags = src->regs[1]->flags;

	/* if what we are combining into already has (abs) flags,
	 * we can drop (neg) from src:
	 */
	if (*dstflags & IR3_REG_FABS)
		srcflags &= ~IR3_REG_FNEG;
	if (*dstflags & IR3_REG_SABS)
		srcflags &= ~IR3_REG_SNEG;

	if (srcflags & IR3_REG_FABS)
		*dstflags |= IR3_REG_FABS;
	if (srcflags & IR3_REG_SABS)
		*dstflags |= IR3_REG_SABS;
	if (srcflags & IR3_REG_FNEG)
		*dstflags ^= IR3_REG_FNEG;
	if (srcflags & IR3_REG_SNEG)
		*dstflags ^= IR3_REG_SNEG;
	if (srcflags & IR3_REG_BNOT)
		*dstflags ^= IR3_REG_BNOT;

	*dstflags &= ~IR3_REG_SSA;
	*dstflags |= srcflags & IR3_REG_SSA;
	*dstflags |= srcflags & IR3_REG_CONST;
	*dstflags |= srcflags & IR3_REG_IMMED;
	*dstflags |= srcflags & IR3_REG_RELATIV;
	*dstflags |= srcflags & IR3_REG_ARRAY;
	*dstflags |= srcflags & IR3_REG_HIGH;

	/* if src of the src is boolean we can drop the (abs) since we know
	 * the source value is already a postitive integer.  This cleans
	 * up the absnegs that get inserted when converting between nir and
	 * native boolean (see ir3_b2n/n2b)
	 */
	struct ir3_instruction *srcsrc = ssa(src->regs[1]);
	if (srcsrc && is_bool(srcsrc))
		*dstflags &= ~IR3_REG_SABS;
}

/* Tries lowering an immediate register argument to a const buffer access by
 * adding to the list of immediates to be pushed to the const buffer when
 * switching to this shader.
 */
static bool
lower_immed(struct ir3_cp_ctx *ctx, struct ir3_instruction *instr, unsigned n,
		struct ir3_register *reg, unsigned new_flags)
{
	if (!(new_flags & IR3_REG_IMMED))
		return false;

	new_flags &= ~IR3_REG_IMMED;
	new_flags |= IR3_REG_CONST;

	if (!ir3_valid_flags(instr, n, new_flags))
		return false;

	reg = ir3_reg_clone(ctx->shader, reg);

	/* Half constant registers seems to handle only 32-bit values
	 * within floating-point opcodes. So convert back to 32-bit values.
	 */
	bool f_opcode = (is_cat2_float(instr->opc) ||
			is_cat3_float(instr->opc)) ? true : false;
	if (f_opcode && (new_flags & IR3_REG_HALF))
		reg->uim_val = fui(_mesa_half_to_float(reg->uim_val));

	/* in some cases, there are restrictions on (abs)/(neg) plus const..
	 * so just evaluate those and clear the flags:
	 */
	if (new_flags & IR3_REG_SABS) {
		reg->iim_val = abs(reg->iim_val);
		new_flags &= ~IR3_REG_SABS;
	}

	if (new_flags & IR3_REG_FABS) {
		reg->fim_val = fabs(reg->fim_val);
		new_flags &= ~IR3_REG_FABS;
	}

	if (new_flags & IR3_REG_SNEG) {
		reg->iim_val = -reg->iim_val;
		new_flags &= ~IR3_REG_SNEG;
	}

	if (new_flags & IR3_REG_FNEG) {
		reg->fim_val = -reg->fim_val;
		new_flags &= ~IR3_REG_FNEG;
	}

	/* Reallocate for 4 more elements whenever it's necessary.  Note that ir3
	 * printing relies on having groups of 4 dwords, so we fill the unused
	 * slots with a dummy value.
	 */
	struct ir3_const_state *const_state = ir3_const_state(ctx->so);
	if (const_state->immediates_count == const_state->immediates_size) {
		const_state->immediates = rerzalloc(const_state,
				const_state->immediates,
				__typeof__(const_state->immediates[0]),
				const_state->immediates_size,
				const_state->immediates_size + 4);
		const_state->immediates_size += 4;

		for (int i = const_state->immediates_count; i < const_state->immediates_size; i++)
			const_state->immediates[i] = 0xd0d0d0d0;
	}

	int i;
	for (i = 0; i < const_state->immediates_count; i++) {
		if (const_state->immediates[i] == reg->uim_val)
			break;
	}

	if (i == const_state->immediates_count) {
		/* Add on a new immediate to be pushed, if we have space left in the
		 * constbuf.
		 */
		if (const_state->offsets.immediate + const_state->immediates_count / 4 >=
				ir3_max_const(ctx->so))
			return false;

		const_state->immediates[i] = reg->uim_val;
		const_state->immediates_count++;
	}

	reg->flags = new_flags;
	reg->num = i + (4 * const_state->offsets.immediate);

	instr->regs[n + 1] = reg;

	return true;
}

static void
unuse(struct ir3_instruction *instr)
{
	debug_assert(instr->use_count > 0);

	if (--instr->use_count == 0) {
		struct ir3_block *block = instr->block;

		instr->barrier_class = 0;
		instr->barrier_conflict = 0;

		/* we don't want to remove anything in keeps (which could
		 * be things like array store's)
		 */
		for (unsigned i = 0; i < block->keeps_count; i++) {
			debug_assert(block->keeps[i] != instr);
		}
	}
}

/**
 * Handles the special case of the 2nd src (n == 1) to "normal" mad
 * instructions, which cannot reference a constant.  See if it is
 * possible to swap the 1st and 2nd sources.
 */
static bool
try_swap_mad_two_srcs(struct ir3_instruction *instr, unsigned new_flags)
{
	if (!is_mad(instr->opc))
		return false;

	/* NOTE: pre-swap first two src's before valid_flags(),
	 * which might try to dereference the n'th src:
	 */
	swap(instr->regs[0 + 1], instr->regs[1 + 1]);

	/* cat3 doesn't encode immediate, but we can lower immediate
	 * to const if that helps:
	 */
	if (new_flags & IR3_REG_IMMED) {
		new_flags &= ~IR3_REG_IMMED;
		new_flags |=  IR3_REG_CONST;
	}

	bool valid_swap =
		/* can we propagate mov if we move 2nd src to first? */
		ir3_valid_flags(instr, 0, new_flags) &&
		/* and does first src fit in second slot? */
		ir3_valid_flags(instr, 1, instr->regs[1 + 1]->flags);

	if (!valid_swap) {
		/* put things back the way they were: */
		swap(instr->regs[0 + 1], instr->regs[1 + 1]);
	}   /* otherwise leave things swapped */

	return valid_swap;
}

/**
 * Handle cp for a given src register.  This additionally handles
 * the cases of collapsing immedate/const (which replace the src
 * register with a non-ssa src) or collapsing mov's from relative
 * src (which needs to also fixup the address src reference by the
 * instruction).
 */
static bool
reg_cp(struct ir3_cp_ctx *ctx, struct ir3_instruction *instr,
		struct ir3_register *reg, unsigned n)
{
	struct ir3_instruction *src = ssa(reg);

	if (is_eligible_mov(src, instr, true)) {
		/* simple case, no immed/const/relativ, only mov's w/ ssa src: */
		struct ir3_register *src_reg = src->regs[1];
		unsigned new_flags = reg->flags;

		combine_flags(&new_flags, src);

		if (ir3_valid_flags(instr, n, new_flags)) {
			if (new_flags & IR3_REG_ARRAY) {
				debug_assert(!(reg->flags & IR3_REG_ARRAY));
				reg->array = src_reg->array;
			}
			reg->flags = new_flags;
			reg->instr = ssa(src_reg);

			instr->barrier_class |= src->barrier_class;
			instr->barrier_conflict |= src->barrier_conflict;

			unuse(src);
			reg->instr->use_count++;

			return true;
		}
	} else if ((is_same_type_mov(src) || is_const_mov(src)) &&
			/* cannot collapse const/immed/etc into meta instrs: */
			!is_meta(instr)) {
		/* immed/const/etc cases, which require some special handling: */
		struct ir3_register *src_reg = src->regs[1];
		unsigned new_flags = reg->flags;

		combine_flags(&new_flags, src);

		if (!ir3_valid_flags(instr, n, new_flags)) {
			/* See if lowering an immediate to const would help. */
			if (lower_immed(ctx, instr, n, src_reg, new_flags))
				return true;

			/* special case for "normal" mad instructions, we can
			 * try swapping the first two args if that fits better.
			 *
			 * the "plain" MAD's (ie. the ones that don't shift first
			 * src prior to multiply) can swap their first two srcs if
			 * src[0] is !CONST and src[1] is CONST:
			 */
			if ((n == 1) && try_swap_mad_two_srcs(instr, new_flags)) {
				return true;
			} else {
				return false;
			}
		}

		/* Here we handle the special case of mov from
		 * CONST and/or RELATIV.  These need to be handled
		 * specially, because in the case of move from CONST
		 * there is no src ir3_instruction so we need to
		 * replace the ir3_register.  And in the case of
		 * RELATIV we need to handle the address register
		 * dependency.
		 */
		if (src_reg->flags & IR3_REG_CONST) {
			/* an instruction cannot reference two different
			 * address registers:
			 */
			if ((src_reg->flags & IR3_REG_RELATIV) &&
					conflicts(instr->address, reg->instr->address))
				return false;

			/* This seems to be a hw bug, or something where the timings
			 * just somehow don't work out.  This restriction may only
			 * apply if the first src is also CONST.
			 */
			if ((opc_cat(instr->opc) == 3) && (n == 2) &&
					(src_reg->flags & IR3_REG_RELATIV) &&
					(src_reg->array.offset == 0))
				return false;

			/* When narrowing constant from 32b to 16b, it seems
			 * to work only for float. So we should do this only with
			 * float opcodes.
			 */
			if (src->cat1.dst_type == TYPE_F16) {
				if (instr->opc == OPC_MOV && !type_float(instr->cat1.src_type))
					return false;
				if (!is_cat2_float(instr->opc) && !is_cat3_float(instr->opc))
					return false;
			}

			src_reg = ir3_reg_clone(instr->block->shader, src_reg);
			src_reg->flags = new_flags;
			instr->regs[n+1] = src_reg;

			if (src_reg->flags & IR3_REG_RELATIV)
				ir3_instr_set_address(instr, reg->instr->address);

			return true;
		}

		if ((src_reg->flags & IR3_REG_RELATIV) &&
				!conflicts(instr->address, reg->instr->address)) {
			src_reg = ir3_reg_clone(instr->block->shader, src_reg);
			src_reg->flags = new_flags;
			instr->regs[n+1] = src_reg;
			ir3_instr_set_address(instr, reg->instr->address);

			return true;
		}

		/* NOTE: seems we can only do immed integers, so don't
		 * need to care about float.  But we do need to handle
		 * abs/neg *before* checking that the immediate requires
		 * few enough bits to encode:
		 *
		 * TODO: do we need to do something to avoid accidentally
		 * catching a float immed?
		 */
		if (src_reg->flags & IR3_REG_IMMED) {
			int32_t iim_val = src_reg->iim_val;

			debug_assert((opc_cat(instr->opc) == 1) ||
					(opc_cat(instr->opc) == 6) ||
					ir3_cat2_int(instr->opc) ||
					(is_mad(instr->opc) && (n == 0)));

			if (new_flags & IR3_REG_SABS)
				iim_val = abs(iim_val);

			if (new_flags & IR3_REG_SNEG)
				iim_val = -iim_val;

			if (new_flags & IR3_REG_BNOT)
				iim_val = ~iim_val;

			/* other than category 1 (mov) we can only encode up to 10 bits: */
			if (ir3_valid_flags(instr, n, new_flags) &&
					((instr->opc == OPC_MOV) ||
					 !((iim_val & ~0x3ff) && (-iim_val & ~0x3ff)))) {
				new_flags &= ~(IR3_REG_SABS | IR3_REG_SNEG | IR3_REG_BNOT);
				src_reg = ir3_reg_clone(instr->block->shader, src_reg);
				src_reg->flags = new_flags;
				src_reg->iim_val = iim_val;
				instr->regs[n+1] = src_reg;

				return true;
			} else if (lower_immed(ctx, instr, n, src_reg, new_flags)) {
				/* Fell back to loading the immediate as a const */
				return true;
			}
		}
	}

	return false;
}

/* Handle special case of eliminating output mov, and similar cases where
 * there isn't a normal "consuming" instruction.  In this case we cannot
 * collapse flags (ie. output mov from const, or w/ abs/neg flags, cannot
 * be eliminated)
 */
static struct ir3_instruction *
eliminate_output_mov(struct ir3_cp_ctx *ctx, struct ir3_instruction *instr)
{
	if (is_eligible_mov(instr, NULL, false)) {
		struct ir3_register *reg = instr->regs[1];
		if (!(reg->flags & IR3_REG_ARRAY)) {
			struct ir3_instruction *src_instr = ssa(reg);
			debug_assert(src_instr);
			ctx->progress = true;
			return src_instr;
		}
	}
	return instr;
}

/**
 * Find instruction src's which are mov's that can be collapsed, replacing
 * the mov dst with the mov src
 */
static void
instr_cp(struct ir3_cp_ctx *ctx, struct ir3_instruction *instr)
{
	if (instr->regs_count == 0)
		return;

	if (ir3_instr_check_mark(instr))
		return;

	/* walk down the graph from each src: */
	bool progress;
	do {
		progress = false;
		foreach_src_n (reg, n, instr) {
			struct ir3_instruction *src = ssa(reg);

			if (!src)
				continue;

			instr_cp(ctx, src);

			/* TODO non-indirect access we could figure out which register
			 * we actually want and allow cp..
			 */
			if (reg->flags & IR3_REG_ARRAY)
				continue;

			/* Don't CP absneg into meta instructions, that won't end well: */
			if (is_meta(instr) && (src->opc != OPC_MOV))
				continue;

			progress |= reg_cp(ctx, instr, reg, n);
			ctx->progress |= progress;
		}
	} while (progress);

	if (instr->regs[0]->flags & IR3_REG_ARRAY) {
		struct ir3_instruction *src = ssa(instr->regs[0]);
		if (src)
			instr_cp(ctx, src);
	}

	if (instr->address) {
		instr_cp(ctx, instr->address);
		ir3_instr_set_address(instr, eliminate_output_mov(ctx, instr->address));
	}

	/* we can end up with extra cmps.s from frontend, which uses a
	 *
	 *    cmps.s p0.x, cond, 0
	 *
	 * as a way to mov into the predicate register.  But frequently 'cond'
	 * is itself a cmps.s/cmps.f/cmps.u.  So detect this special case and
	 * just re-write the instruction writing predicate register to get rid
	 * of the double cmps.
	 */
	if ((instr->opc == OPC_CMPS_S) &&
			(instr->regs[0]->num == regid(REG_P0, 0)) &&
			ssa(instr->regs[1]) &&
			(instr->regs[2]->flags & IR3_REG_IMMED) &&
			(instr->regs[2]->iim_val == 0) &&
			(instr->cat2.condition == IR3_COND_NE)) {
		struct ir3_instruction *cond = ssa(instr->regs[1]);
		switch (cond->opc) {
		case OPC_CMPS_S:
		case OPC_CMPS_F:
		case OPC_CMPS_U:
			instr->opc   = cond->opc;
			instr->flags = cond->flags;
			instr->cat2  = cond->cat2;
			ir3_instr_set_address(instr, cond->address);
			instr->regs[1] = cond->regs[1];
			instr->regs[2] = cond->regs[2];
			instr->barrier_class |= cond->barrier_class;
			instr->barrier_conflict |= cond->barrier_conflict;
			unuse(cond);
			ctx->progress = true;
			break;
		default:
			break;
		}
	}

	/* Handle converting a sam.s2en (taking samp/tex idx params via register)
	 * into a normal sam (encoding immediate samp/tex idx) if they are
	 * immediate. This saves some instructions and regs in the common case
	 * where we know samp/tex at compile time. This needs to be done in the
	 * frontend for bindless tex, though, so don't replicate it here.
	 */
	if (is_tex(instr) && (instr->flags & IR3_INSTR_S2EN) &&
			!(instr->flags & IR3_INSTR_B) &&
			!(ir3_shader_debug & IR3_DBG_FORCES2EN)) {
		/* The first src will be a collect, if both of it's
		 * two sources are mov from imm, then we can
		 */
		struct ir3_instruction *samp_tex = ssa(instr->regs[1]);

		debug_assert(samp_tex->opc == OPC_META_COLLECT);

		struct ir3_instruction *samp = ssa(samp_tex->regs[1]);
		struct ir3_instruction *tex  = ssa(samp_tex->regs[2]);

		if ((samp->opc == OPC_MOV) &&
				(samp->regs[1]->flags & IR3_REG_IMMED) &&
				(tex->opc == OPC_MOV) &&
				(tex->regs[1]->flags & IR3_REG_IMMED)) {
			instr->flags &= ~IR3_INSTR_S2EN;
			instr->cat5.samp = samp->regs[1]->iim_val;
			instr->cat5.tex  = tex->regs[1]->iim_val;

			/* shuffle around the regs to remove the first src: */
			instr->regs_count--;
			for (unsigned i = 1; i < instr->regs_count; i++) {
				instr->regs[i] = instr->regs[i + 1];
			}

			ctx->progress = true;
		}
	}
}

bool
ir3_cp(struct ir3 *ir, struct ir3_shader_variant *so)
{
	struct ir3_cp_ctx ctx = {
			.shader = ir,
			.so = so,
	};

	/* This is a bit annoying, and probably wouldn't be necessary if we
	 * tracked a reverse link from producing instruction to consumer.
	 * But we need to know when we've eliminated the last consumer of
	 * a mov, so we need to do a pass to first count consumers of a
	 * mov.
	 */
	foreach_block (block, &ir->block_list) {
		foreach_instr (instr, &block->instr_list) {

			/* by the way, we don't account for false-dep's, so the CP
			 * pass should always happen before false-dep's are inserted
			 */
			debug_assert(instr->deps_count == 0);

			foreach_ssa_src (src, instr) {
				src->use_count++;
			}
		}
	}

	ir3_clear_mark(ir);

	foreach_output_n (out, n, ir) {
		instr_cp(&ctx, out);
		ir->outputs[n] = eliminate_output_mov(&ctx, out);
	}

	foreach_block (block, &ir->block_list) {
		if (block->condition) {
			instr_cp(&ctx, block->condition);
			block->condition = eliminate_output_mov(&ctx, block->condition);
		}

		for (unsigned i = 0; i < block->keeps_count; i++) {
			instr_cp(&ctx, block->keeps[i]);
			block->keeps[i] = eliminate_output_mov(&ctx, block->keeps[i]);
		}
	}

	return ctx.progress;
}
