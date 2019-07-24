/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
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

#include "util/mesa-sha1.h"
#include "util/u_atomic.h"
#include "radv_debug.h"
#include "radv_private.h"
#include "radv_shader.h"
#include "radv_shader_helper.h"
#include "nir/nir.h"
#include "nir/nir_builder.h"
#include "spirv/nir_spirv.h"

#include <llvm-c/Core.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/Support.h>

#include "sid.h"
#include "ac_binary.h"
#include "ac_llvm_util.h"
#include "ac_nir_to_llvm.h"
#include "ac_rtld.h"
#include "vk_format.h"
#include "util/debug.h"
#include "ac_exp_param.h"

#include "util/string_buffer.h"

static const struct nir_shader_compiler_options nir_options = {
	.vertex_id_zero_based = true,
	.lower_scmp = true,
	.lower_flrp16 = true,
	.lower_flrp32 = true,
	.lower_flrp64 = true,
	.lower_device_index_to_zero = true,
	.lower_fsat = true,
	.lower_fdiv = true,
	.lower_bitfield_insert_to_bitfield_select = true,
	.lower_bitfield_extract = true,
	.lower_sub = true,
	.lower_pack_snorm_2x16 = true,
	.lower_pack_snorm_4x8 = true,
	.lower_pack_unorm_2x16 = true,
	.lower_pack_unorm_4x8 = true,
	.lower_unpack_snorm_2x16 = true,
	.lower_unpack_snorm_4x8 = true,
	.lower_unpack_unorm_2x16 = true,
	.lower_unpack_unorm_4x8 = true,
	.lower_extract_byte = true,
	.lower_extract_word = true,
	.lower_ffma = true,
	.lower_fpow = true,
	.lower_mul_2x32_64 = true,
	.lower_rotate = true,
	.max_unroll_iterations = 32,
	.use_interpolated_input_intrinsics = true,
};

VkResult radv_CreateShaderModule(
	VkDevice                                    _device,
	const VkShaderModuleCreateInfo*             pCreateInfo,
	const VkAllocationCallbacks*                pAllocator,
	VkShaderModule*                             pShaderModule)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	struct radv_shader_module *module;

	assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
	assert(pCreateInfo->flags == 0);

	module = vk_alloc2(&device->alloc, pAllocator,
			     sizeof(*module) + pCreateInfo->codeSize, 8,
			     VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
	if (module == NULL)
		return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

	module->nir = NULL;
	module->size = pCreateInfo->codeSize;
	memcpy(module->data, pCreateInfo->pCode, module->size);

	_mesa_sha1_compute(module->data, module->size, module->sha1);

	*pShaderModule = radv_shader_module_to_handle(module);

	return VK_SUCCESS;
}

void radv_DestroyShaderModule(
	VkDevice                                    _device,
	VkShaderModule                              _module,
	const VkAllocationCallbacks*                pAllocator)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	RADV_FROM_HANDLE(radv_shader_module, module, _module);

	if (!module)
		return;

	vk_free2(&device->alloc, pAllocator, module);
}

void
radv_optimize_nir(struct nir_shader *shader, bool optimize_conservatively,
                  bool allow_copies)
{
        bool progress;
        unsigned lower_flrp =
                (shader->options->lower_flrp16 ? 16 : 0) |
                (shader->options->lower_flrp32 ? 32 : 0) |
                (shader->options->lower_flrp64 ? 64 : 0);

        do {
                progress = false;

		NIR_PASS(progress, shader, nir_split_array_vars, nir_var_function_temp);
		NIR_PASS(progress, shader, nir_shrink_vec_array_vars, nir_var_function_temp);

                NIR_PASS_V(shader, nir_lower_vars_to_ssa);
		NIR_PASS_V(shader, nir_lower_pack);

		if (allow_copies) {
			/* Only run this pass in the first call to
			 * radv_optimize_nir.  Later calls assume that we've
			 * lowered away any copy_deref instructions and we
			 *  don't want to introduce any more.
			*/
			NIR_PASS(progress, shader, nir_opt_find_array_copies);
		}

		NIR_PASS(progress, shader, nir_opt_copy_prop_vars);
		NIR_PASS(progress, shader, nir_opt_dead_write_vars);

                NIR_PASS_V(shader, nir_lower_alu_to_scalar, NULL);
                NIR_PASS_V(shader, nir_lower_phis_to_scalar);

                NIR_PASS(progress, shader, nir_copy_prop);
                NIR_PASS(progress, shader, nir_opt_remove_phis);
                NIR_PASS(progress, shader, nir_opt_dce);
                if (nir_opt_trivial_continues(shader)) {
                        progress = true;
                        NIR_PASS(progress, shader, nir_copy_prop);
			NIR_PASS(progress, shader, nir_opt_remove_phis);
                        NIR_PASS(progress, shader, nir_opt_dce);
                }
                NIR_PASS(progress, shader, nir_opt_if, true);
                NIR_PASS(progress, shader, nir_opt_dead_cf);
                NIR_PASS(progress, shader, nir_opt_cse);
                NIR_PASS(progress, shader, nir_opt_peephole_select, 8, true, true);
                NIR_PASS(progress, shader, nir_opt_constant_folding);
                NIR_PASS(progress, shader, nir_opt_algebraic);

                if (lower_flrp != 0) {
                        bool lower_flrp_progress = false;
                        NIR_PASS(lower_flrp_progress,
                                 shader,
                                 nir_lower_flrp,
                                 lower_flrp,
                                 false /* always_precise */,
                                 shader->options->lower_ffma);
                        if (lower_flrp_progress) {
                                NIR_PASS(progress, shader,
                                         nir_opt_constant_folding);
                                progress = true;
                        }

                        /* Nothing should rematerialize any flrps, so we only
                         * need to do this lowering once.
                         */
                        lower_flrp = 0;
                }

                NIR_PASS(progress, shader, nir_opt_undef);
                if (shader->options->max_unroll_iterations) {
                        NIR_PASS(progress, shader, nir_opt_loop_unroll, 0);
                }
        } while (progress && !optimize_conservatively);

	NIR_PASS(progress, shader, nir_opt_conditional_discard);
        NIR_PASS(progress, shader, nir_opt_shrink_load);
        NIR_PASS(progress, shader, nir_opt_move_load_ubo);
}

