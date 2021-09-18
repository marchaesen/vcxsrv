/*
 * Copyright 2019 Google LLC
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv and radv which are:
 * Copyright © 2015 Intel Corporation
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 */

#include "vn_wsi.h"

#include "vk_enum_to_str.h"

#include "vn_device.h"
#include "vn_image.h"
#include "vn_instance.h"
#include "vn_physical_device.h"
#include "vn_queue.h"

/* The common WSI support makes some assumptions about the driver.
 *
 * In wsi_device_init, it assumes VK_EXT_pci_bus_info is available.  In
 * wsi_create_native_image and wsi_create_prime_image, it assumes
 * VK_KHR_external_memory_fd and VK_EXT_external_memory_dma_buf are enabled.
 *
 * In wsi_create_native_image, if wsi_device::supports_modifiers is set and
 * the window system supports modifiers, it assumes
 * VK_EXT_image_drm_format_modifier is enabled.  Otherwise, it assumes that
 * wsi_image_create_info can be chained to VkImageCreateInfo and
 * vkGetImageSubresourceLayout can be called even the tiling is
 * VK_IMAGE_TILING_OPTIMAL.
 *
 * Together, it knows how to share dma-bufs, with explicit or implicit
 * modifiers, to the window system.
 *
 * For venus, we use explicit modifiers when the renderer and the window
 * system support them.  Otherwise, we have to fall back to
 * VK_IMAGE_TILING_LINEAR (or trigger the prime blit path).  But the fallback
 * can be problematic when the memory is scanned out directly and special
 * requirements (e.g., alignments) must be met.
 *
 * The common WSI support makes other assumptions about the driver to support
 * implicit fencing.  In wsi_create_native_image and wsi_create_prime_image,
 * it assumes wsi_memory_allocate_info can be chained to VkMemoryAllocateInfo.
 * In wsi_common_queue_present, it assumes wsi_memory_signal_submit_info can
 * be chained to VkSubmitInfo.  Finally, in wsi_common_acquire_next_image2, it
 * calls wsi_device::signal_semaphore_for_memory, and
 * wsi_device::signal_fence_for_memory if the driver provides them.
 *
 * Some drivers use wsi_memory_allocate_info to set up implicit fencing.
 * Others use wsi_memory_signal_submit_info to set up implicit IN-fences and
 * use wsi_device::signal_*_for_memory to set up implicit OUT-fences.
 *
 * For venus, implicit fencing is broken (and there is no explicit fencing
 * support yet).  The kernel driver assumes everything is in the same fence
 * context and no synchronization is needed.  It should be fixed for
 * correctness, but it is still not ideal.  venus requires explicit fencing
 * (and renderer-side synchronization) to work well.
 */

/* cast a WSI object to a pointer for logging */
#define VN_WSI_PTR(obj) ((const void *)(uintptr_t)(obj))

static PFN_vkVoidFunction
vn_wsi_proc_addr(VkPhysicalDevice physicalDevice, const char *pName)
{
   struct vn_physical_device *physical_dev =
      vn_physical_device_from_handle(physicalDevice);
   return vk_instance_get_proc_addr_unchecked(
      &physical_dev->instance->base.base, pName);
}

VkResult
vn_wsi_init(struct vn_physical_device *physical_dev)
{
   const VkAllocationCallbacks *alloc =
      &physical_dev->instance->base.base.alloc;
   VkResult result = wsi_device_init(
      &physical_dev->wsi_device, vn_physical_device_to_handle(physical_dev),
      vn_wsi_proc_addr, alloc, -1, &physical_dev->instance->dri_options,
      false);
   if (result != VK_SUCCESS)
      return result;

   if (physical_dev->base.base.supported_extensions
          .EXT_image_drm_format_modifier)
      physical_dev->wsi_device.supports_modifiers = true;

   return VK_SUCCESS;
}

