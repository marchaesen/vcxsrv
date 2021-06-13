/*
 * Copyright (C) 2019 Google, Inc.
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


#include "util/dag.h"
#include "util/u_math.h"

#include "ir3.h"
#include "ir3_compiler.h"
#include "ir3_context.h"

#ifdef DEBUG
#define SCHED_DEBUG (ir3_shader_debug & IR3_DBG_SCHEDMSGS)
#else
#define SCHED_DEBUG 0
#endif
#define d(fmt, ...) do { if (SCHED_DEBUG) { \
	printf("PSCHED: "fmt"\n", ##__VA_ARGS__); \
} } while (0)

#define di(instr, fmt, ...) do { if (SCHED_DEBUG) { \
	printf("PSCHED: "fmt": ", ##__VA_ARGS__); \
	ir3_print_instr(instr); \
} } while (0)

/*
 * Post RA Instruction Scheduling
 */

struct ir3_postsched_ctx {
	struct ir3 *ir;

	struct ir3_shader_variant *v;

	void *mem_ctx;
	struct ir3_block *block;           /* the current block */
	struct dag *dag;

	struct list_head unscheduled_list; /* unscheduled instructions */

	int sfu_delay;
	int tex_delay;
};

struct ir3_postsched_node {
	struct dag_node dag;     /* must be first for util_dynarray_foreach */
	struct ir3_instruction *instr;
	bool partially_evaluated_path;

	unsigned delay;
	unsigned max_delay;
};

#define foreach_sched_node(__n, __list) \
	list_for_each_entry(struct ir3_postsched_node, __n, __list, dag.link)

static void
schedule(struct ir3_postsched_ctx *ctx, struct ir3_instruction *instr)
{
	debug_assert(ctx->block == instr->block);

	/* remove from unscheduled_list:
	 */
	list_delinit(&instr->node);

	di(instr, "schedule");

	list_addtail(&instr->node, &instr->block->instr_list);

	struct ir3_postsched_node *n = instr->data;
	dag_prune_head(ctx->dag, &n->dag);

	if (is_meta(instr) && (instr->opc != OPC_META_TEX_PREFETCH))
		return;

	if (is_sfu(instr)) {
		ctx->sfu_delay = 8;
	} else if (check_src_cond(instr, is_sfu)) {
		ctx->sfu_delay = 0;
	} else if (ctx->sfu_delay > 0) {
		ctx->sfu_delay--;
	}

	if (is_tex_or_prefetch(instr)) {
		ctx->tex_delay = 10;
	} else if (check_src_cond(instr, is_tex_or_prefetch)) {
		ctx->tex_delay = 0;
	} else if (ctx->tex_delay > 0) {
		ctx->tex_delay--;
	}
}

static void
dump_state(struct ir3_postsched_ctx *ctx)
{
	if (!SCHED_DEBUG)
		return;

	foreach_sched_node (n, &ctx->dag->heads) {
		di(n->instr, "maxdel=%3d    ", n->max_delay);

		util_dynarray_foreach(&n->dag.edges, struct dag_edge, edge) {
			struct ir3_postsched_node *child =
				(struct ir3_postsched_node *)edge->child;

			di(child->instr, " -> (%d parents) ", child->dag.parent_count);
		}
	}
}

/* Determine if this is an instruction that we'd prefer not to schedule
 * yet, in order to avoid an (ss) sync.  This is limited by the sfu_delay
 * counter, ie. the more cycles it has been since the last SFU, the less
 * costly a sync would be.
 */
static bool
would_sync(struct ir3_postsched_ctx *ctx, struct ir3_instruction *instr)
{
	if (ctx->sfu_delay) {
		if (check_src_cond(instr, is_sfu))
			return true;
	}

	if (ctx->tex_delay) {
		if (check_src_cond(instr, is_tex_or_prefetch))
			return true;
	}

	return false;
}

