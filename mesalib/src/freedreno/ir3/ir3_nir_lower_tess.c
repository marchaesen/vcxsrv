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
#include "compiler/nir/nir_builder.h"

struct state {
	uint32_t topology;

	struct primitive_map {
		unsigned loc[32];
		unsigned size[32];
		unsigned stride;
	} map;

	nir_ssa_def *header;

	nir_variable *vertex_count_var;
	nir_variable *emitted_vertex_var;
	nir_variable *vertex_flags_out;

	struct exec_list old_outputs;
	struct exec_list new_outputs;
	struct exec_list emit_outputs;

	/* tess ctrl shader on a650 gets the local primitive id at different bits: */
	unsigned local_primitive_id_start;
};

static nir_ssa_def *
bitfield_extract(nir_builder *b, nir_ssa_def *v, uint32_t start, uint32_t mask)
{
	return nir_iand(b, nir_ushr(b, v, nir_imm_int(b, start)),
			nir_imm_int(b, mask));
}

static nir_ssa_def *
build_invocation_id(nir_builder *b, struct state *state)
{
	return bitfield_extract(b, state->header, 11, 31);
}

static nir_ssa_def *
build_vertex_id(nir_builder *b, struct state *state)
{
	return bitfield_extract(b, state->header, 6, 31);
}

static nir_ssa_def *
build_local_primitive_id(nir_builder *b, struct state *state)
{
	return bitfield_extract(b, state->header, state->local_primitive_id_start, 63);
}

static nir_variable *
get_var(nir_shader *shader, nir_variable_mode mode, int driver_location)
{
	nir_foreach_variable_with_modes (v, shader, mode) {
		if (v->data.driver_location == driver_location) {
			return v;
		}
	}

	return NULL;
}

static bool
is_tess_levels(nir_variable *var)
{
	return (var->data.location == VARYING_SLOT_TESS_LEVEL_OUTER ||
			var->data.location == VARYING_SLOT_TESS_LEVEL_INNER);
}

static nir_ssa_def *
build_local_offset(nir_builder *b, struct state *state,
		nir_ssa_def *vertex, uint32_t base, nir_ssa_def *offset)
{
	nir_ssa_def *primitive_stride = nir_load_vs_primitive_stride_ir3(b);
	nir_ssa_def *primitive_offset =
		nir_imul24(b, build_local_primitive_id(b, state), primitive_stride);
	nir_ssa_def *attr_offset;
	nir_ssa_def *vertex_stride;

	switch (b->shader->info.stage) {
	case MESA_SHADER_VERTEX:
	case MESA_SHADER_TESS_EVAL:
		vertex_stride = nir_imm_int(b, state->map.stride * 4);
		attr_offset = nir_imm_int(b, state->map.loc[base] * 4);
		break;
	case MESA_SHADER_TESS_CTRL:
	case MESA_SHADER_GEOMETRY:
		vertex_stride = nir_load_vs_vertex_stride_ir3(b);
		attr_offset = nir_load_primitive_location_ir3(b, base);
		break;
	default:
		unreachable("bad shader stage");
	}

	nir_ssa_def *vertex_offset = nir_imul24(b, vertex, vertex_stride);

	return nir_iadd(b, nir_iadd(b, primitive_offset, vertex_offset),
			nir_iadd(b, attr_offset, offset));
}

static nir_intrinsic_instr *
replace_intrinsic(nir_builder *b, nir_intrinsic_instr *intr,
		nir_intrinsic_op op, nir_ssa_def *src0, nir_ssa_def *src1, nir_ssa_def *src2)
{
	nir_intrinsic_instr *new_intr =
		nir_intrinsic_instr_create(b->shader, op);

	new_intr->src[0] = nir_src_for_ssa(src0);
	if (src1)
		new_intr->src[1] = nir_src_for_ssa(src1);
	if (src2)
		new_intr->src[2] = nir_src_for_ssa(src2);

	new_intr->num_components = intr->num_components;

	if (nir_intrinsic_infos[op].has_dest)
		nir_ssa_dest_init(&new_intr->instr, &new_intr->dest,
						  intr->num_components, 32, NULL);

	nir_builder_instr_insert(b, &new_intr->instr);

	if (nir_intrinsic_infos[op].has_dest)
		nir_ssa_def_rewrite_uses(&intr->dest.ssa, nir_src_for_ssa(&new_intr->dest.ssa));

	nir_instr_remove(&intr->instr);

	return new_intr;
}

