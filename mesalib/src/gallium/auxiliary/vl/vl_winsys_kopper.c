/*
 * Copyright © 2024 Valve Corporation
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
 *
 * Authors:
 *    Mike Blumenkrantz <michael.blumenkrantz@gmail.com>
 */

#include "pipe-loader/pipe_loader.h"
#include "pipe/p_screen.h"
#include "util/u_memory.h"
#include "vl/vl_winsys.h"
#include "loader.h"
#include "pipe-loader/pipe_loader.h"
#include "vl/vl_compositor.h"
#if defined(HAVE_X11_PLATFORM) && defined(HAVE_LIBDRM)
#include <X11/Xlib-xcb.h>
#include "gallium/drivers/zink/zink_kopper.h"
#include <vulkan/vulkan_xcb.h>
#include "x11/loader_x11.h"
#endif
#include "zink_public.h"

struct vl_kopper_screen
{
   struct vl_screen base;
   struct pipe_context *pipe;
#if defined(HAVE_X11_PLATFORM) && defined(HAVE_LIBDRM)
   xcb_connection_t *conn;
   bool is_different_gpu;
   int fd;
   struct u_rect dirty_area;
   struct pipe_resource *drawable_texture;
#endif
   int screen;
};

static void
vl_screen_destroy(struct vl_screen *vscreen)
{
   if (vscreen == NULL)
      return;

   if (vscreen->pscreen)
      vscreen->pscreen->destroy(vscreen->pscreen);

   if (vscreen->dev)
      pipe_loader_release(&vscreen->dev, 1);

   FREE(vscreen);
}

static void
vl_kopper_screen_destroy(struct vl_screen *vscreen)
{
   if (vscreen == NULL)
      return;

   struct vl_kopper_screen *scrn = (struct vl_kopper_screen *) vscreen;

#if defined(HAVE_X11_PLATFORM) && defined(HAVE_LIBDRM)
   if (scrn->fd != -1)
      close(scrn->fd);
   if (scrn->drawable_texture)
      pipe_resource_reference(&scrn->drawable_texture, NULL);
#endif

   if (scrn->pipe)
      scrn->pipe->destroy(scrn->pipe);

   vl_screen_destroy(&scrn->base);
}

#if defined(HAVE_X11_PLATFORM) && defined(HAVE_LIBDRM)
static void *
vl_kopper_get_private(struct vl_screen *vscreen)
{
   return NULL;
}

static struct u_rect *
vl_kopper_get_dirty_area(struct vl_screen *vscreen)
{
   struct vl_kopper_screen *scrn = (struct vl_kopper_screen *) vscreen;
   return &scrn->dirty_area;
}

static struct pipe_resource *
vl_kopper_texture_from_drawable(struct vl_screen *vscreen, void *d)
{
   struct vl_kopper_screen *scrn = (struct vl_kopper_screen *) vscreen;
   Drawable drawable = (Drawable)d;
   xcb_get_geometry_cookie_t cookie;
   xcb_get_geometry_reply_t *reply;
   xcb_generic_error_t *error;
   int w, h;

   if (scrn->fd == -1 && scrn->drawable_texture) {
      zink_kopper_update(vscreen->pscreen, scrn->drawable_texture, &w, &h);
   } else {
      cookie = xcb_get_geometry(scrn->conn, drawable);
      reply = xcb_get_geometry_reply(scrn->conn, cookie, &error);
      w = reply->width, h = reply->height;
      free(reply);
   }

   bool needs_new_back_buffer_allocation = true;
   if (scrn->drawable_texture) {
      needs_new_back_buffer_allocation =
         (scrn->drawable_texture->width0 != w || scrn->drawable_texture->height0 != h);
   }

   if (needs_new_back_buffer_allocation) {
      struct kopper_loader_info info;
      VkXcbSurfaceCreateInfoKHR *xcb = (VkXcbSurfaceCreateInfoKHR *)&info.bos;
      xcb->sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
      xcb->pNext = NULL;
      xcb->flags = 0;
      xcb->connection = scrn->conn;
      xcb->window = drawable;
      info.has_alpha = scrn->base.color_depth == 32;

      if (scrn->drawable_texture)
         pipe_resource_reference(&scrn->drawable_texture, NULL);

      struct pipe_resource templat;
      memset(&templat, 0, sizeof(templat));
      templat.target = PIPE_TEXTURE_2D;
      templat.format = vl_dri2_format_for_depth(vscreen, scrn->base.color_depth);
      templat.width0 = w;
      templat.height0 = h;
      templat.depth0 = 1;
      templat.array_size = 1;
      templat.last_level = 0;
      templat.bind = (PIPE_BIND_RENDER_TARGET | PIPE_BIND_DISPLAY_TARGET | PIPE_BIND_SAMPLER_VIEW);

      scrn->drawable_texture = vscreen->pscreen->resource_create_drawable(vscreen->pscreen, &templat, &info);
      vl_compositor_reset_dirty_area(&scrn->dirty_area);
   } else {
      struct pipe_resource *drawable_texture = NULL;
      pipe_resource_reference(&drawable_texture, scrn->drawable_texture);
   }

   return scrn->drawable_texture;
}

