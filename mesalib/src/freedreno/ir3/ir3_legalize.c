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
#include "ir3_shader.h"

/*
 * Legalize:
 *
 * The legalize pass handles ensuring sufficient nop's and sync flags for
 * correct execution.
 *
 * 1) Iteratively determine where sync ((sy)/(ss)) flags are needed,
 *    based on state flowing out of predecessor blocks until there is
 *    no further change.  In some cases this requires inserting nops.
 * 2) Mark (ei) on last varying input, and (ul) on last use of a0.x
 * 3) Final nop scheduling for instruction latency
 * 4) Resolve jumps and schedule blocks, marking potential convergence
 *    points with (jp)
 */

struct ir3_legalize_ctx {
	struct ir3_compiler *compiler;
	struct ir3_shader_variant *so;
	gl_shader_stage type;
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
	bool mergedregs = ctx->so->mergedregs;

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

	foreach_instr_safe (n, &instr_list) {
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
			regmask_init(&state->needs_ss_war, mergedregs);
			regmask_init(&state->needs_ss, mergedregs);
			regmask_init(&state->needs_sy, mergedregs);
		}

		if (last_n && (last_n->opc == OPC_PREDT)) {
			n->flags |= IR3_INSTR_SS;
			regmask_init(&state->needs_ss_war, mergedregs);
			regmask_init(&state->needs_ss, mergedregs);
		}

