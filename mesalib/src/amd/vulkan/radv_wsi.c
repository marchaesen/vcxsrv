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

MAYBE_UNUSED static const struct wsi_callbacks wsi_cbs = {
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
	ICD_FROM_HANDLE(VkIcdSurfaceBase, surface, _surface);

	vk_free2(&instance->alloc, pAllocator, surface);
}

VkResult radv_GetPhysicalDeviceSurfaceSupportKHR(
	VkPhysicalDevice                            physicalDevice,
	uint32_t                                    queueFamilyIndex,
	VkSurfaceKHR                                _surface,
	VkBool32*                                   pSupported)
{
	RADV_FROM_HANDLE(radv_physical_device, device, physicalDevice);
	ICD_FROM_HANDLE(VkIcdSurfaceBase, surface, _surface);
	struct wsi_interface *iface = device->wsi_device.wsi[surface->platform];

	return iface->get_support(surface, &device->wsi_device,
				  &device->instance->alloc,
				  queueFamilyIndex, device->local_fd, true, pSupported);
}

VkResult radv_GetPhysicalDeviceSurfaceCapabilitiesKHR(
	VkPhysicalDevice                            physicalDevice,
	VkSurfaceKHR                                _surface,
	VkSurfaceCapabilitiesKHR*                   pSurfaceCapabilities)
{
	RADV_FROM_HANDLE(radv_physical_device, device, physicalDevice);
	ICD_FROM_HANDLE(VkIcdSurfaceBase, surface, _surface);
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
	ICD_FROM_HANDLE(VkIcdSurfaceBase, surface, _surface);
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
	ICD_FROM_HANDLE(VkIcdSurfaceBase, surface, _surface);
	struct wsi_interface *iface = device->wsi_device.wsi[surface->platform];

	return iface->get_present_modes(surface, pPresentModeCount,
					pPresentModes);
}

