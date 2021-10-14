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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "tu_private.h"

#include "vk_util.h"
#include "wsi_common.h"
#include "drm-uapi/drm_fourcc.h"

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
tu_wsi_proc_addr(VkPhysicalDevice physicalDevice, const char *pName)
{
   TU_FROM_HANDLE(tu_physical_device, pdevice, physicalDevice);
   return vk_instance_get_proc_addr_unchecked(&pdevice->instance->vk, pName);
}

VkResult
tu_wsi_init(struct tu_physical_device *physical_device)
{
   VkResult result;

   result = wsi_device_init(&physical_device->wsi_device,
                            tu_physical_device_to_handle(physical_device),
                            tu_wsi_proc_addr,
                            &physical_device->instance->vk.alloc,
                            physical_device->master_fd, NULL,
                            false);
   if (result != VK_SUCCESS)
      return result;

   physical_device->wsi_device.supports_modifiers = true;
   physical_device->vk.wsi_device = &physical_device->wsi_device;

   return VK_SUCCESS;
}

void
tu_wsi_finish(struct tu_physical_device *physical_device)
{
   physical_device->vk.wsi_device = NULL;
   wsi_device_finish(&physical_device->wsi_device,
                     &physical_device->instance->vk.alloc);
}

VKAPI_ATTR VkResult VKAPI_CALL
tu_AcquireNextImage2KHR(VkDevice _device,
                        const VkAcquireNextImageInfoKHR *pAcquireInfo,
                        uint32_t *pImageIndex)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   TU_FROM_HANDLE(tu_syncobj, fence, pAcquireInfo->fence);
   TU_FROM_HANDLE(tu_syncobj, semaphore, pAcquireInfo->semaphore);

   struct tu_physical_device *pdevice = device->physical_device;

   VkResult result = wsi_common_acquire_next_image2(
      &pdevice->wsi_device, _device, pAcquireInfo, pImageIndex);

   /* signal fence/semaphore - image is available immediately */
   tu_signal_fences(device, fence, semaphore);

   return result;
}

VKAPI_ATTR VkResult VKAPI_CALL
tu_QueuePresentKHR(VkQueue _queue, const VkPresentInfoKHR *pPresentInfo)
{
   TU_FROM_HANDLE(tu_queue, queue, _queue);

   u_trace_context_process(&queue->device->trace_context, true);

   return wsi_common_queue_present(
      &queue->device->physical_device->wsi_device,
      tu_device_to_handle(queue->device), _queue, queue->vk.queue_family_index,
      pPresentInfo);
}
