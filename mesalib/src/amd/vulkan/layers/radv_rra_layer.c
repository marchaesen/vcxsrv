/*
 * Copyright © 2022 Friedrich Vock
 *
 * SPDX-License-Identifier: MIT
 */

#include "meta/radv_meta.h"
#include "util/u_process.h"
#include "radv_event.h"
#include "radv_rra.h"
#include "vk_acceleration_structure.h"
#include "vk_common_entrypoints.h"

VKAPI_ATTR VkResult VKAPI_CALL
rra_QueuePresentKHR(VkQueue _queue, const VkPresentInfoKHR *pPresentInfo)
{
   VK_FROM_HANDLE(radv_queue, queue, _queue);
   struct radv_device *device = radv_queue_device(queue);

   if (device->rra_trace.triggered) {
      device->rra_trace.triggered = false;

      if (_mesa_hash_table_num_entries(device->rra_trace.accel_structs) == 0) {
         fprintf(stderr, "radv: No acceleration structures captured, not saving RRA trace.\n");
      } else {
         char filename[2048];
         time_t t = time(NULL);
         struct tm now = *localtime(&t);
         snprintf(filename, sizeof(filename), "/tmp/%s_%04d.%02d.%02d_%02d.%02d.%02d.rra", util_get_process_name(),
                  1900 + now.tm_year, now.tm_mon + 1, now.tm_mday, now.tm_hour, now.tm_min, now.tm_sec);

         VkResult result = radv_rra_dump_trace(_queue, filename);
         if (result == VK_SUCCESS)
            fprintf(stderr, "radv: RRA capture saved to '%s'\n", filename);
         else
            fprintf(stderr, "radv: Failed to save RRA capture!\n");
      }
   }

   VkResult result = device->layer_dispatch.rra.QueuePresentKHR(_queue, pPresentInfo);
   if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
      return result;

   VkDevice _device = radv_device_to_handle(device);
   radv_rra_trace_clear_ray_history(_device, &device->rra_trace);

   if (device->rra_trace.triggered && device->rra_trace.ray_history_buffer) {
      result = device->layer_dispatch.rra.DeviceWaitIdle(_device);
      if (result != VK_SUCCESS)
         return result;

      struct radv_ray_history_header *header = device->rra_trace.ray_history_data;
      header->offset = sizeof(struct radv_ray_history_header);
   }

   if (!device->rra_trace.copy_after_build)
      return VK_SUCCESS;

   struct hash_table *accel_structs = device->rra_trace.accel_structs;

   hash_table_foreach (accel_structs, entry) {
      struct radv_rra_accel_struct_data *data = entry->data;
      if (!data->is_dead)
         continue;

      radv_destroy_rra_accel_struct_data(_device, data);
      _mesa_hash_table_remove(accel_structs, entry);
   }

   return VK_SUCCESS;
}

static VkResult
rra_init_accel_struct_data_buffer(VkDevice vk_device, struct radv_rra_accel_struct_buffer *buffer, uint32_t size)
{
   VK_FROM_HANDLE(radv_device, device, vk_device);

   buffer->ref_cnt = 1;

   VkBufferCreateInfo buffer_create_info = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = size,
   };

   VkResult result = radv_create_buffer(device, &buffer_create_info, NULL, &buffer->buffer, true);
   if (result != VK_SUCCESS)
      return result;

   VkMemoryRequirements requirements;
   vk_common_GetBufferMemoryRequirements(vk_device, buffer->buffer, &requirements);

   VkMemoryAllocateFlagsInfo flags_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
      .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT,
   };

   VkMemoryAllocateInfo alloc_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .pNext = &flags_info,
      .allocationSize = requirements.size,
      .memoryTypeIndex = device->rra_trace.copy_memory_index,
   };
   result = radv_alloc_memory(device, &alloc_info, NULL, &buffer->memory, true);
   if (result != VK_SUCCESS)
      goto fail_buffer;

   result = vk_common_BindBufferMemory(vk_device, buffer->buffer, buffer->memory, 0);
   if (result != VK_SUCCESS)
      goto fail_memory;

   return result;
fail_memory:
   radv_FreeMemory(vk_device, buffer->memory, NULL);
   buffer->memory = VK_NULL_HANDLE;
fail_buffer:
   radv_DestroyBuffer(vk_device, buffer->buffer, NULL);
   buffer->buffer = VK_NULL_HANDLE;
   return result;
}