/* find instruction to schedule: */
static struct ir3_instruction *
choose_instr(struct ir3_postsched_ctx *ctx)
{
	struct ir3_postsched_node *chosen = NULL;

	dump_state(ctx);

	foreach_sched_node (n, &ctx->dag->heads) {
		if (!is_meta(n->instr))
			continue;

		if (!chosen || (chosen->max_delay < n->max_delay))
			chosen = n;
	}

	if (chosen) {
		di(chosen->instr, "prio: chose (meta)");
		return chosen->instr;
	}

	/* Try to schedule inputs with a higher priority, if possible, as
	 * the last bary.f unlocks varying storage to unblock more VS
	 * warps.
	 */
	foreach_sched_node (n, &ctx->dag->heads) {
		if (!is_input(n->instr))
			continue;

		if (!chosen || (chosen->max_delay < n->max_delay))
			chosen = n;
	}

	if (chosen) {
		di(chosen->instr, "prio: chose (input)");
		return chosen->instr;
	}

	/* Next prioritize discards: */
	foreach_sched_node (n, &ctx->dag->heads) {
		unsigned d = ir3_delay_calc(ctx->block, n->instr, false, false);

		if (d > 0)
			continue;

		if (!is_kill(n->instr))
			continue;

		if (!chosen || (chosen->max_delay < n->max_delay))
			chosen = n;
	}

	if (chosen) {
		di(chosen->instr, "csp: chose (kill, hard ready)");
		return chosen->instr;
	}

	/* Next prioritize expensive instructions: */
	foreach_sched_node (n, &ctx->dag->heads) {
		unsigned d = ir3_delay_calc(ctx->block, n->instr, false, false);

		if (d > 0)
			continue;

		if (!(is_sfu(n->instr) || is_tex(n->instr)))
			continue;

		if (!chosen || (chosen->max_delay < n->max_delay))
			chosen = n;
	}

	if (chosen) {
		di(chosen->instr, "csp: chose (sfu/tex, hard ready)");
		return chosen->instr;
	}

	/*
	 * Sometimes be better to take a nop, rather than scheduling an
	 * instruction that would require an (ss) shortly after another
	 * SFU..  ie. if last SFU was just one or two instr ago, and we
	 * could choose between taking a nop and then scheduling
	 * something else, vs scheduling the immed avail instruction that
	 * would require (ss), we are better with the nop.
	 */
	for (unsigned delay = 0; delay < 4; delay++) {
		foreach_sched_node (n, &ctx->dag->heads) {
			if (would_sync(ctx, n->instr))
				continue;

			unsigned d = ir3_delay_calc(ctx->block, n->instr, true, false);

			if (d > delay)
				continue;

			if (!chosen || (chosen->max_delay < n->max_delay))
				chosen = n;
		}

		if (chosen) {
			di(chosen->instr, "csp: chose (soft ready, delay=%u)", delay);
			return chosen->instr;
		}
	}

	/* Next try to find a ready leader w/ soft delay (ie. including extra
	 * delay for things like tex fetch which can be synchronized w/ sync
	 * bit (but we probably do want to schedule some other instructions
	 * while we wait)
	 */
	foreach_sched_node (n, &ctx->dag->heads) {
		unsigned d = ir3_delay_calc(ctx->block, n->instr, true, false);

		if (d > 0)
			continue;

		if (!chosen || (chosen->max_delay < n->max_delay))
			chosen = n;
	}

	if (chosen) {
		di(chosen->instr, "csp: chose (soft ready)");
		return chosen->instr;
	}

	/* Next try to find a ready leader that can be scheduled without nop's,
	 * which in the case of things that need (sy)/(ss) could result in
	 * stalls.. but we've already decided there is not a better option.
	 */
	foreach_sched_node (n, &ctx->dag->heads) {
		unsigned d = ir3_delay_calc(ctx->block, n->instr, false, false);

		if (d > 0)
			continue;

		if (!chosen || (chosen->max_delay < n->max_delay))
			chosen = n;
	}

	if (chosen) {
		di(chosen->instr, "csp: chose (hard ready)");
		return chosen->instr;
	}

	/* Otherwise choose leader with maximum cost:
	 *
	 * TODO should we try to balance cost and delays?  I guess it is
	 * a balance between now-nop's and future-nop's?
	 */
	foreach_sched_node (n, &ctx->dag->heads) {
		if (!chosen || chosen->max_delay < n->max_delay)
			chosen = n;
	}

	if (chosen) {
		di(chosen->instr, "csp: chose (leader)");
		return chosen->instr;
	}

	return NULL;
}

struct ir3_postsched_deps_state {
	struct ir3_postsched_ctx *ctx;

