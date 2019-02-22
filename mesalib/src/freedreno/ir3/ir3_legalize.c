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

#include "util/ralloc.h"
#include "util/u_math.h"

#include "ir3.h"
#include "ir3_compiler.h"

/*
 * Legalize:
 *
 * We currently require that scheduling ensures that we have enough nop's
 * in all the right places.  The legalize step mostly handles fixing up
 * instruction flags ((ss)/(sy)/(ei)), and collapses sequences of nop's
 * into fewer nop's w/ rpt flag.
 */

struct ir3_legalize_ctx {
	struct ir3_compiler *compiler;
	int num_samp;
	bool has_ssbo;
	int max_bary;
};

struct ir3_legalize_state {
	regmask_t needs_ss;
	regmask_t needs_ss_war;       /* write after read */
	regmask_t needs_sy;
};

struct ir3_legalize_block_data {
	bool valid;
	struct ir3_legalize_state state;
};

/* We want to evaluate each block from the position of any other
 * predecessor block, in order that the flags set are the union of
 * all possible program paths.
 *
 * To do this, we need to know the output state (needs_ss/ss_war/sy)
 * of all predecessor blocks.  The tricky thing is loops, which mean
 * that we can't simply recursively process each predecessor block
 * before legalizing the current block.
 *
 * How we handle that is by looping over all the blocks until the
 * results converge.  If the output state of a given block changes
 * in a given pass, this means that all successor blocks are not
 * yet fully legalized.
 */

