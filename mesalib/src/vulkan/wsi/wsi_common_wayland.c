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

#include <wayland-client.h>

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>

#include <drm_fourcc.h>

#include "vk_util.h"
#include "wsi_common_private.h"
#include "wsi_common_wayland.h"
#include "wayland-drm-client-protocol.h"
#include "linux-dmabuf-unstable-v1-client-protocol.h"

#include <util/hash_table.h>
#include <util/u_vector.h>

#define typed_memcpy(dest, src, count) ({ \
   STATIC_ASSERT(sizeof(*src) == sizeof(*dest)); \
   memcpy((dest), (src), (count) * sizeof(*(src))); \
})

struct wsi_wayland;

struct wsi_wl_display {
   /* The real wl_display */
   struct wl_display *                          wl_display;
   /* Actually a proxy wrapper around the event queue */
   struct wl_display *                          wl_display_wrapper;
   struct wl_event_queue *                      queue;
   struct wl_drm *                              drm;
   struct zwp_linux_dmabuf_v1 *                 dmabuf;

   struct wsi_wayland *wsi_wl;
   /* Vector of VkFormats supported */
   struct u_vector                            formats;

   struct {
      struct u_vector                           argb8888;
      struct u_vector                           xrgb8888;
   } modifiers;

   uint32_t                                     capabilities;

   /* Only used for displays created by wsi_wl_display_create */
   uint32_t                                     refcount;
};

struct wsi_wayland {
   struct wsi_interface                     base;

   struct wsi_device *wsi;

   const VkAllocationCallbacks *alloc;
   VkPhysicalDevice physical_device;
};

static void
wsi_wl_display_add_vk_format(struct wsi_wl_display *display, VkFormat format)
{
   /* Don't add a format that's already in the list */
   VkFormat *f;
   u_vector_foreach(f, &display->formats)
      if (*f == format)
         return;

   /* Don't add formats that aren't renderable. */
   VkFormatProperties props;

   display->wsi_wl->wsi->GetPhysicalDeviceFormatProperties(display->wsi_wl->physical_device,
                                                           format, &props);
   if (!(props.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT))
      return;

   f = u_vector_add(&display->formats);
   if (f)
      *f = format;
}

