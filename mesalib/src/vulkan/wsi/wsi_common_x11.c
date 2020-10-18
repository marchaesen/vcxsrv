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
#include <fcntl.h>
#include <poll.h>
#include <xf86drm.h>
#include "drm-uapi/drm_fourcc.h"
#include "util/hash_table.h"
#include "util/xmlconfig.h"

#include "vk_util.h"
#include "vk_enum_to_str.h"
#include "wsi_common_private.h"
#include "wsi_common_x11.h"
#include "wsi_common_queue.h"

#define typed_memcpy(dest, src, count) ({ \
   STATIC_ASSERT(sizeof(*src) == sizeof(*dest)); \
   memcpy((dest), (src), (count) * sizeof(*(src))); \
})

struct wsi_x11_connection {
   bool has_dri3;
   bool has_dri3_modifiers;
   bool has_present;
   bool is_proprietary_x11;
};

struct wsi_x11 {
   struct wsi_interface base;

   pthread_mutex_t                              mutex;
   /* Hash table of xcb_connection -> wsi_x11_connection mappings */
   struct hash_table *connections;
};


/** wsi_dri3_open
 *
 * Wrapper around xcb_dri3_open
 */
static int
wsi_dri3_open(xcb_connection_t *conn,
	      xcb_window_t root,
	      uint32_t provider)
{
   xcb_dri3_open_cookie_t       cookie;
   xcb_dri3_open_reply_t        *reply;
   int                          fd;

   cookie = xcb_dri3_open(conn,
                          root,
                          provider);

   reply = xcb_dri3_open_reply(conn, cookie, NULL);
   if (!reply)
      return -1;

   if (reply->nfd != 1) {
      free(reply);
      return -1;
   }

   fd = xcb_dri3_open_reply_fds(conn, reply)[0];
   free(reply);
   fcntl(fd, F_SETFD, fcntl(fd, F_GETFD) | FD_CLOEXEC);

   return fd;
}

static bool
wsi_x11_check_dri3_compatible(const struct wsi_device *wsi_dev,
                              xcb_connection_t *conn)
{
   xcb_screen_iterator_t screen_iter =
      xcb_setup_roots_iterator(xcb_get_setup(conn));
   xcb_screen_t *screen = screen_iter.data;

   int dri3_fd = wsi_dri3_open(conn, screen->root, None);
   if (dri3_fd == -1)
      return true;

   bool match = wsi_device_matches_drm_fd(wsi_dev, dri3_fd);

   close(dri3_fd);

   return match;
}

static struct wsi_x11_connection *
wsi_x11_connection_create(struct wsi_device *wsi_dev,
                          xcb_connection_t *conn)
{
   xcb_query_extension_cookie_t dri3_cookie, pres_cookie, amd_cookie, nv_cookie;
   xcb_query_extension_reply_t *dri3_reply, *pres_reply, *amd_reply, *nv_reply;
   bool has_dri3_v1_2 = false;
   bool has_present_v1_2 = false;

   struct wsi_x11_connection *wsi_conn =
      vk_alloc(&wsi_dev->instance_alloc, sizeof(*wsi_conn), 8,
                VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
   if (!wsi_conn)
      return NULL;

   dri3_cookie = xcb_query_extension(conn, 4, "DRI3");
   pres_cookie = xcb_query_extension(conn, 7, "Present");

   /* We try to be nice to users and emit a warning if they try to use a
    * Vulkan application on a system without DRI3 enabled.  However, this ends
    * up spewing the warning when a user has, for example, both Intel
    * integrated graphics and a discrete card with proprietary drivers and are
    * running on the discrete card with the proprietary DDX.  In this case, we
    * really don't want to print the warning because it just confuses users.
    * As a heuristic to detect this case, we check for a couple of proprietary
    * X11 extensions.
    */
   amd_cookie = xcb_query_extension(conn, 11, "ATIFGLRXDRI");
   nv_cookie = xcb_query_extension(conn, 10, "NV-CONTROL");

   dri3_reply = xcb_query_extension_reply(conn, dri3_cookie, NULL);
   pres_reply = xcb_query_extension_reply(conn, pres_cookie, NULL);
   amd_reply = xcb_query_extension_reply(conn, amd_cookie, NULL);
   nv_reply = xcb_query_extension_reply(conn, nv_cookie, NULL);
   if (!dri3_reply || !pres_reply) {
      free(dri3_reply);
      free(pres_reply);
      free(amd_reply);
      free(nv_reply);
      vk_free(&wsi_dev->instance_alloc, wsi_conn);
      return NULL;
   }

   wsi_conn->has_dri3 = dri3_reply->present != 0;
#ifdef HAVE_DRI3_MODIFIERS
   if (wsi_conn->has_dri3) {
      xcb_dri3_query_version_cookie_t ver_cookie;
      xcb_dri3_query_version_reply_t *ver_reply;

      ver_cookie = xcb_dri3_query_version(conn, 1, 2);
      ver_reply = xcb_dri3_query_version_reply(conn, ver_cookie, NULL);
      has_dri3_v1_2 =
         (ver_reply->major_version > 1 || ver_reply->minor_version >= 2);
      free(ver_reply);
   }
#endif

   wsi_conn->has_present = pres_reply->present != 0;
#ifdef HAVE_DRI3_MODIFIERS
   if (wsi_conn->has_present) {
      xcb_present_query_version_cookie_t ver_cookie;
      xcb_present_query_version_reply_t *ver_reply;

      ver_cookie = xcb_present_query_version(conn, 1, 2);
      ver_reply = xcb_present_query_version_reply(conn, ver_cookie, NULL);
      has_present_v1_2 =
        (ver_reply->major_version > 1 || ver_reply->minor_version >= 2);
      free(ver_reply);
   }
#endif

   wsi_conn->has_dri3_modifiers = has_dri3_v1_2 && has_present_v1_2;
   wsi_conn->is_proprietary_x11 = false;
   if (amd_reply && amd_reply->present)
      wsi_conn->is_proprietary_x11 = true;
   if (nv_reply && nv_reply->present)
      wsi_conn->is_proprietary_x11 = true;

   free(dri3_reply);
   free(pres_reply);
   free(amd_reply);
   free(nv_reply);

   return wsi_conn;
}

static void
wsi_x11_connection_destroy(struct wsi_device *wsi_dev,
                           struct wsi_x11_connection *conn)
{
   vk_free(&wsi_dev->instance_alloc, conn);
}

static bool
wsi_x11_check_for_dri3(struct wsi_x11_connection *wsi_conn)
{
  if (wsi_conn->has_dri3)
    return true;
  if (!wsi_conn->is_proprietary_x11) {
    fprintf(stderr, "vulkan: No DRI3 support detected - required for presentation\n"
                    "Note: you can probably enable DRI3 in your Xorg config\n");
  }
  return false;
}

static struct wsi_x11_connection *
wsi_x11_get_connection(struct wsi_device *wsi_dev,
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
         wsi_x11_connection_create(wsi_dev, conn);
      if (!wsi_conn)
         return NULL;

      pthread_mutex_lock(&wsi->mutex);

      entry = _mesa_hash_table_search(wsi->connections, conn);
      if (entry) {
         /* Oops, someone raced us to it */
         wsi_x11_connection_destroy(wsi_dev, wsi_conn);
      } else {
         entry = _mesa_hash_table_insert(wsi->connections, conn, wsi_conn);
      }
   }

   pthread_mutex_unlock(&wsi->mutex);

   return entry->data;
}

