#include "nir/nir_builder.h"
#include "radv_meta.h"

#include "radv_cs.h"
#include "sid.h"

static nir_shader *
build_buffer_fill_shader(struct radv_device *dev)
{
   nir_builder b = radv_meta_init_shader(MESA_SHADER_COMPUTE, "meta_buffer_fill");
   b.shader->info.workgroup_size[0] = 64;

   nir_ssa_def *global_id = get_global_ids(&b, 1);

   nir_ssa_def *offset = nir_imul(&b, global_id, nir_imm_int(&b, 16));
   offset = nir_channel(&b, offset, 0);

   nir_ssa_def *dst_buf = radv_meta_load_descriptor(&b, 0, 0);

   nir_ssa_def *load = nir_load_push_constant(&b, 1, 32, nir_imm_int(&b, 0), .range = 4);
   nir_ssa_def *swizzled_load = nir_swizzle(&b, load, (unsigned[]){0, 0, 0, 0}, 4);

   nir_store_ssbo(&b, swizzled_load, dst_buf, offset, .access = ACCESS_NON_READABLE,
                  .align_mul = 16);

   return b.shader;
}

static nir_shader *
build_buffer_copy_shader(struct radv_device *dev)
{
   nir_builder b = radv_meta_init_shader(MESA_SHADER_COMPUTE, "meta_buffer_copy");
   b.shader->info.workgroup_size[0] = 64;

   nir_ssa_def *global_id = get_global_ids(&b, 1);

   nir_ssa_def *offset = nir_imul(&b, global_id, nir_imm_int(&b, 16));
   offset = nir_channel(&b, offset, 0);

   nir_ssa_def *dst_buf = radv_meta_load_descriptor(&b, 0, 0);
   nir_ssa_def *src_buf = radv_meta_load_descriptor(&b, 0, 1);

   nir_ssa_def *load = nir_load_ssbo(&b, 4, 32, src_buf, offset, .align_mul = 16);
   nir_store_ssbo(&b, load, dst_buf, offset, .access = ACCESS_NON_READABLE, .align_mul = 16);

   return b.shader;
}

VkResult
radv_device_init_meta_buffer_state(struct radv_device *device)
{
   VkResult result;
   nir_shader *fill_cs = build_buffer_fill_shader(device);
   nir_shader *copy_cs = build_buffer_copy_shader(device);

   VkDescriptorSetLayoutCreateInfo fill_ds_create_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR,
      .bindingCount = 1,
      .pBindings = (VkDescriptorSetLayoutBinding[]){
         {.binding = 0,
          .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
          .descriptorCount = 1,
          .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
          .pImmutableSamplers = NULL},
      }};

   result = radv_CreateDescriptorSetLayout(radv_device_to_handle(device), &fill_ds_create_info,
                                           &device->meta_state.alloc,
                                           &device->meta_state.buffer.fill_ds_layout);
   if (result != VK_SUCCESS)
      goto fail;

   VkDescriptorSetLayoutCreateInfo copy_ds_create_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR,
      .bindingCount = 2,
      .pBindings = (VkDescriptorSetLayoutBinding[]){
         {.binding = 0,
          .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
          .descriptorCount = 1,
          .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
          .pImmutableSamplers = NULL},
         {.binding = 1,
          .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
          .descriptorCount = 1,
          .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
          .pImmutableSamplers = NULL},
      }};

   result = radv_CreateDescriptorSetLayout(radv_device_to_handle(device), &copy_ds_create_info,
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

   result = radv_CreatePipelineLayout(radv_device_to_handle(device), &fill_pl_create_info,
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

   result = radv_CreatePipelineLayout(radv_device_to_handle(device), &copy_pl_create_info,
                                      &device->meta_state.alloc,
                                      &device->meta_state.buffer.copy_p_layout);
   if (result != VK_SUCCESS)
      goto fail;

   VkPipelineShaderStageCreateInfo fill_pipeline_shader_stage = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_COMPUTE_BIT,
      .module = vk_shader_module_handle_from_nir(fill_cs),
      .pName = "main",
      .pSpecializationInfo = NULL,
   };

   VkComputePipelineCreateInfo fill_vk_pipeline_info = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage = fill_pipeline_shader_stage,
      .flags = 0,
      .layout = device->meta_state.buffer.fill_p_layout,
   };

   result = radv_CreateComputePipelines(
      radv_device_to_handle(device), radv_pipeline_cache_to_handle(&device->meta_state.cache), 1,
      &fill_vk_pipeline_info, NULL, &device->meta_state.buffer.fill_pipeline);
   if (result != VK_SUCCESS)
      goto fail;

   VkPipelineShaderStageCreateInfo copy_pipeline_shader_stage = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_COMPUTE_BIT,
      .module = vk_shader_module_handle_from_nir(copy_cs),
      .pName = "main",
      .pSpecializationInfo = NULL,
   };

   VkComputePipelineCreateInfo copy_vk_pipeline_info = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage = copy_pipeline_shader_stage,
      .flags = 0,
      .layout = device->meta_state.buffer.copy_p_layout,
   };

   result = radv_CreateComputePipelines(
      radv_device_to_handle(device), radv_pipeline_cache_to_handle(&device->meta_state.cache), 1,
      &copy_vk_pipeline_info, NULL, &device->meta_state.buffer.copy_pipeline);
   if (result != VK_SUCCESS)
      goto fail;

   ralloc_free(fill_cs);
   ralloc_free(copy_cs);
   return VK_SUCCESS;