static void
build_primitive_map(nir_shader *shader, nir_variable_mode mode, struct primitive_map *map)
{
	nir_foreach_variable_with_modes (var, shader, mode) {
		switch (var->data.location) {
		case VARYING_SLOT_TESS_LEVEL_OUTER:
		case VARYING_SLOT_TESS_LEVEL_INNER:
			continue;
		}

		unsigned size = glsl_count_attribute_slots(var->type, false) * 4;

		assert(var->data.driver_location < ARRAY_SIZE(map->size));
		map->size[var->data.driver_location] =
			MAX2(map->size[var->data.driver_location], size);
	}

	unsigned loc = 0;
	for (uint32_t i = 0; i < ARRAY_SIZE(map->size); i++) {
		if (map->size[i] == 0)
				continue;
		nir_variable *var = get_var(shader, mode, i);
		map->loc[i] = loc;
		loc += map->size[i];

		if (var->data.patch)
			map->size[i] = 0;
		else
			map->size[i] = map->size[i] / glsl_get_length(var->type);
	}

	map->stride = loc;
}

static void
lower_block_to_explicit_output(nir_block *block, nir_builder *b, struct state *state)
{
	nir_foreach_instr_safe (instr, block) {
		if (instr->type != nir_instr_type_intrinsic)
			continue;

		nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

		switch (intr->intrinsic) {
		case nir_intrinsic_store_output: {
			// src[] = { value, offset }.

			/* nir_lower_io_to_temporaries replaces all access to output
			 * variables with temp variables and then emits a nir_copy_var at
			 * the end of the shader.  Thus, we should always get a full wrmask
			 * here.
			 */
			assert(util_is_power_of_two_nonzero(nir_intrinsic_write_mask(intr) + 1));

			b->cursor = nir_instr_remove(&intr->instr);

			nir_ssa_def *vertex_id = build_vertex_id(b, state);
			nir_ssa_def *offset = build_local_offset(b, state, vertex_id, nir_intrinsic_base(intr),
					intr->src[1].ssa);
			nir_intrinsic_instr *store =
				nir_intrinsic_instr_create(b->shader, nir_intrinsic_store_shared_ir3);

			store->src[0] = nir_src_for_ssa(intr->src[0].ssa);
			store->src[1] = nir_src_for_ssa(offset);
			store->num_components = intr->num_components;

			nir_builder_instr_insert(b, &store->instr);
			break;
		}

		default:
			break;
		}
	}
}

static nir_ssa_def *
local_thread_id(nir_builder *b)
{
	return bitfield_extract(b, nir_load_gs_header_ir3(b), 16, 1023);
}

void
ir3_nir_lower_to_explicit_output(nir_shader *shader, struct ir3_shader_variant *v,
		unsigned topology)
{
	struct state state = { };

	build_primitive_map(shader, nir_var_shader_out, &state.map);
	memcpy(v->output_loc, state.map.loc, sizeof(v->output_loc));

	nir_function_impl *impl = nir_shader_get_entrypoint(shader);
	assert(impl);

	nir_builder b;
	nir_builder_init(&b, impl);
	b.cursor = nir_before_cf_list(&impl->body);

	if (v->type == MESA_SHADER_VERTEX && topology != IR3_TESS_NONE)
		state.header = nir_load_tcs_header_ir3(&b);
	else
		state.header = nir_load_gs_header_ir3(&b);

	nir_foreach_block_safe (block, impl)
		lower_block_to_explicit_output(block, &b, &state);

	nir_metadata_preserve(impl, nir_metadata_block_index |
			nir_metadata_dominance);

	v->output_size = state.map.stride;
}