static VkResult
radv_wsi_image_create(VkDevice device_h,
		      const VkSwapchainCreateInfoKHR *pCreateInfo,
		      const VkAllocationCallbacks* pAllocator,
		      bool needs_linear_copy,
		      bool linear,
		      VkImage *image_p,
		      VkDeviceMemory *memory_p,
		      uint32_t *size,
		      uint32_t *offset,
		      uint32_t *row_pitch, int *fd_p)
{
	VkResult result = VK_SUCCESS;
	struct radeon_surf *surface;
	VkImage image_h;
	struct radv_image *image;
	int fd;
	RADV_FROM_HANDLE(radv_device, device, device_h);

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
						   .tiling = linear ? VK_IMAGE_TILING_LINEAR : VK_IMAGE_TILING_OPTIMAL,
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

	const VkMemoryDedicatedAllocateInfoKHR ded_alloc = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO_KHR,
		.pNext = NULL,
		.buffer = VK_NULL_HANDLE,
		.image = image_h
	};

	/* Find the first VRAM memory type, or GART for PRIME images. */
	int memory_type_index = -1;
	for (int i = 0; i < device->physical_device->memory_properties.memoryTypeCount; ++i) {
		bool is_local = !!(device->physical_device->memory_properties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		if ((linear && !is_local) || (!linear && is_local)) {
			memory_type_index = i;
			break;
		}
	}

	/* fallback */
	if (memory_type_index == -1)
		memory_type_index = 0;

	result = radv_alloc_memory(device_h,
				     &(VkMemoryAllocateInfo) {
					     .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
					     .pNext = &ded_alloc,
					     .allocationSize = image->size,
					     .memoryTypeIndex = memory_type_index,
				     },
				     NULL /* XXX: pAllocator */,
				     RADV_MEM_IMPLICIT_SYNC,
				     &memory_h);
	if (result != VK_SUCCESS)
		goto fail_create_image;

	radv_BindImageMemory(device_h, image_h, memory_h, 0);

	/*
	 * return the fd for the image in the no copy mode,
	 * or the fd for the linear image if a copy is required.
	 */
	if (!needs_linear_copy || (needs_linear_copy && linear)) {
		RADV_FROM_HANDLE(radv_device_memory, memory, memory_h);
		if (!radv_get_memory_fd(device, memory, &fd))
			goto fail_alloc_memory;
		*fd_p = fd;
	}

	surface = &image->surface;

	*image_p = image_h;
	*memory_p = memory_h;
	*size = image->size;
	*offset = image->offset;

	if (device->physical_device->rad_info.chip_class >= GFX9)
		*row_pitch = surface->u.gfx9.surf_pitch * surface->bpe;
	else
		*row_pitch = surface->u.legacy.level[0].nblk_x * surface->bpe;
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

#define NUM_PRIME_POOLS RADV_QUEUE_TRANSFER
static void
radv_wsi_free_prime_command_buffers(struct radv_device *device,
				    struct wsi_swapchain *swapchain)
{
	const int num_pools = NUM_PRIME_POOLS;
	const int num_images = swapchain->image_count;
	int i;
	for (i = 0; i < num_pools; i++) {
		radv_FreeCommandBuffers(radv_device_to_handle(device),
				     swapchain->cmd_pools[i],
				     swapchain->image_count,
				     &swapchain->cmd_buffers[i * num_images]);

		radv_DestroyCommandPool(radv_device_to_handle(device),
				     swapchain->cmd_pools[i],
				     &swapchain->alloc);
	}
}

static VkResult
radv_wsi_create_prime_command_buffers(struct radv_device *device,
				      const VkAllocationCallbacks *alloc,
				      struct wsi_swapchain *swapchain)
{
	const int num_pools = NUM_PRIME_POOLS;
	const int num_images = swapchain->image_count;
	int num_cmd_buffers = num_images * num_pools; //TODO bump to MAX_QUEUE_FAMILIES
	VkResult result;
	int i, j;

	swapchain->cmd_buffers = vk_alloc(alloc, (sizeof(VkCommandBuffer) * num_cmd_buffers), 8,
					  VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
	if (!swapchain->cmd_buffers)
		return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

	memset(swapchain->cmd_buffers, 0, sizeof(VkCommandBuffer) * num_cmd_buffers);
	memset(swapchain->cmd_pools, 0, sizeof(VkCommandPool) * num_pools);
	for (i = 0; i < num_pools; i++) {
		VkCommandPoolCreateInfo pool_create_info;

		pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		pool_create_info.pNext = NULL;
		pool_create_info.flags = 0;
		pool_create_info.queueFamilyIndex = i;

		result = radv_CreateCommandPool(radv_device_to_handle(device),
						&pool_create_info, alloc,
						&swapchain->cmd_pools[i]);
		if (result != VK_SUCCESS)
			goto fail;

		VkCommandBufferAllocateInfo cmd_buffer_info;
		cmd_buffer_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		cmd_buffer_info.pNext = NULL;
		cmd_buffer_info.commandPool = swapchain->cmd_pools[i];
		cmd_buffer_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		cmd_buffer_info.commandBufferCount = num_images;

		result = radv_AllocateCommandBuffers(radv_device_to_handle(device),
						     &cmd_buffer_info,
						     &swapchain->cmd_buffers[i * num_images]);
		if (result != VK_SUCCESS)
			goto fail;
		for (j = 0; j < num_images; j++) {
			VkImage image, linear_image;
			int idx = (i * num_images) + j;

			swapchain->get_image_and_linear(swapchain, j, &image, &linear_image);
			VkCommandBufferBeginInfo begin_info = {0};

			begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

			radv_BeginCommandBuffer(swapchain->cmd_buffers[idx], &begin_info);

			radv_blit_to_prime_linear(radv_cmd_buffer_from_handle(swapchain->cmd_buffers[idx]),
						  radv_image_from_handle(image),
						  radv_image_from_handle(linear_image));

			radv_EndCommandBuffer(swapchain->cmd_buffers[idx]);
		}
	}
	return VK_SUCCESS;
fail:
	radv_wsi_free_prime_command_buffers(device, swapchain);
	return result;
}

VkResult radv_CreateSwapchainKHR(
	VkDevice                                     _device,
	const VkSwapchainCreateInfoKHR*              pCreateInfo,
	const VkAllocationCallbacks*                 pAllocator,
	VkSwapchainKHR*                              pSwapchain)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	ICD_FROM_HANDLE(VkIcdSurfaceBase, surface, pCreateInfo->surface);
	struct wsi_interface *iface =
		device->physical_device->wsi_device.wsi[surface->platform];
	struct wsi_swapchain *swapchain;
	const VkAllocationCallbacks *alloc;
	if (pAllocator)
		alloc = pAllocator;
	else
		alloc = &device->alloc;
	VkResult result = iface->create_swapchain(surface, _device,
						  &device->physical_device->wsi_device,
						  device->physical_device->local_fd,
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

	if (swapchain->needs_linear_copy) {
		result = radv_wsi_create_prime_command_buffers(device, alloc,
							       swapchain);
		if (result != VK_SUCCESS)
			return result;
	}

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

	if (!_swapchain)
		return;

	if (pAllocator)
		alloc = pAllocator;
	else
		alloc = &device->alloc;

	for (unsigned i = 0; i < ARRAY_SIZE(swapchain->fences); i++) {
		if (swapchain->fences[i] != VK_NULL_HANDLE)
			radv_DestroyFence(_device, swapchain->fences[i], pAllocator);
	}

	if (swapchain->needs_linear_copy)
		radv_wsi_free_prime_command_buffers(device, swapchain);

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
	VkFence                                      _fence,
	uint32_t*                                    pImageIndex)
{
	RADV_FROM_HANDLE(wsi_swapchain, swapchain, _swapchain);
	RADV_FROM_HANDLE(radv_fence, fence, _fence);

	VkResult result = swapchain->acquire_next_image(swapchain, timeout, semaphore,
	                                                pImageIndex);

	if (fence && (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR)) {
		fence->submitted = true;
		fence->signalled = true;
	}
	return result;
}

VkResult radv_QueuePresentKHR(
	VkQueue                                  _queue,
	const VkPresentInfoKHR*                  pPresentInfo)
{
	RADV_FROM_HANDLE(radv_queue, queue, _queue);
	VkResult result = VK_SUCCESS;
	const VkPresentRegionsKHR *regions =
	         vk_find_struct_const(pPresentInfo->pNext, PRESENT_REGIONS_KHR);

	for (uint32_t i = 0; i < pPresentInfo->swapchainCount; i++) {
		RADV_FROM_HANDLE(wsi_swapchain, swapchain, pPresentInfo->pSwapchains[i]);
		struct radeon_winsys_cs *cs;
		const VkPresentRegionKHR *region = NULL;
		VkResult item_result;
		struct radv_winsys_sem_info sem_info;

		item_result = radv_alloc_sem_info(&sem_info,
						  pPresentInfo->waitSemaphoreCount,
						  pPresentInfo->pWaitSemaphores,
						  0,
						  NULL);
		if (pPresentInfo->pResults != NULL)
			pPresentInfo->pResults[i] = item_result;
		result = result == VK_SUCCESS ? item_result : result;
		if (item_result != VK_SUCCESS) {
			radv_free_sem_info(&sem_info);
			continue;
		}

		assert(radv_device_from_handle(swapchain->device) == queue->device);
		if (swapchain->fences[0] == VK_NULL_HANDLE) {
			item_result = radv_CreateFence(radv_device_to_handle(queue->device),
						  &(VkFenceCreateInfo) {
							  .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
								  .flags = 0,
								  }, &swapchain->alloc, &swapchain->fences[0]);
			if (pPresentInfo->pResults != NULL)
				pPresentInfo->pResults[i] = item_result;
			result = result == VK_SUCCESS ? item_result : result;
			if (item_result != VK_SUCCESS) {
				radv_free_sem_info(&sem_info);
				continue;
			}
		} else {
			radv_ResetFences(radv_device_to_handle(queue->device),
					 1, &swapchain->fences[0]);
		}

		if (swapchain->needs_linear_copy) {
			int idx = (queue->queue_family_index * swapchain->image_count) + pPresentInfo->pImageIndices[i];
			cs = radv_cmd_buffer_from_handle(swapchain->cmd_buffers[idx])->cs;
		} else
			cs = queue->device->empty_cs[queue->queue_family_index];
		RADV_FROM_HANDLE(radv_fence, fence, swapchain->fences[0]);
		struct radeon_winsys_fence *base_fence = fence->fence;
		struct radeon_winsys_ctx *ctx = queue->hw_ctx;

		queue->device->ws->cs_submit(ctx, queue->queue_idx,
					     &cs,
					     1, NULL, NULL,
					     &sem_info,
					     false, base_fence);
		fence->submitted = true;

		if (regions && regions->pRegions)
			region = &regions->pRegions[i];

		item_result = swapchain->queue_present(swapchain,
						  pPresentInfo->pImageIndices[i],
						  region);
		/* TODO: What if one of them returns OUT_OF_DATE? */
		if (pPresentInfo->pResults != NULL)
			pPresentInfo->pResults[i] = item_result;
		result = result == VK_SUCCESS ? item_result : result;
		if (item_result != VK_SUCCESS) {
			radv_free_sem_info(&sem_info);
			continue;
		}

		VkFence last = swapchain->fences[2];
		swapchain->fences[2] = swapchain->fences[1];
		swapchain->fences[1] = swapchain->fences[0];
		swapchain->fences[0] = last;

		if (last != VK_NULL_HANDLE) {
			radv_WaitForFences(radv_device_to_handle(queue->device),
					   1, &last, true, 1);
		}

		radv_free_sem_info(&sem_info);
	}

	return VK_SUCCESS;
}