fail:
   radv_device_finish_meta_buffer_state(device);
   ralloc_free(fill_cs);
   ralloc_free(copy_cs);
   return result;
}

void
radv_device_finish_meta_buffer_state(struct radv_device *device)
{
   struct radv_meta_state *state = &device->meta_state;

   radv_DestroyPipeline(radv_device_to_handle(device), state->buffer.copy_pipeline, &state->alloc);
   radv_DestroyPipeline(radv_device_to_handle(device), state->buffer.fill_pipeline, &state->alloc);
   radv_DestroyPipelineLayout(radv_device_to_handle(device), state->buffer.copy_p_layout,
                              &state->alloc);
   radv_DestroyPipelineLayout(radv_device_to_handle(device), state->buffer.fill_p_layout,
                              &state->alloc);
   radv_DestroyDescriptorSetLayout(radv_device_to_handle(device), state->buffer.copy_ds_layout,
                                   &state->alloc);
   radv_DestroyDescriptorSetLayout(radv_device_to_handle(device), state->buffer.fill_ds_layout,
                                   &state->alloc);
}

static void
fill_buffer_shader(struct radv_cmd_buffer *cmd_buffer, struct radeon_winsys_bo *bo, uint64_t offset,
                   uint64_t size, uint32_t value)
{
   struct radv_device *device = cmd_buffer->device;
   uint64_t block_count = round_up_u64(size, 1024);
   struct radv_meta_saved_state saved_state;
   struct radv_buffer dst_buffer;

   radv_meta_save(
      &saved_state, cmd_buffer,
      RADV_META_SAVE_COMPUTE_PIPELINE | RADV_META_SAVE_CONSTANTS | RADV_META_SAVE_DESCRIPTORS);

   radv_buffer_init(&dst_buffer, cmd_buffer->device, bo, size, offset);

   radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer), VK_PIPELINE_BIND_POINT_COMPUTE,
                        device->meta_state.buffer.fill_pipeline);

   radv_meta_push_descriptor_set(
      cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, device->meta_state.buffer.fill_p_layout,
      0, /* set */
      1, /* descriptorWriteCount */
      (VkWriteDescriptorSet[]){
         {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
          .dstBinding = 0,
          .dstArrayElement = 0,
          .descriptorCount = 1,
          .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
          .pBufferInfo = &(VkDescriptorBufferInfo){.buffer = radv_buffer_to_handle(&dst_buffer),
                                                   .offset = 0,
                                                   .range = size}}});

   radv_CmdPushConstants(radv_cmd_buffer_to_handle(cmd_buffer),
                         device->meta_state.buffer.fill_p_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 4,
                         &value);

   radv_CmdDispatch(radv_cmd_buffer_to_handle(cmd_buffer), block_count, 1, 1);

   radv_buffer_finish(&dst_buffer);

   radv_meta_restore(&saved_state, cmd_buffer);
}

