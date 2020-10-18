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
#include "util/register_allocate.h"
#include "util/ralloc.h"
#include "util/bitset.h"

#include "ir3.h"
#include "ir3_shader.h"
#include "ir3_ra.h"


#ifdef DEBUG
#define RA_DEBUG (ir3_shader_debug & IR3_DBG_RAMSGS)
#else
#define RA_DEBUG 0
#endif
#define d(fmt, ...) do { if (RA_DEBUG) { \
	printf("RA: "fmt"\n", ##__VA_ARGS__); \
} } while (0)

#define di(instr, fmt, ...) do { if (RA_DEBUG) { \
	printf("RA: "fmt": ", ##__VA_ARGS__); \
	ir3_print_instr(instr); \
} } while (0)

/*
 * Register Assignment:
 *
 * Uses the register_allocate util, which implements graph coloring
 * algo with interference classes.  To handle the cases where we need
 * consecutive registers (for example, texture sample instructions),
 * we model these as larger (double/quad/etc) registers which conflict
 * with the corresponding registers in other classes.
 *
 * Additionally we create additional classes for half-regs, which
 * do not conflict with the full-reg classes.  We do need at least
 * sizes 1-4 (to deal w/ texture sample instructions output to half-
 * reg).  At the moment we don't create the higher order half-reg
 * classes as half-reg frequently does not have enough precision
 * for texture coords at higher resolutions.
 *
 * There are some additional cases that we need to handle specially,
 * as the graph coloring algo doesn't understand "partial writes".
 * For example, a sequence like:
 *
 *   add r0.z, ...
 *   sam (f32)(xy)r0.x, ...
 *   ...
 *   sam (f32)(xyzw)r0.w, r0.x, ...  ; 3d texture, so r0.xyz are coord
 *
 * In this scenario, we treat r0.xyz as class size 3, which is written
 * (from a use/def perspective) at the 'add' instruction and ignore the
 * subsequent partial writes to r0.xy.  So the 'add r0.z, ...' is the
 * defining instruction, as it is the first to partially write r0.xyz.
 *
 * To address the fragmentation that this can potentially cause, a
 * two pass register allocation is used.  After the first pass the
 * assignment of scalars is discarded, but the assignment of vecN (for
 * N > 1) is used to pre-color in the second pass, which considers
 * only scalars.
 *
 * Arrays of arbitrary size are handled via pre-coloring a consecutive
 * sequence of registers.  Additional scalar (single component) reg
 * names are allocated starting at ctx->class_base[total_class_count]
 * (see arr->base), which are pre-colored.  In the use/def graph direct
 * access is treated as a single element use/def, and indirect access
 * is treated as use or def of all array elements.  (Only the first
 * def is tracked, in case of multiple indirect writes, etc.)
 *
 * TODO arrays that fit in one of the pre-defined class sizes should
 * not need to be pre-colored, but instead could be given a normal
 * vreg name.  (Ignoring this for now since it is a good way to work
 * out the kinks with arbitrary sized arrays.)
 *
 * TODO might be easier for debugging to split this into two passes,
 * the first assigning vreg names in a way that we could ir3_print()
 * the result.
 */


static struct ir3_instruction * name_to_instr(struct ir3_ra_ctx *ctx, unsigned name);

static bool name_is_array(struct ir3_ra_ctx *ctx, unsigned name);
static struct ir3_array * name_to_array(struct ir3_ra_ctx *ctx, unsigned name);

/* does it conflict? */
static inline bool
intersects(unsigned a_start, unsigned a_end, unsigned b_start, unsigned b_end)
{
	return !((a_start >= b_end) || (b_start >= a_end));
}

static bool
instr_before(struct ir3_instruction *a, struct ir3_instruction *b)
{
	if (a->flags & IR3_INSTR_UNUSED)
		return false;
	return (a->ip < b->ip);
}

static struct ir3_instruction *
get_definer(struct ir3_ra_ctx *ctx, struct ir3_instruction *instr,
		int *sz, int *off)
{
	struct ir3_ra_instr_data *id = &ctx->instrd[instr->ip];
	struct ir3_instruction *d = NULL;

	if (ctx->scalar_pass) {
		id->defn = instr;
		id->off = 0;
		id->sz = 1;     /* considering things as N scalar regs now */
	}

	if (id->defn) {
		*sz = id->sz;
		*off = id->off;
		return id->defn;
	}

	if (instr->opc == OPC_META_COLLECT) {
		/* What about the case where collect is subset of array, we
		 * need to find the distance between where actual array starts
		 * and collect..  that probably doesn't happen currently.
		 */
		int dsz, doff;

		/* note: don't use foreach_ssa_src as this gets called once
		 * while assigning regs (which clears SSA flag)
		 */
		foreach_src_n (src, n, instr) {
			struct ir3_instruction *dd;
			if (!src->instr)
				continue;

			dd = get_definer(ctx, src->instr, &dsz, &doff);

			if ((!d) || instr_before(dd, d)) {
				d = dd;
				*sz = dsz;
				*off = doff - n;
			}
		}

	} else if (instr->cp.right || instr->cp.left) {
		/* covers also the meta:fo case, which ends up w/ single
		 * scalar instructions for each component:
		 */
		struct ir3_instruction *f = ir3_neighbor_first(instr);

		/* by definition, the entire sequence forms one linked list
		 * of single scalar register nodes (even if some of them may
		 * be splits from a texture sample (for example) instr.  We
		 * just need to walk the list finding the first element of
		 * the group defined (lowest ip)
		 */
		int cnt = 0;

		/* need to skip over unused in the group: */
		while (f && (f->flags & IR3_INSTR_UNUSED)) {
			f = f->cp.right;
			cnt++;
		}

		while (f) {
			if ((!d) || instr_before(f, d))
				d = f;
			if (f == instr)
				*off = cnt;
			f = f->cp.right;
			cnt++;
		}

		*sz = cnt;

	} else {
		/* second case is looking directly at the instruction which
		 * produces multiple values (eg, texture sample), rather
		 * than the split nodes that point back to that instruction.
		 * This isn't quite right, because it may be part of a larger
		 * group, such as:
		 *
		 *     sam (f32)(xyzw)r0.x, ...
		 *     add r1.x, ...
		 *     add r1.y, ...
		 *     sam (f32)(xyzw)r2.x, r0.w  <-- (r0.w, r1.x, r1.y)
		 *
		 * need to come up with a better way to handle that case.
		 */
		if (instr->address) {
			*sz = instr->regs[0]->size;
		} else {
			*sz = util_last_bit(instr->regs[0]->wrmask);
		}
		*off = 0;
		d = instr;
	}

	if (d->opc == OPC_META_SPLIT) {
		struct ir3_instruction *dd;
		int dsz, doff;

		dd = get_definer(ctx, d->regs[1]->instr, &dsz, &doff);

		/* by definition, should come before: */
		ra_assert(ctx, instr_before(dd, d));

		*sz = MAX2(*sz, dsz);

		if (instr->opc == OPC_META_SPLIT)
			*off = MAX2(*off, instr->split.off);

		d = dd;
	}

	ra_assert(ctx, d->opc != OPC_META_SPLIT);

	id->defn = d;
	id->sz = *sz;
	id->off = *off;

	return d;
}

