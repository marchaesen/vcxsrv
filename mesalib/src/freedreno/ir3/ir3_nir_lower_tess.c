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
	nir_variable *vertex_flags_var;
	nir_variable *vertex_flags_out;

	nir_variable *output_vars[32];

	nir_ssa_def *outer_levels[4];
	nir_ssa_def *inner_levels[2];
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
	return bitfield_extract(b, state->header, 0, 63);
}

static nir_variable *
get_var(struct exec_list *list, int driver_location)
{
	nir_foreach_variable(v, list) {
		if (v->data.driver_location == driver_location) {
			return v;
		}
	}

	return NULL;
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
build_primitive_map(nir_shader *shader, struct primitive_map *map, struct exec_list *list)
{
	nir_foreach_variable(var, list) {
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
		nir_variable *var = get_var(list, i);
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
lower_vs_block(nir_block *block, nir_builder *b, struct state *state)
{
	nir_foreach_instr_safe(instr, block) {
		if (instr->type != nir_instr_type_intrinsic)
			continue;

		nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

		switch (intr->intrinsic) {
		case nir_intrinsic_store_output: {
			// src[] = { value, offset }.

			b->cursor = nir_before_instr(&intr->instr);

			nir_ssa_def *vertex_id = build_vertex_id(b, state);
			nir_ssa_def *offset = build_local_offset(b, state, vertex_id, nir_intrinsic_base(intr),
					intr->src[1].ssa);
			nir_intrinsic_instr *store =
				nir_intrinsic_instr_create(b->shader, nir_intrinsic_store_shared_ir3);

			nir_intrinsic_set_write_mask(store, MASK(intr->num_components));
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
ir3_nir_lower_to_explicit_io(nir_shader *shader, struct ir3_shader *s, unsigned topology)
{
	struct state state = { };

	build_primitive_map(shader, &state.map, &shader->outputs);
	memcpy(s->output_loc, state.map.loc, sizeof(s->output_loc));

	nir_function_impl *impl = nir_shader_get_entrypoint(shader);
	assert(impl);

	nir_builder b;
	nir_builder_init(&b, impl);
	b.cursor = nir_before_cf_list(&impl->body);

	if (s->type == MESA_SHADER_VERTEX && topology != IR3_TESS_NONE)
		state.header = nir_load_tcs_header_ir3(&b);
	else
		state.header = nir_load_gs_header_ir3(&b);

	nir_foreach_block_safe(block, impl)
		lower_vs_block(block, &b, &state);

	nir_metadata_preserve(impl, nir_metadata_block_index |
			nir_metadata_dominance);

	s->output_size = state.map.stride;
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

static nir_ssa_def *
build_tessfactor_base(nir_builder *b, gl_varying_slot slot, struct state *state)
{
	uint32_t inner_levels, outer_levels;
	switch (state->topology) {
	case IR3_TESS_TRIANGLES:
		inner_levels = 1;
		outer_levels = 3;
		break;
	case IR3_TESS_QUADS:
		inner_levels = 2;
		outer_levels = 4;
		break;
	case IR3_TESS_ISOLINES:
		inner_levels = 0;
		outer_levels = 2;
		break;
	default:
		unreachable("bad");
	}

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
	nir_foreach_instr_safe(instr, block) {
		if (instr->type != nir_instr_type_intrinsic)
			continue;

		nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

		switch (intr->intrinsic) {
		case nir_intrinsic_load_invocation_id:
			b->cursor = nir_before_instr(&intr->instr);

			nir_ssa_def *invocation_id = build_invocation_id(b, state);
			nir_ssa_def_rewrite_uses(&intr->dest.ssa,
									 nir_src_for_ssa(invocation_id));
			nir_instr_remove(&intr->instr);
			break;

		case nir_intrinsic_barrier:
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
			nir_variable *var = get_var(&b->shader->outputs, nir_intrinsic_base(intr));
			nir_ssa_def *offset = build_per_vertex_offset(b, state,
					intr->src[0].ssa, intr->src[1].ssa, var);

			replace_intrinsic(b, intr, nir_intrinsic_load_global_ir3, address, offset, NULL);
			break;
		}

		case nir_intrinsic_store_per_vertex_output: {
			// src[] = { value, vertex, offset }.

			b->cursor = nir_before_instr(&intr->instr);

			nir_ssa_def *value = intr->src[0].ssa;
			nir_ssa_def *address = nir_load_tess_param_base_ir3(b);
			nir_variable *var = get_var(&b->shader->outputs, nir_intrinsic_base(intr));
			nir_ssa_def *offset = build_per_vertex_offset(b, state,
					intr->src[1].ssa, intr->src[2].ssa, var);

			nir_intrinsic_instr *store =
				replace_intrinsic(b, intr, nir_intrinsic_store_global_ir3, value, address,
								  nir_iadd(b, offset, nir_imm_int(b, nir_intrinsic_component(intr))));

			nir_intrinsic_set_write_mask(store, nir_intrinsic_write_mask(intr));

			break;
		}

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

		case nir_intrinsic_load_tess_level_inner:
		case nir_intrinsic_load_tess_level_outer: {
			b->cursor = nir_before_instr(&intr->instr);

			gl_varying_slot slot;
			if (intr->intrinsic == nir_intrinsic_load_tess_level_inner)
				slot = VARYING_SLOT_TESS_LEVEL_INNER;
			else
				slot = VARYING_SLOT_TESS_LEVEL_OUTER;

			nir_ssa_def *address = nir_load_tess_factor_base_ir3(b);
			nir_ssa_def *offset = build_tessfactor_base(b, slot, state);

			replace_intrinsic(b, intr, nir_intrinsic_load_global_ir3, address, offset, NULL);
			break;
		}

		case nir_intrinsic_load_output: {
			// src[] = { offset }.

			nir_variable *var = get_var(&b->shader->outputs, nir_intrinsic_base(intr));

			b->cursor = nir_before_instr(&intr->instr);

			nir_ssa_def *address = nir_load_tess_param_base_ir3(b);
			nir_ssa_def *offset = build_patch_offset(b, state, intr->src[0].ssa, var);

			replace_intrinsic(b, intr, nir_intrinsic_load_global_ir3, address, offset, NULL);
			break;
		}

		case nir_intrinsic_store_output: {
			// src[] = { value, offset }.

			/* write patch output to bo */

			nir_variable *var = get_var(&b->shader->outputs, nir_intrinsic_base(intr));

			nir_ssa_def **levels = NULL;
			if (var->data.location == VARYING_SLOT_TESS_LEVEL_OUTER)
				levels = state->outer_levels;
			else if (var->data.location == VARYING_SLOT_TESS_LEVEL_INNER)
				levels = state->inner_levels;

			b->cursor = nir_before_instr(&intr->instr);

			if (levels) {
				for (int i = 0; i < 4; i++)
					if (nir_intrinsic_write_mask(intr) & (1 << i))
						levels[i] = nir_channel(b, intr->src[0].ssa, i);
				nir_instr_remove(&intr->instr);
			} else {
				nir_ssa_def *address = nir_load_tess_param_base_ir3(b);
				nir_ssa_def *offset = build_patch_offset(b, state, intr->src[1].ssa, var);

				debug_assert(nir_intrinsic_component(intr) == 0);

				nir_intrinsic_instr *store =
					replace_intrinsic(b, intr, nir_intrinsic_store_global_ir3,
							intr->src[0].ssa, address, offset);

				nir_intrinsic_set_write_mask(store, nir_intrinsic_write_mask(intr));
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
	nir_ssa_def *tessfactor_address = nir_load_tess_factor_base_ir3(b);
	nir_ssa_def *levels[2];

	/* Then emit the epilogue that actually writes out the tessellation levels
	 * to the BOs.
	 */
	switch (state->topology) {
	case IR3_TESS_TRIANGLES:
		levels[0] = nir_vec4(b, state->outer_levels[0], state->outer_levels[1],
				state->outer_levels[2], state->inner_levels[0]);
		levels[1] = NULL;
		break;
	case IR3_TESS_QUADS:
		levels[0] = nir_vec4(b, state->outer_levels[0], state->outer_levels[1],
				state->outer_levels[2], state->outer_levels[3]);
		levels[1] = nir_vec2(b, state->inner_levels[0], state->inner_levels[1]);
		break;
	case IR3_TESS_ISOLINES:
		levels[0] = nir_vec2(b, state->outer_levels[0], state->outer_levels[1]);
		levels[1] = NULL;
		break;
	default:
		unreachable("nope");
	}

	nir_ssa_def *offset = build_tessfactor_base(b, VARYING_SLOT_TESS_LEVEL_OUTER, state);

	nir_intrinsic_instr *store =
		nir_intrinsic_instr_create(b->shader, nir_intrinsic_store_global_ir3);

	store->src[0] = nir_src_for_ssa(levels[0]);
	store->src[1] = nir_src_for_ssa(tessfactor_address);
	store->src[2] = nir_src_for_ssa(offset);
	nir_builder_instr_insert(b, &store->instr);
	store->num_components = levels[0]->num_components;
	nir_intrinsic_set_write_mask(store, (1 << levels[0]->num_components) - 1);

	if (levels[1]) {
		store = nir_intrinsic_instr_create(b->shader, nir_intrinsic_store_global_ir3);
		offset = nir_iadd(b, offset, nir_imm_int(b, levels[0]->num_components));

		store->src[0] = nir_src_for_ssa(levels[1]);
		store->src[1] = nir_src_for_ssa(tessfactor_address);
		store->src[2] = nir_src_for_ssa(offset);
		nir_builder_instr_insert(b, &store->instr);
		store->num_components = levels[1]->num_components;
		nir_intrinsic_set_write_mask(store, (1 << levels[1]->num_components) - 1);
	}

	/* Finally, Insert endpatch instruction, maybe signalling the tess engine
	 * that another primitive is ready?
	 */

	nir_intrinsic_instr *end_patch =
		nir_intrinsic_instr_create(b->shader, nir_intrinsic_end_patch_ir3);
	nir_builder_instr_insert(b, &end_patch->instr);
}

void
ir3_nir_lower_tess_ctrl(nir_shader *shader, struct ir3_shader *s, unsigned topology)
{
	struct state state = { .topology = topology };

	if (shader_debug_enabled(shader->info.stage)) {
		fprintf(stderr, "NIR (before tess lowering) for %s shader:\n",
				_mesa_shader_stage_to_string(shader->info.stage));
		nir_print_shader(shader, stderr);
	}

	build_primitive_map(shader, &state.map, &shader->outputs);
	memcpy(s->output_loc, state.map.loc, sizeof(s->output_loc));
	s->output_size = state.map.stride;

	nir_function_impl *impl = nir_shader_get_entrypoint(shader);
	assert(impl);

	nir_builder b;
	nir_builder_init(&b, impl);
	b.cursor = nir_before_cf_list(&impl->body);

	state.header = nir_load_tcs_header_ir3(&b);

	nir_foreach_block_safe(block, impl)
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
	nir_foreach_instr_safe(instr, block) {
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
			nir_variable *var = get_var(&b->shader->inputs, nir_intrinsic_base(intr));
			nir_ssa_def *offset = build_per_vertex_offset(b, state,
					intr->src[0].ssa, intr->src[1].ssa, var);

			replace_intrinsic(b, intr, nir_intrinsic_load_global_ir3, address, offset, NULL);
			break;
		}

		case nir_intrinsic_load_tess_level_inner:
		case nir_intrinsic_load_tess_level_outer: {
				b->cursor = nir_before_instr(&intr->instr);

				gl_varying_slot slot;
				if (intr->intrinsic == nir_intrinsic_load_tess_level_inner)
					slot = VARYING_SLOT_TESS_LEVEL_INNER;
				else
					slot = VARYING_SLOT_TESS_LEVEL_OUTER;

				nir_ssa_def *address = nir_load_tess_factor_base_ir3(b);
				nir_ssa_def *offset = build_tessfactor_base(b, slot, state);

				/* Loading across a vec4 (16b) memory boundary is problematic
				 * if we don't use components from the second vec4.  The tess
				 * levels aren't guaranteed to be vec4 aligned and we don't
				 * know which levels are actually used, so we load each
				 * component individually.
				 */
				nir_ssa_def *levels[4];
				for (unsigned i = 0; i < intr->num_components; i++) {
					nir_intrinsic_instr *new_intr =
						nir_intrinsic_instr_create(b->shader, nir_intrinsic_load_global_ir3);

					new_intr->src[0] = nir_src_for_ssa(address);
					new_intr->src[1] = nir_src_for_ssa(nir_iadd(b, offset, nir_imm_int(b, i)));
					new_intr->num_components = 1;
					nir_ssa_dest_init(&new_intr->instr, &new_intr->dest, 1, 32, NULL);
					nir_builder_instr_insert(b, &new_intr->instr);
					levels[i] = &new_intr->dest.ssa;
				}

				nir_ssa_def *v = nir_vec(b, levels, intr->num_components);

				nir_ssa_def_rewrite_uses(&intr->dest.ssa, nir_src_for_ssa(v));

				nir_instr_remove(&intr->instr);
				break;
		}

		case nir_intrinsic_load_input: {
			// src[] = { offset }.

			nir_variable *var = get_var(&b->shader->inputs, nir_intrinsic_base(intr));

			debug_assert(var->data.patch);

			b->cursor = nir_before_instr(&intr->instr);

			nir_ssa_def *address = nir_load_tess_param_base_ir3(b);
			nir_ssa_def *offset = build_patch_offset(b, state, intr->src[0].ssa, var);

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
	build_primitive_map(shader, &state.map, &shader->inputs);

	nir_function_impl *impl = nir_shader_get_entrypoint(shader);
	assert(impl);

	nir_builder b;
	nir_builder_init(&b, impl);

	nir_foreach_block_safe(block, impl)
		lower_tess_eval_block(block, &b, &state);

	nir_metadata_preserve(impl, 0);
}

static void
lower_gs_block(nir_block *block, nir_builder *b, struct state *state)
{
	nir_intrinsic_instr *outputs[32] = {};

	nir_foreach_instr_safe(instr, block) {
		if (instr->type != nir_instr_type_intrinsic)
			continue;

		nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

		switch (intr->intrinsic) {
		case nir_intrinsic_store_output: {
			// src[] = { value, offset }.

			uint32_t loc = nir_intrinsic_base(intr);
			outputs[loc] = intr;
			break;
		}

		case nir_intrinsic_end_primitive: {
			b->cursor = nir_before_instr(&intr->instr);
			nir_store_var(b, state->vertex_flags_var, nir_imm_int(b, 4), 0x1);
			nir_instr_remove(&intr->instr);
			break;
		}

		case nir_intrinsic_emit_vertex: {

			/* Load the vertex count */
			b->cursor = nir_before_instr(&intr->instr);
			nir_ssa_def *count = nir_load_var(b, state->vertex_count_var);

			nir_push_if(b, nir_ieq(b, count, local_thread_id(b)));

			for (uint32_t i = 0; i < ARRAY_SIZE(outputs); i++) {
				if (outputs[i]) {
					nir_store_var(b, state->output_vars[i],
							outputs[i]->src[0].ssa,
							(1 << outputs[i]->num_components) - 1);

					nir_instr_remove(&outputs[i]->instr);
				}
				outputs[i] = NULL;
			}

			nir_instr_remove(&intr->instr);

			nir_store_var(b, state->emitted_vertex_var,
					nir_iadd(b, nir_load_var(b, state->emitted_vertex_var), nir_imm_int(b, 1)), 0x1);

			nir_store_var(b, state->vertex_flags_out,
					nir_load_var(b, state->vertex_flags_var), 0x1);

			nir_pop_if(b, NULL);

			/* Increment the vertex count by 1 */
			nir_store_var(b, state->vertex_count_var,
					nir_iadd(b, count, nir_imm_int(b, 1)), 0x1); /* .x */
			nir_store_var(b, state->vertex_flags_var, nir_imm_int(b, 0), 0x1);

			break;
		}

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

static void
emit_store_outputs(nir_builder *b, struct state *state)
{
	/* This also stores the internally added vertex_flags output. */

	for (uint32_t i = 0; i < ARRAY_SIZE(state->output_vars); i++) {
		if (!state->output_vars[i])
			continue;

		nir_intrinsic_instr *store =
			nir_intrinsic_instr_create(b->shader, nir_intrinsic_store_output);

		nir_intrinsic_set_base(store, i);
		store->src[0] = nir_src_for_ssa(nir_load_var(b, state->output_vars[i]));
		store->src[1] = nir_src_for_ssa(nir_imm_int(b, 0));
		store->num_components = store->src[0].ssa->num_components;

		nir_builder_instr_insert(b, &store->instr);
	}
}

static void
clean_up_split_vars(nir_shader *shader, struct exec_list *list)
{
	uint32_t components[32] = {};

	nir_foreach_variable(var, list) {
		uint32_t mask =
			((1 << glsl_get_components(glsl_without_array(var->type))) - 1) << var->data.location_frac;
		components[var->data.driver_location] |= mask;
	}

	nir_foreach_variable_safe(var, list) {
		uint32_t mask =
			((1 << glsl_get_components(glsl_without_array(var->type))) - 1) << var->data.location_frac;
		bool subset =
			(components[var->data.driver_location] | mask) != mask;
		if (subset)
			exec_node_remove(&var->node);
	}
}

void
ir3_nir_lower_gs(nir_shader *shader, struct ir3_shader *s)
{
	struct state state = { };

	if (shader_debug_enabled(shader->info.stage)) {
		fprintf(stderr, "NIR (before gs lowering):\n");
		nir_print_shader(shader, stderr);
	}

	clean_up_split_vars(shader, &shader->inputs);
	clean_up_split_vars(shader, &shader->outputs);

	build_primitive_map(shader, &state.map, &shader->inputs);

	uint32_t loc = 0;
	nir_foreach_variable(var, &shader->outputs) {
		uint32_t end = var->data.driver_location + glsl_count_attribute_slots(var->type, false);
		loc = MAX2(loc, end);
	}

	state.vertex_flags_out = nir_variable_create(shader, nir_var_shader_out,
			glsl_uint_type(), "vertex_flags");
	state.vertex_flags_out->data.driver_location = loc;
	state.vertex_flags_out->data.location = VARYING_SLOT_GS_VERTEX_FLAGS_IR3;

	nir_function_impl *impl = nir_shader_get_entrypoint(shader);
	assert(impl);

	nir_builder b;
	nir_builder_init(&b, impl);
	b.cursor = nir_before_cf_list(&impl->body);

	state.header = nir_load_gs_header_ir3(&b);

	nir_foreach_variable(var, &shader->outputs) {
		state.output_vars[var->data.driver_location] = 
			nir_local_variable_create(impl, var->type,
					ralloc_asprintf(var, "%s:gs-temp", var->name));
	}

	state.vertex_count_var =
		nir_local_variable_create(impl, glsl_uint_type(), "vertex_count");
	state.emitted_vertex_var =
		nir_local_variable_create(impl, glsl_uint_type(), "emitted_vertex");
	state.vertex_flags_var =
		nir_local_variable_create(impl, glsl_uint_type(), "vertex_flags");
	state.vertex_flags_out = state.output_vars[state.vertex_flags_out->data.driver_location];

	/* initialize to 0 */
	b.cursor = nir_before_cf_list(&impl->body);
	nir_store_var(&b, state.vertex_count_var, nir_imm_int(&b, 0), 0x1);
	nir_store_var(&b, state.emitted_vertex_var, nir_imm_int(&b, 0), 0x1);
	nir_store_var(&b, state.vertex_flags_var, nir_imm_int(&b, 4), 0x1);

	nir_foreach_block_safe(block, impl)
		lower_gs_block(block, &b, &state);

	set_foreach(impl->end_block->predecessors, block_entry) {
		struct nir_block *block = (void *)block_entry->key;
		b.cursor = nir_after_block_before_jump(block);

		nir_intrinsic_instr *discard_if =
			nir_intrinsic_instr_create(b.shader, nir_intrinsic_discard_if);

		nir_ssa_def *cond = nir_ieq(&b, nir_load_var(&b, state.emitted_vertex_var), nir_imm_int(&b, 0));

		discard_if->src[0] = nir_src_for_ssa(cond);

		nir_builder_instr_insert(&b, &discard_if->instr);

		emit_store_outputs(&b, &state);
	}

	nir_metadata_preserve(impl, 0);

	if (shader_debug_enabled(shader->info.stage)) {
		fprintf(stderr, "NIR (after gs lowering):\n");
		nir_print_shader(shader, stderr);
	}
}
