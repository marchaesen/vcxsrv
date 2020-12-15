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

#include "util/memstream.h"
#include "util/mesa-sha1.h"
#include "util/u_atomic.h"
#include "radv_debug.h"
#include "radv_private.h"
#include "radv_shader.h"
#include "radv_shader_helper.h"
#include "radv_shader_args.h"
#include "nir/nir.h"
#include "nir/nir_builder.h"
#include "spirv/nir_spirv.h"

#include "sid.h"
#include "ac_binary.h"
#include "ac_llvm_util.h"
#include "ac_nir_to_llvm.h"
#include "ac_rtld.h"
#include "vk_format.h"
#include "util/debug.h"
#include "ac_exp_param.h"

static const struct nir_shader_compiler_options nir_options = {
	.vertex_id_zero_based = true,
	.lower_scmp = true,
	.lower_flrp16 = true,
	.lower_flrp32 = true,
	.lower_flrp64 = true,
	.lower_device_index_to_zero = true,
	.lower_fdiv = true,
	.lower_fmod = true,
	.lower_bitfield_insert_to_bitfield_select = true,
	.lower_bitfield_extract = true,
	.lower_pack_snorm_2x16 = true,
	.lower_pack_snorm_4x8 = true,
	.lower_pack_unorm_2x16 = true,
	.lower_pack_unorm_4x8 = true,
	.lower_pack_half_2x16 = true,
	.lower_pack_64_2x32 = true,
	.lower_pack_64_4x16 = true,
	.lower_pack_32_2x16 = true,
	.lower_unpack_snorm_2x16 = true,
	.lower_unpack_snorm_4x8 = true,
	.lower_unpack_unorm_2x16 = true,
	.lower_unpack_unorm_4x8 = true,
	.lower_unpack_half_2x16 = true,
	.lower_extract_byte = true,
	.lower_extract_word = true,
	.lower_ffma16 = true,
	.lower_ffma32 = true,
	.lower_ffma64 = true,
	.lower_fpow = true,
	.lower_mul_2x32_64 = true,
	.lower_rotate = true,
	.use_scoped_barrier = true,
	.max_unroll_iterations = 32,
	.use_interpolated_input_intrinsics = true,
	/* nir_lower_int64() isn't actually called for the LLVM backend, but
	 * this helps the loop unrolling heuristics. */
	.lower_int64_options = nir_lower_imul64 |
                               nir_lower_imul_high64 |
                               nir_lower_imul_2x32_64 |
                               nir_lower_divmod64 |
                               nir_lower_minmax64 |
                               nir_lower_iabs64,
	.lower_doubles_options = nir_lower_drcp |
				 nir_lower_dsqrt |
				 nir_lower_drsq |
				 nir_lower_ddiv,
   .divergence_analysis_options = nir_divergence_view_index_uniform,
};

bool
radv_can_dump_shader(struct radv_device *device,
		     struct radv_shader_module *module,
		     bool is_gs_copy_shader)
{
	if (!(device->instance->debug_flags & RADV_DEBUG_DUMP_SHADERS))
		return false;
	if (module)
		return !module->nir ||
			(device->instance->debug_flags & RADV_DEBUG_DUMP_META_SHADERS);

	return is_gs_copy_shader;
}

bool
radv_can_dump_shader_stats(struct radv_device *device,
			   struct radv_shader_module *module)
{
	/* Only dump non-meta shader stats. */
	return device->instance->debug_flags & RADV_DEBUG_DUMP_SHADER_STATS &&
	       module && !module->nir;
}

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

	module = vk_alloc2(&device->vk.alloc, pAllocator,
			     sizeof(*module) + pCreateInfo->codeSize, 8,
			     VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
	if (module == NULL)
		return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

	vk_object_base_init(&device->vk, &module->base,
			    VK_OBJECT_TYPE_SHADER_MODULE);

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

	vk_object_base_finish(&module->base);
	vk_free2(&device->vk.alloc, pAllocator, module);
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
		NIR_PASS(progress, shader, nir_remove_dead_variables,
			 nir_var_function_temp | nir_var_shader_in | nir_var_shader_out,
			 NULL);

                NIR_PASS_V(shader, nir_lower_alu_to_scalar, NULL, NULL);
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
                                 false /* always_precise */);
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
                NIR_PASS(progress, shader, nir_opt_shrink_vectors);
                if (shader->options->max_unroll_iterations) {
                        NIR_PASS(progress, shader, nir_opt_loop_unroll, 0);
                }
        } while (progress && !optimize_conservatively);

	NIR_PASS(progress, shader, nir_opt_conditional_discard);
        NIR_PASS(progress, shader, nir_opt_move, nir_move_load_ubo);
}

static void
shared_var_info(const struct glsl_type *type, unsigned *size, unsigned *align)
{
	assert(glsl_type_is_vector_or_scalar(type));

	uint32_t comp_size = glsl_type_is_boolean(type) ? 4 : glsl_get_bit_size(type) / 8;
	unsigned length = glsl_get_vector_elements(type);
	*size = comp_size * length,
	*align = comp_size;
}

struct radv_shader_debug_data {
	struct radv_device *device;
	const struct radv_shader_module *module;
};

static void radv_spirv_nir_debug(void *private_data,
				 enum nir_spirv_debug_level level,
				 size_t spirv_offset,
				 const char *message)
{
	struct radv_shader_debug_data *debug_data = private_data;
	struct radv_instance *instance = debug_data->device->instance;

	static const VkDebugReportFlagsEXT vk_flags[] = {
		[NIR_SPIRV_DEBUG_LEVEL_INFO] = VK_DEBUG_REPORT_INFORMATION_BIT_EXT,
		[NIR_SPIRV_DEBUG_LEVEL_WARNING] = VK_DEBUG_REPORT_WARNING_BIT_EXT,
		[NIR_SPIRV_DEBUG_LEVEL_ERROR] = VK_DEBUG_REPORT_ERROR_BIT_EXT,
	};
	char buffer[256];

	snprintf(buffer, sizeof(buffer), "SPIR-V offset %lu: %s",
		 (unsigned long)spirv_offset, message);

	vk_debug_report(&instance->debug_report_callbacks,
			vk_flags[level],
			VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT,
			(uint64_t)(uintptr_t)debug_data->module,
			0, 0, "radv", buffer);
}

