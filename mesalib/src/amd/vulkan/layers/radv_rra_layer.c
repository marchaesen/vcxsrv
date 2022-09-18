/*
 * Copyright © 2022 Friedrich Vock
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

#include "util/u_process.h"
#include "radv_acceleration_structure.h"
#include "radv_meta.h"
#include "radv_private.h"
#include "vk_common_entrypoints.h"
#include "wsi_common_entrypoints.h"

static void
radv_rra_handle_trace(VkQueue _queue)
{
   RADV_FROM_HANDLE(radv_queue, queue, _queue);

   simple_mtx_lock(&queue->device->rra_trace.data_mtx);
   /*
    * TODO: This code is shared with RGP tracing and could be merged in a common helper.
    */
   bool frame_trigger =
      queue->device->rra_trace.elapsed_frames == queue->device->rra_trace.trace_frame;
   if (queue->device->rra_trace.elapsed_frames <= queue->device->rra_trace.trace_frame)
      ++queue->device->rra_trace.elapsed_frames;

   bool file_trigger = false;
#ifndef _WIN32
   if (queue->device->rra_trace.trigger_file &&
       access(queue->device->rra_trace.trigger_file, W_OK) == 0) {
      if (unlink(queue->device->rra_trace.trigger_file) == 0) {
         file_trigger = true;
      } else {
         /* Do not enable tracing if we cannot remove the file,
          * because by then we'll trace every frame ... */
         fprintf(stderr, "radv: could not remove RRA trace trigger file, ignoring\n");
      }
   }
#endif

   if (!frame_trigger && !file_trigger) {
      simple_mtx_unlock(&queue->device->rra_trace.data_mtx);
      return;
   }

   if (_mesa_hash_table_num_entries(queue->device->rra_trace.accel_structs) == 0) {
      fprintf(stderr, "radv: No acceleration structures captured, not saving RRA trace.\n");
      simple_mtx_unlock(&queue->device->rra_trace.data_mtx);
      return;
   }

   char filename[2048];
   struct tm now;
   time_t t;

   t = time(NULL);
   now = *localtime(&t);

   snprintf(filename, sizeof(filename), "/tmp/%s_%04d.%02d.%02d_%02d.%02d.%02d.rra",
            util_get_process_name(), 1900 + now.tm_year, now.tm_mon + 1, now.tm_mday, now.tm_hour,
            now.tm_min, now.tm_sec);

   VkResult result = radv_rra_dump_trace(_queue, filename);

   if (result == VK_SUCCESS)
      fprintf(stderr, "radv: RRA capture saved to '%s'\n", filename);
   else
      fprintf(stderr, "radv: Failed to save RRA capture!\n");

   simple_mtx_unlock(&queue->device->rra_trace.data_mtx);
}

VKAPI_ATTR VkResult VKAPI_CALL
rra_QueuePresentKHR(VkQueue _queue, const VkPresentInfoKHR *pPresentInfo)
{
   VkResult result = wsi_QueuePresentKHR(_queue, pPresentInfo);
   if (result != VK_SUCCESS)
      return result;

   radv_rra_handle_trace(_queue);

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
rra_CreateAccelerationStructureKHR(VkDevice _device,
                                   const VkAccelerationStructureCreateInfoKHR *pCreateInfo,
                                   const VkAllocationCallbacks *pAllocator,
                                   VkAccelerationStructureKHR *pAccelerationStructure)
{
   VkResult result =
      radv_CreateAccelerationStructureKHR(_device, pCreateInfo, pAllocator, pAccelerationStructure);

   if (result != VK_SUCCESS)
      return result;

   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_acceleration_structure, structure, *pAccelerationStructure);
   simple_mtx_lock(&device->rra_trace.data_mtx);

   if (_mesa_hash_table_u64_search(device->rra_trace.accel_struct_vas, structure->va) != NULL) {
      fprintf(stderr, "radv: Memory aliasing between acceleration structures detected. RRA "
                      "captures might not work correctly.\n");
      goto end;
   }

   VkEvent _build_submit_event;
   radv_CreateEvent(radv_device_to_handle(device),
                    &(const VkEventCreateInfo){
                       .sType = VK_STRUCTURE_TYPE_EVENT_CREATE_INFO,
                    },
                    NULL, &_build_submit_event);

   RADV_FROM_HANDLE(radv_event, build_submit_event, _build_submit_event);

   _mesa_hash_table_insert(device->rra_trace.accel_structs, structure, build_submit_event);
   _mesa_hash_table_u64_insert(device->rra_trace.accel_struct_vas, structure->va, structure);

end:
   simple_mtx_unlock(&device->rra_trace.data_mtx);
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
rra_CmdBuildAccelerationStructuresKHR(
   VkCommandBuffer commandBuffer, uint32_t infoCount,
   const VkAccelerationStructureBuildGeometryInfoKHR *pInfos,
   const VkAccelerationStructureBuildRangeInfoKHR *const *ppBuildRangeInfos)
{
   RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);

   radv_CmdBuildAccelerationStructuresKHR(commandBuffer, infoCount, pInfos, ppBuildRangeInfos);
   simple_mtx_lock(&cmd_buffer->device->rra_trace.data_mtx);
   for (uint32_t i = 0; i < infoCount; ++i) {
      RADV_FROM_HANDLE(radv_acceleration_structure, structure, pInfos[i].dstAccelerationStructure);
      struct hash_entry *entry = _mesa_hash_table_search(
         cmd_buffer->device->rra_trace.accel_structs, structure);

      assert(entry);

      vk_common_CmdSetEvent(commandBuffer, radv_event_to_handle(entry->data), 0);
   }
   simple_mtx_unlock(&cmd_buffer->device->rra_trace.data_mtx);
}

