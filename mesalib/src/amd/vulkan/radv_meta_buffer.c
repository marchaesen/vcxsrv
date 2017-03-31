#include "radv_meta.h"
#include "nir/nir_builder.h"

#include "sid.h"
#include "radv_cs.h"

static nir_shader *
build_buffer_fill_shader(struct radv_device *dev)
{
	nir_builder b;

	nir_builder_init_simple_shader(&b, NULL, MESA_SHADER_COMPUTE, NULL);
	b.shader->info->name = ralloc_strdup(b.shader, "meta_buffer_fill");
	b.shader->info->cs.local_size[0] = 64;
	b.shader->info->cs.local_size[1] = 1;
	b.shader->info->cs.local_size[2] = 1;

	nir_ssa_def *invoc_id = nir_load_system_value(&b, nir_intrinsic_load_local_invocation_id, 0);
	nir_ssa_def *wg_id = nir_load_system_value(&b, nir_intrinsic_load_work_group_id, 0);
	nir_ssa_def *block_size = nir_imm_ivec4(&b,
						b.shader->info->cs.local_size[0],
						b.shader->info->cs.local_size[1],
						b.shader->info->cs.local_size[2], 0);

	nir_ssa_def *global_id = nir_iadd(&b, nir_imul(&b, wg_id, block_size), invoc_id);

	nir_ssa_def *offset = nir_imul(&b, global_id, nir_imm_int(&b, 16));
	offset = nir_swizzle(&b, offset, (unsigned[]) {0, 0, 0, 0}, 1, false);

	nir_intrinsic_instr *dst_buf = nir_intrinsic_instr_create(b.shader,
	                                                          nir_intrinsic_vulkan_resource_index);
	dst_buf->src[0] = nir_src_for_ssa(nir_imm_int(&b, 0));
	nir_intrinsic_set_desc_set(dst_buf, 0);
	nir_intrinsic_set_binding(dst_buf, 0);
	nir_ssa_dest_init(&dst_buf->instr, &dst_buf->dest, 1, 32, NULL);
	nir_builder_instr_insert(&b, &dst_buf->instr);

	nir_intrinsic_instr *load = nir_intrinsic_instr_create(b.shader, nir_intrinsic_load_push_constant);
	load->src[0] = nir_src_for_ssa(nir_imm_int(&b, 0));
	load->num_components = 1;
	nir_ssa_dest_init(&load->instr, &load->dest, 1, 32, "fill_value");
	nir_builder_instr_insert(&b, &load->instr);

	nir_ssa_def *swizzled_load = nir_swizzle(&b, &load->dest.ssa, (unsigned[]) { 0, 0, 0, 0}, 4, false);

	nir_intrinsic_instr *store = nir_intrinsic_instr_create(b.shader, nir_intrinsic_store_ssbo);
	store->src[0] = nir_src_for_ssa(swizzled_load);
	store->src[1] = nir_src_for_ssa(&dst_buf->dest.ssa);
	store->src[2] = nir_src_for_ssa(offset);
	nir_intrinsic_set_write_mask(store, 0xf);
	store->num_components = 4;
	nir_builder_instr_insert(&b, &store->instr);

	return b.shader;
}