static void
ra_block_find_definers(struct ir3_ra_ctx *ctx, struct ir3_block *block)
{
	foreach_instr (instr, &block->instr_list) {
		struct ir3_ra_instr_data *id = &ctx->instrd[instr->ip];
		if (instr->regs_count == 0)
			continue;
		/* couple special cases: */
		if (writes_addr0(instr) || writes_addr1(instr) || writes_pred(instr)) {
			id->cls = -1;
		} else if (instr->regs[0]->flags & IR3_REG_ARRAY) {
			id->cls = total_class_count;
		} else {
			/* and the normal case: */
			id->defn = get_definer(ctx, instr, &id->sz, &id->off);
			id->cls = ra_size_to_class(id->sz, is_half(id->defn), is_high(id->defn));

			/* this is a bit of duct-tape.. if we have a scenario like:
			 *
			 *   sam (f32)(x) out.x, ...
			 *   sam (f32)(x) out.y, ...
			 *
			 * Then the fanout/split meta instructions for the two different
			 * tex instructions end up grouped as left/right neighbors.  The
			 * upshot is that in when you get_definer() on one of the meta:fo's
			 * you get definer as the first sam with sz=2, but when you call
			 * get_definer() on the either of the sam's you get itself as the
			 * definer with sz=1.
			 *
			 * (We actually avoid this scenario exactly, the neighbor links
			 * prevent one of the output mov's from being eliminated, so this
			 * hack should be enough.  But probably we need to rethink how we
			 * find the "defining" instruction.)
			 *
			 * TODO how do we figure out offset properly...
			 */
			if (id->defn != instr) {
				struct ir3_ra_instr_data *did = &ctx->instrd[id->defn->ip];
				if (did->sz < id->sz) {
					did->sz = id->sz;
					did->cls = id->cls;
				}
			}
		}
	}
}

/* give each instruction a name (and ip), and count up the # of names
 * of each class
 */
static void
ra_block_name_instructions(struct ir3_ra_ctx *ctx, struct ir3_block *block)
{
	foreach_instr (instr, &block->instr_list) {
		struct ir3_ra_instr_data *id = &ctx->instrd[instr->ip];

#ifdef DEBUG
		instr->name = ~0;
#endif

		ctx->instr_cnt++;

		if (!writes_gpr(instr))
			continue;

		if (id->defn != instr)
			continue;

		/* In scalar pass, collect/split don't get their own names,
		 * but instead inherit them from their src(s):
		 *
		 * Possibly we don't need this because of scalar_name(), but
		 * it does make the ir3_print() dumps easier to read.
		 */
		if (ctx->scalar_pass) {
			if (instr->opc == OPC_META_SPLIT) {
				instr->name = instr->regs[1]->instr->name + instr->split.off;
				continue;
			}

			if (instr->opc == OPC_META_COLLECT) {
				instr->name = instr->regs[1]->instr->name;
				continue;
			}
		}

		/* arrays which don't fit in one of the pre-defined class
		 * sizes are pre-colored:
		 */
		if ((id->cls >= 0) && (id->cls < total_class_count)) {
			/* in the scalar pass, we generate a name for each
			 * scalar component, instr->name is the name of the
			 * first component.
			 */
			unsigned n = ctx->scalar_pass ? dest_regs(instr) : 1;
			instr->name = ctx->class_alloc_count[id->cls];
			ctx->class_alloc_count[id->cls] += n;
			ctx->alloc_count += n;
		}
	}
}

/**
 * Set a value for max register target.
 *
 * Currently this just rounds up to a multiple of full-vec4 (ie. the
 * granularity that we configure the hw for.. there is no point to
 * using r3.x if you aren't going to make r3.yzw available).  But
 * in reality there seems to be multiple thresholds that affect the
 * number of waves.. and we should round up the target to the next
 * threshold when we round-robin registers, to give postsched more
 * options.  When we understand that better, this is where we'd
 * implement that.
 */
static void
ra_set_register_target(struct ir3_ra_ctx *ctx, unsigned max_target)
{
	const unsigned hvec4 = 4;
	const unsigned vec4 = 2 * hvec4;

	ctx->max_target = align(max_target, vec4);

	d("New max_target=%u", ctx->max_target);
}

static int
pick_in_range(BITSET_WORD *regs, unsigned min, unsigned max)
{
	for (unsigned i = min; i <= max; i++) {
		if (BITSET_TEST(regs, i)) {
			return i;
		}
	}
	return -1;
}

static int
pick_in_range_rev(BITSET_WORD *regs, int min, int max)
{
	for (int i = max; i >= min; i--) {
		if (BITSET_TEST(regs, i)) {
			return i;
		}
	}
	return -1;
}