static void radv_compiler_debug(void *private_data,
				enum radv_compiler_debug_level level,
				const char *message)
{
	struct radv_shader_debug_data *debug_data = private_data;
	struct radv_instance *instance = debug_data->device->instance;

	static const VkDebugReportFlagsEXT vk_flags[] = {
		[RADV_COMPILER_DEBUG_LEVEL_PERFWARN] = VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT,
		[RADV_COMPILER_DEBUG_LEVEL_ERROR] = VK_DEBUG_REPORT_ERROR_BIT_EXT,
	};

	/* VK_DEBUG_REPORT_DEBUG_BIT_EXT specifies diagnostic information
	 * from the implementation and layers.
	 */
	vk_debug_report(&instance->debug_report_callbacks,
			vk_flags[level] | VK_DEBUG_REPORT_DEBUG_BIT_EXT,
			VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT,
			(uint64_t)(uintptr_t)debug_data->module,
			0, 0, "radv", message);
}

static bool
lower_load_vulkan_descriptor(nir_shader *nir)
{
	nir_function_impl *entry = nir_shader_get_entrypoint(nir);
	bool progress = false;
	nir_builder b;

	nir_builder_init(&b, entry);

	nir_foreach_block(block, entry) {
		nir_foreach_instr_safe(instr, block) {
			if (instr->type != nir_instr_type_intrinsic)
				continue;

			nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
			if (intrin->intrinsic != nir_intrinsic_load_vulkan_descriptor)
				continue;

			b.cursor = nir_before_instr(&intrin->instr);

			nir_ssa_def *def = nir_vec2(&b,
						    nir_channel(&b, intrin->src[0].ssa, 0),
						    nir_imm_int(&b, 0));
			nir_ssa_def_rewrite_uses(&intrin->dest.ssa,
						 nir_src_for_ssa(def));

			nir_instr_remove(instr);
			progress = true;
		}
	}

	return progress;
}

nir_shader *
radv_shader_compile_to_nir(struct radv_device *device,
			   struct radv_shader_module *module,
			   const char *entrypoint_name,
			   gl_shader_stage stage,
			   const VkSpecializationInfo *spec_info,
			   const VkPipelineCreateFlags flags,
			   const struct radv_pipeline_layout *layout,
			   unsigned subgroup_size, unsigned ballot_bit_size)
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
			radv_print_spirv(module->data, module->size, stderr);

		uint32_t num_spec_entries = 0;
		struct nir_spirv_specialization *spec_entries = NULL;
		if (spec_info && spec_info->mapEntryCount > 0) {
			num_spec_entries = spec_info->mapEntryCount;
			spec_entries = calloc(num_spec_entries, sizeof(*spec_entries));
			for (uint32_t i = 0; i < num_spec_entries; i++) {
				VkSpecializationMapEntry entry = spec_info->pMapEntries[i];
				const void *data = spec_info->pData + entry.offset;
				assert(data + entry.size <= spec_info->pData + spec_info->dataSize);

				spec_entries[i].id = spec_info->pMapEntries[i].constantID;
				switch (entry.size) {
				case 8:
					memcpy(&spec_entries[i].value.u64, data, sizeof(uint64_t));
					break;
				case 4:
					memcpy(&spec_entries[i].value.u32, data, sizeof(uint32_t));
					break;
				case 2:
					memcpy(&spec_entries[i].value.u16, data, sizeof(uint16_t));
					break;
				case 1:
					memcpy(&spec_entries[i].value.u8, data, sizeof(uint8_t));
					break;
				default:
					assert(!"Invalid spec constant size");
					break;
				}
			}
		}

		struct radv_shader_debug_data spirv_debug_data = {
			.device = device,
			.module = module,
		};
		const struct spirv_to_nir_options spirv_options = {
			.caps = {
				.amd_fragment_mask = true,
				.amd_gcn_shader = true,
				.amd_image_gather_bias_lod = true,
				.amd_image_read_write_lod = true,
				.amd_shader_ballot = true,
				.amd_shader_explicit_vertex_parameter = true,
				.amd_trinary_minmax = true,
				.demote_to_helper_invocation = true,
				.derivative_group = true,
				.descriptor_array_dynamic_indexing = true,
				.descriptor_array_non_uniform_indexing = true,
				.descriptor_indexing = true,
				.device_group = true,
				.draw_parameters = true,
				.float_controls = true,
				.float16 = device->physical_device->rad_info.has_packed_math_16bit,
				.float32_atomic_add = true,
				.float64 = true,
				.geometry_streams = true,
				.image_atomic_int64 = true,
				.image_ms_array = true,
				.image_read_without_format = true,
				.image_write_without_format = true,
				.int8 = true,
				.int16 = true,
				.int64 = true,
				.int64_atomics = true,
				.min_lod = true,
				.multiview = true,
				.physical_storage_buffer_address = true,
				.post_depth_coverage = true,
				.runtime_descriptor_array = true,
				.shader_clock = true,
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
				.vk_memory_model = true,
				.vk_memory_model_device_scope = true,
				.fragment_shading_rate = device->physical_device->rad_info.chip_class >= GFX10_3,
			},
			.ubo_addr_format = nir_address_format_32bit_index_offset,
			.ssbo_addr_format = nir_address_format_32bit_index_offset,
			.phys_ssbo_addr_format = nir_address_format_64bit_global,
			.push_const_addr_format = nir_address_format_logical,
			.shared_addr_format = nir_address_format_32bit_offset,
			.frag_coord_is_sysval = true,
			.debug = {
				.func = radv_spirv_nir_debug,
				.private_data = &spirv_debug_data,
			},
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
		NIR_PASS_V(nir, nir_lower_variable_initializers, nir_var_function_temp);
		NIR_PASS_V(nir, nir_lower_returns);
		NIR_PASS_V(nir, nir_inline_functions);
		NIR_PASS_V(nir, nir_copy_prop);
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
		NIR_PASS_V(nir, nir_lower_variable_initializers, nir_var_shader_out);

		/* Now that we've deleted all but the main function, we can go ahead and
		 * lower the rest of the constant initializers.
		 */
		NIR_PASS_V(nir, nir_lower_variable_initializers, ~0);

		/* Split member structs.  We do this before lower_io_to_temporaries so that
		 * it doesn't lower system values to temporaries by accident.
		 */
		NIR_PASS_V(nir, nir_split_var_copies);
		NIR_PASS_V(nir, nir_split_per_member_structs);

		if (nir->info.stage == MESA_SHADER_FRAGMENT)
                        NIR_PASS_V(nir, nir_lower_io_to_vector, nir_var_shader_out);
		if (nir->info.stage == MESA_SHADER_FRAGMENT)
			NIR_PASS_V(nir, nir_lower_input_attachments,
				   &(nir_input_attachment_options) {
					.use_fragcoord_sysval = true,
					.use_layer_id_sysval = false,
				   });

		NIR_PASS_V(nir, nir_remove_dead_variables,
		           nir_var_shader_in | nir_var_shader_out | nir_var_system_value | nir_var_mem_shared,
			   NULL);

		NIR_PASS_V(nir, nir_propagate_invariant);

		NIR_PASS_V(nir, nir_lower_system_values);
		NIR_PASS_V(nir, nir_lower_compute_system_values, NULL);

		NIR_PASS_V(nir, nir_lower_clip_cull_distance_arrays);

		NIR_PASS_V(nir, nir_lower_discard_or_demote,
			   device->instance->debug_flags & RADV_DEBUG_DISCARD_TO_DEMOTE);

		nir_lower_doubles_options lower_doubles =
			nir->options->lower_doubles_options;

		if (device->physical_device->rad_info.chip_class == GFX6) {
			/* GFX6 doesn't support v_floor_f64 and the precision
			 * of v_fract_f64 which is used to implement 64-bit
			 * floor is less than what Vulkan requires.
			 */
			lower_doubles |= nir_lower_dfloor;
		}

		NIR_PASS_V(nir, nir_lower_doubles, NULL, lower_doubles);
	}

	/* Vulkan uses the separate-shader linking model */
	nir->info.separate_shader = true;

	nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));

	if (nir->info.stage == MESA_SHADER_GEOMETRY) {
		unsigned nir_gs_flags = nir_lower_gs_intrinsics_per_stream;

		if (device->physical_device->use_ngg && !radv_use_llvm_for_stage(device, stage)) {
			/* ACO needs NIR to do some of the hard lifting */
			nir_gs_flags |= nir_lower_gs_intrinsics_count_primitives |
			                nir_lower_gs_intrinsics_count_vertices_per_primitive |
							nir_lower_gs_intrinsics_overwrite_incomplete;
		}

		nir_lower_gs_intrinsics(nir, nir_gs_flags);
	}

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
	nir_remove_dead_variables(nir, nir_var_function_temp, NULL);
	bool gfx7minus = device->physical_device->rad_info.chip_class <= GFX7;
	nir_lower_subgroups(nir, &(struct nir_lower_subgroups_options) {
			.subgroup_size = subgroup_size,
			.ballot_bit_size = ballot_bit_size,
			.lower_to_scalar = 1,
			.lower_subgroup_masks = 1,
			.lower_shuffle = 1,
			.lower_shuffle_to_32bit = 1,
			.lower_vote_eq_to_ballot = 1,
			.lower_quad_broadcast_dynamic = 1,
			.lower_quad_broadcast_dynamic_to_const = gfx7minus,
			.lower_shuffle_to_swizzle_amd = 1,
			.lower_elect = radv_use_llvm_for_stage(device, stage),
		});

	nir_lower_load_const_to_scalar(nir);

	if (!(flags & VK_PIPELINE_CREATE_DISABLE_OPTIMIZATION_BIT))
		radv_optimize_nir(nir, false, true);

	/* call radv_nir_lower_ycbcr_textures() late as there might still be
	 * tex with undef texture/sampler before first optimization */
	NIR_PASS_V(nir, radv_nir_lower_ycbcr_textures, layout);

	/* We call nir_lower_var_copies() after the first radv_optimize_nir()
	 * to remove any copies introduced by nir_opt_find_array_copies().
	 */
	nir_lower_var_copies(nir);

	NIR_PASS_V(nir, nir_lower_explicit_io, nir_var_mem_push_const,
		   nir_address_format_32bit_offset);

	NIR_PASS_V(nir, nir_lower_explicit_io,
		   nir_var_mem_ubo | nir_var_mem_ssbo,
		   nir_address_format_32bit_index_offset);

	NIR_PASS_V(nir, lower_load_vulkan_descriptor);

	/* Lower deref operations for compute shared memory. */
	if (nir->info.stage == MESA_SHADER_COMPUTE) {
		NIR_PASS_V(nir, nir_lower_vars_to_explicit_types,
			   nir_var_mem_shared, shared_var_info);
		NIR_PASS_V(nir, nir_lower_explicit_io,
			   nir_var_mem_shared, nir_address_format_32bit_offset);
	}

	nir_lower_explicit_io(nir, nir_var_mem_global,
			      nir_address_format_64bit_global);

	/* Lower large variables that are always constant with load_constant
	 * intrinsics, which get turned into PC-relative loads from a data
	 * section next to the shader.
	 */
	NIR_PASS_V(nir, nir_opt_large_constants,
		   glsl_get_natural_size_align_bytes, 16);

	/* Indirect lowering must be called after the radv_optimize_nir() loop
	 * has been called at least once. Otherwise indirect lowering can
	 * bloat the instruction count of the loop and cause it to be
	 * considered too large for unrolling.
	 */
	if (ac_lower_indirect_derefs(nir, device->physical_device->rad_info.chip_class) &&
	    !(flags & VK_PIPELINE_CREATE_DISABLE_OPTIMIZATION_BIT) &&
	    nir->info.stage != MESA_SHADER_COMPUTE) {
		/* Optimize the lowered code before the linking optimizations. */
		radv_optimize_nir(nir, false, false);
	}

	return nir;
}

