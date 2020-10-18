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

#define GPU 400

#include "ir3_context.h"
#include "ir3_image.h"

/*
 * Handlers for instructions changed/added in a4xx:
 */


/* src[] = { buffer_index, offset }. No const_index */
static void
emit_intrinsic_load_ssbo(struct ir3_context *ctx, nir_intrinsic_instr *intr,
		struct ir3_instruction **dst)
{
	struct ir3_block *b = ctx->block;
	struct ir3_instruction *ldgb, *src0, *src1, *byte_offset, *offset;

	struct ir3_instruction *ssbo = ir3_ssbo_to_ibo(ctx, intr->src[0]);

	byte_offset = ir3_get_src(ctx, &intr->src[1])[0];
	offset = ir3_get_src(ctx, &intr->src[2])[0];

	/* src0 is uvec2(offset*4, 0), src1 is offset.. nir already *= 4: */
	src0 = ir3_create_collect(ctx, (struct ir3_instruction*[]){
		byte_offset,
		create_immed(b, 0),
	}, 2);
	src1 = offset;

	ldgb = ir3_LDGB(b, ssbo, 0,
			src0, 0, src1, 0);
	ldgb->regs[0]->wrmask = MASK(intr->num_components);
	ldgb->cat6.iim_val = intr->num_components;
	ldgb->cat6.d = 4;
	ldgb->cat6.type = TYPE_U32;
	ldgb->barrier_class = IR3_BARRIER_BUFFER_R;
	ldgb->barrier_conflict = IR3_BARRIER_BUFFER_W;

	ir3_split_dest(b, dst, ldgb, 0, intr->num_components);
}

/* src[] = { value, block_index, offset }. const_index[] = { write_mask } */
static void
emit_intrinsic_store_ssbo(struct ir3_context *ctx, nir_intrinsic_instr *intr)
{
	struct ir3_block *b = ctx->block;
	struct ir3_instruction *stgb, *src0, *src1, *src2, *byte_offset, *offset;
	unsigned wrmask = nir_intrinsic_write_mask(intr);
	unsigned ncomp = ffs(~wrmask) - 1;

	assert(wrmask == BITFIELD_MASK(intr->num_components));

	struct ir3_instruction *ssbo = ir3_ssbo_to_ibo(ctx, intr->src[1]);

	byte_offset = ir3_get_src(ctx, &intr->src[2])[0];
	offset = ir3_get_src(ctx, &intr->src[3])[0];

	/* src0 is value, src1 is offset, src2 is uvec2(offset*4, 0)..
	 * nir already *= 4:
	 */
	src0 = ir3_create_collect(ctx, ir3_get_src(ctx, &intr->src[0]), ncomp);
	src1 = offset;
	src2 = ir3_create_collect(ctx, (struct ir3_instruction*[]){
		byte_offset,
		create_immed(b, 0),
	}, 2);

	stgb = ir3_STGB(b, ssbo, 0, src0, 0, src1, 0, src2, 0);
	stgb->cat6.iim_val = ncomp;
	stgb->cat6.d = 4;
	stgb->cat6.type = TYPE_U32;
	stgb->barrier_class = IR3_BARRIER_BUFFER_W;
	stgb->barrier_conflict = IR3_BARRIER_BUFFER_R | IR3_BARRIER_BUFFER_W;

	array_insert(b, b->keeps, stgb);
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
	struct ir3_instruction *atomic, *ssbo, *src0, *src1, *src2, *byte_offset,
		*offset;
	type_t type = TYPE_U32;

	ssbo = ir3_ssbo_to_ibo(ctx, intr->src[0]);

	byte_offset = ir3_get_src(ctx, &intr->src[1])[0];
	offset = ir3_get_src(ctx, &intr->src[3])[0];

	/* src0 is data (or uvec2(data, compare))
	 * src1 is offset
	 * src2 is uvec2(offset*4, 0) (appears to be 64b byte offset)
	 *
	 * Note that nir already multiplies the offset by four
	 */
	src0 = ir3_get_src(ctx, &intr->src[2])[0];
	src1 = offset;
	src2 = ir3_create_collect(ctx, (struct ir3_instruction*[]){
		byte_offset,
		create_immed(b, 0),
	}, 2);

	switch (intr->intrinsic) {
	case nir_intrinsic_ssbo_atomic_add_ir3:
		atomic = ir3_ATOMIC_ADD_G(b, ssbo, 0, src0, 0, src1, 0, src2, 0);
		break;
	case nir_intrinsic_ssbo_atomic_imin_ir3:
		atomic = ir3_ATOMIC_MIN_G(b, ssbo, 0, src0, 0, src1, 0, src2, 0);
		type = TYPE_S32;
		break;
	case nir_intrinsic_ssbo_atomic_umin_ir3:
		atomic = ir3_ATOMIC_MIN_G(b, ssbo, 0, src0, 0, src1, 0, src2, 0);
		break;
	case nir_intrinsic_ssbo_atomic_imax_ir3:
		atomic = ir3_ATOMIC_MAX_G(b, ssbo, 0, src0, 0, src1, 0, src2, 0);
		type = TYPE_S32;
		break;
	case nir_intrinsic_ssbo_atomic_umax_ir3:
		atomic = ir3_ATOMIC_MAX_G(b, ssbo, 0, src0, 0, src1, 0, src2, 0);
		break;
	case nir_intrinsic_ssbo_atomic_and_ir3:
		atomic = ir3_ATOMIC_AND_G(b, ssbo, 0, src0, 0, src1, 0, src2, 0);
		break;
	case nir_intrinsic_ssbo_atomic_or_ir3:
		atomic = ir3_ATOMIC_OR_G(b, ssbo, 0, src0, 0, src1, 0, src2, 0);
		break;
	case nir_intrinsic_ssbo_atomic_xor_ir3:
		atomic = ir3_ATOMIC_XOR_G(b, ssbo, 0, src0, 0, src1, 0, src2, 0);
		break;
	case nir_intrinsic_ssbo_atomic_exchange_ir3:
		atomic = ir3_ATOMIC_XCHG_G(b, ssbo, 0, src0, 0, src1, 0, src2, 0);
		break;
	case nir_intrinsic_ssbo_atomic_comp_swap_ir3:
		/* for cmpxchg, src0 is [ui]vec2(data, compare): */
		src0 = ir3_create_collect(ctx, (struct ir3_instruction*[]){
			ir3_get_src(ctx, &intr->src[3])[0],
			src0,
		}, 2);
		src1 = ir3_get_src(ctx, &intr->src[4])[0];
		atomic = ir3_ATOMIC_CMPXCHG_G(b, ssbo, 0, src0, 0, src1, 0, src2, 0);
		break;
	default:
		unreachable("boo");
	}

	atomic->cat6.iim_val = 1;
	atomic->cat6.d = 4;
	atomic->cat6.type = type;
	atomic->barrier_class = IR3_BARRIER_BUFFER_W;
	atomic->barrier_conflict = IR3_BARRIER_BUFFER_R | IR3_BARRIER_BUFFER_W;

	/* even if nothing consume the result, we can't DCE the instruction: */
	array_insert(b, b->keeps, atomic);

	return atomic;
}