/* register selector for the a6xx+ merged register file: */
static unsigned int
ra_select_reg_merged(unsigned int n, BITSET_WORD *regs, void *data)
{
	struct ir3_ra_ctx *ctx = data;
	unsigned int class = ra_get_node_class(ctx->g, n);
	bool half, high;
	int sz = ra_class_to_size(class, &half, &high);

	assert (sz > 0);

	/* dimensions within the register class: */
	unsigned max_target, start;

	/* the regs bitset will include *all* of the virtual regs, but we lay
	 * out the different classes consecutively in the virtual register
	 * space.  So we just need to think about the base offset of a given
	 * class within the virtual register space, and offset the register
	 * space we search within by that base offset.
	 */
	unsigned base;

	/* TODO I think eventually we want to round-robin in vector pass
	 * as well, but needs some more work to calculate # of live vals
	 * for this.  (Maybe with some work, we could just figure out
	 * the scalar target and use that, since that is what we care
	 * about in the end.. but that would mean setting up use-def/
	 * liveranges for scalar pass before doing vector pass.)
	 *
	 * For now, in the vector class, just move assignments for scalar
	 * vals higher to hopefully prevent them from limiting where vecN
	 * values can be placed.  Since the scalar values are re-assigned
	 * in the 2nd pass, we don't really care where they end up in the
	 * vector pass.
	 */
	if (!ctx->scalar_pass) {
		base = ctx->set->gpr_to_ra_reg[class][0];
		if (high) {
			max_target = HIGH_CLASS_REGS(class - HIGH_OFFSET);
		} else if (half) {
			max_target = HALF_CLASS_REGS(class - HALF_OFFSET);
		} else {
			max_target = CLASS_REGS(class);
		}

		if ((sz == 1) && !high) {
			return pick_in_range_rev(regs, base, base + max_target);
		} else {
			return pick_in_range(regs, base, base + max_target);
		}
	} else {
		ra_assert(ctx, sz == 1);
	}

	/* NOTE: this is only used in scalar pass, so the register
	 * class will be one of the scalar classes (ie. idx==0):
	 */
	base = ctx->set->gpr_to_ra_reg[class][0];
	if (high) {
		max_target = HIGH_CLASS_REGS(0);
		start = 0;
	} else if (half) {
		max_target = ctx->max_target;
		start = ctx->start_search_reg;
	} else {
		max_target = ctx->max_target / 2;
		start = ctx->start_search_reg;
	}

	/* For cat4 instructions, if the src reg is already assigned, and
	 * avail to pick, use it.  Because this doesn't introduce unnecessary
	 * dependencies, and it potentially avoids needing (ss) syncs to
	 * for write after read hazards:
	 */
	struct ir3_instruction *instr = name_to_instr(ctx, n);
	if (is_sfu(instr)) {
		struct ir3_register *src = instr->regs[1];
		int src_n;

		if ((src->flags & IR3_REG_ARRAY) && !(src->flags & IR3_REG_RELATIV)) {
			struct ir3_array *arr = ir3_lookup_array(ctx->ir, src->array.id);
			src_n = arr->base + src->array.offset;
		} else {
			src_n = scalar_name(ctx, src->instr, 0);
		}

		unsigned reg = ra_get_node_reg(ctx->g, src_n);

		/* Check if the src register has been assigned yet: */
		if (reg != NO_REG) {
			if (BITSET_TEST(regs, reg)) {
				return reg;
			}
		}
	}

	int r = pick_in_range(regs, base + start, base + max_target);
	if (r < 0) {
		/* wrap-around: */
		r = pick_in_range(regs, base, base + start);
	}

	if (r < 0) {
		/* overflow, we need to increase max_target: */
		ra_set_register_target(ctx, ctx->max_target + 1);
		return ra_select_reg_merged(n, regs, data);
	}

	if (class == ctx->set->half_classes[0]) {
		int n = r - base;
		ctx->start_search_reg = (n + 1) % ctx->max_target;
	} else if (class == ctx->set->classes[0]) {
		int n = (r - base) * 2;
		ctx->start_search_reg = (n + 1) % ctx->max_target;
	}

	return r;
}

static void
ra_init(struct ir3_ra_ctx *ctx)
{
	unsigned n, base;

	ir3_clear_mark(ctx->ir);
	n = ir3_count_instructions_ra(ctx->ir);

	ctx->instrd = rzalloc_array(NULL, struct ir3_ra_instr_data, n);

	foreach_block (block, &ctx->ir->block_list) {
		ra_block_find_definers(ctx, block);
	}

	foreach_block (block, &ctx->ir->block_list) {
		ra_block_name_instructions(ctx, block);
	}

	/* figure out the base register name for each class.  The
	 * actual ra name is class_base[cls] + instr->name;
	 */
	ctx->class_base[0] = 0;
	for (unsigned i = 1; i <= total_class_count; i++) {
		ctx->class_base[i] = ctx->class_base[i-1] +
				ctx->class_alloc_count[i-1];
	}

	/* and vreg names for array elements: */
	base = ctx->class_base[total_class_count];
	foreach_array (arr, &ctx->ir->array_list) {
		arr->base = base;
		ctx->class_alloc_count[total_class_count] += arr->length;
		base += arr->length;
	}
	ctx->alloc_count += ctx->class_alloc_count[total_class_count];

	/* Add vreg names for r0.xyz */
	ctx->r0_xyz_nodes = ctx->alloc_count;
	ctx->alloc_count += 3;
	ctx->hr0_xyz_nodes = ctx->alloc_count;
	ctx->alloc_count += 3;

	/* Add vreg name for prefetch-exclusion range: */
	ctx->prefetch_exclude_node = ctx->alloc_count++;

	if (RA_DEBUG) {
		d("INSTRUCTION VREG NAMES:");
		foreach_block (block, &ctx->ir->block_list) {
			foreach_instr (instr, &block->instr_list) {
				if (!ctx->instrd[instr->ip].defn)
					continue;
				if (!writes_gpr(instr))
					continue;
				di(instr, "%04u", scalar_name(ctx, instr, 0));
			}
		}
		d("ARRAY VREG NAMES:");
		foreach_array (arr, &ctx->ir->array_list) {
			d("%04u: arr%u", arr->base, arr->id);
		}
		d("EXTRA VREG NAMES:");
		d("%04u: r0_xyz_nodes", ctx->r0_xyz_nodes);
		d("%04u: hr0_xyz_nodes", ctx->hr0_xyz_nodes);
		d("%04u: prefetch_exclude_node", ctx->prefetch_exclude_node);
	}

	ctx->g = ra_alloc_interference_graph(ctx->set->regs, ctx->alloc_count);
	ralloc_steal(ctx->g, ctx->instrd);
	ctx->def = rzalloc_array(ctx->g, unsigned, ctx->alloc_count);
	ctx->use = rzalloc_array(ctx->g, unsigned, ctx->alloc_count);

	/* TODO add selector callback for split (pre-a6xx) register file: */
	if (ctx->v->mergedregs) {
		ra_set_select_reg_callback(ctx->g, ra_select_reg_merged, ctx);

		if (ctx->scalar_pass) {
			ctx->name_to_instr = _mesa_hash_table_create(ctx->g,
					_mesa_hash_int, _mesa_key_int_equal);
		}
	}
}