static void
copy_buffer_shader(struct radv_cmd_buffer *cmd_buffer, struct radeon_winsys_bo *src_bo,
                   struct radeon_winsys_bo *dst_bo, uint64_t src_offset, uint64_t dst_offset,
                   uint64_t size)
{
   struct radv_device *device = cmd_buffer->device;
   uint64_t block_count = round_up_u64(size, 1024);
   struct radv_meta_saved_state saved_state;
   struct radv_buffer src_buffer, dst_buffer;

   radv_meta_save(&saved_state, cmd_buffer,
                  RADV_META_SAVE_COMPUTE_PIPELINE | RADV_META_SAVE_DESCRIPTORS);

   radv_buffer_init(&src_buffer, cmd_buffer->device, src_bo, size, src_offset);
   radv_buffer_init(&dst_buffer, cmd_buffer->device, dst_bo, size, dst_offset);

   radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer), VK_PIPELINE_BIND_POINT_COMPUTE,
                        device->meta_state.buffer.copy_pipeline);

   radv_meta_push_descriptor_set(
      cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, device->meta_state.buffer.copy_p_layout,
      0, /* set */
      2, /* descriptorWriteCount */
      (VkWriteDescriptorSet[]){
         {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
          .dstBinding = 0,
          .dstArrayElement = 0,
          .descriptorCount = 1,
          .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
          .pBufferInfo = &(VkDescriptorBufferInfo){.buffer = radv_buffer_to_handle(&dst_buffer),
                                                   .offset = 0,
                                                   .range = size}},
         {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
          .dstBinding = 1,
          .dstArrayElement = 0,
          .descriptorCount = 1,
          .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
          .pBufferInfo = &(VkDescriptorBufferInfo){.buffer = radv_buffer_to_handle(&src_buffer),
                                                   .offset = 0,
                                                   .range = size}}});

   radv_CmdDispatch(radv_cmd_buffer_to_handle(cmd_buffer), block_count, 1, 1);

   radv_buffer_finish(&src_buffer);
   radv_buffer_finish(&dst_buffer);

   radv_meta_restore(&saved_state, cmd_buffer);
}

static bool
radv_prefer_compute_dma(const struct radv_device *device, uint64_t size,
                        struct radeon_winsys_bo *src_bo, struct radeon_winsys_bo *dst_bo)
{
   bool use_compute = size >= RADV_BUFFER_OPS_CS_THRESHOLD;

   if (device->physical_device->rad_info.chip_class >= GFX10 &&
       device->physical_device->rad_info.has_dedicated_vram) {
      if ((src_bo && !(src_bo->initial_domain & RADEON_DOMAIN_VRAM)) ||
          !(dst_bo->initial_domain & RADEON_DOMAIN_VRAM)) {
         /* Prefer CP DMA for GTT on dGPUS due to slow PCIe. */
         use_compute = false;
      }
   }

   return use_compute;
}

uint32_t
radv_fill_buffer(struct radv_cmd_buffer *cmd_buffer, const struct radv_image *image,
                 struct radeon_winsys_bo *bo, uint64_t offset, uint64_t size, uint32_t value)
{
   bool use_compute = radv_prefer_compute_dma(cmd_buffer->device, size, NULL, bo);
   uint32_t flush_bits = 0;

   assert(!(offset & 3));
   assert(!(size & 3));

   if (use_compute) {
      cmd_buffer->state.flush_bits |=
         radv_dst_access_flush(cmd_buffer, VK_ACCESS_2_SHADER_WRITE_BIT_KHR, image);

      fill_buffer_shader(cmd_buffer, bo, offset, size, value);

      flush_bits = RADV_CMD_FLAG_CS_PARTIAL_FLUSH | RADV_CMD_FLAG_INV_VCACHE |
                   radv_src_access_flush(cmd_buffer, VK_ACCESS_2_SHADER_WRITE_BIT_KHR, image);
   } else if (size) {
      uint64_t va = radv_buffer_get_va(bo);
      va += offset;
      radv_cs_add_buffer(cmd_buffer->device->ws, cmd_buffer->cs, bo);
      si_cp_dma_clear_buffer(cmd_buffer, va, size, value);
   }

   return flush_bits;
}