nir_shader *
radv_shader_compile_to_nir(struct radv_device *device,
			   struct radv_shader_module *module,
			   const char *entrypoint_name,
			   gl_shader_stage stage,
			   const VkSpecializationInfo *spec_info,
			   const VkPipelineCreateFlags flags,
			   const struct radv_pipeline_layout *layout)
{
	nir_shader *nir;
	if (module->nir) {
		/* Some things such as our meta clear/blit code will give us a NIR
		 * shader directly.  In that case, we just ignore the SPIR-V entirely
		 * and just use the NIR shader */
		nir = module->nir;
		nir->options = &nir_options;
		nir_validate_shader(nir, "in internal shader");

		assert(exec_list_length(&nir->functions) == 1);
	} else {
		uint32_t *spirv = (uint32_t *) module->data;
		assert(module->size % 4 == 0);

		if (device->instance->debug_flags & RADV_DEBUG_DUMP_SPIRV)
			radv_print_spirv(spirv, module->size, stderr);

		uint32_t num_spec_entries = 0;
		struct nir_spirv_specialization *spec_entries = NULL;
		if (spec_info && spec_info->mapEntryCount > 0) {
			num_spec_entries = spec_info->mapEntryCount;
			spec_entries = malloc(num_spec_entries * sizeof(*spec_entries));
			for (uint32_t i = 0; i < num_spec_entries; i++) {
				VkSpecializationMapEntry entry = spec_info->pMapEntries[i];
				const void *data = spec_info->pData + entry.offset;
				assert(data + entry.size <= spec_info->pData + spec_info->dataSize);

				spec_entries[i].id = spec_info->pMapEntries[i].constantID;
				if (spec_info->dataSize == 8)
					spec_entries[i].data64 = *(const uint64_t *)data;
				else
					spec_entries[i].data32 = *(const uint32_t *)data;
			}
		}
		const struct spirv_to_nir_options spirv_options = {
			.lower_ubo_ssbo_access_to_offsets = true,
			.caps = {
				.amd_gcn_shader = true,
				.amd_shader_ballot = device->instance->perftest_flags & RADV_PERFTEST_SHADER_BALLOT,
				.amd_trinary_minmax = true,
				.derivative_group = true,
				.descriptor_array_dynamic_indexing = true,
				.descriptor_array_non_uniform_indexing = true,
				.descriptor_indexing = true,
				.device_group = true,
				.draw_parameters = true,
				.float16 = true,
				.float64 = true,
				.geometry_streams = true,
				.image_read_without_format = true,
				.image_write_without_format = true,
				.int8 = true,
				.int16 = true,
				.int64 = true,
				.int64_atomics = true,
				.multiview = true,
				.physical_storage_buffer_address = true,
				.post_depth_coverage = true,
				.runtime_descriptor_array = true,
				.shader_viewport_index_layer = true,
				.stencil_export = true,
				.storage_8bit = true,
				.storage_16bit = true,
				.storage_image_ms = true,
				.subgroup_arithmetic = true,
				.subgroup_ballot = true,
				.subgroup_basic = true,
				.subgroup_quad = true,
				.subgroup_shuffle = true,
				.subgroup_vote = true,
				.tessellation = true,
				.transform_feedback = true,
				.variable_pointers = true,
			},
			.ubo_addr_format = nir_address_format_32bit_index_offset,
			.ssbo_addr_format = nir_address_format_32bit_index_offset,
			.phys_ssbo_addr_format = nir_address_format_64bit_global,
			.push_const_addr_format = nir_address_format_logical,
			.shared_addr_format = nir_address_format_32bit_offset,
			.frag_coord_is_sysval = true,
		};
		nir = spirv_to_nir(spirv, module->size / 4,
				   spec_entries, num_spec_entries,
				   stage, entrypoint_name,
				   &spirv_options, &nir_options);
		assert(nir->info.stage == stage);
		nir_validate_shader(nir, "after spirv_to_nir");

		free(spec_entries);

		/* We have to lower away local constant initializers right before we
		 * inline functions.  That way they get properly initialized at the top
		 * of the function and not at the top of its caller.
		 */
		NIR_PASS_V(nir, nir_lower_constant_initializers, nir_var_function_temp);
		NIR_PASS_V(nir, nir_lower_returns);
		NIR_PASS_V(nir, nir_inline_functions);
		NIR_PASS_V(nir, nir_opt_deref);

		/* Pick off the single entrypoint that we want */
		foreach_list_typed_safe(nir_function, func, node, &nir->functions) {
			if (func->is_entrypoint)
				func->name = ralloc_strdup(func, "main");
			else
				exec_node_remove(&func->node);
		}
		assert(exec_list_length(&nir->functions) == 1);

		/* Make sure we lower constant initializers on output variables so that
		 * nir_remove_dead_variables below sees the corresponding stores
		 */
		NIR_PASS_V(nir, nir_lower_constant_initializers, nir_var_shader_out);

		/* Now that we've deleted all but the main function, we can go ahead and
		 * lower the rest of the constant initializers.
		 */
		NIR_PASS_V(nir, nir_lower_constant_initializers, ~0);

		/* Split member structs.  We do this before lower_io_to_temporaries so that
		 * it doesn't lower system values to temporaries by accident.
		 */
		NIR_PASS_V(nir, nir_split_var_copies);
		NIR_PASS_V(nir, nir_split_per_member_structs);

		if (nir->info.stage == MESA_SHADER_FRAGMENT)
			NIR_PASS_V(nir, nir_lower_input_attachments, true);

		NIR_PASS_V(nir, nir_remove_dead_variables,
		           nir_var_shader_in | nir_var_shader_out | nir_var_system_value);

		NIR_PASS_V(nir, nir_lower_system_values);
		NIR_PASS_V(nir, nir_lower_clip_cull_distance_arrays);
		NIR_PASS_V(nir, radv_nir_lower_ycbcr_textures, layout);
	}

	/* Vulkan uses the separate-shader linking model */
	nir->info.separate_shader = true;

	nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));

	static const nir_lower_tex_options tex_options = {
	  .lower_txp = ~0,
	  .lower_tg4_offsets = true,
	};

	nir_lower_tex(nir, &tex_options);

	nir_lower_vars_to_ssa(nir);

	if (nir->info.stage == MESA_SHADER_VERTEX ||
	    nir->info.stage == MESA_SHADER_GEOMETRY ||
	    nir->info.stage == MESA_SHADER_FRAGMENT) {
		NIR_PASS_V(nir, nir_lower_io_to_temporaries,
			   nir_shader_get_entrypoint(nir), true, true);
	} else if (nir->info.stage == MESA_SHADER_TESS_EVAL) {
		NIR_PASS_V(nir, nir_lower_io_to_temporaries,
			   nir_shader_get_entrypoint(nir), true, false);
	}

	nir_split_var_copies(nir);

	nir_lower_global_vars_to_local(nir);
	nir_remove_dead_variables(nir, nir_var_function_temp);
	nir_lower_subgroups(nir, &(struct nir_lower_subgroups_options) {
			.subgroup_size = 64,
			.ballot_bit_size = 64,
			.lower_to_scalar = 1,
			.lower_subgroup_masks = 1,
			.lower_shuffle = 1,
			.lower_shuffle_to_32bit = 1,
			.lower_vote_eq_to_ballot = 1,
		});

	nir_lower_load_const_to_scalar(nir);

	if (!(flags & VK_PIPELINE_CREATE_DISABLE_OPTIMIZATION_BIT))
		radv_optimize_nir(nir, false, true);

	/* We call nir_lower_var_copies() after the first radv_optimize_nir()
	 * to remove any copies introduced by nir_opt_find_array_copies().
	 */
	nir_lower_var_copies(nir);

	/* Indirect lowering must be called after the radv_optimize_nir() loop
	 * has been called at least once. Otherwise indirect lowering can
	 * bloat the instruction count of the loop and cause it to be
	 * considered too large for unrolling.
	 */
	ac_lower_indirect_derefs(nir, device->physical_device->rad_info.chip_class);
	radv_optimize_nir(nir, flags & VK_PIPELINE_CREATE_DISABLE_OPTIMIZATION_BIT, false);

	return nir;
}

