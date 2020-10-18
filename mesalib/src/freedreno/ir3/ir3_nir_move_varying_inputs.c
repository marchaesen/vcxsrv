/*
 * Copyright © 2019 Red Hat
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
 * This pass moves varying fetches (and the instructions they depend on
 * into the start block.
 *
 * We need to set the (ei) "end input" flag on the last varying fetch.
 * And we want to ensure that all threads execute the instruction that
 * sets (ei).  The easiest way to ensure this is to move all varying
 * fetches into the start block.  Which is something we used to get for
 * free by using lower_all_io_to_temps=true.
 *
 * This may come at the cost of additional register usage.  OTOH setting
 * the (ei) flag earlier probably frees up more VS to run.
 */


typedef struct {
	nir_shader *shader;
	nir_block *start_block;
} state;

static void move_instruction_to_start_block(state *state, nir_instr *instr);

static bool
move_src(nir_src *src, void *state)
{
	/* At this point we shouldn't have any non-ssa src: */
	debug_assert(src->is_ssa);
	move_instruction_to_start_block(state, src->ssa->parent_instr);
	return true;
}

static void
move_instruction_to_start_block(state *state, nir_instr *instr)
{
	/* nothing to do if the instruction is already in the start block */
	if (instr->block == state->start_block)
		return;

	/* first move (recursively) all src's to ensure they appear before
	 * load*_input that we are trying to move:
	 */
	nir_foreach_src(instr, move_src, state);

	/* and then move the instruction itself:
	 */
	exec_node_remove(&instr->node);
	exec_list_push_tail(&state->start_block->instr_list, &instr->node);
	instr->block = state->start_block;
}

static bool
move_varying_inputs_block(state *state, nir_block *block)
{
	bool progress = false;

	nir_foreach_instr_safe (instr, block) {
		if (instr->type != nir_instr_type_intrinsic)
			continue;

		nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

		switch (intr->intrinsic) {
		case nir_intrinsic_load_interpolated_input:
		case nir_intrinsic_load_input:
			/* TODO any others to handle? */
			break;
		default:
			continue;
		}

		debug_assert(intr->dest.is_ssa);

		move_instruction_to_start_block(state, instr);

		progress = true;
	}

	return progress;
}

bool
ir3_nir_move_varying_inputs(nir_shader *shader)
{
	bool progress = false;

	debug_assert(shader->info.stage == MESA_SHADER_FRAGMENT);

	nir_foreach_function (function, shader) {
		state state;

		if (!function->impl)
			continue;

		state.shader = shader;
		state.start_block = nir_start_block(function->impl);

		bool progress = false;
		nir_foreach_block (block, function->impl) {
			/* don't need to move anything that is already in the first block */
			if (block == state.start_block)
				continue;
			progress |= move_varying_inputs_block(&state, block);
		}

		if (progress) {
			nir_metadata_preserve(function->impl,
				nir_metadata_block_index | nir_metadata_dominance);
		}
	}

	return progress;
}