static void
lower_block_to_explicit_input(nir_block *block, nir_builder *b, struct state *state)
{
	nir_foreach_instr_safe (instr, block) {
		if (instr->type != nir_instr_type_intrinsic)
			continue;

		nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

		switch (intr->intrinsic) {
		case nir_intrinsic_load_per_vertex_input: {
			// src[] = { vertex, offset }.

			b->cursor = nir_before_instr(&intr->instr);

			nir_ssa_def *offset = build_local_offset(b, state,
					intr->src[0].ssa, // this is typically gl_InvocationID
					nir_intrinsic_base(intr),
					intr->src[1].ssa);

			replace_intrinsic(b, intr, nir_intrinsic_load_shared_ir3, offset, NULL, NULL);
			break;
		}

		case nir_intrinsic_load_invocation_id: {
			b->cursor = nir_before_instr(&intr->instr);

			nir_ssa_def *iid = build_invocation_id(b, state);
			nir_ssa_def_rewrite_uses(&intr->dest.ssa, nir_src_for_ssa(iid));
			nir_instr_remove(&intr->instr);
			break;
		}

		default:
			break;
		}
	}
}

void
ir3_nir_lower_to_explicit_input(nir_shader *shader, struct ir3_compiler *compiler)
{
 	struct state state = { };

	/* when using stl/ldl (instead of stlw/ldlw) for linking VS and HS,
	 * HS uses a different primitive id, which starts at bit 16 in the header
	 */
	if (shader->info.stage == MESA_SHADER_TESS_CTRL && compiler->tess_use_shared)
		state.local_primitive_id_start = 16;

	nir_function_impl *impl = nir_shader_get_entrypoint(shader);
	assert(impl);

	nir_builder b;
	nir_builder_init(&b, impl);
	b.cursor = nir_before_cf_list(&impl->body);

	if (shader->info.stage == MESA_SHADER_GEOMETRY)
		state.header = nir_load_gs_header_ir3(&b);
	else
		state.header = nir_load_tcs_header_ir3(&b);

	nir_foreach_block_safe (block, impl)
		lower_block_to_explicit_input(block, &b, &state);
}


static nir_ssa_def *
build_per_vertex_offset(nir_builder *b, struct state *state,
		nir_ssa_def *vertex, nir_ssa_def *offset, nir_variable *var)
{
	nir_ssa_def *primitive_id = nir_load_primitive_id(b);
	nir_ssa_def *patch_stride = nir_load_hs_patch_stride_ir3(b);
	nir_ssa_def *patch_offset = nir_imul24(b, primitive_id, patch_stride);
	nir_ssa_def *attr_offset;
	int loc = var->data.driver_location;

	switch (b->shader->info.stage) {
	case MESA_SHADER_TESS_CTRL:
		attr_offset = nir_imm_int(b, state->map.loc[loc]);
		break;
	case MESA_SHADER_TESS_EVAL:
		attr_offset = nir_load_primitive_location_ir3(b, loc);
		break;
	default:
		unreachable("bad shader state");
	}

	nir_ssa_def *attr_stride = nir_imm_int(b, state->map.size[loc]);
	nir_ssa_def *vertex_offset = nir_imul24(b, vertex, attr_stride);

	return nir_iadd(b, nir_iadd(b, patch_offset, attr_offset),
			nir_iadd(b, vertex_offset, nir_ishl(b, offset, nir_imm_int(b, 2))));
}

static nir_ssa_def *
build_patch_offset(nir_builder *b, struct state *state, nir_ssa_def *offset, nir_variable *var)
{
	debug_assert(var && var->data.patch);

	return build_per_vertex_offset(b, state, nir_imm_int(b, 0), offset, var);
}

static void
tess_level_components(struct state *state, uint32_t *inner, uint32_t *outer)
{
	switch (state->topology) {
	case IR3_TESS_TRIANGLES:
		*inner = 1;
		*outer = 3;
		break;
	case IR3_TESS_QUADS:
		*inner = 2;
		*outer = 4;
		break;
	case IR3_TESS_ISOLINES:
		*inner = 0;
		*outer = 2;
		break;
	default:
		unreachable("bad");
	}
}