static void mark_16bit_fs_input(struct radv_shader_variant_info *shader_info,
                                const struct glsl_type *type,
                                int location)
{
	if (glsl_type_is_scalar(type) || glsl_type_is_vector(type) || glsl_type_is_matrix(type)) {
		unsigned attrib_count = glsl_count_attribute_slots(type, false);
		if (glsl_type_is_16bit(type)) {
			shader_info->fs.float16_shaded_mask |= ((1ull << attrib_count) - 1) << location;
		}
	} else if (glsl_type_is_array(type)) {
		unsigned stride = glsl_count_attribute_slots(glsl_get_array_element(type), false);
		for (unsigned i = 0; i < glsl_get_length(type); ++i) {
			mark_16bit_fs_input(shader_info, glsl_get_array_element(type), location + i * stride);
		}
	} else {
		assert(glsl_type_is_struct_or_ifc(type));
		for (unsigned i = 0; i < glsl_get_length(type); i++) {
			mark_16bit_fs_input(shader_info, glsl_get_struct_field(type, i), location);
			location += glsl_count_attribute_slots(glsl_get_struct_field(type, i), false);
		}
	}
}

static void
handle_fs_input_decl(struct radv_shader_variant_info *shader_info,
		     struct nir_variable *variable)
{
	unsigned attrib_count = glsl_count_attribute_slots(variable->type, false);

	if (variable->data.compact) {
		unsigned component_count = variable->data.location_frac +
		                           glsl_get_length(variable->type);
		attrib_count = (component_count + 3) / 4;
	} else {
		mark_16bit_fs_input(shader_info, variable->type,
				    variable->data.driver_location);
	}

	uint64_t mask = ((1ull << attrib_count) - 1);

	if (variable->data.interpolation == INTERP_MODE_FLAT)
		shader_info->fs.flat_shaded_mask |= mask << variable->data.driver_location;

	if (variable->data.location >= VARYING_SLOT_VAR0)
		shader_info->fs.input_mask |= mask << (variable->data.location - VARYING_SLOT_VAR0);
}

static int
type_size_vec4(const struct glsl_type *type, bool bindless)
{
	return glsl_count_attribute_slots(type, false);
}

static nir_variable *
find_layer_in_var(nir_shader *nir)
{
	nir_foreach_variable(var, &nir->inputs) {
		if (var->data.location == VARYING_SLOT_LAYER) {
			return var;
		}
	}

	nir_variable *var =
		nir_variable_create(nir, nir_var_shader_in, glsl_int_type(), "layer id");
	var->data.location = VARYING_SLOT_LAYER;
	var->data.interpolation = INTERP_MODE_FLAT;
	return var;
}

/* We use layered rendering to implement multiview, which means we need to map
 * view_index to gl_Layer. The attachment lowering also uses needs to know the
 * layer so that it can sample from the correct layer. The code generates a
 * load from the layer_id sysval, but since we don't have a way to get at this
 * information from the fragment shader, we also need to lower this to the
 * gl_Layer varying.  This pass lowers both to a varying load from the LAYER
 * slot, before lowering io, so that nir_assign_var_locations() will give the
 * LAYER varying the correct driver_location.
 */

static bool
lower_view_index(nir_shader *nir)
{
	bool progress = false;
	nir_function_impl *entry = nir_shader_get_entrypoint(nir);
	nir_builder b;
	nir_builder_init(&b, entry);
	
	nir_variable *layer = NULL;
	nir_foreach_block(block, entry) {
		nir_foreach_instr_safe(instr, block) {
			if (instr->type != nir_instr_type_intrinsic)
				continue;

			nir_intrinsic_instr *load = nir_instr_as_intrinsic(instr);
			if (load->intrinsic != nir_intrinsic_load_view_index &&
			    load->intrinsic != nir_intrinsic_load_layer_id)
				continue;

			if (!layer)
				layer = find_layer_in_var(nir);

			b.cursor = nir_before_instr(instr);
			nir_ssa_def *def = nir_load_var(&b, layer);
			nir_ssa_def_rewrite_uses(&load->dest.ssa,
						 nir_src_for_ssa(def));

			nir_instr_remove(instr);
			progress = true;
		}
	}

	return progress;
}

/* Gather information needed to setup the vs<->ps linking registers in
 * radv_pipeline_generate_ps_inputs().
 */

static void
handle_fs_inputs(nir_shader *nir, struct radv_shader_variant_info *shader_info)
{
	shader_info->fs.num_interp = nir->num_inputs;
	
	nir_foreach_variable(variable, &nir->inputs)
		handle_fs_input_decl(shader_info, variable);
}

static void
lower_fs_io(nir_shader *nir, struct radv_shader_variant_info *shader_info)
{
	NIR_PASS_V(nir, lower_view_index);
	nir_assign_io_var_locations(&nir->inputs, &nir->num_inputs,
				    MESA_SHADER_FRAGMENT);

	handle_fs_inputs(nir, shader_info);

	NIR_PASS_V(nir, nir_lower_io, nir_var_shader_in, type_size_vec4, 0);

	/* This pass needs actual constants */
	nir_opt_constant_folding(nir);

	NIR_PASS_V(nir, nir_io_add_const_offset_to_base, nir_var_shader_in);
}


void *
radv_alloc_shader_memory(struct radv_device *device,
			 struct radv_shader_variant *shader)
{
	mtx_lock(&device->shader_slab_mutex);
	list_for_each_entry(struct radv_shader_slab, slab, &device->shader_slabs, slabs) {
		uint64_t offset = 0;
		list_for_each_entry(struct radv_shader_variant, s, &slab->shaders, slab_list) {
			if (s->bo_offset - offset >= shader->code_size) {
				shader->bo = slab->bo;
				shader->bo_offset = offset;
				list_addtail(&shader->slab_list, &s->slab_list);
				mtx_unlock(&device->shader_slab_mutex);
				return slab->ptr + offset;
			}
			offset = align_u64(s->bo_offset + s->code_size, 256);
		}
		if (slab->size - offset >= shader->code_size) {
			shader->bo = slab->bo;
			shader->bo_offset = offset;
			list_addtail(&shader->slab_list, &slab->shaders);
			mtx_unlock(&device->shader_slab_mutex);
			return slab->ptr + offset;
		}
	}

	mtx_unlock(&device->shader_slab_mutex);
	struct radv_shader_slab *slab = calloc(1, sizeof(struct radv_shader_slab));

	slab->size = 256 * 1024;
	slab->bo = device->ws->buffer_create(device->ws, slab->size, 256,
	                                     RADEON_DOMAIN_VRAM,
					     RADEON_FLAG_NO_INTERPROCESS_SHARING |
					     (device->physical_device->cpdma_prefetch_writes_memory ?
					             0 : RADEON_FLAG_READ_ONLY),
					     RADV_BO_PRIORITY_SHADER);
	slab->ptr = (char*)device->ws->buffer_map(slab->bo);
	list_inithead(&slab->shaders);

	mtx_lock(&device->shader_slab_mutex);
	list_add(&slab->slabs, &device->shader_slabs);

	shader->bo = slab->bo;
	shader->bo_offset = 0;
	list_add(&shader->slab_list, &slab->shaders);
	mtx_unlock(&device->shader_slab_mutex);
	return slab->ptr;
}

