#include "radv_meta.h"
#include "nir/nir_builder.h"

#include "sid.h"
#include "radv_cs.h"

static nir_shader *
build_buffer_fill_shader(struct radv_device *dev)
{
	nir_builder b;

	nir_builder_init_simple_shader(&b, NULL, MESA_SHADER_COMPUTE, NULL);
	b.shader->info.name = ralloc_strdup(b.shader, "meta_buffer_fill");
	b.shader->info.cs.local_size[0] = 64;
	b.shader->info.cs.local_size[1] = 1;
	b.shader->info.cs.local_size[2] = 1;

	nir_ssa_def *invoc_id = nir_load_local_invocation_id(&b);
	nir_ssa_def *wg_id = nir_load_work_group_id(&b, 32);
	nir_ssa_def *block_size = nir_imm_ivec4(&b,
						b.shader->info.cs.local_size[0],
						b.shader->info.cs.local_size[1],
						b.shader->info.cs.local_size[2], 0);

	nir_ssa_def *global_id = nir_iadd(&b, nir_imul(&b, wg_id, block_size), invoc_id);

	nir_ssa_def *offset = nir_imul(&b, global_id, nir_imm_int(&b, 16));
	offset = nir_channel(&b, offset, 0);

	nir_ssa_def *dst_buf = radv_meta_load_descriptor(&b, 0, 0);

	nir_intrinsic_instr *load = nir_intrinsic_instr_create(b.shader, nir_intrinsic_load_push_constant);
	nir_intrinsic_set_base(load, 0);
	nir_intrinsic_set_range(load, 4);
	load->src[0] = nir_src_for_ssa(nir_imm_int(&b, 0));
	load->num_components = 1;
	nir_ssa_dest_init(&load->instr, &load->dest, 1, 32, "fill_value");
	nir_builder_instr_insert(&b, &load->instr);

	nir_ssa_def *swizzled_load = nir_swizzle(&b, &load->dest.ssa, (unsigned[]) { 0, 0, 0, 0}, 4);

	nir_intrinsic_instr *store = nir_intrinsic_instr_create(b.shader, nir_intrinsic_store_ssbo);
	store->src[0] = nir_src_for_ssa(swizzled_load);
	store->src[1] = nir_src_for_ssa(dst_buf);
	store->src[2] = nir_src_for_ssa(offset);
	nir_intrinsic_set_write_mask(store, 0xf);
	nir_intrinsic_set_access(store, ACCESS_NON_READABLE);
	nir_intrinsic_set_align(store, 16, 0);
	store->num_components = 4;
	nir_builder_instr_insert(&b, &store->instr);

	return b.shader;
}

static nir_shader *
build_buffer_copy_shader(struct radv_device *dev)
{
	nir_builder b;

	nir_builder_init_simple_shader(&b, NULL, MESA_SHADER_COMPUTE, NULL);
	b.shader->info.name = ralloc_strdup(b.shader, "meta_buffer_copy");
	b.shader->info.cs.local_size[0] = 64;
	b.shader->info.cs.local_size[1] = 1;
	b.shader->info.cs.local_size[2] = 1;

	nir_ssa_def *invoc_id = nir_load_local_invocation_id(&b);
	nir_ssa_def *wg_id = nir_load_work_group_id(&b, 32);
	nir_ssa_def *block_size = nir_imm_ivec4(&b,
						b.shader->info.cs.local_size[0],
						b.shader->info.cs.local_size[1],
						b.shader->info.cs.local_size[2], 0);

	nir_ssa_def *global_id = nir_iadd(&b, nir_imul(&b, wg_id, block_size), invoc_id);

	nir_ssa_def *offset = nir_imul(&b, global_id, nir_imm_int(&b, 16));
	offset = nir_channel(&b, offset, 0);

	nir_ssa_def *dst_buf = radv_meta_load_descriptor(&b, 0, 0);
	nir_ssa_def *src_buf = radv_meta_load_descriptor(&b, 0, 1);

	nir_intrinsic_instr *load = nir_intrinsic_instr_create(b.shader, nir_intrinsic_load_ssbo);
	load->src[0] = nir_src_for_ssa(src_buf);
	load->src[1] = nir_src_for_ssa(offset);
	nir_ssa_dest_init(&load->instr, &load->dest, 4, 32, NULL);
	load->num_components = 4;
	nir_intrinsic_set_align(load, 16, 0);
	nir_builder_instr_insert(&b, &load->instr);

	nir_intrinsic_instr *store = nir_intrinsic_instr_create(b.shader, nir_intrinsic_store_ssbo);
	store->src[0] = nir_src_for_ssa(&load->dest.ssa);
	store->src[1] = nir_src_for_ssa(dst_buf);
	store->src[2] = nir_src_for_ssa(offset);
	nir_intrinsic_set_write_mask(store, 0xf);
	nir_intrinsic_set_access(store, ACCESS_NON_READABLE);
	nir_intrinsic_set_align(store, 16, 0);
	store->num_components = 4;
	nir_builder_instr_insert(&b, &store->instr);

	return b.shader;
}