struct vl_screen *
vl_kopper_screen_create_x11(Display *display, int screen)
{
   xcb_get_geometry_cookie_t geom_cookie;
   xcb_get_geometry_reply_t *geom_reply;
   struct vl_kopper_screen *scrn = CALLOC_STRUCT(vl_kopper_screen);
   bool err = false;
   if (!scrn)
      goto error;

   scrn->conn = XGetXCBConnection(display);
   if (!scrn->conn)
      goto error;

   int fd = x11_dri3_open(scrn->conn, RootWindow(display, screen), 0);
   bool explicit_modifiers = false;
   x11_dri3_check_multibuffer(scrn->conn, &err, &explicit_modifiers);
   if (fd < 0 || !explicit_modifiers) {
      goto error;
   }

   scrn->is_different_gpu = loader_get_user_preferred_fd(&fd, NULL);

   geom_cookie = xcb_get_geometry(scrn->conn, RootWindow(display, screen));
   geom_reply = xcb_get_geometry_reply(scrn->conn, geom_cookie, NULL);
   if (!geom_reply)
      goto error;

   scrn->base.xcb_screen = vl_dri_get_screen_for_root(scrn->conn, geom_reply->root);
   if (!scrn->base.xcb_screen) {
      free(geom_reply);
      goto error;
   }

   /* TODO support depth other than 24 or 30 */
   if (geom_reply->depth != 24 && geom_reply->depth != 30) {
      free(geom_reply);
      goto error;
   }
   scrn->base.color_depth = geom_reply->depth;
   free(geom_reply);

   scrn->fd = fd;
   bool success;
   if (fd != -1)
      success = pipe_loader_drm_probe_fd(&scrn->base.dev, fd, true);
   else
      success = pipe_loader_vk_probe_dri(&scrn->base.dev);

   if (success)
      pipe_loader_create_screen_vk(scrn->base.dev, false, false);
   if (!scrn->base.pscreen)
      goto error;

   scrn->base.get_private = vl_kopper_get_private;
   scrn->base.texture_from_drawable = vl_kopper_texture_from_drawable;
   scrn->base.get_dirty_area = vl_kopper_get_dirty_area;
   scrn->base.destroy = vl_kopper_screen_destroy;
   scrn->pipe = scrn->base.pscreen->context_create(scrn->base.pscreen, NULL, 0);

   vl_compositor_reset_dirty_area(&scrn->dirty_area);

   return &scrn->base;

error:
   vl_kopper_screen_destroy(&scrn->base);

   return NULL;
}
#endif

#ifdef _WIN32
struct vl_screen *
vl_kopper_screen_create_win32(LUID *luid)
{
   struct vl_kopper_screen *scrn = CALLOC_STRUCT(vl_kopper_screen);
   uint64_t adapter_luid = 0;

   if (luid)
      memcpy(&adapter_luid, luid, sizeof(adapter_luid));
   scrn->base.pscreen = zink_win32_create_screen(adapter_luid);
   if (!scrn->base.pscreen)
      goto error;

   scrn->base.destroy = vl_kopper_screen_destroy;

   scrn->pipe = scrn->base.pscreen->context_create(scrn->base.pscreen, NULL, 0);

   return &scrn->base;

error:
   vl_kopper_screen_destroy(&scrn->base);

   return NULL;
}
#endif
