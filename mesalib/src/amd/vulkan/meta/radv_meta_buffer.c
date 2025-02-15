/* Based on anv:
 * Copyright © 2015 Intel Corporation
 *
 * Copyright © 2016 Red Hat Inc.
 * Copyright © 2025 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "radv_cp_dma.h"
#include "radv_debug.h"
#include "radv_meta.h"
#include "nir/radv_meta_nir.h"
#include "radv_sdma.h"

#include "radv_cs.h"
#include "vk_common_entrypoints.h"

struct fill_constants {
   uint64_t addr;
   uint32_t max_offset;
   uint32_t data;
};

static VkResult
get_fill_pipeline(struct radv_device *device, VkPipeline *pipeline_out, VkPipelineLayout *layout_out)
{
   enum radv_meta_object_key_type key = RADV_META_OBJECT_KEY_FILL_BUFFER;
   VkResult result;

   const VkPushConstantRange pc_range = {
      .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      .size = sizeof(struct fill_constants),
   };

   result = vk_meta_get_pipeline_layout(&device->vk, &device->meta_state.device, NULL, &pc_range, &key, sizeof(key),
                                        layout_out);
   if (result != VK_SUCCESS)
      return result;

   VkPipeline pipeline_from_cache = vk_meta_lookup_pipeline(&device->meta_state.device, &key, sizeof(key));
   if (pipeline_from_cache != VK_NULL_HANDLE) {
      *pipeline_out = pipeline_from_cache;
      return VK_SUCCESS;
   }

   nir_shader *cs = radv_meta_nir_build_buffer_fill_shader(device);

   const VkPipelineShaderStageCreateInfo stage_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_COMPUTE_BIT,
      .module = vk_shader_module_handle_from_nir(cs),
      .pName = "main",
      .pSpecializationInfo = NULL,
   };

   const VkComputePipelineCreateInfo pipeline_info = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage = stage_info,
      .flags = 0,
      .layout = *layout_out,
   };

   result = vk_meta_create_compute_pipeline(&device->vk, &device->meta_state.device, &pipeline_info, &key, sizeof(key),
                                            pipeline_out);

   ralloc_free(cs);
   return result;
}

struct copy_constants {
   uint64_t src_addr;
   uint64_t dst_addr;
   uint32_t max_offset;
};

static VkResult
get_copy_pipeline(struct radv_device *device, VkPipeline *pipeline_out, VkPipelineLayout *layout_out)
{
   enum radv_meta_object_key_type key = RADV_META_OBJECT_KEY_COPY_BUFFER;
   VkResult result;

   const VkPushConstantRange pc_range = {
      .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      .size = sizeof(struct copy_constants),
   };

   result = vk_meta_get_pipeline_layout(&device->vk, &device->meta_state.device, NULL, &pc_range, &key, sizeof(key),
                                        layout_out);
   if (result != VK_SUCCESS)
      return result;

   VkPipeline pipeline_from_cache = vk_meta_lookup_pipeline(&device->meta_state.device, &key, sizeof(key));
   if (pipeline_from_cache != VK_NULL_HANDLE) {
      *pipeline_out = pipeline_from_cache;
      return VK_SUCCESS;
   }

   nir_shader *cs = radv_meta_nir_build_buffer_copy_shader(device);

   const VkPipelineShaderStageCreateInfo stage_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_COMPUTE_BIT,
      .module = vk_shader_module_handle_from_nir(cs),
      .pName = "main",
      .pSpecializationInfo = NULL,
   };

   const VkComputePipelineCreateInfo pipeline_info = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage = stage_info,
      .flags = 0,
      .layout = *layout_out,
   };

   result = vk_meta_create_compute_pipeline(&device->vk, &device->meta_state.device, &pipeline_info, &key, sizeof(key),
                                            pipeline_out);

   ralloc_free(cs);
   return result;
}

static void
radv_compute_fill_memory(struct radv_cmd_buffer *cmd_buffer, uint64_t va, uint64_t size, uint32_t data)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   struct radv_meta_saved_state saved_state;
   VkPipelineLayout layout;
   VkPipeline pipeline;
   VkResult result;

   result = get_fill_pipeline(device, &pipeline, &layout);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd_buffer->vk, result);
      return;
   }

   radv_meta_save(&saved_state, cmd_buffer, RADV_META_SAVE_COMPUTE_PIPELINE | RADV_META_SAVE_CONSTANTS);

   radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer), VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

   assert(size >= 16 && size <= UINT32_MAX);

   struct fill_constants fill_consts = {
      .addr = va,
      .max_offset = size - 16,
      .data = data,
   };

   vk_common_CmdPushConstants(radv_cmd_buffer_to_handle(cmd_buffer), layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                              sizeof(fill_consts), &fill_consts);

   radv_unaligned_dispatch(cmd_buffer, DIV_ROUND_UP(size, 16), 1, 1);

   radv_meta_restore(&saved_state, cmd_buffer);
}

static void
radv_compute_copy_memory(struct radv_cmd_buffer *cmd_buffer, uint64_t src_va, uint64_t dst_va, uint64_t size)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   struct radv_meta_saved_state saved_state;
   VkPipelineLayout layout;
   VkPipeline pipeline;
   VkResult result;

   result = get_copy_pipeline(device, &pipeline, &layout);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd_buffer->vk, result);
      return;
   }

   radv_meta_save(&saved_state, cmd_buffer, RADV_META_SAVE_COMPUTE_PIPELINE | RADV_META_SAVE_CONSTANTS);

   radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer), VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

   assert(size >= 16 && size <= UINT32_MAX);

   struct copy_constants copy_consts = {
      .src_addr = src_va,
      .dst_addr = dst_va,
      .max_offset = size - 16,
   };

   vk_common_CmdPushConstants(radv_cmd_buffer_to_handle(cmd_buffer), layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                              sizeof(copy_consts), &copy_consts);

   radv_unaligned_dispatch(cmd_buffer, DIV_ROUND_UP(size, 16), 1, 1);

   radv_meta_restore(&saved_state, cmd_buffer);
}

static uint32_t
radv_fill_memory(struct radv_cmd_buffer *cmd_buffer, const struct radv_image *image, uint64_t va, uint64_t size,
                 uint32_t value)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   uint32_t flush_bits = 0;

   assert(!(va & 3));
   assert(!(size & 3));

   if (cmd_buffer->qf == RADV_QUEUE_TRANSFER) {
      radv_sdma_fill_memory(device, cmd_buffer->cs, va, size, value);
   } else if (size >= RADV_BUFFER_OPS_CS_THRESHOLD) {
      radv_compute_fill_memory(cmd_buffer, va, size, value);

      flush_bits = RADV_CMD_FLAG_CS_PARTIAL_FLUSH | RADV_CMD_FLAG_INV_VCACHE |
                   radv_src_access_flush(cmd_buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                         VK_ACCESS_2_SHADER_WRITE_BIT, 0, image, NULL);
   } else if (size)
      radv_cp_dma_fill_memory(cmd_buffer, va, size, value);

   return flush_bits;
}

uint32_t
radv_fill_buffer(struct radv_cmd_buffer *cmd_buffer, const struct radv_image *image, struct radeon_winsys_bo *bo,
                 uint64_t va, uint64_t size, uint32_t value)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);

   if (bo)
      radv_cs_add_buffer(device->ws, cmd_buffer->cs, bo);

   return radv_fill_memory(cmd_buffer, image, va, size, value);
}

VKAPI_ATTR void VKAPI_CALL
radv_CmdFillBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize fillSize,
                   uint32_t data)
{
   VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   VK_FROM_HANDLE(radv_buffer, dst_buffer, dstBuffer);
   bool old_predicating;

   /* VK_EXT_conditional_rendering says that copy commands should not be
    * affected by conditional rendering.
    */
   old_predicating = cmd_buffer->state.predicating;
   cmd_buffer->state.predicating = false;

   fillSize = vk_buffer_range(&dst_buffer->vk, dstOffset, fillSize) & ~3ull;

   radv_fill_buffer(cmd_buffer, NULL, dst_buffer->bo, dst_buffer->addr + dstOffset, fillSize, data);

   /* Restore conditional rendering. */
   cmd_buffer->state.predicating = old_predicating;
}