static int
type_size_vec4(const struct glsl_type *type, bool bindless)
{
	return glsl_count_attribute_slots(type, false);
}

static nir_variable *
find_layer_in_var(nir_shader *nir)
{
	nir_variable *var =
		nir_find_variable_with_location(nir, nir_var_shader_in, VARYING_SLOT_LAYER);
	if (var != NULL)
		return var;

	var = nir_variable_create(nir, nir_var_shader_in, glsl_int_type(), "layer id");
	var->data.location = VARYING_SLOT_LAYER;
	var->data.interpolation = INTERP_MODE_FLAT;
	return var;
}

/* We use layered rendering to implement multiview, which means we need to map
 * view_index to gl_Layer. The code generates a load from the layer_id sysval,
 * but since we don't have a way to get at this information from the fragment
 * shader, we also need to lower this to the gl_Layer varying.  This pass
 * lowers both to a varying load from the LAYER slot, before lowering io, so
 * that nir_assign_var_locations() will give the LAYER varying the correct
 * driver_location.
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
			if (load->intrinsic != nir_intrinsic_load_view_index)
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

void
radv_lower_io(struct radv_device *device, nir_shader *nir)
{
	if (nir->info.stage == MESA_SHADER_COMPUTE)
		return;

	if (nir->info.stage == MESA_SHADER_FRAGMENT) {
		NIR_PASS_V(nir, lower_view_index);
		nir_assign_io_var_locations(nir, nir_var_shader_in, &nir->num_inputs,
					    MESA_SHADER_FRAGMENT);
	}

	/* The RADV/LLVM backend expects 64-bit IO to be lowered. */
	nir_lower_io_options options =
		radv_use_llvm_for_stage(device, nir->info.stage) ? nir_lower_io_lower_64bit_to_32 : 0;

	NIR_PASS_V(nir, nir_lower_io, nir_var_shader_in | nir_var_shader_out,
		   type_size_vec4, options);

	/* This pass needs actual constants */
	nir_opt_constant_folding(nir);

	NIR_PASS_V(nir, nir_io_add_const_offset_to_base,
		   nir_var_shader_in | nir_var_shader_out);
}