VkResult radv_device_init_meta_buffer_state(struct radv_device *device)
{
	VkResult result;
	struct radv_shader_module fill_cs = { .nir = NULL };
	struct radv_shader_module copy_cs = { .nir = NULL };

	fill_cs.nir = build_buffer_fill_shader(device);
	copy_cs.nir = build_buffer_copy_shader(device);

	VkDescriptorSetLayoutCreateInfo fill_ds_create_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR,
		.bindingCount = 1,
		.pBindings = (VkDescriptorSetLayoutBinding[]) {
			{
				.binding = 0,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
				.pImmutableSamplers = NULL
			},
		}
	};

	result = radv_CreateDescriptorSetLayout(radv_device_to_handle(device),
						&fill_ds_create_info,
						&device->meta_state.alloc,
						&device->meta_state.buffer.fill_ds_layout);
	if (result != VK_SUCCESS)
		goto fail;

	VkDescriptorSetLayoutCreateInfo copy_ds_create_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR,
		.bindingCount = 2,
		.pBindings = (VkDescriptorSetLayoutBinding[]) {
			{
				.binding = 0,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
				.pImmutableSamplers = NULL
			},
			{
				.binding = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
				.pImmutableSamplers = NULL
			},
		}
	};

	result = radv_CreateDescriptorSetLayout(radv_device_to_handle(device),
						&copy_ds_create_info,
						&device->meta_state.alloc,
						&device->meta_state.buffer.copy_ds_layout);
	if (result != VK_SUCCESS)
		goto fail;


	VkPipelineLayoutCreateInfo fill_pl_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &device->meta_state.buffer.fill_ds_layout,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &(VkPushConstantRange){VK_SHADER_STAGE_COMPUTE_BIT, 0, 4},
	};

	result = radv_CreatePipelineLayout(radv_device_to_handle(device),
					  &fill_pl_create_info,
					  &device->meta_state.alloc,
					  &device->meta_state.buffer.fill_p_layout);
	if (result != VK_SUCCESS)
		goto fail;

	VkPipelineLayoutCreateInfo copy_pl_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &device->meta_state.buffer.copy_ds_layout,
		.pushConstantRangeCount = 0,
	};

	result = radv_CreatePipelineLayout(radv_device_to_handle(device),
					  &copy_pl_create_info,
					  &device->meta_state.alloc,
					  &device->meta_state.buffer.copy_p_layout);
	if (result != VK_SUCCESS)
		goto fail;

	VkPipelineShaderStageCreateInfo fill_pipeline_shader_stage = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.stage = VK_SHADER_STAGE_COMPUTE_BIT,
		.module = radv_shader_module_to_handle(&fill_cs),
		.pName = "main",
		.pSpecializationInfo = NULL,
	};

	VkComputePipelineCreateInfo fill_vk_pipeline_info = {
		.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
		.stage = fill_pipeline_shader_stage,
		.flags = 0,
		.layout = device->meta_state.buffer.fill_p_layout,
	};

	result = radv_CreateComputePipelines(radv_device_to_handle(device),
					     radv_pipeline_cache_to_handle(&device->meta_state.cache),
					     1, &fill_vk_pipeline_info, NULL,
					     &device->meta_state.buffer.fill_pipeline);
	if (result != VK_SUCCESS)
		goto fail;

	VkPipelineShaderStageCreateInfo copy_pipeline_shader_stage = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.stage = VK_SHADER_STAGE_COMPUTE_BIT,
		.module = radv_shader_module_to_handle(&copy_cs),
		.pName = "main",
		.pSpecializationInfo = NULL,
	};

	VkComputePipelineCreateInfo copy_vk_pipeline_info = {
		.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
		.stage = copy_pipeline_shader_stage,
		.flags = 0,
		.layout = device->meta_state.buffer.copy_p_layout,
	};

	result = radv_CreateComputePipelines(radv_device_to_handle(device),
					     radv_pipeline_cache_to_handle(&device->meta_state.cache),
					     1, &copy_vk_pipeline_info, NULL,
					     &device->meta_state.buffer.copy_pipeline);
	if (result != VK_SUCCESS)
		goto fail;

	ralloc_free(fill_cs.nir);
	ralloc_free(copy_cs.nir);
	return VK_SUCCESS;
