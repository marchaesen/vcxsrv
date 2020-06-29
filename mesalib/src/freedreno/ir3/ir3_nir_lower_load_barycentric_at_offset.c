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
 * This pass lowers load_barycentric_at_offset to dsx.3d/dsy.3d and alu
 * instructions.
 */

static nir_ssa_def *
load(nir_builder *b, unsigned ncomp, nir_intrinsic_op op)
{
	nir_intrinsic_instr *load_size = nir_intrinsic_instr_create(b->shader, op);
	nir_ssa_dest_init(&load_size->instr, &load_size->dest, ncomp, 32, NULL);
	nir_builder_instr_insert(b, &load_size->instr);

	return &load_size->dest.ssa;
}

static nir_ssa_def *
ir3_nir_lower_load_barycentric_at_offset_instr(nir_builder *b,
	   nir_instr *instr, void *data)
{
	nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

#define chan(var, c) nir_channel(b, var, c)

	nir_ssa_def *off = intr->src[0].ssa;
	nir_ssa_def *ij = load(b, 2, nir_intrinsic_load_barycentric_pixel);
	nir_ssa_def *s  = load(b, 1, nir_intrinsic_load_size_ir3);

	s = nir_frcp(b, s);

	/* scaled ij with s as 3rd component: */
	nir_ssa_def *sij = nir_vec3(b,
			nir_fmul(b, chan(ij, 0), s),
			nir_fmul(b, chan(ij, 1), s),
			s);

	nir_ssa_def *foo = nir_fddx(b, sij);
	nir_ssa_def *bar = nir_fddy(b, sij);

	if (b->shader->info.stage == MESA_SHADER_FRAGMENT)
		b->shader->info.fs.needs_helper_invocations = true;

	nir_ssa_def *x, *y, *z, *i, *j;

	x = nir_ffma(b, chan(off, 0), chan(foo, 0), chan(sij, 0));
	y = nir_ffma(b, chan(off, 0), chan(foo, 1), chan(sij, 1));
	z = nir_ffma(b, chan(off, 0), chan(foo, 2), chan(sij, 2));

	x = nir_ffma(b, chan(off, 1), chan(bar, 0), x);
	y = nir_ffma(b, chan(off, 1), chan(bar, 1), y);
	z = nir_ffma(b, chan(off, 1), chan(bar, 2), z);

	/* convert back into primitive space: */
	z = nir_frcp(b, z);
	i = nir_fmul(b, z, x);
	j = nir_fmul(b, z, y);

	ij = nir_vec2(b, i, j);

	return ij;
}

static bool
ir3_nir_lower_load_barycentric_at_offset_filter(const nir_instr *instr,
		const void *data)
{
	return (instr->type == nir_instr_type_intrinsic &&
			nir_instr_as_intrinsic(instr)->intrinsic ==
			nir_intrinsic_load_barycentric_at_offset);
}

bool
ir3_nir_lower_load_barycentric_at_offset(nir_shader *shader)
{
	debug_assert(shader->info.stage == MESA_SHADER_FRAGMENT);

	return nir_shader_lower_instructions(shader,
			ir3_nir_lower_load_barycentric_at_offset_filter,
			ir3_nir_lower_load_barycentric_at_offset_instr,
			NULL);
}
