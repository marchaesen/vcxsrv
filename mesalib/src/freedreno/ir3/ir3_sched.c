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


#include "util/u_math.h"

#include "ir3.h"
#include "ir3_compiler.h"

#ifdef DEBUG
#define SCHED_DEBUG (ir3_shader_debug & IR3_DBG_SCHEDMSGS)
#else
#define SCHED_DEBUG 0
#endif
#define d(fmt, ...) do { if (SCHED_DEBUG) { \
	printf("SCHED: "fmt"\n", ##__VA_ARGS__); \
} } while (0)

#define di(instr, fmt, ...) do { if (SCHED_DEBUG) { \
	printf("SCHED: "fmt": ", ##__VA_ARGS__); \
	ir3_print_instr(instr); \
} } while (0)

/*
 * Instruction Scheduling:
 *
 * A recursive depth based scheduling algo.  Recursively find an eligible
 * instruction to schedule from the deepest instruction (recursing through
 * it's unscheduled src instructions).  Normally this would result in a
 * lot of re-traversal of the same instructions, so we cache results in
 * instr->data (and clear cached results that would be no longer valid
 * after scheduling an instruction).
 *
 * There are a few special cases that need to be handled, since sched
 * is currently independent of register allocation.  Usages of address
 * register (a0.x) or predicate register (p0.x) must be serialized.  Ie.
 * if you have two pairs of instructions that write the same special
 * register and then read it, then those pairs cannot be interleaved.
 * To solve this, when we are in such a scheduling "critical section",
 * and we encounter a conflicting write to a special register, we try
 * to schedule any remaining instructions that use that value first.
 */

struct ir3_sched_ctx {
	struct ir3_block *block;           /* the current block */
	struct list_head depth_list;       /* depth sorted unscheduled instrs */
	struct ir3_instruction *scheduled; /* last scheduled instr XXX remove*/
	struct ir3_instruction *addr;      /* current a0.x user, if any */
	struct ir3_instruction *pred;      /* current p0.x user, if any */
	int live_values;                   /* estimate of current live values */
	int half_live_values;              /* estimate of current half precision live values */
	bool error;

	unsigned live_threshold_hi;
	unsigned live_threshold_lo;
	unsigned depth_threshold_hi;
	unsigned depth_threshold_lo;
};

static bool is_scheduled(struct ir3_instruction *instr)
{
	return !!(instr->flags & IR3_INSTR_MARK);
}

static void
unuse_each_src(struct ir3_sched_ctx *ctx, struct ir3_instruction *instr)
{
	struct ir3_instruction *src;

	foreach_ssa_src_n (src, n, instr) {
		if (__is_false_dep(instr, n))
			continue;
		if (instr->block != src->block)
			continue;
		if ((src->opc == OPC_META_COLLECT) || (src->opc == OPC_META_SPLIT)) {
			unuse_each_src(ctx, src);
		} else {
			debug_assert(src->use_count > 0);

			if (--src->use_count == 0) {
				if (is_half(src)) {
					ctx->half_live_values -= dest_regs(src);
					debug_assert(ctx->half_live_values >= 0);
				} else {
					ctx->live_values -= dest_regs(src);
					debug_assert(ctx->live_values >= 0);
				}
			}
		}
	}
}

static void clear_cache(struct ir3_sched_ctx *ctx, struct ir3_instruction *instr);
static void use_instr(struct ir3_instruction *instr);

/* transfers a use-count to new instruction, for cases where we
 * "spill" address or predicate.  Note this might cause the
 * previous instruction that loaded a0.x/p0.x to become live
 * again, when we previously thought it was dead.
 */
static void
transfer_use(struct ir3_sched_ctx *ctx, struct ir3_instruction *orig_instr,
		struct ir3_instruction *new_instr)
{
	struct ir3_instruction *src;

	debug_assert(is_scheduled(orig_instr));

	foreach_ssa_src_n (src, n, new_instr) {
		if (__is_false_dep(new_instr, n))
			continue;
		if (is_half(new_instr)) {
			ctx->half_live_values += dest_regs(src);
		} else {
			ctx->live_values += dest_regs(src);
		}
		use_instr(src);
	}

	clear_cache(ctx, orig_instr);
}

static void
use_each_src(struct ir3_instruction *instr)
{
	struct ir3_instruction *src;

	foreach_ssa_src_n (src, n, instr) {
		if (__is_false_dep(instr, n))
			continue;
		use_instr(src);
	}
}