void
radv_copy_buffer(struct radv_cmd_buffer *cmd_buffer, struct radeon_winsys_bo *src_bo,
                 struct radeon_winsys_bo *dst_bo, uint64_t src_offset, uint64_t dst_offset,
                 uint64_t size)
{
   bool use_compute = !(size & 3) && !(src_offset & 3) && !(dst_offset & 3) &&
                      radv_prefer_compute_dma(cmd_buffer->device, size, src_bo, dst_bo);

   if (use_compute)
      copy_buffer_shader(cmd_buffer, src_bo, dst_bo, src_offset, dst_offset, size);
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

VKAPI_ATTR void VKAPI_CALL
radv_CmdFillBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset,
                   VkDeviceSize fillSize, uint32_t data)
{
   RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   RADV_FROM_HANDLE(radv_buffer, dst_buffer, dstBuffer);

   if (fillSize == VK_WHOLE_SIZE)
      fillSize = (dst_buffer->size - dstOffset) & ~3ull;

   radv_fill_buffer(cmd_buffer, NULL, dst_buffer->bo, dst_buffer->offset + dstOffset, fillSize,
                    data);
}

static void
copy_buffer(struct radv_cmd_buffer *cmd_buffer, struct radv_buffer *src_buffer,
            struct radv_buffer *dst_buffer, const VkBufferCopy2KHR *region)
{
   bool old_predicating;

   /* VK_EXT_conditional_rendering says that copy commands should not be
    * affected by conditional rendering.
    */
   old_predicating = cmd_buffer->state.predicating;
   cmd_buffer->state.predicating = false;

   radv_copy_buffer(cmd_buffer, src_buffer->bo, dst_buffer->bo,
                    src_buffer->offset + region->srcOffset, dst_buffer->offset + region->dstOffset,
                    region->size);

   /* Restore conditional rendering. */
   cmd_buffer->state.predicating = old_predicating;
}

VKAPI_ATTR void VKAPI_CALL
radv_CmdCopyBuffer2KHR(VkCommandBuffer commandBuffer, const VkCopyBufferInfo2KHR *pCopyBufferInfo)
{
   RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   RADV_FROM_HANDLE(radv_buffer, src_buffer, pCopyBufferInfo->srcBuffer);
   RADV_FROM_HANDLE(radv_buffer, dst_buffer, pCopyBufferInfo->dstBuffer);

   for (unsigned r = 0; r < pCopyBufferInfo->regionCount; r++) {
      copy_buffer(cmd_buffer, src_buffer, dst_buffer, &pCopyBufferInfo->pRegions[r]);
   }
}

void
radv_update_buffer_cp(struct radv_cmd_buffer *cmd_buffer, uint64_t va, const void *data,
                      uint64_t size)
{
   uint64_t words = size / 4;
   bool mec = radv_cmd_buffer_uses_mec(cmd_buffer);

   assert(size < RADV_BUFFER_UPDATE_THRESHOLD);

   si_emit_cache_flush(cmd_buffer);
   radeon_check_space(cmd_buffer->device->ws, cmd_buffer->cs, words + 4);

   radeon_emit(cmd_buffer->cs, PKT3(PKT3_WRITE_DATA, 2 + words, 0));
   radeon_emit(cmd_buffer->cs, S_370_DST_SEL(mec ? V_370_MEM : V_370_MEM_GRBM) |
                                  S_370_WR_CONFIRM(1) | S_370_ENGINE_SEL(V_370_ME));
   radeon_emit(cmd_buffer->cs, va);
   radeon_emit(cmd_buffer->cs, va >> 32);
   radeon_emit_array(cmd_buffer->cs, data, words);

   if (unlikely(cmd_buffer->device->trace_bo))
      radv_cmd_buffer_trace_emit(cmd_buffer);
}

VKAPI_ATTR void VKAPI_CALL
radv_CmdUpdateBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset,
                     VkDeviceSize dataSize, const void *pData)
{
   RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   RADV_FROM_HANDLE(radv_buffer, dst_buffer, dstBuffer);
   uint64_t va = radv_buffer_get_va(dst_buffer->bo);
   va += dstOffset + dst_buffer->offset;

   assert(!(dataSize & 3));
   assert(!(va & 3));

   if (!dataSize)
      return;

   if (dataSize < RADV_BUFFER_UPDATE_THRESHOLD) {
      radv_cs_add_buffer(cmd_buffer->device->ws, cmd_buffer->cs, dst_buffer->bo);
      radv_update_buffer_cp(cmd_buffer, va, pData, dataSize);
   } else {
      uint32_t buf_offset;
      radv_cmd_buffer_upload_data(cmd_buffer, dataSize, pData, &buf_offset);
      radv_copy_buffer(cmd_buffer, cmd_buffer->upload.upload_bo, dst_buffer->bo, buf_offset,
                       dstOffset + dst_buffer->offset, dataSize);
   }
}
