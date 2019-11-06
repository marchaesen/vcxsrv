/*
 * Copyright © 2019 Google, Inc.
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
 */

#include "ir3_nir.h"
#include "ir3_compiler.h"
#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"
#include "mesa/main/macros.h"

static inline struct ir3_ubo_range
get_ubo_load_range(nir_intrinsic_instr *instr)
{
	struct ir3_ubo_range r;

	const int offset = nir_src_as_uint(instr->src[1]);
	const int bytes = nir_intrinsic_dest_components(instr) * 4;

	r.start = ROUND_DOWN_TO(offset, 16 * 4);
	r.end = ALIGN(offset + bytes, 16 * 4);

	return r;
}

static void
gather_ubo_ranges(nir_shader *nir, nir_intrinsic_instr *instr,
				  struct ir3_ubo_analysis_state *state)
{
	if (!nir_src_is_const(instr->src[0]))
		return;

	if (!nir_src_is_const(instr->src[1])) {
		if (nir_src_as_uint(instr->src[0]) == 0) {
			/* If this is an indirect on UBO 0, we'll still lower it back to
			 * load_uniform.  Set the range to cover all of UBO 0.
			 */
			state->range[0].end = align(nir->num_uniforms * 16, 16 * 4);
		}

		return;
	}

	const struct ir3_ubo_range r = get_ubo_load_range(instr);
	const uint32_t block = nir_src_as_uint(instr->src[0]);

	/* if UBO lowering is disabled, we still want to lower block 0
	 * (which is normal uniforms):
	 */
	if ((block > 0) && (ir3_shader_debug & IR3_DBG_NOUBOOPT))
		return;

	if (r.start < state->range[block].start)
		state->range[block].start = r.start;
	if (state->range[block].end < r.end)
		state->range[block].end = r.end;
}

/* For indirect offset, it is common to see a pattern of multiple
 * loads with the same base, but different constant offset, ie:
 *
 *    vec1 32 ssa_33 = iadd ssa_base, const_offset
 *    vec4 32 ssa_34 = intrinsic load_uniform (ssa_33) (base=N, 0, 0)
 *
 * Detect this, and peel out the const_offset part, to end up with:
 *
 *    vec4 32 ssa_34 = intrinsic load_uniform (ssa_base) (base=N+const_offset, 0, 0)
 *
 * Or similarly:
 *
 *    vec1 32 ssa_33 = imad24_ir3 a, b, const_offset
 *    vec4 32 ssa_34 = intrinsic load_uniform (ssa_33) (base=N, 0, 0)
 *
 * Can be converted to:
 *
 *    vec1 32 ssa_base = imul24 a, b
 *    vec4 32 ssa_34 = intrinsic load_uniform (ssa_base) (base=N+const_offset, 0, 0)
 *
 * This gives the other opt passes something much easier to work
 * with (ie. not requiring value range tracking)
 */
static void
handle_partial_const(nir_builder *b, nir_ssa_def **srcp, unsigned *offp)
{
	if ((*srcp)->parent_instr->type != nir_instr_type_alu)
		return;

	nir_alu_instr *alu = nir_instr_as_alu((*srcp)->parent_instr);

	if (alu->op == nir_op_imad24_ir3) {
		/* This case is slightly more complicated as we need to
		 * replace the imad24_ir3 with an imul24:
		 */
		if (!nir_src_is_const(alu->src[2].src))
			return;

		*offp += nir_src_as_uint(alu->src[2].src);
		*srcp = nir_imul24(b, nir_ssa_for_alu_src(b, alu, 0),
				nir_ssa_for_alu_src(b, alu, 1));

		return;
	}

	if (alu->op != nir_op_iadd)
		return;

	if (!(alu->src[0].src.is_ssa && alu->src[1].src.is_ssa))
		return;

	if (nir_src_is_const(alu->src[0].src)) {
		*offp += nir_src_as_uint(alu->src[0].src);
		*srcp = alu->src[1].src.ssa;
	} else if (nir_src_is_const(alu->src[1].src)) {
		*srcp = alu->src[0].src.ssa;
		*offp += nir_src_as_uint(alu->src[1].src);
	}
}

static void
lower_ubo_load_to_uniform(nir_intrinsic_instr *instr, nir_builder *b,
						  struct ir3_ubo_analysis_state *state)
{
	/* We don't lower dynamic block index UBO loads to load_uniform, but we
	 * could probably with some effort determine a block stride in number of
	 * registers.
	 */
	if (!nir_src_is_const(instr->src[0]))
		return;

	const uint32_t block = nir_src_as_uint(instr->src[0]);

