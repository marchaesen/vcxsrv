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

#include "radv_private.h"
#include "radv_meta.h"
#include "wsi_common.h"
#include "vk_util.h"
#include "util/macros.h"

static PFN_vkVoidFunction
radv_wsi_proc_addr(VkPhysicalDevice physicalDevice, const char *pName)
{
	return radv_lookup_entrypoint(pName);
}

static void
radv_wsi_set_memory_ownership(VkDevice _device,
                              VkDeviceMemory _mem,
                              VkBool32 ownership)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	RADV_FROM_HANDLE(radv_device_memory, mem, _mem);

	if (ownership)
		radv_bo_list_add(device, mem->bo);
	else
		radv_bo_list_remove(device, mem->bo);
}

VkResult
radv_init_wsi(struct radv_physical_device *physical_device)
{
	VkResult result =  wsi_device_init(&physical_device->wsi_device,
					   radv_physical_device_to_handle(physical_device),
					   radv_wsi_proc_addr,
					   &physical_device->instance->alloc,
					   physical_device->master_fd,
					   &physical_device->instance->dri_options,
					   false);
	if (result != VK_SUCCESS)
		return result;

	physical_device->wsi_device.set_memory_ownership = radv_wsi_set_memory_ownership;
	return VK_SUCCESS;
}

void
radv_finish_wsi(struct radv_physical_device *physical_device)
{
	wsi_device_finish(&physical_device->wsi_device,
			  &physical_device->instance->alloc);
}

void radv_DestroySurfaceKHR(
	VkInstance                                   _instance,
	VkSurfaceKHR                                 _surface,
	const VkAllocationCallbacks*                 pAllocator)
{
	RADV_FROM_HANDLE(radv_instance, instance, _instance);
	ICD_FROM_HANDLE(VkIcdSurfaceBase, surface, _surface);

	vk_free2(&instance->alloc, pAllocator, surface);
}

VkResult radv_GetPhysicalDeviceSurfaceSupportKHR(
	VkPhysicalDevice                            physicalDevice,
	uint32_t                                    queueFamilyIndex,
	VkSurfaceKHR                                surface,
	VkBool32*                                   pSupported)
{
	RADV_FROM_HANDLE(radv_physical_device, device, physicalDevice);

	return wsi_common_get_surface_support(&device->wsi_device,
					      queueFamilyIndex,
					      surface,
					      pSupported);
}

VkResult radv_GetPhysicalDeviceSurfaceCapabilitiesKHR(
	VkPhysicalDevice                            physicalDevice,
	VkSurfaceKHR                                surface,
	VkSurfaceCapabilitiesKHR*                   pSurfaceCapabilities)
{
	RADV_FROM_HANDLE(radv_physical_device, device, physicalDevice);

	return wsi_common_get_surface_capabilities(&device->wsi_device,
						   surface,
						   pSurfaceCapabilities);
}

VkResult radv_GetPhysicalDeviceSurfaceCapabilities2KHR(
	VkPhysicalDevice                            physicalDevice,
	const VkPhysicalDeviceSurfaceInfo2KHR*      pSurfaceInfo,
	VkSurfaceCapabilities2KHR*                  pSurfaceCapabilities)
{
	RADV_FROM_HANDLE(radv_physical_device, device, physicalDevice);

	return wsi_common_get_surface_capabilities2(&device->wsi_device,
						    pSurfaceInfo,
						    pSurfaceCapabilities);
}

VkResult radv_GetPhysicalDeviceSurfaceCapabilities2EXT(
 	VkPhysicalDevice                            physicalDevice,
	VkSurfaceKHR                                surface,
	VkSurfaceCapabilities2EXT*                  pSurfaceCapabilities)
{
	RADV_FROM_HANDLE(radv_physical_device, device, physicalDevice);

	return wsi_common_get_surface_capabilities2ext(&device->wsi_device,
						       surface,
						       pSurfaceCapabilities);
}

VkResult radv_GetPhysicalDeviceSurfaceFormatsKHR(
	VkPhysicalDevice                            physicalDevice,
	VkSurfaceKHR                                surface,
	uint32_t*                                   pSurfaceFormatCount,
	VkSurfaceFormatKHR*                         pSurfaceFormats)
{
	RADV_FROM_HANDLE(radv_physical_device, device, physicalDevice);

	return wsi_common_get_surface_formats(&device->wsi_device,
					      surface,
					      pSurfaceFormatCount,
					      pSurfaceFormats);
}

VkResult radv_GetPhysicalDeviceSurfaceFormats2KHR(
	VkPhysicalDevice                            physicalDevice,
	const VkPhysicalDeviceSurfaceInfo2KHR*      pSurfaceInfo,
	uint32_t*                                   pSurfaceFormatCount,
	VkSurfaceFormat2KHR*                        pSurfaceFormats)
{
	RADV_FROM_HANDLE(radv_physical_device, device, physicalDevice);

	return wsi_common_get_surface_formats2(&device->wsi_device,
					       pSurfaceInfo,
					       pSurfaceFormatCount,
					       pSurfaceFormats);
}