/* Map the name back to instruction: */
static struct ir3_instruction *
name_to_instr(struct ir3_ra_ctx *ctx, unsigned name)
{
	ra_assert(ctx, !name_is_array(ctx, name));
	struct hash_entry *entry = _mesa_hash_table_search(ctx->name_to_instr, &name);
	if (entry)
		return entry->data;
	ra_unreachable(ctx, "invalid instr name");
	return NULL;
}

static bool
name_is_array(struct ir3_ra_ctx *ctx, unsigned name)
{
	return name >= ctx->class_base[total_class_count];
}

static struct ir3_array *
name_to_array(struct ir3_ra_ctx *ctx, unsigned name)
{
	ra_assert(ctx, name_is_array(ctx, name));
	foreach_array (arr, &ctx->ir->array_list) {
		if (name < (arr->base + arr->length))
			return arr;
	}
	ra_unreachable(ctx, "invalid array name");
	return NULL;
}

static void
ra_destroy(struct ir3_ra_ctx *ctx)
{
	ralloc_free(ctx->g);
}

static void
__def(struct ir3_ra_ctx *ctx, struct ir3_ra_block_data *bd, unsigned name,
		struct ir3_instruction *instr)
{
	ra_assert(ctx, name < ctx->alloc_count);

	/* split/collect do not actually define any real value */
	if ((instr->opc == OPC_META_SPLIT) || (instr->opc == OPC_META_COLLECT))
		return;

	/* defined on first write: */
	if (!ctx->def[name])
		ctx->def[name] = instr->ip;
	ctx->use[name] = MAX2(ctx->use[name], instr->ip);
	BITSET_SET(bd->def, name);
}

static void
__use(struct ir3_ra_ctx *ctx, struct ir3_ra_block_data *bd, unsigned name,
		struct ir3_instruction *instr)
{
	ra_assert(ctx, name < ctx->alloc_count);
	ctx->use[name] = MAX2(ctx->use[name], instr->ip);
	if (!BITSET_TEST(bd->def, name))
		BITSET_SET(bd->use, name);
}

static void
ra_block_compute_live_ranges(struct ir3_ra_ctx *ctx, struct ir3_block *block)
{
	struct ir3_ra_block_data *bd;
	unsigned bitset_words = BITSET_WORDS(ctx->alloc_count);

#define def(name, instr) __def(ctx, bd, name, instr)
#define use(name, instr) __use(ctx, bd, name, instr)

	bd = rzalloc(ctx->g, struct ir3_ra_block_data);

	bd->def     = rzalloc_array(bd, BITSET_WORD, bitset_words);
	bd->use     = rzalloc_array(bd, BITSET_WORD, bitset_words);
	bd->livein  = rzalloc_array(bd, BITSET_WORD, bitset_words);
	bd->liveout = rzalloc_array(bd, BITSET_WORD, bitset_words);

	block->data = bd;

	struct ir3_instruction *first_non_input = NULL;
	foreach_instr (instr, &block->instr_list) {
		if (instr->opc != OPC_META_INPUT) {
			first_non_input = instr;
			break;
		}
	}

	foreach_instr (instr, &block->instr_list) {
		foreach_def (name, ctx, instr) {
			if (name_is_array(ctx, name)) {
				struct ir3_array *arr = name_to_array(ctx, name);

				arr->start_ip = MIN2(arr->start_ip, instr->ip);
				arr->end_ip = MAX2(arr->end_ip, instr->ip);

				for (unsigned i = 0; i < arr->length; i++) {
					unsigned name = arr->base + i;
					if(arr->half)
						ra_set_node_class(ctx->g, name, ctx->set->half_classes[0]);
					else
						ra_set_node_class(ctx->g, name, ctx->set->classes[0]);
				}
			} else {
				struct ir3_ra_instr_data *id = &ctx->instrd[instr->ip];
				if (is_high(instr)) {
					ra_set_node_class(ctx->g, name,
							ctx->set->high_classes[id->cls - HIGH_OFFSET]);
				} else if (is_half(instr)) {
					ra_set_node_class(ctx->g, name,
							ctx->set->half_classes[id->cls - HALF_OFFSET]);
				} else {
					ra_set_node_class(ctx->g, name,
							ctx->set->classes[id->cls]);
				}
			}

			def(name, instr);

			if ((instr->opc == OPC_META_INPUT) && first_non_input)
				use(name, first_non_input);

			/* Texture instructions with writemasks can be treated as smaller
			 * vectors (or just scalars!) to allocate knowing that the
			 * masked-out regs won't be written, but we need to make sure that
			 * the start of the vector doesn't come before the first register
			 * or we'll wrap.
			 */
			if (is_tex_or_prefetch(instr)) {
				int writemask_skipped_regs = ffs(instr->regs[0]->wrmask) - 1;
				int r0_xyz = is_half(instr) ?
					ctx->hr0_xyz_nodes : ctx->r0_xyz_nodes;
				for (int i = 0; i < writemask_skipped_regs; i++)
					ra_add_node_interference(ctx->g, name, r0_xyz + i);
			}

			/* Pre-fetched textures have a lower limit for bits to encode dst
			 * register, so add additional interference with registers above
			 * that limit.
			 */
			if (instr->opc == OPC_META_TEX_PREFETCH) {
				ra_add_node_interference(ctx->g, name,
						ctx->prefetch_exclude_node);
			}
		}

		foreach_use (name, ctx, instr) {
			if (name_is_array(ctx, name)) {
				struct ir3_array *arr = name_to_array(ctx, name);

				arr->start_ip = MIN2(arr->start_ip, instr->ip);
				arr->end_ip = MAX2(arr->end_ip, instr->ip);

				/* NOTE: arrays are not SSA so unconditionally
				 * set use bit:
				 */
				BITSET_SET(bd->use, name);
			}

			use(name, instr);
		}

		foreach_name (name, ctx, instr) {
			/* split/collect instructions have duplicate names
			 * as real instructions, so they skip the hashtable:
			 */
			if (ctx->name_to_instr && !((instr->opc == OPC_META_SPLIT) ||
					(instr->opc == OPC_META_COLLECT))) {
				/* this is slightly annoying, we can't just use an
				 * integer on the stack
				 */
				unsigned *key = ralloc(ctx->name_to_instr, unsigned);
				*key = name;
				ra_assert(ctx, !_mesa_hash_table_search(ctx->name_to_instr, key));
				_mesa_hash_table_insert(ctx->name_to_instr, key, instr);
			}
		}
	}
}