void
radv_destroy_shader_slabs(struct radv_device *device)
{
	list_for_each_entry_safe(struct radv_shader_slab, slab, &device->shader_slabs, slabs) {
		device->ws->buffer_destroy(slab->bo);
		free(slab);
	}
	mtx_destroy(&device->shader_slab_mutex);
}

/* For the UMR disassembler. */
#define DEBUGGER_END_OF_CODE_MARKER    0xbf9f0000 /* invalid instruction */
#define DEBUGGER_NUM_MARKERS           5

static unsigned
radv_get_shader_binary_size(size_t code_size)
{
	return code_size + DEBUGGER_NUM_MARKERS * 4;
}

static void radv_postprocess_config(const struct radv_physical_device *pdevice,
				    const struct ac_shader_config *config_in,
				    const struct radv_shader_variant_info *info,
				    gl_shader_stage stage,
				    struct ac_shader_config *config_out)
{
	bool scratch_enabled = config_in->scratch_bytes_per_wave > 0;
	unsigned vgpr_comp_cnt = 0;
	unsigned num_input_vgprs = info->num_input_vgprs;

	if (stage == MESA_SHADER_FRAGMENT) {
		num_input_vgprs = 0;
		if (G_0286CC_PERSP_SAMPLE_ENA(config_in->spi_ps_input_addr))
			num_input_vgprs += 2;
		if (G_0286CC_PERSP_CENTER_ENA(config_in->spi_ps_input_addr))
			num_input_vgprs += 2;
		if (G_0286CC_PERSP_CENTROID_ENA(config_in->spi_ps_input_addr))
			num_input_vgprs += 2;
		if (G_0286CC_PERSP_PULL_MODEL_ENA(config_in->spi_ps_input_addr))
			num_input_vgprs += 3;
		if (G_0286CC_LINEAR_SAMPLE_ENA(config_in->spi_ps_input_addr))
			num_input_vgprs += 2;
		if (G_0286CC_LINEAR_CENTER_ENA(config_in->spi_ps_input_addr))
			num_input_vgprs += 2;
		if (G_0286CC_LINEAR_CENTROID_ENA(config_in->spi_ps_input_addr))
			num_input_vgprs += 2;
		if (G_0286CC_LINE_STIPPLE_TEX_ENA(config_in->spi_ps_input_addr))
			num_input_vgprs += 1;
		if (G_0286CC_POS_X_FLOAT_ENA(config_in->spi_ps_input_addr))
			num_input_vgprs += 1;
		if (G_0286CC_POS_Y_FLOAT_ENA(config_in->spi_ps_input_addr))
			num_input_vgprs += 1;
		if (G_0286CC_POS_Z_FLOAT_ENA(config_in->spi_ps_input_addr))
			num_input_vgprs += 1;
		if (G_0286CC_POS_W_FLOAT_ENA(config_in->spi_ps_input_addr))
			num_input_vgprs += 1;
		if (G_0286CC_FRONT_FACE_ENA(config_in->spi_ps_input_addr))
			num_input_vgprs += 1;
		if (G_0286CC_ANCILLARY_ENA(config_in->spi_ps_input_addr))
			num_input_vgprs += 1;
		if (G_0286CC_SAMPLE_COVERAGE_ENA(config_in->spi_ps_input_addr))
			num_input_vgprs += 1;
		if (G_0286CC_POS_FIXED_PT_ENA(config_in->spi_ps_input_addr))
			num_input_vgprs += 1;
	}

	unsigned num_vgprs = MAX2(config_in->num_vgprs, num_input_vgprs);
	/* +3 for scratch wave offset and VCC */
	unsigned num_sgprs = MAX2(config_in->num_sgprs, info->num_input_sgprs + 3);

	*config_out = *config_in;
	config_out->num_vgprs = num_vgprs;
	config_out->num_sgprs = num_sgprs;

	/* Enable 64-bit and 16-bit denormals, because there is no performance
	 * cost.
	 *
	 * If denormals are enabled, all floating-point output modifiers are
	 * ignored.
	 *
	 * Don't enable denormals for 32-bit floats, because:
	 * - Floating-point output modifiers would be ignored by the hw.
	 * - Some opcodes don't support denormals, such as v_mad_f32. We would
	 *   have to stop using those.
	 * - GFX6 & GFX7 would be very slow.
	 */
	config_out->float_mode |= V_00B028_FP_64_DENORMS;

	config_out->rsrc2 = S_00B12C_USER_SGPR(info->num_user_sgprs) |
			    S_00B12C_SCRATCH_EN(scratch_enabled) |
			    S_00B12C_SO_BASE0_EN(!!info->info.so.strides[0]) |
			    S_00B12C_SO_BASE1_EN(!!info->info.so.strides[1]) |
			    S_00B12C_SO_BASE2_EN(!!info->info.so.strides[2]) |
			    S_00B12C_SO_BASE3_EN(!!info->info.so.strides[3]) |
			    S_00B12C_SO_EN(!!info->info.so.num_outputs);

	config_out->rsrc1 = S_00B848_VGPRS((num_vgprs - 1) / 4) |
			    S_00B848_DX10_CLAMP(1) |
			    S_00B848_FLOAT_MODE(config_out->float_mode);

	if (pdevice->rad_info.chip_class >= GFX10) {
		config_out->rsrc2 |= S_00B22C_USER_SGPR_MSB_GFX10(info->num_user_sgprs >> 5);
	} else {
		config_out->rsrc1 |= S_00B228_SGPRS((num_sgprs - 1) / 8);
		config_out->rsrc2 |= S_00B22C_USER_SGPR_MSB_GFX9(info->num_user_sgprs >> 5);
	}