	enum { F, R } direction;

	bool merged;

	/* Track the mapping between sched node (instruction) that last
	 * wrote a given register (in whichever direction we are iterating
	 * the block)
	 *
	 * Note, this table is twice as big as the # of regs, to deal with
	 * half-precision regs.  The approach differs depending on whether
	 * the half and full precision register files are "merged" (conflict,
	 * ie. a6xx+) in which case we consider each full precision dep
	 * as two half-precision dependencies, vs older separate (non-
	 * conflicting) in which case the first half of the table is used
	 * for full precision and 2nd half for half-precision.
	 */
	struct ir3_postsched_node *regs[2 * 256];
};

/* bounds checking read/write accessors, since OoB access to stuff on
 * the stack is gonna cause a bad day.
 */
#define dep_reg(state, idx) *({ \
		assert((idx) < ARRAY_SIZE((state)->regs)); \
		&(state)->regs[(idx)]; \
	})

static void
add_dep(struct ir3_postsched_deps_state *state,
		struct ir3_postsched_node *before,
		struct ir3_postsched_node *after)
{
	if (!before || !after)
		return;

	assert(before != after);

	if (state->direction == F) {
		dag_add_edge(&before->dag, &after->dag, NULL);
	} else {
		dag_add_edge(&after->dag, &before->dag, NULL);
	}
}

static void
add_single_reg_dep(struct ir3_postsched_deps_state *state,
		struct ir3_postsched_node *node, unsigned num, bool write)
{
	add_dep(state, dep_reg(state, num), node);
	if (write) {
		dep_reg(state, num) = node;
	}
}

/* This is where we handled full vs half-precision, and potential conflicts
 * between half and full precision that result in additional dependencies.
 * The 'reg' arg is really just to know half vs full precision.
 */
static void
add_reg_dep(struct ir3_postsched_deps_state *state,
		struct ir3_postsched_node *node, const struct ir3_register *reg,
		unsigned num, bool write)
{
	if (state->merged) {
		if (reg->flags & IR3_REG_HALF) {
			/* single conflict in half-reg space: */
			add_single_reg_dep(state, node, num, write);
		} else {
			/* two conflicts in half-reg space: */
			add_single_reg_dep(state, node, 2 * num + 0, write);
			add_single_reg_dep(state, node, 2 * num + 1, write);
		}
	} else {
		if (reg->flags & IR3_REG_HALF)
			num += ARRAY_SIZE(state->regs) / 2;
		add_single_reg_dep(state, node, num, write);
	}
}

static void
calculate_deps(struct ir3_postsched_deps_state *state,
		struct ir3_postsched_node *node)
{
	/* Add dependencies on instructions that previously (or next,
	 * in the reverse direction) wrote any of our src registers:
	 */
	foreach_src_n (reg, i, node->instr) {
		if (reg->flags & (IR3_REG_CONST | IR3_REG_IMMED))
			continue;

		if (reg->flags & IR3_REG_RELATIV) {
			/* mark entire array as read: */
			struct ir3_array *arr = ir3_lookup_array(state->ctx->ir, reg->array.id);
			for (unsigned i = 0; i < arr->length; i++) {
				add_reg_dep(state, node, reg, arr->reg + i, false);
			}
		} else {
			assert(reg->wrmask >= 1);
			u_foreach_bit (b, reg->wrmask) {
				add_reg_dep(state, node, reg, reg->num + b, false);

				struct ir3_postsched_node *dep = dep_reg(state, reg->num + b);
				if (dep && (state->direction == F)) {
					unsigned d = ir3_delayslots(dep->instr, node->instr, i, true);
					node->delay = MAX2(node->delay, d);
				}
			}
		}
	}

	if (node->instr->address) {
		add_reg_dep(state, node, node->instr->address->regs[0],
					node->instr->address->regs[0]->num,
					false);
	}

	if (dest_regs(node->instr) == 0)
		return;

	/* And then after we update the state for what this instruction
	 * wrote:
	 */
	struct ir3_register *reg = node->instr->regs[0];
	if (reg->flags & IR3_REG_RELATIV) {
		/* mark the entire array as written: */
		struct ir3_array *arr = ir3_lookup_array(state->ctx->ir, reg->array.id);
		for (unsigned i = 0; i < arr->length; i++) {
			add_reg_dep(state, node, reg, arr->reg + i, true);
		}
	} else {
		assert(reg->wrmask >= 1);
		u_foreach_bit (b, reg->wrmask) {
			add_reg_dep(state, node, reg, reg->num + b, true);
		}
	}
}