static nir_ssa_def *
build_tessfactor_base(nir_builder *b, gl_varying_slot slot, struct state *state)
{
	uint32_t inner_levels, outer_levels;
	tess_level_components(state, &inner_levels, &outer_levels);

	const uint32_t patch_stride = 1 + inner_levels + outer_levels;

	nir_ssa_def *primitive_id = nir_load_primitive_id(b);

	nir_ssa_def *patch_offset = nir_imul24(b, primitive_id, nir_imm_int(b, patch_stride));

	uint32_t offset;
	switch (slot) {
	case VARYING_SLOT_TESS_LEVEL_OUTER:
		/* There's some kind of header dword, tess levels start at index 1. */
		offset = 1;
		break;
	case VARYING_SLOT_TESS_LEVEL_INNER:
		offset = 1 + outer_levels;
		break;
	default:
		unreachable("bad");
	}

	return nir_iadd(b, patch_offset, nir_imm_int(b, offset));
}

static void
lower_tess_ctrl_block(nir_block *block, nir_builder *b, struct state *state)
{
	nir_foreach_instr_safe (instr, block) {
		if (instr->type != nir_instr_type_intrinsic)
			continue;

		nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

		switch (intr->intrinsic) {
		case nir_intrinsic_control_barrier:
		case nir_intrinsic_memory_barrier_tcs_patch:
			/* Hull shaders dispatch 32 wide so an entire patch will always
			 * fit in a single warp and execute in lock-step.  Consequently,
			 * we don't need to do anything for TCS barriers so just remove
			 * the intrinsic. Otherwise we'll emit an actual barrier
			 * instructions, which will deadlock.
			 */
			nir_instr_remove(&intr->instr);
			break;

		case nir_intrinsic_load_per_vertex_output: {
			// src[] = { vertex, offset }.

			b->cursor = nir_before_instr(&intr->instr);

			nir_ssa_def *address = nir_load_tess_param_base_ir3(b);
			nir_variable *var = get_var(b->shader, nir_var_shader_out, nir_intrinsic_base(intr));
			nir_ssa_def *offset = build_per_vertex_offset(b, state,
					intr->src[0].ssa, intr->src[1].ssa, var);

			replace_intrinsic(b, intr, nir_intrinsic_load_global_ir3, address, offset, NULL);
			break;
		}

		case nir_intrinsic_store_per_vertex_output: {
			// src[] = { value, vertex, offset }.

			b->cursor = nir_before_instr(&intr->instr);

			/* sparse writemask not supported */
			assert(util_is_power_of_two_nonzero(nir_intrinsic_write_mask(intr) + 1));

			nir_ssa_def *value = intr->src[0].ssa;
			nir_ssa_def *address = nir_load_tess_param_base_ir3(b);
			nir_variable *var = get_var(b->shader, nir_var_shader_out, nir_intrinsic_base(intr));
			nir_ssa_def *offset = build_per_vertex_offset(b, state,
					intr->src[1].ssa, intr->src[2].ssa, var);

			replace_intrinsic(b, intr, nir_intrinsic_store_global_ir3, value, address,
					nir_iadd(b, offset, nir_imm_int(b, nir_intrinsic_component(intr))));

			break;
		}

		case nir_intrinsic_load_output: {
			// src[] = { offset }.

			nir_variable *var = get_var(b->shader, nir_var_shader_out, nir_intrinsic_base(intr));

			b->cursor = nir_before_instr(&intr->instr);

			nir_ssa_def *address, *offset;

			/* note if vectorization of the tess level loads ever happens:
			 * "ldg" across 16-byte boundaries can behave incorrectly if results
			 * are never used. most likely some issue with (sy) not properly
			 * syncing with values coming from a second memory transaction.
			 */
			if (is_tess_levels(var)) {
				assert(intr->dest.ssa.num_components == 1);
				address = nir_load_tess_factor_base_ir3(b);
				offset = build_tessfactor_base(b, var->data.location, state);
			} else {
				address = nir_load_tess_param_base_ir3(b);
				offset = build_patch_offset(b, state, intr->src[0].ssa, var);
			}

			replace_intrinsic(b, intr, nir_intrinsic_load_global_ir3, address, offset, NULL);
			break;
		}

		case nir_intrinsic_store_output: {
			// src[] = { value, offset }.

			/* write patch output to bo */

			nir_variable *var = get_var(b->shader, nir_var_shader_out, nir_intrinsic_base(intr));

			b->cursor = nir_before_instr(&intr->instr);

			/* sparse writemask not supported */
			assert(util_is_power_of_two_nonzero(nir_intrinsic_write_mask(intr) + 1));

			if (is_tess_levels(var)) {
				/* with tess levels are defined as float[4] and float[2],
				 * but tess factor BO has smaller sizes for tris/isolines,
				 * so we have to discard any writes beyond the number of
				 * components for inner/outer levels */
				uint32_t inner_levels, outer_levels, levels;
				tess_level_components(state, &inner_levels, &outer_levels);

				if (var->data.location == VARYING_SLOT_TESS_LEVEL_OUTER)
					levels = outer_levels;
				else
					levels = inner_levels;

				assert(intr->src[0].ssa->num_components == 1);

				nir_ssa_def *offset =
					nir_iadd_imm(b, intr->src[1].ssa, nir_intrinsic_component(intr));

				nir_if *nif = nir_push_if(b, nir_ult(b, offset, nir_imm_int(b, levels)));

				replace_intrinsic(b, intr, nir_intrinsic_store_global_ir3,
						intr->src[0].ssa,
						nir_load_tess_factor_base_ir3(b),
						nir_iadd(b, offset, build_tessfactor_base(b, var->data.location, state)));

				nir_pop_if(b, nif);
			} else {
				nir_ssa_def *address = nir_load_tess_param_base_ir3(b);
				nir_ssa_def *offset = build_patch_offset(b, state, intr->src[1].ssa, var);

				debug_assert(nir_intrinsic_component(intr) == 0);

				replace_intrinsic(b, intr, nir_intrinsic_store_global_ir3,
						intr->src[0].ssa, address, offset);
			}
			break;
		}

		default:
			break;
		}
	}
}