static void
drm_handle_device(void *data, struct wl_drm *drm, const char *name)
{
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
   case VK_FORMAT_B8G8R8_UNORM:
   case VK_FORMAT_B8G8R8_SRGB:
      return WL_DRM_FORMAT_BGRX8888;
   case VK_FORMAT_B8G8R8A8_UNORM:
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
   if (display->formats.element_size == 0)
      return;

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
      wsi_wl_display_add_vk_format(display, VK_FORMAT_B8G8R8_UNORM);
      /* fallthrough */
   case WL_DRM_FORMAT_ARGB8888:
      wsi_wl_display_add_vk_format(display, VK_FORMAT_B8G8R8A8_SRGB);
      wsi_wl_display_add_vk_format(display, VK_FORMAT_B8G8R8A8_UNORM);
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
dmabuf_handle_format(void *data, struct zwp_linux_dmabuf_v1 *dmabuf,
                     uint32_t format)
{
   /* Formats are implicitly advertised by the modifier event, so we ignore
    * them here. */
}

static void
dmabuf_handle_modifier(void *data, struct zwp_linux_dmabuf_v1 *dmabuf,
                       uint32_t format, uint32_t modifier_hi,
                       uint32_t modifier_lo)
{
   struct wsi_wl_display *display = data;
   uint64_t *mod = NULL;

   /* If we're not fetching formats, don't fetch modifiers either. */
   if (display->formats.element_size == 0)
      return;

   if (modifier_hi == (DRM_FORMAT_MOD_INVALID >> 32) &&
       modifier_lo == (DRM_FORMAT_MOD_INVALID & 0xffffffff))
      return;

   switch (format) {
   case WL_DRM_FORMAT_ARGB8888:
      mod = u_vector_add(&display->modifiers.argb8888);
      break;
   case WL_DRM_FORMAT_XRGB8888:
      mod = u_vector_add(&display->modifiers.xrgb8888);
      break;
   default:
      break;
   }

   if (!mod)
      return;

   *mod = (uint64_t) modifier_hi << 32;
   *mod |= (uint64_t) (modifier_lo & 0xffffffff);
}

static const struct zwp_linux_dmabuf_v1_listener dmabuf_listener = {
   dmabuf_handle_format,
   dmabuf_handle_modifier,
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
   } else if (strcmp(interface, "zwp_linux_dmabuf_v1") == 0 && version >= 3) {
      display->dmabuf =
         wl_registry_bind(registry, name, &zwp_linux_dmabuf_v1_interface, 3);
      zwp_linux_dmabuf_v1_add_listener(display->dmabuf, &dmabuf_listener,
                                       display);
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
wsi_wl_display_finish(struct wsi_wl_display *display)
{
   assert(display->refcount == 0);

   u_vector_finish(&display->formats);
   if (display->drm)
      wl_drm_destroy(display->drm);
   if (display->wl_display_wrapper)
      wl_proxy_wrapper_destroy(display->wl_display_wrapper);
   if (display->queue)
      wl_event_queue_destroy(display->queue);
}

static VkResult
wsi_wl_display_init(struct wsi_wayland *wsi_wl,
                    struct wsi_wl_display *display,
                    struct wl_display *wl_display,
                    bool get_format_list)
{
   VkResult result = VK_SUCCESS;
   memset(display, 0, sizeof(*display));

   display->wsi_wl = wsi_wl;
   display->wl_display = wl_display;

   if (get_format_list) {
      if (!u_vector_init(&display->formats, sizeof(VkFormat), 8) ||
          !u_vector_init(&display->modifiers.argb8888, sizeof(uint64_t), 32) ||
          !u_vector_init(&display->modifiers.xrgb8888, sizeof(uint64_t), 32)) {
         result = VK_ERROR_OUT_OF_HOST_MEMORY;
         goto fail;
      }
   }

   display->queue = wl_display_create_queue(wl_display);
   if (!display->queue) {
      result = VK_ERROR_OUT_OF_HOST_MEMORY;
      goto fail;
   }

   display->wl_display_wrapper = wl_proxy_create_wrapper(wl_display);
   if (!display->wl_display_wrapper) {
      result = VK_ERROR_OUT_OF_HOST_MEMORY;
      goto fail;
   }

   wl_proxy_set_queue((struct wl_proxy *) display->wl_display_wrapper,
                      display->queue);

   struct wl_registry *registry =
      wl_display_get_registry(display->wl_display_wrapper);
   if (!registry) {
      result = VK_ERROR_OUT_OF_HOST_MEMORY;
      goto fail;
   }

   wl_registry_add_listener(registry, &registry_listener, display);

   /* Round-trip to get the wl_drm global */
   wl_display_roundtrip_queue(display->wl_display, display->queue);

   if (!display->drm) {
      result = VK_ERROR_SURFACE_LOST_KHR;
      goto fail_registry;
   }

   /* Round-trip to get wl_drm formats and capabilities */
   wl_display_roundtrip_queue(display->wl_display, display->queue);

   /* We need prime support */
   if (!(display->capabilities & WL_DRM_CAPABILITY_PRIME)) {
      result = VK_ERROR_SURFACE_LOST_KHR;
      goto fail_registry;
   }

   /* We don't need this anymore */
   wl_registry_destroy(registry);

   display->refcount = 0;

   return VK_SUCCESS;

fail_registry:
   if (registry)
      wl_registry_destroy(registry);

fail:
   wsi_wl_display_finish(display);
   return result;
}

static VkResult
wsi_wl_display_create(struct wsi_wayland *wsi, struct wl_display *wl_display,
                      struct wsi_wl_display **display_out)
{
   struct wsi_wl_display *display =
      vk_alloc(wsi->alloc, sizeof(*display), 8,
               VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
   if (!display)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   VkResult result = wsi_wl_display_init(wsi, display, wl_display, true);
   if (result != VK_SUCCESS) {
      vk_free(wsi->alloc, display);
      return result;
   }

   display->refcount++;
   *display_out = display;

   return result;
}

static struct wsi_wl_display *
wsi_wl_display_ref(struct wsi_wl_display *display)
{
   display->refcount++;
   return display;
}

static void
wsi_wl_display_unref(struct wsi_wl_display *display)
{
   if (display->refcount-- > 1)
      return;

   struct wsi_wayland *wsi = display->wsi_wl;
   wsi_wl_display_finish(display);
   vk_free(wsi->alloc, display);
}

VkBool32
wsi_wl_get_presentation_support(struct wsi_device *wsi_device,
				struct wl_display *wl_display)
{
   struct wsi_wayland *wsi =
      (struct wsi_wayland *)wsi_device->wsi[VK_ICD_WSI_PLATFORM_WAYLAND];

   struct wsi_wl_display display;
   int ret = wsi_wl_display_init(wsi, &display, wl_display, false);
   wsi_wl_display_finish(&display);

   return ret == 0;
}

static VkResult
wsi_wl_surface_get_support(VkIcdSurfaceBase *surface,
                           struct wsi_device *wsi_device,
                           const VkAllocationCallbacks *alloc,
                           uint32_t queueFamilyIndex,
                           int local_fd,
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
                                VkSurfaceCapabilitiesKHR* caps)
{
   /* For true mailbox mode, we need at least 4 images:
    *  1) One to scan out from
    *  2) One to have queued for scan-out
    *  3) One to be currently held by the Wayland compositor
    *  4) One to render to
    */
   caps->minImageCount = 4;
   /* There is no real maximum */
   caps->maxImageCount = 0;

   caps->currentExtent = (VkExtent2D) { -1, -1 };
   caps->minImageExtent = (VkExtent2D) { 1, 1 };
   /* This is the maximum supported size on Intel */
   caps->maxImageExtent = (VkExtent2D) { 1 << 14, 1 << 14 };
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
wsi_wl_surface_get_capabilities2(VkIcdSurfaceBase *surface,
                                 const void *info_next,
                                 VkSurfaceCapabilities2KHR* caps)
{
   assert(caps->sType == VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR);

   return wsi_wl_surface_get_capabilities(surface, &caps->surfaceCapabilities);
}

static VkResult
wsi_wl_surface_get_formats(VkIcdSurfaceBase *icd_surface,
			   struct wsi_device *wsi_device,
                           uint32_t* pSurfaceFormatCount,
                           VkSurfaceFormatKHR* pSurfaceFormats)
{
   VkIcdSurfaceWayland *surface = (VkIcdSurfaceWayland *)icd_surface;
   struct wsi_wayland *wsi =
      (struct wsi_wayland *)wsi_device->wsi[VK_ICD_WSI_PLATFORM_WAYLAND];

   struct wsi_wl_display display;
   if (wsi_wl_display_init(wsi, &display, surface->display, true))
      return VK_ERROR_SURFACE_LOST_KHR;

   VK_OUTARRAY_MAKE(out, pSurfaceFormats, pSurfaceFormatCount);

   VkFormat *disp_fmt;
   u_vector_foreach(disp_fmt, &display.formats) {
      vk_outarray_append(&out, out_fmt) {
         out_fmt->format = *disp_fmt;
         out_fmt->colorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
      }
   }

   wsi_wl_display_finish(&display);

   return vk_outarray_status(&out);
}

static VkResult
wsi_wl_surface_get_formats2(VkIcdSurfaceBase *icd_surface,
			    struct wsi_device *wsi_device,
                            const void *info_next,
                            uint32_t* pSurfaceFormatCount,
                            VkSurfaceFormat2KHR* pSurfaceFormats)
{
   VkIcdSurfaceWayland *surface = (VkIcdSurfaceWayland *)icd_surface;
   struct wsi_wayland *wsi =
      (struct wsi_wayland *)wsi_device->wsi[VK_ICD_WSI_PLATFORM_WAYLAND];

   struct wsi_wl_display display;
   if (wsi_wl_display_init(wsi, &display, surface->display, true))
      return VK_ERROR_SURFACE_LOST_KHR;

   VK_OUTARRAY_MAKE(out, pSurfaceFormats, pSurfaceFormatCount);

   VkFormat *disp_fmt;
   u_vector_foreach(disp_fmt, &display.formats) {
      vk_outarray_append(&out, out_fmt) {
         out_fmt->surfaceFormat.format = *disp_fmt;
         out_fmt->surfaceFormat.colorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
      }
   }

   wsi_wl_display_finish(&display);

   return vk_outarray_status(&out);
}

static VkResult
wsi_wl_surface_get_present_modes(VkIcdSurfaceBase *surface,
                                 uint32_t* pPresentModeCount,
                                 VkPresentModeKHR* pPresentModes)
{
   if (pPresentModes == NULL) {
      *pPresentModeCount = ARRAY_SIZE(present_modes);
      return VK_SUCCESS;
   }

   *pPresentModeCount = MIN2(*pPresentModeCount, ARRAY_SIZE(present_modes));
   typed_memcpy(pPresentModes, present_modes, *pPresentModeCount);

   if (*pPresentModeCount < ARRAY_SIZE(present_modes))
      return VK_INCOMPLETE;
   else
      return VK_SUCCESS;
}

VkResult wsi_create_wl_surface(const VkAllocationCallbacks *pAllocator,
			       const VkWaylandSurfaceCreateInfoKHR *pCreateInfo,
			       VkSurfaceKHR *pSurface)
{
   VkIcdSurfaceWayland *surface;

   surface = vk_alloc(pAllocator, sizeof *surface, 8,
                      VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (surface == NULL)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   surface->base.platform = VK_ICD_WSI_PLATFORM_WAYLAND;
   surface->display = pCreateInfo->display;
   surface->surface = pCreateInfo->surface;

   *pSurface = VkIcdSurfaceBase_to_handle(&surface->base);

   return VK_SUCCESS;
}

struct wsi_wl_image {
   struct wsi_image                             base;
   struct wl_buffer *                           buffer;
   bool                                         busy;
};

struct wsi_wl_swapchain {
   struct wsi_swapchain                        base;

   struct wsi_wl_display                        *display;

   struct wl_surface *                          surface;
   uint32_t                                     surface_version;
   struct wl_drm *                              drm_wrapper;
   struct wl_callback *                         frame;

   VkExtent2D                                   extent;
   VkFormat                                     vk_format;
   uint32_t                                     drm_format;

   VkPresentModeKHR                             present_mode;
   bool                                         fifo_ready;

   struct wsi_wl_image                          images[0];
};

static struct wsi_image *
wsi_wl_swapchain_get_wsi_image(struct wsi_swapchain *wsi_chain,
                               uint32_t image_index)
{
   struct wsi_wl_swapchain *chain = (struct wsi_wl_swapchain *)wsi_chain;
   return &chain->images[image_index].base;
}

static VkResult
wsi_wl_swapchain_acquire_next_image(struct wsi_swapchain *wsi_chain,
                                    uint64_t timeout,
                                    VkSemaphore semaphore,
                                    uint32_t *image_index)
{
   struct wsi_wl_swapchain *chain = (struct wsi_wl_swapchain *)wsi_chain;

   int ret = wl_display_dispatch_queue_pending(chain->display->wl_display,
                                               chain->display->queue);
   /* XXX: I'm not sure if out-of-date is the right error here.  If
    * wl_display_dispatch_queue_pending fails it most likely means we got
    * kicked by the server so this seems more-or-less correct.
    */
   if (ret < 0)
      return VK_ERROR_OUT_OF_DATE_KHR;

   while (1) {
      for (uint32_t i = 0; i < chain->base.image_count; i++) {
         if (!chain->images[i].busy) {
            /* We found a non-busy image */
            *image_index = i;
            chain->images[i].busy = true;
            return VK_SUCCESS;
         }
      }

      /* This time we do a blocking dispatch because we can't go
       * anywhere until we get an event.
       */
      int ret = wl_display_roundtrip_queue(chain->display->wl_display,
                                           chain->display->queue);
      if (ret < 0)
         return VK_ERROR_OUT_OF_DATE_KHR;
   }
}

static void
frame_handle_done(void *data, struct wl_callback *callback, uint32_t serial)
{
   struct wsi_wl_swapchain *chain = data;

   chain->frame = NULL;
   chain->fifo_ready = true;

   wl_callback_destroy(callback);
}

static const struct wl_callback_listener frame_listener = {
   frame_handle_done,
};

static VkResult
wsi_wl_swapchain_queue_present(struct wsi_swapchain *wsi_chain,
                               uint32_t image_index,
                               const VkPresentRegionKHR *damage)
{
   struct wsi_wl_swapchain *chain = (struct wsi_wl_swapchain *)wsi_chain;

   if (chain->base.present_mode == VK_PRESENT_MODE_FIFO_KHR) {
      while (!chain->fifo_ready) {
         int ret = wl_display_dispatch_queue(chain->display->wl_display,
                                             chain->display->queue);
         if (ret < 0)
            return VK_ERROR_OUT_OF_DATE_KHR;
      }
   }

   assert(image_index < chain->base.image_count);
   wl_surface_attach(chain->surface, chain->images[image_index].buffer, 0, 0);

   if (chain->surface_version >= 4 && damage &&
       damage->pRectangles && damage->rectangleCount > 0) {
      for (unsigned i = 0; i < damage->rectangleCount; i++) {
         const VkRectLayerKHR *rect = &damage->pRectangles[i];
         assert(rect->layer == 0);
         wl_surface_damage_buffer(chain->surface,
                                  rect->offset.x, rect->offset.y,
                                  rect->extent.width, rect->extent.height);
      }
   } else {
      wl_surface_damage(chain->surface, 0, 0, INT32_MAX, INT32_MAX);
   }

   if (chain->base.present_mode == VK_PRESENT_MODE_FIFO_KHR) {
      chain->frame = wl_surface_frame(chain->surface);
      wl_callback_add_listener(chain->frame, &frame_listener, chain);
      chain->fifo_ready = false;
   }

   chain->images[image_index].busy = true;
   wl_surface_commit(chain->surface);
   wl_display_flush(chain->display->wl_display);

   return VK_SUCCESS;
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
   struct wsi_wl_display *display = chain->display;
   const uint64_t *modifiers = NULL;
   uint32_t num_modifiers = 0;
   VkResult result;

   if (display->dmabuf && chain->base.wsi->supports_modifiers) {
      switch (chain->drm_format) {
      case WL_DRM_FORMAT_ARGB8888:
         modifiers = u_vector_tail(&display->modifiers.argb8888);
         num_modifiers = u_vector_length(&display->modifiers.argb8888);
         break;
      case WL_DRM_FORMAT_XRGB8888:
         modifiers = u_vector_tail(&display->modifiers.xrgb8888);
         num_modifiers = u_vector_length(&display->modifiers.xrgb8888);
         break;
      default:
         break;
      }
   }

   result = wsi_create_native_image(&chain->base, pCreateInfo,
                                    num_modifiers > 0 ? 1 : 0,
                                    &num_modifiers, &modifiers,
                                    &image->base);

   if (result != VK_SUCCESS)
      return result;

   if (image->base.drm_modifier != DRM_FORMAT_MOD_INVALID) {
      /* Only request modifiers if we have dmabuf, else it must be implicit. */
      assert(display->dmabuf);

      struct zwp_linux_buffer_params_v1 *params =
         zwp_linux_dmabuf_v1_create_params(display->dmabuf);
      wl_proxy_set_queue((struct wl_proxy *) params, chain->display->queue);

      for (int i = 0; i < image->base.num_planes; i++) {
         zwp_linux_buffer_params_v1_add(params,
                                        image->base.fds[i],
                                        i,
                                        image->base.offsets[i],
                                        image->base.row_pitches[i],
                                        image->base.drm_modifier >> 32,
                                        image->base.drm_modifier & 0xffffffff);
         close(image->base.fds[i]);
      }

      image->buffer =
         zwp_linux_buffer_params_v1_create_immed(params,
                                                 chain->extent.width,
                                                 chain->extent.height,
                                                 chain->drm_format,
                                                 0);
      zwp_linux_buffer_params_v1_destroy(params);
   } else {
      /* Without passing modifiers, we can't have multi-plane RGB images. */
      assert(image->base.num_planes == 1);

      image->buffer =
         wl_drm_create_prime_buffer(chain->drm_wrapper,
                                    image->base.fds[0], /* name */
                                    chain->extent.width,
                                    chain->extent.height,
                                    chain->drm_format,
                                    image->base.offsets[0],
                                    image->base.row_pitches[0],
                                    0, 0, 0, 0 /* unused */);
      close(image->base.fds[0]);
   }

   if (!image->buffer)
      goto fail_image;

   wl_buffer_add_listener(image->buffer, &buffer_listener, image);

   return VK_SUCCESS;

fail_image:
   wsi_destroy_image(&chain->base, &image->base);

   return result;
}

static VkResult
wsi_wl_swapchain_destroy(struct wsi_swapchain *wsi_chain,
                         const VkAllocationCallbacks *pAllocator)
{
   struct wsi_wl_swapchain *chain = (struct wsi_wl_swapchain *)wsi_chain;

   for (uint32_t i = 0; i < chain->base.image_count; i++) {
      if (chain->images[i].buffer) {
         wl_buffer_destroy(chain->images[i].buffer);
         wsi_destroy_image(&chain->base, &chain->images[i].base);
      }
   }

   if (chain->frame)
      wl_callback_destroy(chain->frame);
   if (chain->surface)
      wl_proxy_wrapper_destroy(chain->surface);
   if (chain->drm_wrapper)
      wl_proxy_wrapper_destroy(chain->drm_wrapper);

   if (chain->display)
      wsi_wl_display_unref(chain->display);

   wsi_swapchain_finish(&chain->base);

   vk_free(pAllocator, chain);

   return VK_SUCCESS;
}

static VkResult
wsi_wl_surface_create_swapchain(VkIcdSurfaceBase *icd_surface,
                                VkDevice device,
                                struct wsi_device *wsi_device,
                                int local_fd,
                                const VkSwapchainCreateInfoKHR* pCreateInfo,
                                const VkAllocationCallbacks* pAllocator,
                                struct wsi_swapchain **swapchain_out)
{
   VkIcdSurfaceWayland *surface = (VkIcdSurfaceWayland *)icd_surface;
   struct wsi_wayland *wsi =
      (struct wsi_wayland *)wsi_device->wsi[VK_ICD_WSI_PLATFORM_WAYLAND];
   struct wsi_wl_swapchain *chain;
   VkResult result;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR);

   int num_images = pCreateInfo->minImageCount;

   size_t size = sizeof(*chain) + num_images * sizeof(chain->images[0]);
   chain = vk_alloc(pAllocator, size, 8,
                      VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (chain == NULL)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   result = wsi_swapchain_init(wsi_device, &chain->base, device,
                               pCreateInfo, pAllocator);
   if (result != VK_SUCCESS) {
      vk_free(pAllocator, chain);
      return result;
   }

   /* Mark a bunch of stuff as NULL.  This way we can just call
    * destroy_swapchain for cleanup.
    */
   for (uint32_t i = 0; i < num_images; i++)
      chain->images[i].buffer = NULL;
   chain->surface = NULL;
   chain->drm_wrapper = NULL;
   chain->frame = NULL;

   bool alpha = pCreateInfo->compositeAlpha ==
                      VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;

   chain->base.destroy = wsi_wl_swapchain_destroy;
   chain->base.get_wsi_image = wsi_wl_swapchain_get_wsi_image;
   chain->base.acquire_next_image = wsi_wl_swapchain_acquire_next_image;
   chain->base.queue_present = wsi_wl_swapchain_queue_present;
   chain->base.present_mode = pCreateInfo->presentMode;
   chain->base.image_count = num_images;
   chain->extent = pCreateInfo->imageExtent;
   chain->vk_format = pCreateInfo->imageFormat;
   chain->drm_format = wl_drm_format_for_vk_format(chain->vk_format, alpha);

   if (pCreateInfo->oldSwapchain) {
      /* If we have an oldSwapchain parameter, copy the display struct over
       * from the old one so we don't have to fully re-initialize it.
       */
      struct wsi_wl_swapchain *old_chain = (void *)pCreateInfo->oldSwapchain;
      chain->display = wsi_wl_display_ref(old_chain->display);
   } else {
      chain->display = NULL;
      result = wsi_wl_display_create(wsi, surface->display, &chain->display);
      if (result != VK_SUCCESS)
         goto fail;
   }

   chain->surface = wl_proxy_create_wrapper(surface->surface);
   if (!chain->surface) {
      result = VK_ERROR_OUT_OF_HOST_MEMORY;
      goto fail;
   }
   wl_proxy_set_queue((struct wl_proxy *) chain->surface,
                      chain->display->queue);
   chain->surface_version = wl_proxy_get_version((void *)surface->surface);

   chain->drm_wrapper = wl_proxy_create_wrapper(chain->display->drm);
   if (!chain->drm_wrapper) {
      result = VK_ERROR_OUT_OF_HOST_MEMORY;
      goto fail;
   }
   wl_proxy_set_queue((struct wl_proxy *) chain->drm_wrapper,
                      chain->display->queue);

   chain->fifo_ready = true;

   for (uint32_t i = 0; i < chain->base.image_count; i++) {
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
wsi_wl_init_wsi(struct wsi_device *wsi_device,
                const VkAllocationCallbacks *alloc,
                VkPhysicalDevice physical_device)
{
   struct wsi_wayland *wsi;
   VkResult result;

   wsi = vk_alloc(alloc, sizeof(*wsi), 8,
                   VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
   if (!wsi) {
      result = VK_ERROR_OUT_OF_HOST_MEMORY;
      goto fail;
   }

   wsi->physical_device = physical_device;
   wsi->alloc = alloc;
   wsi->wsi = wsi_device;

   wsi->base.get_support = wsi_wl_surface_get_support;
   wsi->base.get_capabilities = wsi_wl_surface_get_capabilities;
   wsi->base.get_capabilities2 = wsi_wl_surface_get_capabilities2;
   wsi->base.get_formats = wsi_wl_surface_get_formats;
   wsi->base.get_formats2 = wsi_wl_surface_get_formats2;
   wsi->base.get_present_modes = wsi_wl_surface_get_present_modes;
   wsi->base.create_swapchain = wsi_wl_surface_create_swapchain;

   wsi_device->wsi[VK_ICD_WSI_PLATFORM_WAYLAND] = &wsi->base;

   return VK_SUCCESS;

fail:
   wsi_device->wsi[VK_ICD_WSI_PLATFORM_WAYLAND] = NULL;

   return result;
}

void
wsi_wl_finish_wsi(struct wsi_device *wsi_device,
                  const VkAllocationCallbacks *alloc)
{
   struct wsi_wayland *wsi =
      (struct wsi_wayland *)wsi_device->wsi[VK_ICD_WSI_PLATFORM_WAYLAND];
   if (!wsi)
      return;

   vk_free(alloc, wsi);
}