static void
calculate_forward_deps(struct ir3_postsched_ctx *ctx)
{
	struct ir3_postsched_deps_state state = {
			.ctx = ctx,
			.direction = F,
			.merged = ctx->v->mergedregs,
	};

	foreach_instr (instr, &ctx->unscheduled_list) {
		calculate_deps(&state, instr->data);
	}
}

static void
calculate_reverse_deps(struct ir3_postsched_ctx *ctx)
{
	struct ir3_postsched_deps_state state = {
			.ctx = ctx,
			.direction = R,
			.merged = ctx->v->mergedregs,
	};

	foreach_instr_rev (instr, &ctx->unscheduled_list) {
		calculate_deps(&state, instr->data);
	}
}

static void
sched_node_init(struct ir3_postsched_ctx *ctx, struct ir3_instruction *instr)
{
	struct ir3_postsched_node *n = rzalloc(ctx->mem_ctx, struct ir3_postsched_node);

	dag_init_node(ctx->dag, &n->dag);

	n->instr = instr;
	instr->data = n;
}

static void
sched_dag_max_delay_cb(struct dag_node *node, void *state)
{
	struct ir3_postsched_node *n = (struct ir3_postsched_node *)node;
	uint32_t max_delay = 0;

	util_dynarray_foreach(&n->dag.edges, struct dag_edge, edge) {
		struct ir3_postsched_node *child = (struct ir3_postsched_node *)edge->child;
		max_delay = MAX2(child->max_delay, max_delay);
	}

	n->max_delay = MAX2(n->max_delay, max_delay + n->delay);
}

static void
sched_dag_init(struct ir3_postsched_ctx *ctx)
{
	ctx->mem_ctx = ralloc_context(NULL);

	ctx->dag = dag_create(ctx->mem_ctx);

	foreach_instr (instr, &ctx->unscheduled_list)
		sched_node_init(ctx, instr);

	calculate_forward_deps(ctx);
	calculate_reverse_deps(ctx);

	/*
	 * To avoid expensive texture fetches, etc, from being moved ahead
	 * of kills, track the kills we've seen so far, so we can add an
	 * extra dependency on them for tex/mem instructions
	 */
	struct util_dynarray kills;
	util_dynarray_init(&kills, ctx->mem_ctx);

	/*
	 * Normal srcs won't be in SSA at this point, those are dealt with in
	 * calculate_forward_deps() and calculate_reverse_deps().  But we still
	 * have the false-dep information in SSA form, so go ahead and add
	 * dependencies for that here:
	 */
	foreach_instr (instr, &ctx->unscheduled_list) {
		struct ir3_postsched_node *n = instr->data;

		foreach_ssa_src_n (src, i, instr) {
			if (src->block != instr->block)
				continue;

			/* we can end up with unused false-deps.. just skip them: */
			if (src->flags & IR3_INSTR_UNUSED)
				continue;

			struct ir3_postsched_node *sn = src->data;

			/* don't consider dependencies in other blocks: */
			if (src->block != instr->block)
				continue;

			dag_add_edge(&sn->dag, &n->dag, NULL);
		}

		if (is_kill(instr)) {
			util_dynarray_append(&kills, struct ir3_instruction *, instr);
		} else if (is_tex(instr) || is_mem(instr)) {
			util_dynarray_foreach(&kills, struct ir3_instruction *, instrp) {
				struct ir3_instruction *kill = *instrp;
				struct ir3_postsched_node *kn = kill->data;
				dag_add_edge(&kn->dag, &n->dag, NULL);
			}
		}
	}

	// TODO do we want to do this after reverse-dependencies?
	dag_traverse_bottom_up(ctx->dag, sched_dag_max_delay_cb, NULL);
}

static void
sched_dag_destroy(struct ir3_postsched_ctx *ctx)
{
	ralloc_free(ctx->mem_ctx);
	ctx->mem_ctx = NULL;
	ctx->dag = NULL;
}