static bool
legalize_block(struct ir3_legalize_ctx *ctx, struct ir3_block *block)
{
	struct ir3_legalize_block_data *bd = block->data;

	if (bd->valid)
		return false;

	struct ir3_instruction *last_input = NULL;
	struct ir3_instruction *last_rel = NULL;
	struct ir3_instruction *last_n = NULL;
	struct list_head instr_list;
	struct ir3_legalize_state prev_state = bd->state;
	struct ir3_legalize_state *state = &bd->state;

	/* our input state is the OR of all predecessor blocks' state: */
	for (unsigned i = 0; i < block->predecessors_count; i++) {
		struct ir3_legalize_block_data *pbd = block->predecessors[i]->data;
		struct ir3_legalize_state *pstate = &pbd->state;

		/* Our input (ss)/(sy) state is based on OR'ing the output
		 * state of all our predecessor blocks
		 */
		regmask_or(&state->needs_ss,
				&state->needs_ss, &pstate->needs_ss);
		regmask_or(&state->needs_ss_war,
				&state->needs_ss_war, &pstate->needs_ss_war);
		regmask_or(&state->needs_sy,
				&state->needs_sy, &pstate->needs_sy);
	}

	/* remove all the instructions from the list, we'll be adding
	 * them back in as we go
	 */
	list_replace(&block->instr_list, &instr_list);
	list_inithead(&block->instr_list);

	list_for_each_entry_safe (struct ir3_instruction, n, &instr_list, node) {
		struct ir3_register *reg;
		unsigned i;

		n->flags &= ~(IR3_INSTR_SS | IR3_INSTR_SY);

		if (is_meta(n))
			continue;

		if (is_input(n)) {
			struct ir3_register *inloc = n->regs[1];
			assert(inloc->flags & IR3_REG_IMMED);
			ctx->max_bary = MAX2(ctx->max_bary, inloc->iim_val);
		}

		if (last_n && is_barrier(last_n))
			n->flags |= IR3_INSTR_SS | IR3_INSTR_SY;

		/* NOTE: consider dst register too.. it could happen that
		 * texture sample instruction (for example) writes some
		 * components which are unused.  A subsequent instruction
		 * that writes the same register can race w/ the sam instr
		 * resulting in undefined results:
		 */
		for (i = 0; i < n->regs_count; i++) {
			reg = n->regs[i];

			if (reg_gpr(reg)) {

				/* TODO: we probably only need (ss) for alu
				 * instr consuming sfu result.. need to make
				 * some tests for both this and (sy)..
				 */
				if (regmask_get(&state->needs_ss, reg)) {
					n->flags |= IR3_INSTR_SS;
					regmask_init(&state->needs_ss_war);
					regmask_init(&state->needs_ss);
				}

				if (regmask_get(&state->needs_sy, reg)) {
					n->flags |= IR3_INSTR_SY;
					regmask_init(&state->needs_sy);
				}
			}

			/* TODO: is it valid to have address reg loaded from a
			 * relative src (ie. mova a0, c<a0.x+4>)?  If so, the
			 * last_rel check below should be moved ahead of this:
			 */
			if (reg->flags & IR3_REG_RELATIV)
				last_rel = n;
		}

		if (n->regs_count > 0) {
			reg = n->regs[0];
			if (regmask_get(&state->needs_ss_war, reg)) {
				n->flags |= IR3_INSTR_SS;
				regmask_init(&state->needs_ss_war);
				regmask_init(&state->needs_ss);
			}

			if (last_rel && (reg->num == regid(REG_A0, 0))) {
				last_rel->flags |= IR3_INSTR_UL;
				last_rel = NULL;
			}
		}

		/* cat5+ does not have an (ss) bit, if needed we need to
		 * insert a nop to carry the sync flag.  Would be kinda
		 * clever if we were aware of this during scheduling, but
		 * this should be a pretty rare case:
		 */
		if ((n->flags & IR3_INSTR_SS) && (opc_cat(n->opc) >= 5)) {
			struct ir3_instruction *nop;
			nop = ir3_NOP(block);
			nop->flags |= IR3_INSTR_SS;
			n->flags &= ~IR3_INSTR_SS;
		}

		/* need to be able to set (ss) on first instruction: */
		if (list_empty(&block->instr_list) && (opc_cat(n->opc) >= 5))
			ir3_NOP(block);

		if (is_nop(n) && !list_empty(&block->instr_list)) {
			struct ir3_instruction *last = list_last_entry(&block->instr_list,
					struct ir3_instruction, node);
			if (is_nop(last) && (last->repeat < 5)) {
				last->repeat++;
				last->flags |= n->flags;
				continue;
			}
		}

		list_addtail(&n->node, &block->instr_list);

		if (is_sfu(n))
			regmask_set(&state->needs_ss, n->regs[0]);

		if (is_tex(n)) {
			/* this ends up being the # of samp instructions.. but that
			 * is ok, everything else only cares whether it is zero or
			 * not.  We do this here, rather than when we encounter a
			 * SAMP decl, because (especially in binning pass shader)
			 * the samp instruction(s) could get eliminated if the
			 * result is not used.
			 */
			ctx->num_samp = MAX2(ctx->num_samp, n->cat5.samp + 1);
			regmask_set(&state->needs_sy, n->regs[0]);
		} else if (n->opc == OPC_RESINFO) {
			regmask_set(&state->needs_ss, n->regs[0]);
			ir3_NOP(block)->flags |= IR3_INSTR_SS;
		} else if (is_load(n)) {
			/* seems like ldlv needs (ss) bit instead??  which is odd but
			 * makes a bunch of flat-varying tests start working on a4xx.
			 */
			if ((n->opc == OPC_LDLV) || (n->opc == OPC_LDL))
				regmask_set(&state->needs_ss, n->regs[0]);
			else
				regmask_set(&state->needs_sy, n->regs[0]);
		} else if (is_atomic(n->opc)) {
			if (n->flags & IR3_INSTR_G) {
				if (ctx->compiler->gpu_id >= 600) {
					/* New encoding, returns  result via second src: */
					regmask_set(&state->needs_sy, n->regs[3]);
				} else {
					regmask_set(&state->needs_sy, n->regs[0]);
				}
			} else {
				regmask_set(&state->needs_ss, n->regs[0]);
			}
		}

		if (is_ssbo(n->opc) || (is_atomic(n->opc) && (n->flags & IR3_INSTR_G)))
			ctx->has_ssbo = true;

		/* both tex/sfu appear to not always immediately consume
		 * their src register(s):
		 */
		if (is_tex(n) || is_sfu(n) || is_mem(n)) {
			foreach_src(reg, n) {
				if (reg_gpr(reg))
					regmask_set(&state->needs_ss_war, reg);
			}
		}

		if (is_input(n))
			last_input = n;

		last_n = n;
	}

	if (last_input) {
		/* special hack.. if using ldlv to bypass interpolation,
		 * we need to insert a dummy bary.f on which we can set
		 * the (ei) flag:
		 */
		if (is_mem(last_input) && (last_input->opc == OPC_LDLV)) {
			struct ir3_instruction *baryf;

			/* (ss)bary.f (ei)r63.x, 0, r0.x */
			baryf = ir3_instr_create(block, OPC_BARY_F);
			baryf->flags |= IR3_INSTR_SS;
			ir3_reg_create(baryf, regid(63, 0), 0);
			ir3_reg_create(baryf, 0, IR3_REG_IMMED)->iim_val = 0;
			ir3_reg_create(baryf, regid(0, 0), 0);

			/* insert the dummy bary.f after last_input: */
			list_delinit(&baryf->node);
			list_add(&baryf->node, &last_input->node);

			last_input = baryf;
		}
		last_input->regs[0]->flags |= IR3_REG_EI;
	}

	if (last_rel)
		last_rel->flags |= IR3_INSTR_UL;

	bd->valid = true;

	if (memcmp(&prev_state, state, sizeof(*state))) {
		/* our output state changed, this invalidates all of our
		 * successors:
		 */
		for (unsigned i = 0; i < ARRAY_SIZE(block->successors); i++) {
			if (!block->successors[i])
				break;
			struct ir3_legalize_block_data *pbd = block->successors[i]->data;
			pbd->valid = false;
		}
	}

	return true;
}

