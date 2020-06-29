/*
 * Copyright (C) 2017-2018 Rob Clark <robclark@freedesktop.org>
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

#define GPU 600

#include "ir3_context.h"
#include "ir3_image.h"

/*
 * Handlers for instructions changed/added in a6xx:
 *
 * Starting with a6xx, isam and stbi is used for SSBOs as well; stbi and the
 * atomic instructions (used for both SSBO and image) use a new instruction
 * encoding compared to a4xx/a5xx.
 */

/* src[] = { buffer_index, offset }. No const_index */
static void
emit_intrinsic_load_ssbo(struct ir3_context *ctx, nir_intrinsic_instr *intr,
		struct ir3_instruction **dst)
{
	struct ir3_block *b = ctx->block;
	struct ir3_instruction *offset;
	struct ir3_instruction *ldib;

	offset = ir3_get_src(ctx, &intr->src[2])[0];

	ldib = ir3_LDIB(b, ir3_ssbo_to_ibo(ctx, intr->src[0]), 0, offset, 0);
	ldib->regs[0]->wrmask = MASK(intr->num_components);
	ldib->cat6.iim_val = intr->num_components;
	ldib->cat6.d = 1;
	ldib->cat6.type = intr->dest.ssa.bit_size == 16 ? TYPE_U16 : TYPE_U32;
	ldib->barrier_class = IR3_BARRIER_BUFFER_R;
	ldib->barrier_conflict = IR3_BARRIER_BUFFER_W;
	ir3_handle_bindless_cat6(ldib, intr->src[0]);

	ir3_split_dest(b, dst, ldib, 0, intr->num_components);
}

/* src[] = { value, block_index, offset }. const_index[] = { write_mask } */
static void
emit_intrinsic_store_ssbo(struct ir3_context *ctx, nir_intrinsic_instr *intr)
{
	struct ir3_block *b = ctx->block;
	struct ir3_instruction *stib, *val, *offset;
	unsigned wrmask = nir_intrinsic_write_mask(intr);
	unsigned ncomp = ffs(~wrmask) - 1;

	assert(wrmask == BITFIELD_MASK(intr->num_components));

	/* src0 is offset, src1 is value:
	 */
	val = ir3_create_collect(ctx, ir3_get_src(ctx, &intr->src[0]), ncomp);
	offset = ir3_get_src(ctx, &intr->src[3])[0];

	stib = ir3_STIB(b, ir3_ssbo_to_ibo(ctx, intr->src[1]), 0, offset, 0, val, 0);
	stib->cat6.iim_val = ncomp;
	stib->cat6.d = 1;
	stib->cat6.type = intr->src[0].ssa->bit_size == 16 ? TYPE_U16 : TYPE_U32;
	stib->barrier_class = IR3_BARRIER_BUFFER_W;
	stib->barrier_conflict = IR3_BARRIER_BUFFER_R | IR3_BARRIER_BUFFER_W;
	ir3_handle_bindless_cat6(stib, intr->src[1]);

	array_insert(b, b->keeps, stib);
}

/*
 * SSBO atomic intrinsics
 *
 * All of the SSBO atomic memory operations read a value from memory,
 * compute a new value using one of the operations below, write the new
 * value to memory, and return the original value read.
 *
 * All operations take 3 sources except CompSwap that takes 4. These
 * sources represent:
 *
 * 0: The SSBO buffer index.
 * 1: The offset into the SSBO buffer of the variable that the atomic
 *    operation will operate on.
 * 2: The data parameter to the atomic function (i.e. the value to add
 *    in ssbo_atomic_add, etc).
 * 3: For CompSwap only: the second data parameter.
 */
