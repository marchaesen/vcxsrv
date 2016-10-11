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

#include "radv_wsi.h"


VkResult
radv_init_wsi(struct radv_physical_device *physical_device)
{
	VkResult result;

	memset(physical_device->wsi, 0, sizeof(physical_device->wsi));

#ifdef VK_USE_PLATFORM_XCB_KHR
	result = radv_x11_init_wsi(physical_device);
	if (result != VK_SUCCESS)
		return result;
#endif

#ifdef VK_USE_PLATFORM_WAYLAND_KHR
	result = radv_wl_init_wsi(physical_device);
	if (result != VK_SUCCESS) {
#ifdef VK_USE_PLATFORM_XCB_KHR
		radv_x11_finish_wsi(physical_device);
#endif
		return result;
	}
#endif

	return VK_SUCCESS;
}

void
radv_finish_wsi(struct radv_physical_device *physical_device)
{
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
	radv_wl_finish_wsi(physical_device);
#endif
#ifdef VK_USE_PLATFORM_XCB_KHR
	radv_x11_finish_wsi(physical_device);
#endif
}

void radv_DestroySurfaceKHR(
	VkInstance                                   _instance,
	VkSurfaceKHR                                 _surface,
	const VkAllocationCallbacks*                 pAllocator)
{
	RADV_FROM_HANDLE(radv_instance, instance, _instance);
	RADV_FROM_HANDLE(_VkIcdSurfaceBase, surface, _surface);

	radv_free2(&instance->alloc, pAllocator, surface);
}

VkResult radv_GetPhysicalDeviceSurfaceSupportKHR(
	VkPhysicalDevice                            physicalDevice,
	uint32_t                                    queueFamilyIndex,
	VkSurfaceKHR                                _surface,
	VkBool32*                                   pSupported)
{
	RADV_FROM_HANDLE(radv_physical_device, device, physicalDevice);
	RADV_FROM_HANDLE(_VkIcdSurfaceBase, surface, _surface);
	struct radv_wsi_interface *iface = device->wsi[surface->platform];

	return iface->get_support(surface, device, queueFamilyIndex, pSupported);
}

VkResult radv_GetPhysicalDeviceSurfaceCapabilitiesKHR(
	VkPhysicalDevice                            physicalDevice,
	VkSurfaceKHR                                _surface,
	VkSurfaceCapabilitiesKHR*                   pSurfaceCapabilities)
{
	RADV_FROM_HANDLE(radv_physical_device, device, physicalDevice);
	RADV_FROM_HANDLE(_VkIcdSurfaceBase, surface, _surface);
	struct radv_wsi_interface *iface = device->wsi[surface->platform];

	return iface->get_capabilities(surface, device, pSurfaceCapabilities);
}

VkResult radv_GetPhysicalDeviceSurfaceFormatsKHR(
	VkPhysicalDevice                            physicalDevice,
	VkSurfaceKHR                                _surface,
	uint32_t*                                   pSurfaceFormatCount,
	VkSurfaceFormatKHR*                         pSurfaceFormats)
{
	RADV_FROM_HANDLE(radv_physical_device, device, physicalDevice);
	RADV_FROM_HANDLE(_VkIcdSurfaceBase, surface, _surface);
	struct radv_wsi_interface *iface = device->wsi[surface->platform];

	return iface->get_formats(surface, device, pSurfaceFormatCount,
				  pSurfaceFormats);
}

VkResult radv_GetPhysicalDeviceSurfacePresentModesKHR(
	VkPhysicalDevice                            physicalDevice,
	VkSurfaceKHR                                _surface,
	uint32_t*                                   pPresentModeCount,
	VkPresentModeKHR*                           pPresentModes)
{
	RADV_FROM_HANDLE(radv_physical_device, device, physicalDevice);
	RADV_FROM_HANDLE(_VkIcdSurfaceBase, surface, _surface);
	struct radv_wsi_interface *iface = device->wsi[surface->platform];

	return iface->get_present_modes(surface, device, pPresentModeCount,
					pPresentModes);
}