static void
emit_tess_epilouge(nir_builder *b, struct state *state)
{
	/* Insert endpatch instruction:
	 *
	 * TODO we should re-work this to use normal flow control.
	 */

	nir_intrinsic_instr *end_patch =
		nir_intrinsic_instr_create(b->shader, nir_intrinsic_end_patch_ir3);
	nir_builder_instr_insert(b, &end_patch->instr);
}

void
ir3_nir_lower_tess_ctrl(nir_shader *shader, struct ir3_shader_variant *v,
		unsigned topology)
{
	struct state state = { .topology = topology };

	if (shader_debug_enabled(shader->info.stage)) {
		fprintf(stderr, "NIR (before tess lowering) for %s shader:\n",
				_mesa_shader_stage_to_string(shader->info.stage));
		nir_print_shader(shader, stderr);
	}

	build_primitive_map(shader, nir_var_shader_out, &state.map);
	memcpy(v->output_loc, state.map.loc, sizeof(v->output_loc));
	v->output_size = state.map.stride;

	nir_function_impl *impl = nir_shader_get_entrypoint(shader);
	assert(impl);

	nir_builder b;
	nir_builder_init(&b, impl);
	b.cursor = nir_before_cf_list(&impl->body);

	state.header = nir_load_tcs_header_ir3(&b);

	nir_foreach_block_safe (block, impl)
		lower_tess_ctrl_block(block, &b, &state);

	/* Now move the body of the TCS into a conditional:
	 *
	 *   if (gl_InvocationID < num_vertices)
	 *     // body
	 *
	 */

	nir_cf_list body;
	nir_cf_extract(&body, nir_before_cf_list(&impl->body),
				   nir_after_cf_list(&impl->body));

	b.cursor = nir_after_cf_list(&impl->body);

	/* Re-emit the header, since the old one got moved into the if branch */
	state.header = nir_load_tcs_header_ir3(&b);
	nir_ssa_def *iid = build_invocation_id(&b, &state);

	const uint32_t nvertices = shader->info.tess.tcs_vertices_out;
	nir_ssa_def *cond = nir_ult(&b, iid, nir_imm_int(&b, nvertices));

	nir_if *nif = nir_push_if(&b, cond);

	nir_cf_reinsert(&body, b.cursor);

	b.cursor = nir_after_cf_list(&nif->then_list);

	/* Insert conditional exit for threads invocation id != 0 */
	nir_ssa_def *iid0_cond = nir_ieq(&b, iid, nir_imm_int(&b, 0));
	nir_intrinsic_instr *cond_end =
		nir_intrinsic_instr_create(shader, nir_intrinsic_cond_end_ir3);
	cond_end->src[0] = nir_src_for_ssa(iid0_cond);
	nir_builder_instr_insert(&b, &cond_end->instr);

	emit_tess_epilouge(&b, &state);

	nir_pop_if(&b, nif);

	nir_metadata_preserve(impl, 0);
}


