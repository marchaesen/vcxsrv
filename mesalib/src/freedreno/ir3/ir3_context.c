/*
 * Copyright (C) 2015-2018 Rob Clark <robclark@freedesktop.org>
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

#include "ir3_compiler.h"
#include "ir3_context.h"
#include "ir3_shader.h"
#include "ir3_nir.h"

struct ir3_context *
ir3_context_init(struct ir3_compiler *compiler,
		struct ir3_shader_variant *so)
{
	struct ir3_context *ctx = rzalloc(NULL, struct ir3_context);

	if (compiler->gpu_id >= 400) {
		if (so->type == MESA_SHADER_VERTEX) {
			ctx->astc_srgb = so->key.vastc_srgb;
		} else if (so->type == MESA_SHADER_FRAGMENT) {
			ctx->astc_srgb = so->key.fastc_srgb;
		}

	} else {
		if (so->type == MESA_SHADER_VERTEX) {
			ctx->samples = so->key.vsamples;
		} else if (so->type == MESA_SHADER_FRAGMENT) {
			ctx->samples = so->key.fsamples;
		}
	}

	ctx->compiler = compiler;
	ctx->so = so;
	ctx->def_ht = _mesa_hash_table_create(ctx,
			_mesa_hash_pointer, _mesa_key_pointer_equal);
	ctx->block_ht = _mesa_hash_table_create(ctx,
			_mesa_hash_pointer, _mesa_key_pointer_equal);

	/* TODO: maybe generate some sort of bitmask of what key
	 * lowers vs what shader has (ie. no need to lower
	 * texture clamp lowering if no texture sample instrs)..
	 * although should be done further up the stack to avoid
	 * creating duplicate variants..
	 */

	if (ir3_key_lowers_nir(&so->key)) {
		nir_shader *s = nir_shader_clone(ctx, so->shader->nir);
		ctx->s = ir3_optimize_nir(so->shader, s, &so->key);
	} else {
		/* fast-path for shader key that lowers nothing in NIR: */
		ctx->s = so->shader->nir;
	}

	/* this needs to be the last pass run, so do this here instead of
	 * in ir3_optimize_nir():
	 */
	NIR_PASS_V(ctx->s, nir_lower_locals_to_regs);
	NIR_PASS_V(ctx->s, nir_convert_from_ssa, true);

	if (ir3_shader_debug & IR3_DBG_DISASM) {
		DBG("dump nir%dv%d: type=%d, k={cts=%u,hp=%u}",
			so->shader->id, so->id, so->type,
			so->key.color_two_side, so->key.half_precision);
		nir_print_shader(ctx->s, stdout);
	}

	if (shader_debug_enabled(so->type)) {
		fprintf(stderr, "NIR (final form) for %s shader:\n",
			_mesa_shader_stage_to_string(so->type));
		nir_print_shader(ctx->s, stderr);
	}

	ir3_nir_scan_driver_consts(ctx->s, &so->const_layout);

	so->num_uniforms = ctx->s->num_uniforms;
	so->num_ubos = ctx->s->info.num_ubos;

	/* Layout of constant registers, each section aligned to vec4.  Note
	 * that pointer size (ubo, etc) changes depending on generation.
	 *
	 *    user consts
	 *    UBO addresses
	 *    SSBO sizes
	 *    if (vertex shader) {
	 *        driver params (IR3_DP_*)
	 *        if (stream_output.num_outputs > 0)
	 *           stream-out addresses
	 *    }
	 *    immediates
	 *
	 * Immediates go last mostly because they are inserted in the CP pass
	 * after the nir -> ir3 frontend.
	 */
	unsigned constoff = align(ctx->s->num_uniforms, 4);
	unsigned ptrsz = ir3_pointer_size(ctx);

	memset(&so->constbase, ~0, sizeof(so->constbase));

	if (so->num_ubos > 0) {
		so->constbase.ubo = constoff;
		constoff += align(ctx->s->info.num_ubos * ptrsz, 4) / 4;
	}

	if (so->const_layout.ssbo_size.count > 0) {
		unsigned cnt = so->const_layout.ssbo_size.count;
		so->constbase.ssbo_sizes = constoff;
		constoff += align(cnt, 4) / 4;
	}

	if (so->const_layout.image_dims.count > 0) {
		unsigned cnt = so->const_layout.image_dims.count;
		so->constbase.image_dims = constoff;
		constoff += align(cnt, 4) / 4;
	}

	unsigned num_driver_params = 0;
	if (so->type == MESA_SHADER_VERTEX) {
		num_driver_params = IR3_DP_VS_COUNT;
	} else if (so->type == MESA_SHADER_COMPUTE) {
		num_driver_params = IR3_DP_CS_COUNT;
	}

	so->constbase.driver_param = constoff;
	constoff += align(num_driver_params, 4) / 4;

	if ((so->type == MESA_SHADER_VERTEX) &&
			(compiler->gpu_id < 500) &&
			so->shader->stream_output.num_outputs > 0) {
		so->constbase.tfbo = constoff;
		constoff += align(IR3_MAX_SO_BUFFERS * ptrsz, 4) / 4;
	}

	so->constbase.immediate = constoff;

	return ctx;
}