	switch (stage) {
	case MESA_SHADER_TESS_EVAL:
		if (info->is_ngg) {
			config_out->rsrc1 |= S_00B228_MEM_ORDERED(pdevice->rad_info.chip_class >= GFX10);
			config_out->rsrc2 |= S_00B22C_OC_LDS_EN(1);
		} else if (info->tes.as_es) {
			assert(pdevice->rad_info.chip_class <= GFX8);
			vgpr_comp_cnt = info->info.uses_prim_id ? 3 : 2;

			config_out->rsrc2 |= S_00B12C_OC_LDS_EN(1);
		} else {
			bool enable_prim_id = info->tes.export_prim_id || info->info.uses_prim_id;
			vgpr_comp_cnt = enable_prim_id ? 3 : 2;

			config_out->rsrc1 |= S_00B128_MEM_ORDERED(pdevice->rad_info.chip_class >= GFX10);
			config_out->rsrc2 |= S_00B12C_OC_LDS_EN(1);
		}
		break;
	case MESA_SHADER_TESS_CTRL:
		if (pdevice->rad_info.chip_class >= GFX9) {
			/* We need at least 2 components for LS.
			 * VGPR0-3: (VertexID, RelAutoindex, InstanceID / StepRate0, InstanceID).
			 * StepRate0 is set to 1. so that VGPR3 doesn't have to be loaded.
			 */
			if (pdevice->rad_info.chip_class >= GFX10) {
				vgpr_comp_cnt = info->info.vs.needs_instance_id ? 3 : 1;
			} else {
				vgpr_comp_cnt = info->info.vs.needs_instance_id ? 2 : 1;
			}
		} else {
			config_out->rsrc2 |= S_00B12C_OC_LDS_EN(1);
		}
		config_out->rsrc1 |= S_00B428_MEM_ORDERED(pdevice->rad_info.chip_class >= GFX10) |
				     S_00B848_WGP_MODE(pdevice->rad_info.chip_class >= GFX10);
		break;
	case MESA_SHADER_VERTEX:
		if (info->is_ngg) {
			config_out->rsrc1 |= S_00B228_MEM_ORDERED(pdevice->rad_info.chip_class >= GFX10);
		} else if (info->vs.as_ls) {
			assert(pdevice->rad_info.chip_class <= GFX8);
			/* We need at least 2 components for LS.
			 * VGPR0-3: (VertexID, RelAutoindex, InstanceID / StepRate0, InstanceID).
			 * StepRate0 is set to 1. so that VGPR3 doesn't have to be loaded.
			 */
			vgpr_comp_cnt = info->info.vs.needs_instance_id ? 2 : 1;
		} else if (info->vs.as_es) {
			assert(pdevice->rad_info.chip_class <= GFX8);
			/* VGPR0-3: (VertexID, InstanceID / StepRate0, ...) */
			vgpr_comp_cnt = info->info.vs.needs_instance_id ? 1 : 0;
		} else {
			/* VGPR0-3: (VertexID, InstanceID / StepRate0, PrimID, InstanceID)
			 * If PrimID is disabled. InstanceID / StepRate1 is loaded instead.
			 * StepRate0 is set to 1. so that VGPR3 doesn't have to be loaded.
			 */
			if (info->vs.export_prim_id) {
				vgpr_comp_cnt = 2;
			} else if (info->info.vs.needs_instance_id) {
				vgpr_comp_cnt = pdevice->rad_info.chip_class >= GFX10 ? 3 : 1;
			} else {
				vgpr_comp_cnt = 0;
			}

			config_out->rsrc1 |= S_00B128_MEM_ORDERED(pdevice->rad_info.chip_class >= GFX10);
		}
		break;
	case MESA_SHADER_FRAGMENT:
		config_out->rsrc1 |= S_00B028_MEM_ORDERED(pdevice->rad_info.chip_class >= GFX10);
		break;
	case MESA_SHADER_GEOMETRY:
		config_out->rsrc1 |= S_00B228_MEM_ORDERED(pdevice->rad_info.chip_class >= GFX10) |
				     S_00B848_WGP_MODE(pdevice->rad_info.chip_class >= GFX10);
		break;
	case MESA_SHADER_COMPUTE:
		config_out->rsrc1 |= S_00B848_MEM_ORDERED(pdevice->rad_info.chip_class >= GFX10) |
				     S_00B848_WGP_MODE(pdevice->rad_info.chip_class >= GFX10);
		config_out->rsrc2 |=
			S_00B84C_TGID_X_EN(info->info.cs.uses_block_id[0]) |
			S_00B84C_TGID_Y_EN(info->info.cs.uses_block_id[1]) |
			S_00B84C_TGID_Z_EN(info->info.cs.uses_block_id[2]) |
			S_00B84C_TIDIG_COMP_CNT(info->info.cs.uses_thread_id[2] ? 2 :
						info->info.cs.uses_thread_id[1] ? 1 : 0) |
			S_00B84C_TG_SIZE_EN(info->info.cs.uses_local_invocation_idx) |
			S_00B84C_LDS_SIZE(config_in->lds_size);
		break;
	default:
		unreachable("unsupported shader type");
		break;
	}

	if (pdevice->rad_info.chip_class >= GFX10 && info->is_ngg &&
	    (stage == MESA_SHADER_VERTEX || stage == MESA_SHADER_TESS_EVAL || stage == MESA_SHADER_GEOMETRY)) {
		unsigned gs_vgpr_comp_cnt, es_vgpr_comp_cnt;
		gl_shader_stage es_stage = stage;
		if (stage == MESA_SHADER_GEOMETRY)
			es_stage = info->gs.es_type;

		/* VGPR5-8: (VertexID, UserVGPR0, UserVGPR1, UserVGPR2 / InstanceID) */
		if (es_stage == MESA_SHADER_VERTEX) {
			es_vgpr_comp_cnt = info->info.vs.needs_instance_id ? 3 : 0;
		} else if (es_stage == MESA_SHADER_TESS_EVAL) {
			bool enable_prim_id = info->tes.export_prim_id || info->info.uses_prim_id;
			es_vgpr_comp_cnt = enable_prim_id ? 3 : 2;
		} else
			unreachable("Unexpected ES shader stage");

		bool tes_triangles = stage == MESA_SHADER_TESS_EVAL &&
			info->tes.primitive_mode >= 4; /* GL_TRIANGLES */
		if (info->info.uses_invocation_id || stage == MESA_SHADER_VERTEX) {
			gs_vgpr_comp_cnt = 3; /* VGPR3 contains InvocationID. */
		} else if (info->info.uses_prim_id) {
			gs_vgpr_comp_cnt = 2; /* VGPR2 contains PrimitiveID. */
		} else if (info->gs.vertices_in >= 3 || tes_triangles) {
			gs_vgpr_comp_cnt = 1; /* VGPR1 contains offsets 2, 3 */
		} else {
			gs_vgpr_comp_cnt = 0; /* VGPR0 contains offsets 0, 1 */
		}

		config_out->rsrc1 |= S_00B228_GS_VGPR_COMP_CNT(gs_vgpr_comp_cnt) |
				     S_00B228_WGP_MODE(1);
		config_out->rsrc2 |= S_00B22C_ES_VGPR_COMP_CNT(es_vgpr_comp_cnt) |
				     S_00B22C_LDS_SIZE(config_in->lds_size) |
				     S_00B22C_OC_LDS_EN(es_stage == MESA_SHADER_TESS_EVAL);
	} else if (pdevice->rad_info.chip_class >= GFX9 &&
		   stage == MESA_SHADER_GEOMETRY) {
		unsigned es_type = info->gs.es_type;
		unsigned gs_vgpr_comp_cnt, es_vgpr_comp_cnt;

		if (es_type == MESA_SHADER_VERTEX) {
			/* VGPR0-3: (VertexID, InstanceID / StepRate0, ...) */
			if (info->info.vs.needs_instance_id) {
				es_vgpr_comp_cnt = pdevice->rad_info.chip_class >= GFX10 ? 3 : 1;
			} else {
				es_vgpr_comp_cnt = 0;
			}
		} else if (es_type == MESA_SHADER_TESS_EVAL) {
			es_vgpr_comp_cnt = info->info.uses_prim_id ? 3 : 2;
		} else {
			unreachable("invalid shader ES type");
		}

		/* If offsets 4, 5 are used, GS_VGPR_COMP_CNT is ignored and
		 * VGPR[0:4] are always loaded.
		 */
		if (info->info.uses_invocation_id) {
			gs_vgpr_comp_cnt = 3; /* VGPR3 contains InvocationID. */
		} else if (info->info.uses_prim_id) {
			gs_vgpr_comp_cnt = 2; /* VGPR2 contains PrimitiveID. */
		} else if (info->gs.vertices_in >= 3) {
			gs_vgpr_comp_cnt = 1; /* VGPR1 contains offsets 2, 3 */
		} else {
			gs_vgpr_comp_cnt = 0; /* VGPR0 contains offsets 0, 1 */
		}

		config_out->rsrc1 |= S_00B228_GS_VGPR_COMP_CNT(gs_vgpr_comp_cnt);
		config_out->rsrc2 |= S_00B22C_ES_VGPR_COMP_CNT(es_vgpr_comp_cnt) |
		                         S_00B22C_OC_LDS_EN(es_type == MESA_SHADER_TESS_EVAL);
	} else if (pdevice->rad_info.chip_class >= GFX9 &&
		   stage == MESA_SHADER_TESS_CTRL) {
		config_out->rsrc1 |= S_00B428_LS_VGPR_COMP_CNT(vgpr_comp_cnt);
	} else {
		config_out->rsrc1 |= S_00B128_VGPR_COMP_CNT(vgpr_comp_cnt);
	}
}