static struct ir3_instruction *
emit_intrinsic_atomic_ssbo(struct ir3_context *ctx, nir_intrinsic_instr *intr)
{
	struct ir3_block *b = ctx->block;
	struct ir3_instruction *atomic, *ibo, *src0, *src1, *data, *dummy;
	type_t type = TYPE_U32;

	ibo = ir3_ssbo_to_ibo(ctx, intr->src[0]);

	data   = ir3_get_src(ctx, &intr->src[2])[0];

	/* So this gets a bit creative:
	 *
	 *    src0    - vecN offset/coords
	 *    src1.x  - is actually destination register
	 *    src1.y  - is 'data' except for cmpxchg where src2.y is 'compare'
	 *    src1.z  - is 'data' for cmpxchg
	 *
	 * The combining src and dest kinda doesn't work out so well with how
	 * scheduling and RA work.  So for now we create a dummy src2.x, and
	 * then in a later fixup path, insert an extra MOV out of src1.x.
	 * See ir3_a6xx_fixup_atomic_dests().
	 *
	 * Note that nir already multiplies the offset by four
	 */
	dummy = create_immed(b, 0);

	if (intr->intrinsic == nir_intrinsic_ssbo_atomic_comp_swap_ir3) {
		src0 = ir3_get_src(ctx, &intr->src[4])[0];
		struct ir3_instruction *compare = ir3_get_src(ctx, &intr->src[3])[0];
		src1 = ir3_create_collect(ctx, (struct ir3_instruction*[]){
			dummy, compare, data
		}, 3);
	} else {
		src0 = ir3_get_src(ctx, &intr->src[3])[0];
		src1 = ir3_create_collect(ctx, (struct ir3_instruction*[]){
			dummy, data
		}, 2);
	}

	switch (intr->intrinsic) {
	case nir_intrinsic_ssbo_atomic_add_ir3:
		atomic = ir3_ATOMIC_ADD_G(b, ibo, 0, src0, 0, src1, 0);
		break;
	case nir_intrinsic_ssbo_atomic_imin_ir3:
		atomic = ir3_ATOMIC_MIN_G(b, ibo, 0, src0, 0, src1, 0);
		type = TYPE_S32;
		break;
	case nir_intrinsic_ssbo_atomic_umin_ir3:
		atomic = ir3_ATOMIC_MIN_G(b, ibo, 0, src0, 0, src1, 0);
		break;
	case nir_intrinsic_ssbo_atomic_imax_ir3:
		atomic = ir3_ATOMIC_MAX_G(b, ibo, 0, src0, 0, src1, 0);
		type = TYPE_S32;
		break;
	case nir_intrinsic_ssbo_atomic_umax_ir3:
		atomic = ir3_ATOMIC_MAX_G(b, ibo, 0, src0, 0, src1, 0);
		break;
	case nir_intrinsic_ssbo_atomic_and_ir3:
		atomic = ir3_ATOMIC_AND_G(b, ibo, 0, src0, 0, src1, 0);
		break;
	case nir_intrinsic_ssbo_atomic_or_ir3:
		atomic = ir3_ATOMIC_OR_G(b, ibo, 0, src0, 0, src1, 0);
		break;
	case nir_intrinsic_ssbo_atomic_xor_ir3:
		atomic = ir3_ATOMIC_XOR_G(b, ibo, 0, src0, 0, src1, 0);
		break;
	case nir_intrinsic_ssbo_atomic_exchange_ir3:
		atomic = ir3_ATOMIC_XCHG_G(b, ibo, 0, src0, 0, src1, 0);
		break;
	case nir_intrinsic_ssbo_atomic_comp_swap_ir3:
		atomic = ir3_ATOMIC_CMPXCHG_G(b, ibo, 0, src0, 0, src1, 0);
		break;
	default:
		unreachable("boo");
	}

	atomic->cat6.iim_val = 1;
	atomic->cat6.d = 1;
	atomic->cat6.type = type;
	atomic->barrier_class = IR3_BARRIER_BUFFER_W;
	atomic->barrier_conflict = IR3_BARRIER_BUFFER_R | IR3_BARRIER_BUFFER_W;
	ir3_handle_bindless_cat6(atomic, intr->src[0]);

	/* even if nothing consume the result, we can't DCE the instruction: */
	array_insert(b, b->keeps, atomic);

	return atomic;
}

