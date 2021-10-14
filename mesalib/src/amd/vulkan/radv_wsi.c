/*
 * Copyright © 2016 Red Hat
 * based on intel anv code:
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

#include "util/macros.h"
#include "radv_meta.h"
#include "radv_private.h"
#include "vk_util.h"
#include "wsi_common.h"

static PFN_vkVoidFunction
radv_wsi_proc_addr(VkPhysicalDevice physicalDevice, const char *pName)
{
   RADV_FROM_HANDLE(radv_physical_device, pdevice, physicalDevice);
   return vk_instance_get_proc_addr_unchecked(&pdevice->instance->vk, pName);
}

static void
radv_wsi_set_memory_ownership(VkDevice _device, VkDeviceMemory _mem, VkBool32 ownership)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_device_memory, mem, _mem);

   if (device->use_global_bo_list) {
      device->ws->buffer_make_resident(device->ws, mem->bo, ownership);
   }
}

VkResult
radv_init_wsi(struct radv_physical_device *physical_device)
{
   VkResult result =
      wsi_device_init(&physical_device->wsi_device, radv_physical_device_to_handle(physical_device),
                      radv_wsi_proc_addr, &physical_device->instance->vk.alloc,
                      physical_device->master_fd, &physical_device->instance->dri_options, false);
   if (result != VK_SUCCESS)
      return result;

   physical_device->wsi_device.supports_modifiers = physical_device->rad_info.chip_class >= GFX9;
   physical_device->wsi_device.set_memory_ownership = radv_wsi_set_memory_ownership;

   physical_device->vk.wsi_device = &physical_device->wsi_device;

   return VK_SUCCESS;
}

void
radv_finish_wsi(struct radv_physical_device *physical_device)
{
   physical_device->vk.wsi_device = NULL;
   wsi_device_finish(&physical_device->wsi_device, &physical_device->instance->vk.alloc);
}

VkResult
radv_AcquireNextImage2KHR(VkDevice _device, const VkAcquireNextImageInfoKHR *pAcquireInfo,
                          uint32_t *pImageIndex)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   struct radv_physical_device *pdevice = device->physical_device;
   RADV_FROM_HANDLE(radv_fence, fence, pAcquireInfo->fence);
   RADV_FROM_HANDLE(radv_semaphore, semaphore, pAcquireInfo->semaphore);

   VkResult result =
      wsi_common_acquire_next_image2(&pdevice->wsi_device, _device, pAcquireInfo, pImageIndex);

   if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR) {
      if (fence) {
         struct radv_fence_part *part =
            fence->temporary.kind != RADV_FENCE_NONE ? &fence->temporary : &fence->permanent;

         device->ws->signal_syncobj(device->ws, part->syncobj, 0);
      }
      if (semaphore) {
         struct radv_semaphore_part *part = semaphore->temporary.kind != RADV_SEMAPHORE_NONE
                                               ? &semaphore->temporary
                                               : &semaphore->permanent;

         switch (part->kind) {
         case RADV_SEMAPHORE_NONE:
            /* Do not need to do anything. */
            break;
         case RADV_SEMAPHORE_TIMELINE:
         case RADV_SEMAPHORE_TIMELINE_SYNCOBJ:
            unreachable("WSI only allows binary semaphores.");
         case RADV_SEMAPHORE_SYNCOBJ:
            device->ws->signal_syncobj(device->ws, part->syncobj, 0);
            break;
         }
      }
   }
   return result;
}

VkResult
radv_QueuePresentKHR(VkQueue _queue, const VkPresentInfoKHR *pPresentInfo)
{
   RADV_FROM_HANDLE(radv_queue, queue, _queue);
   return wsi_common_queue_present(&queue->device->physical_device->wsi_device,
                                   radv_device_to_handle(queue->device), _queue,
                                   queue->vk.queue_family_index, pPresentInfo);
}