static void
use_instr(struct ir3_instruction *instr)
{
	if ((instr->opc == OPC_META_COLLECT) || (instr->opc == OPC_META_SPLIT)) {
		use_each_src(instr);
	} else {
		instr->use_count++;
	}
}

static void
update_live_values(struct ir3_sched_ctx *ctx, struct ir3_instruction *scheduled)
{
	if ((scheduled->opc == OPC_META_COLLECT) || (scheduled->opc == OPC_META_SPLIT))
		return;

	if ((scheduled->regs_count > 0) && is_half(scheduled)) {
		ctx->half_live_values += dest_regs(scheduled);
	} else {
		ctx->live_values += dest_regs(scheduled);
	}

	unuse_each_src(ctx, scheduled);
}

static void
update_use_count(struct ir3 *ir)
{
	foreach_block (block, &ir->block_list) {
		foreach_instr (instr, &block->instr_list) {
			instr->use_count = 0;
		}
	}

	foreach_block (block, &ir->block_list) {
		foreach_instr (instr, &block->instr_list) {
			if ((instr->opc == OPC_META_COLLECT) || (instr->opc == OPC_META_SPLIT))
				continue;

			use_each_src(instr);
		}
	}

	/* Shader outputs are also used:
	 */
	struct ir3_instruction *out;
	foreach_output (out, ir)
		use_instr(out);
}

#define NULL_INSTR ((void *)~0)

static void
clear_cache(struct ir3_sched_ctx *ctx, struct ir3_instruction *instr)
{
	foreach_instr (instr2, &ctx->depth_list) {
		if ((instr2->data == instr) || (instr2->data == NULL_INSTR) || !instr)
			instr2->data = NULL;
	}
}

static void
schedule(struct ir3_sched_ctx *ctx, struct ir3_instruction *instr)
{
	debug_assert(ctx->block == instr->block);

	/* remove from depth list:
	 */
	list_delinit(&instr->node);

	if (writes_addr(instr)) {
		debug_assert(ctx->addr == NULL);
		ctx->addr = instr;
	}

	if (writes_pred(instr)) {
		debug_assert(ctx->pred == NULL);
		ctx->pred = instr;
	}

	instr->flags |= IR3_INSTR_MARK;

	di(instr, "schedule");

	list_addtail(&instr->node, &instr->block->instr_list);
	ctx->scheduled = instr;

	update_live_values(ctx, instr);

	if (writes_addr(instr) || writes_pred(instr) || is_input(instr)) {
		clear_cache(ctx, NULL);
	} else {
		/* invalidate only the necessary entries.. */
		clear_cache(ctx, instr);
	}
}

static struct ir3_instruction *
deepest(struct ir3_instruction **srcs, unsigned nsrcs)
{
	struct ir3_instruction *d = NULL;
	unsigned i = 0, id = 0;

	while ((i < nsrcs) && !(d = srcs[id = i]))
		i++;

	if (!d)
		return NULL;

	for (; i < nsrcs; i++)
		if (srcs[i] && (srcs[i]->depth > d->depth))
			d = srcs[id = i];

	srcs[id] = NULL;

	return d;
}

struct ir3_sched_notes {
	/* there is at least one kill which could be scheduled, except
	 * for unscheduled bary.f's:
	 */
	bool blocked_kill;
	/* there is at least one instruction that could be scheduled,
	 * except for conflicting address/predicate register usage:
	 */
	bool addr_conflict, pred_conflict;
};

/* could an instruction be scheduled if specified ssa src was scheduled? */
static bool
could_sched(struct ir3_instruction *instr, struct ir3_instruction *src)
{
	struct ir3_instruction *other_src;
	foreach_ssa_src (other_src, instr) {
		/* if dependency not scheduled, we aren't ready yet: */
		if ((src != other_src) && !is_scheduled(other_src)) {
			return false;
		}
	}
	return true;
}

/* Check if instruction is ok to schedule.  Make sure it is not blocked
 * by use of addr/predicate register, etc.
 */
static bool
check_instr(struct ir3_sched_ctx *ctx, struct ir3_sched_notes *notes,
		struct ir3_instruction *instr)
{
	debug_assert(!is_scheduled(instr));