static const VkFormat formats[] = {
   VK_FORMAT_B8G8R8A8_SRGB,
   VK_FORMAT_B8G8R8A8_UNORM,
};

static const VkPresentModeKHR present_modes[] = {
   VK_PRESENT_MODE_IMMEDIATE_KHR,
   VK_PRESENT_MODE_MAILBOX_KHR,
   VK_PRESENT_MODE_FIFO_KHR,
   VK_PRESENT_MODE_FIFO_RELAXED_KHR,
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
    uint32_t                                    queueFamilyIndex,
    xcb_connection_t*                           connection,
    xcb_visualid_t                              visual_id)
{
   struct wsi_x11_connection *wsi_conn =
      wsi_x11_get_connection(wsi_device, connection);

   if (!wsi_conn)
      return false;

   if (!wsi_x11_check_for_dri3(wsi_conn))
      return false;

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
                        uint32_t queueFamilyIndex,
                        VkBool32* pSupported)
{
   xcb_connection_t *conn = x11_surface_get_connection(icd_surface);
   xcb_window_t window = x11_surface_get_window(icd_surface);

   struct wsi_x11_connection *wsi_conn =
      wsi_x11_get_connection(wsi_device, conn);
   if (!wsi_conn)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   if (!wsi_x11_check_for_dri3(wsi_conn)) {
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

static uint32_t
x11_get_min_image_count(struct wsi_device *wsi_device)
{
   if (wsi_device->x11.override_minImageCount)
      return wsi_device->x11.override_minImageCount;

   /* For IMMEDIATE and FIFO, most games work in a pipelined manner where the
    * can produce frames at a rate of 1/MAX(CPU duration, GPU duration), but
    * the render latency is CPU duration + GPU duration.
    *
    * This means that with scanout from pageflipping we need 3 frames to run
    * full speed:
    * 1) CPU rendering work
    * 2) GPU rendering work
    * 3) scanout
    *
    * Once we have a nonblocking acquire that returns a semaphore we can merge
    * 1 and 3. Hence the ideal implementation needs only 2 images, but games
    * cannot tellwe currently do not have an ideal implementation and that
    * hence they need to allocate 3 images. So let us do it for them.
    *
    * This is a tradeoff as it uses more memory than needed for non-fullscreen
    * and non-performance intensive applications.
    */
   return 3;
}

static VkResult
x11_surface_get_capabilities(VkIcdSurfaceBase *icd_surface,
                             struct wsi_device *wsi_device,
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

   if (!visual)
      return VK_ERROR_SURFACE_LOST_KHR;

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
      caps->currentExtent = (VkExtent2D) { UINT32_MAX, UINT32_MAX };
      caps->minImageExtent = (VkExtent2D) { 1, 1 };
      caps->maxImageExtent = (VkExtent2D) {
         wsi_device->maxImageDimension2D,
         wsi_device->maxImageDimension2D,
      };
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

   caps->minImageCount = x11_get_min_image_count(wsi_device);
   /* There is no real maximum */
   caps->maxImageCount = 0;

   caps->supportedTransforms = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
   caps->currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
   caps->maxImageArrayLayers = 1;
   caps->supportedUsageFlags =
      VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
      VK_IMAGE_USAGE_SAMPLED_BIT |
      VK_IMAGE_USAGE_TRANSFER_DST_BIT |
      VK_IMAGE_USAGE_STORAGE_BIT |
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

   return VK_SUCCESS;
}

static VkResult
x11_surface_get_capabilities2(VkIcdSurfaceBase *icd_surface,
                              struct wsi_device *wsi_device,
                              const void *info_next,
                              VkSurfaceCapabilities2KHR *caps)
{
   assert(caps->sType == VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR);

   VkResult result =
      x11_surface_get_capabilities(icd_surface, wsi_device,
                                   &caps->surfaceCapabilities);

   vk_foreach_struct(ext, caps->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_SURFACE_PROTECTED_CAPABILITIES_KHR: {
         VkSurfaceProtectedCapabilitiesKHR *protected = (void *)ext;
         protected->supportsProtected = VK_FALSE;
         break;
      }

      default:
         /* Ignored */
         break;
      }
   }

   return result;
}

static void
get_sorted_vk_formats(struct wsi_device *wsi_device, VkFormat *sorted_formats)
{
   memcpy(sorted_formats, formats, sizeof(formats));

   if (wsi_device->force_bgra8_unorm_first) {
      for (unsigned i = 0; i < ARRAY_SIZE(formats); i++) {
         if (sorted_formats[i] == VK_FORMAT_B8G8R8A8_UNORM) {
            sorted_formats[i] = sorted_formats[0];
            sorted_formats[0] = VK_FORMAT_B8G8R8A8_UNORM;
            break;
         }
      }
   }
}

static VkResult
x11_surface_get_formats(VkIcdSurfaceBase *surface,
                        struct wsi_device *wsi_device,
                        uint32_t *pSurfaceFormatCount,
                        VkSurfaceFormatKHR *pSurfaceFormats)
{
   VK_OUTARRAY_MAKE(out, pSurfaceFormats, pSurfaceFormatCount);

   VkFormat sorted_formats[ARRAY_SIZE(formats)];
   get_sorted_vk_formats(wsi_device, sorted_formats);

   for (unsigned i = 0; i < ARRAY_SIZE(sorted_formats); i++) {
      vk_outarray_append(&out, f) {
         f->format = sorted_formats[i];
         f->colorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
      }
   }

   return vk_outarray_status(&out);
}

static VkResult
x11_surface_get_formats2(VkIcdSurfaceBase *surface,
                        struct wsi_device *wsi_device,
                        const void *info_next,
                        uint32_t *pSurfaceFormatCount,
                        VkSurfaceFormat2KHR *pSurfaceFormats)
{
   VK_OUTARRAY_MAKE(out, pSurfaceFormats, pSurfaceFormatCount);

   VkFormat sorted_formats[ARRAY_SIZE(formats)];
   get_sorted_vk_formats(wsi_device, sorted_formats);

   for (unsigned i = 0; i < ARRAY_SIZE(sorted_formats); i++) {
      vk_outarray_append(&out, f) {
         assert(f->sType == VK_STRUCTURE_TYPE_SURFACE_FORMAT_2_KHR);
         f->surfaceFormat.format = sorted_formats[i];
         f->surfaceFormat.colorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
      }
   }

   return vk_outarray_status(&out);
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

   *pPresentModeCount = MIN2(*pPresentModeCount, ARRAY_SIZE(present_modes));
   typed_memcpy(pPresentModes, present_modes, *pPresentModeCount);

   return *pPresentModeCount < ARRAY_SIZE(present_modes) ?
      VK_INCOMPLETE : VK_SUCCESS;
}

static VkResult
x11_surface_get_present_rectangles(VkIcdSurfaceBase *icd_surface,
                                   struct wsi_device *wsi_device,
                                   uint32_t* pRectCount,
                                   VkRect2D* pRects)
{
   xcb_connection_t *conn = x11_surface_get_connection(icd_surface);
   xcb_window_t window = x11_surface_get_window(icd_surface);
   VK_OUTARRAY_MAKE(out, pRects, pRectCount);

   vk_outarray_append(&out, rect) {
      xcb_generic_error_t *err = NULL;
      xcb_get_geometry_cookie_t geom_cookie = xcb_get_geometry(conn, window);
      xcb_get_geometry_reply_t *geom =
         xcb_get_geometry_reply(conn, geom_cookie, &err);
      free(err);
      if (geom) {
         *rect = (VkRect2D) {
            .offset = { 0, 0 },
            .extent = { geom->width, geom->height },
         };
      } else {
         /* This can happen if the client didn't wait for the configure event
          * to come back from the compositor.  In that case, we don't know the
          * size of the window so we just return valid "I don't know" stuff.
          */
         *rect = (VkRect2D) {
            .offset = { 0, 0 },
            .extent = { UINT32_MAX, UINT32_MAX },
         };
      }
      free(geom);
   }

   return vk_outarray_status(&out);
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

   *pSurface = VkIcdSurfaceBase_to_handle(&surface->base);
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

   *pSurface = VkIcdSurfaceBase_to_handle(&surface->base);
   return VK_SUCCESS;
}

struct x11_image {
   struct wsi_image                          base;
   xcb_pixmap_t                              pixmap;
   bool                                      busy;
   struct xshmfence *                        shm_fence;
   uint32_t                                  sync_fence;
};

struct x11_swapchain {
   struct wsi_swapchain                        base;

   bool                                         has_dri3_modifiers;

   xcb_connection_t *                           conn;
   xcb_window_t                                 window;
   xcb_gc_t                                     gc;
   uint32_t                                     depth;
   VkExtent2D                                   extent;

   xcb_present_event_t                          event_id;
   xcb_special_event_t *                        special_event;
   uint64_t                                     send_sbc;
   uint64_t                                     last_present_msc;
   uint32_t                                     stamp;
   int                                          sent_image_count;

   bool                                         has_present_queue;
   bool                                         has_acquire_queue;
   VkResult                                     status;
   xcb_present_complete_mode_t                  last_present_mode;
   struct wsi_queue                             present_queue;
   struct wsi_queue                             acquire_queue;
   pthread_t                                    queue_manager;

   struct x11_image                             images[0];
};
VK_DEFINE_NONDISP_HANDLE_CASTS(x11_swapchain, base.base, VkSwapchainKHR,
                               VK_OBJECT_TYPE_SWAPCHAIN_KHR)

/**
 * Update the swapchain status with the result of an operation, and return
 * the combined status. The chain status will eventually be returned from
 * AcquireNextImage and QueuePresent.
 *
 * We make sure to 'stick' more pessimistic statuses: an out-of-date error
 * is permanent once seen, and every subsequent call will return this. If
 * this has not been seen, success will be returned.
 */
static VkResult
_x11_swapchain_result(struct x11_swapchain *chain, VkResult result,
                      const char *file, int line)
{
   /* Prioritise returning existing errors for consistency. */
   if (chain->status < 0)
      return chain->status;

   /* If we have a new error, mark it as permanent on the chain and return. */
   if (result < 0) {
#ifndef NDEBUG
      fprintf(stderr, "%s:%d: Swapchain status changed to %s\n",
              file, line, vk_Result_to_str(result));
#endif
      chain->status = result;
      return result;
   }

   /* Return temporary errors, but don't persist them. */
   if (result == VK_TIMEOUT || result == VK_NOT_READY)
      return result;

   /* Suboptimal isn't an error, but is a status which sticks to the swapchain
    * and is always returned rather than success.
    */
   if (result == VK_SUBOPTIMAL_KHR) {
#ifndef NDEBUG
      if (chain->status != VK_SUBOPTIMAL_KHR) {
         fprintf(stderr, "%s:%d: Swapchain status changed to %s\n",
                 file, line, vk_Result_to_str(result));
      }
#endif
      chain->status = result;
      return result;
   }

   /* No changes, so return the last status. */
   return chain->status;
}
#define x11_swapchain_result(chain, result) \
   _x11_swapchain_result(chain, result, __FILE__, __LINE__)

static struct wsi_image *
x11_get_wsi_image(struct wsi_swapchain *wsi_chain, uint32_t image_index)
{
   struct x11_swapchain *chain = (struct x11_swapchain *)wsi_chain;
   return &chain->images[image_index].base;
}

/**
 * Process an X11 Present event. Does not update chain->status.
 */
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

      for (unsigned i = 0; i < chain->base.image_count; i++) {
         if (chain->images[i].pixmap == idle->pixmap) {
            chain->images[i].busy = false;
            chain->sent_image_count--;
            assert(chain->sent_image_count >= 0);
            if (chain->has_acquire_queue)
               wsi_queue_push(&chain->acquire_queue, i);
            break;
         }
      }

      break;
   }

   case XCB_PRESENT_EVENT_COMPLETE_NOTIFY: {
      xcb_present_complete_notify_event_t *complete = (void *) event;
      if (complete->kind == XCB_PRESENT_COMPLETE_KIND_PIXMAP)
         chain->last_present_msc = complete->msc;

      VkResult result = VK_SUCCESS;

      /* The winsys is now trying to flip directly and cannot due to our
       * configuration. Request the user reallocate.
       */
#ifdef HAVE_DRI3_MODIFIERS
      if (complete->mode == XCB_PRESENT_COMPLETE_MODE_SUBOPTIMAL_COPY &&
          chain->last_present_mode != XCB_PRESENT_COMPLETE_MODE_SUBOPTIMAL_COPY)
         result = VK_SUBOPTIMAL_KHR;
#endif

      /* When we go from flipping to copying, the odds are very likely that
       * we could reallocate in a more optimal way if we didn't have to care
       * about scanout, so we always do this.
       */
      if (complete->mode == XCB_PRESENT_COMPLETE_MODE_COPY &&
          chain->last_present_mode == XCB_PRESENT_COMPLETE_MODE_FLIP)
         result = VK_SUBOPTIMAL_KHR;

      chain->last_present_mode = complete->mode;
      return result;
   }

   default:
      break;
   }

   return VK_SUCCESS;
}