static bool
ra_compute_livein_liveout(struct ir3_ra_ctx *ctx)
{
	unsigned bitset_words = BITSET_WORDS(ctx->alloc_count);
	bool progress = false;

	foreach_block (block, &ctx->ir->block_list) {
		struct ir3_ra_block_data *bd = block->data;

		/* update livein: */
		for (unsigned i = 0; i < bitset_words; i++) {
			/* anything used but not def'd within a block is
			 * by definition a live value coming into the block:
			 */
			BITSET_WORD new_livein =
				(bd->use[i] | (bd->liveout[i] & ~bd->def[i]));

			if (new_livein & ~bd->livein[i]) {
				bd->livein[i] |= new_livein;
				progress = true;
			}
		}

		/* update liveout: */
		for (unsigned j = 0; j < ARRAY_SIZE(block->successors); j++) {
			struct ir3_block *succ = block->successors[j];
			struct ir3_ra_block_data *succ_bd;

			if (!succ)
				continue;

			succ_bd = succ->data;

			for (unsigned i = 0; i < bitset_words; i++) {
				/* add anything that is livein in a successor block
				 * to our liveout:
				 */
				BITSET_WORD new_liveout =
					(succ_bd->livein[i] & ~bd->liveout[i]);

				if (new_liveout) {
					bd->liveout[i] |= new_liveout;
					progress = true;
				}
			}
		}
	}

	return progress;
}

static void
print_bitset(const char *name, BITSET_WORD *bs, unsigned cnt)
{
	bool first = true;
	debug_printf("RA:  %s:", name);
	for (unsigned i = 0; i < cnt; i++) {
		if (BITSET_TEST(bs, i)) {
			if (!first)
				debug_printf(",");
			debug_printf(" %04u", i);
			first = false;
		}
	}
	debug_printf("\n");
}

/* size of one component of instruction result, ie. half vs full: */
static unsigned
live_size(struct ir3_instruction *instr)
{
	if (is_half(instr)) {
		return 1;
	} else if (is_high(instr)) {
		/* doesn't count towards footprint */
		return 0;
	} else {
		return 2;
	}
}

static unsigned
name_size(struct ir3_ra_ctx *ctx, unsigned name)
{
	if (name_is_array(ctx, name)) {
		struct ir3_array *arr = name_to_array(ctx, name);
		return arr->half ? 1 : 2;
	} else {
		struct ir3_instruction *instr = name_to_instr(ctx, name);
		/* in scalar pass, each name represents on scalar value,
		 * half or full precision
		 */
		return live_size(instr);
	}
}

static unsigned
ra_calc_block_live_values(struct ir3_ra_ctx *ctx, struct ir3_block *block)
{
	struct ir3_ra_block_data *bd = block->data;
	unsigned name;

	ra_assert(ctx, ctx->name_to_instr);

	/* TODO this gets a bit more complicated in non-scalar pass.. but
	 * possibly a lowball estimate is fine to start with if we do
	 * round-robin in non-scalar pass?  Maybe we just want to handle
	 * that in a different fxn?
	 */
	ra_assert(ctx, ctx->scalar_pass);

	BITSET_WORD *live =
		rzalloc_array(bd, BITSET_WORD, BITSET_WORDS(ctx->alloc_count));

	/* Add the live input values: */
	unsigned livein = 0;
	BITSET_FOREACH_SET (name, bd->livein, ctx->alloc_count) {
		livein += name_size(ctx, name);
		BITSET_SET(live, name);
	}

	d("---------------------");
	d("block%u: LIVEIN: %u", block_id(block), livein);

	unsigned max = livein;
	int cur_live = max;

	/* Now that we know the live inputs to the block, iterate the
	 * instructions adjusting the current # of live values as we
	 * see their last use:
	 */
	foreach_instr (instr, &block->instr_list) {
		if (RA_DEBUG)
			print_bitset("LIVE", live, ctx->alloc_count);
		di(instr, "CALC");

		unsigned new_live = 0;    /* newly live values */
		unsigned new_dead = 0;    /* newly no-longer live values */
		unsigned next_dead = 0;   /* newly dead following this instr */

		foreach_def (name, ctx, instr) {
			/* NOTE: checking ctx->def filters out things like split/
			 * collect which are just redefining existing live names
			 * or array writes to already live array elements:
			 */
			if (ctx->def[name] != instr->ip)
				continue;
			new_live += live_size(instr);
			d("NEW_LIVE: %u (new_live=%u, use=%u)", name, new_live, ctx->use[name]);
			BITSET_SET(live, name);
			/* There can be cases where this is *also* the last use
			 * of a value, for example instructions that write multiple
			 * values, only some of which are used.  These values are
			 * dead *after* (rather than during) this instruction.
			 */
			if (ctx->use[name] != instr->ip)
				continue;
			next_dead += live_size(instr);
			d("NEXT_DEAD: %u (next_dead=%u)", name, next_dead);
			BITSET_CLEAR(live, name);
		}

		/* To be more resilient against special cases where liverange
		 * is extended (like first_non_input), rather than using the
		 * foreach_use() iterator, we iterate the current live values
		 * instead:
		 */
		BITSET_FOREACH_SET (name, live, ctx->alloc_count) {
			/* Is this the last use? */
			if (ctx->use[name] != instr->ip)
				continue;
			new_dead += name_size(ctx, name);
			d("NEW_DEAD: %u (new_dead=%u)", name, new_dead);
			BITSET_CLEAR(live, name);
		}

		cur_live += new_live;
		cur_live -= new_dead;

		ra_assert(ctx, cur_live >= 0);
		d("CUR_LIVE: %u", cur_live);

		max = MAX2(max, cur_live);

		/* account for written values which are not used later,
		 * but after updating max (since they are for one cycle
		 * live)
		 */
		cur_live -= next_dead;
		ra_assert(ctx, cur_live >= 0);

		if (RA_DEBUG) {
			unsigned cnt = 0;
			BITSET_FOREACH_SET (name, live, ctx->alloc_count) {
				cnt += name_size(ctx, name);
			}
			ra_assert(ctx, cur_live == cnt);
		}
	}

	d("block%u max=%u", block_id(block), max);

	/* the remaining live should match liveout (for extra sanity testing): */
	if (RA_DEBUG) {
		unsigned new_dead = 0;
		BITSET_FOREACH_SET (name, live, ctx->alloc_count) {
			/* Is this the last use? */
			if (ctx->use[name] != block->end_ip)
				continue;
			new_dead += name_size(ctx, name);
			d("NEW_DEAD: %u (new_dead=%u)", name, new_dead);
			BITSET_CLEAR(live, name);
		}
		unsigned liveout = 0;
		BITSET_FOREACH_SET (name, bd->liveout, ctx->alloc_count) {
			liveout += name_size(ctx, name);
			BITSET_CLEAR(live, name);
		}

		if (cur_live != liveout) {
			print_bitset("LEAKED", live, ctx->alloc_count);
			/* TODO there are a few edge cases where live-range extension
			 * tells us a value is livein.  But not used by the block or
			 * liveout for the block.  Possibly a bug in the liverange
			 * extension.  But for now leave the assert disabled:
			ra_assert(ctx, cur_live == liveout);
			 */
		}
	}

	ralloc_free(live);

	return max;
}