static void
radv_copy_memory(struct radv_cmd_buffer *cmd_buffer, uint64_t src_va, uint64_t dst_va, uint64_t size)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const bool use_compute = !(size & 3) && !(src_va & 3) && !(dst_va & 3) && size >= RADV_BUFFER_OPS_CS_THRESHOLD;

   if (cmd_buffer->qf == RADV_QUEUE_TRANSFER) {
      radv_sdma_copy_memory(device, cmd_buffer->cs, src_va, dst_va, size);
   } else if (use_compute) {
      radv_compute_copy_memory(cmd_buffer, src_va, dst_va, size);
   } else if (size) {
      radv_cp_dma_copy_memory(cmd_buffer, src_va, dst_va, size);
   }
}

void
radv_copy_buffer(struct radv_cmd_buffer *cmd_buffer, struct radeon_winsys_bo *src_bo, struct radeon_winsys_bo *dst_bo,
                 uint64_t src_va, uint64_t dst_va, uint64_t size)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);

   radv_cs_add_buffer(device->ws, cmd_buffer->cs, src_bo);
   radv_cs_add_buffer(device->ws, cmd_buffer->cs, dst_bo);

   radv_copy_memory(cmd_buffer, src_va, dst_va, size);
}

VKAPI_ATTR void VKAPI_CALL
radv_CmdCopyBuffer2(VkCommandBuffer commandBuffer, const VkCopyBufferInfo2 *pCopyBufferInfo)
{
   VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   VK_FROM_HANDLE(radv_buffer, src_buffer, pCopyBufferInfo->srcBuffer);
   VK_FROM_HANDLE(radv_buffer, dst_buffer, pCopyBufferInfo->dstBuffer);
   bool old_predicating;

   /* VK_EXT_conditional_rendering says that copy commands should not be
    * affected by conditional rendering.
    */
   old_predicating = cmd_buffer->state.predicating;
   cmd_buffer->state.predicating = false;

   for (unsigned r = 0; r < pCopyBufferInfo->regionCount; r++) {
      const VkBufferCopy2 *region = &pCopyBufferInfo->pRegions[r];
      const uint64_t src_va = src_buffer->addr + region->srcOffset;
      const uint64_t dst_va = dst_buffer->addr + region->dstOffset;

      radv_copy_buffer(cmd_buffer, src_buffer->bo, dst_buffer->bo, src_va, dst_va, region->size);
   }

   /* Restore conditional rendering. */
   cmd_buffer->state.predicating = old_predicating;
}