fail:
	radv_device_finish_meta_buffer_state(device);
	ralloc_free(fill_cs.nir);
	ralloc_free(copy_cs.nir);
	return result;
}

void radv_device_finish_meta_buffer_state(struct radv_device *device)
{
	struct radv_meta_state *state = &device->meta_state;

	radv_DestroyPipeline(radv_device_to_handle(device),
			     state->buffer.copy_pipeline, &state->alloc);
	radv_DestroyPipeline(radv_device_to_handle(device),
			     state->buffer.fill_pipeline, &state->alloc);
	radv_DestroyPipelineLayout(radv_device_to_handle(device),
				   state->buffer.copy_p_layout, &state->alloc);
	radv_DestroyPipelineLayout(radv_device_to_handle(device),
				   state->buffer.fill_p_layout, &state->alloc);
	radv_DestroyDescriptorSetLayout(radv_device_to_handle(device),
					state->buffer.copy_ds_layout,
					&state->alloc);
	radv_DestroyDescriptorSetLayout(radv_device_to_handle(device),
					state->buffer.fill_ds_layout,
					&state->alloc);
}

static void fill_buffer_shader(struct radv_cmd_buffer *cmd_buffer,
			       struct radeon_winsys_bo *bo,
			       uint64_t offset, uint64_t size, uint32_t value)
{
	struct radv_device *device = cmd_buffer->device;
	uint64_t block_count = round_up_u64(size, 1024);
	struct radv_meta_saved_state saved_state;

	radv_meta_save(&saved_state, cmd_buffer,
		       RADV_META_SAVE_COMPUTE_PIPELINE |
		       RADV_META_SAVE_CONSTANTS |
		       RADV_META_SAVE_DESCRIPTORS);

	struct radv_buffer dst_buffer = {
		.bo = bo,
		.offset = offset,
		.size = size
	};

	radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer),
			     VK_PIPELINE_BIND_POINT_COMPUTE,
			     device->meta_state.buffer.fill_pipeline);

	radv_meta_push_descriptor_set(cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
			              device->meta_state.buffer.fill_p_layout,
				      0, /* set */
				      1, /* descriptorWriteCount */
				      (VkWriteDescriptorSet[]) {
				              {
				                      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				                      .dstBinding = 0,
				                      .dstArrayElement = 0,
				                      .descriptorCount = 1,
				                      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				                      .pBufferInfo = &(VkDescriptorBufferInfo) {
				                              .buffer = radv_buffer_to_handle(&dst_buffer),
				                              .offset = 0,
				                              .range = size
				                      }
				              }
				      });

	radv_CmdPushConstants(radv_cmd_buffer_to_handle(cmd_buffer),
			      device->meta_state.buffer.fill_p_layout,
			      VK_SHADER_STAGE_COMPUTE_BIT, 0, 4,
			      &value);

	radv_CmdDispatch(radv_cmd_buffer_to_handle(cmd_buffer), block_count, 1, 1);

	radv_meta_restore(&saved_state, cmd_buffer);
}

