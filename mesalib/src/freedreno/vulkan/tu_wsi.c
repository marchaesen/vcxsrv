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

static VKAPI_PTR PFN_vkVoidFunction
tu_wsi_proc_addr(VkPhysicalDevice physicalDevice, const char *pName)
{
   return tu_lookup_entrypoint_unchecked(pName);
}

VkResult
tu_wsi_init(struct tu_physical_device *physical_device)
{
   VkResult result;

   result = wsi_device_init(&physical_device->wsi_device,
                            tu_physical_device_to_handle(physical_device),
                            tu_wsi_proc_addr,
                            &physical_device->instance->alloc,
                            physical_device->master_fd, NULL,
                            false);
   if (result != VK_SUCCESS)
      return result;

   physical_device->wsi_device.supports_modifiers = true;

   return VK_SUCCESS;
}

void
tu_wsi_finish(struct tu_physical_device *physical_device)
{
   wsi_device_finish(&physical_device->wsi_device,
                     &physical_device->instance->alloc);
}

void
tu_DestroySurfaceKHR(VkInstance _instance,
                     VkSurfaceKHR _surface,
                     const VkAllocationCallbacks *pAllocator)
{
   TU_FROM_HANDLE(tu_instance, instance, _instance);
   ICD_FROM_HANDLE(VkIcdSurfaceBase, surface, _surface);

   vk_free2(&instance->alloc, pAllocator, surface);
}

VkResult
tu_GetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice physicalDevice,
                                      uint32_t queueFamilyIndex,
                                      VkSurfaceKHR surface,
                                      VkBool32 *pSupported)
{
   TU_FROM_HANDLE(tu_physical_device, device, physicalDevice);

   return wsi_common_get_surface_support(
      &device->wsi_device, queueFamilyIndex, surface, pSupported);
}

VkResult
tu_GetPhysicalDeviceSurfaceCapabilitiesKHR(
   VkPhysicalDevice physicalDevice,
   VkSurfaceKHR surface,
   VkSurfaceCapabilitiesKHR *pSurfaceCapabilities)
{
   TU_FROM_HANDLE(tu_physical_device, device, physicalDevice);

   return wsi_common_get_surface_capabilities(&device->wsi_device, surface,
                                              pSurfaceCapabilities);
}

VkResult
tu_GetPhysicalDeviceSurfaceCapabilities2KHR(
   VkPhysicalDevice physicalDevice,
   const VkPhysicalDeviceSurfaceInfo2KHR *pSurfaceInfo,
   VkSurfaceCapabilities2KHR *pSurfaceCapabilities)
{
   TU_FROM_HANDLE(tu_physical_device, device, physicalDevice);

   return wsi_common_get_surface_capabilities2(
      &device->wsi_device, pSurfaceInfo, pSurfaceCapabilities);
}

VkResult
tu_GetPhysicalDeviceSurfaceCapabilities2EXT(
   VkPhysicalDevice physicalDevice,
   VkSurfaceKHR surface,
   VkSurfaceCapabilities2EXT *pSurfaceCapabilities)
{
   TU_FROM_HANDLE(tu_physical_device, device, physicalDevice);

   return wsi_common_get_surface_capabilities2ext(
      &device->wsi_device, surface, pSurfaceCapabilities);
}

VkResult
tu_GetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice physicalDevice,
                                      VkSurfaceKHR surface,
                                      uint32_t *pSurfaceFormatCount,
                                      VkSurfaceFormatKHR *pSurfaceFormats)
{
   TU_FROM_HANDLE(tu_physical_device, device, physicalDevice);

   return wsi_common_get_surface_formats(
      &device->wsi_device, surface, pSurfaceFormatCount, pSurfaceFormats);
}

VkResult
tu_GetPhysicalDeviceSurfaceFormats2KHR(
   VkPhysicalDevice physicalDevice,
   const VkPhysicalDeviceSurfaceInfo2KHR *pSurfaceInfo,
   uint32_t *pSurfaceFormatCount,
   VkSurfaceFormat2KHR *pSurfaceFormats)
{
   TU_FROM_HANDLE(tu_physical_device, device, physicalDevice);

   return wsi_common_get_surface_formats2(&device->wsi_device, pSurfaceInfo,
                                          pSurfaceFormatCount,
                                          pSurfaceFormats);
}

