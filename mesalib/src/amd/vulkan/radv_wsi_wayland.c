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

#include <wayland-client.h>
#include <wayland-drm-client-protocol.h>

#include "radv_wsi.h"

#include "vk_format.h"
#include <util/hash_table.h>

#define MIN_NUM_IMAGES 2

struct wsi_wl_display {
	struct radv_physical_device                  *physical_device;
	struct wl_display *                          display;
	struct wl_drm *                              drm;

	/* Vector of VkFormats supported */
	struct radv_vector                            formats;

	uint32_t                                     capabilities;
};

struct wsi_wayland {
	struct radv_wsi_interface                     base;

	struct radv_physical_device *                 physical_device;

	pthread_mutex_t                              mutex;
	/* Hash table of wl_display -> wsi_wl_display mappings */
	struct hash_table *                          displays;
};

static void
wsi_wl_display_add_vk_format(struct wsi_wl_display *display, VkFormat format)
{
	/* Don't add a format that's already in the list */
	VkFormat *f;
	radv_vector_foreach(f, &display->formats)
		if (*f == format)
			return;

	/* Don't add formats that aren't renderable. */
	VkFormatProperties props;
	radv_GetPhysicalDeviceFormatProperties(
		radv_physical_device_to_handle(display->physical_device), format, &props);
	if (!(props.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT))
		return;

	f = radv_vector_add(&display->formats);
	if (f)
		*f = format;
}

static void
drm_handle_device(void *data, struct wl_drm *drm, const char *name)
{
	fprintf(stderr, "wl_drm.device(%s)\n", name);
}

static uint32_t
wl_drm_format_for_vk_format(VkFormat vk_format, bool alpha)
{
	switch (vk_format) {
		/* TODO: Figure out what all the formats mean and make this table
		 * correct.
		 */
#if 0
	case VK_FORMAT_R4G4B4A4_UNORM:
		return alpha ? WL_DRM_FORMAT_ABGR4444 : WL_DRM_FORMAT_XBGR4444;
	case VK_FORMAT_R5G6B5_UNORM:
		return WL_DRM_FORMAT_BGR565;
	case VK_FORMAT_R5G5B5A1_UNORM:
		return alpha ? WL_DRM_FORMAT_ABGR1555 : WL_DRM_FORMAT_XBGR1555;
	case VK_FORMAT_R8G8B8_UNORM:
		return WL_DRM_FORMAT_XBGR8888;
	case VK_FORMAT_R8G8B8A8_UNORM:
		return alpha ? WL_DRM_FORMAT_ABGR8888 : WL_DRM_FORMAT_XBGR8888;
	case VK_FORMAT_R10G10B10A2_UNORM:
		return alpha ? WL_DRM_FORMAT_ABGR2101010 : WL_DRM_FORMAT_XBGR2101010;
	case VK_FORMAT_B4G4R4A4_UNORM:
		return alpha ? WL_DRM_FORMAT_ARGB4444 : WL_DRM_FORMAT_XRGB4444;
	case VK_FORMAT_B5G6R5_UNORM:
		return WL_DRM_FORMAT_RGB565;
	case VK_FORMAT_B5G5R5A1_UNORM:
		return alpha ? WL_DRM_FORMAT_XRGB1555 : WL_DRM_FORMAT_XRGB1555;
#endif
	case VK_FORMAT_B8G8R8_SRGB:
		return WL_DRM_FORMAT_BGRX8888;
	case VK_FORMAT_B8G8R8A8_SRGB:
		return alpha ? WL_DRM_FORMAT_ARGB8888 : WL_DRM_FORMAT_XRGB8888;
#if 0
	case VK_FORMAT_B10G10R10A2_UNORM:
		return alpha ? WL_DRM_FORMAT_ARGB2101010 : WL_DRM_FORMAT_XRGB2101010;
#endif

	default:
		assert(!"Unsupported Vulkan format");
		return 0;
	}
}