	/* For instructions that write address register we need to
	 * make sure there is at least one instruction that uses the
	 * addr value which is otherwise ready.
	 *
	 * TODO if any instructions use pred register and have other
	 * src args, we would need to do the same for writes_pred()..
	 */
	if (writes_addr(instr)) {
		struct ir3 *ir = instr->block->shader;
		bool ready = false;
		for (unsigned i = 0; (i < ir->indirects_count) && !ready; i++) {
			struct ir3_instruction *indirect = ir->indirects[i];
			if (!indirect)
				continue;
			if (indirect->address != instr)
				continue;
			ready = could_sched(indirect, instr);
		}

		/* nothing could be scheduled, so keep looking: */
		if (!ready)
			return false;
	}

	/* if this is a write to address/predicate register, and that
	 * register is currently in use, we need to defer until it is
	 * free:
	 */
	if (writes_addr(instr) && ctx->addr) {
		debug_assert(ctx->addr != instr);
		notes->addr_conflict = true;
		return false;
	}

	if (writes_pred(instr) && ctx->pred) {
		debug_assert(ctx->pred != instr);
		notes->pred_conflict = true;
		return false;
	}

	/* if the instruction is a kill, we need to ensure *every*
	 * bary.f is scheduled.  The hw seems unhappy if the thread
	 * gets killed before the end-input (ei) flag is hit.
	 *
	 * We could do this by adding each bary.f instruction as
	 * virtual ssa src for the kill instruction.  But we have
	 * fixed length instr->regs[].
	 *
	 * TODO this wouldn't be quite right if we had multiple
	 * basic blocks, if any block was conditional.  We'd need
	 * to schedule the bary.f's outside of any block which
	 * was conditional that contained a kill.. I think..
	 */
	if (is_kill(instr)) {
		struct ir3 *ir = instr->block->shader;

		for (unsigned i = 0; i < ir->baryfs_count; i++) {
			struct ir3_instruction *baryf = ir->baryfs[i];
			if (baryf->flags & IR3_INSTR_UNUSED)
				continue;
			if (!is_scheduled(baryf)) {
				notes->blocked_kill = true;
				return false;
			}
		}
	}

	return true;
}

/* Find the best instruction to schedule from specified instruction or
 * recursively it's ssa sources.
 */
static struct ir3_instruction *
find_instr_recursive(struct ir3_sched_ctx *ctx, struct ir3_sched_notes *notes,
		struct ir3_instruction *instr)
{
	struct ir3_instruction *srcs[__ssa_src_cnt(instr)];
	struct ir3_instruction *src;
	unsigned nsrcs = 0;

	if (is_scheduled(instr))
		return NULL;

	/* use instr->data to cache the results of recursing up the
	 * instr src's.  Otherwise the recursive algo can scale quite
	 * badly w/ shader size.  But this takes some care to clear
	 * the cache appropriately when instructions are scheduled.
	 */
	if (instr->data) {
		if (instr->data == NULL_INSTR)
			return NULL;
		return instr->data;
	}

	/* find unscheduled srcs: */
	foreach_ssa_src (src, instr) {
		if (!is_scheduled(src) && (src->block == instr->block)) {
			debug_assert(nsrcs < ARRAY_SIZE(srcs));
			srcs[nsrcs++] = src;
		}
	}

	/* if all our src's are already scheduled: */
	if (nsrcs == 0) {
		if (check_instr(ctx, notes, instr)) {
			instr->data = instr;
			return instr;
		}
		return NULL;
	}

	while ((src = deepest(srcs, nsrcs))) {
		struct ir3_instruction *candidate;

		candidate = find_instr_recursive(ctx, notes, src);
		if (!candidate)
			continue;

		if (check_instr(ctx, notes, candidate)) {
			instr->data = candidate;
			return candidate;
		}
	}

	instr->data = NULL_INSTR;
	return NULL;
}

/* find net change to live values if instruction were scheduled: */
static int
live_effect(struct ir3_instruction *instr)
{
	struct ir3_instruction *src;
	int new_live = dest_regs(instr);
	int old_live = 0;

	foreach_ssa_src_n (src, n, instr) {
		if (__is_false_dep(instr, n))
			continue;

		if (instr->block != src->block)
			continue;

		/* for split, just pass things along to the real src: */
		if (src->opc == OPC_META_SPLIT)
			src = ssa(src->regs[1]);

		/* for collect, if this is the last use of *each* src,
		 * then it will decrease the live values, since RA treats
		 * them as a whole:
		 */
		if (src->opc == OPC_META_COLLECT) {
			struct ir3_instruction *src2;
			bool last_use = true;

			foreach_ssa_src (src2, src) {
				if (src2->use_count > 1) {
					last_use = false;
					break;
				}
			}

			if (last_use)
				old_live += dest_regs(src);

		} else {
			debug_assert(src->use_count > 0);

			if (src->use_count == 1) {
				old_live += dest_regs(src);
			}
		}
	}

	return new_live - old_live;
}

