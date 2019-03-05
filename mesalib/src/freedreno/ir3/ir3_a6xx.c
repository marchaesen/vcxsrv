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


static struct ir3_instruction *
ssbo_offset(struct ir3_block *b, struct ir3_instruction *byte_offset)
{
	/* TODO hardware wants offset in terms of elements, not bytes.  Which
	 * is kinda nice but opposite of what nir does.  It would be nice if
	 * we had a way to request the units of the offset to avoid the extra
	 * shift instructions..
	 */
	return ir3_SHR_B(b, byte_offset, 0, create_immed(b, 2), 0);
}

/* src[] = { buffer_index, offset }. No const_index */
static void
emit_intrinsic_load_ssbo(struct ir3_context *ctx, nir_intrinsic_instr *intr,
		struct ir3_instruction **dst)
{
	struct ir3_block *b = ctx->block;
	struct ir3_instruction *offset;
	struct ir3_instruction *ldib;
	nir_const_value *buffer_index;

	/* can this be non-const buffer_index?  how do we handle that? */
	buffer_index = nir_src_as_const_value(intr->src[0]);
	compile_assert(ctx, buffer_index);

	int ibo_idx = ir3_ssbo_to_ibo(&ctx->so->image_mapping, buffer_index->u32[0]);

	offset = ssbo_offset(b, ir3_get_src(ctx, &intr->src[1])[0]);

	ldib = ir3_LDIB(b, create_immed(b, ibo_idx), 0, offset, 0);
	ldib->regs[0]->wrmask = MASK(intr->num_components);
	ldib->cat6.iim_val = intr->num_components;
	ldib->cat6.d = 1;
	ldib->cat6.type = TYPE_U32;
	ldib->barrier_class = IR3_BARRIER_BUFFER_R;
	ldib->barrier_conflict = IR3_BARRIER_BUFFER_W;

	ir3_split_dest(b, dst, ldib, 0, intr->num_components);
}