/* src[] = { deref, coord, sample_index }. const_index[] = {} */
static void
emit_intrinsic_load_image(struct ir3_context *ctx, nir_intrinsic_instr *intr,
		struct ir3_instruction **dst)
{
	struct ir3_block *b = ctx->block;
	struct ir3_instruction *ldib;
	struct ir3_instruction * const *coords = ir3_get_src(ctx, &intr->src[1]);
	unsigned ncoords = ir3_get_image_coords(intr, NULL);

	ldib = ir3_LDIB(b, ir3_image_to_ibo(ctx, intr->src[0]), 0,
					ir3_create_collect(ctx, coords, ncoords), 0);
	ldib->regs[0]->wrmask = MASK(intr->num_components);
	ldib->cat6.iim_val = intr->num_components;
	ldib->cat6.d = ncoords;
	ldib->cat6.type = ir3_get_type_for_image_intrinsic(intr);
	ldib->cat6.typed = true;
	ldib->barrier_class = IR3_BARRIER_IMAGE_R;
	ldib->barrier_conflict = IR3_BARRIER_IMAGE_W;
	ir3_handle_bindless_cat6(ldib, intr->src[0]);

	ir3_split_dest(b, dst, ldib, 0, intr->num_components);
}

/* src[] = { deref, coord, sample_index, value }. const_index[] = {} */
static void
emit_intrinsic_store_image(struct ir3_context *ctx, nir_intrinsic_instr *intr)
{
	struct ir3_block *b = ctx->block;
	struct ir3_instruction *stib;
	struct ir3_instruction * const *value = ir3_get_src(ctx, &intr->src[3]);
	struct ir3_instruction * const *coords = ir3_get_src(ctx, &intr->src[1]);
	unsigned ncoords = ir3_get_image_coords(intr, NULL);
	enum pipe_format format = nir_intrinsic_format(intr);
	unsigned ncomp = ir3_get_num_components_for_image_format(format);

	/* src0 is offset, src1 is value:
	 */
	stib = ir3_STIB(b, ir3_image_to_ibo(ctx, intr->src[0]), 0,
			ir3_create_collect(ctx, coords, ncoords), 0,
			ir3_create_collect(ctx, value, ncomp), 0);
	stib->cat6.iim_val = ncomp;
	stib->cat6.d = ncoords;
	stib->cat6.type = ir3_get_type_for_image_intrinsic(intr);
	stib->cat6.typed = true;
	stib->barrier_class = IR3_BARRIER_IMAGE_W;
	stib->barrier_conflict = IR3_BARRIER_IMAGE_R | IR3_BARRIER_IMAGE_W;
	ir3_handle_bindless_cat6(stib, intr->src[0]);

	array_insert(b, b->keeps, stib);
}