static struct ir3_instruction *
get_image_offset(struct ir3_context *ctx, const nir_intrinsic_instr *instr,
		struct ir3_instruction * const *coords, bool byteoff)
{
	struct ir3_block *b = ctx->block;
	struct ir3_instruction *offset;
	unsigned index = nir_src_as_uint(instr->src[0]);
	unsigned ncoords = ir3_get_image_coords(instr, NULL);

	/* to calculate the byte offset (yes, uggg) we need (up to) three
	 * const values to know the bytes per pixel, and y and z stride:
	 */
	const struct ir3_const_state *const_state = ir3_const_state(ctx->so);
	unsigned cb = regid(const_state->offsets.image_dims, 0) +
		const_state->image_dims.off[index];

	debug_assert(const_state->image_dims.mask & (1 << index));

	/* offset = coords.x * bytes_per_pixel: */
	offset = ir3_MUL_S24(b, coords[0], 0, create_uniform(b, cb + 0), 0);
	if (ncoords > 1) {
		/* offset += coords.y * y_pitch: */
		offset = ir3_MAD_S24(b, create_uniform(b, cb + 1), 0,
				coords[1], 0, offset, 0);
	}
	if (ncoords > 2) {
		/* offset += coords.z * z_pitch: */
		offset = ir3_MAD_S24(b, create_uniform(b, cb + 2), 0,
				coords[2], 0, offset, 0);
	}

	if (!byteoff) {
		/* Some cases, like atomics, seem to use dword offset instead
		 * of byte offsets.. blob just puts an extra shr.b in there
		 * in those cases:
		 */
		offset = ir3_SHR_B(b, offset, 0, create_immed(b, 2), 0);
	}

	return ir3_create_collect(ctx, (struct ir3_instruction*[]){
		offset,
		create_immed(b, 0),
	}, 2);
}