static void
sched_block(struct ir3_postsched_ctx *ctx, struct ir3_block *block)
{
	ctx->block = block;
	ctx->tex_delay = 0;
	ctx->sfu_delay = 0;

	/* move all instructions to the unscheduled list, and
	 * empty the block's instruction list (to which we will
	 * be inserting).
	 */
	list_replace(&block->instr_list, &ctx->unscheduled_list);
	list_inithead(&block->instr_list);

	// TODO once we are using post-sched for everything we can
	// just not stick in NOP's prior to post-sched, and drop this.
	// for now keep this, since it makes post-sched optional:
	foreach_instr_safe (instr, &ctx->unscheduled_list) {
		switch (instr->opc) {
		case OPC_NOP:
		case OPC_B:
		case OPC_JUMP:
			list_delinit(&instr->node);
			break;
		default:
			break;
		}
	}

	sched_dag_init(ctx);

	/* First schedule all meta:input instructions, followed by
	 * tex-prefetch.  We want all of the instructions that load
	 * values into registers before the shader starts to go
	 * before any other instructions.  But in particular we
	 * want inputs to come before prefetches.  This is because
	 * a FS's bary_ij input may not actually be live in the
	 * shader, but it should not be scheduled on top of any
	 * other input (but can be overwritten by a tex prefetch)
	 */
	foreach_instr_safe (instr, &ctx->unscheduled_list)
		if (instr->opc == OPC_META_INPUT)
			schedule(ctx, instr);

	foreach_instr_safe (instr, &ctx->unscheduled_list)
		if (instr->opc == OPC_META_TEX_PREFETCH)
			schedule(ctx, instr);

	while (!list_is_empty(&ctx->unscheduled_list)) {
		struct ir3_instruction *instr = choose_instr(ctx);

		unsigned delay = ir3_delay_calc(ctx->block, instr, false, false);
		d("delay=%u", delay);

		/* and if we run out of instructions that can be scheduled,
		 * then it is time for nop's:
		 */
		debug_assert(delay <= 6);
		while (delay > 0) {
			ir3_NOP(block);
			delay--;
		}

		schedule(ctx, instr);
	}

	sched_dag_destroy(ctx);
}


static bool
is_self_mov(struct ir3_instruction *instr)
{
	if (!is_same_type_mov(instr))
		return false;

	if (instr->regs[0]->num != instr->regs[1]->num)
		return false;

	if (instr->regs[0]->flags & IR3_REG_RELATIV)
		return false;

	if (instr->regs[1]->flags & (IR3_REG_CONST | IR3_REG_IMMED |
			IR3_REG_RELATIV | IR3_REG_FNEG | IR3_REG_FABS |
			IR3_REG_SNEG | IR3_REG_SABS | IR3_REG_BNOT |
			IR3_REG_EVEN | IR3_REG_POS_INF))
		return false;

	return true;
}

/* sometimes we end up w/ in-place mov's, ie. mov.u32u32 r1.y, r1.y
 * as a result of places were before RA we are not sure that it is
 * safe to eliminate.  We could eliminate these earlier, but sometimes
 * they are tangled up in false-dep's, etc, so it is easier just to
 * let them exist until after RA
 */
static void
cleanup_self_movs(struct ir3 *ir)
{
	foreach_block (block, &ir->block_list) {
		foreach_instr_safe (instr, &block->instr_list) {

			foreach_src (reg, instr) {
				if (!reg->instr)
					continue;

				if (is_self_mov(reg->instr)) {
					list_delinit(&reg->instr->node);
					reg->instr = reg->instr->regs[1]->instr;
				}
			}

			for (unsigned i = 0; i < instr->deps_count; i++) {
				if (instr->deps[i] && is_self_mov(instr->deps[i])) {
					list_delinit(&instr->deps[i]->node);
					instr->deps[i] = instr->deps[i]->regs[1]->instr;
				}
			}
		}
	}
}

bool
ir3_postsched(struct ir3 *ir, struct ir3_shader_variant *v)
{
	struct ir3_postsched_ctx ctx = {
			.ir = ir,
			.v  = v,
	};

	ir3_remove_nops(ir);
	cleanup_self_movs(ir);

	foreach_block (block, &ir->block_list) {
		sched_block(&ctx, block);
	}

	return true;
}