/* find instruction to schedule: */
static struct ir3_instruction *
find_eligible_instr(struct ir3_sched_ctx *ctx, struct ir3_sched_notes *notes,
		bool soft)
{
	struct ir3_instruction *best_instr = NULL;
	int best_rank = INT_MAX;      /* lower is better */
	unsigned deepest = 0;

	/* TODO we'd really rather use the list/array of block outputs.  But we
	 * don't have such a thing.  Recursing *every* instruction in the list
	 * will result in a lot of repeated traversal, since instructions will
	 * get traversed both when they appear as ssa src to a later instruction
	 * as well as where they appear in the depth_list.
	 */
	foreach_instr_rev (instr, &ctx->depth_list) {
		struct ir3_instruction *candidate;

		candidate = find_instr_recursive(ctx, notes, instr);
		if (!candidate)
			continue;

		if (is_meta(candidate))
			return candidate;

		deepest = MAX2(deepest, candidate->depth);
	}

	/* traverse the list a second time.. but since we cache the result of
	 * find_instr_recursive() it isn't as bad as it looks.
	 */
	foreach_instr_rev (instr, &ctx->depth_list) {
		struct ir3_instruction *candidate;

		candidate = find_instr_recursive(ctx, notes, instr);
		if (!candidate)
			continue;

		/* determine net change to # of live values: */
		int le = live_effect(candidate);
		unsigned live_values = (2 * ctx->live_values) + ctx->half_live_values;

		/* if there is a net increase in # of live values, then apply some
		 * threshold to avoid instructions getting scheduled *too* early
		 * and increasing register pressure.
		 */
		if (le >= 1) {
			unsigned threshold;

			if (live_values > ctx->live_threshold_lo) {
				threshold = ctx->depth_threshold_lo;
			} else {
				threshold = ctx->depth_threshold_hi;
			}

			/* Filter out any "shallow" instructions which would otherwise
			 * tend to get scheduled too early to fill delay slots even
			 * when they are not needed for a while.  There will probably
			 * be later delay slots that they could just as easily fill.
			 *
			 * A classic case where this comes up is frag shaders that
			 * write a constant value (like 1.0f) to one of the channels
			 * of the output color(s).  Since the mov from immed has no
			 * dependencies, it would otherwise get scheduled early to
			 * fill delay slots, occupying a register until the end of
			 * the program.
			 */
			if ((deepest - candidate->depth) > threshold)
				continue;
		}

		int rank = ir3_delay_calc(ctx->block, candidate, soft, false);

		/* if too many live values, prioritize instructions that reduce the
		 * number of live values:
		 */
		if (live_values > ctx->live_threshold_hi) {
			rank = le;
		} else if (live_values > ctx->live_threshold_lo) {
			rank += le;
		}

		if (rank < best_rank) {
			best_instr = candidate;
			best_rank = rank;
		}
	}

	return best_instr;
}

static struct ir3_instruction *
split_instr(struct ir3_sched_ctx *ctx, struct ir3_instruction *orig_instr)
{
	struct ir3_instruction *new_instr = ir3_instr_clone(orig_instr);
	ir3_insert_by_depth(new_instr, &ctx->depth_list);
	transfer_use(ctx, orig_instr, new_instr);
	return new_instr;
}

/* "spill" the address register by remapping any unscheduled
 * instructions which depend on the current address register
 * to a clone of the instruction which wrote the address reg.
 */
static struct ir3_instruction *
split_addr(struct ir3_sched_ctx *ctx)
{
	struct ir3 *ir;
	struct ir3_instruction *new_addr = NULL;
	unsigned i;

	debug_assert(ctx->addr);

	ir = ctx->addr->block->shader;

	for (i = 0; i < ir->indirects_count; i++) {
		struct ir3_instruction *indirect = ir->indirects[i];

		if (!indirect)
			continue;

		/* skip instructions already scheduled: */
		if (is_scheduled(indirect))
			continue;

		/* remap remaining instructions using current addr
		 * to new addr:
		 */
		if (indirect->address == ctx->addr) {
			if (!new_addr) {
				new_addr = split_instr(ctx, ctx->addr);
				/* original addr is scheduled, but new one isn't: */
				new_addr->flags &= ~IR3_INSTR_MARK;
			}
			indirect->address = NULL;
			ir3_instr_set_address(indirect, new_addr);
		}
	}

	/* all remaining indirects remapped to new addr: */
	ctx->addr = NULL;

	return new_addr;
}

