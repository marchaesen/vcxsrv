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

#ifndef RADV_WSI_H
#define RADV_WSI_H

#include "radv_private.h"

struct radv_swapchain;
struct radv_wsi_interface {
	VkResult (*get_support)(VkIcdSurfaceBase *surface,
				struct radv_physical_device *device,
				uint32_t queueFamilyIndex,
				VkBool32* pSupported);
	VkResult (*get_capabilities)(VkIcdSurfaceBase *surface,
				     struct radv_physical_device *device,
				     VkSurfaceCapabilitiesKHR* pSurfaceCapabilities);
	VkResult (*get_formats)(VkIcdSurfaceBase *surface,
				struct radv_physical_device *device,
				uint32_t* pSurfaceFormatCount,
				VkSurfaceFormatKHR* pSurfaceFormats);
	VkResult (*get_present_modes)(VkIcdSurfaceBase *surface,
				      struct radv_physical_device *device,
				      uint32_t* pPresentModeCount,
				      VkPresentModeKHR* pPresentModes);
	VkResult (*create_swapchain)(VkIcdSurfaceBase *surface,
				     struct radv_device *device,
				     const VkSwapchainCreateInfoKHR* pCreateInfo,
				     const VkAllocationCallbacks* pAllocator,
				     struct radv_swapchain **swapchain);
};

struct radv_swapchain {
	struct radv_device *device;

	VkAllocationCallbacks alloc;

	VkFence fences[3];

	VkResult (*destroy)(struct radv_swapchain *swapchain,
			    const VkAllocationCallbacks *pAllocator);
	VkResult (*get_images)(struct radv_swapchain *swapchain,
			       uint32_t *pCount, VkImage *pSwapchainImages);
	VkResult (*acquire_next_image)(struct radv_swapchain *swap_chain,
				       uint64_t timeout, VkSemaphore semaphore,
				       uint32_t *image_index);
	VkResult (*queue_present)(struct radv_swapchain *swap_chain,
				  struct radv_queue *queue,
				  uint32_t image_index);
};

RADV_DEFINE_NONDISP_HANDLE_CASTS(_VkIcdSurfaceBase, VkSurfaceKHR)
RADV_DEFINE_NONDISP_HANDLE_CASTS(radv_swapchain, VkSwapchainKHR)

VkResult radv_x11_init_wsi(struct radv_physical_device *physical_device);
void radv_x11_finish_wsi(struct radv_physical_device *physical_device);
VkResult radv_wl_init_wsi(struct radv_physical_device *physical_device);
void radv_wl_finish_wsi(struct radv_physical_device *physical_device);

#endif /* RADV_WSI_H */