void
vn_wsi_fini(struct vn_physical_device *physical_dev)
{
   const VkAllocationCallbacks *alloc =
      &physical_dev->instance->base.base.alloc;
   wsi_device_finish(&physical_dev->wsi_device, alloc);
}

VkResult
vn_wsi_create_image(struct vn_device *dev,
                    const VkImageCreateInfo *create_info,
                    const struct wsi_image_create_info *wsi_info,
                    const VkAllocationCallbacks *alloc,
                    struct vn_image **out_img)
{
   /* TODO This is the legacy path used by wsi_create_native_image when there
    * is no modifier support.  Instead of forcing VK_IMAGE_TILING_LINEAR, we
    * should ask wsi to use wsi_create_prime_image instead.
    *
    * In fact, this is not enough when the image is truely used for scanout by
    * the host compositor.  There can be requirements we fail to meet.  We
    * should require modifier support at some point.
    */
   VkImageCreateInfo local_create_info;
   if (wsi_info->scanout) {
      local_create_info = *create_info;
      local_create_info.tiling = VK_IMAGE_TILING_LINEAR;
      create_info = &local_create_info;

      if (VN_DEBUG(WSI))
         vn_log(dev->instance, "forcing scanout image linear");
   }

   struct vn_image *img;
   VkResult result = vn_image_create(dev, create_info, alloc, &img);
   if (result != VK_SUCCESS)
      return result;

   img->is_wsi = true;
   img->is_prime_blit_src = wsi_info->prime_blit_src;

   *out_img = img;
   return VK_SUCCESS;
}

/* surface commands */

void
vn_DestroySurfaceKHR(VkInstance _instance,
                     VkSurfaceKHR surface,
                     const VkAllocationCallbacks *pAllocator)
{
   struct vn_instance *instance = vn_instance_from_handle(_instance);
   ICD_FROM_HANDLE(VkIcdSurfaceBase, surf, surface);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &instance->base.base.alloc;

   vk_free(alloc, surf);
}

VkResult
vn_GetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice physicalDevice,
                                      uint32_t queueFamilyIndex,
                                      VkSurfaceKHR surface,
                                      VkBool32 *pSupported)
{
   struct vn_physical_device *physical_dev =
      vn_physical_device_from_handle(physicalDevice);

   VkResult result = wsi_common_get_surface_support(
      &physical_dev->wsi_device, queueFamilyIndex, surface, pSupported);

   return vn_result(physical_dev->instance, result);
}

VkResult
vn_GetPhysicalDeviceSurfaceCapabilitiesKHR(
   VkPhysicalDevice physicalDevice,
   VkSurfaceKHR surface,
   VkSurfaceCapabilitiesKHR *pSurfaceCapabilities)
{
   struct vn_physical_device *physical_dev =
      vn_physical_device_from_handle(physicalDevice);

   VkResult result = wsi_common_get_surface_capabilities(
      &physical_dev->wsi_device, surface, pSurfaceCapabilities);

   return vn_result(physical_dev->instance, result);
}

VkResult
vn_GetPhysicalDeviceSurfaceCapabilities2KHR(
   VkPhysicalDevice physicalDevice,
   const VkPhysicalDeviceSurfaceInfo2KHR *pSurfaceInfo,
   VkSurfaceCapabilities2KHR *pSurfaceCapabilities)
{
   struct vn_physical_device *physical_dev =
      vn_physical_device_from_handle(physicalDevice);

   VkResult result = wsi_common_get_surface_capabilities2(
      &physical_dev->wsi_device, pSurfaceInfo, pSurfaceCapabilities);

   return vn_result(physical_dev->instance, result);
}

VkResult
vn_GetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice physicalDevice,
                                      VkSurfaceKHR surface,
                                      uint32_t *pSurfaceFormatCount,
                                      VkSurfaceFormatKHR *pSurfaceFormats)
{
   struct vn_physical_device *physical_dev =
      vn_physical_device_from_handle(physicalDevice);

   VkResult result =
      wsi_common_get_surface_formats(&physical_dev->wsi_device, surface,
                                     pSurfaceFormatCount, pSurfaceFormats);

   return vn_result(physical_dev->instance, result);
}