static void
lower_tess_eval_block(nir_block *block, nir_builder *b, struct state *state)
{
	nir_foreach_instr_safe (instr, block) {
		if (instr->type != nir_instr_type_intrinsic)
			continue;

		nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

		switch (intr->intrinsic) {
		case nir_intrinsic_load_tess_coord: {
			b->cursor = nir_after_instr(&intr->instr);
			nir_ssa_def *x = nir_channel(b, &intr->dest.ssa, 0);
			nir_ssa_def *y = nir_channel(b, &intr->dest.ssa, 1);
			nir_ssa_def *z;

			if (state->topology == IR3_TESS_TRIANGLES)
				z = nir_fsub(b, nir_fsub(b, nir_imm_float(b, 1.0f), y), x);
			else
				z = nir_imm_float(b, 0.0f);

			nir_ssa_def *coord = nir_vec3(b, x, y, z);

			nir_ssa_def_rewrite_uses_after(&intr->dest.ssa,
					nir_src_for_ssa(coord),
					b->cursor.instr);
			break;
		}

		case nir_intrinsic_load_per_vertex_input: {
			// src[] = { vertex, offset }.

			b->cursor = nir_before_instr(&intr->instr);

			nir_ssa_def *address = nir_load_tess_param_base_ir3(b);
			nir_variable *var = get_var(b->shader, nir_var_shader_in, nir_intrinsic_base(intr));
			nir_ssa_def *offset = build_per_vertex_offset(b, state,
					intr->src[0].ssa, intr->src[1].ssa, var);

			replace_intrinsic(b, intr, nir_intrinsic_load_global_ir3, address, offset, NULL);
			break;
		}

		case nir_intrinsic_load_input: {
			// src[] = { offset }.

			nir_variable *var = get_var(b->shader, nir_var_shader_in, nir_intrinsic_base(intr));

			debug_assert(var->data.patch);

			b->cursor = nir_before_instr(&intr->instr);

			nir_ssa_def *address, *offset;

			/* note if vectorization of the tess level loads ever happens:
			 * "ldg" across 16-byte boundaries can behave incorrectly if results
			 * are never used. most likely some issue with (sy) not properly
			 * syncing with values coming from a second memory transaction.
			 */
			if (is_tess_levels(var)) {
				assert(intr->dest.ssa.num_components == 1);
				address = nir_load_tess_factor_base_ir3(b);
				offset = build_tessfactor_base(b, var->data.location, state);
			} else {
				address = nir_load_tess_param_base_ir3(b);
				offset = build_patch_offset(b, state, intr->src[0].ssa, var);
			}

			offset = nir_iadd(b, offset, nir_imm_int(b, nir_intrinsic_component(intr)));

			replace_intrinsic(b, intr, nir_intrinsic_load_global_ir3, address, offset, NULL);
			break;
		}

		default:
			break;
		}
	}
}

void
ir3_nir_lower_tess_eval(nir_shader *shader, unsigned topology)
{
	struct state state = { .topology = topology };

	if (shader_debug_enabled(shader->info.stage)) {
		fprintf(stderr, "NIR (before tess lowering) for %s shader:\n",
				_mesa_shader_stage_to_string(shader->info.stage));
		nir_print_shader(shader, stderr);
	}

	/* Build map of inputs so we have the sizes. */
	build_primitive_map(shader, nir_var_shader_in, &state.map);

	nir_function_impl *impl = nir_shader_get_entrypoint(shader);
	assert(impl);

	nir_builder b;
	nir_builder_init(&b, impl);

	nir_foreach_block_safe (block, impl)
		lower_tess_eval_block(block, &b, &state);

	nir_metadata_preserve(impl, 0);
}