static void *
radv_alloc_shader_memory(struct radv_device *device,
			 struct radv_shader_variant *shader)
{
	mtx_lock(&device->shader_slab_mutex);
	list_for_each_entry(struct radv_shader_slab, slab, &device->shader_slabs, slabs) {
		uint64_t offset = 0;

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#endif
		list_for_each_entry(struct radv_shader_variant, s, &slab->shaders, slab_list) {
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
			if (s->bo_offset - offset >= shader->code_size) {
				shader->bo = slab->bo;
				shader->bo_offset = offset;
				list_addtail(&shader->slab_list, &s->slab_list);
				mtx_unlock(&device->shader_slab_mutex);
				return slab->ptr + offset;
			}
			offset = align_u64(s->bo_offset + s->code_size, 256);
		}
		if (offset <= slab->size && slab->size - offset >= shader->code_size) {
			shader->bo = slab->bo;
			shader->bo_offset = offset;
			list_addtail(&shader->slab_list, &slab->shaders);
			mtx_unlock(&device->shader_slab_mutex);
			return slab->ptr + offset;
		}
	}

	mtx_unlock(&device->shader_slab_mutex);
	struct radv_shader_slab *slab = calloc(1, sizeof(struct radv_shader_slab));

	slab->size = MAX2(256 * 1024, shader->code_size);
	slab->bo = device->ws->buffer_create(device->ws, slab->size, 256,
	                                     RADEON_DOMAIN_VRAM,
					     RADEON_FLAG_NO_INTERPROCESS_SHARING |
					     (device->physical_device->rad_info.cpdma_prefetch_writes_memory ?
					             0 : RADEON_FLAG_READ_ONLY),
					     RADV_BO_PRIORITY_SHADER);
	if (!slab->bo) {
		free(slab);
		return NULL;
	}

	slab->ptr = (char*)device->ws->buffer_map(slab->bo);
	if (!slab->ptr) {
		device->ws->buffer_destroy(slab->bo);
		free(slab);
		return NULL;
	}

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

static void radv_postprocess_config(const struct radv_device *device,
				    const struct ac_shader_config *config_in,
				    const struct radv_shader_info *info,
				    gl_shader_stage stage,
				    struct ac_shader_config *config_out)
{
	const struct radv_physical_device *pdevice = device->physical_device;
	bool scratch_enabled = config_in->scratch_bytes_per_wave > 0;
	bool trap_enabled = !!device->trap_handler_shader;
	unsigned vgpr_comp_cnt = 0;
	unsigned num_input_vgprs = info->num_input_vgprs;

	if (stage == MESA_SHADER_FRAGMENT) {
		num_input_vgprs = ac_get_fs_input_vgpr_cnt(config_in, NULL, NULL);
	}

	unsigned num_vgprs = MAX2(config_in->num_vgprs, num_input_vgprs);
	/* +3 for scratch wave offset and VCC */
	unsigned num_sgprs = MAX2(config_in->num_sgprs, info->num_input_sgprs + 3);
	unsigned num_shared_vgprs = config_in->num_shared_vgprs;
	/* shared VGPRs are introduced in Navi and are allocated in blocks of 8 (RDNA ref 3.6.5) */
	assert((pdevice->rad_info.chip_class >= GFX10 && num_shared_vgprs % 8 == 0)
	       || (pdevice->rad_info.chip_class < GFX10 && num_shared_vgprs == 0));
	unsigned num_shared_vgpr_blocks = num_shared_vgprs / 8;
	unsigned excp_en = 0;

	*config_out = *config_in;
	config_out->num_vgprs = num_vgprs;
	config_out->num_sgprs = num_sgprs;
	config_out->num_shared_vgprs = num_shared_vgprs;

	config_out->rsrc2 = S_00B12C_USER_SGPR(info->num_user_sgprs) |
			    S_00B12C_SCRATCH_EN(scratch_enabled) |
			    S_00B12C_TRAP_PRESENT(trap_enabled);

	if (trap_enabled) {
		/* Configure the shader exceptions like memory violation, etc.
		 * TODO: Enable (and validate) more exceptions.
		 */
		excp_en = 1 << 8; /* mem_viol */
	}

	if (!pdevice->use_ngg_streamout) {
		config_out->rsrc2 |= S_00B12C_SO_BASE0_EN(!!info->so.strides[0]) |
				     S_00B12C_SO_BASE1_EN(!!info->so.strides[1]) |
				     S_00B12C_SO_BASE2_EN(!!info->so.strides[2]) |
				     S_00B12C_SO_BASE3_EN(!!info->so.strides[3]) |
				     S_00B12C_SO_EN(!!info->so.num_outputs);
	}