	if (block > 0) {
		/* We don't lower dynamic array indexing either, but we definitely should.
		 * We don't have a good way of determining the range of the dynamic
		 * access, so for now just fall back to pulling.
		 */
		if (!nir_src_is_const(instr->src[1]))
			return;

		/* After gathering the UBO access ranges, we limit the total
		 * upload. Reject if we're now outside the range.
		 */
		const struct ir3_ubo_range r = get_ubo_load_range(instr);
		if (!(state->range[block].start <= r.start &&
			  r.end <= state->range[block].end))
			return;
	}

	b->cursor = nir_before_instr(&instr->instr);

	nir_ssa_def *ubo_offset = nir_ssa_for_src(b, instr->src[1], 1);
	unsigned const_offset = 0;

	handle_partial_const(b, &ubo_offset, &const_offset);

	/* UBO offset is in bytes, but uniform offset is in units of
	 * dwords, so we need to divide by 4 (right-shift by 2).  And
	 * also the same for the constant part of the offset:
	 */
	nir_ssa_def *new_offset = ir3_nir_try_propagate_bit_shift(b, ubo_offset, -2);
	nir_ssa_def *uniform_offset = NULL;
	if (new_offset) {
		uniform_offset = new_offset;
	} else {
		uniform_offset = nir_ushr(b, ubo_offset, nir_imm_int(b, 2));
	}

	debug_assert(!(const_offset & 0x3));
	const_offset >>= 2;

	const int range_offset =
		(state->range[block].offset - state->range[block].start) / 4;
	const_offset += range_offset;

	nir_intrinsic_instr *uniform =
		nir_intrinsic_instr_create(b->shader, nir_intrinsic_load_uniform);
	uniform->num_components = instr->num_components;
	uniform->src[0] = nir_src_for_ssa(uniform_offset);
	nir_intrinsic_set_base(uniform, const_offset);
	nir_ssa_dest_init(&uniform->instr, &uniform->dest,
					  uniform->num_components, instr->dest.ssa.bit_size,
					  instr->dest.ssa.name);
	nir_builder_instr_insert(b, &uniform->instr);
	nir_ssa_def_rewrite_uses(&instr->dest.ssa,
							 nir_src_for_ssa(&uniform->dest.ssa));

	nir_instr_remove(&instr->instr);

	state->lower_count++;
}

bool
ir3_nir_analyze_ubo_ranges(nir_shader *nir, struct ir3_shader *shader)
{
	struct ir3_ubo_analysis_state *state = &shader->ubo_state;

	memset(state, 0, sizeof(*state));

	nir_foreach_function(function, nir) {
		if (function->impl) {
			nir_foreach_block(block, function->impl) {
				nir_foreach_instr(instr, block) {
					if (instr->type == nir_instr_type_intrinsic &&
						nir_instr_as_intrinsic(instr)->intrinsic == nir_intrinsic_load_ubo)
						gather_ubo_ranges(nir, nir_instr_as_intrinsic(instr), state);
				}
			}
		}
	}

	/* For now, everything we upload is accessed statically and thus will be
	 * used by the shader. Once we can upload dynamically indexed data, we may
	 * upload sparsely accessed arrays, at which point we probably want to
	 * give priority to smaller UBOs, on the assumption that big UBOs will be
	 * accessed dynamically.  Alternatively, we can track statically and
	 * dynamically accessed ranges separately and upload static rangtes
	 * first.
	 */
	const uint32_t max_upload = 16 * 1024;
	uint32_t offset = 0;
	for (uint32_t i = 0; i < ARRAY_SIZE(state->range); i++) {
		uint32_t range_size = state->range[i].end - state->range[i].start;

		debug_assert(offset <= max_upload);
		state->range[i].offset = offset;
		if (offset + range_size > max_upload) {
			range_size = max_upload - offset;
			state->range[i].end = state->range[i].start + range_size;
		}
		offset += range_size;
	}
	state->size = offset;

	nir_foreach_function(function, nir) {
		if (function->impl) {
			nir_builder builder;
			nir_builder_init(&builder, function->impl);
			nir_foreach_block(block, function->impl) {
				nir_foreach_instr_safe(instr, block) {
					if (instr->type == nir_instr_type_intrinsic &&
						nir_instr_as_intrinsic(instr)->intrinsic == nir_intrinsic_load_ubo)
						lower_ubo_load_to_uniform(nir_instr_as_intrinsic(instr), &builder, state);
				}
			}

			nir_metadata_preserve(function->impl, nir_metadata_block_index |
								  nir_metadata_dominance);
		}
	}

	return state->lower_count > 0;
}
