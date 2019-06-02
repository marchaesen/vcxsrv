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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "ir3_nir.h"
#include "compiler/nir/nir_builder.h"

/**
 * This pass lowers load_barycentric_at_sample to load_sample_pos_from_id
 * plus load_barycentric_at_offset.
 *
 * It also lowers load_sample_pos to load_sample_pos_from_id, mostly because
 * that needs to happen at the same early stage (before wpos_ytransform)
 */

static nir_ssa_def *
load_sample_pos(nir_builder *b, nir_ssa_def *samp_id)
{
	nir_intrinsic_instr *load_sp =
			nir_intrinsic_instr_create(b->shader,
					nir_intrinsic_load_sample_pos_from_id);
	load_sp->num_components = 2;
	load_sp->src[0] = nir_src_for_ssa(samp_id);
	nir_ssa_dest_init(&load_sp->instr, &load_sp->dest, 2, 32, NULL);
	nir_builder_instr_insert(b, &load_sp->instr);

	return &load_sp->dest.ssa;
}

static void
lower_load_barycentric_at_sample(nir_builder *b, nir_intrinsic_instr *intr)
{
	nir_ssa_def *pos = load_sample_pos(b, intr->src[0].ssa);

	nir_intrinsic_instr *load_bary_at_offset =
			nir_intrinsic_instr_create(b->shader,
					nir_intrinsic_load_barycentric_at_offset);
	load_bary_at_offset->num_components = 2;
	load_bary_at_offset->src[0] = nir_src_for_ssa(pos);
	nir_ssa_dest_init(&load_bary_at_offset->instr,
			&load_bary_at_offset->dest, 2, 32, NULL);
	nir_builder_instr_insert(b, &load_bary_at_offset->instr);

	nir_ssa_def_rewrite_uses(&intr->dest.ssa,
			nir_src_for_ssa(&load_bary_at_offset->dest.ssa));
}

static void
lower_load_sample_pos(nir_builder *b, nir_intrinsic_instr *intr)
{
	nir_ssa_def *pos = load_sample_pos(b, nir_load_sample_id(b));

	/* Note that gl_SamplePosition is offset by +vec2(0.5, 0.5) vs the
	 * offset passed to interpolateAtOffset().   See
	 * dEQP-GLES31.functional.shaders.multisample_interpolation.interpolate_at_offset.at_sample_position.default_framebuffer
	 * for example.
	 */
	nir_ssa_def *half = nir_imm_float(b, 0.5);
	pos = nir_fadd(b, pos, nir_vec2(b, half, half));

	nir_ssa_def_rewrite_uses(&intr->dest.ssa, nir_src_for_ssa(pos));
}

bool
ir3_nir_lower_load_barycentric_at_sample(nir_shader *shader)
{
	bool progress = false;

	debug_assert(shader->info.stage == MESA_SHADER_FRAGMENT);

	nir_foreach_function (function, shader) {
		if (!function->impl)
			continue;

		nir_builder b;
		nir_builder_init(&b, function->impl);

		nir_foreach_block (block, function->impl) {
			nir_foreach_instr_safe(instr, block) {
				if (instr->type != nir_instr_type_intrinsic)
					continue;

				nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

				if (intr->intrinsic != nir_intrinsic_load_barycentric_at_sample &&
						intr->intrinsic != nir_intrinsic_load_sample_pos)
					continue;

				debug_assert(intr->dest.is_ssa);

				b.cursor = nir_before_instr(instr);

				if (intr->intrinsic == nir_intrinsic_load_sample_pos) {
					lower_load_sample_pos(&b, intr);
				} else {
					debug_assert(intr->src[0].is_ssa);
					lower_load_barycentric_at_sample(&b, intr);
				}

				progress = true;
			}
		}

		if (progress) {
			nir_metadata_preserve(function->impl,
				nir_metadata_block_index | nir_metadata_dominance);
		}
	}

	return progress;
}
