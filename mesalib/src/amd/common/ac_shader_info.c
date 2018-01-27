/*
 * Copyright © 2017 Red Hat
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
#include "ac_shader_info.h"
#include "ac_nir_to_llvm.h"

static void mark_sampler_desc(const nir_variable *var,
			      struct ac_shader_info *info)
{
	info->desc_set_used_mask = (1 << var->data.descriptor_set);
}

static void
gather_intrinsic_info(const nir_shader *nir, const nir_intrinsic_instr *instr,
		      struct ac_shader_info *info)
{
	switch (instr->intrinsic) {
	case nir_intrinsic_interp_var_at_sample:
		info->ps.needs_sample_positions = true;
		break;
	case nir_intrinsic_load_draw_id:
		info->vs.needs_draw_id = true;
		break;
	case nir_intrinsic_load_instance_id:
		info->vs.needs_instance_id = true;
		break;
	case nir_intrinsic_load_num_work_groups:
		info->cs.uses_grid_size = true;
		break;
	case nir_intrinsic_load_local_invocation_id:
	case nir_intrinsic_load_work_group_id: {
		unsigned mask = nir_ssa_def_components_read(&instr->dest.ssa);
		while (mask) {
			unsigned i = u_bit_scan(&mask);

			if (instr->intrinsic == nir_intrinsic_load_work_group_id)
				info->cs.uses_block_id[i] = true;
			else
				info->cs.uses_thread_id[i] = true;
		}
		break;
	}
	case nir_intrinsic_load_local_invocation_index:
		info->cs.uses_local_invocation_idx = true;
		break;
	case nir_intrinsic_load_sample_id:
		info->ps.force_persample = true;
		break;
	case nir_intrinsic_load_sample_pos:
		info->ps.force_persample = true;
		break;
	case nir_intrinsic_load_view_index:
		info->needs_multiview_view_index = true;
		break;
	case nir_intrinsic_load_invocation_id:
		info->uses_invocation_id = true;
		break;
	case nir_intrinsic_load_primitive_id:
		info->uses_prim_id = true;
		break;
	case nir_intrinsic_load_push_constant:
		info->loads_push_constants = true;
		break;
	case nir_intrinsic_vulkan_resource_index:
		info->desc_set_used_mask |= (1 << nir_intrinsic_desc_set(instr));
		break;
	case nir_intrinsic_image_load:
	case nir_intrinsic_image_store:
	case nir_intrinsic_image_atomic_add:
	case nir_intrinsic_image_atomic_min:
	case nir_intrinsic_image_atomic_max:
	case nir_intrinsic_image_atomic_and:
	case nir_intrinsic_image_atomic_or:
	case nir_intrinsic_image_atomic_xor:
	case nir_intrinsic_image_atomic_exchange:
	case nir_intrinsic_image_atomic_comp_swap:
	case nir_intrinsic_image_size: {
		const struct glsl_type *type = instr->variables[0]->var->type;
		if(instr->variables[0]->deref.child)
			type = instr->variables[0]->deref.child->type;

		enum glsl_sampler_dim dim = glsl_get_sampler_dim(type);
		if (dim == GLSL_SAMPLER_DIM_SUBPASS ||
		    dim == GLSL_SAMPLER_DIM_SUBPASS_MS)
			info->ps.uses_input_attachments = true;
		mark_sampler_desc(instr->variables[0]->var, info);

		if (nir_intrinsic_image_store ||
		    nir_intrinsic_image_atomic_add ||
		    nir_intrinsic_image_atomic_min ||
		    nir_intrinsic_image_atomic_max ||
		    nir_intrinsic_image_atomic_and ||
		    nir_intrinsic_image_atomic_or ||
		    nir_intrinsic_image_atomic_xor ||
		    nir_intrinsic_image_atomic_exchange ||
		    nir_intrinsic_image_atomic_comp_swap) {
			if (nir->info.stage == MESA_SHADER_FRAGMENT)
				info->ps.writes_memory = true;
		}
		break;
	}
	case nir_intrinsic_store_ssbo:
	case nir_intrinsic_ssbo_atomic_add:
	case nir_intrinsic_ssbo_atomic_imin:
	case nir_intrinsic_ssbo_atomic_umin:
	case nir_intrinsic_ssbo_atomic_imax:
	case nir_intrinsic_ssbo_atomic_umax:
	case nir_intrinsic_ssbo_atomic_and:
	case nir_intrinsic_ssbo_atomic_or:
	case nir_intrinsic_ssbo_atomic_xor:
	case nir_intrinsic_ssbo_atomic_exchange:
	case nir_intrinsic_ssbo_atomic_comp_swap:
		if (nir->info.stage == MESA_SHADER_FRAGMENT)
			info->ps.writes_memory = true;
		break;
	default:
		break;
	}
}

static void
gather_tex_info(const nir_shader *nir, const nir_tex_instr *instr,
		struct ac_shader_info *info)
{
	if (instr->sampler)
		mark_sampler_desc(instr->sampler->var, info);
	if (instr->texture)
		mark_sampler_desc(instr->texture->var, info);
}

static void
gather_info_block(const nir_shader *nir, const nir_block *block,
		  struct ac_shader_info *info)
{
	nir_foreach_instr(instr, block) {
		switch (instr->type) {
		case nir_instr_type_intrinsic:
			gather_intrinsic_info(nir, nir_instr_as_intrinsic(instr), info);
			break;
		case nir_instr_type_tex:
			gather_tex_info(nir, nir_instr_as_tex(instr), info);
			break;
		default:
			break;
		}
	}
}

static void
gather_info_input_decl(const nir_shader *nir, const nir_variable *var,
		       struct ac_shader_info *info)
{
	switch (nir->info.stage) {
	case MESA_SHADER_VERTEX:
		info->vs.has_vertex_buffers = true;
		break;
	default:
		break;
	}
}

void
ac_nir_shader_info_pass(const struct nir_shader *nir,
			const struct ac_nir_compiler_options *options,
			struct ac_shader_info *info)
{
	struct nir_function *func =
		(struct nir_function *)exec_list_get_head_const(&nir->functions);

	if (options->layout->dynamic_offset_count)
		info->loads_push_constants = true;

	nir_foreach_variable(variable, &nir->inputs)
		gather_info_input_decl(nir, variable, info);

	nir_foreach_block(block, func->impl) {
		gather_info_block(nir, block, info);
	}
}
