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
	return radv_lookup_entrypoint_unchecked(pName);
}

VkResult
radv_init_wsi(struct radv_physical_device *physical_device)
{
	return wsi_device_init(&physical_device->wsi_device,
			       radv_physical_device_to_handle(physical_device),
			       radv_wsi_proc_addr,
			       &physical_device->instance->alloc);
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
					      device->local_fd,
					      queueFamilyIndex,
					      surface,
					      &device->instance->alloc,
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
		alloc = &device->alloc;

	return wsi_common_create_swapchain(&device->physical_device->wsi_device,
					   radv_device_to_handle(device),
					   device->physical_device->local_fd,
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
		alloc = &device->alloc;

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
	VkDevice                                     _device,
	VkSwapchainKHR                               swapchain,
	uint64_t                                     timeout,
	VkSemaphore                                  semaphore,
	VkFence                                      _fence,
	uint32_t*                                    pImageIndex)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	struct radv_physical_device *pdevice = device->physical_device;
	RADV_FROM_HANDLE(radv_fence, fence, _fence);

	VkResult result = wsi_common_acquire_next_image(&pdevice->wsi_device,
							_device,
							swapchain,
							timeout,
							semaphore,
							pImageIndex);

	if (fence && (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR)) {
		fence->submitted = true;
		fence->signalled = true;
		if (fence->temp_syncobj) {
			device->ws->signal_syncobj(device->ws, fence->temp_syncobj);
		} else if (fence->syncobj) {
			device->ws->signal_syncobj(device->ws, fence->syncobj);
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