VkResult
tu_GetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice physicalDevice,
                                           VkSurfaceKHR surface,
                                           uint32_t *pPresentModeCount,
                                           VkPresentModeKHR *pPresentModes)
{
   TU_FROM_HANDLE(tu_physical_device, device, physicalDevice);

   return wsi_common_get_surface_present_modes(
      &device->wsi_device, surface, pPresentModeCount, pPresentModes);
}

VkResult
tu_CreateSwapchainKHR(VkDevice _device,
                      const VkSwapchainCreateInfoKHR *pCreateInfo,
                      const VkAllocationCallbacks *pAllocator,
                      VkSwapchainKHR *pSwapchain)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   const VkAllocationCallbacks *alloc;
   if (pAllocator)
      alloc = pAllocator;
   else
      alloc = &device->vk.alloc;

   return wsi_common_create_swapchain(&device->physical_device->wsi_device,
                                      tu_device_to_handle(device),
                                      pCreateInfo, alloc, pSwapchain);
}

void
tu_DestroySwapchainKHR(VkDevice _device,
                       VkSwapchainKHR swapchain,
                       const VkAllocationCallbacks *pAllocator)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   const VkAllocationCallbacks *alloc;

   if (pAllocator)
      alloc = pAllocator;
   else
      alloc = &device->vk.alloc;

   wsi_common_destroy_swapchain(_device, swapchain, alloc);
}

VkResult
tu_GetSwapchainImagesKHR(VkDevice device,
                         VkSwapchainKHR swapchain,
                         uint32_t *pSwapchainImageCount,
                         VkImage *pSwapchainImages)
{
   return wsi_common_get_images(swapchain, pSwapchainImageCount,
                                pSwapchainImages);
}

VkResult
tu_AcquireNextImageKHR(VkDevice device,
                       VkSwapchainKHR swapchain,
                       uint64_t timeout,
                       VkSemaphore semaphore,
                       VkFence fence,
                       uint32_t *pImageIndex)
{
   VkAcquireNextImageInfoKHR acquire_info = {
      .sType = VK_STRUCTURE_TYPE_ACQUIRE_NEXT_IMAGE_INFO_KHR,
      .swapchain = swapchain,
      .timeout = timeout,
      .semaphore = semaphore,
      .fence = fence,
      .deviceMask = 0,
   };

   return tu_AcquireNextImage2KHR(device, &acquire_info, pImageIndex);
}

VkResult
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

VkResult
tu_QueuePresentKHR(VkQueue _queue, const VkPresentInfoKHR *pPresentInfo)
{
   TU_FROM_HANDLE(tu_queue, queue, _queue);
   return wsi_common_queue_present(
      &queue->device->physical_device->wsi_device,
      tu_device_to_handle(queue->device), _queue, queue->queue_family_index,
      pPresentInfo);
}

VkResult
tu_GetDeviceGroupPresentCapabilitiesKHR(
   VkDevice device, VkDeviceGroupPresentCapabilitiesKHR *pCapabilities)
{
   memset(pCapabilities->presentMask, 0, sizeof(pCapabilities->presentMask));
   pCapabilities->presentMask[0] = 0x1;
   pCapabilities->modes = VK_DEVICE_GROUP_PRESENT_MODE_LOCAL_BIT_KHR;

   return VK_SUCCESS;
}

VkResult
tu_GetDeviceGroupSurfacePresentModesKHR(
   VkDevice device,
   VkSurfaceKHR surface,
   VkDeviceGroupPresentModeFlagsKHR *pModes)
{
   *pModes = VK_DEVICE_GROUP_PRESENT_MODE_LOCAL_BIT_KHR;

   return VK_SUCCESS;
}

VkResult
tu_GetPhysicalDevicePresentRectanglesKHR(VkPhysicalDevice physicalDevice,
                                         VkSurfaceKHR surface,
                                         uint32_t *pRectCount,
                                         VkRect2D *pRects)
{
   TU_FROM_HANDLE(tu_physical_device, device, physicalDevice);

   return wsi_common_get_present_rectangles(&device->wsi_device, surface,
                                            pRectCount, pRects);
}
