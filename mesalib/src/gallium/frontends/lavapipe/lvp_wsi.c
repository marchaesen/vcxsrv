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

static PFN_vkVoidFunction
lvp_wsi_proc_addr(VkPhysicalDevice physicalDevice, const char *pName)
{
   return lvp_lookup_entrypoint(pName);
}

VkResult
lvp_init_wsi(struct lvp_physical_device *physical_device)
{
   return wsi_device_init(&physical_device->wsi_device,
                          lvp_physical_device_to_handle(physical_device),
                          lvp_wsi_proc_addr,
                          &physical_device->instance->alloc,
                          -1, NULL, true);
}

void
lvp_finish_wsi(struct lvp_physical_device *physical_device)
{
   wsi_device_finish(&physical_device->wsi_device,
                     &physical_device->instance->alloc);
}

void lvp_DestroySurfaceKHR(
   VkInstance                                   _instance,
   VkSurfaceKHR                                 _surface,
   const VkAllocationCallbacks*                 pAllocator)
{
   LVP_FROM_HANDLE(lvp_instance, instance, _instance);
   ICD_FROM_HANDLE(VkIcdSurfaceBase, surface, _surface);

   vk_free2(&instance->alloc, pAllocator, surface);
}

VkResult lvp_GetPhysicalDeviceSurfaceSupportKHR(
   VkPhysicalDevice                            physicalDevice,
   uint32_t                                    queueFamilyIndex,
   VkSurfaceKHR                                surface,
   VkBool32*                                   pSupported)
{
   LVP_FROM_HANDLE(lvp_physical_device, device, physicalDevice);

   return wsi_common_get_surface_support(&device->wsi_device,
                                         queueFamilyIndex,
                                         surface,
                                         pSupported);
}

VkResult lvp_GetPhysicalDeviceSurfaceCapabilitiesKHR(
   VkPhysicalDevice                            physicalDevice,
   VkSurfaceKHR                                surface,
   VkSurfaceCapabilitiesKHR*                   pSurfaceCapabilities)
{
   LVP_FROM_HANDLE(lvp_physical_device, device, physicalDevice);

   return wsi_common_get_surface_capabilities(&device->wsi_device,
                                              surface,
                                              pSurfaceCapabilities);
}

VkResult lvp_GetPhysicalDeviceSurfaceCapabilities2KHR(
   VkPhysicalDevice                            physicalDevice,
   const VkPhysicalDeviceSurfaceInfo2KHR*      pSurfaceInfo,
   VkSurfaceCapabilities2KHR*                  pSurfaceCapabilities)
{
   LVP_FROM_HANDLE(lvp_physical_device, device, physicalDevice);

   return wsi_common_get_surface_capabilities2(&device->wsi_device,
                                               pSurfaceInfo,
                                               pSurfaceCapabilities);
}

VkResult lvp_GetPhysicalDeviceSurfaceCapabilities2EXT(
   VkPhysicalDevice                            physicalDevice,
   VkSurfaceKHR                                surface,
   VkSurfaceCapabilities2EXT*                  pSurfaceCapabilities)
{
   LVP_FROM_HANDLE(lvp_physical_device, device, physicalDevice);

   return wsi_common_get_surface_capabilities2ext(&device->wsi_device,
                                                  surface,
                                                  pSurfaceCapabilities);
}

VkResult lvp_GetPhysicalDeviceSurfaceFormatsKHR(
   VkPhysicalDevice                            physicalDevice,
   VkSurfaceKHR                                surface,
   uint32_t*                                   pSurfaceFormatCount,
   VkSurfaceFormatKHR*                         pSurfaceFormats)
{
   LVP_FROM_HANDLE(lvp_physical_device, device, physicalDevice);
   return wsi_common_get_surface_formats(&device->wsi_device,
                                         surface,
                                         pSurfaceFormatCount,
                                         pSurfaceFormats);
}

