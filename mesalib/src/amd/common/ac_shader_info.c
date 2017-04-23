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

static void mark_sampler_desc(nir_variable *var, struct ac_shader_info *info)
{
	info->desc_set_used_mask = (1 << var->data.descriptor_set);
}

static void
gather_intrinsic_info(nir_intrinsic_instr *instr, struct ac_shader_info *info)
{
	switch (instr->intrinsic) {
	case nir_intrinsic_interp_var_at_sample:
		info->ps.needs_sample_positions = true;
		break;
	case nir_intrinsic_load_draw_id:
		info->vs.needs_draw_id = true;
		break;
	case nir_intrinsic_load_num_work_groups:
		info->cs.grid_components_used = instr->num_components;
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
	case nir_intrinsic_image_size:
		mark_sampler_desc(instr->variables[0]->var, info);
		break;
	default:
		break;
	}
}

static void
gather_tex_info(nir_tex_instr *instr, struct ac_shader_info *info)
{
	if (instr->sampler)
		mark_sampler_desc(instr->sampler->var, info);
	if (instr->texture)
		mark_sampler_desc(instr->texture->var, info);
}

static void
gather_info_block(nir_block *block, struct ac_shader_info *info)
{
	nir_foreach_instr(instr, block) {
		switch (instr->type) {
		case nir_instr_type_intrinsic:
			gather_intrinsic_info(nir_instr_as_intrinsic(instr), info);
			break;
		case nir_instr_type_tex:
			gather_tex_info(nir_instr_as_tex(instr), info);
			break;
		default:
			break;
		}
	}
}

static void
gather_info_input_decl(nir_shader *nir,
		       const struct ac_nir_compiler_options *options,
		       nir_variable *var,
		       struct ac_shader_info *info)
{
	switch (nir->stage) {
	case MESA_SHADER_VERTEX:
		info->vs.has_vertex_buffers = true;
		break;
	default:
		break;
	}
}

void
ac_nir_shader_info_pass(struct nir_shader *nir,
			const struct ac_nir_compiler_options *options,
			struct ac_shader_info *info)
{
	struct nir_function *func = (struct nir_function *)exec_list_get_head(&nir->functions);

	info->needs_push_constants = true;
	if (!options->layout)
		info->needs_push_constants = false;
	else if (!options->layout->push_constant_size &&
		 !options->layout->dynamic_offset_count)
		info->needs_push_constants = false;

	nir_foreach_variable(variable, &nir->inputs)
		gather_info_input_decl(nir, options, variable, info);

	nir_foreach_block(block, func->impl) {
		gather_info_block(block, info);
	}
}