static unsigned
ra_calc_max_live_values(struct ir3_ra_ctx *ctx)
{
	unsigned max = 0;

	foreach_block (block, &ctx->ir->block_list) {
		unsigned block_live = ra_calc_block_live_values(ctx, block);
		max = MAX2(max, block_live);
	}

	return max;
}

static void
ra_add_interference(struct ir3_ra_ctx *ctx)
{
	struct ir3 *ir = ctx->ir;

	/* initialize array live ranges: */
	foreach_array (arr, &ir->array_list) {
		arr->start_ip = ~0;
		arr->end_ip = 0;
	}

	/* set up the r0.xyz precolor regs. */
	for (int i = 0; i < 3; i++) {
		ra_set_node_reg(ctx->g, ctx->r0_xyz_nodes + i, i);
		ra_set_node_reg(ctx->g, ctx->hr0_xyz_nodes + i,
				ctx->set->first_half_reg + i);
	}

	/* pre-color node that conflict with half/full regs higher than what
	 * can be encoded for tex-prefetch:
	 */
	ra_set_node_reg(ctx->g, ctx->prefetch_exclude_node,
			ctx->set->prefetch_exclude_reg);

	/* compute live ranges (use/def) on a block level, also updating
	 * block's def/use bitmasks (used below to calculate per-block
	 * livein/liveout):
	 */
	foreach_block (block, &ir->block_list) {
		ra_block_compute_live_ranges(ctx, block);
	}

	/* update per-block livein/liveout: */
	while (ra_compute_livein_liveout(ctx)) {}

	if (RA_DEBUG) {
		d("AFTER LIVEIN/OUT:");
		foreach_block (block, &ir->block_list) {
			struct ir3_ra_block_data *bd = block->data;
			d("block%u:", block_id(block));
			print_bitset("  def", bd->def, ctx->alloc_count);
			print_bitset("  use", bd->use, ctx->alloc_count);
			print_bitset("  l/i", bd->livein, ctx->alloc_count);
			print_bitset("  l/o", bd->liveout, ctx->alloc_count);
		}
		foreach_array (arr, &ir->array_list) {
			d("array%u:", arr->id);
			d("   length:   %u", arr->length);
			d("   start_ip: %u", arr->start_ip);
			d("   end_ip:   %u", arr->end_ip);
		}
	}

	/* extend start/end ranges based on livein/liveout info from cfg: */
	foreach_block (block, &ir->block_list) {
		struct ir3_ra_block_data *bd = block->data;

		for (unsigned i = 0; i < ctx->alloc_count; i++) {
			if (BITSET_TEST(bd->livein, i)) {
				ctx->def[i] = MIN2(ctx->def[i], block->start_ip);
				ctx->use[i] = MAX2(ctx->use[i], block->start_ip);
			}

			if (BITSET_TEST(bd->liveout, i)) {
				ctx->def[i] = MIN2(ctx->def[i], block->end_ip);
				ctx->use[i] = MAX2(ctx->use[i], block->end_ip);
			}
		}

		foreach_array (arr, &ctx->ir->array_list) {
			for (unsigned i = 0; i < arr->length; i++) {
				if (BITSET_TEST(bd->livein, i + arr->base)) {
					arr->start_ip = MIN2(arr->start_ip, block->start_ip);
				}
				if (BITSET_TEST(bd->liveout, i + arr->base)) {
					arr->end_ip = MAX2(arr->end_ip, block->end_ip);
				}
			}
		}
	}

	if (ctx->name_to_instr) {
		unsigned max = ra_calc_max_live_values(ctx);
		ra_set_register_target(ctx, max);
	}

	for (unsigned i = 0; i < ctx->alloc_count; i++) {
		for (unsigned j = 0; j < ctx->alloc_count; j++) {
			if (intersects(ctx->def[i], ctx->use[i],
					ctx->def[j], ctx->use[j])) {
				ra_add_node_interference(ctx->g, i, j);
			}
		}
	}
}

/* NOTE: instr could be NULL for IR3_REG_ARRAY case, for the first
 * array access(es) which do not have any previous access to depend
 * on from scheduling point of view
 */
static void
reg_assign(struct ir3_ra_ctx *ctx, struct ir3_register *reg,
		struct ir3_instruction *instr)
{
	struct ir3_ra_instr_data *id;

	if (reg->flags & IR3_REG_ARRAY) {
		struct ir3_array *arr =
			ir3_lookup_array(ctx->ir, reg->array.id);
		unsigned name = arr->base + reg->array.offset;
		unsigned r = ra_get_node_reg(ctx->g, name);
		unsigned num = ctx->set->ra_reg_to_gpr[r];

		if (reg->flags & IR3_REG_RELATIV) {
			reg->array.offset = num;
		} else {
			reg->num = num;
			reg->flags &= ~IR3_REG_SSA;
		}

		reg->flags &= ~IR3_REG_ARRAY;
	} else if ((id = &ctx->instrd[instr->ip]) && id->defn) {
		unsigned first_component = 0;

		/* Special case for tex instructions, which may use the wrmask
		 * to mask off the first component(s).  In the scalar pass,
		 * this means the masked off component(s) are not def'd/use'd,
		 * so we get a bogus value when we ask the register_allocate
		 * algo to get the assigned reg for the unused/untouched
		 * component.  So we need to consider the first used component:
		 */
		if (ctx->scalar_pass && is_tex_or_prefetch(id->defn)) {
			unsigned n = ffs(id->defn->regs[0]->wrmask);
			ra_assert(ctx, n > 0);
			first_component = n - 1;
		}

		unsigned name = scalar_name(ctx, id->defn, first_component);
		unsigned r = ra_get_node_reg(ctx->g, name);
		unsigned num = ctx->set->ra_reg_to_gpr[r] + id->off;

		ra_assert(ctx, !(reg->flags & IR3_REG_RELATIV));

		ra_assert(ctx, num >= first_component);

		if (is_high(id->defn))
			num += FIRST_HIGH_REG;

		reg->num = num - first_component;

		reg->flags &= ~IR3_REG_SSA;

		if (is_half(id->defn))
			reg->flags |= IR3_REG_HALF;
	}
}