static uint64_t wsi_get_absolute_timeout(uint64_t timeout)
{
   uint64_t current_time = wsi_common_get_current_time();

   timeout = MIN2(UINT64_MAX - current_time, timeout);

   return current_time + timeout;
}

static VkResult
x11_acquire_next_image_poll_x11(struct x11_swapchain *chain,
                                uint32_t *image_index, uint64_t timeout)
{
   xcb_generic_event_t *event;
   struct pollfd pfds;
   uint64_t atimeout;
   while (1) {
      for (uint32_t i = 0; i < chain->base.image_count; i++) {
         if (!chain->images[i].busy) {
            /* We found a non-busy image */
            xshmfence_await(chain->images[i].shm_fence);
            *image_index = i;
            chain->images[i].busy = true;
            return x11_swapchain_result(chain, VK_SUCCESS);
         }
      }

      xcb_flush(chain->conn);

      if (timeout == UINT64_MAX) {
         event = xcb_wait_for_special_event(chain->conn, chain->special_event);
         if (!event)
            return x11_swapchain_result(chain, VK_ERROR_OUT_OF_DATE_KHR);
      } else {
         event = xcb_poll_for_special_event(chain->conn, chain->special_event);
         if (!event) {
            int ret;
            if (timeout == 0)
               return x11_swapchain_result(chain, VK_NOT_READY);

            atimeout = wsi_get_absolute_timeout(timeout);

            pfds.fd = xcb_get_file_descriptor(chain->conn);
            pfds.events = POLLIN;
            ret = poll(&pfds, 1, timeout / 1000 / 1000);
            if (ret == 0)
               return x11_swapchain_result(chain, VK_TIMEOUT);
            if (ret == -1)
               return x11_swapchain_result(chain, VK_ERROR_OUT_OF_DATE_KHR);

            /* If a non-special event happens, the fd will still
             * poll. So recalculate the timeout now just in case.
             */
            uint64_t current_time = wsi_common_get_current_time();
            if (atimeout > current_time)
               timeout = atimeout - current_time;
            else
               timeout = 0;
            continue;
         }
      }

      /* Update the swapchain status here. We may catch non-fatal errors here,
       * in which case we need to update the status and continue.
       */
      VkResult result = x11_handle_dri3_present_event(chain, (void *)event);
      free(event);
      if (result < 0)
         return x11_swapchain_result(chain, result);
   }
}

