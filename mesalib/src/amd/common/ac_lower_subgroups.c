/*
 * Copyright © 2018 Google Inc.
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

#include "nir/nir.h"
#include "nir/nir_builder.h"

#include "ac_nir_to_llvm.h"

static nir_ssa_def *ac_lower_subgroups_intrin(nir_builder *b, nir_intrinsic_instr *intrin)
{
	switch(intrin->intrinsic) {
	case nir_intrinsic_vote_ieq:
	case nir_intrinsic_vote_feq: {
		nir_intrinsic_instr *rfi =
			nir_intrinsic_instr_create(b->shader, nir_intrinsic_read_first_invocation);
		nir_ssa_dest_init(&rfi->instr, &rfi->dest,
		                  1, intrin->src[0].ssa->bit_size, NULL);
		nir_src_copy(&rfi->src[0], &intrin->src[0], rfi);
		rfi->num_components = 1;

		nir_ssa_def *is_ne;
		if (intrin->intrinsic == nir_intrinsic_vote_feq)
			is_ne = nir_fne(b, &rfi->dest.ssa, intrin->src[0].ssa);
		else
			is_ne = nir_ine(b, &rfi->dest.ssa, intrin->src[0].ssa);

		nir_intrinsic_instr *ballot =
			nir_intrinsic_instr_create(b->shader, nir_intrinsic_ballot);
		nir_ssa_dest_init(&ballot->instr, &ballot->dest,
		                  1, 64, NULL);
		ballot->src[0] = nir_src_for_ssa(is_ne);
		ballot->num_components = 1;

		return nir_ieq(b, &ballot->dest.ssa, nir_imm_int64(b, 0));
	}
	default:
		return NULL;
	}
}

bool ac_lower_subgroups(struct nir_shader *shader)
{
	bool progress = false;

	nir_foreach_function(function, shader) {
		if (!function->impl)
			continue;

		nir_builder b;
		nir_builder_init(&b, function->impl);

		nir_foreach_block(block, function->impl) {
			nir_foreach_instr_safe(instr, block) {
				if (instr->type != nir_instr_type_intrinsic)
					continue;

				nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
				b.cursor = nir_before_instr(instr);

				nir_ssa_def *lower = ac_lower_subgroups_intrin(&b, intrin);
				if (!lower)
					continue;

				nir_ssa_def_rewrite_uses(&intrin->dest.ssa, nir_src_for_ssa(lower));
				nir_instr_remove(instr);
				progress = true;
			}
		}
	}

	return progress;
}
