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

#include "ir3.h"
#include "ir3_compiler.h"
#include "ir3_shader.h"

/*
 * Copy Propagate:
 */

struct ir3_cp_ctx {
	struct ir3 *shader;
	struct ir3_shader_variant *so;
	unsigned immediate_idx;
};

/* is it a type preserving mov, with ok flags? */
static bool is_eligible_mov(struct ir3_instruction *instr, bool allow_flags)
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

		/* TODO: remove this hack: */
		if (src_instr->opc == OPC_META_FO)
			return false;

		return true;
	}
	return false;
}

static unsigned cp_flags(unsigned flags)
{
	/* only considering these flags (at least for now): */
	flags &= (IR3_REG_CONST | IR3_REG_IMMED |
			IR3_REG_FNEG | IR3_REG_FABS |
			IR3_REG_SNEG | IR3_REG_SABS |
			IR3_REG_BNOT | IR3_REG_RELATIV);
	return flags;
}

static bool valid_flags(struct ir3_instruction *instr, unsigned n,
		unsigned flags)
{
	unsigned valid_flags;

	if ((flags & IR3_REG_HIGH) &&
			(opc_cat(instr->opc) > 1) &&
			(instr->block->shader->compiler->gpu_id >= 600))
		return false;

	flags = cp_flags(flags);

	/* If destination is indirect, then source cannot be.. at least
	 * I don't think so..
	 */
	if ((instr->regs[0]->flags & IR3_REG_RELATIV) &&
			(flags & IR3_REG_RELATIV))
		return false;

	/* TODO it seems to *mostly* work to cp RELATIV, except we get some
	 * intermittent piglit variable-indexing fails.  Newer blob driver
	 * doesn't seem to cp these.  Possibly this is hw workaround?  Not
	 * sure, but until that is understood better, lets just switch off
	 * cp for indirect src's:
	 */
	if (flags & IR3_REG_RELATIV)
		return false;

	switch (opc_cat(instr->opc)) {
	case 1:
		valid_flags = IR3_REG_IMMED | IR3_REG_CONST | IR3_REG_RELATIV;
		if (flags & ~valid_flags)
			return false;
		break;
	case 2:
		valid_flags = ir3_cat2_absneg(instr->opc) |
				IR3_REG_CONST | IR3_REG_RELATIV;

		if (ir3_cat2_int(instr->opc))
			valid_flags |= IR3_REG_IMMED;

		if (flags & ~valid_flags)
			return false;

		if (flags & (IR3_REG_CONST | IR3_REG_IMMED)) {
			unsigned m = (n ^ 1) + 1;
			/* cannot deal w/ const in both srcs:
			 * (note that some cat2 actually only have a single src)
			 */
			if (m < instr->regs_count) {
				struct ir3_register *reg = instr->regs[m];
				if ((flags & IR3_REG_CONST) && (reg->flags & IR3_REG_CONST))
					return false;
				if ((flags & IR3_REG_IMMED) && (reg->flags & IR3_REG_IMMED))
					return false;
			}
			/* cannot be const + ABS|NEG: */
			if (flags & (IR3_REG_FABS | IR3_REG_FNEG |
					IR3_REG_SABS | IR3_REG_SNEG | IR3_REG_BNOT))
				return false;
		}
		break;
	case 3:
		valid_flags = ir3_cat3_absneg(instr->opc) |
				IR3_REG_CONST | IR3_REG_RELATIV;

		if (flags & ~valid_flags)
			return false;

		if (flags & (IR3_REG_CONST | IR3_REG_RELATIV)) {
			/* cannot deal w/ const/relativ in 2nd src: */
			if (n == 1)
				return false;
		}

		if (flags & IR3_REG_CONST) {
			/* cannot be const + ABS|NEG: */
			if (flags & (IR3_REG_FABS | IR3_REG_FNEG |
					IR3_REG_SABS | IR3_REG_SNEG | IR3_REG_BNOT))
				return false;
		}
		break;
	case 4:
		/* seems like blob compiler avoids const as src.. */
		/* TODO double check if this is still the case on a4xx */
		if (flags & (IR3_REG_CONST | IR3_REG_IMMED))
			return false;
		if (flags & (IR3_REG_SABS | IR3_REG_SNEG))
			return false;
		break;
	case 5:
		/* no flags allowed */
		if (flags)
			return false;
		break;
	case 6:
		valid_flags = IR3_REG_IMMED;
		if (flags & ~valid_flags)
			return false;

		if (flags & IR3_REG_IMMED) {
			/* doesn't seem like we can have immediate src for store
			 * instructions:
			 *
			 * TODO this restriction could also apply to load instructions,
			 * but for load instructions this arg is the address (and not
			 * really sure any good way to test a hard-coded immed addr src)
			 */
			if (is_store(instr) && (n == 1))
				return false;

			if ((instr->opc == OPC_LDL) && (n != 1))
				return false;

			if ((instr->opc == OPC_STL) && (n != 2))
				return false;

			/* disallow CP into anything but the SSBO slot argument for
			 * atomics:
			 */
			if (is_atomic(instr->opc) && (n != 0))
				return false;

			if (is_atomic(instr->opc) && !(instr->flags & IR3_INSTR_G))
				return false;

			/* as with atomics, ldib on a6xx can only have immediate for
			 * SSBO slot argument
			 */
			if ((instr->opc == OPC_LDIB) && (n != 0))
				return false;
		}

		break;
	}

	return true;
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

static struct ir3_register *
lower_immed(struct ir3_cp_ctx *ctx, struct ir3_register *reg, unsigned new_flags)
{
	unsigned swiz, idx, i;

	reg = ir3_reg_clone(ctx->shader, reg);

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

	/* Reallocate for 4 more elements whenever it's necessary */
	if (ctx->immediate_idx == ctx->so->immediates_size * 4) {
		ctx->so->immediates_size += 4;
		ctx->so->immediates = realloc (ctx->so->immediates,
			ctx->so->immediates_size * sizeof (ctx->so->immediates[0]));
	}

	for (i = 0; i < ctx->immediate_idx; i++) {
		swiz = i % 4;
		idx  = i / 4;

		if (ctx->so->immediates[idx].val[swiz] == reg->uim_val) {
			break;
		}
	}

	if (i == ctx->immediate_idx) {
		/* need to generate a new immediate: */
		swiz = i % 4;
		idx  = i / 4;
		ctx->so->immediates[idx].val[swiz] = reg->uim_val;
		ctx->so->immediates_count = idx + 1;
		ctx->immediate_idx++;
	}

	new_flags &= ~IR3_REG_IMMED;
	new_flags |= IR3_REG_CONST;
	reg->flags = new_flags;
	reg->num = i + (4 * ctx->so->constbase.immediate);

	return reg;
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
 * Handle cp for a given src register.  This additionally handles
 * the cases of collapsing immedate/const (which replace the src
 * register with a non-ssa src) or collapsing mov's from relative
 * src (which needs to also fixup the address src reference by the
 * instruction).
 */
static void
reg_cp(struct ir3_cp_ctx *ctx, struct ir3_instruction *instr,
		struct ir3_register *reg, unsigned n)
{
	struct ir3_instruction *src = ssa(reg);

	if (is_eligible_mov(src, true)) {
		/* simple case, no immed/const/relativ, only mov's w/ ssa src: */
		struct ir3_register *src_reg = src->regs[1];
		unsigned new_flags = reg->flags;

		combine_flags(&new_flags, src);

		if (valid_flags(instr, n, new_flags)) {
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
		}

	} else if (is_same_type_mov(src) &&
			/* cannot collapse const/immed/etc into meta instrs: */
			!is_meta(instr)) {
		/* immed/const/etc cases, which require some special handling: */
		struct ir3_register *src_reg = src->regs[1];
		unsigned new_flags = reg->flags;

		combine_flags(&new_flags, src);

		if (!valid_flags(instr, n, new_flags)) {
			/* See if lowering an immediate to const would help. */
			if (valid_flags(instr, n, (new_flags & ~IR3_REG_IMMED) | IR3_REG_CONST)) {
				debug_assert(new_flags & IR3_REG_IMMED);
				instr->regs[n + 1] = lower_immed(ctx, src_reg, new_flags);
				return;
			}

			/* special case for "normal" mad instructions, we can
			 * try swapping the first two args if that fits better.
			 *
			 * the "plain" MAD's (ie. the ones that don't shift first
			 * src prior to multiply) can swap their first two srcs if
			 * src[0] is !CONST and src[1] is CONST:
			 */
			if ((n == 1) && is_mad(instr->opc) &&
					!(instr->regs[0 + 1]->flags & (IR3_REG_CONST | IR3_REG_RELATIV)) &&
					valid_flags(instr, 0, new_flags & ~IR3_REG_IMMED)) {
				/* swap src[0] and src[1]: */
				struct ir3_register *tmp;
				tmp = instr->regs[0 + 1];
				instr->regs[0 + 1] = instr->regs[1 + 1];
				instr->regs[1 + 1] = tmp;

				n = 0;
			} else {
				return;
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
				return;

			/* This seems to be a hw bug, or something where the timings
			 * just somehow don't work out.  This restriction may only
			 * apply if the first src is also CONST.
			 */
			if ((opc_cat(instr->opc) == 3) && (n == 2) &&
					(src_reg->flags & IR3_REG_RELATIV) &&
					(src_reg->array.offset == 0))
				return;

			src_reg = ir3_reg_clone(instr->block->shader, src_reg);
			src_reg->flags = new_flags;
			instr->regs[n+1] = src_reg;

			if (src_reg->flags & IR3_REG_RELATIV)
				ir3_instr_set_address(instr, reg->instr->address);

			return;
		}

		if ((src_reg->flags & IR3_REG_RELATIV) &&
				!conflicts(instr->address, reg->instr->address)) {
			src_reg = ir3_reg_clone(instr->block->shader, src_reg);
			src_reg->flags = new_flags;
			instr->regs[n+1] = src_reg;
			ir3_instr_set_address(instr, reg->instr->address);

			return;
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
			if ((instr->opc == OPC_MOV) ||
					!((iim_val & ~0x3ff) && (-iim_val & ~0x3ff))) {
				new_flags &= ~(IR3_REG_SABS | IR3_REG_SNEG | IR3_REG_BNOT);
				src_reg = ir3_reg_clone(instr->block->shader, src_reg);
				src_reg->flags = new_flags;
				src_reg->iim_val = iim_val;
				instr->regs[n+1] = src_reg;
			} else if (valid_flags(instr, n, (new_flags & ~IR3_REG_IMMED) | IR3_REG_CONST)) {
				/* See if lowering an immediate to const would help. */
				instr->regs[n+1] = lower_immed(ctx, src_reg, new_flags);
			}

			return;
		}
	}
}

/* Handle special case of eliminating output mov, and similar cases where
 * there isn't a normal "consuming" instruction.  In this case we cannot
 * collapse flags (ie. output mov from const, or w/ abs/neg flags, cannot
 * be eliminated)
 */
static struct ir3_instruction *
eliminate_output_mov(struct ir3_instruction *instr)
{
	if (is_eligible_mov(instr, false)) {
		struct ir3_register *reg = instr->regs[1];
		if (!(reg->flags & IR3_REG_ARRAY)) {
			struct ir3_instruction *src_instr = ssa(reg);
			debug_assert(src_instr);
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
	struct ir3_register *reg;

	if (instr->regs_count == 0)
		return;

	if (ir3_instr_check_mark(instr))
		return;

	/* walk down the graph from each src: */
	foreach_src_n(reg, n, instr) {
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

		reg_cp(ctx, instr, reg, n);
	}

	if (instr->regs[0]->flags & IR3_REG_ARRAY) {
		struct ir3_instruction *src = ssa(instr->regs[0]);
		if (src)
			instr_cp(ctx, src);
	}

	if (instr->address) {
		instr_cp(ctx, instr->address);
		ir3_instr_set_address(instr, eliminate_output_mov(instr->address));
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
			(instr->regs[2]->iim_val == 0)) {
		struct ir3_instruction *cond = ssa(instr->regs[1]);
		switch (cond->opc) {
		case OPC_CMPS_S:
		case OPC_CMPS_F:
		case OPC_CMPS_U:
			instr->opc   = cond->opc;
			instr->flags = cond->flags;
			instr->cat2  = cond->cat2;
			instr->address = cond->address;
			instr->regs[1] = cond->regs[1];
			instr->regs[2] = cond->regs[2];
			instr->barrier_class |= cond->barrier_class;
			instr->barrier_conflict |= cond->barrier_conflict;
			unuse(cond);
			break;
		default:
			break;
		}
	}

	/* Handle converting a sam.s2en (taking samp/tex idx params via
	 * register) into a normal sam (encoding immediate samp/tex idx)
	 * if they are immediate.  This saves some instructions and regs
	 * in the common case where we know samp/tex at compile time:
	 */
	if (is_tex(instr) && (instr->flags & IR3_INSTR_S2EN) &&
			!(ir3_shader_debug & IR3_DBG_FORCES2EN)) {
		/* The first src will be a fan-in (collect), if both of it's
		 * two sources are mov from imm, then we can
		 */
		struct ir3_instruction *samp_tex = ssa(instr->regs[1]);

		debug_assert(samp_tex->opc == OPC_META_FI);

		struct ir3_instruction *samp = ssa(samp_tex->regs[1]);
		struct ir3_instruction *tex  = ssa(samp_tex->regs[2]);

		if ((samp->opc == OPC_MOV) &&
				(samp->regs[1]->flags & IR3_REG_IMMED) &&
				(tex->opc == OPC_MOV) &&
				(tex->regs[1]->flags & IR3_REG_IMMED)) {
			instr->flags &= ~IR3_INSTR_S2EN;
			instr->cat5.samp = samp->regs[1]->iim_val;
			instr->cat5.tex  = tex->regs[1]->iim_val;
			instr->regs[1]->instr = NULL;
		}
	}
}

void
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
	list_for_each_entry (struct ir3_block, block, &ir->block_list, node) {
		list_for_each_entry (struct ir3_instruction, instr, &block->instr_list, node) {
			struct ir3_instruction *src;

			/* by the way, we don't account for false-dep's, so the CP
			 * pass should always happen before false-dep's are inserted
			 */
			debug_assert(instr->deps_count == 0);

			foreach_ssa_src(src, instr) {
				src->use_count++;
			}
		}
	}

	ir3_clear_mark(ir);

	for (unsigned i = 0; i < ir->noutputs; i++) {
		if (ir->outputs[i]) {
			instr_cp(&ctx, ir->outputs[i]);
			ir->outputs[i] = eliminate_output_mov(ir->outputs[i]);
		}
	}

	list_for_each_entry (struct ir3_block, block, &ir->block_list, node) {
		if (block->condition) {
			instr_cp(&ctx, block->condition);
			block->condition = eliminate_output_mov(block->condition);
		}

		for (unsigned i = 0; i < block->keeps_count; i++) {
			instr_cp(&ctx, block->keeps[i]);
			block->keeps[i] = eliminate_output_mov(block->keeps[i]);
		}
	}
}