/* NOTE: branch instructions are always the last instruction(s)
 * in the block.  We take advantage of this as we resolve the
 * branches, since "if (foo) break;" constructs turn into
 * something like:
 *
 *   block3 {
 *   	...
 *   	0029:021: mov.s32s32 r62.x, r1.y
 *   	0082:022: br !p0.x, target=block5
 *   	0083:023: br p0.x, target=block4
 *   	// succs: if _[0029:021: mov.s32s32] block4; else block5;
 *   }
 *   block4 {
 *   	0084:024: jump, target=block6
 *   	// succs: block6;
 *   }
 *   block5 {
 *   	0085:025: jump, target=block7
 *   	// succs: block7;
 *   }
 *
 * ie. only instruction in block4/block5 is a jump, so when
 * resolving branches we can easily detect this by checking
 * that the first instruction in the target block is itself
 * a jump, and setup the br directly to the jump's target
 * (and strip back out the now unreached jump)
 *
 * TODO sometimes we end up with things like:
 *
 *    br !p0.x, #2
 *    br p0.x, #12
 *    add.u r0.y, r0.y, 1
 *
 * If we swapped the order of the branches, we could drop one.
 */
static struct ir3_block *
resolve_dest_block(struct ir3_block *block)
{
	/* special case for last block: */
	if (!block->successors[0])
		return block;

	/* NOTE that we may or may not have inserted the jump
	 * in the target block yet, so conditions to resolve
	 * the dest to the dest block's successor are:
	 *
	 *   (1) successor[1] == NULL &&
	 *   (2) (block-is-empty || only-instr-is-jump)
	 */
	if (block->successors[1] == NULL) {
		if (list_empty(&block->instr_list)) {
			return block->successors[0];
		} else if (list_length(&block->instr_list) == 1) {
			struct ir3_instruction *instr = list_first_entry(
					&block->instr_list, struct ir3_instruction, node);
			if (instr->opc == OPC_JUMP)
				return block->successors[0];
		}
	}
	return block;
}

static bool
resolve_jump(struct ir3_instruction *instr)
{
	struct ir3_block *tblock =
		resolve_dest_block(instr->cat0.target);
	struct ir3_instruction *target;

	if (tblock != instr->cat0.target) {
		list_delinit(&instr->cat0.target->node);
		instr->cat0.target = tblock;
		return true;
	}

	target = list_first_entry(&tblock->instr_list,
				struct ir3_instruction, node);

	/* TODO maybe a less fragile way to do this.  But we are expecting
	 * a pattern from sched_block() that looks like:
	 *
	 *   br !p0.x, #else-block
	 *   br p0.x, #if-block
	 *
	 * if the first branch target is +2, or if 2nd branch target is +1
	 * then we can just drop the jump.
	 */
	unsigned next_block;
	if (instr->cat0.inv == true)
		next_block = 2;
	else
		next_block = 1;

	if ((!target) || (target->ip == (instr->ip + next_block))) {
		list_delinit(&instr->node);
		return true;
	} else {
		instr->cat0.immed =
			(int)target->ip - (int)instr->ip;
	}
	return false;
}