static void radv_init_llvm_target()
{
	LLVMInitializeAMDGPUTargetInfo();
	LLVMInitializeAMDGPUTarget();
	LLVMInitializeAMDGPUTargetMC();
	LLVMInitializeAMDGPUAsmPrinter();

	/* For inline assembly. */
	LLVMInitializeAMDGPUAsmParser();

	/* Workaround for bug in llvm 4.0 that causes image intrinsics
	 * to disappear.
	 * https://reviews.llvm.org/D26348
	 *
	 * Workaround for bug in llvm that causes the GPU to hang in presence
	 * of nested loops because there is an exec mask issue. The proper
	 * solution is to fix LLVM but this might require a bunch of work.
	 * https://bugs.llvm.org/show_bug.cgi?id=37744
	 *
	 * "mesa" is the prefix for error messages.
	 */
	if (HAVE_LLVM >= 0x0800) {
		const char *argv[2] = { "mesa", "-simplifycfg-sink-common=false" };
		LLVMParseCommandLineOptions(2, argv, NULL);

	} else {
		const char *argv[3] = { "mesa", "-simplifycfg-sink-common=false",
					"-amdgpu-skip-threshold=1" };
		LLVMParseCommandLineOptions(3, argv, NULL);
	}
}

static once_flag radv_init_llvm_target_once_flag = ONCE_FLAG_INIT;

static void radv_init_llvm_once(void)
{
	call_once(&radv_init_llvm_target_once_flag, radv_init_llvm_target);
}

struct radv_shader_variant *
radv_shader_variant_create(struct radv_device *device,
			   const struct radv_shader_binary *binary)
{
	struct ac_shader_config config = {0};
	struct ac_rtld_binary rtld_binary = {0};
	struct radv_shader_variant *variant = calloc(1, sizeof(struct radv_shader_variant));
	if (!variant)
		return NULL;

	variant->ref_count = 1;

	if (binary->type == RADV_BINARY_TYPE_RTLD) {
		struct ac_rtld_symbol lds_symbols[1];
		unsigned num_lds_symbols = 0;
		const char *elf_data = (const char *)((struct radv_shader_binary_rtld *)binary)->data;
		size_t elf_size = ((struct radv_shader_binary_rtld *)binary)->elf_size;
		unsigned esgs_ring_size = 0;

		if (device->physical_device->rad_info.chip_class >= GFX9 &&
		    binary->stage == MESA_SHADER_GEOMETRY && !binary->is_gs_copy_shader) {
			/* TODO: Do not hardcode this value */
			esgs_ring_size = 32 * 1024;
		}

		if (binary->variant_info.is_ngg) {
			/* GS stores Primitive IDs into LDS at the address
			 * corresponding to the ES thread of the provoking
			 * vertex. All ES threads load and export PrimitiveID
			 * for their thread.
			 */
			if (binary->stage == MESA_SHADER_VERTEX &&
			    binary->variant_info.vs.export_prim_id) {
				/* TODO: Do not harcode this value */
				esgs_ring_size = 256 /* max_out_verts */ * 4;
			}
		}

		if (esgs_ring_size) {
			/* We add this symbol even on LLVM <= 8 to ensure that
			 * shader->config.lds_size is set correctly below.
			 */
			struct ac_rtld_symbol *sym = &lds_symbols[num_lds_symbols++];
			sym->name = "esgs_ring";
			sym->size = esgs_ring_size;
			sym->align = 64 * 1024;

			/* Make sure to have LDS space for NGG scratch. */
			/* TODO: Compute this correctly somehow? */
			if (binary->variant_info.is_ngg)
				sym->size -= 32;
		}
		struct ac_rtld_open_info open_info = {
			.info = &device->physical_device->rad_info,
			.shader_type = binary->stage,
			.wave_size = 64,
			.num_parts = 1,
			.elf_ptrs = &elf_data,
			.elf_sizes = &elf_size,
			.num_shared_lds_symbols = num_lds_symbols,
			.shared_lds_symbols = lds_symbols,
		};
		
		if (!ac_rtld_open(&rtld_binary, open_info)) {
			free(variant);
			return NULL;
		}

		if (!ac_rtld_read_config(&rtld_binary, &config)) {
			ac_rtld_close(&rtld_binary);
			free(variant);
			return NULL;
		}

		if (rtld_binary.lds_size > 0) {
			unsigned alloc_granularity = device->physical_device->rad_info.chip_class >= GFX7 ? 512 : 256;
			config.lds_size = align(rtld_binary.lds_size, alloc_granularity) / alloc_granularity;
		}

		variant->code_size = rtld_binary.rx_size;
	} else {
		assert(binary->type == RADV_BINARY_TYPE_LEGACY);
		config = ((struct radv_shader_binary_legacy *)binary)->config;
		variant->code_size  = radv_get_shader_binary_size(((struct radv_shader_binary_legacy *)binary)->code_size);
	}

	variant->info = binary->variant_info;
	radv_postprocess_config(device->physical_device, &config, &binary->variant_info,
				binary->stage, &variant->config);
	
	void *dest_ptr = radv_alloc_shader_memory(device, variant);

	if (binary->type == RADV_BINARY_TYPE_RTLD) {
		struct radv_shader_binary_rtld* bin = (struct radv_shader_binary_rtld *)binary;
		struct ac_rtld_upload_info info = {
			.binary = &rtld_binary,
			.rx_va = radv_buffer_get_va(variant->bo) + variant->bo_offset,
			.rx_ptr = dest_ptr, 
		};

		if (!ac_rtld_upload(&info)) {
			radv_shader_variant_destroy(device, variant);
			ac_rtld_close(&rtld_binary);
			return NULL;
		}

		if (device->keep_shader_info ||
		    (device->instance->debug_flags & RADV_DEBUG_DUMP_SHADERS)) {
			const char *disasm_data;
			size_t disasm_size;
			if (!ac_rtld_get_section_by_name(&rtld_binary, ".AMDGPU.disasm", &disasm_data, &disasm_size)) {
				radv_shader_variant_destroy(device, variant);
				ac_rtld_close(&rtld_binary);
				return NULL;
			}

			variant->llvm_ir_string = bin->llvm_ir_size ? strdup((const char*)(bin->data + bin->elf_size)) : NULL;
			variant->disasm_string = malloc(disasm_size + 1);
			memcpy(variant->disasm_string, disasm_data, disasm_size);
			variant->disasm_string[disasm_size] = 0;
		}

		ac_rtld_close(&rtld_binary);
	} else {
		struct radv_shader_binary_legacy* bin = (struct radv_shader_binary_legacy *)binary;
		memcpy(dest_ptr, bin->data, bin->code_size);

		/* Add end-of-code markers for the UMR disassembler. */
		uint32_t *ptr32 = (uint32_t *)dest_ptr + bin->code_size / 4;
		for (unsigned i = 0; i < DEBUGGER_NUM_MARKERS; i++)
			ptr32[i] = DEBUGGER_END_OF_CODE_MARKER;

		variant->llvm_ir_string = bin->llvm_ir_size ? strdup((const char*)(bin->data + bin->code_size)) : NULL;
		variant->disasm_string = bin->disasm_size ? strdup((const char*)(bin->data + bin->code_size + bin->llvm_ir_size)) : NULL;
	}
	return variant;
}

