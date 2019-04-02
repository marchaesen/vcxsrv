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
#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"
#include "util/u_dynarray.h"
#include "mesa/main/macros.h"

static inline struct ir3_ubo_range
get_ubo_load_range(nir_intrinsic_instr *instr)
{
	struct ir3_ubo_range r;

	const int bytes = nir_intrinsic_dest_components(instr) *
		(nir_dest_bit_size(instr->dest) / 8);

	r.start = ROUND_DOWN_TO(nir_src_as_uint(instr->src[1]), 16 * 4);
	r.end = ALIGN(r.start + bytes, 16 * 4);

	return r;
}

static void
gather_ubo_ranges(nir_intrinsic_instr *instr,
				  struct ir3_ubo_analysis_state *state)
{
	if (!nir_src_is_const(instr->src[0]))
		return;

	if (!nir_src_is_const(instr->src[1]))
		return;

	const struct ir3_ubo_range r = get_ubo_load_range(instr);
	const uint32_t block = nir_src_as_uint(instr->src[0]);

	if (r.start < state->range[block].start)
		state->range[block].start = r.start;
	if (state->range[block].end < r.end)
		state->range[block].end = r.end;
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
	nir_ssa_def *new_offset = ir3_nir_try_propagate_bit_shift(b, ubo_offset, -2);
	if (new_offset)
		ubo_offset = new_offset;
	else
		ubo_offset = nir_ushr(b, ubo_offset, nir_imm_int(b, 2));

	const int range_offset =
		(state->range[block].offset - state->range[block].start) / 4;
	nir_ssa_def *uniform_offset =
		nir_iadd(b, ubo_offset, nir_imm_int(b, range_offset));

	nir_intrinsic_instr *uniform =
		nir_intrinsic_instr_create(b->shader, nir_intrinsic_load_uniform);
	uniform->num_components = instr->num_components;
	uniform->src[0] = nir_src_for_ssa(uniform_offset);
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
	state->range[0].end = nir->num_uniforms * 16;

	nir_foreach_function(function, nir) {
		if (function->impl) {
			nir_foreach_block(block, function->impl) {
				nir_foreach_instr(instr, block) {
					if (instr->type == nir_instr_type_intrinsic &&
						nir_instr_as_intrinsic(instr)->intrinsic == nir_intrinsic_load_ubo)
						gather_ubo_ranges(nir_instr_as_intrinsic(instr), state);
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