	config_out->rsrc1 = S_00B848_VGPRS((num_vgprs - 1) /
					   (info->wave_size == 32 ? 8 : 4)) |
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
			config_out->rsrc2 |= S_00B22C_OC_LDS_EN(1) |
					     S_00B22C_EXCP_EN(excp_en);
		} else if (info->tes.as_es) {
			assert(pdevice->rad_info.chip_class <= GFX8);
			vgpr_comp_cnt = info->uses_prim_id ? 3 : 2;

			config_out->rsrc2 |= S_00B12C_OC_LDS_EN(1) |
					     S_00B12C_EXCP_EN(excp_en);
		} else {
			bool enable_prim_id = info->tes.export_prim_id || info->uses_prim_id;
			vgpr_comp_cnt = enable_prim_id ? 3 : 2;

			config_out->rsrc1 |= S_00B128_MEM_ORDERED(pdevice->rad_info.chip_class >= GFX10);
			config_out->rsrc2 |= S_00B12C_OC_LDS_EN(1) |
					     S_00B12C_EXCP_EN(excp_en);
		}
		config_out->rsrc2 |= S_00B22C_SHARED_VGPR_CNT(num_shared_vgpr_blocks);
		break;
	case MESA_SHADER_TESS_CTRL:
		if (pdevice->rad_info.chip_class >= GFX9) {
			/* We need at least 2 components for LS.
			 * VGPR0-3: (VertexID, RelAutoindex, InstanceID / StepRate0, InstanceID).
			 * StepRate0 is set to 1. so that VGPR3 doesn't have to be loaded.
			 */
			if (pdevice->rad_info.chip_class >= GFX10) {
				vgpr_comp_cnt = info->vs.needs_instance_id ? 3 : 1;
				config_out->rsrc2 |= S_00B42C_LDS_SIZE_GFX10(info->tcs.num_lds_blocks) |
						     S_00B42C_EXCP_EN_GFX6(excp_en);
			} else {
				vgpr_comp_cnt = info->vs.needs_instance_id ? 2 : 1;
				config_out->rsrc2 |= S_00B42C_LDS_SIZE_GFX9(info->tcs.num_lds_blocks) |
						     S_00B42C_EXCP_EN_GFX9(excp_en);
			}
		} else {
			config_out->rsrc2 |= S_00B12C_OC_LDS_EN(1) |
					     S_00B12C_EXCP_EN(excp_en);
		}
		config_out->rsrc1 |= S_00B428_MEM_ORDERED(pdevice->rad_info.chip_class >= GFX10) |
				     S_00B848_WGP_MODE(pdevice->rad_info.chip_class >= GFX10);
		config_out->rsrc2 |= S_00B42C_SHARED_VGPR_CNT(num_shared_vgpr_blocks);
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
			vgpr_comp_cnt = info->vs.needs_instance_id ? 2 : 1;
		} else if (info->vs.as_es) {
			assert(pdevice->rad_info.chip_class <= GFX8);
			/* VGPR0-3: (VertexID, InstanceID / StepRate0, ...) */
			vgpr_comp_cnt = info->vs.needs_instance_id ? 1 : 0;
		} else {
			/* VGPR0-3: (VertexID, InstanceID / StepRate0, PrimID, InstanceID)
			 * If PrimID is disabled. InstanceID / StepRate1 is loaded instead.
			 * StepRate0 is set to 1. so that VGPR3 doesn't have to be loaded.
			 */
			if (info->vs.needs_instance_id && pdevice->rad_info.chip_class >= GFX10) {
				vgpr_comp_cnt = 3;
			} else if (info->vs.export_prim_id) {
				vgpr_comp_cnt = 2;
			} else if (info->vs.needs_instance_id) {
				vgpr_comp_cnt = 1;
			} else {
				vgpr_comp_cnt = 0;
			}

			config_out->rsrc1 |= S_00B128_MEM_ORDERED(pdevice->rad_info.chip_class >= GFX10);
		}
		config_out->rsrc2 |= S_00B12C_SHARED_VGPR_CNT(num_shared_vgpr_blocks) |
				     S_00B12C_EXCP_EN(excp_en);
		break;
	case MESA_SHADER_FRAGMENT:
		config_out->rsrc1 |= S_00B028_MEM_ORDERED(pdevice->rad_info.chip_class >= GFX10);
		config_out->rsrc2 |= S_00B02C_SHARED_VGPR_CNT(num_shared_vgpr_blocks) |
				     S_00B02C_TRAP_PRESENT(1) |
				     S_00B02C_EXCP_EN(excp_en);
		break;
	case MESA_SHADER_GEOMETRY:
		config_out->rsrc1 |= S_00B228_MEM_ORDERED(pdevice->rad_info.chip_class >= GFX10) |
				     S_00B848_WGP_MODE(pdevice->rad_info.chip_class >= GFX10);
		config_out->rsrc2 |= S_00B22C_SHARED_VGPR_CNT(num_shared_vgpr_blocks) |
				     S_00B22C_EXCP_EN(excp_en);
		break;
	case MESA_SHADER_COMPUTE:
		config_out->rsrc1 |= S_00B848_MEM_ORDERED(pdevice->rad_info.chip_class >= GFX10) |
				     S_00B848_WGP_MODE(pdevice->rad_info.chip_class >= GFX10);
		config_out->rsrc2 |=
			S_00B84C_TGID_X_EN(info->cs.uses_block_id[0]) |
			S_00B84C_TGID_Y_EN(info->cs.uses_block_id[1]) |
			S_00B84C_TGID_Z_EN(info->cs.uses_block_id[2]) |
			S_00B84C_TIDIG_COMP_CNT(info->cs.uses_thread_id[2] ? 2 :
						info->cs.uses_thread_id[1] ? 1 : 0) |
			S_00B84C_TG_SIZE_EN(info->cs.uses_local_invocation_idx) |
			S_00B84C_LDS_SIZE(config_in->lds_size) |
			S_00B84C_EXCP_EN(excp_en);
		config_out->rsrc3 |= S_00B8A0_SHARED_VGPR_CNT(num_shared_vgpr_blocks);

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
			es_vgpr_comp_cnt = info->vs.needs_instance_id ? 3 : 0;
		} else if (es_stage == MESA_SHADER_TESS_EVAL) {
			bool enable_prim_id = info->tes.export_prim_id || info->uses_prim_id;
			es_vgpr_comp_cnt = enable_prim_id ? 3 : 2;
		} else
			unreachable("Unexpected ES shader stage");

		bool tes_triangles = stage == MESA_SHADER_TESS_EVAL &&
			info->tes.primitive_mode >= 4; /* GL_TRIANGLES */
		if (info->uses_invocation_id || stage == MESA_SHADER_VERTEX) {
			gs_vgpr_comp_cnt = 3; /* VGPR3 contains InvocationID. */
		} else if (info->uses_prim_id) {
			gs_vgpr_comp_cnt = 2; /* VGPR2 contains PrimitiveID. */
		} else if (info->gs.vertices_in >= 3 || tes_triangles) {
			gs_vgpr_comp_cnt = 1; /* VGPR1 contains offsets 2, 3 */
		} else {
			gs_vgpr_comp_cnt = 0; /* VGPR0 contains offsets 0, 1 */
		}

		/* Disable the WGP mode on gfx10.3 because it can hang. (it
		 * happened on VanGogh) Let's disable it on all chips that
		 * disable exactly 1 CU per SA for GS.
		 */
		config_out->rsrc1 |= S_00B228_GS_VGPR_COMP_CNT(gs_vgpr_comp_cnt) |
				     S_00B848_WGP_MODE(pdevice->rad_info.chip_class == GFX10);
		config_out->rsrc2 |= S_00B22C_ES_VGPR_COMP_CNT(es_vgpr_comp_cnt) |
				     S_00B22C_LDS_SIZE(config_in->lds_size) |
				     S_00B22C_OC_LDS_EN(es_stage == MESA_SHADER_TESS_EVAL);
	} else if (pdevice->rad_info.chip_class >= GFX9 &&
		   stage == MESA_SHADER_GEOMETRY) {
		unsigned es_type = info->gs.es_type;
		unsigned gs_vgpr_comp_cnt, es_vgpr_comp_cnt;

		if (es_type == MESA_SHADER_VERTEX) {
			/* VGPR0-3: (VertexID, InstanceID / StepRate0, ...) */
			if (info->vs.needs_instance_id) {
				es_vgpr_comp_cnt = pdevice->rad_info.chip_class >= GFX10 ? 3 : 1;
			} else {
				es_vgpr_comp_cnt = 0;
			}
		} else if (es_type == MESA_SHADER_TESS_EVAL) {
			es_vgpr_comp_cnt = info->uses_prim_id ? 3 : 2;
		} else {
			unreachable("invalid shader ES type");
		}

		/* If offsets 4, 5 are used, GS_VGPR_COMP_CNT is ignored and
		 * VGPR[0:4] are always loaded.
		 */
		if (info->uses_invocation_id) {
			gs_vgpr_comp_cnt = 3; /* VGPR3 contains InvocationID. */
		} else if (info->uses_prim_id) {
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

struct radv_shader_variant *
radv_shader_variant_create(struct radv_device *device,
			   const struct radv_shader_binary *binary,
			   bool keep_shader_info)
{
	struct ac_shader_config config = {0};
	struct ac_rtld_binary rtld_binary = {0};
	struct radv_shader_variant *variant = calloc(1, sizeof(struct radv_shader_variant));
	if (!variant)
		return NULL;

	variant->ref_count = 1;

	if (binary->type == RADV_BINARY_TYPE_RTLD) {
		struct ac_rtld_symbol lds_symbols[2];
		unsigned num_lds_symbols = 0;
		const char *elf_data = (const char *)((struct radv_shader_binary_rtld *)binary)->data;
		size_t elf_size = ((struct radv_shader_binary_rtld *)binary)->elf_size;

		if (device->physical_device->rad_info.chip_class >= GFX9 &&
		    (binary->stage == MESA_SHADER_GEOMETRY || binary->info.is_ngg) &&
		    !binary->is_gs_copy_shader) {
			/* We add this symbol even on LLVM <= 8 to ensure that
			 * shader->config.lds_size is set correctly below.
			 */
			struct ac_rtld_symbol *sym = &lds_symbols[num_lds_symbols++];
			sym->name = "esgs_ring";
			sym->size = binary->info.ngg_info.esgs_ring_size;
			sym->align = 64 * 1024;
		}

		if (binary->info.is_ngg &&
		    binary->stage == MESA_SHADER_GEOMETRY) {
			struct ac_rtld_symbol *sym = &lds_symbols[num_lds_symbols++];
			sym->name = "ngg_emit";
			sym->size = binary->info.ngg_info.ngg_emit_size * 4;
			sym->align = 4;
		}

		struct ac_rtld_open_info open_info = {
			.info = &device->physical_device->rad_info,
			.shader_type = binary->stage,
			.wave_size = binary->info.wave_size,
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

		if (!ac_rtld_read_config(&device->physical_device->rad_info,
					 &rtld_binary, &config)) {
			ac_rtld_close(&rtld_binary);
			free(variant);
			return NULL;
		}

		if (rtld_binary.lds_size > 0) {
			unsigned alloc_granularity = device->physical_device->rad_info.chip_class >= GFX7 ? 512 : 256;
			config.lds_size = align(rtld_binary.lds_size, alloc_granularity) / alloc_granularity;
		}

		variant->code_size = rtld_binary.rx_size;
		variant->exec_size = rtld_binary.exec_size;
	} else {
		assert(binary->type == RADV_BINARY_TYPE_LEGACY);
		config = ((struct radv_shader_binary_legacy *)binary)->config;
		variant->code_size = radv_get_shader_binary_size(((struct radv_shader_binary_legacy *)binary)->code_size);
		variant->exec_size = ((struct radv_shader_binary_legacy *)binary)->exec_size;
	}

	variant->info = binary->info;
	radv_postprocess_config(device, &config, &binary->info,
				binary->stage, &variant->config);

	void *dest_ptr = radv_alloc_shader_memory(device, variant);
	if (!dest_ptr) {
		if (binary->type == RADV_BINARY_TYPE_RTLD)
			ac_rtld_close(&rtld_binary);
		free(variant);
		return NULL;
	}

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

		if (keep_shader_info ||
		    (device->instance->debug_flags & RADV_DEBUG_DUMP_SHADERS)) {
			const char *disasm_data;
			size_t disasm_size;
			if (!ac_rtld_get_section_by_name(&rtld_binary, ".AMDGPU.disasm", &disasm_data, &disasm_size)) {
				radv_shader_variant_destroy(device, variant);
				ac_rtld_close(&rtld_binary);
				return NULL;
			}

			variant->ir_string = bin->llvm_ir_size ? strdup((const char*)(bin->data + bin->elf_size)) : NULL;
			variant->disasm_string = malloc(disasm_size + 1);
			memcpy(variant->disasm_string, disasm_data, disasm_size);
			variant->disasm_string[disasm_size] = 0;
		}

		ac_rtld_close(&rtld_binary);
	} else {
		struct radv_shader_binary_legacy* bin = (struct radv_shader_binary_legacy *)binary;
		memcpy(dest_ptr, bin->data + bin->stats_size, bin->code_size);

		/* Add end-of-code markers for the UMR disassembler. */
		uint32_t *ptr32 = (uint32_t *)dest_ptr + bin->code_size / 4;
		for (unsigned i = 0; i < DEBUGGER_NUM_MARKERS; i++)
			ptr32[i] = DEBUGGER_END_OF_CODE_MARKER;

		variant->ir_string = bin->ir_size ? strdup((const char*)(bin->data + bin->stats_size + bin->code_size)) : NULL;
		variant->disasm_string = bin->disasm_size ? strdup((const char*)(bin->data + bin->stats_size + bin->code_size + bin->ir_size)) : NULL;

		if (bin->stats_size) {
			variant->statistics = calloc(bin->stats_size, 1);
			memcpy(variant->statistics, bin->data, bin->stats_size);
		}
	}
	return variant;
}

static char *
radv_dump_nir_shaders(struct nir_shader * const *shaders,
                      int shader_count)
{
	char *data = NULL;
	char *ret = NULL;
	size_t size = 0;
	struct u_memstream mem;
	if (u_memstream_open(&mem, &data, &size)) {
		FILE *const memf = u_memstream_get(&mem);
		for (int i = 0; i < shader_count; ++i)
			nir_print_shader(shaders[i], memf);
		u_memstream_close(&mem);
	}

	ret = malloc(size + 1);
	if (ret) {
		memcpy(ret, data, size);
		ret[size] = 0;
	}
	free(data);
	return ret;
}

static struct radv_shader_variant *
shader_variant_compile(struct radv_device *device,
		       struct radv_shader_module *module,
		       struct nir_shader * const *shaders,
		       int shader_count,
		       gl_shader_stage stage,
		       struct radv_shader_info *info,
		       struct radv_nir_compiler_options *options,
		       bool gs_copy_shader,
		       bool trap_handler_shader,
		       bool keep_shader_info,
		       bool keep_statistic_info,
		       struct radv_shader_binary **binary_out)
{
	enum radeon_family chip_family = device->physical_device->rad_info.family;
	struct radv_shader_binary *binary = NULL;

	struct radv_shader_debug_data debug_data = {
		.device = device,
                .module = module,
        };

	options->family = chip_family;
	options->chip_class = device->physical_device->rad_info.chip_class;
	options->dump_shader = radv_can_dump_shader(device, module, gs_copy_shader);
	options->dump_preoptir = options->dump_shader &&
				 device->instance->debug_flags & RADV_DEBUG_PREOPTIR;
	options->record_ir = keep_shader_info;
	options->record_stats = keep_statistic_info;
	options->check_ir = device->instance->debug_flags & RADV_DEBUG_CHECKIR;
	options->tess_offchip_block_dw_size = device->tess_offchip_block_dw_size;
	options->address32_hi = device->physical_device->rad_info.address32_hi;
	options->has_ls_vgpr_init_bug = device->physical_device->rad_info.has_ls_vgpr_init_bug;
	options->use_ngg_streamout = device->physical_device->use_ngg_streamout;
	options->enable_mrt_output_nan_fixup = device->instance->enable_mrt_output_nan_fixup;
	options->adjust_frag_coord_z = device->adjust_frag_coord_z;
	options->debug.func = radv_compiler_debug;
	options->debug.private_data = &debug_data;

	struct radv_shader_args args = {0};
	args.options = options;
	args.shader_info = info;
	args.is_gs_copy_shader = gs_copy_shader;
	args.is_trap_handler_shader = trap_handler_shader;

	radv_declare_shader_args(&args,
				 gs_copy_shader ? MESA_SHADER_VERTEX
						: shaders[shader_count - 1]->info.stage,
				 shader_count >= 2,
				 shader_count >= 2 ? shaders[shader_count - 2]->info.stage
						   : MESA_SHADER_VERTEX);

	if (radv_use_llvm_for_stage(device, stage) ||
	    options->dump_shader || options->record_ir)
		ac_init_llvm_once();

	if (radv_use_llvm_for_stage(device, stage)) {
		llvm_compile_shader(device, shader_count, shaders, &binary, &args);
	} else {
		aco_compile_shader(shader_count, shaders, &binary, &args);
	}

	binary->info = *info;

	struct radv_shader_variant *variant = radv_shader_variant_create(device, binary,
									 keep_shader_info);
	if (!variant) {
		free(binary);
		return NULL;
	}

	if (options->dump_shader) {
		fprintf(stderr, "%s", radv_get_shader_name(info, shaders[0]->info.stage));
		for (int i = 1; i < shader_count; ++i)
			fprintf(stderr, " + %s", radv_get_shader_name(info, shaders[i]->info.stage));

		fprintf(stderr, "\ndisasm:\n%s\n", variant->disasm_string);
	}


	if (keep_shader_info) {
		variant->nir_string = radv_dump_nir_shaders(shaders, shader_count);
		if (!gs_copy_shader && !trap_handler_shader && !module->nir) {
			variant->spirv = malloc(module->size);
			if (!variant->spirv) {
				free(variant);
				free(binary);
				return NULL;
			}

			memcpy(variant->spirv, module->data, module->size);
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
			   struct radv_shader_info *info,
			   bool keep_shader_info, bool keep_statistic_info,
			   bool disable_optimizations,
			   struct radv_shader_binary **binary_out)
{
	gl_shader_stage stage =  shaders[shader_count - 1]->info.stage;
	struct radv_nir_compiler_options options = {0};

	options.layout = layout;
	if (key)
		options.key = *key;

	options.explicit_scratch_args = !radv_use_llvm_for_stage(device, stage);
	options.robust_buffer_access = device->robust_buffer_access;
	options.disable_optimizations = disable_optimizations;

	return shader_variant_compile(device, module, shaders, shader_count, stage, info,
				      &options, false, false,
				      keep_shader_info, keep_statistic_info, binary_out);
}

struct radv_shader_variant *
radv_create_gs_copy_shader(struct radv_device *device,
			   struct nir_shader *shader,
			   struct radv_shader_info *info,
			   struct radv_shader_binary **binary_out,
			   bool keep_shader_info, bool keep_statistic_info,
			   bool multiview, bool disable_optimizations)
{
	struct radv_nir_compiler_options options = {0};
	gl_shader_stage stage = MESA_SHADER_VERTEX;

	options.explicit_scratch_args = !radv_use_llvm_for_stage(device, stage);
	options.key.has_multiview_view_index = multiview;
	options.disable_optimizations = disable_optimizations;

	return shader_variant_compile(device, NULL, &shader, 1, stage,
				      info, &options, true, false,
				      keep_shader_info, keep_statistic_info, binary_out);
}

struct radv_shader_variant *
radv_create_trap_handler_shader(struct radv_device *device)
{
	struct radv_nir_compiler_options options = {0};
	struct radv_shader_variant *shader = NULL;
	struct radv_shader_binary *binary = NULL;
	struct radv_shader_info info = {0};

	nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, NULL, "meta_trap_handler");

	options.explicit_scratch_args = true;
	info.wave_size = 64;

	shader = shader_variant_compile(device, NULL, &b.shader, 1,
					MESA_SHADER_COMPUTE, &info, &options,
					false, true, true, false, &binary);

	ralloc_free(b.shader);
	free(binary);

	return shader;
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

	free(variant->spirv);
	free(variant->nir_string);
	free(variant->disasm_string);
	free(variant->ir_string);
	free(variant->statistics);
	free(variant);
}

const char *
radv_get_shader_name(struct radv_shader_info *info,
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

unsigned
radv_get_max_workgroup_size(enum chip_class chip_class,
                            gl_shader_stage stage,
                            const unsigned *sizes)
{
	switch (stage) {
	case MESA_SHADER_TESS_CTRL:
		return chip_class >= GFX7 ? 128 : 64;
	case MESA_SHADER_GEOMETRY:
		return chip_class >= GFX9 ? 128 : 64;
	case MESA_SHADER_COMPUTE:
		break;
	default:
		return 0;
	}

	unsigned max_workgroup_size = sizes[0] * sizes[1] * sizes[2];
	return max_workgroup_size;
}

unsigned
radv_get_max_waves(struct radv_device *device,
                   struct radv_shader_variant *variant,
                   gl_shader_stage stage)
{
	enum chip_class chip_class = device->physical_device->rad_info.chip_class;
	unsigned lds_increment = chip_class >= GFX7 ? 512 : 256;
	uint8_t wave_size = variant->info.wave_size;
	struct ac_shader_config *conf = &variant->config;
	unsigned max_simd_waves;
	unsigned lds_per_wave = 0;

	max_simd_waves = device->physical_device->rad_info.max_wave64_per_simd;

	if (stage == MESA_SHADER_FRAGMENT) {
		lds_per_wave = conf->lds_size * lds_increment +
			       align(variant->info.ps.num_interp * 48,
				     lds_increment);
	} else if (stage == MESA_SHADER_COMPUTE) {
		unsigned max_workgroup_size =
			radv_get_max_workgroup_size(chip_class, stage, variant->info.cs.block_size);
		lds_per_wave = (conf->lds_size * lds_increment) /
			       DIV_ROUND_UP(max_workgroup_size, wave_size);
	}

	if (conf->num_sgprs) {
		unsigned sgprs = align(conf->num_sgprs, chip_class >= GFX8 ? 16 : 8);
		max_simd_waves =
			MIN2(max_simd_waves,
			     device->physical_device->rad_info.num_physical_sgprs_per_simd /
			     sgprs);
	}

	if (conf->num_vgprs) {
		unsigned vgprs = align(conf->num_vgprs, wave_size == 32 ? 8 : 4);
		max_simd_waves =
			MIN2(max_simd_waves,
			     device->physical_device->rad_info.num_physical_wave64_vgprs_per_simd / vgprs);
	}

	unsigned max_lds_per_simd = device->physical_device->rad_info.lds_size_per_workgroup / device->physical_device->rad_info.num_simd_per_compute_unit;
	if (lds_per_wave)
		max_simd_waves = MIN2(max_simd_waves, max_lds_per_simd / lds_per_wave);

	return max_simd_waves;
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

			VkShaderStatisticsInfoAMD statistics = {0};
			statistics.shaderStageMask = shaderStage;
			statistics.numPhysicalVgprs = device->physical_device->rad_info.num_physical_wave64_vgprs_per_simd;
			statistics.numPhysicalSgprs = device->physical_device->rad_info.num_physical_sgprs_per_simd;
			statistics.numAvailableSgprs = statistics.numPhysicalSgprs;

			if (stage == MESA_SHADER_COMPUTE) {
				unsigned *local_size = variant->info.cs.block_size;
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
	case VK_SHADER_INFO_TYPE_DISASSEMBLY_AMD: {
		char *out;
	        size_t outsize;
		struct u_memstream mem;
		u_memstream_open(&mem, &out, &outsize);
		FILE *const memf = u_memstream_get(&mem);

		fprintf(memf, "%s:\n", radv_get_shader_name(&variant->info, stage));
		fprintf(memf, "%s\n\n", variant->ir_string);
		fprintf(memf, "%s\n\n", variant->disasm_string);
		radv_dump_shader_stats(device, pipeline, stage, memf);
		u_memstream_close(&mem);

		/* Need to include the null terminator. */
		size_t length = outsize + 1;

		if (!pInfo) {
			*pInfoSize = length;
		} else {
			size_t size = *pInfoSize;
			*pInfoSize = length;

			memcpy(pInfo, out, MIN2(size, length));

			if (size < length)
				result = VK_INCOMPLETE;
		}

		free(out);
		break;
	}
	default:
		/* VK_SHADER_INFO_TYPE_BINARY_AMD unimplemented for now. */
		result = VK_ERROR_FEATURE_NOT_PRESENT;
		break;
	}

	return result;
}

VkResult
radv_dump_shader_stats(struct radv_device *device,
		       struct radv_pipeline *pipeline,
		       gl_shader_stage stage, FILE *output)
{
	struct radv_shader_variant *shader = pipeline->shaders[stage];
	VkPipelineExecutablePropertiesKHR *props = NULL;
	uint32_t prop_count = 0;
	VkResult result;

	VkPipelineInfoKHR pipeline_info = {0};
	pipeline_info.sType = VK_STRUCTURE_TYPE_PIPELINE_INFO_KHR;
	pipeline_info.pipeline = radv_pipeline_to_handle(pipeline);

	result = radv_GetPipelineExecutablePropertiesKHR(radv_device_to_handle(device),
							 &pipeline_info,
							 &prop_count, NULL);
	if (result != VK_SUCCESS)
		return result;

	props = calloc(prop_count, sizeof(*props));
	if (!props)
		return VK_ERROR_OUT_OF_HOST_MEMORY;

	result = radv_GetPipelineExecutablePropertiesKHR(radv_device_to_handle(device),
							 &pipeline_info,
							 &prop_count, props);
	if (result != VK_SUCCESS)
		goto fail;

	for (unsigned exec_idx = 0; exec_idx < prop_count; exec_idx++) {
		if (!(props[exec_idx].stages & mesa_to_vk_shader_stage(stage)))
			continue;

		VkPipelineExecutableStatisticKHR *stats = NULL;
		uint32_t stat_count = 0;

		VkPipelineExecutableInfoKHR exec_info = {0};
		exec_info.pipeline = radv_pipeline_to_handle(pipeline);
		exec_info.executableIndex = exec_idx;

		result = radv_GetPipelineExecutableStatisticsKHR(radv_device_to_handle(device),
								 &exec_info,
								 &stat_count, NULL);
		if (result != VK_SUCCESS)
			goto fail;

		stats = calloc(stat_count, sizeof(*stats));
		if (!stats) {
			result = VK_ERROR_OUT_OF_HOST_MEMORY;
			goto fail;
		}

		result = radv_GetPipelineExecutableStatisticsKHR(radv_device_to_handle(device),
								 &exec_info,
								 &stat_count, stats);
		if (result != VK_SUCCESS) {
			free(stats);
			goto fail;
		}

		fprintf(output, "\n%s:\n",
			radv_get_shader_name(&shader->info, stage));
		fprintf(output, "*** SHADER STATS ***\n");

		for (unsigned i = 0; i < stat_count; i++) {
			fprintf(output, "%s: ", stats[i].name);
			switch (stats[i].format) {
			case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_BOOL32_KHR:
				fprintf(output, "%s", stats[i].value.b32 == VK_TRUE ? "true" : "false");
				break;
			case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_INT64_KHR:
				fprintf(output, "%"PRIi64, stats[i].value.i64);
				break;
			case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR:
				fprintf(output, "%"PRIu64, stats[i].value.u64);
				break;
			case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_FLOAT64_KHR:
				fprintf(output, "%f", stats[i].value.f64);
				break;
			default:
				unreachable("Invalid pipeline statistic format");
			}
			fprintf(output, "\n");
		}

		fprintf(output, "********************\n\n\n");

		free(stats);
	}

fail:
	free(props);
	return result;
}