static void copy_buffer_shader(struct radv_cmd_buffer *cmd_buffer,
			       struct radeon_winsys_bo *src_bo,
			       struct radeon_winsys_bo *dst_bo,
			       uint64_t src_offset, uint64_t dst_offset,
			       uint64_t size)
{
	struct radv_device *device = cmd_buffer->device;
	uint64_t block_count = round_up_u64(size, 1024);
	struct radv_meta_saved_state saved_state;

	radv_meta_save(&saved_state, cmd_buffer,
		       RADV_META_SAVE_COMPUTE_PIPELINE |
		       RADV_META_SAVE_DESCRIPTORS);

	struct radv_buffer dst_buffer = {
		.bo = dst_bo,
		.offset = dst_offset,
		.size = size
	};

	struct radv_buffer src_buffer = {
		.bo = src_bo,
		.offset = src_offset,
		.size = size
	};

	radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer),
			     VK_PIPELINE_BIND_POINT_COMPUTE,
			     device->meta_state.buffer.copy_pipeline);

	radv_meta_push_descriptor_set(cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
			              device->meta_state.buffer.copy_p_layout,
				      0, /* set */
				      2, /* descriptorWriteCount */
				      (VkWriteDescriptorSet[]) {
				              {
				                      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				                      .dstBinding = 0,
				                      .dstArrayElement = 0,
				                      .descriptorCount = 1,
				                      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				                      .pBufferInfo = &(VkDescriptorBufferInfo) {
				                              .buffer = radv_buffer_to_handle(&dst_buffer),
				                              .offset = 0,
				                              .range = size
				                      }
				              },
				              {
				                      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				                      .dstBinding = 1,
				                      .dstArrayElement = 0,
				                      .descriptorCount = 1,
				                      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				                      .pBufferInfo = &(VkDescriptorBufferInfo) {
				                              .buffer = radv_buffer_to_handle(&src_buffer),
				                              .offset = 0,
				                              .range = size
				                      }
				              }
				      });

	radv_CmdDispatch(radv_cmd_buffer_to_handle(cmd_buffer), block_count, 1, 1);

	radv_meta_restore(&saved_state, cmd_buffer);
}


uint32_t radv_fill_buffer(struct radv_cmd_buffer *cmd_buffer,
		      struct radeon_winsys_bo *bo,
		      uint64_t offset, uint64_t size, uint32_t value)
{
	uint32_t flush_bits = 0;

	assert(!(offset & 3));
	assert(!(size & 3));

	if (size >= RADV_BUFFER_OPS_CS_THRESHOLD) {
		fill_buffer_shader(cmd_buffer, bo, offset, size, value);
		flush_bits = RADV_CMD_FLAG_CS_PARTIAL_FLUSH |
			     RADV_CMD_FLAG_INV_VCACHE |
			     RADV_CMD_FLAG_WB_L2;
	} else if (size) {
		uint64_t va = radv_buffer_get_va(bo);
		va += offset;
		radv_cs_add_buffer(cmd_buffer->device->ws, cmd_buffer->cs, bo);
		si_cp_dma_clear_buffer(cmd_buffer, va, size, value);
	}

	return flush_bits;
}

static
void radv_copy_buffer(struct radv_cmd_buffer *cmd_buffer,
		      struct radeon_winsys_bo *src_bo,
		      struct radeon_winsys_bo *dst_bo,
		      uint64_t src_offset, uint64_t dst_offset,
		      uint64_t size)
{
	if (size >= RADV_BUFFER_OPS_CS_THRESHOLD && !(size & 3) && !(src_offset & 3) && !(dst_offset & 3))
		copy_buffer_shader(cmd_buffer, src_bo, dst_bo,
				   src_offset, dst_offset, size);
	else if (size) {
		uint64_t src_va = radv_buffer_get_va(src_bo);
		uint64_t dst_va = radv_buffer_get_va(dst_bo);
		src_va += src_offset;
		dst_va += dst_offset;

		radv_cs_add_buffer(cmd_buffer->device->ws, cmd_buffer->cs, src_bo);
		radv_cs_add_buffer(cmd_buffer->device->ws, cmd_buffer->cs, dst_bo);

		si_cp_dma_buffer_copy(cmd_buffer, src_va, dst_va, size);
	}
}

void radv_CmdFillBuffer(
    VkCommandBuffer                             commandBuffer,
    VkBuffer                                    dstBuffer,
    VkDeviceSize                                dstOffset,
    VkDeviceSize                                fillSize,
    uint32_t                                    data)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	RADV_FROM_HANDLE(radv_buffer, dst_buffer, dstBuffer);

	if (fillSize == VK_WHOLE_SIZE)
		fillSize = (dst_buffer->size - dstOffset) & ~3ull;

	radv_fill_buffer(cmd_buffer, dst_buffer->bo, dst_buffer->offset + dstOffset,
			 fillSize, data);
}