static struct radv_shader_variant *
shader_variant_compile(struct radv_device *device,
		       struct radv_shader_module *module,
		       struct nir_shader * const *shaders,
		       int shader_count,
		       gl_shader_stage stage,
		       struct radv_nir_compiler_options *options,
		       bool gs_copy_shader,
		       struct radv_shader_binary **binary_out)
{
	enum radeon_family chip_family = device->physical_device->rad_info.family;
	enum ac_target_machine_options tm_options = 0;
	struct ac_llvm_compiler ac_llvm;
	struct radv_shader_binary *binary = NULL;
	struct radv_shader_variant_info variant_info = {0};
	bool thread_compiler;

	if (shaders[0]->info.stage == MESA_SHADER_FRAGMENT)
		lower_fs_io(shaders[0], &variant_info);

	options->family = chip_family;
	options->chip_class = device->physical_device->rad_info.chip_class;
	options->dump_shader = radv_can_dump_shader(device, module, gs_copy_shader);
	options->dump_preoptir = options->dump_shader &&
				 device->instance->debug_flags & RADV_DEBUG_PREOPTIR;
	options->record_llvm_ir = device->keep_shader_info;
	options->check_ir = device->instance->debug_flags & RADV_DEBUG_CHECKIR;
	options->tess_offchip_block_dw_size = device->tess_offchip_block_dw_size;
	options->address32_hi = device->physical_device->rad_info.address32_hi;

	if (options->supports_spill)
		tm_options |= AC_TM_SUPPORTS_SPILL;
	if (device->instance->perftest_flags & RADV_PERFTEST_SISCHED)
		tm_options |= AC_TM_SISCHED;
	if (options->check_ir)
		tm_options |= AC_TM_CHECK_IR;
	if (device->instance->debug_flags & RADV_DEBUG_NO_LOAD_STORE_OPT)
		tm_options |= AC_TM_NO_LOAD_STORE_OPT;

	thread_compiler = !(device->instance->debug_flags & RADV_DEBUG_NOTHREADLLVM);
	radv_init_llvm_once();
	radv_init_llvm_compiler(&ac_llvm,
				thread_compiler,
				chip_family, tm_options);
	if (gs_copy_shader) {
		assert(shader_count == 1);
		radv_compile_gs_copy_shader(&ac_llvm, *shaders, &binary,
					    &variant_info, options);
	} else {
		radv_compile_nir_shader(&ac_llvm, &binary, &variant_info,
					shaders, shader_count, options);
	}
	binary->variant_info = variant_info;

	radv_destroy_llvm_compiler(&ac_llvm, thread_compiler);

	struct radv_shader_variant *variant = radv_shader_variant_create(device, binary);
	if (!variant) {
		free(binary);
		return NULL;
	}

	if (options->dump_shader) {
		fprintf(stderr, "disasm:\n%s\n", variant->disasm_string);
	}


	if (device->keep_shader_info) {
		if (!gs_copy_shader && !module->nir) {
			variant->nir = *shaders;
			variant->spirv = (uint32_t *)module->data;
			variant->spirv_size = module->size;
		}
	}

	if (binary_out)
		*binary_out = binary;
	else
		free(binary);

	return variant;
}

struct radv_shader_variant *
radv_shader_variant_compile(struct radv_device *device,
			   struct radv_shader_module *module,
			   struct nir_shader *const *shaders,
			   int shader_count,
			   struct radv_pipeline_layout *layout,
			   const struct radv_shader_variant_key *key,
			   struct radv_shader_binary **binary_out)
{
	struct radv_nir_compiler_options options = {0};

	options.layout = layout;
	if (key)
		options.key = *key;

	options.unsafe_math = !!(device->instance->debug_flags & RADV_DEBUG_UNSAFE_MATH);
	options.supports_spill = true;

	return shader_variant_compile(device, module, shaders, shader_count, shaders[shader_count - 1]->info.stage,
				     &options, false, binary_out);
}

struct radv_shader_variant *
radv_create_gs_copy_shader(struct radv_device *device,
			   struct nir_shader *shader,
			   struct radv_shader_binary **binary_out,
			   bool multiview)
{
	struct radv_nir_compiler_options options = {0};

	options.key.has_multiview_view_index = multiview;

	return shader_variant_compile(device, NULL, &shader, 1, MESA_SHADER_VERTEX,
				     &options, true, binary_out);
}

void
radv_shader_variant_destroy(struct radv_device *device,
			    struct radv_shader_variant *variant)
{
	if (!p_atomic_dec_zero(&variant->ref_count))
		return;

	mtx_lock(&device->shader_slab_mutex);
	list_del(&variant->slab_list);
	mtx_unlock(&device->shader_slab_mutex);

	ralloc_free(variant->nir);
	free(variant->disasm_string);
	free(variant->llvm_ir_string);
	free(variant);
}

const char *
radv_get_shader_name(struct radv_shader_variant_info *info,
		     gl_shader_stage stage)
{
	switch (stage) {
	case MESA_SHADER_VERTEX:
		if (info->vs.as_ls)
			return "Vertex Shader as LS";
		else if (info->vs.as_es)
			return "Vertex Shader as ES";
		else if (info->is_ngg)
			return "Vertex Shader as ESGS";
		else
			return "Vertex Shader as VS";
	case MESA_SHADER_TESS_CTRL:
		return "Tessellation Control Shader";
	case MESA_SHADER_TESS_EVAL:
		if (info->tes.as_es)
			return "Tessellation Evaluation Shader as ES";
		else if (info->is_ngg)
			return "Tessellation Evaluation Shader as ESGS";
		else
			return "Tessellation Evaluation Shader as VS";
	case MESA_SHADER_GEOMETRY:
		return "Geometry Shader";
	case MESA_SHADER_FRAGMENT:
		return "Pixel Shader";
	case MESA_SHADER_COMPUTE:
		return "Compute Shader";
	default:
		return "Unknown shader";
	};
}

static void
generate_shader_stats(struct radv_device *device,
		      struct radv_shader_variant *variant,
		      gl_shader_stage stage,
		      struct _mesa_string_buffer *buf)
{
	enum chip_class chip_class = device->physical_device->rad_info.chip_class;
	unsigned lds_increment = chip_class >= GFX7 ? 512 : 256;
	struct ac_shader_config *conf;
	unsigned max_simd_waves;
	unsigned lds_per_wave = 0;