static VkResult
x11_acquire_next_image_from_queue(struct x11_swapchain *chain,
                                  uint32_t *image_index_out, uint64_t timeout)
{
   assert(chain->has_acquire_queue);

   uint32_t image_index;
   VkResult result = wsi_queue_pull(&chain->acquire_queue,
                                    &image_index, timeout);
   if (result < 0 || result == VK_TIMEOUT) {
      /* On error, the thread has shut down, so safe to update chain->status.
       * Calling x11_swapchain_result with VK_TIMEOUT won't modify
       * chain->status so that is also safe.
       */
      return x11_swapchain_result(chain, result);
   } else if (chain->status < 0) {
      return chain->status;
   }

   assert(image_index < chain->base.image_count);
   xshmfence_await(chain->images[image_index].shm_fence);

   *image_index_out = image_index;

   return chain->status;
}

static VkResult
x11_present_to_x11_dri3(struct x11_swapchain *chain, uint32_t image_index,
                        uint32_t target_msc)
{
   struct x11_image *image = &chain->images[image_index];

   assert(image_index < chain->base.image_count);

   uint32_t options = XCB_PRESENT_OPTION_NONE;

   int64_t divisor = 0;
   int64_t remainder = 0;

   if (chain->base.present_mode == VK_PRESENT_MODE_IMMEDIATE_KHR ||
       chain->base.present_mode == VK_PRESENT_MODE_FIFO_RELAXED_KHR)
      options |= XCB_PRESENT_OPTION_ASYNC;

#ifdef HAVE_DRI3_MODIFIERS
   if (chain->has_dri3_modifiers)
      options |= XCB_PRESENT_OPTION_SUBOPTIMAL;
#endif

   /* Poll for any available event and update the swapchain status. This could
    * update the status of the swapchain to SUBOPTIMAL or OUT_OF_DATE if the
    * associated X11 surface has been resized.
    */
   xcb_generic_event_t *event;
   while ((event = xcb_poll_for_special_event(chain->conn, chain->special_event))) {
      VkResult result = x11_handle_dri3_present_event(chain, (void *)event);
      free(event);
      if (result < 0)
         return x11_swapchain_result(chain, result);
      x11_swapchain_result(chain, result);
   }

   xshmfence_reset(image->shm_fence);

   ++chain->sent_image_count;
   assert(chain->sent_image_count <= chain->base.image_count);

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

   xcb_flush(chain->conn);

   return x11_swapchain_result(chain, VK_SUCCESS);
}