static void
drm_handle_format(void *data, struct wl_drm *drm, uint32_t wl_format)
{
	struct wsi_wl_display *display = data;

	switch (wl_format) {
#if 0
	case WL_DRM_FORMAT_ABGR4444:
	case WL_DRM_FORMAT_XBGR4444:
		wsi_wl_display_add_vk_format(display, VK_FORMAT_R4G4B4A4_UNORM);
		break;
	case WL_DRM_FORMAT_BGR565:
		wsi_wl_display_add_vk_format(display, VK_FORMAT_R5G6B5_UNORM);
		break;
	case WL_DRM_FORMAT_ABGR1555:
	case WL_DRM_FORMAT_XBGR1555:
		wsi_wl_display_add_vk_format(display, VK_FORMAT_R5G5B5A1_UNORM);
		break;
	case WL_DRM_FORMAT_XBGR8888:
		wsi_wl_display_add_vk_format(display, VK_FORMAT_R8G8B8_UNORM);
		/* fallthrough */
	case WL_DRM_FORMAT_ABGR8888:
		wsi_wl_display_add_vk_format(display, VK_FORMAT_R8G8B8A8_UNORM);
		break;
	case WL_DRM_FORMAT_ABGR2101010:
	case WL_DRM_FORMAT_XBGR2101010:
		wsi_wl_display_add_vk_format(display, VK_FORMAT_R10G10B10A2_UNORM);
		break;
	case WL_DRM_FORMAT_ARGB4444:
	case WL_DRM_FORMAT_XRGB4444:
		wsi_wl_display_add_vk_format(display, VK_FORMAT_B4G4R4A4_UNORM);
		break;
	case WL_DRM_FORMAT_RGB565:
		wsi_wl_display_add_vk_format(display, VK_FORMAT_B5G6R5_UNORM);
		break;
	case WL_DRM_FORMAT_ARGB1555:
	case WL_DRM_FORMAT_XRGB1555:
		wsi_wl_display_add_vk_format(display, VK_FORMAT_B5G5R5A1_UNORM);
		break;
#endif
	case WL_DRM_FORMAT_XRGB8888:
		wsi_wl_display_add_vk_format(display, VK_FORMAT_B8G8R8_SRGB);
		/* fallthrough */
	case WL_DRM_FORMAT_ARGB8888:
		wsi_wl_display_add_vk_format(display, VK_FORMAT_B8G8R8A8_SRGB);
		break;
#if 0
	case WL_DRM_FORMAT_ARGB2101010:
	case WL_DRM_FORMAT_XRGB2101010:
		wsi_wl_display_add_vk_format(display, VK_FORMAT_B10G10R10A2_UNORM);
		break;
#endif
	}
}

static void
drm_handle_authenticated(void *data, struct wl_drm *drm)
{
}

static void
drm_handle_capabilities(void *data, struct wl_drm *drm, uint32_t capabilities)
{
	struct wsi_wl_display *display = data;

	display->capabilities = capabilities;
}

static const struct wl_drm_listener drm_listener = {
	drm_handle_device,
	drm_handle_format,
	drm_handle_authenticated,
	drm_handle_capabilities,
};

static void
registry_handle_global(void *data, struct wl_registry *registry,
                       uint32_t name, const char *interface, uint32_t version)
{
	struct wsi_wl_display *display = data;

	if (strcmp(interface, "wl_drm") == 0) {
		assert(display->drm == NULL);

		assert(version >= 2);
		display->drm = wl_registry_bind(registry, name, &wl_drm_interface, 2);

		if (display->drm)
			wl_drm_add_listener(display->drm, &drm_listener, display);
	}
}

static void
registry_handle_global_remove(void *data, struct wl_registry *registry,
                              uint32_t name)
{ /* No-op */ }

static const struct wl_registry_listener registry_listener = {
	registry_handle_global,
	registry_handle_global_remove
};

static void
wsi_wl_display_destroy(struct wsi_wayland *wsi, struct wsi_wl_display *display)
{
	radv_vector_finish(&display->formats);
	if (display->drm)
		wl_drm_destroy(display->drm);
	radv_free(&wsi->physical_device->instance->alloc, display);
}

