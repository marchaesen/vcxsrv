/*
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

#include "lvp_wsi.h"

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
lvp_wsi_proc_addr(VkPhysicalDevice physicalDevice, const char *pName)
{
   LVP_FROM_HANDLE(lvp_physical_device, pdevice, physicalDevice);
   PFN_vkVoidFunction func;

   func = vk_instance_dispatch_table_get(&pdevice->vk.instance->dispatch_table, pName);
   if (func != NULL)
      return func;

   func = vk_physical_device_dispatch_table_get(&pdevice->vk.dispatch_table, pName);
   if (func != NULL)
      return func;

   return vk_device_dispatch_table_get(&vk_device_trampolines, pName);
}

VkResult
lvp_init_wsi(struct lvp_physical_device *physical_device)
{
   VkResult result;

   result = wsi_device_init(&physical_device->wsi_device,
                            lvp_physical_device_to_handle(physical_device),
                            lvp_wsi_proc_addr,
                            &physical_device->vk.instance->alloc,
                            -1, NULL, true);
   if (result != VK_SUCCESS)
      return result;

   physical_device->vk.wsi_device = &physical_device->wsi_device;

   return VK_SUCCESS;
}

void
lvp_finish_wsi(struct lvp_physical_device *physical_device)
{
   physical_device->vk.wsi_device = NULL;
   wsi_device_finish(&physical_device->wsi_device,
                     &physical_device->vk.instance->alloc);
}

VKAPI_ATTR VkResult VKAPI_CALL lvp_AcquireNextImage2KHR(
   VkDevice                                     _device,
   const VkAcquireNextImageInfoKHR*             pAcquireInfo,
   uint32_t*                                    pImageIndex)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   struct lvp_physical_device *pdevice = device->physical_device;

   VkResult result = wsi_common_acquire_next_image2(&pdevice->wsi_device,
                                                    _device,
                                                    pAcquireInfo,
                                                    pImageIndex);

   LVP_FROM_HANDLE(lvp_fence, fence, pAcquireInfo->fence);

   if (fence && (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR)) {
      fence->timeline = p_atomic_inc_return(&device->queue.timeline);
      util_queue_add_job(&device->queue.queue, fence, &fence->fence, queue_thread_noop, NULL, 0);
   }
   return result;
}