static VkResult
x11_present_to_x11_sw(struct x11_swapchain *chain, uint32_t image_index,
                      uint32_t target_msc)
{
   struct x11_image *image = &chain->images[image_index];

   xcb_void_cookie_t cookie;
   void *myptr;
   chain->base.wsi->MapMemory(chain->base.device,
                              image->base.memory,
                              0, 0, 0, &myptr);

   cookie = xcb_put_image(chain->conn, XCB_IMAGE_FORMAT_Z_PIXMAP,
                          chain->window,
                          chain->gc,
			  image->base.row_pitches[0] / 4,
                          chain->extent.height,
                          0,0,0,24,
                          image->base.row_pitches[0] * chain->extent.height,
                          myptr);

   chain->base.wsi->UnmapMemory(chain->base.device, image->base.memory);
   xcb_discard_reply(chain->conn, cookie.sequence);
   xcb_flush(chain->conn);
   return x11_swapchain_result(chain, VK_SUCCESS);
}
static VkResult
x11_present_to_x11(struct x11_swapchain *chain, uint32_t image_index,
                   uint32_t target_msc)
{
   if (chain->base.wsi->sw)
      return x11_present_to_x11_sw(chain, image_index, target_msc);
   return x11_present_to_x11_dri3(chain, image_index, target_msc);
}

static VkResult
x11_acquire_next_image(struct wsi_swapchain *anv_chain,
                       const VkAcquireNextImageInfoKHR *info,
                       uint32_t *image_index)
{
   struct x11_swapchain *chain = (struct x11_swapchain *)anv_chain;
   uint64_t timeout = info->timeout;

   /* If the swapchain is in an error state, don't go any further. */
   if (chain->status < 0)
      return chain->status;

   if (chain->base.wsi->sw) {
      *image_index = 0;
      return VK_SUCCESS;
   }
   if (chain->has_acquire_queue) {
      return x11_acquire_next_image_from_queue(chain, image_index, timeout);
   } else {
      return x11_acquire_next_image_poll_x11(chain, image_index, timeout);
   }
}

static VkResult
x11_queue_present(struct wsi_swapchain *anv_chain,
                  uint32_t image_index,
                  const VkPresentRegionKHR *damage)
{
   struct x11_swapchain *chain = (struct x11_swapchain *)anv_chain;

   /* If the swapchain is in an error state, don't go any further. */
   if (chain->status < 0)
      return chain->status;

   chain->images[image_index].busy = true;
   if (chain->has_present_queue) {
      wsi_queue_push(&chain->present_queue, image_index);
      return chain->status;
   } else {
      return x11_present_to_x11(chain, image_index, 0);
   }
}

static void *
x11_manage_fifo_queues(void *state)
{
   struct x11_swapchain *chain = state;
   VkResult result = VK_SUCCESS;

   assert(chain->has_present_queue);
   while (chain->status >= 0) {
      /* We can block here unconditionally because after an image was sent to
       * the server (later on in this loop) we ensure at least one image is
       * acquirable by the consumer or wait there on such an event.
       */
      uint32_t image_index = 0;
      result = wsi_queue_pull(&chain->present_queue, &image_index, INT64_MAX);
      assert(result != VK_TIMEOUT);
      if (result < 0) {
         goto fail;
      } else if (chain->status < 0) {
         /* The status can change underneath us if the swapchain is destroyed
          * from another thread.
          */
         return NULL;
      }

      if (chain->base.present_mode == VK_PRESENT_MODE_MAILBOX_KHR) {
         result = chain->base.wsi->WaitForFences(chain->base.device, 1,
                                        &chain->base.fences[image_index],
                                        true, UINT64_MAX);
         if (result != VK_SUCCESS) {
            result = VK_ERROR_OUT_OF_DATE_KHR;
            goto fail;
         }
      }

      uint64_t target_msc = 0;
      if (chain->has_acquire_queue)
         target_msc = chain->last_present_msc + 1;

      result = x11_present_to_x11(chain, image_index, target_msc);
      if (result < 0)
         goto fail;

      if (chain->has_acquire_queue) {
         /* Wait for our presentation to occur and ensure we have at least one
          * image that can be acquired by the client afterwards. This ensures we
          * can pull on the present-queue on the next loop.
          */
         while (chain->last_present_msc < target_msc ||
                chain->sent_image_count == chain->base.image_count) {
            xcb_generic_event_t *event =
               xcb_wait_for_special_event(chain->conn, chain->special_event);
            if (!event) {
               result = VK_ERROR_OUT_OF_DATE_KHR;
               goto fail;
            }

            result = x11_handle_dri3_present_event(chain, (void *)event);
            free(event);
            if (result < 0)
               goto fail;
         }
      }
   }

fail:
   x11_swapchain_result(chain, result);
   if (chain->has_acquire_queue)
      wsi_queue_push(&chain->acquire_queue, UINT32_MAX);

   return NULL;
}