VkResult radv_GetPhysicalDeviceSurfacePresentModesKHR(
	VkPhysicalDevice                            physicalDevice,
	VkSurfaceKHR                                surface,
	uint32_t*                                   pPresentModeCount,
	VkPresentModeKHR*                           pPresentModes)
{
	RADV_FROM_HANDLE(radv_physical_device, device, physicalDevice);

	return wsi_common_get_surface_present_modes(&device->wsi_device,
						    surface,
						    pPresentModeCount,
						    pPresentModes);
}

VkResult radv_CreateSwapchainKHR(
	VkDevice                                     _device,
	const VkSwapchainCreateInfoKHR*              pCreateInfo,
	const VkAllocationCallbacks*                 pAllocator,
	VkSwapchainKHR*                              pSwapchain)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	const VkAllocationCallbacks *alloc;
	if (pAllocator)
		alloc = pAllocator;
	else
		alloc = &device->vk.alloc;

	return wsi_common_create_swapchain(&device->physical_device->wsi_device,
					   radv_device_to_handle(device),
					   pCreateInfo,
					   alloc,
					   pSwapchain);
}

void radv_DestroySwapchainKHR(
	VkDevice                                     _device,
	VkSwapchainKHR                               swapchain,
	const VkAllocationCallbacks*                 pAllocator)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	const VkAllocationCallbacks *alloc;

	if (pAllocator)
		alloc = pAllocator;
	else
		alloc = &device->vk.alloc;

	wsi_common_destroy_swapchain(_device, swapchain, alloc);
}

VkResult radv_GetSwapchainImagesKHR(
	VkDevice                                     device,
	VkSwapchainKHR                               swapchain,
	uint32_t*                                    pSwapchainImageCount,
	VkImage*                                     pSwapchainImages)
{
	return wsi_common_get_images(swapchain,
				     pSwapchainImageCount,
				     pSwapchainImages);
}

VkResult radv_AcquireNextImageKHR(
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

	return radv_AcquireNextImage2KHR(device, &acquire_info, pImageIndex);
}

VkResult radv_AcquireNextImage2KHR(
	VkDevice                                     _device,
	const VkAcquireNextImageInfoKHR*             pAcquireInfo,
	uint32_t*                                    pImageIndex)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	struct radv_physical_device *pdevice = device->physical_device;
	RADV_FROM_HANDLE(radv_fence, fence, pAcquireInfo->fence);
	RADV_FROM_HANDLE(radv_semaphore, semaphore, pAcquireInfo->semaphore);

	VkResult result = wsi_common_acquire_next_image2(&pdevice->wsi_device,
							 _device,
                                                         pAcquireInfo,
							 pImageIndex);

	if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR) {
		if (fence) {
			struct radv_fence_part *part =
				fence->temporary.kind != RADV_FENCE_NONE ?
				&fence->temporary : &fence->permanent;

			switch (part->kind) {
			case RADV_FENCE_NONE:
				break;
			case RADV_FENCE_WINSYS:
				device->ws->signal_fence(part->fence);
				break;
			case RADV_FENCE_SYNCOBJ:
				device->ws->signal_syncobj(device->ws, part->syncobj, 0);
				break;
			default:
				unreachable("Invalid WSI fence type");
			}
		}
		if (semaphore) {
			struct radv_semaphore_part *part =
				semaphore->temporary.kind != RADV_SEMAPHORE_NONE ?
					&semaphore->temporary : &semaphore->permanent;

			switch (part->kind) {
			case RADV_SEMAPHORE_NONE:
			case RADV_SEMAPHORE_WINSYS:
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

VkResult radv_QueuePresentKHR(
	VkQueue                                  _queue,
	const VkPresentInfoKHR*                  pPresentInfo)
{
	RADV_FROM_HANDLE(radv_queue, queue, _queue);
	return wsi_common_queue_present(&queue->device->physical_device->wsi_device,
					radv_device_to_handle(queue->device),
				        _queue,
				        queue->queue_family_index,
				        pPresentInfo);
}


VkResult radv_GetDeviceGroupPresentCapabilitiesKHR(
    VkDevice                                    device,
    VkDeviceGroupPresentCapabilitiesKHR*        pCapabilities)
{
   memset(pCapabilities->presentMask, 0,
          sizeof(pCapabilities->presentMask));
   pCapabilities->presentMask[0] = 0x1;
   pCapabilities->modes = VK_DEVICE_GROUP_PRESENT_MODE_LOCAL_BIT_KHR;

   return VK_SUCCESS;
}

VkResult radv_GetDeviceGroupSurfacePresentModesKHR(
    VkDevice                                    device,
    VkSurfaceKHR                                surface,
    VkDeviceGroupPresentModeFlagsKHR*           pModes)
{
   *pModes = VK_DEVICE_GROUP_PRESENT_MODE_LOCAL_BIT_KHR;

   return VK_SUCCESS;
}

VkResult radv_GetPhysicalDevicePresentRectanglesKHR(
	VkPhysicalDevice                            physicalDevice,
	VkSurfaceKHR                                surface,
	uint32_t*                                   pRectCount,
	VkRect2D*                                   pRects)
{
	RADV_FROM_HANDLE(radv_physical_device, device, physicalDevice);

	return wsi_common_get_present_rectangles(&device->wsi_device,
						 surface,
						 pRectCount, pRects);
}
