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

#ifndef IR3_RA_H_
#define IR3_RA_H_

#include <setjmp.h>

#include "util/bitset.h"


static const unsigned class_sizes[] = {
	1, 2, 3, 4,
	4 + 4, /* txd + 1d/2d */
	4 + 6, /* txd + 3d */
};
#define class_count ARRAY_SIZE(class_sizes)

static const unsigned half_class_sizes[] = {
	1, 2, 3, 4,
};
#define half_class_count  ARRAY_SIZE(half_class_sizes)

/* seems to just be used for compute shaders?  Seems like vec1 and vec3
 * are sufficient (for now?)
 */
static const unsigned high_class_sizes[] = {
	1, 3,
};
#define high_class_count ARRAY_SIZE(high_class_sizes)

#define total_class_count (class_count + half_class_count + high_class_count)

/* Below a0.x are normal regs.  RA doesn't need to assign a0.x/p0.x. */
#define NUM_REGS             (4 * 48)  /* r0 to r47 */
#define NUM_HIGH_REGS        (4 * 8)   /* r48 to r55 */
#define FIRST_HIGH_REG       (4 * 48)
/* Number of virtual regs in a given class: */

static inline unsigned CLASS_REGS(unsigned i)
{
	assert(i < class_count);

	return (NUM_REGS - (class_sizes[i] - 1));
}

static inline unsigned HALF_CLASS_REGS(unsigned i)
{
	assert(i < half_class_count);

	return (NUM_REGS - (half_class_sizes[i] - 1));
}

static inline unsigned HIGH_CLASS_REGS(unsigned i)
{
	assert(i < high_class_count);

	return (NUM_HIGH_REGS - (high_class_sizes[i] - 1));
}

#define HALF_OFFSET          (class_count)
#define HIGH_OFFSET          (class_count + half_class_count)

/* register-set, created one time, used for all shaders: */
struct ir3_ra_reg_set {
	struct ra_regs *regs;
	unsigned int classes[class_count];
	unsigned int half_classes[half_class_count];
	unsigned int high_classes[high_class_count];

	/* pre-fetched tex dst is limited, on current gens to regs
	 * 0x3f and below.  An additional register class, with one
	 * vreg, that is setup to conflict with any regs above that
	 * limit.
	 */
	unsigned prefetch_exclude_class;
	unsigned prefetch_exclude_reg;

	/* The virtual register space flattens out all the classes,
	 * starting with full, followed by half and then high, ie:
	 *
	 *   scalar full  (starting at zero)
	 *   vec2 full
	 *   vec3 full
	 *   ...
	 *   vecN full
	 *   scalar half  (starting at first_half_reg)
	 *   vec2 half
	 *   ...
	 *   vecN half
	 *   scalar high  (starting at first_high_reg)
	 *   ...
	 *   vecN high
	 *
	 */
	unsigned first_half_reg, first_high_reg;

	/* maps flat virtual register space to base gpr: */
	uint16_t *ra_reg_to_gpr;
	/* maps cls,gpr to flat virtual register space: */
	uint16_t **gpr_to_ra_reg;
};

/* additional block-data (per-block) */
struct ir3_ra_block_data {
	BITSET_WORD *def;        /* variables defined before used in block */
	BITSET_WORD *use;        /* variables used before defined in block */
	BITSET_WORD *livein;     /* which defs reach entry point of block */
	BITSET_WORD *liveout;    /* which defs reach exit point of block */
};

/* additional instruction-data (per-instruction) */
struct ir3_ra_instr_data {
	/* cached instruction 'definer' info: */
	struct ir3_instruction *defn;
	int off, sz, cls;
};

/* register-assign context, per-shader */
struct ir3_ra_ctx {
	struct ir3_shader_variant *v;
	struct ir3 *ir;

	struct ir3_ra_reg_set *set;
	struct ra_graph *g;

	/* Are we in the scalar assignment pass?  In this pass, all larger-
	 * than-vec1 vales have already been assigned and pre-colored, so
	 * we only consider scalar values.
	 */
	bool scalar_pass;