VkResult lvp_GetPhysicalDeviceSurfacePresentModesKHR(
   VkPhysicalDevice                            physicalDevice,
   VkSurfaceKHR                                surface,
   uint32_t*                                   pPresentModeCount,
   VkPresentModeKHR*                           pPresentModes)
{
   LVP_FROM_HANDLE(lvp_physical_device, device, physicalDevice);

   return wsi_common_get_surface_present_modes(&device->wsi_device,
                                               surface,
                                               pPresentModeCount,
                                               pPresentModes);
}

VkResult lvp_CreateSwapchainKHR(
   VkDevice                                     _device,
   const VkSwapchainCreateInfoKHR*              pCreateInfo,
   const VkAllocationCallbacks*                 pAllocator,
   VkSwapchainKHR*                              pSwapchain)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   const VkAllocationCallbacks *alloc;
   if (pAllocator)
      alloc = pAllocator;
   else
      alloc = &device->vk.alloc;

   return wsi_common_create_swapchain(&device->physical_device->wsi_device,
                                      lvp_device_to_handle(device),
                                      pCreateInfo,
                                      alloc,
                                      pSwapchain);
}

void lvp_DestroySwapchainKHR(
   VkDevice                                     _device,
   VkSwapchainKHR                               swapchain,
   const VkAllocationCallbacks*                 pAllocator)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   const VkAllocationCallbacks *alloc;

   if (pAllocator)
      alloc = pAllocator;
   else
      alloc = &device->vk.alloc;

   wsi_common_destroy_swapchain(_device, swapchain, alloc);
}

VkResult lvp_GetSwapchainImagesKHR(
   VkDevice                                     device,
   VkSwapchainKHR                               swapchain,
   uint32_t*                                    pSwapchainImageCount,
   VkImage*                                     pSwapchainImages)
{
   return wsi_common_get_images(swapchain,
                                pSwapchainImageCount,
                                pSwapchainImages);
}

VkResult lvp_AcquireNextImageKHR(
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

   return lvp_AcquireNextImage2KHR(device, &acquire_info, pImageIndex);
}

VkResult lvp_AcquireNextImage2KHR(
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
      fence->signaled = true;
   }
   return result;
}

VkResult lvp_QueuePresentKHR(
   VkQueue                                  _queue,
   const VkPresentInfoKHR*                  pPresentInfo)
{
   LVP_FROM_HANDLE(lvp_queue, queue, _queue);
   return wsi_common_queue_present(&queue->device->physical_device->wsi_device,
                                   lvp_device_to_handle(queue->device),
                                   _queue, 0,
                                   pPresentInfo);
}


VkResult lvp_GetDeviceGroupPresentCapabilitiesKHR(
   VkDevice                                    device,
   VkDeviceGroupPresentCapabilitiesKHR*        pCapabilities)
{
   memset(pCapabilities->presentMask, 0,
          sizeof(pCapabilities->presentMask));
   pCapabilities->presentMask[0] = 0x1;
   pCapabilities->modes = VK_DEVICE_GROUP_PRESENT_MODE_LOCAL_BIT_KHR;

   return VK_SUCCESS;
}

VkResult lvp_GetDeviceGroupSurfacePresentModesKHR(
   VkDevice                                    device,
   VkSurfaceKHR                                surface,
   VkDeviceGroupPresentModeFlagsKHR*           pModes)
{
   *pModes = VK_DEVICE_GROUP_PRESENT_MODE_LOCAL_BIT_KHR;

   return VK_SUCCESS;
}

VkResult lvp_GetPhysicalDevicePresentRectanglesKHR(
   VkPhysicalDevice                            physicalDevice,
   VkSurfaceKHR                                surface,
   uint32_t*                                   pRectCount,
   VkRect2D*                                   pRects)
{
   LVP_FROM_HANDLE(lvp_physical_device, device, physicalDevice);

   return wsi_common_get_present_rectangles(&device->wsi_device,
                                            surface,
                                            pRectCount, pRects);
}