VkResult
vn_GetPhysicalDeviceSurfaceFormats2KHR(
   VkPhysicalDevice physicalDevice,
   const VkPhysicalDeviceSurfaceInfo2KHR *pSurfaceInfo,
   uint32_t *pSurfaceFormatCount,
   VkSurfaceFormat2KHR *pSurfaceFormats)
{
   struct vn_physical_device *physical_dev =
      vn_physical_device_from_handle(physicalDevice);

   VkResult result =
      wsi_common_get_surface_formats2(&physical_dev->wsi_device, pSurfaceInfo,
                                      pSurfaceFormatCount, pSurfaceFormats);

   return vn_result(physical_dev->instance, result);
}

VkResult
vn_GetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice physicalDevice,
                                           VkSurfaceKHR surface,
                                           uint32_t *pPresentModeCount,
                                           VkPresentModeKHR *pPresentModes)
{
   struct vn_physical_device *physical_dev =
      vn_physical_device_from_handle(physicalDevice);

   VkResult result = wsi_common_get_surface_present_modes(
      &physical_dev->wsi_device, surface, pPresentModeCount, pPresentModes);

   return vn_result(physical_dev->instance, result);
}

VkResult
vn_GetDeviceGroupPresentCapabilitiesKHR(
   VkDevice device, VkDeviceGroupPresentCapabilitiesKHR *pCapabilities)
{
   memset(pCapabilities->presentMask, 0, sizeof(pCapabilities->presentMask));
   pCapabilities->presentMask[0] = 0x1;
   pCapabilities->modes = VK_DEVICE_GROUP_PRESENT_MODE_LOCAL_BIT_KHR;

   return VK_SUCCESS;
}

VkResult
vn_GetDeviceGroupSurfacePresentModesKHR(
   VkDevice device,
   VkSurfaceKHR surface,
   VkDeviceGroupPresentModeFlagsKHR *pModes)
{
   *pModes = VK_DEVICE_GROUP_PRESENT_MODE_LOCAL_BIT_KHR;

   return VK_SUCCESS;
}

VkResult
vn_GetPhysicalDevicePresentRectanglesKHR(VkPhysicalDevice physicalDevice,
                                         VkSurfaceKHR surface,
                                         uint32_t *pRectCount,
                                         VkRect2D *pRects)
{
   struct vn_physical_device *physical_dev =
      vn_physical_device_from_handle(physicalDevice);

   VkResult result = wsi_common_get_present_rectangles(
      &physical_dev->wsi_device, surface, pRectCount, pRects);

   return vn_result(physical_dev->instance, result);
}

/* swapchain commands */

VkResult
vn_CreateSwapchainKHR(VkDevice device,
                      const VkSwapchainCreateInfoKHR *pCreateInfo,
                      const VkAllocationCallbacks *pAllocator,
                      VkSwapchainKHR *pSwapchain)
{
   struct vn_device *dev = vn_device_from_handle(device);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.base.alloc;

   VkResult result =
      wsi_common_create_swapchain(&dev->physical_device->wsi_device, device,
                                  pCreateInfo, alloc, pSwapchain);
   if (VN_DEBUG(WSI) && result == VK_SUCCESS) {
      vn_log(dev->instance,
             "swapchain %p: created with surface %p, min count %d, size "
             "%dx%d, mode %s, old %p",
             VN_WSI_PTR(*pSwapchain), VN_WSI_PTR(pCreateInfo->surface),
             pCreateInfo->minImageCount, pCreateInfo->imageExtent.width,
             pCreateInfo->imageExtent.height,
             vk_PresentModeKHR_to_str(pCreateInfo->presentMode),
             VN_WSI_PTR(pCreateInfo->oldSwapchain));
   }

   return vn_result(dev->instance, result);
}

