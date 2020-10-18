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
#include "vk_format_info.h"
#include "vk_util.h"
#include "wsi_common.h"

static PFN_vkVoidFunction
v3dv_wsi_proc_addr(VkPhysicalDevice physicalDevice, const char *pName)
{
   V3DV_FROM_HANDLE(v3dv_physical_device, physical_device, physicalDevice);
   return v3dv_lookup_entrypoint(&physical_device->devinfo, pName);
}

VkResult
v3dv_wsi_init(struct v3dv_physical_device *physical_device)
{
   VkResult result;

   result = wsi_device_init(&physical_device->wsi_device,
                            v3dv_physical_device_to_handle(physical_device),
                            v3dv_wsi_proc_addr,
                            &physical_device->instance->alloc,
                            physical_device->display_fd, NULL, false);

   if (result != VK_SUCCESS)
      return result;

   physical_device->wsi_device.supports_modifiers = true;

   return VK_SUCCESS;
}

void
v3dv_wsi_finish(struct v3dv_physical_device *physical_device)
{
   wsi_device_finish(&physical_device->wsi_device,
                     &physical_device->instance->alloc);
}

void v3dv_DestroySurfaceKHR(
    VkInstance                                   _instance,
    VkSurfaceKHR                                 _surface,
    const VkAllocationCallbacks*                 pAllocator)
{
   V3DV_FROM_HANDLE(v3dv_instance, instance, _instance);
   ICD_FROM_HANDLE(VkIcdSurfaceBase, surface, _surface);

   if (!surface)
      return;

   vk_free2(&instance->alloc, pAllocator, surface);
}

VkResult v3dv_GetPhysicalDeviceSurfaceSupportKHR(
    VkPhysicalDevice                            physicalDevice,
    uint32_t                                    queueFamilyIndex,
    VkSurfaceKHR                                surface,
    VkBool32*                                   pSupported)
{
   V3DV_FROM_HANDLE(v3dv_physical_device, device, physicalDevice);

   return wsi_common_get_surface_support(&device->wsi_device,
                                         queueFamilyIndex,
                                         surface,
                                         pSupported);
}

static void
contraint_surface_capabilities(VkSurfaceCapabilitiesKHR *caps)
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

VkResult v3dv_GetPhysicalDeviceSurfaceCapabilitiesKHR(
    VkPhysicalDevice                            physicalDevice,
    VkSurfaceKHR                                surface,
    VkSurfaceCapabilitiesKHR*                   pSurfaceCapabilities)
{
   V3DV_FROM_HANDLE(v3dv_physical_device, device, physicalDevice);

   VkResult result;
   result = wsi_common_get_surface_capabilities(&device->wsi_device,
                                                surface,
                                                pSurfaceCapabilities);
   contraint_surface_capabilities(pSurfaceCapabilities);
   return result;
}

VkResult v3dv_GetPhysicalDeviceSurfaceCapabilities2KHR(
    VkPhysicalDevice                            physicalDevice,
    const VkPhysicalDeviceSurfaceInfo2KHR*      pSurfaceInfo,
    VkSurfaceCapabilities2KHR*                  pSurfaceCapabilities)
{
   V3DV_FROM_HANDLE(v3dv_physical_device, device, physicalDevice);

   VkResult result;
   result = wsi_common_get_surface_capabilities2(&device->wsi_device,
                                                 pSurfaceInfo,
                                                 pSurfaceCapabilities);
   contraint_surface_capabilities(&pSurfaceCapabilities->surfaceCapabilities);
   return result;
}

VkResult v3dv_GetPhysicalDeviceSurfaceFormatsKHR(
    VkPhysicalDevice                            physicalDevice,
    VkSurfaceKHR                                surface,
    uint32_t*                                   pSurfaceFormatCount,
    VkSurfaceFormatKHR*                         pSurfaceFormats)
{
   V3DV_FROM_HANDLE(v3dv_physical_device, device, physicalDevice);

   return wsi_common_get_surface_formats(&device->wsi_device, surface,
                                         pSurfaceFormatCount, pSurfaceFormats);
}

VkResult v3dv_GetPhysicalDeviceSurfaceFormats2KHR(
    VkPhysicalDevice                            physicalDevice,
    const VkPhysicalDeviceSurfaceInfo2KHR*      pSurfaceInfo,
    uint32_t*                                   pSurfaceFormatCount,
    VkSurfaceFormat2KHR*                        pSurfaceFormats)
{
   V3DV_FROM_HANDLE(v3dv_physical_device, device, physicalDevice);

   return wsi_common_get_surface_formats2(&device->wsi_device, pSurfaceInfo,
                                          pSurfaceFormatCount, pSurfaceFormats);
}