	unsigned alloc_count;
	unsigned r0_xyz_nodes; /* ra node numbers for r0.[xyz] precolors */
	unsigned hr0_xyz_nodes; /* ra node numbers for hr0.[xyz] precolors */
	unsigned prefetch_exclude_node;
	/* one per class, plus one slot for arrays: */
	unsigned class_alloc_count[total_class_count + 1];
	unsigned class_base[total_class_count + 1];
	unsigned instr_cnt;
	unsigned *def, *use;     /* def/use table */
	struct ir3_ra_instr_data *instrd;

	/* Mapping vreg name back to instruction, used select reg callback: */
	struct hash_table *name_to_instr;

	/* Tracking for select_reg callback */
	unsigned start_search_reg;
	unsigned max_target;

	/* Temporary buffer for def/use iterators
	 *
	 * The worst case should probably be an array w/ relative access (ie.
	 * all elements are def'd or use'd), and that can't be larger than
	 * the number of registers.
	 *
	 * NOTE we could declare this on the stack if needed, but I don't
	 * think there is a need for nested iterators.
	 */
	unsigned namebuf[NUM_REGS];
	unsigned namecnt, nameidx;

	/* Error handling: */
	jmp_buf jmp_env;
};

#define ra_assert(ctx, expr) do { \
		if (!(expr)) { \
			_debug_printf("RA: %s:%u: %s: Assertion `%s' failed.\n", __FILE__, __LINE__, __func__, #expr); \
			longjmp((ctx)->jmp_env, -1); \
		} \
	} while (0)
#define ra_unreachable(ctx, str) ra_assert(ctx, !str)

static inline int
ra_name(struct ir3_ra_ctx *ctx, struct ir3_ra_instr_data *id)
{
	unsigned name;
	debug_assert(id->cls >= 0);
	debug_assert(id->cls < total_class_count);  /* we shouldn't get arrays here.. */
	name = ctx->class_base[id->cls] + id->defn->name;
	debug_assert(name < ctx->alloc_count);
	return name;
}

/* Get the scalar name of the n'th component of an instruction dst: */
static inline int
scalar_name(struct ir3_ra_ctx *ctx, struct ir3_instruction *instr, unsigned n)
{
	if (ctx->scalar_pass) {
		if (instr->opc == OPC_META_SPLIT) {
			debug_assert(n == 0);     /* split results in a scalar */
			struct ir3_instruction *src = instr->regs[1]->instr;
			return scalar_name(ctx, src, instr->split.off);
		} else if (instr->opc == OPC_META_COLLECT) {
			debug_assert(n < (instr->regs_count + 1));
			struct ir3_instruction *src = instr->regs[n + 1]->instr;
			return scalar_name(ctx, src, 0);
		}
	} else {
		debug_assert(n == 0);
	}

	return ra_name(ctx, &ctx->instrd[instr->ip]) + n;
}

#define NO_NAME ~0

/*
 * Iterators to iterate the vreg names of an instructions def's and use's
 */

static inline unsigned
__ra_name_cnt(struct ir3_ra_ctx *ctx, struct ir3_instruction *instr)
{
	if (!instr)
		return 0;

	/* Filter special cases, ie. writes to a0.x or p0.x, or non-ssa: */
	if (!writes_gpr(instr) || (instr->regs[0]->flags & IR3_REG_ARRAY))
		return 0;

	/* in scalar pass, we aren't considering virtual register classes, ie.
	 * if an instruction writes a vec2, then it defines two different scalar
	 * register names.
	 */
	if (ctx->scalar_pass)
		return dest_regs(instr);

	return 1;
}

#define foreach_name_n(__name, __n, __ctx, __instr) \
	for (unsigned __cnt = __ra_name_cnt(__ctx, __instr), __n = 0, __name; \
	     (__n < __cnt) && ({__name = scalar_name(__ctx, __instr, __n); 1;}); __n++)

#define foreach_name(__name, __ctx, __instr) \
	foreach_name_n(__name, __n, __ctx, __instr)