/* src[] = { index, coord, sample_index, value }. const_index[] = {} */
static void
emit_intrinsic_store_image(struct ir3_context *ctx, nir_intrinsic_instr *intr)
{
	struct ir3_block *b = ctx->block;
	struct ir3_instruction *stib, *offset;
	struct ir3_instruction * const *value = ir3_get_src(ctx, &intr->src[3]);
	struct ir3_instruction * const *coords = ir3_get_src(ctx, &intr->src[1]);
	struct ir3_instruction * ibo = ir3_image_to_ibo(ctx, intr->src[0]);
	unsigned ncoords = ir3_get_image_coords(intr, NULL);
	unsigned ncomp = ir3_get_num_components_for_image_format(nir_intrinsic_format(intr));

	/* src0 is value
	 * src1 is coords
	 * src2 is 64b byte offset
	 */

	offset = get_image_offset(ctx, intr, coords, true);

	/* NOTE: stib seems to take byte offset, but stgb.typed can be used
	 * too and takes a dword offset.. not quite sure yet why blob uses
	 * one over the other in various cases.
	 */

	stib = ir3_STIB(b, ibo, 0,
			ir3_create_collect(ctx, value, ncomp), 0,
			ir3_create_collect(ctx, coords, ncoords), 0,
			offset, 0);
	stib->cat6.iim_val = ncomp;
	stib->cat6.d = ncoords;
	stib->cat6.type = ir3_get_type_for_image_intrinsic(intr);
	stib->cat6.typed = true;
	stib->barrier_class = IR3_BARRIER_IMAGE_W;
	stib->barrier_conflict = IR3_BARRIER_IMAGE_R | IR3_BARRIER_IMAGE_W;

	array_insert(b, b->keeps, stib);
}

/* src[] = { deref, coord, sample_index, value, compare }. const_index[] = {} */
static struct ir3_instruction *
emit_intrinsic_atomic_image(struct ir3_context *ctx, nir_intrinsic_instr *intr)
{
	struct ir3_block *b = ctx->block;
	struct ir3_instruction *atomic, *src0, *src1, *src2;
	struct ir3_instruction * const *coords = ir3_get_src(ctx, &intr->src[1]);
	struct ir3_instruction * image = ir3_image_to_ibo(ctx, intr->src[0]);
	unsigned ncoords = ir3_get_image_coords(intr, NULL);

	/* src0 is value (or uvec2(value, compare))
	 * src1 is coords
	 * src2 is 64b byte offset
	 */
	src0 = ir3_get_src(ctx, &intr->src[3])[0];
	src1 = ir3_create_collect(ctx, coords, ncoords);
	src2 = get_image_offset(ctx, intr, coords, false);

	switch (intr->intrinsic) {
	case nir_intrinsic_image_atomic_add:
		atomic = ir3_ATOMIC_ADD_G(b, image, 0, src0, 0, src1, 0, src2, 0);
		break;
	case nir_intrinsic_image_atomic_imin:
	case nir_intrinsic_image_atomic_umin:
		atomic = ir3_ATOMIC_MIN_G(b, image, 0, src0, 0, src1, 0, src2, 0);
		break;
	case nir_intrinsic_image_atomic_imax:
	case nir_intrinsic_image_atomic_umax:
		atomic = ir3_ATOMIC_MAX_G(b, image, 0, src0, 0, src1, 0, src2, 0);
		break;
	case nir_intrinsic_image_atomic_and:
		atomic = ir3_ATOMIC_AND_G(b, image, 0, src0, 0, src1, 0, src2, 0);
		break;
	case nir_intrinsic_image_atomic_or:
		atomic = ir3_ATOMIC_OR_G(b, image, 0, src0, 0, src1, 0, src2, 0);
		break;
	case nir_intrinsic_image_atomic_xor:
		atomic = ir3_ATOMIC_XOR_G(b, image, 0, src0, 0, src1, 0, src2, 0);
		break;
	case nir_intrinsic_image_atomic_exchange:
		atomic = ir3_ATOMIC_XCHG_G(b, image, 0, src0, 0, src1, 0, src2, 0);
		break;
	case nir_intrinsic_image_atomic_comp_swap:
		/* for cmpxchg, src0 is [ui]vec2(data, compare): */
		src0 = ir3_create_collect(ctx, (struct ir3_instruction*[]){
			ir3_get_src(ctx, &intr->src[4])[0],
			src0,
		}, 2);
		atomic = ir3_ATOMIC_CMPXCHG_G(b, image, 0, src0, 0, src1, 0, src2, 0);
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

	/* even if nothing consume the result, we can't DCE the instruction: */
	array_insert(b, b->keeps, atomic);

	return atomic;
}

const struct ir3_context_funcs ir3_a4xx_funcs = {
		.emit_intrinsic_load_ssbo = emit_intrinsic_load_ssbo,
		.emit_intrinsic_store_ssbo = emit_intrinsic_store_ssbo,
		.emit_intrinsic_atomic_ssbo = emit_intrinsic_atomic_ssbo,
		.emit_intrinsic_store_image = emit_intrinsic_store_image,
		.emit_intrinsic_atomic_image = emit_intrinsic_atomic_image,
		.emit_intrinsic_image_size = emit_intrinsic_image_size_tex,
};