void
vn_DestroySwapchainKHR(VkDevice device,
                       VkSwapchainKHR swapchain,
                       const VkAllocationCallbacks *pAllocator)
{
   struct vn_device *dev = vn_device_from_handle(device);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.base.alloc;

   wsi_common_destroy_swapchain(device, swapchain, alloc);
   if (VN_DEBUG(WSI))
      vn_log(dev->instance, "swapchain %p: destroyed", VN_WSI_PTR(swapchain));
}

VkResult
vn_GetSwapchainImagesKHR(VkDevice device,
                         VkSwapchainKHR swapchain,
                         uint32_t *pSwapchainImageCount,
                         VkImage *pSwapchainImages)
{
   struct vn_device *dev = vn_device_from_handle(device);

   VkResult result = wsi_common_get_images(swapchain, pSwapchainImageCount,
                                           pSwapchainImages);

   return vn_result(dev->instance, result);
}

VkResult
vn_AcquireNextImageKHR(VkDevice device,
                       VkSwapchainKHR swapchain,
                       uint64_t timeout,
                       VkSemaphore semaphore,
                       VkFence fence,
                       uint32_t *pImageIndex)
{
   const VkAcquireNextImageInfoKHR acquire_info = {
      .sType = VK_STRUCTURE_TYPE_ACQUIRE_NEXT_IMAGE_INFO_KHR,
      .swapchain = swapchain,
      .timeout = timeout,
      .semaphore = semaphore,
      .fence = fence,
      .deviceMask = 0x1,
   };

   return vn_AcquireNextImage2KHR(device, &acquire_info, pImageIndex);
}

VkResult
vn_QueuePresentKHR(VkQueue _queue, const VkPresentInfoKHR *pPresentInfo)
{
   struct vn_queue *queue = vn_queue_from_handle(_queue);

   VkResult result =
      wsi_common_queue_present(&queue->device->physical_device->wsi_device,
                               vn_device_to_handle(queue->device), _queue,
                               queue->family, pPresentInfo);
   if (VN_DEBUG(WSI) && result != VK_SUCCESS) {
      for (uint32_t i = 0; i < pPresentInfo->swapchainCount; i++) {
         const VkResult r =
            pPresentInfo->pResults ? pPresentInfo->pResults[i] : result;
         vn_log(queue->device->instance,
                "swapchain %p: presented image %d: %s",
                VN_WSI_PTR(pPresentInfo->pSwapchains[i]),
                pPresentInfo->pImageIndices[i], vk_Result_to_str(r));
      }
   }

   return vn_result(queue->device->instance, result);
}

VkResult
vn_AcquireNextImage2KHR(VkDevice device,
                        const VkAcquireNextImageInfoKHR *pAcquireInfo,
                        uint32_t *pImageIndex)
{
   struct vn_device *dev = vn_device_from_handle(device);

   VkResult result = wsi_common_acquire_next_image2(
      &dev->physical_device->wsi_device, device, pAcquireInfo, pImageIndex);
   if (VN_DEBUG(WSI) && result != VK_SUCCESS) {
      const int idx = result >= VK_SUCCESS ? *pImageIndex : -1;
      vn_log(dev->instance, "swapchain %p: acquired image %d: %s",
             VN_WSI_PTR(pAcquireInfo->swapchain), idx,
             vk_Result_to_str(result));
   }

   /* XXX this relies on implicit sync */
   if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR) {
      struct vn_semaphore *sem =
         vn_semaphore_from_handle(pAcquireInfo->semaphore);
      if (sem)
         vn_semaphore_signal_wsi(dev, sem);

      struct vn_fence *fence = vn_fence_from_handle(pAcquireInfo->fence);
      if (fence)
         vn_fence_signal_wsi(dev, fence);
   }

   return vn_result(dev->instance, result);
}