static nir_shader *
build_buffer_copy_shader(struct radv_device *dev)
{
	nir_builder b;

	nir_builder_init_simple_shader(&b, NULL, MESA_SHADER_COMPUTE, NULL);
	b.shader->info->name = ralloc_strdup(b.shader, "meta_buffer_copy");
	b.shader->info->cs.local_size[0] = 64;
	b.shader->info->cs.local_size[1] = 1;
	b.shader->info->cs.local_size[2] = 1;

	nir_ssa_def *invoc_id = nir_load_system_value(&b, nir_intrinsic_load_local_invocation_id, 0);
	nir_ssa_def *wg_id = nir_load_system_value(&b, nir_intrinsic_load_work_group_id, 0);
	nir_ssa_def *block_size = nir_imm_ivec4(&b,
						b.shader->info->cs.local_size[0],
						b.shader->info->cs.local_size[1],
						b.shader->info->cs.local_size[2], 0);

	nir_ssa_def *global_id = nir_iadd(&b, nir_imul(&b, wg_id, block_size), invoc_id);

	nir_ssa_def *offset = nir_imul(&b, global_id, nir_imm_int(&b, 16));
	offset = nir_swizzle(&b, offset, (unsigned[]) {0, 0, 0, 0}, 1, false);

	nir_intrinsic_instr *dst_buf = nir_intrinsic_instr_create(b.shader,
	                                                          nir_intrinsic_vulkan_resource_index);
	dst_buf->src[0] = nir_src_for_ssa(nir_imm_int(&b, 0));
	nir_intrinsic_set_desc_set(dst_buf, 0);
	nir_intrinsic_set_binding(dst_buf, 0);
	nir_ssa_dest_init(&dst_buf->instr, &dst_buf->dest, 1, 32, NULL);
	nir_builder_instr_insert(&b, &dst_buf->instr);

	nir_intrinsic_instr *src_buf = nir_intrinsic_instr_create(b.shader,
	                                                          nir_intrinsic_vulkan_resource_index);
	src_buf->src[0] = nir_src_for_ssa(nir_imm_int(&b, 0));
	nir_intrinsic_set_desc_set(src_buf, 0);
	nir_intrinsic_set_binding(src_buf, 1);
	nir_ssa_dest_init(&src_buf->instr, &src_buf->dest, 1, 32, NULL);
	nir_builder_instr_insert(&b, &src_buf->instr);

	nir_intrinsic_instr *load = nir_intrinsic_instr_create(b.shader, nir_intrinsic_load_ssbo);
	load->src[0] = nir_src_for_ssa(&src_buf->dest.ssa);
	load->src[1] = nir_src_for_ssa(offset);
	nir_ssa_dest_init(&load->instr, &load->dest, 4, 32, NULL);
	load->num_components = 4;
	nir_builder_instr_insert(&b, &load->instr);

	nir_intrinsic_instr *store = nir_intrinsic_instr_create(b.shader, nir_intrinsic_store_ssbo);
	store->src[0] = nir_src_for_ssa(&load->dest.ssa);
	store->src[1] = nir_src_for_ssa(&dst_buf->dest.ssa);
	store->src[2] = nir_src_for_ssa(offset);
	nir_intrinsic_set_write_mask(store, 0xf);
	store->num_components = 4;
	nir_builder_instr_insert(&b, &store->instr);

	return b.shader;
}



VkResult radv_device_init_meta_buffer_state(struct radv_device *device)
{
	VkResult result;
	struct radv_shader_module fill_cs = { .nir = NULL };
	struct radv_shader_module copy_cs = { .nir = NULL };

	zero(device->meta_state.buffer);

	fill_cs.nir = build_buffer_fill_shader(device);
	copy_cs.nir = build_buffer_copy_shader(device);

	VkDescriptorSetLayoutCreateInfo fill_ds_create_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
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
	if (device->meta_state.buffer.copy_pipeline)
		radv_DestroyPipeline(radv_device_to_handle(device),
				     device->meta_state.buffer.copy_pipeline,
				     &device->meta_state.alloc);

	if (device->meta_state.buffer.fill_pipeline)
		radv_DestroyPipeline(radv_device_to_handle(device),
				     device->meta_state.buffer.fill_pipeline,
				     &device->meta_state.alloc);

	if (device->meta_state.buffer.copy_p_layout)
		radv_DestroyPipelineLayout(radv_device_to_handle(device),
					   device->meta_state.buffer.copy_p_layout,
					   &device->meta_state.alloc);

	if (device->meta_state.buffer.fill_p_layout)
		radv_DestroyPipelineLayout(radv_device_to_handle(device),
					   device->meta_state.buffer.fill_p_layout,
					   &device->meta_state.alloc);

	if (device->meta_state.buffer.copy_ds_layout)
		radv_DestroyDescriptorSetLayout(radv_device_to_handle(device),
						device->meta_state.buffer.copy_ds_layout,
						&device->meta_state.alloc);

	if (device->meta_state.buffer.fill_ds_layout)
		radv_DestroyDescriptorSetLayout(radv_device_to_handle(device),
						device->meta_state.buffer.fill_ds_layout,
						&device->meta_state.alloc);
}