/* helper to determine which regs to assign in which pass: */
static bool
should_assign(struct ir3_ra_ctx *ctx, struct ir3_instruction *instr)
{
	if ((instr->opc == OPC_META_SPLIT) &&
			(util_bitcount(instr->regs[1]->wrmask) > 1))
		return !ctx->scalar_pass;
	if ((instr->opc == OPC_META_COLLECT) &&
			(util_bitcount(instr->regs[0]->wrmask) > 1))
		return !ctx->scalar_pass;
	return ctx->scalar_pass;
}

static void
ra_block_alloc(struct ir3_ra_ctx *ctx, struct ir3_block *block)
{
	foreach_instr (instr, &block->instr_list) {

		if (writes_gpr(instr)) {
			if (should_assign(ctx, instr)) {
				reg_assign(ctx, instr->regs[0], instr);
			}
		}

		foreach_src_n (reg, n, instr) {
			struct ir3_instruction *src = reg->instr;

			if (src && !should_assign(ctx, src) && !should_assign(ctx, instr))
				continue;

			if (src && should_assign(ctx, instr))
				reg_assign(ctx, src->regs[0], src);

			/* Note: reg->instr could be null for IR3_REG_ARRAY */
			if (src || (reg->flags & IR3_REG_ARRAY))
				reg_assign(ctx, instr->regs[n+1], src);
		}
	}

	/* We need to pre-color outputs for the scalar pass in
	 * ra_precolor_assigned(), so we need to actually assign
	 * them in the first pass:
	 */
	if (!ctx->scalar_pass) {
		foreach_input (in, ctx->ir) {
			reg_assign(ctx, in->regs[0], in);
		}
		foreach_output (out, ctx->ir) {
			reg_assign(ctx, out->regs[0], out);
		}
	}
}

static void
assign_arr_base(struct ir3_ra_ctx *ctx, struct ir3_array *arr,
		struct ir3_instruction **precolor, unsigned nprecolor)
{
	/* In the mergedregs case, we convert full precision arrays
	 * to their effective half-precision base, and find conflicts
	 * amongst all other arrays/inputs.
	 *
	 * In the splitregs case (halfreg file and fullreg file do
	 * not conflict), we ignore arrays and other pre-colors that
	 * are not the same precision.
	 */
	bool mergedregs = ctx->v->mergedregs;
	unsigned base = 0;

	/* figure out what else we conflict with which has already
	 * been assigned:
	 */
retry:
	foreach_array (arr2, &ctx->ir->array_list) {
		if (arr2 == arr)
			break;
		ra_assert(ctx, arr2->start_ip <= arr2->end_ip);

		unsigned base2 = arr2->reg;
		unsigned len2  = arr2->length;
		unsigned len   = arr->length;

		if (mergedregs) {
			/* convert into half-reg space: */
			if (!arr2->half) {
				base2 *= 2;
				len2  *= 2;
			}
			if (!arr->half) {
				len   *= 2;
			}
		} else if (arr2->half != arr->half) {
			/* for split-register-file mode, we only conflict with
			 * other arrays of same precision:
			 */
			continue;
		}

		/* if it intersects with liverange AND register range.. */
		if (intersects(arr->start_ip, arr->end_ip,
				arr2->start_ip, arr2->end_ip) &&
			intersects(base, base + len,
				base2, base2 + len2)) {
			base = MAX2(base, base2 + len2);
			goto retry;
		}
	}

	/* also need to not conflict with any pre-assigned inputs: */
	for (unsigned i = 0; i < nprecolor; i++) {
		struct ir3_instruction *instr = precolor[i];

		if (!instr || (instr->flags & IR3_INSTR_UNUSED))
			continue;

		struct ir3_ra_instr_data *id = &ctx->instrd[instr->ip];

		/* only consider the first component: */
		if (id->off > 0)
			continue;

		unsigned name   = ra_name(ctx, id);
		unsigned regid  = instr->regs[0]->num;
		unsigned reglen = class_sizes[id->cls];
		unsigned len    = arr->length;

		if (mergedregs) {
			/* convert into half-reg space: */
			if (!is_half(instr)) {
				regid  *= 2;
				reglen *= 2;
			}
			if (!arr->half) {
				len   *= 2;
			}
		} else if (is_half(instr) != arr->half) {
			/* for split-register-file mode, we only conflict with
			 * other arrays of same precision:
			 */
			continue;
		}

		/* Check if array intersects with liverange AND register
		 * range of the input:
		 */
		if (intersects(arr->start_ip, arr->end_ip,
						ctx->def[name], ctx->use[name]) &&
				intersects(base, base + len,
						regid, regid + reglen)) {
			base = MAX2(base, regid + reglen);
			goto retry;
		}
	}

	/* convert back from half-reg space to fullreg space: */
	if (mergedregs && !arr->half) {
		base = DIV_ROUND_UP(base, 2);
	}

	arr->reg = base;
}

/* handle pre-colored registers.  This includes "arrays" (which could be of
 * length 1, used for phi webs lowered to registers in nir), as well as
 * special shader input values that need to be pinned to certain registers.
 */
