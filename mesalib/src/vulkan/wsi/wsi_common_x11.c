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

#include <X11/Xlib-xcb.h>
#include <X11/xshmfence.h>
#include <xcb/xcb.h>
#include <xcb/dri3.h>
#include <xcb/present.h>

#include "util/macros.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include "util/hash_table.h"

#include "wsi_common.h"
#include "wsi_common_x11.h"

#define typed_memcpy(dest, src, count) ({ \
   static_assert(sizeof(*src) == sizeof(*dest), ""); \
   memcpy((dest), (src), (count) * sizeof(*(src))); \
})

struct wsi_x11_connection {
   bool has_dri3;
   bool has_present;
};

struct wsi_x11 {
   struct wsi_interface base;

   pthread_mutex_t                              mutex;
   /* Hash table of xcb_connection -> wsi_x11_connection mappings */
   struct hash_table *connections;
};

static struct wsi_x11_connection *
wsi_x11_connection_create(const VkAllocationCallbacks *alloc,
                          xcb_connection_t *conn)
{
   xcb_query_extension_cookie_t dri3_cookie, pres_cookie;
   xcb_query_extension_reply_t *dri3_reply, *pres_reply;

   struct wsi_x11_connection *wsi_conn =
      vk_alloc(alloc, sizeof(*wsi_conn), 8,
                VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
   if (!wsi_conn)
      return NULL;

   dri3_cookie = xcb_query_extension(conn, 4, "DRI3");
   pres_cookie = xcb_query_extension(conn, 7, "PRESENT");

   dri3_reply = xcb_query_extension_reply(conn, dri3_cookie, NULL);
   pres_reply = xcb_query_extension_reply(conn, pres_cookie, NULL);
   if (dri3_reply == NULL || pres_reply == NULL) {
      free(dri3_reply);
      free(pres_reply);
      vk_free(alloc, wsi_conn);
      return NULL;
   }

   wsi_conn->has_dri3 = dri3_reply->present != 0;
   wsi_conn->has_present = pres_reply->present != 0;

   free(dri3_reply);
   free(pres_reply);

   return wsi_conn;
}

static void
wsi_x11_connection_destroy(const VkAllocationCallbacks *alloc,
                           struct wsi_x11_connection *conn)
{
   vk_free(alloc, conn);
}

static struct wsi_x11_connection *
wsi_x11_get_connection(struct wsi_device *wsi_dev,
		       const VkAllocationCallbacks *alloc,
                       xcb_connection_t *conn)
{
   struct wsi_x11 *wsi =
      (struct wsi_x11 *)wsi_dev->wsi[VK_ICD_WSI_PLATFORM_XCB];

   pthread_mutex_lock(&wsi->mutex);

   struct hash_entry *entry = _mesa_hash_table_search(wsi->connections, conn);
   if (!entry) {
      /* We're about to make a bunch of blocking calls.  Let's drop the
       * mutex for now so we don't block up too badly.
       */
      pthread_mutex_unlock(&wsi->mutex);

      struct wsi_x11_connection *wsi_conn =
         wsi_x11_connection_create(alloc, conn);

      pthread_mutex_lock(&wsi->mutex);

      entry = _mesa_hash_table_search(wsi->connections, conn);
      if (entry) {
         /* Oops, someone raced us to it */
         wsi_x11_connection_destroy(alloc, wsi_conn);
      } else {
         entry = _mesa_hash_table_insert(wsi->connections, conn, wsi_conn);
      }
   }

   pthread_mutex_unlock(&wsi->mutex);

   return entry->data;
}

static const VkSurfaceFormatKHR formats[] = {
   { .format = VK_FORMAT_B8G8R8A8_SRGB, },
   { .format = VK_FORMAT_B8G8R8A8_UNORM, },
};

static const VkPresentModeKHR present_modes[] = {
   VK_PRESENT_MODE_MAILBOX_KHR,
};

static xcb_screen_t *
get_screen_for_root(xcb_connection_t *conn, xcb_window_t root)
{
   xcb_screen_iterator_t screen_iter =
      xcb_setup_roots_iterator(xcb_get_setup(conn));

   for (; screen_iter.rem; xcb_screen_next (&screen_iter)) {
      if (screen_iter.data->root == root)
         return screen_iter.data;
   }

   return NULL;
}

static xcb_visualtype_t *
screen_get_visualtype(xcb_screen_t *screen, xcb_visualid_t visual_id,
                      unsigned *depth)
{
   xcb_depth_iterator_t depth_iter =
      xcb_screen_allowed_depths_iterator(screen);

   for (; depth_iter.rem; xcb_depth_next (&depth_iter)) {
      xcb_visualtype_iterator_t visual_iter =
         xcb_depth_visuals_iterator (depth_iter.data);

      for (; visual_iter.rem; xcb_visualtype_next (&visual_iter)) {
         if (visual_iter.data->visual_id == visual_id) {
            if (depth)
               *depth = depth_iter.data->depth;
            return visual_iter.data;
         }
      }
   }

   return NULL;
}

static xcb_visualtype_t *
connection_get_visualtype(xcb_connection_t *conn, xcb_visualid_t visual_id,
                          unsigned *depth)
{
   xcb_screen_iterator_t screen_iter =
      xcb_setup_roots_iterator(xcb_get_setup(conn));

   /* For this we have to iterate over all of the screens which is rather
    * annoying.  Fortunately, there is probably only 1.
    */
   for (; screen_iter.rem; xcb_screen_next (&screen_iter)) {
      xcb_visualtype_t *visual = screen_get_visualtype(screen_iter.data,
                                                       visual_id, depth);
      if (visual)
         return visual;
   }

   return NULL;
}

static xcb_visualtype_t *
get_visualtype_for_window(xcb_connection_t *conn, xcb_window_t window,
                          unsigned *depth)
{
   xcb_query_tree_cookie_t tree_cookie;
   xcb_get_window_attributes_cookie_t attrib_cookie;
   xcb_query_tree_reply_t *tree;
   xcb_get_window_attributes_reply_t *attrib;

   tree_cookie = xcb_query_tree(conn, window);
   attrib_cookie = xcb_get_window_attributes(conn, window);

   tree = xcb_query_tree_reply(conn, tree_cookie, NULL);
   attrib = xcb_get_window_attributes_reply(conn, attrib_cookie, NULL);
   if (attrib == NULL || tree == NULL) {
      free(attrib);
      free(tree);
      return NULL;
   }

   xcb_window_t root = tree->root;
   xcb_visualid_t visual_id = attrib->visual;
   free(attrib);
   free(tree);

   xcb_screen_t *screen = get_screen_for_root(conn, root);
   if (screen == NULL)
      return NULL;

   return screen_get_visualtype(screen, visual_id, depth);
}

static bool
visual_has_alpha(xcb_visualtype_t *visual, unsigned depth)
{
   uint32_t rgb_mask = visual->red_mask |
                       visual->green_mask |
                       visual->blue_mask;

   uint32_t all_mask = 0xffffffff >> (32 - depth);

   /* Do we have bits left over after RGB? */
   return (all_mask & ~rgb_mask) != 0;
}

VkBool32 wsi_get_physical_device_xcb_presentation_support(
    struct wsi_device *wsi_device,
    VkAllocationCallbacks *alloc,
    uint32_t                                    queueFamilyIndex,
    xcb_connection_t*                           connection,
    xcb_visualid_t                              visual_id)
{
   struct wsi_x11_connection *wsi_conn =
      wsi_x11_get_connection(wsi_device, alloc, connection);

   if (!wsi_conn->has_dri3) {
      fprintf(stderr, "vulkan: No DRI3 support\n");
      return false;
   }

   unsigned visual_depth;
   if (!connection_get_visualtype(connection, visual_id, &visual_depth))
      return false;

   if (visual_depth != 24 && visual_depth != 32)
      return false;

   return true;
}

static xcb_connection_t*
x11_surface_get_connection(VkIcdSurfaceBase *icd_surface)
{
   if (icd_surface->platform == VK_ICD_WSI_PLATFORM_XLIB)
      return XGetXCBConnection(((VkIcdSurfaceXlib *)icd_surface)->dpy);
   else
      return ((VkIcdSurfaceXcb *)icd_surface)->connection;
}

static xcb_window_t
x11_surface_get_window(VkIcdSurfaceBase *icd_surface)
{
   if (icd_surface->platform == VK_ICD_WSI_PLATFORM_XLIB)
      return ((VkIcdSurfaceXlib *)icd_surface)->window;
   else
      return ((VkIcdSurfaceXcb *)icd_surface)->window;
}

static VkResult
x11_surface_get_support(VkIcdSurfaceBase *icd_surface,
                        struct wsi_device *wsi_device,
                        const VkAllocationCallbacks *alloc,
                        uint32_t queueFamilyIndex,
                        VkBool32* pSupported)
{
   xcb_connection_t *conn = x11_surface_get_connection(icd_surface);
   xcb_window_t window = x11_surface_get_window(icd_surface);

   struct wsi_x11_connection *wsi_conn =
      wsi_x11_get_connection(wsi_device, alloc, conn);
   if (!wsi_conn)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   if (!wsi_conn->has_dri3) {
      fprintf(stderr, "vulkan: No DRI3 support\n");
      *pSupported = false;
      return VK_SUCCESS;
   }

   unsigned visual_depth;
   if (!get_visualtype_for_window(conn, window, &visual_depth)) {
      *pSupported = false;
      return VK_SUCCESS;
   }

   if (visual_depth != 24 && visual_depth != 32) {
      *pSupported = false;
      return VK_SUCCESS;
   }

   *pSupported = true;
   return VK_SUCCESS;
}

static VkResult
x11_surface_get_capabilities(VkIcdSurfaceBase *icd_surface,
                             VkSurfaceCapabilitiesKHR *caps)
{
   xcb_connection_t *conn = x11_surface_get_connection(icd_surface);
   xcb_window_t window = x11_surface_get_window(icd_surface);
   xcb_get_geometry_cookie_t geom_cookie;
   xcb_generic_error_t *err;
   xcb_get_geometry_reply_t *geom;
   unsigned visual_depth;

   geom_cookie = xcb_get_geometry(conn, window);

   /* This does a round-trip.  This is why we do get_geometry first and
    * wait to read the reply until after we have a visual.
    */
   xcb_visualtype_t *visual =
      get_visualtype_for_window(conn, window, &visual_depth);

   geom = xcb_get_geometry_reply(conn, geom_cookie, &err);
   if (geom) {
      VkExtent2D extent = { geom->width, geom->height };
      caps->currentExtent = extent;
      caps->minImageExtent = extent;
      caps->maxImageExtent = extent;
   } else {
      /* This can happen if the client didn't wait for the configure event
       * to come back from the compositor.  In that case, we don't know the
       * size of the window so we just return valid "I don't know" stuff.
       */
      caps->currentExtent = (VkExtent2D) { -1, -1 };
      caps->minImageExtent = (VkExtent2D) { 1, 1 };
      caps->maxImageExtent = (VkExtent2D) { INT16_MAX, INT16_MAX };
   }
   free(err);
   free(geom);

   if (visual_has_alpha(visual, visual_depth)) {
      caps->supportedCompositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR |
                                      VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
   } else {
      caps->supportedCompositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR |
                                      VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
   }

   caps->minImageCount = 2;
   caps->maxImageCount = 4;
   caps->supportedTransforms = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
   caps->currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
   caps->maxImageArrayLayers = 1;
   caps->supportedUsageFlags =
      VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
      VK_IMAGE_USAGE_SAMPLED_BIT |
      VK_IMAGE_USAGE_TRANSFER_DST_BIT |
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

   return VK_SUCCESS;
}

static VkResult
x11_surface_get_formats(VkIcdSurfaceBase *surface,
                        struct wsi_device *wsi_device,
                        uint32_t *pSurfaceFormatCount,
                        VkSurfaceFormatKHR *pSurfaceFormats)
{
   if (pSurfaceFormats == NULL) {
      *pSurfaceFormatCount = ARRAY_SIZE(formats);
      return VK_SUCCESS;
   }

   assert(*pSurfaceFormatCount >= ARRAY_SIZE(formats));
   typed_memcpy(pSurfaceFormats, formats, *pSurfaceFormatCount);
   *pSurfaceFormatCount = ARRAY_SIZE(formats);

   return VK_SUCCESS;
}

static VkResult
x11_surface_get_present_modes(VkIcdSurfaceBase *surface,
                              uint32_t *pPresentModeCount,
                              VkPresentModeKHR *pPresentModes)
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

VkResult wsi_create_xcb_surface(const VkAllocationCallbacks *pAllocator,
				const VkXcbSurfaceCreateInfoKHR *pCreateInfo,
				VkSurfaceKHR *pSurface)
{
   VkIcdSurfaceXcb *surface;

   surface = vk_alloc(pAllocator, sizeof *surface, 8,
                      VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (surface == NULL)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   surface->base.platform = VK_ICD_WSI_PLATFORM_XCB;
   surface->connection = pCreateInfo->connection;
   surface->window = pCreateInfo->window;

   *pSurface = _VkIcdSurfaceBase_to_handle(&surface->base);
   return VK_SUCCESS;
}

VkResult wsi_create_xlib_surface(const VkAllocationCallbacks *pAllocator,
				 const VkXlibSurfaceCreateInfoKHR *pCreateInfo,
				 VkSurfaceKHR *pSurface)
{
   VkIcdSurfaceXlib *surface;

   surface = vk_alloc(pAllocator, sizeof *surface, 8,
                      VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (surface == NULL)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   surface->base.platform = VK_ICD_WSI_PLATFORM_XLIB;
   surface->dpy = pCreateInfo->dpy;
   surface->window = pCreateInfo->window;

   *pSurface = _VkIcdSurfaceBase_to_handle(&surface->base);
   return VK_SUCCESS;
}

struct x11_image {
   VkImage image;
   VkDeviceMemory memory;
   xcb_pixmap_t                              pixmap;
   bool                                      busy;
   struct xshmfence *                        shm_fence;
   uint32_t                                  sync_fence;
};

struct x11_swapchain {
   struct wsi_swapchain                        base;

   xcb_connection_t *                           conn;
   xcb_window_t                                 window;
   xcb_gc_t                                     gc;
   VkExtent2D                                   extent;
   uint32_t                                     image_count;

   xcb_present_event_t                          event_id;
   xcb_special_event_t *                        special_event;
   uint64_t                                     send_sbc;
   uint32_t                                     stamp;

   struct x11_image                             images[0];
};

static VkResult
x11_get_images(struct wsi_swapchain *anv_chain,
               uint32_t* pCount, VkImage *pSwapchainImages)
{
   struct x11_swapchain *chain = (struct x11_swapchain *)anv_chain;

   if (pSwapchainImages == NULL) {
      *pCount = chain->image_count;
      return VK_SUCCESS;
   }

   assert(chain->image_count <= *pCount);
   for (uint32_t i = 0; i < chain->image_count; i++)
      pSwapchainImages[i] = chain->images[i].image;

   *pCount = chain->image_count;

   return VK_SUCCESS;
}

static VkResult
x11_handle_dri3_present_event(struct x11_swapchain *chain,
                              xcb_present_generic_event_t *event)
{
   switch (event->evtype) {
   case XCB_PRESENT_CONFIGURE_NOTIFY: {
      xcb_present_configure_notify_event_t *config = (void *) event;

      if (config->width != chain->extent.width ||
          config->height != chain->extent.height)
         return VK_ERROR_OUT_OF_DATE_KHR;

      break;
   }

   case XCB_PRESENT_EVENT_IDLE_NOTIFY: {
      xcb_present_idle_notify_event_t *idle = (void *) event;

      for (unsigned i = 0; i < chain->image_count; i++) {
         if (chain->images[i].pixmap == idle->pixmap) {
            chain->images[i].busy = false;
            break;
         }
      }

      break;
   }

   case XCB_PRESENT_COMPLETE_NOTIFY:
   default:
      break;
   }

   return VK_SUCCESS;
}

static VkResult
x11_acquire_next_image(struct wsi_swapchain *anv_chain,
                       uint64_t timeout,
                       VkSemaphore semaphore,
                       uint32_t *image_index)
{
   struct x11_swapchain *chain = (struct x11_swapchain *)anv_chain;

   while (1) {
      for (uint32_t i = 0; i < chain->image_count; i++) {
         if (!chain->images[i].busy) {
            /* We found a non-busy image */
            xshmfence_await(chain->images[i].shm_fence);
            *image_index = i;
            chain->images[i].busy = true;
            return VK_SUCCESS;
         }
      }

      xcb_flush(chain->conn);
      xcb_generic_event_t *event =
         xcb_wait_for_special_event(chain->conn, chain->special_event);
      if (!event)
         return VK_ERROR_OUT_OF_DATE_KHR;

      VkResult result = x11_handle_dri3_present_event(chain, (void *)event);
      free(event);
      if (result != VK_SUCCESS)
         return result;
   }
}

static VkResult
x11_queue_present(struct wsi_swapchain *anv_chain,
                  uint32_t image_index)
{
   struct x11_swapchain *chain = (struct x11_swapchain *)anv_chain;
   struct x11_image *image = &chain->images[image_index];

   assert(image_index < chain->image_count);

   uint32_t options = XCB_PRESENT_OPTION_NONE;

   int64_t target_msc = 0;
   int64_t divisor = 0;
   int64_t remainder = 0;

   options |= XCB_PRESENT_OPTION_ASYNC;

   xshmfence_reset(image->shm_fence);

   ++chain->send_sbc;
   xcb_void_cookie_t cookie =
      xcb_present_pixmap(chain->conn,
                         chain->window,
                         image->pixmap,
                         (uint32_t) chain->send_sbc,
                         0,                                    /* valid */
                         0,                                    /* update */
                         0,                                    /* x_off */
                         0,                                    /* y_off */
                         XCB_NONE,                             /* target_crtc */
                         XCB_NONE,
                         image->sync_fence,
                         options,
                         target_msc,
                         divisor,
                         remainder, 0, NULL);
   xcb_discard_reply(chain->conn, cookie.sequence);
   image->busy = true;

   xcb_flush(chain->conn);

   return VK_SUCCESS;
}

static VkResult
x11_image_init(VkDevice device_h, struct x11_swapchain *chain,
               const VkSwapchainCreateInfoKHR *pCreateInfo,
               const VkAllocationCallbacks* pAllocator,
               struct x11_image *image)
{
   xcb_void_cookie_t cookie;
   VkResult result;
   uint32_t row_pitch;
   uint32_t offset;
   uint32_t bpp = 32;
   uint32_t depth = 24;
   int fd;
   uint32_t size;

   result = chain->base.image_fns->create_wsi_image(device_h,
                                                    pCreateInfo,
                                                    pAllocator,
                                                    &image->image,
                                                    &image->memory,
                                                    &size,
                                                    &offset,
                                                    &row_pitch,
                                                    &fd);
   if (result != VK_SUCCESS)
      return result;

   image->pixmap = xcb_generate_id(chain->conn);

   cookie =
      xcb_dri3_pixmap_from_buffer_checked(chain->conn,
                                          image->pixmap,
                                          chain->window,
                                          size,
                                          pCreateInfo->imageExtent.width,
                                          pCreateInfo->imageExtent.height,
                                          row_pitch,
                                          depth, bpp, fd);
   xcb_discard_reply(chain->conn, cookie.sequence);

   int fence_fd = xshmfence_alloc_shm();
   if (fence_fd < 0)
      goto fail_pixmap;

   image->shm_fence = xshmfence_map_shm(fence_fd);
   if (image->shm_fence == NULL)
      goto fail_shmfence_alloc;

   image->sync_fence = xcb_generate_id(chain->conn);
   xcb_dri3_fence_from_fd(chain->conn,
                          image->pixmap,
                          image->sync_fence,
                          false,
                          fence_fd);

   image->busy = false;
   xshmfence_trigger(image->shm_fence);

   return VK_SUCCESS;

fail_shmfence_alloc:
   close(fence_fd);

fail_pixmap:
   cookie = xcb_free_pixmap(chain->conn, image->pixmap);
   xcb_discard_reply(chain->conn, cookie.sequence);

   chain->base.image_fns->free_wsi_image(device_h, pAllocator,
                                        image->image, image->memory);

   return result;
}

static void
x11_image_finish(struct x11_swapchain *chain,
                 const VkAllocationCallbacks* pAllocator,
                 struct x11_image *image)
{
   xcb_void_cookie_t cookie;

   cookie = xcb_sync_destroy_fence(chain->conn, image->sync_fence);
   xcb_discard_reply(chain->conn, cookie.sequence);
   xshmfence_unmap_shm(image->shm_fence);

   cookie = xcb_free_pixmap(chain->conn, image->pixmap);
   xcb_discard_reply(chain->conn, cookie.sequence);

   chain->base.image_fns->free_wsi_image(chain->base.device, pAllocator,
                                        image->image, image->memory);
}

static VkResult
x11_swapchain_destroy(struct wsi_swapchain *anv_chain,
                      const VkAllocationCallbacks *pAllocator)
{
   struct x11_swapchain *chain = (struct x11_swapchain *)anv_chain;
   for (uint32_t i = 0; i < chain->image_count; i++)
      x11_image_finish(chain, pAllocator, &chain->images[i]);

   xcb_unregister_for_special_event(chain->conn, chain->special_event);

   vk_free(pAllocator, chain);

   return VK_SUCCESS;
}

static VkResult
x11_surface_create_swapchain(VkIcdSurfaceBase *icd_surface,
                             VkDevice device,
                             struct wsi_device *wsi_device,
                             const VkSwapchainCreateInfoKHR *pCreateInfo,
                             const VkAllocationCallbacks* pAllocator,
                             const struct wsi_image_fns *image_fns,
                             struct wsi_swapchain **swapchain_out)
{
   struct x11_swapchain *chain;
   xcb_void_cookie_t cookie;
   VkResult result;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR);

   int num_images = pCreateInfo->minImageCount;

   /* For true mailbox mode, we need at least 4 images:
    *  1) One to scan out from
    *  2) One to have queued for scan-out
    *  3) One to be currently held by the Wayland compositor
    *  4) One to render to
    */
   if (pCreateInfo->presentMode == VK_PRESENT_MODE_MAILBOX_KHR)
      num_images = MAX2(num_images, 4);

   size_t size = sizeof(*chain) + num_images * sizeof(chain->images[0]);
   chain = vk_alloc(pAllocator, size, 8,
                      VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (chain == NULL)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   chain->base.device = device;
   chain->base.destroy = x11_swapchain_destroy;
   chain->base.get_images = x11_get_images;
   chain->base.acquire_next_image = x11_acquire_next_image;
   chain->base.queue_present = x11_queue_present;
   chain->base.image_fns = image_fns;
   chain->conn = x11_surface_get_connection(icd_surface);
   chain->window = x11_surface_get_window(icd_surface);
   chain->extent = pCreateInfo->imageExtent;
   chain->image_count = num_images;
   chain->send_sbc = 0;

   chain->event_id = xcb_generate_id(chain->conn);
   xcb_present_select_input(chain->conn, chain->event_id, chain->window,
                            XCB_PRESENT_EVENT_MASK_CONFIGURE_NOTIFY |
                            XCB_PRESENT_EVENT_MASK_COMPLETE_NOTIFY |
                            XCB_PRESENT_EVENT_MASK_IDLE_NOTIFY);

   /* Create an XCB event queue to hold present events outside of the usual
    * application event queue
    */
   chain->special_event =
      xcb_register_for_special_xge(chain->conn, &xcb_present_id,
                                   chain->event_id, NULL);

   chain->gc = xcb_generate_id(chain->conn);
   if (!chain->gc) {
      /* FINISHME: Choose a better error. */
      result = VK_ERROR_OUT_OF_HOST_MEMORY;
      goto fail_register;
   }

   cookie = xcb_create_gc(chain->conn,
                          chain->gc,
                          chain->window,
                          XCB_GC_GRAPHICS_EXPOSURES,
                          (uint32_t []) { 0 });
   xcb_discard_reply(chain->conn, cookie.sequence);

   uint32_t image = 0;
   for (; image < chain->image_count; image++) {
      result = x11_image_init(device, chain, pCreateInfo, pAllocator,
                              &chain->images[image]);
      if (result != VK_SUCCESS)
         goto fail_init_images;
   }

   *swapchain_out = &chain->base;

   return VK_SUCCESS;

fail_init_images:
   for (uint32_t j = 0; j < image; j++)
      x11_image_finish(chain, pAllocator, &chain->images[j]);

fail_register:
   xcb_unregister_for_special_event(chain->conn, chain->special_event);

   vk_free(pAllocator, chain);

   return result;
}

VkResult
wsi_x11_init_wsi(struct wsi_device *wsi_device,
                 const VkAllocationCallbacks *alloc)
{
   struct wsi_x11 *wsi;
   VkResult result;

   wsi = vk_alloc(alloc, sizeof(*wsi), 8,
                   VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
   if (!wsi) {
      result = VK_ERROR_OUT_OF_HOST_MEMORY;
      goto fail;
   }

   int ret = pthread_mutex_init(&wsi->mutex, NULL);
   if (ret != 0) {
      if (ret == ENOMEM) {
         result = VK_ERROR_OUT_OF_HOST_MEMORY;
      } else {
         /* FINISHME: Choose a better error. */
         result = VK_ERROR_OUT_OF_HOST_MEMORY;
      }

      goto fail_alloc;
   }

   wsi->connections = _mesa_hash_table_create(NULL, _mesa_hash_pointer,
                                              _mesa_key_pointer_equal);
   if (!wsi->connections) {
      result = VK_ERROR_OUT_OF_HOST_MEMORY;
      goto fail_mutex;
   }

   wsi->base.get_support = x11_surface_get_support;
   wsi->base.get_capabilities = x11_surface_get_capabilities;
   wsi->base.get_formats = x11_surface_get_formats;
   wsi->base.get_present_modes = x11_surface_get_present_modes;
   wsi->base.create_swapchain = x11_surface_create_swapchain;

   wsi_device->wsi[VK_ICD_WSI_PLATFORM_XCB] = &wsi->base;
   wsi_device->wsi[VK_ICD_WSI_PLATFORM_XLIB] = &wsi->base;

   return VK_SUCCESS;

fail_mutex:
   pthread_mutex_destroy(&wsi->mutex);
fail_alloc:
   vk_free(alloc, wsi);
fail:
   wsi_device->wsi[VK_ICD_WSI_PLATFORM_XCB] = NULL;
   wsi_device->wsi[VK_ICD_WSI_PLATFORM_XLIB] = NULL;

   return result;
}

void
wsi_x11_finish_wsi(struct wsi_device *wsi_device,
                   const VkAllocationCallbacks *alloc)
{
   struct wsi_x11 *wsi =
      (struct wsi_x11 *)wsi_device->wsi[VK_ICD_WSI_PLATFORM_XCB];

   if (wsi) {
      _mesa_hash_table_destroy(wsi->connections, NULL);

      pthread_mutex_destroy(&wsi->mutex);

      vk_free(alloc, wsi);
   }
}