static void
lower_gs_block(nir_block *block, nir_builder *b, struct state *state)
{
	nir_foreach_instr_safe (instr, block) {
		if (instr->type != nir_instr_type_intrinsic)
			continue;

		nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

		switch (intr->intrinsic) {
		case nir_intrinsic_end_primitive: {
			b->cursor = nir_before_instr(&intr->instr);
			nir_store_var(b, state->vertex_flags_out, nir_imm_int(b, 4), 0x1);
			nir_instr_remove(&intr->instr);
			break;
		}

		case nir_intrinsic_emit_vertex: {
			/* Load the vertex count */
			b->cursor = nir_before_instr(&intr->instr);
			nir_ssa_def *count = nir_load_var(b, state->vertex_count_var);

			nir_push_if(b, nir_ieq(b, count, local_thread_id(b)));

			foreach_two_lists(dest_node, &state->emit_outputs, src_node, &state->old_outputs) {
				nir_variable *dest = exec_node_data(nir_variable, dest_node, node);
				nir_variable *src = exec_node_data(nir_variable, src_node, node);
				nir_copy_var(b, dest, src);
			}

			nir_instr_remove(&intr->instr);

			nir_store_var(b, state->emitted_vertex_var,
					nir_iadd(b, nir_load_var(b, state->emitted_vertex_var), nir_imm_int(b, 1)), 0x1);

			nir_pop_if(b, NULL);

			/* Increment the vertex count by 1 */
			nir_store_var(b, state->vertex_count_var,
					nir_iadd(b, count, nir_imm_int(b, 1)), 0x1); /* .x */
			nir_store_var(b, state->vertex_flags_out, nir_imm_int(b, 0), 0x1);

			break;
		}

		default:
			break;
		}
	}
}