/* src[] = { deref, coord, sample_index, value, compare }. const_index[] = {} */
static struct ir3_instruction *
emit_intrinsic_atomic_image(struct ir3_context *ctx, nir_intrinsic_instr *intr)
{
	struct ir3_block *b = ctx->block;
	struct ir3_instruction *atomic, *ibo, *src0, *src1, *dummy;
	struct ir3_instruction * const *coords = ir3_get_src(ctx, &intr->src[1]);
	struct ir3_instruction *value = ir3_get_src(ctx, &intr->src[3])[0];
	unsigned ncoords = ir3_get_image_coords(intr, NULL);

	ibo = ir3_image_to_ibo(ctx, intr->src[0]);

	/* So this gets a bit creative:
	 *
	 *    src0    - vecN offset/coords
	 *    src1.x  - is actually destination register
	 *    src1.y  - is 'value' except for cmpxchg where src2.y is 'compare'
	 *    src1.z  - is 'value' for cmpxchg
	 *
	 * The combining src and dest kinda doesn't work out so well with how
	 * scheduling and RA work.  So for now we create a dummy src2.x, and
	 * then in a later fixup path, insert an extra MOV out of src1.x.
	 * See ir3_a6xx_fixup_atomic_dests().
	 */
	dummy = create_immed(b, 0);
	src0 = ir3_create_collect(ctx, coords, ncoords);

	if (intr->intrinsic == nir_intrinsic_image_atomic_comp_swap ||
		intr->intrinsic == nir_intrinsic_bindless_image_atomic_comp_swap) {
		struct ir3_instruction *compare = ir3_get_src(ctx, &intr->src[4])[0];
		src1 = ir3_create_collect(ctx, (struct ir3_instruction*[]){
			dummy, compare, value
		}, 3);
	} else {
		src1 = ir3_create_collect(ctx, (struct ir3_instruction*[]){
			dummy, value
		}, 2);
	}

	switch (intr->intrinsic) {
	case nir_intrinsic_image_atomic_add:
	case nir_intrinsic_bindless_image_atomic_add:
		atomic = ir3_ATOMIC_ADD_G(b, ibo, 0, src0, 0, src1, 0);
		break;
	case nir_intrinsic_image_atomic_imin:
	case nir_intrinsic_image_atomic_umin:
	case nir_intrinsic_bindless_image_atomic_imin:
	case nir_intrinsic_bindless_image_atomic_umin:
		atomic = ir3_ATOMIC_MIN_G(b, ibo, 0, src0, 0, src1, 0);
		break;
	case nir_intrinsic_image_atomic_imax:
	case nir_intrinsic_image_atomic_umax:
	case nir_intrinsic_bindless_image_atomic_imax:
	case nir_intrinsic_bindless_image_atomic_umax:
		atomic = ir3_ATOMIC_MAX_G(b, ibo, 0, src0, 0, src1, 0);
		break;
	case nir_intrinsic_image_atomic_and:
	case nir_intrinsic_bindless_image_atomic_and:
		atomic = ir3_ATOMIC_AND_G(b, ibo, 0, src0, 0, src1, 0);
		break;
	case nir_intrinsic_image_atomic_or:
	case nir_intrinsic_bindless_image_atomic_or:
		atomic = ir3_ATOMIC_OR_G(b, ibo, 0, src0, 0, src1, 0);
		break;
	case nir_intrinsic_image_atomic_xor:
	case nir_intrinsic_bindless_image_atomic_xor:
		atomic = ir3_ATOMIC_XOR_G(b, ibo, 0, src0, 0, src1, 0);
		break;
	case nir_intrinsic_image_atomic_exchange:
	case nir_intrinsic_bindless_image_atomic_exchange:
		atomic = ir3_ATOMIC_XCHG_G(b, ibo, 0, src0, 0, src1, 0);
		break;
	case nir_intrinsic_image_atomic_comp_swap:
	case nir_intrinsic_bindless_image_atomic_comp_swap:
		atomic = ir3_ATOMIC_CMPXCHG_G(b, ibo, 0, src0, 0, src1, 0);
		break;
	default:
		unreachable("boo");
	}

	atomic->cat6.iim_val = 1;
	atomic->cat6.d = ncoords;
	atomic->cat6.type = ir3_get_type_for_image_intrinsic(intr);
	atomic->cat6.typed = true;
	atomic->barrier_class = IR3_BARRIER_IMAGE_W;
	atomic->barrier_conflict = IR3_BARRIER_IMAGE_R | IR3_BARRIER_IMAGE_W;
	ir3_handle_bindless_cat6(atomic, intr->src[0]);

	/* even if nothing consume the result, we can't DCE the instruction: */
	array_insert(b, b->keeps, atomic);

	return atomic;
}

