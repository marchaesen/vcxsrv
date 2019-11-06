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
	gl_shader_stage type;
	bool has_ssbo;
	bool need_pixlod;
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
	bool last_input_needs_ss = false;
	bool has_tex_prefetch = false;

	/* our input state is the OR of all predecessor blocks' state: */
	set_foreach(block->predecessors, entry) {
		struct ir3_block *predecessor = (struct ir3_block *)entry->key;
		struct ir3_legalize_block_data *pbd = predecessor->data;
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

		/* _meta::tex_prefetch instructions removed later in
		 * collect_tex_prefetches()
		 */
		if (is_meta(n) && (n->opc != OPC_META_TEX_PREFETCH))
			continue;

		if (is_input(n)) {
			struct ir3_register *inloc = n->regs[1];
			assert(inloc->flags & IR3_REG_IMMED);
			ctx->max_bary = MAX2(ctx->max_bary, inloc->iim_val);
		}

		if (last_n && is_barrier(last_n)) {
			n->flags |= IR3_INSTR_SS | IR3_INSTR_SY;
			last_input_needs_ss = false;
		}

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
					last_input_needs_ss = false;
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
				last_input_needs_ss = false;
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
		if (list_is_empty(&block->instr_list) && (opc_cat(n->opc) >= 5))
			ir3_NOP(block);

		if (is_nop(n) && !list_is_empty(&block->instr_list)) {
			struct ir3_instruction *last = list_last_entry(&block->instr_list,
					struct ir3_instruction, node);
			if (is_nop(last) && (last->repeat < 5)) {
				last->repeat++;
				last->flags |= n->flags;
				continue;
			}

			/* NOTE: I think the nopN encoding works for a5xx and
			 * probably a4xx, but not a3xx.  So far only tested on
			 * a6xx.
			 */
			if ((ctx->compiler->gpu_id >= 600) && !n->flags && (last->nop < 3) &&
					((opc_cat(last->opc) == 2) || (opc_cat(last->opc) == 3))) {
				last->nop++;
				continue;
			}
		}

		if (ctx->compiler->samgq_workaround &&
			ctx->type == MESA_SHADER_VERTEX && n->opc == OPC_SAMGQ) {
			struct ir3_instruction *samgp;

			for (i = 0; i < 4; i++) {
				samgp = ir3_instr_clone(n);
				samgp->opc = OPC_SAMGP0 + i;
				if (i > 1)
					samgp->flags |= IR3_INSTR_SY;
			}
			list_delinit(&n->node);
		} else {
			list_addtail(&n->node, &block->instr_list);
		}

		if (is_sfu(n))
			regmask_set(&state->needs_ss, n->regs[0]);

		if (is_tex(n) || (n->opc == OPC_META_TEX_PREFETCH)) {
			regmask_set(&state->needs_sy, n->regs[0]);
			ctx->need_pixlod = true;
			if (n->opc == OPC_META_TEX_PREFETCH)
				has_tex_prefetch = true;
		} else if (n->opc == OPC_RESINFO) {
			regmask_set(&state->needs_ss, n->regs[0]);
			ir3_NOP(block)->flags |= IR3_INSTR_SS;
			last_input_needs_ss = false;
		} else if (is_load(n)) {
			/* seems like ldlv needs (ss) bit instead??  which is odd but
			 * makes a bunch of flat-varying tests start working on a4xx.
			 */
			if ((n->opc == OPC_LDLV) || (n->opc == OPC_LDL) || (n->opc == OPC_LDLW))
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

		if (is_input(n)) {
			last_input = n;
			last_input_needs_ss |= (n->opc == OPC_LDLV);
		}

		last_n = n;
	}

	if (last_input) {
		assert(block == list_first_entry(&block->shader->block_list,
				struct ir3_block, node));
		/* special hack.. if using ldlv to bypass interpolation,
		 * we need to insert a dummy bary.f on which we can set
		 * the (ei) flag:
		 */
		if (is_mem(last_input) && (last_input->opc == OPC_LDLV)) {
			struct ir3_instruction *baryf;

			/* (ss)bary.f (ei)r63.x, 0, r0.x */
			baryf = ir3_instr_create(block, OPC_BARY_F);
			ir3_reg_create(baryf, regid(63, 0), 0);
			ir3_reg_create(baryf, 0, IR3_REG_IMMED)->iim_val = 0;
			ir3_reg_create(baryf, regid(0, 0), 0);

			/* insert the dummy bary.f after last_input: */
			list_delinit(&baryf->node);
			list_add(&baryf->node, &last_input->node);

			last_input = baryf;

			/* by definition, we need (ss) since we are inserting
			 * the dummy bary.f immediately after the ldlv:
			 */
			last_input_needs_ss = true;
		}
		last_input->regs[0]->flags |= IR3_REG_EI;
		if (last_input_needs_ss)
			last_input->flags |= IR3_INSTR_SS;
	} else if (has_tex_prefetch) {
		/* texture prefetch, but *no* inputs.. we need to insert a
		 * dummy bary.f at the top of the shader to unblock varying
		 * storage:
		 */
		struct ir3_instruction *baryf;

		/* (ss)bary.f (ei)r63.x, 0, r0.x */
		baryf = ir3_instr_create(block, OPC_BARY_F);
		ir3_reg_create(baryf, regid(63, 0), 0)->flags |= IR3_REG_EI;
		ir3_reg_create(baryf, 0, IR3_REG_IMMED)->iim_val = 0;
		ir3_reg_create(baryf, regid(0, 0), 0);

		/* insert the dummy bary.f at head: */
		list_delinit(&baryf->node);
		list_add(&baryf->node, &block->instr_list);
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
		if (list_is_empty(&block->instr_list)) {
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

static void
remove_unused_block(struct ir3_block *old_target)
{
	list_delinit(&old_target->node);

	/* cleanup dangling predecessors: */
	for (unsigned i = 0; i < ARRAY_SIZE(old_target->successors); i++) {
		if (old_target->successors[i]) {
			struct ir3_block *succ = old_target->successors[i];
			_mesa_set_remove_key(succ->predecessors, old_target);
		}
	}
}

static void
retarget_jump(struct ir3_instruction *instr, struct ir3_block *new_target)
{
	struct ir3_block *old_target = instr->cat0.target;
	struct ir3_block *cur_block = instr->block;

	/* update current blocks successors to reflect the retargetting: */
	if (cur_block->successors[0] == old_target) {
		cur_block->successors[0] = new_target;
	} else {
		debug_assert(cur_block->successors[1] == old_target);
		cur_block->successors[1] = new_target;
	}

	/* update new target's predecessors: */
	_mesa_set_add(new_target->predecessors, cur_block);

	/* and remove old_target's predecessor: */
	debug_assert(_mesa_set_search(old_target->predecessors, cur_block));
	_mesa_set_remove_key(old_target->predecessors, cur_block);

	if (old_target->predecessors->entries == 0)
		remove_unused_block(old_target);

	instr->cat0.target = new_target;
}

static bool
resolve_jump(struct ir3_instruction *instr)
{
	struct ir3_block *tblock =
		resolve_dest_block(instr->cat0.target);
	struct ir3_instruction *target;

	if (tblock != instr->cat0.target) {
		retarget_jump(instr, tblock);
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

	if (target->ip == (instr->ip + next_block)) {
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

static void mark_jp(struct ir3_block *block)
{
	struct ir3_instruction *target = list_first_entry(&block->instr_list,
			struct ir3_instruction, node);
	target->flags |= IR3_INSTR_JP;
}

/* Mark points where control flow converges or diverges.
 *
 * Divergence points could actually be re-convergence points where
 * "parked" threads are recoverged with threads that took the opposite
 * path last time around.  Possibly it is easier to think of (jp) as
 * "the execution mask might have changed".
 */
static void
mark_xvergence_points(struct ir3 *ir)
{
	list_for_each_entry (struct ir3_block, block, &ir->block_list, node) {
		if (block->predecessors->entries > 1) {
			/* if a block has more than one possible predecessor, then
			 * the first instruction is a convergence point.
			 */
			mark_jp(block);
		} else if (block->predecessors->entries == 1) {
			/* If a block has one predecessor, which has multiple possible
			 * successors, it is a divergence point.
			 */
			set_foreach(block->predecessors, entry) {
				struct ir3_block *predecessor = (struct ir3_block *)entry->key;
				if (predecessor->successors[1]) {
					mark_jp(block);
				}
			}
		}
	}
}

void
ir3_legalize(struct ir3 *ir, bool *has_ssbo, bool *need_pixlod, int *max_bary)
{
	struct ir3_legalize_ctx *ctx = rzalloc(ir, struct ir3_legalize_ctx);
	bool progress;

	ctx->max_bary = -1;
	ctx->compiler = ir->compiler;
	ctx->type = ir->type;

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

	*has_ssbo = ctx->has_ssbo;
	*need_pixlod = ctx->need_pixlod;
	*max_bary = ctx->max_bary;

	do {
		ir3_count_instructions(ir);
	} while(resolve_jumps(ir));

	mark_xvergence_points(ir);

	ralloc_free(ctx);
}