static struct wsi_wl_display *
wsi_wl_display_create(struct wsi_wayland *wsi, struct wl_display *wl_display)
{
	struct wsi_wl_display *display =
		radv_alloc(&wsi->physical_device->instance->alloc, sizeof(*display), 8,
			   VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
	if (!display)
		return NULL;

	memset(display, 0, sizeof(*display));

	display->display = wl_display;
	display->physical_device = wsi->physical_device;

	if (!radv_vector_init(&display->formats, sizeof(VkFormat), 8))
		goto fail;

	struct wl_registry *registry = wl_display_get_registry(wl_display);
	if (!registry)
		return NULL;

	wl_registry_add_listener(registry, &registry_listener, display);

	/* Round-rip to get the wl_drm global */
	wl_display_roundtrip(wl_display);

	if (!display->drm)
		goto fail;

	/* Round-rip to get wl_drm formats and capabilities */
	wl_display_roundtrip(wl_display);

	/* We need prime support */
	if (!(display->capabilities & WL_DRM_CAPABILITY_PRIME))
		goto fail;

	/* We don't need this anymore */
	wl_registry_destroy(registry);

	return display;

fail:
	if (registry)
		wl_registry_destroy(registry);

	wsi_wl_display_destroy(wsi, display);
	return NULL;
}

static struct wsi_wl_display *
wsi_wl_get_display(struct radv_physical_device *device,
                   struct wl_display *wl_display)
{
	struct wsi_wayland *wsi =
		(struct wsi_wayland *)device->wsi[VK_ICD_WSI_PLATFORM_WAYLAND];

	pthread_mutex_lock(&wsi->mutex);

	struct hash_entry *entry = _mesa_hash_table_search(wsi->displays,
							   wl_display);
	if (!entry) {
		/* We're about to make a bunch of blocking calls.  Let's drop the
		 * mutex for now so we don't block up too badly.
		 */
		pthread_mutex_unlock(&wsi->mutex);

		struct wsi_wl_display *display = wsi_wl_display_create(wsi, wl_display);

		pthread_mutex_lock(&wsi->mutex);

		entry = _mesa_hash_table_search(wsi->displays, wl_display);
		if (entry) {
			/* Oops, someone raced us to it */
			wsi_wl_display_destroy(wsi, display);
		} else {
			entry = _mesa_hash_table_insert(wsi->displays, wl_display, display);
		}
	}

	pthread_mutex_unlock(&wsi->mutex);

	return entry->data;
}

VkBool32 radv_GetPhysicalDeviceWaylandPresentationSupportKHR(
	VkPhysicalDevice                            physicalDevice,
	uint32_t                                    queueFamilyIndex,
	struct wl_display*                          display)
{
	RADV_FROM_HANDLE(radv_physical_device, physical_device, physicalDevice);

	return wsi_wl_get_display(physical_device, display) != NULL;
}

static VkResult
wsi_wl_surface_get_support(VkIcdSurfaceBase *surface,
                           struct radv_physical_device *device,
                           uint32_t queueFamilyIndex,
                           VkBool32* pSupported)
{
	*pSupported = true;

	return VK_SUCCESS;
}

static const VkPresentModeKHR present_modes[] = {
	VK_PRESENT_MODE_MAILBOX_KHR,
	VK_PRESENT_MODE_FIFO_KHR,
};

static VkResult
wsi_wl_surface_get_capabilities(VkIcdSurfaceBase *surface,
                                struct radv_physical_device *device,
                                VkSurfaceCapabilitiesKHR* caps)
{
	caps->minImageCount = MIN_NUM_IMAGES;
	caps->maxImageCount = 4;
	caps->currentExtent = (VkExtent2D) { -1, -1 };
	caps->minImageExtent = (VkExtent2D) { 1, 1 };
	caps->maxImageExtent = (VkExtent2D) { INT16_MAX, INT16_MAX };
	caps->supportedTransforms = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	caps->currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	caps->maxImageArrayLayers = 1;

	caps->supportedCompositeAlpha =
		VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR |
		VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;

	caps->supportedUsageFlags =
		VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
		VK_IMAGE_USAGE_SAMPLED_BIT |
		VK_IMAGE_USAGE_TRANSFER_DST_BIT |
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

	return VK_SUCCESS;
}

static VkResult
wsi_wl_surface_get_formats(VkIcdSurfaceBase *icd_surface,
                           struct radv_physical_device *device,
                           uint32_t* pSurfaceFormatCount,
                           VkSurfaceFormatKHR* pSurfaceFormats)
{
	VkIcdSurfaceWayland *surface = (VkIcdSurfaceWayland *)icd_surface;
	struct wsi_wl_display *display =
		wsi_wl_get_display(device, surface->display);

	uint32_t count = radv_vector_length(&display->formats);

	if (pSurfaceFormats == NULL) {
		*pSurfaceFormatCount = count;
		return VK_SUCCESS;
	}

	assert(*pSurfaceFormatCount >= count);
	*pSurfaceFormatCount = count;

	VkFormat *f;
	radv_vector_foreach(f, &display->formats) {
		*(pSurfaceFormats++) = (VkSurfaceFormatKHR) {
			.format = *f,
			/* TODO: We should get this from the compositor somehow */
			.colorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR,
		};
	}

	return VK_SUCCESS;
}

static VkResult
wsi_wl_surface_get_present_modes(VkIcdSurfaceBase *surface,
                                 struct radv_physical_device *device,
                                 uint32_t* pPresentModeCount,
                                 VkPresentModeKHR* pPresentModes)
{
	if (pPresentModes == NULL) {
		*pPresentModeCount = ARRAY_SIZE(present_modes);
		return VK_SUCCESS;
	}

	assert(*pPresentModeCount >= ARRAY_SIZE(present_modes));
	typed_memcpy(pPresentModes, present_modes, *pPresentModeCount);
	*pPresentModeCount = ARRAY_SIZE(present_modes);

	return VK_SUCCESS;
}

static VkResult
wsi_wl_surface_create_swapchain(VkIcdSurfaceBase *surface,
                                struct radv_device *device,
                                const VkSwapchainCreateInfoKHR* pCreateInfo,
                                const VkAllocationCallbacks* pAllocator,
                                struct radv_swapchain **swapchain);

VkResult radv_CreateWaylandSurfaceKHR(
	VkInstance                                  _instance,
	const VkWaylandSurfaceCreateInfoKHR*        pCreateInfo,
	const VkAllocationCallbacks*                pAllocator,
	VkSurfaceKHR*                               pSurface)
{
	RADV_FROM_HANDLE(radv_instance, instance, _instance);

	assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR);

	VkIcdSurfaceWayland *surface;

	surface = radv_alloc2(&instance->alloc, pAllocator, sizeof *surface, 8,
			      VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
	if (surface == NULL)
		return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

	surface->base.platform = VK_ICD_WSI_PLATFORM_WAYLAND;
	surface->display = pCreateInfo->display;
	surface->surface = pCreateInfo->surface;

	*pSurface = _VkIcdSurfaceBase_to_handle(&surface->base);

	return VK_SUCCESS;
}

struct wsi_wl_image {
	struct radv_image *                           image;
	struct radv_device_memory *                   memory;
	struct wl_buffer *                           buffer;
	bool                                         busy;
};

struct wsi_wl_swapchain {
	struct radv_swapchain                        base;

	struct wsi_wl_display *                      display;
	struct wl_event_queue *                      queue;
	struct wl_surface *                          surface;

	VkExtent2D                                   extent;
	VkFormat                                     vk_format;
	uint32_t                                     drm_format;

	VkPresentModeKHR                             present_mode;
	bool                                         fifo_ready;

	uint32_t                                     image_count;
	struct wsi_wl_image                          images[0];
};

static VkResult
wsi_wl_swapchain_get_images(struct radv_swapchain *radv_chain,
                            uint32_t *pCount, VkImage *pSwapchainImages)
{
	struct wsi_wl_swapchain *chain = (struct wsi_wl_swapchain *)radv_chain;

	if (pSwapchainImages == NULL) {
		*pCount = chain->image_count;
		return VK_SUCCESS;
	}

	assert(chain->image_count <= *pCount);
	for (uint32_t i = 0; i < chain->image_count; i++)
		pSwapchainImages[i] = radv_image_to_handle(chain->images[i].image);

	*pCount = chain->image_count;

	return VK_SUCCESS;
}

static VkResult
wsi_wl_swapchain_acquire_next_image(struct radv_swapchain *radv_chain,
                                    uint64_t timeout,
                                    VkSemaphore semaphore,
                                    uint32_t *image_index)
{
	struct wsi_wl_swapchain *chain = (struct wsi_wl_swapchain *)radv_chain;

	int ret = wl_display_dispatch_queue_pending(chain->display->display,
						    chain->queue);
	/* XXX: I'm not sure if out-of-date is the right error here.  If
	 * wl_display_dispatch_queue_pending fails it most likely means we got
	 * kicked by the server so this seems more-or-less correct.
	 */
	if (ret < 0)
		return vk_error(VK_ERROR_OUT_OF_DATE_KHR);

	while (1) {
		for (uint32_t i = 0; i < chain->image_count; i++) {
			if (!chain->images[i].busy) {
				/* We found a non-busy image */
				*image_index = i;
				return VK_SUCCESS;
			}
		}

		/* This time we do a blocking dispatch because we can't go
		 * anywhere until we get an event.
		 */
		int ret = wl_display_roundtrip_queue(chain->display->display,
						     chain->queue);
		if (ret < 0)
			return vk_error(VK_ERROR_OUT_OF_DATE_KHR);
	}
}

static void
frame_handle_done(void *data, struct wl_callback *callback, uint32_t serial)
{
	struct wsi_wl_swapchain *chain = data;

	chain->fifo_ready = true;

	wl_callback_destroy(callback);
}

static const struct wl_callback_listener frame_listener = {
	frame_handle_done,
};

static VkResult
wsi_wl_swapchain_queue_present(struct radv_swapchain *radv_chain,
                               struct radv_queue *queue,
                               uint32_t image_index)
{
	struct wsi_wl_swapchain *chain = (struct wsi_wl_swapchain *)radv_chain;

	if (chain->present_mode == VK_PRESENT_MODE_FIFO_KHR) {
		while (!chain->fifo_ready) {
			int ret = wl_display_dispatch_queue(chain->display->display,
							    chain->queue);
			if (ret < 0)
				return vk_error(VK_ERROR_OUT_OF_DATE_KHR);
		}
	}

	assert(image_index < chain->image_count);
	wl_surface_attach(chain->surface, chain->images[image_index].buffer, 0, 0);
	wl_surface_damage(chain->surface, 0, 0, INT32_MAX, INT32_MAX);

	if (chain->present_mode == VK_PRESENT_MODE_FIFO_KHR) {
		struct wl_callback *frame = wl_surface_frame(chain->surface);
		wl_proxy_set_queue((struct wl_proxy *)frame, chain->queue);
		wl_callback_add_listener(frame, &frame_listener, chain);
		chain->fifo_ready = false;
	}

	chain->images[image_index].busy = true;
	wl_surface_commit(chain->surface);
	wl_display_flush(chain->display->display);

	return VK_SUCCESS;
}

static void
wsi_wl_image_finish(struct wsi_wl_swapchain *chain, struct wsi_wl_image *image,
                    const VkAllocationCallbacks* pAllocator)
{
	VkDevice vk_device = radv_device_to_handle(chain->base.device);
	radv_FreeMemory(vk_device, radv_device_memory_to_handle(image->memory),
			pAllocator);
	radv_DestroyImage(vk_device, radv_image_to_handle(image->image),
			  pAllocator);
}

static void
buffer_handle_release(void *data, struct wl_buffer *buffer)
{
	struct wsi_wl_image *image = data;

	assert(image->buffer == buffer);

	image->busy = false;
}

static const struct wl_buffer_listener buffer_listener = {
	buffer_handle_release,
};

static VkResult
wsi_wl_image_init(struct wsi_wl_swapchain *chain,
                  struct wsi_wl_image *image,
                  const VkSwapchainCreateInfoKHR *pCreateInfo,
                  const VkAllocationCallbacks* pAllocator)
{
	VkDevice vk_device = radv_device_to_handle(chain->base.device);
	VkResult result;
	bool bret;
	VkImage vk_image;
	struct radeon_surf *surface;
	int fd;
	result = radv_image_create(vk_device,
				   &(struct radv_image_create_info) {
					   .vk_info =
						   &(VkImageCreateInfo) {
						   .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
						   .imageType = VK_IMAGE_TYPE_2D,
						   .format = chain->vk_format,
						   .extent = {
							   .width = chain->extent.width,
							   .height = chain->extent.height,
							   .depth = 1
						   },
						   .mipLevels = 1,
						   .arrayLayers = 1,
						   .samples = 1,
						   /* FIXME: Need a way to use X tiling to allow scanout */
						   .tiling = VK_IMAGE_TILING_OPTIMAL,
						   .usage = (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
							     pCreateInfo->imageUsage),
						   .flags = 0,
					   },
						   .scanout = true},
				   pAllocator,
				   &vk_image);

	if (result != VK_SUCCESS)
		return result;

	image->image = radv_image_from_handle(vk_image);
	assert(vk_format_is_color(image->image->vk_format));

	VkDeviceMemory vk_memory;
	result = radv_AllocateMemory(vk_device,
				     &(VkMemoryAllocateInfo) {
					     .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
						     .allocationSize = image->image->size,
						     .memoryTypeIndex = 0,
						     },
				     pAllocator,
				     &vk_memory);

	if (result != VK_SUCCESS)
		goto fail_image;

	image->memory = radv_device_memory_from_handle(vk_memory);

	result = radv_BindImageMemory(vk_device, vk_image, vk_memory, 0);

	if (result != VK_SUCCESS)
		goto fail_mem;

	bret = chain->base.device->ws->buffer_get_fd(chain->base.device->ws,
						     image->memory->bo, &fd);
	if (bret == false)
		goto fail_mem;

	{
		struct radeon_bo_metadata metadata;
		radv_init_metadata(chain->base.device, image->image, &metadata);
		chain->base.device->ws->buffer_set_metadata(image->memory->bo, &metadata);
	}
	surface = &image->image->surface;

	image->buffer = wl_drm_create_prime_buffer(chain->display->drm,
						   fd, /* name */
						   chain->extent.width,
						   chain->extent.height,
						   chain->drm_format,
						   surface->level[0].offset,
						   surface->level[0].pitch_bytes,
						   0, 0, 0, 0 /* unused */);
	wl_display_roundtrip(chain->display->display);
	close(fd);

	wl_proxy_set_queue((struct wl_proxy *)image->buffer, chain->queue);
	wl_buffer_add_listener(image->buffer, &buffer_listener, image);

	return VK_SUCCESS;

fail_mem:
	radv_FreeMemory(vk_device, vk_memory, pAllocator);
fail_image:
	radv_DestroyImage(vk_device, vk_image, pAllocator);

	return result;
}

static VkResult
wsi_wl_swapchain_destroy(struct radv_swapchain *radv_chain,
                         const VkAllocationCallbacks *pAllocator)
{
	struct wsi_wl_swapchain *chain = (struct wsi_wl_swapchain *)radv_chain;

	for (uint32_t i = 0; i < chain->image_count; i++) {
		if (chain->images[i].buffer)
			wsi_wl_image_finish(chain, &chain->images[i], pAllocator);
	}

	radv_free2(&chain->base.device->alloc, pAllocator, chain);

	return VK_SUCCESS;
}

static VkResult
wsi_wl_surface_create_swapchain(VkIcdSurfaceBase *icd_surface,
                                struct radv_device *device,
                                const VkSwapchainCreateInfoKHR* pCreateInfo,
                                const VkAllocationCallbacks* pAllocator,
                                struct radv_swapchain **swapchain_out)
{
	VkIcdSurfaceWayland *surface = (VkIcdSurfaceWayland *)icd_surface;
	struct wsi_wl_swapchain *chain;
	VkResult result;

	assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR);

	int num_images = pCreateInfo->minImageCount;

	assert(num_images >= MIN_NUM_IMAGES);

	/* For true mailbox mode, we need at least 4 images:
	 *  1) One to scan out from
	 *  2) One to have queued for scan-out
	 *  3) One to be currently held by the Wayland compositor
	 *  4) One to render to
	 */
	if (pCreateInfo->presentMode == VK_PRESENT_MODE_MAILBOX_KHR)
		num_images = MAX2(num_images, 4);

	size_t size = sizeof(*chain) + num_images * sizeof(chain->images[0]);
	chain = radv_alloc2(&device->alloc, pAllocator, size, 8,
			    VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
	if (chain == NULL)
		return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

	chain->base.device = device;
	chain->base.destroy = wsi_wl_swapchain_destroy;
	chain->base.get_images = wsi_wl_swapchain_get_images;
	chain->base.acquire_next_image = wsi_wl_swapchain_acquire_next_image;
	chain->base.queue_present = wsi_wl_swapchain_queue_present;

	chain->surface = surface->surface;
	chain->extent = pCreateInfo->imageExtent;
	chain->vk_format = pCreateInfo->imageFormat;
	chain->drm_format = wl_drm_format_for_vk_format(chain->vk_format, false);

	chain->present_mode = pCreateInfo->presentMode;
	chain->fifo_ready = true;

	chain->image_count = num_images;

	/* Mark a bunch of stuff as NULL.  This way we can just call
	 * destroy_swapchain for cleanup.
	 */
	for (uint32_t i = 0; i < chain->image_count; i++)
		chain->images[i].buffer = NULL;
	chain->queue = NULL;

	chain->display = wsi_wl_get_display(&device->instance->physicalDevice,
					    surface->display);
	if (!chain->display) {
		result = vk_error(VK_ERROR_INITIALIZATION_FAILED);
		goto fail;
	}

	chain->queue = wl_display_create_queue(chain->display->display);
	if (!chain->queue) {
		result = vk_error(VK_ERROR_INITIALIZATION_FAILED);
		goto fail;
	}

	for (uint32_t i = 0; i < chain->image_count; i++) {
		result = wsi_wl_image_init(chain, &chain->images[i],
					   pCreateInfo, pAllocator);
		if (result != VK_SUCCESS)
			goto fail;
		chain->images[i].busy = false;
	}

	*swapchain_out = &chain->base;

	return VK_SUCCESS;

fail:
	wsi_wl_swapchain_destroy(&chain->base, pAllocator);

	return result;
}

VkResult
radv_wl_init_wsi(struct radv_physical_device *device)
{
	struct wsi_wayland *wsi;
	VkResult result;

	wsi = radv_alloc(&device->instance->alloc, sizeof(*wsi), 8,
			 VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
	if (!wsi) {
		result = vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);
		goto fail;
	}

	wsi->physical_device = device;

	int ret = pthread_mutex_init(&wsi->mutex, NULL);
	if (ret != 0) {
		if (ret == ENOMEM) {
			result = vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);
		} else {
			/* FINISHME: Choose a better error. */
			result = vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);
		}

		goto fail_alloc;
	}

	wsi->displays = _mesa_hash_table_create(NULL, _mesa_hash_pointer,
						_mesa_key_pointer_equal);
	if (!wsi->displays) {
		result = vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);
		goto fail_mutex;
	}

	wsi->base.get_support = wsi_wl_surface_get_support;
	wsi->base.get_capabilities = wsi_wl_surface_get_capabilities;
	wsi->base.get_formats = wsi_wl_surface_get_formats;
	wsi->base.get_present_modes = wsi_wl_surface_get_present_modes;
	wsi->base.create_swapchain = wsi_wl_surface_create_swapchain;

	device->wsi[VK_ICD_WSI_PLATFORM_WAYLAND] = &wsi->base;

	return VK_SUCCESS;

fail_mutex:
	pthread_mutex_destroy(&wsi->mutex);

fail_alloc:
	radv_free(&device->instance->alloc, wsi);
fail:
	device->wsi[VK_ICD_WSI_PLATFORM_WAYLAND] = NULL;

	return result;
}

void
radv_wl_finish_wsi(struct radv_physical_device *device)
{
	struct wsi_wayland *wsi =
		(struct wsi_wayland *)device->wsi[VK_ICD_WSI_PLATFORM_WAYLAND];

	if (wsi) {
		_mesa_hash_table_destroy(wsi->displays, NULL);

		pthread_mutex_destroy(&wsi->mutex);

		radv_free(&device->instance->alloc, wsi);
	}
}