VkResult v3dv_GetPhysicalDeviceSurfacePresentModesKHR(
    VkPhysicalDevice                            physicalDevice,
    VkSurfaceKHR                                surface,
    uint32_t*                                   pPresentModeCount,
    VkPresentModeKHR*                           pPresentModes)
{
   V3DV_FROM_HANDLE(v3dv_physical_device, device, physicalDevice);

   return wsi_common_get_surface_present_modes(&device->wsi_device, surface,
                                               pPresentModeCount,
                                               pPresentModes);
}

VkResult v3dv_CreateSwapchainKHR(
    VkDevice                                     _device,
    const VkSwapchainCreateInfoKHR*              pCreateInfo,
    const VkAllocationCallbacks*                 pAllocator,
    VkSwapchainKHR*                              pSwapchain)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   struct wsi_device *wsi_device = &device->instance->physicalDevice.wsi_device;
   const VkAllocationCallbacks *alloc;

   if (pAllocator)
     alloc = pAllocator;
   else
     alloc = &device->alloc;

   return wsi_common_create_swapchain(wsi_device, _device,
                                      pCreateInfo, alloc, pSwapchain);
}

void v3dv_DestroySwapchainKHR(
    VkDevice                                     _device,
    VkSwapchainKHR                               swapchain,
    const VkAllocationCallbacks*                 pAllocator)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   const VkAllocationCallbacks *alloc;

   if (pAllocator)
     alloc = pAllocator;
   else
     alloc = &device->alloc;

   wsi_common_destroy_swapchain(_device, swapchain, alloc);
}

VkResult v3dv_GetSwapchainImagesKHR(
    VkDevice                                     device,
    VkSwapchainKHR                               swapchain,
    uint32_t*                                    pSwapchainImageCount,
    VkImage*                                     pSwapchainImages)
{
   return wsi_common_get_images(swapchain,
                                pSwapchainImageCount,
                                pSwapchainImages);
}

VkResult v3dv_AcquireNextImageKHR(
    VkDevice                                     device,
    VkSwapchainKHR                               swapchain,
    uint64_t                                     timeout,
    VkSemaphore                                  semaphore,
    VkFence                                      fence,
    uint32_t*                                    pImageIndex)
{
   VkAcquireNextImageInfoKHR acquire_info = {
      .sType = VK_STRUCTURE_TYPE_ACQUIRE_NEXT_IMAGE_INFO_KHR,
      .swapchain = swapchain,
      .timeout = timeout,
      .semaphore = semaphore,
      .fence = fence,
      .deviceMask = 0,
   };

   return v3dv_AcquireNextImage2KHR(device, &acquire_info, pImageIndex);
}

VkResult v3dv_AcquireNextImage2KHR(
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
         drmSyncobjSignal(device->render_fd, &fence->sync, 1);
      if (semaphore)
         drmSyncobjSignal(device->render_fd, &semaphore->sync, 1);
   }

   return result;
}

VkResult v3dv_QueuePresentKHR(
    VkQueue                                  _queue,
    const VkPresentInfoKHR*                  pPresentInfo)
{
   V3DV_FROM_HANDLE(v3dv_queue, queue, _queue);
   struct v3dv_physical_device *pdevice =
      &queue->device->instance->physicalDevice;

   return wsi_common_queue_present(&pdevice->wsi_device,
                                   v3dv_device_to_handle(queue->device),
                                   _queue, 0,
                                   pPresentInfo);
}

VkResult v3dv_GetDeviceGroupPresentCapabilitiesKHR(
    VkDevice                                    device,
    VkDeviceGroupPresentCapabilitiesKHR*        pCapabilities)
{
   memset(pCapabilities->presentMask, 0,
          sizeof(pCapabilities->presentMask));
   pCapabilities->presentMask[0] = 0x1;
   pCapabilities->modes = VK_DEVICE_GROUP_PRESENT_MODE_LOCAL_BIT_KHR;

   return VK_SUCCESS;
}

VkResult v3dv_GetDeviceGroupSurfacePresentModesKHR(
    VkDevice                                    device,
    VkSurfaceKHR                                surface,
    VkDeviceGroupPresentModeFlagsKHR*           pModes)
{
   *pModes = VK_DEVICE_GROUP_PRESENT_MODE_LOCAL_BIT_KHR;

   return VK_SUCCESS;
}

VkResult v3dv_GetPhysicalDevicePresentRectanglesKHR(
    VkPhysicalDevice                            physicalDevice,
    VkSurfaceKHR                                surface,
    uint32_t*                                   pRectCount,
    VkRect2D*                                   pRects)
{
   V3DV_FROM_HANDLE(v3dv_physical_device, device, physicalDevice);

   return wsi_common_get_present_rectangles(&device->wsi_device,
                                            surface,
                                            pRectCount, pRects);
}