void
ir3_context_free(struct ir3_context *ctx)
{
	ralloc_free(ctx);
}

/*
 * Misc helpers
 */

/* allocate a n element value array (to be populated by caller) and
 * insert in def_ht
 */
struct ir3_instruction **
ir3_get_dst_ssa(struct ir3_context *ctx, nir_ssa_def *dst, unsigned n)
{
	struct ir3_instruction **value =
		ralloc_array(ctx->def_ht, struct ir3_instruction *, n);
	_mesa_hash_table_insert(ctx->def_ht, dst, value);
	return value;
}

struct ir3_instruction **
ir3_get_dst(struct ir3_context *ctx, nir_dest *dst, unsigned n)
{
	struct ir3_instruction **value;

	if (dst->is_ssa) {
		value = ir3_get_dst_ssa(ctx, &dst->ssa, n);
	} else {
		value = ralloc_array(ctx, struct ir3_instruction *, n);
	}

	/* NOTE: in non-ssa case, we don't really need to store last_dst
	 * but this helps us catch cases where put_dst() call is forgotten
	 */
	compile_assert(ctx, !ctx->last_dst);
	ctx->last_dst = value;
	ctx->last_dst_n = n;

	return value;
}

struct ir3_instruction * const *
ir3_get_src(struct ir3_context *ctx, nir_src *src)
{
	if (src->is_ssa) {
		struct hash_entry *entry;
		entry = _mesa_hash_table_search(ctx->def_ht, src->ssa);
		compile_assert(ctx, entry);
		return entry->data;
	} else {
		nir_register *reg = src->reg.reg;
		struct ir3_array *arr = ir3_get_array(ctx, reg);
		unsigned num_components = arr->r->num_components;
		struct ir3_instruction *addr = NULL;
		struct ir3_instruction **value =
			ralloc_array(ctx, struct ir3_instruction *, num_components);

		if (src->reg.indirect)
			addr = ir3_get_addr(ctx, ir3_get_src(ctx, src->reg.indirect)[0],
					reg->num_components);

		for (unsigned i = 0; i < num_components; i++) {
			unsigned n = src->reg.base_offset * reg->num_components + i;
			compile_assert(ctx, n < arr->length);
			value[i] = ir3_create_array_load(ctx, arr, n, addr);
		}

		return value;
	}
}