static void fill_buffer_shader(struct radv_cmd_buffer *cmd_buffer,
			       struct radeon_winsys_bo *bo,
			       uint64_t offset, uint64_t size, uint32_t value)
{
	struct radv_device *device = cmd_buffer->device;
	uint64_t block_count = round_up_u64(size, 1024);
	struct radv_meta_saved_compute_state saved_state;
	VkDescriptorSet ds;

	radv_meta_save_compute(&saved_state, cmd_buffer, 4);

	radv_temp_descriptor_set_create(device, cmd_buffer,
					device->meta_state.buffer.fill_ds_layout,
					&ds);

	struct radv_buffer dst_buffer = {
		.bo = bo,
		.offset = offset,
		.size = size
	};

	radv_UpdateDescriptorSets(radv_device_to_handle(device),
				  1, /* writeCount */
				  (VkWriteDescriptorSet[]) {
					  {
						  .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
						  .dstSet = ds,
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
				  }, 0, NULL);

	radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer),
			     VK_PIPELINE_BIND_POINT_COMPUTE,
			     device->meta_state.buffer.fill_pipeline);

	radv_CmdBindDescriptorSets(radv_cmd_buffer_to_handle(cmd_buffer),
				   VK_PIPELINE_BIND_POINT_COMPUTE,
				   device->meta_state.buffer.fill_p_layout, 0, 1,
				   &ds, 0, NULL);

	radv_CmdPushConstants(radv_cmd_buffer_to_handle(cmd_buffer),
			      device->meta_state.buffer.fill_p_layout,
			      VK_SHADER_STAGE_COMPUTE_BIT, 0, 4,
			      &value);

	radv_CmdDispatch(radv_cmd_buffer_to_handle(cmd_buffer), block_count, 1, 1);

	radv_temp_descriptor_set_destroy(device, ds);

	radv_meta_restore_compute(&saved_state, cmd_buffer, 4);
}

static void copy_buffer_shader(struct radv_cmd_buffer *cmd_buffer,
			       struct radeon_winsys_bo *src_bo,
			       struct radeon_winsys_bo *dst_bo,
			       uint64_t src_offset, uint64_t dst_offset,
			       uint64_t size)
{
	struct radv_device *device = cmd_buffer->device;
	uint64_t block_count = round_up_u64(size, 1024);
	struct radv_meta_saved_compute_state saved_state;
	VkDescriptorSet ds;

	radv_meta_save_compute(&saved_state, cmd_buffer, 0);

	radv_temp_descriptor_set_create(device, cmd_buffer,
					device->meta_state.buffer.copy_ds_layout,
					&ds);

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

	radv_UpdateDescriptorSets(radv_device_to_handle(device),
				  2, /* writeCount */
				  (VkWriteDescriptorSet[]) {
					  {
						  .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
						  .dstSet = ds,
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
						  .dstSet = ds,
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
				  }, 0, NULL);

	radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer),
			     VK_PIPELINE_BIND_POINT_COMPUTE,
			     device->meta_state.buffer.copy_pipeline);

	radv_CmdBindDescriptorSets(radv_cmd_buffer_to_handle(cmd_buffer),
				   VK_PIPELINE_BIND_POINT_COMPUTE,
				   device->meta_state.buffer.copy_p_layout, 0, 1,
				   &ds, 0, NULL);


	radv_CmdDispatch(radv_cmd_buffer_to_handle(cmd_buffer), block_count, 1, 1);

	radv_temp_descriptor_set_destroy(device, ds);

	radv_meta_restore_compute(&saved_state, cmd_buffer, 0);
}