	max_simd_waves = ac_get_max_simd_waves(device->physical_device->rad_info.family);

	conf = &variant->config;

	if (stage == MESA_SHADER_FRAGMENT) {
		lds_per_wave = conf->lds_size * lds_increment +
			       align(variant->info.fs.num_interp * 48,
				     lds_increment);
	} else if (stage == MESA_SHADER_COMPUTE) {
		unsigned max_workgroup_size =
			radv_nir_get_max_workgroup_size(chip_class, stage, variant->nir);
		lds_per_wave = (conf->lds_size * lds_increment) /
			       DIV_ROUND_UP(max_workgroup_size, 64);
	}

	if (conf->num_sgprs)
		max_simd_waves =
			MIN2(max_simd_waves,
			     ac_get_num_physical_sgprs(chip_class) / conf->num_sgprs);

	if (conf->num_vgprs)
		max_simd_waves =
			MIN2(max_simd_waves,
			     RADV_NUM_PHYSICAL_VGPRS / conf->num_vgprs);

	/* LDS is 64KB per CU (4 SIMDs), divided into 16KB blocks per SIMD
	 * that PS can use.
	 */
	if (lds_per_wave)
		max_simd_waves = MIN2(max_simd_waves, 16384 / lds_per_wave);

	if (stage == MESA_SHADER_FRAGMENT) {
		_mesa_string_buffer_printf(buf, "*** SHADER CONFIG ***\n"
					   "SPI_PS_INPUT_ADDR = 0x%04x\n"
					   "SPI_PS_INPUT_ENA  = 0x%04x\n",
					   conf->spi_ps_input_addr, conf->spi_ps_input_ena);
	}

	_mesa_string_buffer_printf(buf, "*** SHADER STATS ***\n"
				   "SGPRS: %d\n"
				   "VGPRS: %d\n"
				   "Spilled SGPRs: %d\n"
				   "Spilled VGPRs: %d\n"
				   "PrivMem VGPRS: %d\n"
				   "Code Size: %d bytes\n"
				   "LDS: %d blocks\n"
				   "Scratch: %d bytes per wave\n"
				   "Max Waves: %d\n"
				   "********************\n\n\n",
				   conf->num_sgprs, conf->num_vgprs,
				   conf->spilled_sgprs, conf->spilled_vgprs,
				   variant->info.private_mem_vgprs, variant->code_size,
				   conf->lds_size, conf->scratch_bytes_per_wave,
				   max_simd_waves);
}

void
radv_shader_dump_stats(struct radv_device *device,
		       struct radv_shader_variant *variant,
		       gl_shader_stage stage,
		       FILE *file)
{
	struct _mesa_string_buffer *buf = _mesa_string_buffer_create(NULL, 256);

	generate_shader_stats(device, variant, stage, buf);

	fprintf(file, "\n%s:\n", radv_get_shader_name(&variant->info, stage));
	fprintf(file, "%s", buf->buf);

	_mesa_string_buffer_destroy(buf);
}

VkResult
radv_GetShaderInfoAMD(VkDevice _device,
		      VkPipeline _pipeline,
		      VkShaderStageFlagBits shaderStage,
		      VkShaderInfoTypeAMD infoType,
		      size_t* pInfoSize,
		      void* pInfo)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	RADV_FROM_HANDLE(radv_pipeline, pipeline, _pipeline);
	gl_shader_stage stage = vk_to_mesa_shader_stage(shaderStage);
	struct radv_shader_variant *variant = pipeline->shaders[stage];
	struct _mesa_string_buffer *buf;
	VkResult result = VK_SUCCESS;

	/* Spec doesn't indicate what to do if the stage is invalid, so just
	 * return no info for this. */
	if (!variant)
		return vk_error(device->instance, VK_ERROR_FEATURE_NOT_PRESENT);

	switch (infoType) {
	case VK_SHADER_INFO_TYPE_STATISTICS_AMD:
		if (!pInfo) {
			*pInfoSize = sizeof(VkShaderStatisticsInfoAMD);
		} else {
			unsigned lds_multiplier = device->physical_device->rad_info.chip_class >= GFX7 ? 512 : 256;
			struct ac_shader_config *conf = &variant->config;

			VkShaderStatisticsInfoAMD statistics = {};
			statistics.shaderStageMask = shaderStage;
			statistics.numPhysicalVgprs = RADV_NUM_PHYSICAL_VGPRS;
			statistics.numPhysicalSgprs = ac_get_num_physical_sgprs(device->physical_device->rad_info.chip_class);
			statistics.numAvailableSgprs = statistics.numPhysicalSgprs;

			if (stage == MESA_SHADER_COMPUTE) {
				unsigned *local_size = variant->nir->info.cs.local_size;
				unsigned workgroup_size = local_size[0] * local_size[1] * local_size[2];

				statistics.numAvailableVgprs = statistics.numPhysicalVgprs /
							       ceil((double)workgroup_size / statistics.numPhysicalVgprs);

				statistics.computeWorkGroupSize[0] = local_size[0];
				statistics.computeWorkGroupSize[1] = local_size[1];
				statistics.computeWorkGroupSize[2] = local_size[2];
			} else {
				statistics.numAvailableVgprs = statistics.numPhysicalVgprs;
			}

			statistics.resourceUsage.numUsedVgprs = conf->num_vgprs;
			statistics.resourceUsage.numUsedSgprs = conf->num_sgprs;
			statistics.resourceUsage.ldsSizePerLocalWorkGroup = 32768;
			statistics.resourceUsage.ldsUsageSizeInBytes = conf->lds_size * lds_multiplier;
			statistics.resourceUsage.scratchMemUsageInBytes = conf->scratch_bytes_per_wave;

			size_t size = *pInfoSize;
			*pInfoSize = sizeof(statistics);

			memcpy(pInfo, &statistics, MIN2(size, *pInfoSize));

			if (size < *pInfoSize)
				result = VK_INCOMPLETE;
		}

		break;
	case VK_SHADER_INFO_TYPE_DISASSEMBLY_AMD:
		buf = _mesa_string_buffer_create(NULL, 1024);

		_mesa_string_buffer_printf(buf, "%s:\n", radv_get_shader_name(&variant->info, stage));
		_mesa_string_buffer_printf(buf, "%s\n\n", variant->llvm_ir_string);
		_mesa_string_buffer_printf(buf, "%s\n\n", variant->disasm_string);
		generate_shader_stats(device, variant, stage, buf);

		/* Need to include the null terminator. */
		size_t length = buf->length + 1;

		if (!pInfo) {
			*pInfoSize = length;
		} else {
			size_t size = *pInfoSize;
			*pInfoSize = length;

			memcpy(pInfo, buf->buf, MIN2(size, length));

			if (size < length)
				result = VK_INCOMPLETE;
		}

		_mesa_string_buffer_destroy(buf);
		break;
	default:
		/* VK_SHADER_INFO_TYPE_BINARY_AMD unimplemented for now. */
		result = VK_ERROR_FEATURE_NOT_PRESENT;
		break;
	}

	return result;
}