VkResult radv_CreateSwapchainKHR(
	VkDevice                                     _device,
	const VkSwapchainCreateInfoKHR*              pCreateInfo,
	const VkAllocationCallbacks*                 pAllocator,
	VkSwapchainKHR*                              pSwapchain)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	RADV_FROM_HANDLE(_VkIcdSurfaceBase, surface, pCreateInfo->surface);
	struct radv_wsi_interface *iface =
		device->instance->physicalDevice.wsi[surface->platform];
	struct radv_swapchain *swapchain;

	VkResult result = iface->create_swapchain(surface, device, pCreateInfo,
						  pAllocator, &swapchain);
	if (result != VK_SUCCESS)
		return result;

	if (pAllocator)
		swapchain->alloc = *pAllocator;
	else
		swapchain->alloc = device->alloc;

	for (unsigned i = 0; i < ARRAY_SIZE(swapchain->fences); i++)
		swapchain->fences[i] = VK_NULL_HANDLE;

	*pSwapchain = radv_swapchain_to_handle(swapchain);

	return VK_SUCCESS;
}

void radv_DestroySwapchainKHR(
	VkDevice                                     device,
	VkSwapchainKHR                               _swapchain,
	const VkAllocationCallbacks*                 pAllocator)
{
	RADV_FROM_HANDLE(radv_swapchain, swapchain, _swapchain);

	for (unsigned i = 0; i < ARRAY_SIZE(swapchain->fences); i++) {
		if (swapchain->fences[i] != VK_NULL_HANDLE)
			radv_DestroyFence(device, swapchain->fences[i], pAllocator);
	}

	swapchain->destroy(swapchain, pAllocator);
}

VkResult radv_GetSwapchainImagesKHR(
	VkDevice                                     device,
	VkSwapchainKHR                               _swapchain,
	uint32_t*                                    pSwapchainImageCount,
	VkImage*                                     pSwapchainImages)
{
	RADV_FROM_HANDLE(radv_swapchain, swapchain, _swapchain);

	return swapchain->get_images(swapchain, pSwapchainImageCount,
				     pSwapchainImages);
}

VkResult radv_AcquireNextImageKHR(
	VkDevice                                     device,
	VkSwapchainKHR                               _swapchain,
	uint64_t                                     timeout,
	VkSemaphore                                  semaphore,
	VkFence                                      fence,
	uint32_t*                                    pImageIndex)
{
	RADV_FROM_HANDLE(radv_swapchain, swapchain, _swapchain);

	return swapchain->acquire_next_image(swapchain, timeout, semaphore,
					     pImageIndex);
}

VkResult radv_QueuePresentKHR(
	VkQueue                                  _queue,
	const VkPresentInfoKHR*                  pPresentInfo)
{
	RADV_FROM_HANDLE(radv_queue, queue, _queue);
	VkResult result = VK_SUCCESS;

	for (uint32_t i = 0; i < pPresentInfo->swapchainCount; i++) {
		RADV_FROM_HANDLE(radv_swapchain, swapchain, pPresentInfo->pSwapchains[i]);

		assert(swapchain->device == queue->device);
		if (swapchain->fences[0] == VK_NULL_HANDLE) {
			result = radv_CreateFence(radv_device_to_handle(queue->device),
						  &(VkFenceCreateInfo) {
							  .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
								  .flags = 0,
								  }, &swapchain->alloc, &swapchain->fences[0]);
			if (result != VK_SUCCESS)
				return result;
		} else {
			radv_ResetFences(radv_device_to_handle(queue->device),
					 1, &swapchain->fences[0]);
		}

		radv_QueueSubmit(_queue, 0, NULL, swapchain->fences[0]);

		result = swapchain->queue_present(swapchain, queue,
						  pPresentInfo->pImageIndices[i]);
		/* TODO: What if one of them returns OUT_OF_DATE? */
		if (result != VK_SUCCESS)
			return result;

		VkFence last = swapchain->fences[2];
		swapchain->fences[2] = swapchain->fences[1];
		swapchain->fences[1] = swapchain->fences[0];
		swapchain->fences[0] = last;

		if (last != VK_NULL_HANDLE) {
			radv_WaitForFences(radv_device_to_handle(queue->device),
					   1, &last, true, 1);
		}

	}

	return VK_SUCCESS;
}
