/*
 * Copyright © Microsoft Corporation
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "dzn_private.h"
#include "vk_util.h"

static PFN_vkVoidFunction VKAPI_PTR
dzn_wsi_proc_addr(VkPhysicalDevice physicalDevice, const char *pName)
{
   VK_FROM_HANDLE(dzn_physical_device, pdevice, physicalDevice);
   return vk_instance_get_proc_addr_unchecked(pdevice->vk.instance, pName);
}

void
dzn_wsi_finish(struct dzn_physical_device *physical_device)
{
   wsi_device_finish(&physical_device->wsi_device,
                     &physical_device->vk.instance->alloc);
}

VkResult
dzn_wsi_init(struct dzn_physical_device *physical_device)
{
   VkResult result;

   /* TODO: implement a proper, non-sw winsys for D3D12 */
   bool sw_device = true;

   result = wsi_device_init(&physical_device->wsi_device,
                            dzn_physical_device_to_handle(physical_device),
                            dzn_wsi_proc_addr,
                            &physical_device->vk.instance->alloc,
                            -1, NULL, sw_device);

   if (result != VK_SUCCESS)
      return result;

   physical_device->wsi_device.supports_modifiers = false;
   physical_device->vk.wsi_device = &physical_device->wsi_device;

   return VK_SUCCESS;
}