VKAPI_ATTR void VKAPI_CALL
rra_CmdCopyAccelerationStructureKHR(VkCommandBuffer commandBuffer,
                                    const VkCopyAccelerationStructureInfoKHR *pInfo)
{
   radv_CmdCopyAccelerationStructureKHR(commandBuffer, pInfo);
   RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);

   simple_mtx_lock(&cmd_buffer->device->rra_trace.data_mtx);

   RADV_FROM_HANDLE(radv_acceleration_structure, structure, pInfo->dst);
   struct hash_entry *entry =
      _mesa_hash_table_search(cmd_buffer->device->rra_trace.accel_structs, structure);

   assert(entry);

   vk_common_CmdSetEvent(commandBuffer, radv_event_to_handle(entry->data), 0);
   simple_mtx_unlock(&cmd_buffer->device->rra_trace.data_mtx);
}

VKAPI_ATTR void VKAPI_CALL
rra_CmdCopyMemoryToAccelerationStructureKHR(VkCommandBuffer commandBuffer,
                                            const VkCopyMemoryToAccelerationStructureInfoKHR *pInfo)
{
   radv_CmdCopyMemoryToAccelerationStructureKHR(commandBuffer, pInfo);
   RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);

   simple_mtx_lock(&cmd_buffer->device->rra_trace.data_mtx);

   RADV_FROM_HANDLE(radv_acceleration_structure, structure, pInfo->dst);
   struct hash_entry *entry =
      _mesa_hash_table_search(cmd_buffer->device->rra_trace.accel_structs, structure);

   assert(entry);

   vk_common_CmdSetEvent(commandBuffer, radv_event_to_handle(entry->data), 0);
   simple_mtx_unlock(&cmd_buffer->device->rra_trace.data_mtx);
}

VKAPI_ATTR void VKAPI_CALL
rra_DestroyAccelerationStructureKHR(VkDevice _device, VkAccelerationStructureKHR _structure,
                                    const VkAllocationCallbacks *pAllocator)
{
   if (!_structure)
      return;

   RADV_FROM_HANDLE(radv_device, device, _device);
   simple_mtx_lock(&device->rra_trace.data_mtx);

   RADV_FROM_HANDLE(radv_acceleration_structure, structure, _structure);

   struct hash_entry *entry =
      _mesa_hash_table_search(device->rra_trace.accel_structs, structure);

   assert(entry);
   
   radv_DestroyEvent(_device, radv_event_to_handle(entry->data), NULL);
   _mesa_hash_table_remove(device->rra_trace.accel_structs, entry);
   _mesa_hash_table_u64_remove(device->rra_trace.accel_struct_vas, structure->va);
   simple_mtx_unlock(&device->rra_trace.data_mtx);

   radv_DestroyAccelerationStructureKHR(_device, _structure, pAllocator);
}