static inline unsigned
__ra_itr_pop(struct ir3_ra_ctx *ctx)
{
	if (ctx->nameidx < ctx->namecnt)
		return ctx->namebuf[ctx->nameidx++];
	return NO_NAME;
}

static inline void
__ra_itr_push(struct ir3_ra_ctx *ctx, unsigned name)
{
	assert(ctx->namecnt < ARRAY_SIZE(ctx->namebuf));
	ctx->namebuf[ctx->namecnt++] = name;
}

static inline unsigned
__ra_init_def_itr(struct ir3_ra_ctx *ctx, struct ir3_instruction *instr)
{
	/* nested use is not supported: */
	assert(ctx->namecnt == ctx->nameidx);

	ctx->namecnt = ctx->nameidx = 0;

	if (!writes_gpr(instr))
		return NO_NAME;

	struct ir3_ra_instr_data *id = &ctx->instrd[instr->ip];
	struct ir3_register *dst = instr->regs[0];

	if (dst->flags & IR3_REG_ARRAY) {
		struct ir3_array *arr = ir3_lookup_array(ctx->ir, dst->array.id);

		/* indirect write is treated like a write to all array
		 * elements, since we don't know which one is actually
		 * written:
		 */
		if (dst->flags & IR3_REG_RELATIV) {
			for (unsigned i = 0; i < arr->length; i++) {
				__ra_itr_push(ctx, arr->base + i);
			}
		} else {
			__ra_itr_push(ctx, arr->base + dst->array.offset);
			debug_assert(dst->array.offset < arr->length);
		}
	} else if (id->defn == instr) {
		foreach_name_n (name, i, ctx, instr) {
			/* tex instructions actually have a wrmask, and
			 * don't touch masked out components.  We can't do
			 * anything useful about that in the first pass,
			 * but in the scalar pass we can realize these
			 * registers are available:
			 */
			if (ctx->scalar_pass && is_tex_or_prefetch(instr) &&
					!(instr->regs[0]->wrmask & (1 << i)))
				continue;
			__ra_itr_push(ctx, name);
		}
	}

	return __ra_itr_pop(ctx);
}

static inline unsigned
__ra_init_use_itr(struct ir3_ra_ctx *ctx, struct ir3_instruction *instr)
{
	/* nested use is not supported: */
	assert(ctx->namecnt == ctx->nameidx);

	ctx->namecnt = ctx->nameidx = 0;

	foreach_src (reg, instr) {
		if (reg->flags & IR3_REG_ARRAY) {
			struct ir3_array *arr =
				ir3_lookup_array(ctx->ir, reg->array.id);

			/* indirect read is treated like a read from all array
			 * elements, since we don't know which one is actually
			 * read:
			 */
			if (reg->flags & IR3_REG_RELATIV) {
				for (unsigned i = 0; i < arr->length; i++) {
					__ra_itr_push(ctx, arr->base + i);
				}
			} else {
				__ra_itr_push(ctx, arr->base + reg->array.offset);
				debug_assert(reg->array.offset < arr->length);
			}
		} else {
			foreach_name_n (name, i, ctx, reg->instr) {
				/* split takes a src w/ wrmask potentially greater
				 * than 0x1, but it really only cares about a single
				 * component.  This shows up in splits coming out of
				 * a tex instruction w/ wrmask=.z, for example.
				 */
				if (ctx->scalar_pass && (instr->opc == OPC_META_SPLIT) &&
						!(i == instr->split.off))
					continue;
				__ra_itr_push(ctx, name);
			}
		}
	}

	return __ra_itr_pop(ctx);
}

#define foreach_def(__name, __ctx, __instr) \
	for (unsigned __name = __ra_init_def_itr(__ctx, __instr); \
	     __name != NO_NAME; __name = __ra_itr_pop(__ctx))

#define foreach_use(__name, __ctx, __instr) \
	for (unsigned __name = __ra_init_use_itr(__ctx, __instr); \
	     __name != NO_NAME; __name = __ra_itr_pop(__ctx))

int ra_size_to_class(unsigned sz, bool half, bool high);
int ra_class_to_size(unsigned class, bool *half, bool *high);

#endif  /* IR3_RA_H_ */