static VkResult
x11_image_init(VkDevice device_h, struct x11_swapchain *chain,
               const VkSwapchainCreateInfoKHR *pCreateInfo,
               const VkAllocationCallbacks* pAllocator,
               const uint64_t *const *modifiers,
               const uint32_t *num_modifiers,
               int num_tranches, struct x11_image *image)
{
   xcb_void_cookie_t cookie;
   VkResult result;
   uint32_t bpp = 32;

   if (chain->base.use_prime_blit) {
      bool use_modifier = num_tranches > 0;
      result = wsi_create_prime_image(&chain->base, pCreateInfo, use_modifier, &image->base);
   } else {
      result = wsi_create_native_image(&chain->base, pCreateInfo,
                                       num_tranches, num_modifiers, modifiers,
                                       &image->base);
   }
   if (result < 0)
      return result;

   if (chain->base.wsi->sw) {
      image->busy = false;
      return VK_SUCCESS;
   }
   image->pixmap = xcb_generate_id(chain->conn);

#ifdef HAVE_DRI3_MODIFIERS
   if (image->base.drm_modifier != DRM_FORMAT_MOD_INVALID) {
      /* If the image has a modifier, we must have DRI3 v1.2. */
      assert(chain->has_dri3_modifiers);

      cookie =
         xcb_dri3_pixmap_from_buffers_checked(chain->conn,
                                              image->pixmap,
                                              chain->window,
                                              image->base.num_planes,
                                              pCreateInfo->imageExtent.width,
                                              pCreateInfo->imageExtent.height,
                                              image->base.row_pitches[0],
                                              image->base.offsets[0],
                                              image->base.row_pitches[1],
                                              image->base.offsets[1],
                                              image->base.row_pitches[2],
                                              image->base.offsets[2],
                                              image->base.row_pitches[3],
                                              image->base.offsets[3],
                                              chain->depth, bpp,
                                              image->base.drm_modifier,
                                              image->base.fds);
   } else
#endif
   {
      /* Without passing modifiers, we can't have multi-plane RGB images. */
      assert(image->base.num_planes == 1);

      cookie =
         xcb_dri3_pixmap_from_buffer_checked(chain->conn,
                                             image->pixmap,
                                             chain->window,
                                             image->base.sizes[0],
                                             pCreateInfo->imageExtent.width,
                                             pCreateInfo->imageExtent.height,
                                             image->base.row_pitches[0],
                                             chain->depth, bpp,
                                             image->base.fds[0]);
   }

   xcb_discard_reply(chain->conn, cookie.sequence);

   /* XCB has now taken ownership of the FDs. */
   for (int i = 0; i < image->base.num_planes; i++)
      image->base.fds[i] = -1;

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

   wsi_destroy_image(&chain->base, &image->base);

   return result;
}

static void
x11_image_finish(struct x11_swapchain *chain,
                 const VkAllocationCallbacks* pAllocator,
                 struct x11_image *image)
{
   xcb_void_cookie_t cookie;

   if (!chain->base.wsi->sw) {
      cookie = xcb_sync_destroy_fence(chain->conn, image->sync_fence);
      xcb_discard_reply(chain->conn, cookie.sequence);
      xshmfence_unmap_shm(image->shm_fence);

      cookie = xcb_free_pixmap(chain->conn, image->pixmap);
      xcb_discard_reply(chain->conn, cookie.sequence);
   }

   wsi_destroy_image(&chain->base, &image->base);
}