/* resolve jumps, removing jumps/branches to immediately following
 * instruction which we end up with from earlier stages.  Since
 * removing an instruction can invalidate earlier instruction's
 * branch offsets, we need to do this iteratively until no more
 * branches are removed.
 */
static bool
resolve_jumps(struct ir3 *ir)
{
	list_for_each_entry (struct ir3_block, block, &ir->block_list, node)
		list_for_each_entry (struct ir3_instruction, instr, &block->instr_list, node)
			if (is_flow(instr) && instr->cat0.target)
				if (resolve_jump(instr))
					return true;

	return false;
}

/* we want to mark points where divergent flow control re-converges
 * with (jp) flags.  For now, since we don't do any optimization for
 * things that start out as a 'do {} while()', re-convergence points
 * will always be a branch or jump target.  Note that this is overly
 * conservative, since unconditional jump targets are not convergence
 * points, we are just assuming that the other path to reach the jump
 * target was divergent.  If we were clever enough to optimize the
 * jump at end of a loop back to a conditional branch into a single
 * conditional branch, ie. like:
 *
 *    add.f r1.w, r0.x, (neg)(r)c2.x   <= loop start
 *    mul.f r1.z, r1.z, r0.x
 *    mul.f r1.y, r1.y, r0.x
 *    mul.f r0.z, r1.x, r0.x
 *    mul.f r0.w, r0.y, r0.x
 *    cmps.f.ge r0.x, (r)c2.y, (r)r1.w
 *    add.s r0.x, (r)r0.x, (r)-1
 *    sel.f32 r0.x, (r)c3.y, (r)r0.x, c3.x
 *    cmps.f.eq p0.x, r0.x, c3.y
 *    mov.f32f32 r0.x, r1.w
 *    mov.f32f32 r0.y, r0.w
 *    mov.f32f32 r1.x, r0.z
 *    (rpt2)nop
 *    br !p0.x, #-13
 *    (jp)mul.f r0.x, c263.y, r1.y
 *
 * Then we'd have to be more clever, as the convergence point is no
 * longer a branch or jump target.
 */
static void
mark_convergence_points(struct ir3 *ir)
{
	list_for_each_entry (struct ir3_block, block, &ir->block_list, node) {
		list_for_each_entry (struct ir3_instruction, instr, &block->instr_list, node) {
			if (is_flow(instr) && instr->cat0.target) {
				struct ir3_instruction *target =
					list_first_entry(&instr->cat0.target->instr_list,
							struct ir3_instruction, node);
				target->flags |= IR3_INSTR_JP;
			}
		}
	}
}

void
ir3_legalize(struct ir3 *ir, int *num_samp, bool *has_ssbo, int *max_bary)
{
	struct ir3_legalize_ctx *ctx = rzalloc(ir, struct ir3_legalize_ctx);
	bool progress;

	ctx->max_bary = -1;
	ctx->compiler = ir->compiler;

	/* allocate per-block data: */
	list_for_each_entry (struct ir3_block, block, &ir->block_list, node) {
		block->data = rzalloc(ctx, struct ir3_legalize_block_data);
	}

	/* process each block: */
	do {
		progress = false;
		list_for_each_entry (struct ir3_block, block, &ir->block_list, node) {
			progress |= legalize_block(ctx, block);
		}
	} while (progress);

	*num_samp = ctx->num_samp;
	*has_ssbo = ctx->has_ssbo;
	*max_bary = ctx->max_bary;

	do {
		ir3_count_instructions(ir);
	} while(resolve_jumps(ir));

	mark_convergence_points(ir);

	ralloc_free(ctx);
}