void radv_fill_buffer(struct radv_cmd_buffer *cmd_buffer,
		      struct radeon_winsys_bo *bo,
		      uint64_t offset, uint64_t size, uint32_t value)
{
	assert(!(offset & 3));
	assert(!(size & 3));

	if (size >= 4096)
		fill_buffer_shader(cmd_buffer, bo, offset, size, value);
	else if (size) {
		uint64_t va = cmd_buffer->device->ws->buffer_get_va(bo);
		va += offset;
		cmd_buffer->device->ws->cs_add_buffer(cmd_buffer->cs, bo, 8);
		si_cp_dma_clear_buffer(cmd_buffer, va, size, value);
	}
}

static
void radv_copy_buffer(struct radv_cmd_buffer *cmd_buffer,
		      struct radeon_winsys_bo *src_bo,
		      struct radeon_winsys_bo *dst_bo,
		      uint64_t src_offset, uint64_t dst_offset,
		      uint64_t size)
{
	if (size >= 4096 && !(size & 3) && !(src_offset & 3) && !(dst_offset & 3))
		copy_buffer_shader(cmd_buffer, src_bo, dst_bo,
				   src_offset, dst_offset, size);
	else if (size) {
		uint64_t src_va = cmd_buffer->device->ws->buffer_get_va(src_bo);
		uint64_t dst_va = cmd_buffer->device->ws->buffer_get_va(dst_bo);
		src_va += src_offset;
		dst_va += dst_offset;

		cmd_buffer->device->ws->cs_add_buffer(cmd_buffer->cs, src_bo, 8);
		cmd_buffer->device->ws->cs_add_buffer(cmd_buffer->cs, dst_bo, 8);

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

void radv_CmdCopyBuffer(
	VkCommandBuffer                             commandBuffer,
	VkBuffer                                    srcBuffer,
	VkBuffer                                    destBuffer,
	uint32_t                                    regionCount,
	const VkBufferCopy*                         pRegions)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	RADV_FROM_HANDLE(radv_buffer, src_buffer, srcBuffer);
	RADV_FROM_HANDLE(radv_buffer, dest_buffer, destBuffer);

	for (unsigned r = 0; r < regionCount; r++) {
		uint64_t src_offset = src_buffer->offset + pRegions[r].srcOffset;
		uint64_t dest_offset = dest_buffer->offset + pRegions[r].dstOffset;
		uint64_t copy_size = pRegions[r].size;

		radv_copy_buffer(cmd_buffer, src_buffer->bo, dest_buffer->bo,
				 src_offset, dest_offset, copy_size);
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
	uint64_t va = cmd_buffer->device->ws->buffer_get_va(dst_buffer->bo);
	va += dstOffset + dst_buffer->offset;

	assert(!(dataSize & 3));
	assert(!(va & 3));

	if (dataSize < 4096) {
		si_emit_cache_flush(cmd_buffer);

		cmd_buffer->device->ws->cs_add_buffer(cmd_buffer->cs, dst_buffer->bo, 8);

		radeon_check_space(cmd_buffer->device->ws, cmd_buffer->cs, words + 4);

		radeon_emit(cmd_buffer->cs, PKT3(PKT3_WRITE_DATA, 2 + words, 0));
		radeon_emit(cmd_buffer->cs, S_370_DST_SEL(mec ?
		                                V_370_MEM_ASYNC : V_370_MEMORY_SYNC) |
		                            S_370_WR_CONFIRM(1) |
		                            S_370_ENGINE_SEL(V_370_ME));
		radeon_emit(cmd_buffer->cs, va);
		radeon_emit(cmd_buffer->cs, va >> 32);
		radeon_emit_array(cmd_buffer->cs, pData, words);
	} else {
		uint32_t buf_offset;
		radv_cmd_buffer_upload_data(cmd_buffer, dataSize, 32, pData, &buf_offset);
		radv_copy_buffer(cmd_buffer, cmd_buffer->upload.upload_bo, dst_buffer->bo,
				 buf_offset, dstOffset + dst_buffer->offset, dataSize);
	}
}