VKAPI_ATTR VkResult VKAPI_CALL
rra_CreateAccelerationStructureKHR(VkDevice _device, const VkAccelerationStructureCreateInfoKHR *pCreateInfo,
                                   const VkAllocationCallbacks *pAllocator,
                                   VkAccelerationStructureKHR *pAccelerationStructure)
{
   VK_FROM_HANDLE(radv_device, device, _device);
   VK_FROM_HANDLE(radv_buffer, buffer, pCreateInfo->buffer);

   VkResult result = device->layer_dispatch.rra.CreateAccelerationStructureKHR(_device, pCreateInfo, pAllocator,
                                                                               pAccelerationStructure);

   if (result != VK_SUCCESS)
      return result;

   VK_FROM_HANDLE(vk_acceleration_structure, structure, *pAccelerationStructure);
   simple_mtx_lock(&device->rra_trace.data_mtx);

   struct radv_rra_accel_struct_data *data = calloc(1, sizeof(struct radv_rra_accel_struct_data));
   if (!data) {
      result = VK_ERROR_OUT_OF_HOST_MEMORY;
      goto fail_as;
   }

   data->va = buffer->bo ? vk_acceleration_structure_get_va(structure) : 0;
   data->type = pCreateInfo->type;
   data->is_dead = false;

   VkEventCreateInfo eventCreateInfo = {
      .sType = VK_STRUCTURE_TYPE_EVENT_CREATE_INFO,
   };

   result = radv_create_event(device, &eventCreateInfo, NULL, &data->build_event, true);
   if (result != VK_SUCCESS)
      goto fail_data;

   _mesa_hash_table_insert(device->rra_trace.accel_structs, structure, data);

   if (data->va)
      _mesa_hash_table_u64_insert(device->rra_trace.accel_struct_vas, data->va, structure);

   goto exit;
fail_data:
   free(data);
fail_as:
   device->layer_dispatch.rra.DestroyAccelerationStructureKHR(_device, *pAccelerationStructure, pAllocator);
   *pAccelerationStructure = VK_NULL_HANDLE;
exit:
   simple_mtx_unlock(&device->rra_trace.data_mtx);
   return result;
}

static void
handle_accel_struct_write(VkCommandBuffer commandBuffer, VkAccelerationStructureKHR accelerationStructure,
                          uint64_t size)
{
   VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   VK_FROM_HANDLE(vk_acceleration_structure, accel_struct, accelerationStructure);

   size = MIN2(size, accel_struct->size);

   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   VkDevice _device = radv_device_to_handle(device);

   struct hash_entry *entry = _mesa_hash_table_search(device->rra_trace.accel_structs, accel_struct);
   struct radv_rra_accel_struct_data *data = entry->data;

   VkMemoryBarrier2 barrier = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
      .srcStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
      .srcAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
      .dstStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
      .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
   };

   VkDependencyInfo dependencyInfo = {
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .memoryBarrierCount = 1,
      .pMemoryBarriers = &barrier,
   };

   radv_CmdPipelineBarrier2(commandBuffer, &dependencyInfo);

   vk_common_CmdSetEvent(commandBuffer, data->build_event, 0);

   if (!data->va) {
      data->va = vk_acceleration_structure_get_va(accel_struct);
      _mesa_hash_table_u64_insert(device->rra_trace.accel_struct_vas, data->va, accel_struct);
   }

   if (data->size < size) {
      data->size = size;

      if (device->rra_trace.copy_after_build) {
         if (data->buffer)
            radv_rra_accel_struct_buffer_unref(device, data->buffer);

         data->buffer = calloc(1, sizeof(struct radv_rra_accel_struct_buffer));
         if (rra_init_accel_struct_data_buffer(_device, data->buffer, size) != VK_SUCCESS)
            return;
      }
   }

   if (!data->buffer)
      return;

   if (!_mesa_set_search(cmd_buffer->accel_struct_buffers, data->buffer)) {
      radv_radv_rra_accel_struct_buffer_ref(data->buffer);
      _mesa_set_add(cmd_buffer->accel_struct_buffers, data->buffer);
   }

   VkBufferCopy2 region = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2,
      .srcOffset = accel_struct->offset,
      .size = size,
   };

   VkCopyBufferInfo2 copyInfo = {
      .sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2,
      .srcBuffer = accel_struct->buffer,
      .dstBuffer = data->buffer->buffer,
      .regionCount = 1,
      .pRegions = &region,
   };

   radv_CmdCopyBuffer2(commandBuffer, &copyInfo);
}

VKAPI_ATTR void VKAPI_CALL
rra_CmdBuildAccelerationStructuresKHR(VkCommandBuffer commandBuffer, uint32_t infoCount,
                                      const VkAccelerationStructureBuildGeometryInfoKHR *pInfos,
                                      const VkAccelerationStructureBuildRangeInfoKHR *const *ppBuildRangeInfos)
{
   VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);

   device->layer_dispatch.rra.CmdBuildAccelerationStructuresKHR(commandBuffer, infoCount, pInfos, ppBuildRangeInfos);

   simple_mtx_lock(&device->rra_trace.data_mtx);

   for (uint32_t i = 0; i < infoCount; ++i) {
      uint32_t *primitive_counts = alloca(pInfos[i].geometryCount * sizeof(uint32_t));
      for (uint32_t geometry_index = 0; geometry_index < pInfos[i].geometryCount; geometry_index++)
         primitive_counts[geometry_index] = ppBuildRangeInfos[i][geometry_index].primitiveCount;

      /* vkd3d-proton specifies the size of the backing buffer. This can cause false positives when removing aliasing
       * acceleration structures, because a buffer can be used by multiple acceleration structures. Therefore we need to
       * compute the actual size. */
      VkAccelerationStructureBuildSizesInfoKHR size_info;
      device->layer_dispatch.rra.GetAccelerationStructureBuildSizesKHR(radv_device_to_handle(device),
                                                                       VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                                                       pInfos + i, primitive_counts, &size_info);

      handle_accel_struct_write(commandBuffer, pInfos[i].dstAccelerationStructure, size_info.accelerationStructureSize);
   }

   simple_mtx_unlock(&device->rra_trace.data_mtx);
}