static void
copy_buffer(struct radv_cmd_buffer *cmd_buffer,
	    struct radv_buffer *src_buffer,
	    struct radv_buffer *dst_buffer,
	    const VkBufferCopy2KHR *region)
{
	bool old_predicating;

	/* VK_EXT_conditional_rendering says that copy commands should not be
	 * affected by conditional rendering.
	 */
	old_predicating = cmd_buffer->state.predicating;
	cmd_buffer->state.predicating = false;

	radv_copy_buffer(cmd_buffer,
			 src_buffer->bo,
			 dst_buffer->bo,
			 src_buffer->offset + region->srcOffset,
			 dst_buffer->offset + region->dstOffset,
			 region->size);

	/* Restore conditional rendering. */
	cmd_buffer->state.predicating = old_predicating;
}

void radv_CmdCopyBuffer(
	VkCommandBuffer                             commandBuffer,
	VkBuffer                                    srcBuffer,
	VkBuffer                                    destBuffer,
	uint32_t                                    regionCount,
	const VkBufferCopy*                         pRegions)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	RADV_FROM_HANDLE(radv_buffer, src_buffer, srcBuffer);
	RADV_FROM_HANDLE(radv_buffer, dst_buffer, destBuffer);

	for (unsigned r = 0; r < regionCount; r++) {
		VkBufferCopy2KHR copy = {
			.sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2_KHR,
			.srcOffset = pRegions[r].srcOffset,
			.dstOffset = pRegions[r].dstOffset,
			.size = pRegions[r].size,
		};

		copy_buffer(cmd_buffer, src_buffer, dst_buffer, &copy);
	}
}

void radv_CmdCopyBuffer2KHR(
	VkCommandBuffer                             commandBuffer,
	const VkCopyBufferInfo2KHR*                 pCopyBufferInfo)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	RADV_FROM_HANDLE(radv_buffer, src_buffer, pCopyBufferInfo->srcBuffer);
	RADV_FROM_HANDLE(radv_buffer, dst_buffer, pCopyBufferInfo->dstBuffer);

	for (unsigned r = 0; r < pCopyBufferInfo->regionCount; r++) {
		copy_buffer(cmd_buffer, src_buffer, dst_buffer,
			    &pCopyBufferInfo->pRegions[r]);
	}
}

void radv_CmdUpdateBuffer(
	VkCommandBuffer                             commandBuffer,
	VkBuffer                                    dstBuffer,
	VkDeviceSize                                dstOffset,
	VkDeviceSize                                dataSize,
	const void*                                 pData)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	RADV_FROM_HANDLE(radv_buffer, dst_buffer, dstBuffer);
	bool mec = radv_cmd_buffer_uses_mec(cmd_buffer);
	uint64_t words = dataSize / 4;
	uint64_t va = radv_buffer_get_va(dst_buffer->bo);
	va += dstOffset + dst_buffer->offset;

	assert(!(dataSize & 3));
	assert(!(va & 3));

	if (!dataSize)
		return;

	if (dataSize < RADV_BUFFER_UPDATE_THRESHOLD) {
		si_emit_cache_flush(cmd_buffer);

		radv_cs_add_buffer(cmd_buffer->device->ws, cmd_buffer->cs, dst_buffer->bo);

		radeon_check_space(cmd_buffer->device->ws, cmd_buffer->cs, words + 4);

		radeon_emit(cmd_buffer->cs, PKT3(PKT3_WRITE_DATA, 2 + words, 0));
		radeon_emit(cmd_buffer->cs, S_370_DST_SEL(mec ?
		                                V_370_MEM : V_370_MEM_GRBM) |
		                            S_370_WR_CONFIRM(1) |
		                            S_370_ENGINE_SEL(V_370_ME));
		radeon_emit(cmd_buffer->cs, va);
		radeon_emit(cmd_buffer->cs, va >> 32);
		radeon_emit_array(cmd_buffer->cs, pData, words);

		if (unlikely(cmd_buffer->device->trace_bo))
			radv_cmd_buffer_trace_emit(cmd_buffer);
	} else {
		uint32_t buf_offset;
		radv_cmd_buffer_upload_data(cmd_buffer, dataSize, 32, pData, &buf_offset);
		radv_copy_buffer(cmd_buffer, cmd_buffer->upload.upload_bo, dst_buffer->bo,
				 buf_offset, dstOffset + dst_buffer->offset, dataSize);
	}
}