		/* NOTE: consider dst register too.. it could happen that
		 * texture sample instruction (for example) writes some
		 * components which are unused.  A subsequent instruction
		 * that writes the same register can race w/ the sam instr
		 * resulting in undefined results:
		 */
		for (i = 0; i < n->regs_count; i++) {
			struct ir3_register *reg = n->regs[i];

			if (reg_gpr(reg)) {

				/* TODO: we probably only need (ss) for alu
				 * instr consuming sfu result.. need to make
				 * some tests for both this and (sy)..
				 */
				if (regmask_get(&state->needs_ss, reg)) {
					n->flags |= IR3_INSTR_SS;
					last_input_needs_ss = false;
					regmask_init(&state->needs_ss_war, mergedregs);
					regmask_init(&state->needs_ss, mergedregs);
				}

				if (regmask_get(&state->needs_sy, reg)) {
					n->flags |= IR3_INSTR_SY;
					regmask_init(&state->needs_sy, mergedregs);
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
			struct ir3_register *reg = n->regs[0];
			if (regmask_get(&state->needs_ss_war, reg)) {
				n->flags |= IR3_INSTR_SS;
				last_input_needs_ss = false;
				regmask_init(&state->needs_ss_war, mergedregs);
				regmask_init(&state->needs_ss, mergedregs);
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

		if (ctx->compiler->samgq_workaround &&
			ctx->type == MESA_SHADER_VERTEX && n->opc == OPC_SAMGQ) {
			struct ir3_instruction *samgp;

			list_delinit(&n->node);

			for (i = 0; i < 4; i++) {
				samgp = ir3_instr_clone(n);
				samgp->opc = OPC_SAMGP0 + i;
				if (i > 1)
					samgp->flags |= IR3_INSTR_SY;
			}
		} else {
			list_addtail(&n->node, &block->instr_list);
		}

		if (is_sfu(n))
			regmask_set(&state->needs_ss, n->regs[0]);

		if (is_tex_or_prefetch(n)) {
			regmask_set(&state->needs_sy, n->regs[0]);
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
			ctx->so->has_ssbo = true;

		/* both tex/sfu appear to not always immediately consume
		 * their src register(s):
		 */
		if (is_tex(n) || is_sfu(n) || is_mem(n)) {
			foreach_src (reg, n) {
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
			ir3_instr_move_after(baryf, last_input);

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

/* Expands dsxpp and dsypp macros to:
 *
 * dsxpp.1 dst, src
 * dsxpp.1.p dst, src
 *
 * We apply this after flags syncing, as we don't want to sync in between the
 * two (which might happen if dst == src).  We do it before nop scheduling
 * because that needs to count actual instructions.
 */
static bool
apply_fine_deriv_macro(struct ir3_legalize_ctx *ctx, struct ir3_block *block)
{
	struct list_head instr_list;

	/* remove all the instructions from the list, we'll be adding
	 * them back in as we go
	 */
	list_replace(&block->instr_list, &instr_list);
	list_inithead(&block->instr_list);

	foreach_instr_safe (n, &instr_list) {
		list_addtail(&n->node, &block->instr_list);

		if (n->opc == OPC_DSXPP_MACRO || n->opc == OPC_DSYPP_MACRO) {
			n->opc = (n->opc == OPC_DSXPP_MACRO) ? OPC_DSXPP_1 : OPC_DSYPP_1;

			struct ir3_instruction *op_p = ir3_instr_clone(n);
			op_p->flags = IR3_INSTR_P;

			ctx->so->need_fine_derivatives = true;
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
	foreach_block (block, &ir->block_list)
		foreach_instr (instr, &block->instr_list)
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
	foreach_block (block, &ir->block_list) {
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

/* Insert the branch/jump instructions for flow control between blocks.
 * Initially this is done naively, without considering if the successor
 * block immediately follows the current block (ie. so no jump required),
 * but that is cleaned up in resolve_jumps().
 *
 * TODO what ensures that the last write to p0.x in a block is the
 * branch condition?  Have we been getting lucky all this time?
 */
static void
block_sched(struct ir3 *ir)
{
	foreach_block (block, &ir->block_list) {
		if (block->successors[1]) {
			/* if/else, conditional branches to "then" or "else": */
			struct ir3_instruction *br;

			debug_assert(block->condition);

			/* create "else" branch first (since "then" block should
			 * frequently/always end up being a fall-thru):
			 */
			br = ir3_B(block, block->condition, 0);
			br->cat0.inv = true;
			br->cat0.target = block->successors[1];

			/* "then" branch: */
			br = ir3_B(block, block->condition, 0);
			br->cat0.target = block->successors[0];

		} else if (block->successors[0]) {
			/* otherwise unconditional jump to next block: */
			struct ir3_instruction *jmp;

			jmp = ir3_JUMP(block);
			jmp->cat0.target = block->successors[0];
		}
	}
}

/* Here we workaround the fact that kill doesn't actually kill the thread as
 * GL expects. The last instruction always needs to be an end instruction,
 * which means that if we're stuck in a loop where kill is the only way out,
 * then we may have to jump out to the end. kill may also have the d3d
 * semantics of converting the thread to a helper thread, rather than setting
 * the exec mask to 0, in which case the helper thread could get stuck in an
 * infinite loop.
 *
 * We do this late, both to give the scheduler the opportunity to reschedule
 * kill instructions earlier and to avoid having to create a separate basic
 * block.
 *
 * TODO: Assuming that the wavefront doesn't stop as soon as all threads are
 * killed, we might benefit by doing this more aggressively when the remaining
 * part of the program after the kill is large, since that would let us
 * skip over the instructions when there are no non-killed threads left.
 */
static void
kill_sched(struct ir3 *ir, struct ir3_shader_variant *so)
{
	/* True if we know that this block will always eventually lead to the end
	 * block:
	 */
	bool always_ends = true;
	bool added = false;
	struct ir3_block *last_block =
		list_last_entry(&ir->block_list, struct ir3_block, node);

	foreach_block_rev (block, &ir->block_list) {
		for (unsigned i = 0; i < 2 && block->successors[i]; i++) {
			if (block->successors[i]->start_ip <= block->end_ip)
				always_ends = false;
		}

		if (always_ends)
			continue;

		foreach_instr_safe (instr, &block->instr_list) {
			if (instr->opc != OPC_KILL)
				continue;

			struct ir3_instruction *br = ir3_instr_create(block, OPC_B);
			br->regs[1] = instr->regs[1];
			br->cat0.target =
				list_last_entry(&ir->block_list, struct ir3_block, node);

			list_del(&br->node);
			list_add(&br->node, &instr->node);

			added = true;
		}
	}

	if (added) {
		/* I'm not entirely sure how the branchstack works, but we probably
		 * need to add at least one entry for the divergence which is resolved
		 * at the end:
		 */
		so->branchstack++;

		/* We don't update predecessors/successors, so we have to do this
		 * manually:
		 */
		mark_jp(last_block);
	}
}

/* Insert nop's required to make this a legal/valid shader program: */
static void
nop_sched(struct ir3 *ir)
{
	foreach_block (block, &ir->block_list) {
		struct ir3_instruction *last = NULL;
		struct list_head instr_list;

		/* remove all the instructions from the list, we'll be adding
		 * them back in as we go
		 */
		list_replace(&block->instr_list, &instr_list);
		list_inithead(&block->instr_list);

		foreach_instr_safe (instr, &instr_list) {
			unsigned delay = ir3_delay_calc(block, instr, false, true);

			/* NOTE: I think the nopN encoding works for a5xx and
			 * probably a4xx, but not a3xx.  So far only tested on
			 * a6xx.
			 */

			if ((delay > 0) && (ir->compiler->gpu_id >= 600) && last &&
					((opc_cat(last->opc) == 2) || (opc_cat(last->opc) == 3)) &&
					(last->repeat == 0)) {
				/* the previous cat2/cat3 instruction can encode at most 3 nop's: */
				unsigned transfer = MIN2(delay, 3 - last->nop);
				last->nop += transfer;
				delay -= transfer;
			}

			if ((delay > 0) && last && (last->opc == OPC_NOP)) {
				/* the previous nop can encode at most 5 repeats: */
				unsigned transfer = MIN2(delay, 5 - last->repeat);
				last->repeat += transfer;
				delay -= transfer;
			}

			if (delay > 0) {
				debug_assert(delay <= 6);
				ir3_NOP(block)->repeat = delay - 1;
			}

			list_addtail(&instr->node, &block->instr_list);
			last = instr;
		}
	}
}

bool
ir3_legalize(struct ir3 *ir, struct ir3_shader_variant *so, int *max_bary)
{
	struct ir3_legalize_ctx *ctx = rzalloc(ir, struct ir3_legalize_ctx);
	bool mergedregs = so->mergedregs;
	bool progress;

	ctx->so = so;
	ctx->max_bary = -1;
	ctx->compiler = ir->compiler;
	ctx->type = ir->type;

	/* allocate per-block data: */
	foreach_block (block, &ir->block_list) {
		struct ir3_legalize_block_data *bd =
				rzalloc(ctx, struct ir3_legalize_block_data);

		regmask_init(&bd->state.needs_ss_war, mergedregs);
		regmask_init(&bd->state.needs_ss, mergedregs);
		regmask_init(&bd->state.needs_sy, mergedregs);

		block->data = bd;
	}

	ir3_remove_nops(ir);

	/* process each block: */
	do {
		progress = false;
		foreach_block (block, &ir->block_list) {
			progress |= legalize_block(ctx, block);
		}
	} while (progress);

	*max_bary = ctx->max_bary;

	block_sched(ir);
	if (so->type == MESA_SHADER_FRAGMENT)
		kill_sched(ir, so);

	foreach_block (block, &ir->block_list) {
		progress |= apply_fine_deriv_macro(ctx, block);
	}

	nop_sched(ir);

	do {
		ir3_count_instructions(ir);
	} while(resolve_jumps(ir));

	mark_xvergence_points(ir);

	ralloc_free(ctx);

	return true;
}