void
ir3_nir_lower_gs(nir_shader *shader)
{
	struct state state = { };

	if (shader_debug_enabled(shader->info.stage)) {
		fprintf(stderr, "NIR (before gs lowering):\n");
		nir_print_shader(shader, stderr);
	}

	build_primitive_map(shader, nir_var_shader_in, &state.map);

	/* Create an output var for vertex_flags. This will be shadowed below,
	 * same way regular outputs get shadowed, and this variable will become a
	 * temporary.
	 */
	state.vertex_flags_out = nir_variable_create(shader, nir_var_shader_out,
			glsl_uint_type(), "vertex_flags");
	state.vertex_flags_out->data.driver_location = shader->num_outputs++;
	state.vertex_flags_out->data.location = VARYING_SLOT_GS_VERTEX_FLAGS_IR3;
	state.vertex_flags_out->data.interpolation = INTERP_MODE_NONE;

	nir_function_impl *impl = nir_shader_get_entrypoint(shader);
	assert(impl);

	nir_builder b;
	nir_builder_init(&b, impl);
	b.cursor = nir_before_cf_list(&impl->body);

	state.header = nir_load_gs_header_ir3(&b);

	/* Generate two set of shadow vars for the output variables.  The first
	 * set replaces the real outputs and the second set (emit_outputs) we'll
	 * assign in the emit_vertex conditionals.  Then at the end of the shader
	 * we copy the emit_outputs to the real outputs, so that we get
	 * store_output in uniform control flow.
	 */
	exec_list_make_empty(&state.old_outputs);
	nir_foreach_shader_out_variable_safe(var, shader) {
		exec_node_remove(&var->node);
		exec_list_push_tail(&state.old_outputs, &var->node);
	}
	exec_list_make_empty(&state.new_outputs);
	exec_list_make_empty(&state.emit_outputs);
	nir_foreach_variable_in_list(var, &state.old_outputs) {
		/* Create a new output var by cloning the original output var and
		 * stealing the name.
		 */
		nir_variable *output = nir_variable_clone(var, shader);
		exec_list_push_tail(&state.new_outputs, &output->node);

		/* Rewrite the original output to be a shadow variable. */
		var->name = ralloc_asprintf(var, "%s@gs-temp", output->name);
		var->data.mode = nir_var_shader_temp;

		/* Clone the shadow variable to create the emit shadow variable that
		 * we'll assign in the emit conditionals.
		 */
		nir_variable *emit_output = nir_variable_clone(var, shader);
		emit_output->name = ralloc_asprintf(var, "%s@emit-temp", output->name);
		exec_list_push_tail(&state.emit_outputs, &emit_output->node);
	}

	/* During the shader we'll keep track of which vertex we're currently
	 * emitting for the EmitVertex test and how many vertices we emitted so we
	 * know to discard if didn't emit any.  In most simple shaders, this can
	 * all be statically determined and gets optimized away.
	 */
	state.vertex_count_var =
		nir_local_variable_create(impl, glsl_uint_type(), "vertex_count");
	state.emitted_vertex_var =
		nir_local_variable_create(impl, glsl_uint_type(), "emitted_vertex");

	/* Initialize to 0. */
	b.cursor = nir_before_cf_list(&impl->body);
	nir_store_var(&b, state.vertex_count_var, nir_imm_int(&b, 0), 0x1);
	nir_store_var(&b, state.emitted_vertex_var, nir_imm_int(&b, 0), 0x1);
	nir_store_var(&b, state.vertex_flags_out, nir_imm_int(&b, 4), 0x1);

	nir_foreach_block_safe (block, impl)
		lower_gs_block(block, &b, &state);

	set_foreach(impl->end_block->predecessors, block_entry) {
		struct nir_block *block = (void *)block_entry->key;
		b.cursor = nir_after_block_before_jump(block);

		nir_intrinsic_instr *discard_if =
			nir_intrinsic_instr_create(b.shader, nir_intrinsic_discard_if);

		nir_ssa_def *cond = nir_ieq(&b, nir_load_var(&b, state.emitted_vertex_var), nir_imm_int(&b, 0));

		discard_if->src[0] = nir_src_for_ssa(cond);

		nir_builder_instr_insert(&b, &discard_if->instr);

		foreach_two_lists(dest_node, &state.new_outputs, src_node, &state.emit_outputs) {
			nir_variable *dest = exec_node_data(nir_variable, dest_node, node);
			nir_variable *src = exec_node_data(nir_variable, src_node, node);
			nir_copy_var(&b, dest, src);
		}
	}

	exec_list_append(&shader->variables, &state.old_outputs);
	exec_list_append(&shader->variables, &state.emit_outputs);
	exec_list_append(&shader->variables, &state.new_outputs);

	nir_metadata_preserve(impl, 0);

	nir_lower_global_vars_to_local(shader);
	nir_split_var_copies(shader);
	nir_lower_var_copies(shader);

	nir_fixup_deref_modes(shader);

	if (shader_debug_enabled(shader->info.stage)) {
		fprintf(stderr, "NIR (after gs lowering):\n");
		nir_print_shader(shader, stderr);
	}
}

uint32_t
ir3_link_geometry_stages(const struct ir3_shader_variant *producer,
		const struct ir3_shader_variant *consumer,
		uint32_t *locs)
{
	uint32_t num_loc = 0, factor;

	switch (consumer->type) {
	case MESA_SHADER_TESS_CTRL:
	case MESA_SHADER_GEOMETRY:
		/* These stages load with ldlw, which expects byte offsets. */
		factor = 4;
		break;
	case MESA_SHADER_TESS_EVAL:
		/* The tess eval shader uses ldg, which takes dword offsets. */
		factor = 1;
		break;
	default:
		unreachable("bad shader stage");
	}

	nir_foreach_shader_in_variable(in_var, consumer->shader->nir) {
		nir_foreach_shader_out_variable(out_var, producer->shader->nir) {
			if (in_var->data.location == out_var->data.location) {
				locs[in_var->data.driver_location] =
					producer->output_loc[out_var->data.driver_location] * factor;

				debug_assert(num_loc <= in_var->data.driver_location + 1);
				num_loc = in_var->data.driver_location + 1;
			}
		}
	}

	return num_loc;
}
