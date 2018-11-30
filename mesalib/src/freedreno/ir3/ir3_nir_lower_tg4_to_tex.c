/*
 * Copyright © 2017 Ilia Mirkin
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

/* A4XX has a broken GATHER4 operation. It performs the texture swizzle on the
 * gather results, rather than before. As a result, it must be emulated with
 * direct texture calls.
 */

static bool
lower_tg4(nir_block *block, nir_builder *b, void *mem_ctx)
{
	bool progress = false;

	static const int offsets[3][2] = { {0, 1}, {1, 1}, {1, 0} };

	nir_foreach_instr_safe(instr, block) {
		if (instr->type != nir_instr_type_tex)
			continue;

		nir_tex_instr *tg4 = (nir_tex_instr *)instr;

		if (tg4->op != nir_texop_tg4)
			continue;

		b->cursor = nir_before_instr(&tg4->instr);

		nir_ssa_def *results[4];
		int offset_index = nir_tex_instr_src_index(tg4, nir_tex_src_offset);
		for (int i = 0; i < 4; i++) {
			int num_srcs = tg4->num_srcs + 1 /* lod */;
			if (offset_index < 0 && i < 3)
				num_srcs++;

			nir_tex_instr *tex = nir_tex_instr_create(b->shader, num_srcs);
			tex->op = nir_texop_txl;
			tex->sampler_dim = tg4->sampler_dim;
			tex->coord_components = tg4->coord_components;
			tex->is_array = tg4->is_array;
			tex->is_shadow = tg4->is_shadow;
			tex->is_new_style_shadow = tg4->is_new_style_shadow;
			tex->texture_index = tg4->texture_index;
			tex->sampler_index = tg4->sampler_index;
			tex->dest_type = tg4->dest_type;

			for (int j = 0; j < tg4->num_srcs; j++) {
				nir_src_copy(&tex->src[j].src, &tg4->src[j].src, tex);
				tex->src[j].src_type = tg4->src[j].src_type;
			}
			if (i != 3) {
				nir_ssa_def *offset =
					nir_vec2(b, nir_imm_int(b, offsets[i][0]),
							 nir_imm_int(b, offsets[i][1]));
				if (offset_index < 0) {
					tex->src[tg4->num_srcs].src = nir_src_for_ssa(offset);
					tex->src[tg4->num_srcs].src_type = nir_tex_src_offset;
				} else {
					assert(nir_tex_instr_src_size(tex, offset_index) == 2);
					nir_ssa_def *orig = nir_ssa_for_src(
							b, tex->src[offset_index].src, 2);
					tex->src[offset_index].src =
						nir_src_for_ssa(nir_iadd(b, orig, offset));
				}
			}
			tex->src[num_srcs - 1].src = nir_src_for_ssa(nir_imm_float(b, 0));
			tex->src[num_srcs - 1].src_type = nir_tex_src_lod;

			nir_ssa_dest_init(&tex->instr, &tex->dest,
							  nir_tex_instr_dest_size(tex), 32, NULL);
			nir_builder_instr_insert(b, &tex->instr);

			results[i] = nir_channel(b, &tex->dest.ssa, tg4->component);
		}

		nir_ssa_def *result = nir_vec4(b, results[0], results[1], results[2], results[3]);
		nir_ssa_def_rewrite_uses(&tg4->dest.ssa, nir_src_for_ssa(result));

		nir_instr_remove(&tg4->instr);

		progress = true;
	}

	return progress;
}

static bool
lower_tg4_func(nir_function_impl *impl)
{
	void *mem_ctx = ralloc_parent(impl);
	nir_builder b;
	nir_builder_init(&b, impl);

	bool progress = false;
	nir_foreach_block_safe(block, impl) {
		progress |= lower_tg4(block, &b, mem_ctx);
	}

	if (progress)
		nir_metadata_preserve(impl, nir_metadata_block_index |
									nir_metadata_dominance);

	return progress;
}

bool
ir3_nir_lower_tg4_to_tex(nir_shader *shader)
{
	bool progress = false;

	nir_foreach_function(function, shader) {
		if (function->impl)
			progress |= lower_tg4_func(function->impl);
	}

	return progress;
}
