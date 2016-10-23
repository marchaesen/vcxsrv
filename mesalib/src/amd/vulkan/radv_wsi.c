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
#include "wsi_common.h"

static const struct wsi_callbacks wsi_cbs = {
   .get_phys_device_format_properties = radv_GetPhysicalDeviceFormatProperties,
};

VkResult
radv_init_wsi(struct radv_physical_device *physical_device)
{
	VkResult result;

	memset(physical_device->wsi_device.wsi, 0, sizeof(physical_device->wsi_device.wsi));

#ifdef VK_USE_PLATFORM_XCB_KHR
	result = wsi_x11_init_wsi(&physical_device->wsi_device, &physical_device->instance->alloc);
	if (result != VK_SUCCESS)
		return result;
#endif

#ifdef VK_USE_PLATFORM_WAYLAND_KHR
	result = wsi_wl_init_wsi(&physical_device->wsi_device, &physical_device->instance->alloc,
				 radv_physical_device_to_handle(physical_device),
				 &wsi_cbs);
	if (result != VK_SUCCESS) {
#ifdef VK_USE_PLATFORM_XCB_KHR
		wsi_x11_finish_wsi(&physical_device->wsi_device, &physical_device->instance->alloc);
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
	wsi_wl_finish_wsi(&physical_device->wsi_device, &physical_device->instance->alloc);
#endif
#ifdef VK_USE_PLATFORM_XCB_KHR
	wsi_x11_finish_wsi(&physical_device->wsi_device, &physical_device->instance->alloc);
#endif
}

void radv_DestroySurfaceKHR(
	VkInstance                                   _instance,
	VkSurfaceKHR                                 _surface,
	const VkAllocationCallbacks*                 pAllocator)
{
	RADV_FROM_HANDLE(radv_instance, instance, _instance);
	RADV_FROM_HANDLE(_VkIcdSurfaceBase, surface, _surface);

	vk_free2(&instance->alloc, pAllocator, surface);
}

VkResult radv_GetPhysicalDeviceSurfaceSupportKHR(
	VkPhysicalDevice                            physicalDevice,
	uint32_t                                    queueFamilyIndex,
	VkSurfaceKHR                                _surface,
	VkBool32*                                   pSupported)
{
	RADV_FROM_HANDLE(radv_physical_device, device, physicalDevice);
	RADV_FROM_HANDLE(_VkIcdSurfaceBase, surface, _surface);
	struct wsi_interface *iface = device->wsi_device.wsi[surface->platform];

	return iface->get_support(surface, &device->wsi_device,
				  &device->instance->alloc,
				  queueFamilyIndex, pSupported);
}

VkResult radv_GetPhysicalDeviceSurfaceCapabilitiesKHR(
	VkPhysicalDevice                            physicalDevice,
	VkSurfaceKHR                                _surface,
	VkSurfaceCapabilitiesKHR*                   pSurfaceCapabilities)
{
	RADV_FROM_HANDLE(radv_physical_device, device, physicalDevice);
	RADV_FROM_HANDLE(_VkIcdSurfaceBase, surface, _surface);
	struct wsi_interface *iface = device->wsi_device.wsi[surface->platform];

	return iface->get_capabilities(surface, pSurfaceCapabilities);
}

VkResult radv_GetPhysicalDeviceSurfaceFormatsKHR(
	VkPhysicalDevice                            physicalDevice,
	VkSurfaceKHR                                _surface,
	uint32_t*                                   pSurfaceFormatCount,
	VkSurfaceFormatKHR*                         pSurfaceFormats)
{
	RADV_FROM_HANDLE(radv_physical_device, device, physicalDevice);
	RADV_FROM_HANDLE(_VkIcdSurfaceBase, surface, _surface);
	struct wsi_interface *iface = device->wsi_device.wsi[surface->platform];

	return iface->get_formats(surface, &device->wsi_device, pSurfaceFormatCount,
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
	struct wsi_interface *iface = device->wsi_device.wsi[surface->platform];

	return iface->get_present_modes(surface, pPresentModeCount,
					pPresentModes);
}

static VkResult
radv_wsi_image_create(VkDevice device_h,
		      const VkSwapchainCreateInfoKHR *pCreateInfo,
		      const VkAllocationCallbacks* pAllocator,
		      VkImage *image_p,
		      VkDeviceMemory *memory_p,
		      uint32_t *size,
		      uint32_t *offset,
		      uint32_t *row_pitch, int *fd_p)
{
	struct radv_device *device = radv_device_from_handle(device_h);
	VkResult result = VK_SUCCESS;
	struct radeon_surf *surface;
	VkImage image_h;
	struct radv_image *image;
	bool bret;
	int fd;

	result = radv_image_create(device_h,
				   &(struct radv_image_create_info) {
					   .vk_info =
						   &(VkImageCreateInfo) {
						   .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
						   .imageType = VK_IMAGE_TYPE_2D,
						   .format = pCreateInfo->imageFormat,
						   .extent = {
							   .width = pCreateInfo->imageExtent.width,
							   .height = pCreateInfo->imageExtent.height,
							   .depth = 1
						   },
						   .mipLevels = 1,
						   .arrayLayers = 1,
						   .samples = 1,
						   /* FIXME: Need a way to use X tiling to allow scanout */
						   .tiling = VK_IMAGE_TILING_OPTIMAL,
						   .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
						   .flags = 0,
					   },
						   .scanout = true},
				   NULL,
				   &image_h);
	if (result != VK_SUCCESS)
		return result;

	image = radv_image_from_handle(image_h);

	VkDeviceMemory memory_h;
	struct radv_device_memory *memory;
	result = radv_AllocateMemory(device_h,
				     &(VkMemoryAllocateInfo) {
					     .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
						     .allocationSize = image->size,
						     .memoryTypeIndex = 0,
						     },
				     NULL /* XXX: pAllocator */,
				     &memory_h);
	if (result != VK_SUCCESS)
		goto fail_create_image;

	memory = radv_device_memory_from_handle(memory_h);

	radv_BindImageMemory(VK_NULL_HANDLE, image_h, memory_h, 0);

	bret = device->ws->buffer_get_fd(device->ws,
					 memory->bo, &fd);
	if (bret == false)
		goto fail_alloc_memory;

	{
		struct radeon_bo_metadata metadata;
		radv_init_metadata(device, image, &metadata);
		device->ws->buffer_set_metadata(memory->bo, &metadata);
	}
	surface = &image->surface;

	*image_p = image_h;
	*memory_p = memory_h;
	*fd_p = fd;
	*size = image->size;
	*offset = image->offset;
	*row_pitch = surface->level[0].pitch_bytes;
	return VK_SUCCESS;
 fail_alloc_memory:
	radv_FreeMemory(device_h, memory_h, pAllocator);

fail_create_image:
	radv_DestroyImage(device_h, image_h, pAllocator);

	return result;
}

static void
radv_wsi_image_free(VkDevice device,
		    const VkAllocationCallbacks* pAllocator,
		    VkImage image_h,
		    VkDeviceMemory memory_h)
{
	radv_DestroyImage(device, image_h, pAllocator);

	radv_FreeMemory(device, memory_h, pAllocator);
}

static const struct wsi_image_fns radv_wsi_image_fns = {
   .create_wsi_image = radv_wsi_image_create,
   .free_wsi_image = radv_wsi_image_free,
};

VkResult radv_CreateSwapchainKHR(
	VkDevice                                     _device,
	const VkSwapchainCreateInfoKHR*              pCreateInfo,
	const VkAllocationCallbacks*                 pAllocator,
	VkSwapchainKHR*                              pSwapchain)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	RADV_FROM_HANDLE(_VkIcdSurfaceBase, surface, pCreateInfo->surface);
	struct wsi_interface *iface =
		device->instance->physicalDevice.wsi_device.wsi[surface->platform];
	struct wsi_swapchain *swapchain;
	const VkAllocationCallbacks *alloc;
	if (pAllocator)
		alloc = pAllocator;
	else
		alloc = &device->alloc;
	VkResult result = iface->create_swapchain(surface, _device,
						  &device->instance->physicalDevice.wsi_device,
						  pCreateInfo,
						  alloc, &radv_wsi_image_fns,
						  &swapchain);
	if (result != VK_SUCCESS)
		return result;

	if (pAllocator)
		swapchain->alloc = *pAllocator;
	else
		swapchain->alloc = device->alloc;

	for (unsigned i = 0; i < ARRAY_SIZE(swapchain->fences); i++)
		swapchain->fences[i] = VK_NULL_HANDLE;

	*pSwapchain = wsi_swapchain_to_handle(swapchain);

	return VK_SUCCESS;
}

void radv_DestroySwapchainKHR(
	VkDevice                                     _device,
	VkSwapchainKHR                               _swapchain,
	const VkAllocationCallbacks*                 pAllocator)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	RADV_FROM_HANDLE(wsi_swapchain, swapchain, _swapchain);
	const VkAllocationCallbacks *alloc;

	if (pAllocator)
		alloc = pAllocator;
	else
		alloc = &device->alloc;

	for (unsigned i = 0; i < ARRAY_SIZE(swapchain->fences); i++) {
		if (swapchain->fences[i] != VK_NULL_HANDLE)
			radv_DestroyFence(_device, swapchain->fences[i], pAllocator);
	}

	swapchain->destroy(swapchain, alloc);
}

VkResult radv_GetSwapchainImagesKHR(
	VkDevice                                     device,
	VkSwapchainKHR                               _swapchain,
	uint32_t*                                    pSwapchainImageCount,
	VkImage*                                     pSwapchainImages)
{
	RADV_FROM_HANDLE(wsi_swapchain, swapchain, _swapchain);

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
	RADV_FROM_HANDLE(wsi_swapchain, swapchain, _swapchain);

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
		RADV_FROM_HANDLE(wsi_swapchain, swapchain, pPresentInfo->pSwapchains[i]);

		assert(radv_device_from_handle(swapchain->device) == queue->device);
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

		result = swapchain->queue_present(swapchain,
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