void
put_dst(struct ir3_context *ctx, nir_dest *dst)
{
	unsigned bit_size = nir_dest_bit_size(*dst);

	if (bit_size < 32) {
		for (unsigned i = 0; i < ctx->last_dst_n; i++) {
			struct ir3_instruction *dst = ctx->last_dst[i];
			dst->regs[0]->flags |= IR3_REG_HALF;
			if (ctx->last_dst[i]->opc == OPC_META_FO)
				dst->regs[1]->instr->regs[0]->flags |= IR3_REG_HALF;
		}
	}

	if (!dst->is_ssa) {
		nir_register *reg = dst->reg.reg;
		struct ir3_array *arr = ir3_get_array(ctx, reg);
		unsigned num_components = ctx->last_dst_n;
		struct ir3_instruction *addr = NULL;

		if (dst->reg.indirect)
			addr = ir3_get_addr(ctx, ir3_get_src(ctx, dst->reg.indirect)[0],
					reg->num_components);

		for (unsigned i = 0; i < num_components; i++) {
			unsigned n = dst->reg.base_offset * reg->num_components + i;
			compile_assert(ctx, n < arr->length);
			if (!ctx->last_dst[i])
				continue;
			ir3_create_array_store(ctx, arr, n, ctx->last_dst[i], addr);
		}

		ralloc_free(ctx->last_dst);
	}
	ctx->last_dst = NULL;
	ctx->last_dst_n = 0;
}

struct ir3_instruction *
ir3_create_collect(struct ir3_context *ctx, struct ir3_instruction *const *arr,
		unsigned arrsz)
{
	struct ir3_block *block = ctx->block;
	struct ir3_instruction *collect;

	if (arrsz == 0)
		return NULL;

	unsigned flags = arr[0]->regs[0]->flags & IR3_REG_HALF;

	collect = ir3_instr_create2(block, OPC_META_FI, 1 + arrsz);
	ir3_reg_create(collect, 0, flags);     /* dst */
	for (unsigned i = 0; i < arrsz; i++) {
		struct ir3_instruction *elem = arr[i];

		/* Since arrays are pre-colored in RA, we can't assume that
		 * things will end up in the right place.  (Ie. if a collect
		 * joins elements from two different arrays.)  So insert an
		 * extra mov.
		 *
		 * We could possibly skip this if all the collected elements
		 * are contiguous elements in a single array.. not sure how
		 * likely that is to happen.
		 *
		 * Fixes a problem with glamor shaders, that in effect do
		 * something like:
		 *
		 *   if (foo)
		 *     texcoord = ..
		 *   else
		 *     texcoord = ..
		 *   color = texture2D(tex, texcoord);
		 *
		 * In this case, texcoord will end up as nir registers (which
		 * translate to ir3 array's of length 1.  And we can't assume
		 * the two (or more) arrays will get allocated in consecutive
		 * scalar registers.
		 *
		 */
		if (elem->regs[0]->flags & IR3_REG_ARRAY) {
			type_t type = (flags & IR3_REG_HALF) ? TYPE_U16 : TYPE_U32;
			elem = ir3_MOV(block, elem, type);
		}

		compile_assert(ctx, (elem->regs[0]->flags & IR3_REG_HALF) == flags);
		ir3_reg_create(collect, 0, IR3_REG_SSA | flags)->instr = elem;
	}

	return collect;
}

/* helper for instructions that produce multiple consecutive scalar
 * outputs which need to have a split/fanout meta instruction inserted
 */
void
ir3_split_dest(struct ir3_block *block, struct ir3_instruction **dst,
		struct ir3_instruction *src, unsigned base, unsigned n)
{
	struct ir3_instruction *prev = NULL;

	if ((n == 1) && (src->regs[0]->wrmask == 0x1)) {
		dst[0] = src;
		return;
	}

	for (int i = 0, j = 0; i < n; i++) {
		struct ir3_instruction *split = ir3_instr_create(block, OPC_META_FO);
		ir3_reg_create(split, 0, IR3_REG_SSA);
		ir3_reg_create(split, 0, IR3_REG_SSA)->instr = src;
		split->fo.off = i + base;

		if (prev) {
			split->cp.left = prev;
			split->cp.left_cnt++;
			prev->cp.right = split;
			prev->cp.right_cnt++;
		}
		prev = split;

		if (src->regs[0]->wrmask & (1 << (i + base)))
			dst[j++] = split;
	}
}