/* "spill" the predicate register by remapping any unscheduled
 * instructions which depend on the current predicate register
 * to a clone of the instruction which wrote the address reg.
 */
static struct ir3_instruction *
split_pred(struct ir3_sched_ctx *ctx)
{
	struct ir3 *ir;
	struct ir3_instruction *new_pred = NULL;
	unsigned i;

	debug_assert(ctx->pred);

	ir = ctx->pred->block->shader;

	for (i = 0; i < ir->predicates_count; i++) {
		struct ir3_instruction *predicated = ir->predicates[i];

		/* skip instructions already scheduled: */
		if (is_scheduled(predicated))
			continue;

		/* remap remaining instructions using current pred
		 * to new pred:
		 *
		 * TODO is there ever a case when pred isn't first
		 * (and only) src?
		 */
		if (ssa(predicated->regs[1]) == ctx->pred) {
			if (!new_pred) {
				new_pred = split_instr(ctx, ctx->pred);
				/* original pred is scheduled, but new one isn't: */
				new_pred->flags &= ~IR3_INSTR_MARK;
			}
			predicated->regs[1]->instr = new_pred;
		}
	}

	/* all remaining predicated remapped to new pred: */
	ctx->pred = NULL;

	return new_pred;
}

static void
sched_block(struct ir3_sched_ctx *ctx, struct ir3_block *block)
{
	struct list_head unscheduled_list;

	ctx->block = block;

	/* addr/pred writes are per-block: */
	ctx->addr = NULL;
	ctx->pred = NULL;

	/* move all instructions to the unscheduled list, and
	 * empty the block's instruction list (to which we will
	 * be inserting).
	 */
	list_replace(&block->instr_list, &unscheduled_list);
	list_inithead(&block->instr_list);
	list_inithead(&ctx->depth_list);

	/* First schedule all meta:input instructions, followed by
	 * tex-prefetch.  We want all of the instructions that load
	 * values into registers before the shader starts to go
	 * before any other instructions.  But in particular we
	 * want inputs to come before prefetches.  This is because
	 * a FS's bary_ij input may not actually be live in the
	 * shader, but it should not be scheduled on top of any
	 * other input (but can be overwritten by a tex prefetch)
	 *
	 * Finally, move all the remaining instructions to the depth-
	 * list
	 */
	foreach_instr_safe (instr, &unscheduled_list)
		if (instr->opc == OPC_META_INPUT)
			schedule(ctx, instr);

	foreach_instr_safe (instr, &unscheduled_list)
		if (instr->opc == OPC_META_TEX_PREFETCH)
			schedule(ctx, instr);

	foreach_instr_safe (instr, &unscheduled_list)
		ir3_insert_by_depth(instr, &ctx->depth_list);

	while (!list_is_empty(&ctx->depth_list)) {
		struct ir3_sched_notes notes = {0};
		struct ir3_instruction *instr;

		instr = find_eligible_instr(ctx, &notes, true);
		if (!instr)
			instr = find_eligible_instr(ctx, &notes, false);

		if (instr) {
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
		} else {
			struct ir3_instruction *new_instr = NULL;

			/* nothing available to schedule.. if we are blocked on
			 * address/predicate register conflict, then break the
			 * deadlock by cloning the instruction that wrote that
			 * reg:
			 */
			if (notes.addr_conflict) {
				new_instr = split_addr(ctx);
			} else if (notes.pred_conflict) {
				new_instr = split_pred(ctx);
			} else {
				debug_assert(0);
				ctx->error = true;
				return;
			}

			if (new_instr) {
				/* clearing current addr/pred can change what is
				 * available to schedule, so clear cache..
				 */
				clear_cache(ctx, NULL);

				ir3_insert_by_depth(new_instr, &ctx->depth_list);
				/* the original instr that wrote addr/pred may have
				 * originated from a different block:
				 */
				new_instr->block = block;
			}
		}
	}
}

