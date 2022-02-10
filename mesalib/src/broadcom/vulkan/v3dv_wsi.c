/*
 * Copyright © 2020 Raspberry Pi
 * based on intel anv code:
 * Copyright © 2015 Intel Corporation

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

#include "v3dv_private.h"
#include "drm-uapi/drm_fourcc.h"
#include "wsi_common_entrypoints.h"
#include "vk_util.h"
#include "wsi_common.h"
#include "wsi_common_drm.h"

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
v3dv_wsi_proc_addr(VkPhysicalDevice physicalDevice, const char *pName)
{
   V3DV_FROM_HANDLE(v3dv_physical_device, pdevice, physicalDevice);
   return vk_instance_get_proc_addr_unchecked(pdevice->vk.instance, pName);
}

static bool
v3dv_wsi_can_present_on_device(VkPhysicalDevice _pdevice, int fd)
{
   V3DV_FROM_HANDLE(v3dv_physical_device, pdevice, _pdevice);

   return wsi_common_drm_devices_equal(fd, pdevice->display_fd);
}

VkResult
v3dv_wsi_init(struct v3dv_physical_device *physical_device)
{
   VkResult result;

   result = wsi_device_init(&physical_device->wsi_device,
                            v3dv_physical_device_to_handle(physical_device),
                            v3dv_wsi_proc_addr,
                            &physical_device->vk.instance->alloc,
                            physical_device->master_fd, NULL, false);

   if (result != VK_SUCCESS)
      return result;

   physical_device->wsi_device.supports_modifiers = true;
   physical_device->wsi_device.can_present_on_device =
      v3dv_wsi_can_present_on_device;

   physical_device->vk.wsi_device = &physical_device->wsi_device;

   return VK_SUCCESS;
}

void
v3dv_wsi_finish(struct v3dv_physical_device *physical_device)
{
   physical_device->vk.wsi_device = NULL;
   wsi_device_finish(&physical_device->wsi_device,
                     &physical_device->vk.instance->alloc);
}

static void
constraint_surface_capabilities(VkSurfaceCapabilitiesKHR *caps)
{
   /* Our display pipeline requires that images are linear, so we cannot
    * ensure that our swapchain images can be sampled. If we are running under
    * a compositor in windowed mode, the DRM modifier negotiation should
    * probably end up selecting an UIF layout for the swapchain images but it
    * may still choose linear and send images directly for scanout if the
    * surface is in fullscreen mode for example. If we are not running under
    * a compositor, then we would always need them to be linear anyway.
    */
   caps->supportedUsageFlags &= ~VK_IMAGE_USAGE_SAMPLED_BIT;
}

VKAPI_ATTR VkResult VKAPI_CALL
v3dv_GetPhysicalDeviceSurfaceCapabilitiesKHR(
    VkPhysicalDevice                            physicalDevice,
    VkSurfaceKHR                                surface,
    VkSurfaceCapabilitiesKHR*                   pSurfaceCapabilities)
{
   VkResult result;
   result = wsi_GetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice,
                                                        surface,
                                                        pSurfaceCapabilities);
   constraint_surface_capabilities(pSurfaceCapabilities);
   return result;
}

VKAPI_ATTR VkResult VKAPI_CALL
v3dv_GetPhysicalDeviceSurfaceCapabilities2KHR(
    VkPhysicalDevice                            physicalDevice,
    const VkPhysicalDeviceSurfaceInfo2KHR*      pSurfaceInfo,
    VkSurfaceCapabilities2KHR*                  pSurfaceCapabilities)
{
   VkResult result;
   result = wsi_GetPhysicalDeviceSurfaceCapabilities2KHR(physicalDevice,
                                                         pSurfaceInfo,
                                                         pSurfaceCapabilities);
   constraint_surface_capabilities(&pSurfaceCapabilities->surfaceCapabilities);
   return result;
}

VKAPI_ATTR VkResult VKAPI_CALL
v3dv_CreateSwapchainKHR(
    VkDevice                                     _device,
    const VkSwapchainCreateInfoKHR*              pCreateInfo,
    const VkAllocationCallbacks*                 pAllocator,
    VkSwapchainKHR*                              pSwapchain)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   struct v3dv_instance *instance = device->instance;
   struct v3dv_physical_device *pdevice = &instance->physicalDevice;

   ICD_FROM_HANDLE(VkIcdSurfaceBase, surface, pCreateInfo->surface);
   VkResult result =
      v3dv_physical_device_acquire_display(instance, pdevice, surface);
   if (result != VK_SUCCESS)
      return result;

   return wsi_CreateSwapchainKHR(_device, pCreateInfo, pAllocator, pSwapchain);
}

struct v3dv_image *
v3dv_wsi_get_image_from_swapchain(VkSwapchainKHR swapchain, uint32_t index)
{
   VkImage image = wsi_common_get_image(swapchain, index);
   return v3dv_image_from_handle(image);
}

VKAPI_ATTR VkResult VKAPI_CALL
v3dv_AcquireNextImage2KHR(
    VkDevice                                     _device,
    const VkAcquireNextImageInfoKHR*             pAcquireInfo,
    uint32_t*                                    pImageIndex)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   V3DV_FROM_HANDLE(v3dv_fence, fence, pAcquireInfo->fence);
   V3DV_FROM_HANDLE(v3dv_semaphore, semaphore, pAcquireInfo->semaphore);

   struct v3dv_physical_device *pdevice = &device->instance->physicalDevice;

   VkResult result;
   result = wsi_common_acquire_next_image2(&pdevice->wsi_device, _device,
                                           pAcquireInfo, pImageIndex);

   if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR) {
      if (fence)
         drmSyncobjSignal(pdevice->render_fd, &fence->sync, 1);
      if (semaphore)
         drmSyncobjSignal(pdevice->render_fd, &semaphore->sync, 1);
   }

   return result;
}