void
ir3_context_error(struct ir3_context *ctx, const char *format, ...)
{
	struct hash_table *errors = NULL;
	va_list ap;
	va_start(ap, format);
	if (ctx->cur_instr) {
		errors = _mesa_hash_table_create(NULL,
				_mesa_hash_pointer,
				_mesa_key_pointer_equal);
		char *msg = ralloc_vasprintf(errors, format, ap);
		_mesa_hash_table_insert(errors, ctx->cur_instr, msg);
	} else {
		_debug_vprintf(format, ap);
	}
	va_end(ap);
	nir_print_shader_annotated(ctx->s, stdout, errors);
	ralloc_free(errors);
	ctx->error = true;
	debug_assert(0);
}

static struct ir3_instruction *
create_addr(struct ir3_block *block, struct ir3_instruction *src, int align)
{
	struct ir3_instruction *instr, *immed;

	/* TODO in at least some cases, the backend could probably be
	 * made clever enough to propagate IR3_REG_HALF..
	 */
	instr = ir3_COV(block, src, TYPE_U32, TYPE_S16);
	instr->regs[0]->flags |= IR3_REG_HALF;

	switch(align){
	case 1:
		/* src *= 1: */
		break;
	case 2:
		/* src *= 2	=> src <<= 1: */
		immed = create_immed(block, 1);
		immed->regs[0]->flags |= IR3_REG_HALF;

		instr = ir3_SHL_B(block, instr, 0, immed, 0);
		instr->regs[0]->flags |= IR3_REG_HALF;
		instr->regs[1]->flags |= IR3_REG_HALF;
		break;
	case 3:
		/* src *= 3: */
		immed = create_immed(block, 3);
		immed->regs[0]->flags |= IR3_REG_HALF;

		instr = ir3_MULL_U(block, instr, 0, immed, 0);
		instr->regs[0]->flags |= IR3_REG_HALF;
		instr->regs[1]->flags |= IR3_REG_HALF;
		break;
	case 4:
		/* src *= 4 => src <<= 2: */
		immed = create_immed(block, 2);
		immed->regs[0]->flags |= IR3_REG_HALF;

		instr = ir3_SHL_B(block, instr, 0, immed, 0);
		instr->regs[0]->flags |= IR3_REG_HALF;
		instr->regs[1]->flags |= IR3_REG_HALF;
		break;
	default:
		unreachable("bad align");
		return NULL;
	}

	instr = ir3_MOV(block, instr, TYPE_S16);
	instr->regs[0]->num = regid(REG_A0, 0);
	instr->regs[0]->flags |= IR3_REG_HALF;
	instr->regs[1]->flags |= IR3_REG_HALF;

	return instr;
}

/* caches addr values to avoid generating multiple cov/shl/mova
 * sequences for each use of a given NIR level src as address
 */
struct ir3_instruction *
ir3_get_addr(struct ir3_context *ctx, struct ir3_instruction *src, int align)
{
	struct ir3_instruction *addr;
	unsigned idx = align - 1;

	compile_assert(ctx, idx < ARRAY_SIZE(ctx->addr_ht));

	if (!ctx->addr_ht[idx]) {
		ctx->addr_ht[idx] = _mesa_hash_table_create(ctx,
				_mesa_hash_pointer, _mesa_key_pointer_equal);
	} else {
		struct hash_entry *entry;
		entry = _mesa_hash_table_search(ctx->addr_ht[idx], src);
		if (entry)
			return entry->data;
	}

	addr = create_addr(ctx->block, src, align);
	_mesa_hash_table_insert(ctx->addr_ht[idx], src, addr);

	return addr;
}

struct ir3_instruction *
ir3_get_predicate(struct ir3_context *ctx, struct ir3_instruction *src)
{
	struct ir3_block *b = ctx->block;
	struct ir3_instruction *cond;

	/* NOTE: only cmps.*.* can write p0.x: */
	cond = ir3_CMPS_S(b, src, 0, create_immed(b, 0), 0);
	cond->cat2.condition = IR3_COND_NE;

	/* condition always goes in predicate register: */
	cond->regs[0]->num = regid(REG_P0, 0);

	return cond;
}

/*
 * Array helpers
 */