static void
setup_thresholds(struct ir3_sched_ctx *ctx, struct ir3 *ir)
{
	if (ir3_has_latency_to_hide(ir)) {
		ctx->live_threshold_hi = 2 * 16 * 4;
		ctx->live_threshold_lo = 2 * 4 * 4;
		ctx->depth_threshold_hi = 6;
		ctx->depth_threshold_lo = 4;
	} else {
		ctx->live_threshold_hi = 2 * 16 * 4;
		ctx->live_threshold_lo = 2 * 12 * 4;
		ctx->depth_threshold_hi = 16;
		ctx->depth_threshold_lo = 16;
	}
}

int ir3_sched(struct ir3 *ir)
{
	struct ir3_sched_ctx ctx = {0};

	setup_thresholds(&ctx, ir);

	ir3_clear_mark(ir);
	update_use_count(ir);

	foreach_block (block, &ir->block_list) {
		ctx.live_values = 0;
		ctx.half_live_values = 0;
		sched_block(&ctx, block);
	}

	if (ctx.error)
		return -1;

	return 0;
}

static unsigned
get_array_id(struct ir3_instruction *instr)
{
	/* The expectation is that there is only a single array
	 * src or dst, ir3_cp should enforce this.
	 */

	for (unsigned i = 0; i < instr->regs_count; i++)
		if (instr->regs[i]->flags & IR3_REG_ARRAY)
			return instr->regs[i]->array.id;

	unreachable("this was unexpected");
}

/* does instruction 'prior' need to be scheduled before 'instr'? */
static bool
depends_on(struct ir3_instruction *instr, struct ir3_instruction *prior)
{
	/* TODO for dependencies that are related to a specific object, ie
	 * a specific SSBO/image/array, we could relax this constraint to
	 * make accesses to unrelated objects not depend on each other (at
	 * least as long as not declared coherent)
	 */
	if (((instr->barrier_class & IR3_BARRIER_EVERYTHING) && prior->barrier_class) ||
			((prior->barrier_class & IR3_BARRIER_EVERYTHING) && instr->barrier_class))
		return true;

	if (instr->barrier_class & prior->barrier_conflict) {
		if (!(instr->barrier_class & ~(IR3_BARRIER_ARRAY_R | IR3_BARRIER_ARRAY_W))) {
			/* if only array barrier, then we can further limit false-deps
			 * by considering the array-id, ie reads/writes to different
			 * arrays do not depend on each other (no aliasing)
			 */
			if (get_array_id(instr) != get_array_id(prior)) {
				return false;
			}
		}

		return true;
	}

	return false;
}

static void
add_barrier_deps(struct ir3_block *block, struct ir3_instruction *instr)
{
	struct list_head *prev = instr->node.prev;
	struct list_head *next = instr->node.next;

	/* add dependencies on previous instructions that must be scheduled
	 * prior to the current instruction
	 */
	while (prev != &block->instr_list) {
		struct ir3_instruction *pi =
			LIST_ENTRY(struct ir3_instruction, prev, node);

		prev = prev->prev;

		if (is_meta(pi))
			continue;

		if (instr->barrier_class == pi->barrier_class) {
			ir3_instr_add_dep(instr, pi);
			break;
		}

		if (depends_on(instr, pi))
			ir3_instr_add_dep(instr, pi);
	}

	/* add dependencies on this instruction to following instructions
	 * that must be scheduled after the current instruction:
	 */
	while (next != &block->instr_list) {
		struct ir3_instruction *ni =
			LIST_ENTRY(struct ir3_instruction, next, node);

		next = next->next;

		if (is_meta(ni))
			continue;

		if (instr->barrier_class == ni->barrier_class) {
			ir3_instr_add_dep(ni, instr);
			break;
		}

		if (depends_on(ni, instr))
			ir3_instr_add_dep(ni, instr);
	}
}

/* before scheduling a block, we need to add any necessary false-dependencies
 * to ensure that:
 *
 *  (1) barriers are scheduled in the right order wrt instructions related
 *      to the barrier
 *
 *  (2) reads that come before a write actually get scheduled before the
 *      write
 */
static void
calculate_deps(struct ir3_block *block)
{
	foreach_instr (instr, &block->instr_list) {
		if (instr->barrier_class) {
			add_barrier_deps(block, instr);
		}
	}
}

void
ir3_sched_add_deps(struct ir3 *ir)
{
	foreach_block (block, &ir->block_list) {
		calculate_deps(block);
	}
}