void
radv_update_buffer_cp(struct radv_cmd_buffer *cmd_buffer, uint64_t va, const void *data, uint64_t size)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   uint64_t words = size / 4;
   bool mec = radv_cmd_buffer_uses_mec(cmd_buffer);

   assert(size < RADV_BUFFER_UPDATE_THRESHOLD);

   radv_emit_cache_flush(cmd_buffer);
   radeon_check_space(device->ws, cmd_buffer->cs, words + 4);

   radeon_emit(cmd_buffer->cs, PKT3(PKT3_WRITE_DATA, 2 + words, 0));
   radeon_emit(cmd_buffer->cs,
               S_370_DST_SEL(mec ? V_370_MEM : V_370_MEM_GRBM) | S_370_WR_CONFIRM(1) | S_370_ENGINE_SEL(V_370_ME));
   radeon_emit(cmd_buffer->cs, va);
   radeon_emit(cmd_buffer->cs, va >> 32);
   radeon_emit_array(cmd_buffer->cs, data, words);

   if (radv_device_fault_detection_enabled(device))
      radv_cmd_buffer_trace_emit(cmd_buffer);
}

static void
radv_update_memory(struct radv_cmd_buffer *cmd_buffer, uint64_t va, uint64_t size, const void *data)
{
   assert(!(size & 3));
   assert(!(va & 3));

   if (!size)
      return;

   if (size < RADV_BUFFER_UPDATE_THRESHOLD && cmd_buffer->qf != RADV_QUEUE_TRANSFER) {
      radv_update_buffer_cp(cmd_buffer, va, data, size);
   } else {
      uint32_t buf_offset;

      radv_cmd_buffer_upload_data(cmd_buffer, size, data, &buf_offset);

      const uint64_t src_va = radv_buffer_get_va(cmd_buffer->upload.upload_bo) + buf_offset;

      radv_copy_memory(cmd_buffer, src_va, va, size);
   }
}

VKAPI_ATTR void VKAPI_CALL
radv_CmdUpdateBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, VkDeviceSize dstOffset, VkDeviceSize dataSize,
                     const void *pData)
{
   VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   VK_FROM_HANDLE(radv_buffer, dst_buffer, dstBuffer);
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const uint64_t dst_va = dst_buffer->addr + dstOffset;
   bool old_predicating;

   /* VK_EXT_conditional_rendering says that copy commands should not be
    * affected by conditional rendering.
    */
   old_predicating = cmd_buffer->state.predicating;
   cmd_buffer->state.predicating = false;

   radv_cs_add_buffer(device->ws, cmd_buffer->cs, dst_buffer->bo);

   radv_update_memory(cmd_buffer, dst_va, dataSize, pData);

   /* Restore conditional rendering. */
   cmd_buffer->state.predicating = old_predicating;
}