VKAPI_ATTR void VKAPI_CALL
rra_CmdCopyAccelerationStructureKHR(VkCommandBuffer commandBuffer, const VkCopyAccelerationStructureInfoKHR *pInfo)
{
   VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);

   device->layer_dispatch.rra.CmdCopyAccelerationStructureKHR(commandBuffer, pInfo);

   simple_mtx_lock(&device->rra_trace.data_mtx);

   VK_FROM_HANDLE(vk_acceleration_structure, src, pInfo->src);

   struct hash_entry *entry = _mesa_hash_table_search(device->rra_trace.accel_structs, src);
   struct radv_rra_accel_struct_data *data = entry->data;

   handle_accel_struct_write(commandBuffer, pInfo->dst, data->size);

   simple_mtx_unlock(&device->rra_trace.data_mtx);
}

VKAPI_ATTR void VKAPI_CALL
rra_CmdCopyMemoryToAccelerationStructureKHR(VkCommandBuffer commandBuffer,
                                            const VkCopyMemoryToAccelerationStructureInfoKHR *pInfo)
{
   VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);

   device->layer_dispatch.rra.CmdCopyMemoryToAccelerationStructureKHR(commandBuffer, pInfo);

   simple_mtx_lock(&device->rra_trace.data_mtx);

   VK_FROM_HANDLE(vk_acceleration_structure, dst, pInfo->dst);
   handle_accel_struct_write(commandBuffer, pInfo->dst, dst->size);

   simple_mtx_unlock(&device->rra_trace.data_mtx);
}

VKAPI_ATTR void VKAPI_CALL
rra_DestroyAccelerationStructureKHR(VkDevice _device, VkAccelerationStructureKHR _structure,
                                    const VkAllocationCallbacks *pAllocator)
{
   if (!_structure)
      return;

   VK_FROM_HANDLE(radv_device, device, _device);
   simple_mtx_lock(&device->rra_trace.data_mtx);

   VK_FROM_HANDLE(vk_acceleration_structure, structure, _structure);

   struct hash_entry *entry = _mesa_hash_table_search(device->rra_trace.accel_structs, structure);

   assert(entry);
   struct radv_rra_accel_struct_data *data = entry->data;

   if (device->rra_trace.copy_after_build)
      data->is_dead = true;
   else
      _mesa_hash_table_remove(device->rra_trace.accel_structs, entry);

   simple_mtx_unlock(&device->rra_trace.data_mtx);

   device->layer_dispatch.rra.DestroyAccelerationStructureKHR(_device, _structure, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL
rra_QueueSubmit2KHR(VkQueue _queue, uint32_t submitCount, const VkSubmitInfo2 *pSubmits, VkFence _fence)
{
   VK_FROM_HANDLE(radv_queue, queue, _queue);
   struct radv_device *device = radv_queue_device(queue);

   VkResult result = device->layer_dispatch.rra.QueueSubmit2KHR(_queue, submitCount, pSubmits, _fence);
   if (result != VK_SUCCESS || !device->rra_trace.triggered)
      return result;

   uint32_t total_trace_count = 0;

   simple_mtx_lock(&device->rra_trace.data_mtx);

   for (uint32_t submit_index = 0; submit_index < submitCount; submit_index++) {
      for (uint32_t i = 0; i < pSubmits[submit_index].commandBufferInfoCount; i++) {
         VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, pSubmits[submit_index].pCommandBufferInfos[i].commandBuffer);
         uint32_t trace_count =
            util_dynarray_num_elements(&cmd_buffer->ray_history, struct radv_rra_ray_history_data *);
         if (!trace_count)
            continue;

         total_trace_count += trace_count;
         util_dynarray_append_dynarray(&device->rra_trace.ray_history, &cmd_buffer->ray_history);
      }
   }

   if (!total_trace_count) {
      simple_mtx_unlock(&device->rra_trace.data_mtx);
      return result;
   }

   result = device->layer_dispatch.rra.DeviceWaitIdle(radv_device_to_handle(device));

   struct radv_ray_history_header *header = device->rra_trace.ray_history_data;
   header->submit_base_index += total_trace_count;

   simple_mtx_unlock(&device->rra_trace.data_mtx);

   return result;
}