void
ir3_declare_array(struct ir3_context *ctx, nir_register *reg)
{
	struct ir3_array *arr = rzalloc(ctx, struct ir3_array);
	arr->id = ++ctx->num_arrays;
	/* NOTE: sometimes we get non array regs, for example for arrays of
	 * length 1.  See fs-const-array-of-struct-of-array.shader_test.  So
	 * treat a non-array as if it was an array of length 1.
	 *
	 * It would be nice if there was a nir pass to convert arrays of
	 * length 1 to ssa.
	 */
	arr->length = reg->num_components * MAX2(1, reg->num_array_elems);
	compile_assert(ctx, arr->length > 0);
	arr->r = reg;
	list_addtail(&arr->node, &ctx->ir->array_list);
}

struct ir3_array *
ir3_get_array(struct ir3_context *ctx, nir_register *reg)
{
	list_for_each_entry (struct ir3_array, arr, &ctx->ir->array_list, node) {
		if (arr->r == reg)
			return arr;
	}
	ir3_context_error(ctx, "bogus reg: %s\n", reg->name);
	return NULL;
}

/* relative (indirect) if address!=NULL */
struct ir3_instruction *
ir3_create_array_load(struct ir3_context *ctx, struct ir3_array *arr, int n,
		struct ir3_instruction *address)
{
	struct ir3_block *block = ctx->block;
	struct ir3_instruction *mov;
	struct ir3_register *src;

	mov = ir3_instr_create(block, OPC_MOV);
	mov->cat1.src_type = TYPE_U32;
	mov->cat1.dst_type = TYPE_U32;
	mov->barrier_class = IR3_BARRIER_ARRAY_R;
	mov->barrier_conflict = IR3_BARRIER_ARRAY_W;
	ir3_reg_create(mov, 0, 0);
	src = ir3_reg_create(mov, 0, IR3_REG_ARRAY |
			COND(address, IR3_REG_RELATIV));
	src->instr = arr->last_write;
	src->size  = arr->length;
	src->array.id = arr->id;
	src->array.offset = n;

	if (address)
		ir3_instr_set_address(mov, address);

	return mov;
}

/* relative (indirect) if address!=NULL */
void
ir3_create_array_store(struct ir3_context *ctx, struct ir3_array *arr, int n,
		struct ir3_instruction *src, struct ir3_instruction *address)
{
	struct ir3_block *block = ctx->block;
	struct ir3_instruction *mov;
	struct ir3_register *dst;

	/* if not relative store, don't create an extra mov, since that
	 * ends up being difficult for cp to remove.
	 */
	if (!address) {
		dst = src->regs[0];

		src->barrier_class |= IR3_BARRIER_ARRAY_W;
		src->barrier_conflict |= IR3_BARRIER_ARRAY_R | IR3_BARRIER_ARRAY_W;

		dst->flags |= IR3_REG_ARRAY;
		dst->instr = arr->last_write;
		dst->size = arr->length;
		dst->array.id = arr->id;
		dst->array.offset = n;

		arr->last_write = src;

		array_insert(block, block->keeps, src);

		return;
	}

	mov = ir3_instr_create(block, OPC_MOV);
	mov->cat1.src_type = TYPE_U32;
	mov->cat1.dst_type = TYPE_U32;
	mov->barrier_class = IR3_BARRIER_ARRAY_W;
	mov->barrier_conflict = IR3_BARRIER_ARRAY_R | IR3_BARRIER_ARRAY_W;
	dst = ir3_reg_create(mov, 0, IR3_REG_ARRAY |
			COND(address, IR3_REG_RELATIV));
	dst->instr = arr->last_write;
	dst->size  = arr->length;
	dst->array.id = arr->id;
	dst->array.offset = n;
	ir3_reg_create(mov, 0, IR3_REG_SSA)->instr = src;

	if (address)
		ir3_instr_set_address(mov, address);

	arr->last_write = mov;

	/* the array store may only matter to something in an earlier
	 * block (ie. loops), but since arrays are not in SSA, depth
	 * pass won't know this.. so keep all array stores:
	 */
	array_insert(block, block->keeps, mov);
}