static void
emit_intrinsic_image_size(struct ir3_context *ctx, nir_intrinsic_instr *intr,
		struct ir3_instruction **dst)
{
	struct ir3_block *b = ctx->block;
	struct ir3_instruction *ibo = ir3_image_to_ibo(ctx, intr->src[0]);
	struct ir3_instruction *resinfo = ir3_RESINFO(b, ibo, 0);
	resinfo->cat6.iim_val = 1;
	resinfo->cat6.d = intr->num_components;
	resinfo->cat6.type = TYPE_U32;
	resinfo->cat6.typed = false;
	/* resinfo has no writemask and always writes out 3 components: */
	compile_assert(ctx, intr->num_components <= 3);
	resinfo->regs[0]->wrmask = MASK(3);
	ir3_handle_bindless_cat6(resinfo, intr->src[0]);

	ir3_split_dest(b, dst, resinfo, 0, intr->num_components);
}

const struct ir3_context_funcs ir3_a6xx_funcs = {
		.emit_intrinsic_load_ssbo = emit_intrinsic_load_ssbo,
		.emit_intrinsic_store_ssbo = emit_intrinsic_store_ssbo,
		.emit_intrinsic_atomic_ssbo = emit_intrinsic_atomic_ssbo,
		.emit_intrinsic_load_image = emit_intrinsic_load_image,
		.emit_intrinsic_store_image = emit_intrinsic_store_image,
		.emit_intrinsic_atomic_image = emit_intrinsic_atomic_image,
		.emit_intrinsic_image_size = emit_intrinsic_image_size,
};

/*
 * Special pass to run after instruction scheduling to insert an
 * extra mov from src1.x to dst.  This way the other compiler passes
 * can ignore this quirk of the new instruction encoding.
 *
 * This should run after RA.
 */

static struct ir3_instruction *
get_atomic_dest_mov(struct ir3_instruction *atomic)
{
	struct ir3_instruction *mov;

	/* if we've already created the mov-out, then re-use it: */
	if (atomic->data)
		return atomic->data;

	/* We are already out of SSA here, so we can't use the nice builders: */
	mov = ir3_instr_create(atomic->block, OPC_MOV);
	ir3_reg_create(mov, 0, 0);    /* dst */
	ir3_reg_create(mov, 0, 0);    /* src */

	mov->cat1.src_type = TYPE_U32;
	mov->cat1.dst_type = TYPE_U32;

	/* extract back out the 'dummy' which serves as stand-in for dest: */
	struct ir3_instruction *src = atomic->regs[3]->instr;
	debug_assert(src->opc == OPC_META_COLLECT);

	*mov->regs[0] = *atomic->regs[0];
	*mov->regs[1] = *src->regs[1]->instr->regs[0];

	mov->flags |= IR3_INSTR_SY;

	/* it will have already been appended to the end of the block, which
	 * isn't where we want it, so fix-up the location:
	 */
	ir3_instr_move_after(mov, atomic);

	return atomic->data = mov;
}

bool
ir3_a6xx_fixup_atomic_dests(struct ir3 *ir, struct ir3_shader_variant *so)
{
	bool progress = false;

	if (ir3_shader_nibo(so) == 0)
		return false;

	foreach_block (block, &ir->block_list) {
		foreach_instr (instr, &block->instr_list) {
			instr->data = NULL;
		}
	}

	foreach_block (block, &ir->block_list) {
		foreach_instr_safe (instr, &block->instr_list) {
			foreach_src (reg, instr) {
				struct ir3_instruction *src = reg->instr;

				if (!src)
					continue;

				if (is_atomic(src->opc) && (src->flags & IR3_INSTR_G)) {
					reg->instr = get_atomic_dest_mov(src);
					progress = true;
				}
			}
		}
	}

	/* we also need to fixup shader outputs: */
	foreach_output_n (out, n, ir) {
		if (is_atomic(out->opc) && (out->flags & IR3_INSTR_G)) {
			ir->outputs[n] = get_atomic_dest_mov(out);
			progress = true;
		}
	}

	return progress;
}