/* src[] = { value, block_index, offset }. const_index[] = { write_mask } */
static void
emit_intrinsic_store_ssbo(struct ir3_context *ctx, nir_intrinsic_instr *intr)
{
	struct ir3_block *b = ctx->block;
	struct ir3_instruction *stib, *val, *offset;
	nir_const_value *buffer_index;
	/* TODO handle wrmask properly, see _store_shared().. but I think
	 * it is more a PITA than that, since blob ends up loading the
	 * masked components and writing them back out.
	 */
	unsigned wrmask = intr->const_index[0];
	unsigned ncomp = ffs(~wrmask) - 1;

	/* can this be non-const buffer_index?  how do we handle that? */
	buffer_index = nir_src_as_const_value(intr->src[1]);
	compile_assert(ctx, buffer_index);

	int ibo_idx = ir3_ssbo_to_ibo(&ctx->so->image_mapping,  buffer_index->u32[0]);

	/* src0 is offset, src1 is value:
	 */
	val = ir3_create_collect(ctx, ir3_get_src(ctx, &intr->src[0]), ncomp);
	offset = ssbo_offset(b, ir3_get_src(ctx, &intr->src[2])[0]);

	stib = ir3_STIB(b, create_immed(b, ibo_idx), 0, offset, 0, val, 0);
	stib->cat6.iim_val = ncomp;
	stib->cat6.d = 1;
	stib->cat6.type = TYPE_U32;
	stib->barrier_class = IR3_BARRIER_BUFFER_W;
	stib->barrier_conflict = IR3_BARRIER_BUFFER_R | IR3_BARRIER_BUFFER_W;

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
	struct ir3_instruction *atomic, *ibo, *src0, *src1, *offset, *data, *dummy;
	nir_const_value *buffer_index;
	type_t type = TYPE_U32;

	/* can this be non-const buffer_index?  how do we handle that? */
	buffer_index = nir_src_as_const_value(intr->src[0]);
	compile_assert(ctx, buffer_index);

	int ibo_idx = ir3_ssbo_to_ibo(&ctx->so->image_mapping,  buffer_index->u32[0]);
	ibo = create_immed(b, ibo_idx);

	offset = ir3_get_src(ctx, &intr->src[1])[0];
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
	src0 = ssbo_offset(b, offset);

	if (intr->intrinsic == nir_intrinsic_ssbo_atomic_comp_swap) {
		struct ir3_instruction *compare = ir3_get_src(ctx, &intr->src[3])[0];
		src1 = ir3_create_collect(ctx, (struct ir3_instruction*[]){
			dummy, compare, data
		}, 3);
	} else {
		src1 = ir3_create_collect(ctx, (struct ir3_instruction*[]){
			dummy, data
		}, 2);
	}

	switch (intr->intrinsic) {
	case nir_intrinsic_ssbo_atomic_add:
		atomic = ir3_ATOMIC_ADD_G(b, ibo, 0, src0, 0, src1, 0);
		break;
	case nir_intrinsic_ssbo_atomic_imin:
		atomic = ir3_ATOMIC_MIN_G(b, ibo, 0, src0, 0, src1, 0);
		type = TYPE_S32;
		break;
	case nir_intrinsic_ssbo_atomic_umin:
		atomic = ir3_ATOMIC_MIN_G(b, ibo, 0, src0, 0, src1, 0);
		break;
	case nir_intrinsic_ssbo_atomic_imax:
		atomic = ir3_ATOMIC_MAX_G(b, ibo, 0, src0, 0, src1, 0);
		type = TYPE_S32;
		break;
	case nir_intrinsic_ssbo_atomic_umax:
		atomic = ir3_ATOMIC_MAX_G(b, ibo, 0, src0, 0, src1, 0);
		break;
	case nir_intrinsic_ssbo_atomic_and:
		atomic = ir3_ATOMIC_AND_G(b, ibo, 0, src0, 0, src1, 0);
		break;
	case nir_intrinsic_ssbo_atomic_or:
		atomic = ir3_ATOMIC_OR_G(b, ibo, 0, src0, 0, src1, 0);
		break;
	case nir_intrinsic_ssbo_atomic_xor:
		atomic = ir3_ATOMIC_XOR_G(b, ibo, 0, src0, 0, src1, 0);
		break;
	case nir_intrinsic_ssbo_atomic_exchange:
		atomic = ir3_ATOMIC_XCHG_G(b, ibo, 0, src0, 0, src1, 0);
		break;
	case nir_intrinsic_ssbo_atomic_comp_swap:
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

	/* even if nothing consume the result, we can't DCE the instruction: */
	array_insert(b, b->keeps, atomic);

	return atomic;
}

/* src[] = { deref, coord, sample_index, value }. const_index[] = {} */
static void
emit_intrinsic_store_image(struct ir3_context *ctx, nir_intrinsic_instr *intr)
{
	struct ir3_block *b = ctx->block;
	const nir_variable *var = nir_intrinsic_get_var(intr, 0);
	struct ir3_instruction *stib;
	struct ir3_instruction * const *value = ir3_get_src(ctx, &intr->src[3]);
	struct ir3_instruction * const *coords = ir3_get_src(ctx, &intr->src[1]);
	unsigned ncoords = ir3_get_image_coords(var, NULL);
	unsigned slot = ir3_get_image_slot(nir_src_as_deref(intr->src[0]));
	unsigned ibo_idx = ir3_image_to_ibo(&ctx->so->image_mapping, slot);
	unsigned ncomp = ir3_get_num_components_for_glformat(var->data.image.format);

	/* src0 is offset, src1 is value:
	 */
	stib = ir3_STIB(b, create_immed(b, ibo_idx), 0,
			ir3_create_collect(ctx, coords, ncoords), 0,
			ir3_create_collect(ctx, value, ncomp), 0);
	stib->cat6.iim_val = ncomp;
	stib->cat6.d = ncoords;
	stib->cat6.type = ir3_get_image_type(var);
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
	const nir_variable *var = nir_intrinsic_get_var(intr, 0);
	struct ir3_instruction *atomic, *ibo, *src0, *src1, *dummy;
	struct ir3_instruction * const *coords = ir3_get_src(ctx, &intr->src[1]);
	struct ir3_instruction *value = ir3_get_src(ctx, &intr->src[3])[0];
	unsigned ncoords = ir3_get_image_coords(var, NULL);
	unsigned slot = ir3_get_image_slot(nir_src_as_deref(intr->src[0]));
	unsigned ibo_idx = ir3_image_to_ibo(&ctx->so->image_mapping, slot);

	ibo = create_immed(b, ibo_idx);

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

	if (intr->intrinsic == nir_intrinsic_image_deref_atomic_comp_swap) {
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
	case nir_intrinsic_image_deref_atomic_add:
		atomic = ir3_ATOMIC_ADD_G(b, ibo, 0, src0, 0, src1, 0);
		break;
	case nir_intrinsic_image_deref_atomic_min:
		atomic = ir3_ATOMIC_MIN_G(b, ibo, 0, src0, 0, src1, 0);
		break;
	case nir_intrinsic_image_deref_atomic_max:
		atomic = ir3_ATOMIC_MAX_G(b, ibo, 0, src0, 0, src1, 0);
		break;
	case nir_intrinsic_image_deref_atomic_and:
		atomic = ir3_ATOMIC_AND_G(b, ibo, 0, src0, 0, src1, 0);
		break;
	case nir_intrinsic_image_deref_atomic_or:
		atomic = ir3_ATOMIC_OR_G(b, ibo, 0, src0, 0, src1, 0);
		break;
	case nir_intrinsic_image_deref_atomic_xor:
		atomic = ir3_ATOMIC_XOR_G(b, ibo, 0, src0, 0, src1, 0);
		break;
	case nir_intrinsic_image_deref_atomic_exchange:
		atomic = ir3_ATOMIC_XCHG_G(b, ibo, 0, src0, 0, src1, 0);
		break;
	case nir_intrinsic_image_deref_atomic_comp_swap:
		atomic = ir3_ATOMIC_CMPXCHG_G(b, ibo, 0, src0, 0, src1, 0);
		break;
	default:
		unreachable("boo");
	}

	atomic->cat6.iim_val = 1;
	atomic->cat6.d = ncoords;
	atomic->cat6.type = ir3_get_image_type(var);
	atomic->cat6.typed = true;
	atomic->barrier_class = IR3_BARRIER_IMAGE_W;
	atomic->barrier_conflict = IR3_BARRIER_IMAGE_R | IR3_BARRIER_IMAGE_W;

	/* even if nothing consume the result, we can't DCE the instruction: */
	array_insert(b, b->keeps, atomic);

	return atomic;
}

const struct ir3_context_funcs ir3_a6xx_funcs = {
		.emit_intrinsic_load_ssbo = emit_intrinsic_load_ssbo,
		.emit_intrinsic_store_ssbo = emit_intrinsic_store_ssbo,
		.emit_intrinsic_atomic_ssbo = emit_intrinsic_atomic_ssbo,
		.emit_intrinsic_store_image = emit_intrinsic_store_image,
		.emit_intrinsic_atomic_image = emit_intrinsic_atomic_image,
};

/*
 * Special pass to run after instruction scheduling to insert an
 * extra mov from src1.x to dst.  This way the other compiler passes
 * can ignore this quirk of the new instruction encoding.
 *
 * This might cause extra complication in the future when we support
 * spilling, as I think we'd want to re-run the scheduling pass.  One
 * possible alternative might be to do this in the RA pass after
 * ra_allocate() but before destroying the SSA links.  (Ie. we do
 * want to know if anything consumes the result of the atomic instr,
 * if there is no consumer then inserting the extra mov is pointless.
 */

static struct ir3_instruction *
get_atomic_dest_mov(struct ir3_instruction *atomic)
{
	/* if we've already created the mov-out, then re-use it: */
	if (atomic->data)
		return atomic->data;

	/* extract back out the 'dummy' which serves as stand-in for dest: */
	struct ir3_instruction *src = ssa(atomic->regs[3]);
	debug_assert(src->opc == OPC_META_FI);
	struct ir3_instruction *dummy = ssa(src->regs[1]);

	struct ir3_instruction *mov = ir3_MOV(atomic->block, dummy, TYPE_U32);

	mov->flags |= IR3_INSTR_SY;

	if (atomic->regs[0]->flags & IR3_REG_ARRAY) {
		mov->regs[0]->flags |= IR3_REG_ARRAY;
		mov->regs[0]->array = atomic->regs[0]->array;
	}

	/* it will have already been appended to the end of the block, which
	 * isn't where we want it, so fix-up the location:
	 */
	list_delinit(&mov->node);
	list_add(&mov->node, &atomic->node);

	/* And because this is after instruction scheduling, we don't really
	 * have a good way to know if extra delay slots are needed.  For
	 * example, if the result is consumed by an stib (storeImage()) there
	 * would be no extra delay slots in place already, but 5 are needed.
	 * Just plan for the worst and hope nobody looks at the resulting
	 * code that is generated :-(
	 */
	struct ir3_instruction *nop = ir3_NOP(atomic->block);
	nop->repeat = 5;

	list_delinit(&nop->node);
	list_add(&nop->node, &mov->node);

	return atomic->data = mov;
}

void
ir3_a6xx_fixup_atomic_dests(struct ir3 *ir, struct ir3_shader_variant *so)
{
	if (so->image_mapping.num_ibo == 0)
		return;

	list_for_each_entry (struct ir3_block, block, &ir->block_list, node) {
		list_for_each_entry (struct ir3_instruction, instr, &block->instr_list, node) {
			instr->data = NULL;
		}
	}

	list_for_each_entry (struct ir3_block, block, &ir->block_list, node) {
		list_for_each_entry_safe (struct ir3_instruction, instr, &block->instr_list, node) {
			struct ir3_register *reg;

			foreach_src(reg, instr) {
				struct ir3_instruction *src = ssa(reg);

				if (!src)
					continue;

				if (is_atomic(src->opc) && (src->flags & IR3_INSTR_G))
					reg->instr = get_atomic_dest_mov(src);
			}
		}

		/* we also need to fixup shader outputs: */
		for (unsigned i = 0; i < ir->noutputs; i++) {
			if (!ir->outputs[i])
				continue;
			if (is_atomic(ir->outputs[i]->opc) && (ir->outputs[i]->flags & IR3_INSTR_G))
				ir->outputs[i] = get_atomic_dest_mov(ir->outputs[i]);
		}
	}

}