static void
wsi_x11_get_dri3_modifiers(struct wsi_x11_connection *wsi_conn,
                           xcb_connection_t *conn, xcb_window_t window,
                           uint8_t depth, uint8_t bpp,
                           VkCompositeAlphaFlagsKHR vk_alpha,
                           uint64_t **modifiers_in, uint32_t *num_modifiers_in,
                           uint32_t *num_tranches_in,
                           const VkAllocationCallbacks *pAllocator)
{
   if (!wsi_conn->has_dri3_modifiers)
      goto out;

#ifdef HAVE_DRI3_MODIFIERS
   xcb_generic_error_t *error = NULL;
   xcb_dri3_get_supported_modifiers_cookie_t mod_cookie =
      xcb_dri3_get_supported_modifiers(conn, window, depth, bpp);
   xcb_dri3_get_supported_modifiers_reply_t *mod_reply =
      xcb_dri3_get_supported_modifiers_reply(conn, mod_cookie, &error);
   free(error);

   if (!mod_reply || (mod_reply->num_window_modifiers == 0 &&
                      mod_reply->num_screen_modifiers == 0)) {
      free(mod_reply);
      goto out;
   }

   uint32_t n = 0;
   uint32_t counts[2];
   uint64_t *modifiers[2];

   if (mod_reply->num_window_modifiers) {
      counts[n] = mod_reply->num_window_modifiers;
      modifiers[n] = vk_alloc(pAllocator,
                              counts[n] * sizeof(uint64_t),
                              8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
      if (!modifiers[n]) {
         free(mod_reply);
         goto out;
      }

      memcpy(modifiers[n],
             xcb_dri3_get_supported_modifiers_window_modifiers(mod_reply),
             counts[n] * sizeof(uint64_t));
      n++;
   }

   if (mod_reply->num_screen_modifiers) {
      counts[n] = mod_reply->num_screen_modifiers;
      modifiers[n] = vk_alloc(pAllocator,
                              counts[n] * sizeof(uint64_t),
                              8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
      if (!modifiers[n]) {
	 if (n > 0)
            vk_free(pAllocator, modifiers[0]);
         free(mod_reply);
         goto out;
      }

      memcpy(modifiers[n],
             xcb_dri3_get_supported_modifiers_screen_modifiers(mod_reply),
             counts[n] * sizeof(uint64_t));
      n++;
   }

   for (int i = 0; i < n; i++) {
      modifiers_in[i] = modifiers[i];
      num_modifiers_in[i] = counts[i];
   }
   *num_tranches_in = n;

   free(mod_reply);
   return;
#endif
out:
   *num_tranches_in = 0;
}

static VkResult
x11_swapchain_destroy(struct wsi_swapchain *anv_chain,
                      const VkAllocationCallbacks *pAllocator)
{
   struct x11_swapchain *chain = (struct x11_swapchain *)anv_chain;
   xcb_void_cookie_t cookie;

   if (chain->has_present_queue) {
      chain->status = VK_ERROR_OUT_OF_DATE_KHR;
      /* Push a UINT32_MAX to wake up the manager */
      wsi_queue_push(&chain->present_queue, UINT32_MAX);
      pthread_join(chain->queue_manager, NULL);

      if (chain->has_acquire_queue)
         wsi_queue_destroy(&chain->acquire_queue);
      wsi_queue_destroy(&chain->present_queue);
   }

   for (uint32_t i = 0; i < chain->base.image_count; i++)
      x11_image_finish(chain, pAllocator, &chain->images[i]);

   xcb_unregister_for_special_event(chain->conn, chain->special_event);
   cookie = xcb_present_select_input_checked(chain->conn, chain->event_id,
                                             chain->window,
                                             XCB_PRESENT_EVENT_MASK_NO_EVENT);
   xcb_discard_reply(chain->conn, cookie.sequence);

   wsi_swapchain_finish(&chain->base);

   vk_free(pAllocator, chain);

   return VK_SUCCESS;
}

static void
wsi_x11_set_adaptive_sync_property(xcb_connection_t *conn,
                                   xcb_drawable_t drawable,
                                   uint32_t state)
{
   static char const name[] = "_VARIABLE_REFRESH";
   xcb_intern_atom_cookie_t cookie;
   xcb_intern_atom_reply_t* reply;
   xcb_void_cookie_t check;

   cookie = xcb_intern_atom(conn, 0, strlen(name), name);
   reply = xcb_intern_atom_reply(conn, cookie, NULL);
   if (reply == NULL)
      return;

   if (state)
      check = xcb_change_property_checked(conn, XCB_PROP_MODE_REPLACE,
                                          drawable, reply->atom,
                                          XCB_ATOM_CARDINAL, 32, 1, &state);
   else
      check = xcb_delete_property_checked(conn, drawable, reply->atom);

   xcb_discard_reply(conn, check.sequence);
   free(reply);
}


static VkResult
x11_surface_create_swapchain(VkIcdSurfaceBase *icd_surface,
                             VkDevice device,
                             struct wsi_device *wsi_device,
                             const VkSwapchainCreateInfoKHR *pCreateInfo,
                             const VkAllocationCallbacks* pAllocator,
                             struct wsi_swapchain **swapchain_out)
{
   struct x11_swapchain *chain;
   xcb_void_cookie_t cookie;
   VkResult result;
   VkPresentModeKHR present_mode = wsi_swapchain_get_present_mode(wsi_device, pCreateInfo);

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR);

   unsigned num_images = pCreateInfo->minImageCount;
   if (wsi_device->x11.strict_imageCount)
      num_images = pCreateInfo->minImageCount;
   else if (present_mode == VK_PRESENT_MODE_MAILBOX_KHR)
      num_images = MAX2(num_images, 5);
   else if (wsi_device->x11.ensure_minImageCount)
      num_images = MAX2(num_images, x11_get_min_image_count(wsi_device));

   xcb_connection_t *conn = x11_surface_get_connection(icd_surface);
   struct wsi_x11_connection *wsi_conn =
      wsi_x11_get_connection(wsi_device, conn);
   if (!wsi_conn)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   /* Check for whether or not we have a window up-front */
   xcb_window_t window = x11_surface_get_window(icd_surface);
   xcb_get_geometry_reply_t *geometry =
      xcb_get_geometry_reply(conn, xcb_get_geometry(conn, window), NULL);
   if (geometry == NULL)
      return VK_ERROR_SURFACE_LOST_KHR;
   const uint32_t bit_depth = geometry->depth;
   free(geometry);

   size_t size = sizeof(*chain) + num_images * sizeof(chain->images[0]);
   chain = vk_alloc(pAllocator, size, 8,
                      VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (chain == NULL)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   result = wsi_swapchain_init(wsi_device, &chain->base, device,
                               pCreateInfo, pAllocator);
   if (result != VK_SUCCESS)
      goto fail_alloc;

   chain->base.destroy = x11_swapchain_destroy;
   chain->base.get_wsi_image = x11_get_wsi_image;
   chain->base.acquire_next_image = x11_acquire_next_image;
   chain->base.queue_present = x11_queue_present;
   chain->base.present_mode = present_mode;
   chain->base.image_count = num_images;
   chain->conn = conn;
   chain->window = window;
   chain->depth = bit_depth;
   chain->extent = pCreateInfo->imageExtent;
   chain->send_sbc = 0;
   chain->sent_image_count = 0;
   chain->last_present_msc = 0;
   chain->has_acquire_queue = false;
   chain->has_present_queue = false;
   chain->status = VK_SUCCESS;
   chain->has_dri3_modifiers = wsi_conn->has_dri3_modifiers;

   /* If we are reallocating from an old swapchain, then we inherit its
    * last completion mode, to ensure we don't get into reallocation
    * cycles. If we are starting anew, we set 'COPY', as that is the only
    * mode which provokes reallocation when anything changes, to make
    * sure we have the most optimal allocation.
    */
   VK_FROM_HANDLE(x11_swapchain, old_chain, pCreateInfo->oldSwapchain);
   if (old_chain)
      chain->last_present_mode = old_chain->last_present_mode;
   else
      chain->last_present_mode = XCB_PRESENT_COMPLETE_MODE_COPY;

   if (!wsi_device->sw)
      if (!wsi_x11_check_dri3_compatible(wsi_device, conn))
         chain->base.use_prime_blit = true;

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

   uint64_t *modifiers[2] = {NULL, NULL};
   uint32_t num_modifiers[2] = {0, 0};
   uint32_t num_tranches = 0;
   if (wsi_device->supports_modifiers)
      wsi_x11_get_dri3_modifiers(wsi_conn, conn, window, chain->depth, 32,
                                 pCreateInfo->compositeAlpha,
                                 modifiers, num_modifiers, &num_tranches,
                                 pAllocator);

   uint32_t image = 0;
   for (; image < chain->base.image_count; image++) {
      result = x11_image_init(device, chain, pCreateInfo, pAllocator,
                              (const uint64_t *const *)modifiers,
                              num_modifiers, num_tranches,
                              &chain->images[image]);
      if (result != VK_SUCCESS)
         goto fail_init_images;
   }

   if ((chain->base.present_mode == VK_PRESENT_MODE_FIFO_KHR ||
       chain->base.present_mode == VK_PRESENT_MODE_FIFO_RELAXED_KHR ||
       chain->base.present_mode == VK_PRESENT_MODE_MAILBOX_KHR) && !chain->base.wsi->sw) {
      chain->has_present_queue = true;

      /* Initialize our queues.  We make them base.image_count + 1 because we will
       * occasionally use UINT32_MAX to signal the other thread that an error
       * has occurred and we don't want an overflow.
       */
      int ret;
      ret = wsi_queue_init(&chain->present_queue, chain->base.image_count + 1);
      if (ret) {
         goto fail_init_images;
      }

      if (chain->base.present_mode == VK_PRESENT_MODE_FIFO_KHR ||
          chain->base.present_mode == VK_PRESENT_MODE_FIFO_RELAXED_KHR) {
         chain->has_acquire_queue = true;

         ret = wsi_queue_init(&chain->acquire_queue, chain->base.image_count + 1);
         if (ret) {
            wsi_queue_destroy(&chain->present_queue);
            goto fail_init_images;
         }

         for (unsigned i = 0; i < chain->base.image_count; i++)
            wsi_queue_push(&chain->acquire_queue, i);
      }

      ret = pthread_create(&chain->queue_manager, NULL,
                           x11_manage_fifo_queues, chain);
      if (ret) {
         wsi_queue_destroy(&chain->present_queue);
         if (chain->has_acquire_queue)
            wsi_queue_destroy(&chain->acquire_queue);

         goto fail_init_images;
      }
   }

   assert(chain->has_present_queue || !chain->has_acquire_queue);

   for (int i = 0; i < ARRAY_SIZE(modifiers); i++)
      vk_free(pAllocator, modifiers[i]);

   /* It is safe to set it here as only one swapchain can be associated with
    * the window, and swapchain creation does the association. At this point
    * we know the creation is going to succeed. */
   wsi_x11_set_adaptive_sync_property(conn, window,
                                      wsi_device->enable_adaptive_sync);

   *swapchain_out = &chain->base;

   return VK_SUCCESS;

fail_init_images:
   for (uint32_t j = 0; j < image; j++)
      x11_image_finish(chain, pAllocator, &chain->images[j]);

   for (int i = 0; i < ARRAY_SIZE(modifiers); i++)
      vk_free(pAllocator, modifiers[i]);

fail_register:
   xcb_unregister_for_special_event(chain->conn, chain->special_event);

   wsi_swapchain_finish(&chain->base);

fail_alloc:
   vk_free(pAllocator, chain);

   return result;
}

VkResult
wsi_x11_init_wsi(struct wsi_device *wsi_device,
                 const VkAllocationCallbacks *alloc,
                 const struct driOptionCache *dri_options)
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

   if (dri_options) {
      if (driCheckOption(dri_options, "vk_x11_override_min_image_count", DRI_INT)) {
         wsi_device->x11.override_minImageCount =
            driQueryOptioni(dri_options, "vk_x11_override_min_image_count");
      }
      if (driCheckOption(dri_options, "vk_x11_strict_image_count", DRI_BOOL)) {
         wsi_device->x11.strict_imageCount =
            driQueryOptionb(dri_options, "vk_x11_strict_image_count");
      }
      if (driCheckOption(dri_options, "vk_x11_ensure_min_image_count", DRI_BOOL)) {
         wsi_device->x11.ensure_minImageCount =
            driQueryOptionb(dri_options, "vk_x11_ensure_min_image_count");
      }

   }

   wsi->base.get_support = x11_surface_get_support;
   wsi->base.get_capabilities2 = x11_surface_get_capabilities2;
   wsi->base.get_formats = x11_surface_get_formats;
   wsi->base.get_formats2 = x11_surface_get_formats2;
   wsi->base.get_present_modes = x11_surface_get_present_modes;
   wsi->base.get_present_rectangles = x11_surface_get_present_rectangles;
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
      hash_table_foreach(wsi->connections, entry)
         wsi_x11_connection_destroy(wsi_device, entry->data);

      _mesa_hash_table_destroy(wsi->connections, NULL);

      pthread_mutex_destroy(&wsi->mutex);

      vk_free(alloc, wsi);
   }
}