static void
ra_precolor(struct ir3_ra_ctx *ctx, struct ir3_instruction **precolor, unsigned nprecolor)
{
	for (unsigned i = 0; i < nprecolor; i++) {
		if (precolor[i] && !(precolor[i]->flags & IR3_INSTR_UNUSED)) {
			struct ir3_instruction *instr = precolor[i];

			if (instr->regs[0]->num == INVALID_REG)
				continue;

			struct ir3_ra_instr_data *id = &ctx->instrd[instr->ip];

			ra_assert(ctx, !(instr->regs[0]->flags & (IR3_REG_HALF | IR3_REG_HIGH)));

			/* 'base' is in scalar (class 0) but we need to map that
			 * the conflicting register of the appropriate class (ie.
			 * input could be vec2/vec3/etc)
			 *
			 * Note that the higher class (larger than scalar) regs
			 * are setup to conflict with others in the same class,
			 * so for example, R1 (scalar) is also the first component
			 * of D1 (vec2/double):
			 *
			 *    Single (base) |  Double
			 *    --------------+---------------
			 *       R0         |  D0
			 *       R1         |  D0 D1
			 *       R2         |     D1 D2
			 *       R3         |        D2
			 *           .. and so on..
			 */
			unsigned regid = instr->regs[0]->num;
			ra_assert(ctx, regid >= id->off);
			regid -= id->off;

			unsigned reg = ctx->set->gpr_to_ra_reg[id->cls][regid];
			unsigned name = ra_name(ctx, id);
			ra_set_node_reg(ctx->g, name, reg);
		}
	}

	/*
	 * Pre-assign array elements:
	 */
	foreach_array (arr, &ctx->ir->array_list) {

		if (arr->end_ip == 0)
			continue;

		if (!ctx->scalar_pass)
			assign_arr_base(ctx, arr, precolor, nprecolor);

		for (unsigned i = 0; i < arr->length; i++) {
			unsigned cls = arr->half ? HALF_OFFSET : 0;

			ra_set_node_reg(ctx->g,
					arr->base + i,   /* vreg name */
					ctx->set->gpr_to_ra_reg[cls][arr->reg + i]);
		}
	}

	if (ir3_shader_debug & IR3_DBG_OPTMSGS) {
		foreach_array (arr, &ctx->ir->array_list) {
			unsigned first = arr->reg;
			unsigned last  = arr->reg + arr->length - 1;
			debug_printf("arr[%d] at r%d.%c->r%d.%c\n", arr->id,
					(first >> 2), "xyzw"[first & 0x3],
					(last >> 2), "xyzw"[last & 0x3]);
		}
	}
}

static void
precolor(struct ir3_ra_ctx *ctx, struct ir3_instruction *instr)
{
	struct ir3_ra_instr_data *id = &ctx->instrd[instr->ip];
	unsigned n = dest_regs(instr);
	for (unsigned i = 0; i < n; i++) {
		/* tex instructions actually have a wrmask, and
		 * don't touch masked out components.  So we
		 * shouldn't precolor them::
		 */
		if (is_tex_or_prefetch(instr) &&
				!(instr->regs[0]->wrmask & (1 << i)))
			continue;

		unsigned name = scalar_name(ctx, instr, i);
		unsigned regid = instr->regs[0]->num + i;

		if (instr->regs[0]->flags & IR3_REG_HIGH)
			regid -= FIRST_HIGH_REG;

		unsigned vreg = ctx->set->gpr_to_ra_reg[id->cls][regid];
		ra_set_node_reg(ctx->g, name, vreg);
	}
}

/* pre-color non-scalar registers based on the registers assigned in previous
 * pass.  Do this by looking actually at the fanout instructions.
 */
static void
ra_precolor_assigned(struct ir3_ra_ctx *ctx)
{
	ra_assert(ctx, ctx->scalar_pass);

	foreach_block (block, &ctx->ir->block_list) {
		foreach_instr (instr, &block->instr_list) {

			if (!writes_gpr(instr))
				continue;

			if (should_assign(ctx, instr))
				continue;

			precolor(ctx, instr);

			foreach_src (src, instr) {
				if (!src->instr)
					continue;
				precolor(ctx, src->instr);
			}
		}
	}
}

static int
ra_alloc(struct ir3_ra_ctx *ctx)
{
	if (!ra_allocate(ctx->g))
		return -1;

	foreach_block (block, &ctx->ir->block_list) {
		ra_block_alloc(ctx, block);
	}

	return 0;
}

/* if we end up with split/collect instructions with non-matching src
 * and dest regs, that means something has gone wrong.  Which makes it
 * a pretty good sanity check.
 */
static void
ra_sanity_check(struct ir3 *ir)
{
	foreach_block (block, &ir->block_list) {
		foreach_instr (instr, &block->instr_list) {
			if (instr->opc == OPC_META_SPLIT) {
				struct ir3_register *dst = instr->regs[0];
				struct ir3_register *src = instr->regs[1];
				debug_assert(dst->num == (src->num + instr->split.off));
			} else if (instr->opc == OPC_META_COLLECT) {
				struct ir3_register *dst = instr->regs[0];

				foreach_src_n (src, n, instr) {
					debug_assert(dst->num == (src->num - n));
				}
			}
		}
	}
}

static int
ir3_ra_pass(struct ir3_shader_variant *v, struct ir3_instruction **precolor,
		unsigned nprecolor, bool scalar_pass)
{
	struct ir3_ra_ctx ctx = {
			.v = v,
			.ir = v->ir,
			.set = v->mergedregs ?
				v->ir->compiler->mergedregs_set : v->ir->compiler->set,
			.scalar_pass = scalar_pass,
	};
	int ret;

	ret = setjmp(ctx.jmp_env);
	if (ret)
		goto fail;

	ra_init(&ctx);
	ra_add_interference(&ctx);
	ra_precolor(&ctx, precolor, nprecolor);
	if (scalar_pass)
		ra_precolor_assigned(&ctx);
	ret = ra_alloc(&ctx);

fail:
	ra_destroy(&ctx);

	return ret;
}

int
ir3_ra(struct ir3_shader_variant *v, struct ir3_instruction **precolor,
		unsigned nprecolor)
{
	int ret;

	/* First pass, assign the vecN (non-scalar) registers: */
	ret = ir3_ra_pass(v, precolor, nprecolor, false);
	if (ret)
		return ret;

	ir3_debug_print(v->ir, "AFTER: ir3_ra (1st pass)");

	/* Second pass, assign the scalar registers: */
	ret = ir3_ra_pass(v, precolor, nprecolor, true);
	if (ret)
		return ret;

	ir3_debug_print(v->ir, "AFTER: ir3_ra (2st pass)");

#ifdef DEBUG
#  define SANITY_CHECK DEBUG
#else
#  define SANITY_CHECK 0
#endif
	if (SANITY_CHECK)
		ra_sanity_check(v->ir);

	return ret;
}
