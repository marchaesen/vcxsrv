/*
 * Copyright 2020 Red Hat, Inc.
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "mesa_interface.h"
#include "git_sha1.h"
#include "util/format/u_format.h"
#include "util/u_memory.h"
#include "util/u_inlines.h"
#include "util/box.h"
#include "util/log.h"
#include "pipe/p_context.h"
#include "pipe-loader/pipe_loader.h"
#include "state_tracker/st_context.h"
#include "zink/zink_public.h"
#include "zink/zink_kopper.h"

#include "dri_screen.h"
#include "dri_context.h"
#include "dri_drawable.h"
#include "dri_helpers.h"

#include <vulkan/vulkan.h>

#ifdef VK_USE_PLATFORM_XCB_KHR
#include <xcb/xcb.h>
#include <xcb/dri3.h>
#include <xcb/present.h>
#include <xcb/xfixes.h>
#include "util/libsync.h"
#include <X11/Xlib-xcb.h>
#include "drm-uapi/drm_fourcc.h"
#include "loader_dri3_helper.h"
#endif

static struct dri_drawable *
kopper_create_drawable(struct dri_screen *screen, const struct gl_config *visual,
                       bool isPixmap, void *loaderPrivate);

struct pipe_screen *
kopper_init_screen(struct dri_screen *screen, bool driver_name_is_inferred)
{
   struct pipe_screen *pscreen = NULL;

   if (!screen->kopper_loader) {
      fprintf(stderr, "mesa: Kopper interface not found!\n"
                      "      Ensure the versions of %s built with this version of Zink are\n"
                      "      in your library path!\n", KOPPER_LIB_NAMES);
      return NULL;
   }

   screen->can_share_buffer = true;

   bool success;
#ifdef HAVE_LIBDRM
   if (screen->fd != -1)
      success = pipe_loader_drm_probe_fd(&screen->dev, screen->fd, false);
   else
      success = pipe_loader_vk_probe_dri(&screen->dev);
#else
   success = pipe_loader_vk_probe_dri(&screen->dev);
#endif

   if (success)
      pscreen = pipe_loader_create_screen(screen->dev, driver_name_is_inferred);

   if (!pscreen)
      return NULL;

   assert(pscreen->caps.device_reset_status_query);
   screen->is_sw = zink_kopper_is_cpu(pscreen);

   return pscreen;
}

// copypasta alert

extern bool
dri_image_drawable_get_buffers(struct dri_drawable *drawable,
                               struct __DRIimageList *images,
                               const enum st_attachment_type *statts,
                               unsigned statts_count);

#ifdef VK_USE_PLATFORM_XCB_KHR
/* Translate from the pipe_format enums used by Gallium to the DRM FourCC
 * codes used by dmabuf import */
static int
pipe_format_to_fourcc(enum pipe_format format)
{
   switch (format) {
   case PIPE_FORMAT_BGRA8888_SRGB:      return __DRI_IMAGE_FOURCC_SABGR8888;
   case PIPE_FORMAT_BGRX8888_SRGB:      return __DRI_IMAGE_FOURCC_SXRGB8888;
   case PIPE_FORMAT_RGBA8888_SRGB:      return __DRI_IMAGE_FOURCC_SABGR8888;
   case PIPE_FORMAT_B5G6R5_UNORM:       return DRM_FORMAT_RGB565;
   case PIPE_FORMAT_BGRX8888_UNORM:     return DRM_FORMAT_XRGB8888;
   case PIPE_FORMAT_BGRA8888_UNORM:     return DRM_FORMAT_ARGB8888;
   case PIPE_FORMAT_RGBA8888_UNORM:     return DRM_FORMAT_ABGR8888;
   case PIPE_FORMAT_RGBX8888_UNORM:     return DRM_FORMAT_XBGR8888;
   case PIPE_FORMAT_B10G10R10X2_UNORM:  return DRM_FORMAT_XRGB2101010;
   case PIPE_FORMAT_B10G10R10A2_UNORM:  return DRM_FORMAT_ARGB2101010;
   case PIPE_FORMAT_R10G10B10X2_UNORM:  return DRM_FORMAT_XBGR2101010;
   case PIPE_FORMAT_R10G10B10A2_UNORM:  return DRM_FORMAT_ABGR2101010;
   case PIPE_FORMAT_R16G16B16A16_FLOAT: return DRM_FORMAT_XBGR16161616F;
   case PIPE_FORMAT_R16G16B16X16_FLOAT: return DRM_FORMAT_ABGR16161616F;
   case PIPE_FORMAT_B5G5R5A1_UNORM:     return DRM_FORMAT_ARGB1555;
   case PIPE_FORMAT_R5G5B5A1_UNORM:     return DRM_FORMAT_ABGR1555;
   case PIPE_FORMAT_B4G4R4A4_UNORM:     return DRM_FORMAT_ARGB4444;
   case PIPE_FORMAT_R4G4B4A4_UNORM:     return DRM_FORMAT_ABGR4444;
   default:                             return DRM_FORMAT_INVALID;
   }
}

/** kopper_get_pixmap_buffer
 *
 * Get the DRM object for a pixmap from the X server and
 * wrap that with a struct dri_image structure using createImageFromDmaBufs
 */
static struct pipe_resource *
kopper_get_pixmap_buffer(struct dri_drawable *drawable,
                         enum pipe_format pf)
{
   xcb_drawable_t                       pixmap;
   int                                  width;
   int                                  height;
   uint32_t fourcc = pipe_format_to_fourcc(pf);
   struct kopper_loader_info *info = &drawable->info;
   assert(info->bos.sType == VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR);
   VkXcbSurfaceCreateInfoKHR *xcb = (VkXcbSurfaceCreateInfoKHR *)&info->bos;
   xcb_connection_t *conn = xcb->connection;
   pixmap = xcb->window;

   if (drawable->image)
      return drawable->image->texture;

   /* FIXME: probably broken for OBS studio?
    * see dri3_get_pixmap_buffer()
    */
   struct dri_screen *screen = drawable->screen;

#ifndef GLX_USE_APPLE
   drawable->image = loader_dri3_get_pixmap_buffer(conn, pixmap, screen,
                                                   fourcc, drawable->screen->dmabuf_import, &width, &height, drawable);
#else
   drawable->image = NULL;
#endif
   if (!drawable->image)
      return NULL;

   drawable->w = width;
   drawable->h = height;

   return drawable->image->texture;
}
#endif //VK_USE_PLATFORM_XCB_KHR

static void
kopper_allocate_textures(struct dri_context *ctx,
                         struct dri_drawable *drawable,
                         const enum st_attachment_type *statts,
                         unsigned statts_count)
{
   struct dri_screen *screen = drawable->screen;
   struct pipe_resource templ;
   unsigned width, height;
   bool resized;
   unsigned i;
   struct __DRIimageList images;
   const __DRIimageLoaderExtension *image = screen->image.loader;

   bool is_window = drawable->is_window;
   bool is_pixmap = !is_window && drawable->info.bos.sType == VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;

   /* Wait for glthread to finish because we can't use pipe_context from
    * multiple threads.
    */
   _mesa_glthread_finish(ctx->st->ctx);

   /* First get the buffers from the loader */
   if (image) {
      if (!dri_image_drawable_get_buffers(drawable, &images,
                                          statts, statts_count))
         return;
   }

   if (image) {
      if (images.image_mask & __DRI_IMAGE_BUFFER_FRONT) {
         struct pipe_resource **buf =
            &drawable->textures[ST_ATTACHMENT_FRONT_LEFT];
         struct pipe_resource *texture = images.front->texture;

         drawable->w = texture->width0;
         drawable->h = texture->height0;

         pipe_resource_reference(buf, texture);
      }

      if (images.image_mask & __DRI_IMAGE_BUFFER_BACK) {
         struct pipe_resource **buf =
            &drawable->textures[ST_ATTACHMENT_BACK_LEFT];
         struct pipe_resource *texture = images.back->texture;

         drawable->w = texture->width0;
         drawable->h = texture->height0;

         pipe_resource_reference(buf, texture);
      }

      if (images.image_mask & __DRI_IMAGE_BUFFER_SHARED) {
         struct pipe_resource **buf =
            &drawable->textures[ST_ATTACHMENT_BACK_LEFT];
         struct pipe_resource *texture = images.back->texture;

         drawable->w = texture->width0;
         drawable->h = texture->height0;

         pipe_resource_reference(buf, texture);

         ctx->is_shared_buffer_bound = true;
      } else {
         ctx->is_shared_buffer_bound = false;
      }
   }

   /* check size after possible loader image resize */
   width  = drawable->w;
   height = drawable->h;

   resized = (drawable->old_w != width ||
              drawable->old_h != height);

   if (!image) {
      /* remove outdated textures */
      if (resized) {
         for (i = 0; i < ST_ATTACHMENT_COUNT; i++) {
            if (drawable->textures[i] && i < ST_ATTACHMENT_DEPTH_STENCIL && !is_pixmap) {
               drawable->textures[i]->width0 = width;
               drawable->textures[i]->height0 = height;
               /* force all contexts to revalidate framebuffer */
               p_atomic_inc(&drawable->base.stamp);
            } else
               pipe_resource_reference(&drawable->textures[i], NULL);
            pipe_resource_reference(&drawable->msaa_textures[i], NULL);
            if (is_pixmap && i == ST_ATTACHMENT_FRONT_LEFT) {
               FREE(drawable->image);
               drawable->image = NULL;
            }
         }
      }
   }

   drawable->old_w = width;
   drawable->old_h = height;

   memset(&templ, 0, sizeof(templ));
   templ.target = screen->target;
   templ.width0 = width;
   templ.height0 = height;
   templ.depth0 = 1;
   templ.array_size = 1;
   templ.last_level = 0;

#if 0
XXX do this once swapinterval is hooked up
   /* pixmaps always have front buffers.
    * Exchange swaps also mandate fake front buffers.
    */
   if (draw->type != LOADER_DRI3_DRAWABLE_WINDOW)
      buffer_mask |= __DRI_IMAGE_BUFFER_FRONT;
#endif

   uint32_t attachments = 0;
   for (i = 0; i < statts_count; i++)
      attachments |= BITFIELD_BIT(statts[i]);
   bool front_only = attachments & ST_ATTACHMENT_FRONT_LEFT_MASK && !(attachments & ST_ATTACHMENT_BACK_LEFT_MASK);

   for (i = 0; i < statts_count; i++) {
      enum pipe_format format;
      unsigned bind;

      dri_drawable_get_format(drawable, statts[i], &format, &bind);
      templ.format = format;

      /* the texture already exists or not requested */
      if (!drawable->textures[statts[i]]) {
         if (statts[i] == ST_ATTACHMENT_BACK_LEFT ||
             statts[i] == ST_ATTACHMENT_DEPTH_STENCIL ||
             (statts[i] == ST_ATTACHMENT_FRONT_LEFT && front_only))
            bind |= PIPE_BIND_DISPLAY_TARGET;

         if (format == PIPE_FORMAT_NONE)
            continue;

         templ.bind = bind;
         templ.nr_samples = 0;
         templ.nr_storage_samples = 0;

         if (statts[i] < ST_ATTACHMENT_DEPTH_STENCIL && is_window) {
            void *data;
            if (statts[i] == ST_ATTACHMENT_BACK_LEFT || (statts[i] == ST_ATTACHMENT_FRONT_LEFT && front_only))
               data = &drawable->info;
            else
               data = drawable->textures[ST_ATTACHMENT_BACK_LEFT];
            assert(data);
            drawable->textures[statts[i]] =
               screen->base.screen->resource_create_drawable(screen->base.screen, &templ, data);
            drawable->window_valid = !!drawable->textures[statts[i]];
         }
#ifdef VK_USE_PLATFORM_XCB_KHR
         else if (is_pixmap && statts[i] == ST_ATTACHMENT_FRONT_LEFT && !screen->is_sw) {
            drawable->textures[statts[i]] = kopper_get_pixmap_buffer(drawable, format);
            if (drawable->textures[statts[i]])
               dri_image_fence_sync(ctx, drawable->image);
         }
#endif
         if (!drawable->textures[statts[i]])
            drawable->textures[statts[i]] =
               screen->base.screen->resource_create(screen->base.screen, &templ);
      }
      if (drawable->stvis.samples > 1 && !drawable->msaa_textures[statts[i]]) {
         templ.bind = bind &
            ~(PIPE_BIND_SCANOUT | PIPE_BIND_SHARED | PIPE_BIND_DISPLAY_TARGET);
         templ.nr_samples = drawable->stvis.samples;
         templ.nr_storage_samples = drawable->stvis.samples;
         drawable->msaa_textures[statts[i]] =
            screen->base.screen->resource_create(screen->base.screen, &templ);

         dri_pipe_blit(ctx->st->pipe,
                       drawable->msaa_textures[statts[i]],
                       drawable->textures[statts[i]]);
      }
   }
}

static inline void
get_drawable_info(struct dri_drawable *drawable, int *x, int *y, int *w, int *h)
{
   const __DRIswrastLoaderExtension *loader = drawable->screen->swrast_loader;

   if (loader)
      loader->getDrawableInfo(drawable, x, y, w, h,
                              drawable->loaderPrivate);
}

static void
kopper_update_drawable_info(struct dri_drawable *drawable)
{
   struct dri_screen *screen = drawable->screen;
   bool is_window = drawable->info.bos.sType != 0;
   int x, y;
   struct pipe_resource *ptex = drawable->textures[ST_ATTACHMENT_BACK_LEFT] ?
                                drawable->textures[ST_ATTACHMENT_BACK_LEFT] :
                                drawable->textures[ST_ATTACHMENT_FRONT_LEFT];

   bool do_kopper_update = is_window && ptex && screen->fd == -1;
   if (drawable->info.bos.sType == VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR && do_kopper_update)
      zink_kopper_update(kopper_get_zink_screen(screen->base.screen), ptex, &drawable->w, &drawable->h);
   else
      get_drawable_info(drawable, &x, &y, &drawable->w, &drawable->h);
}

static inline void
kopper_present_texture(struct pipe_context *pipe, struct dri_drawable *drawable,
                      struct pipe_resource *ptex, unsigned nboxes, struct pipe_box *sub_box)
{
   struct dri_screen *screen = drawable->screen;

   screen->base.screen->flush_frontbuffer(screen->base.screen, pipe, ptex, 0, 0, drawable, nboxes, sub_box);
}

static inline void
kopper_copy_to_front(struct pipe_context *pipe,
                    struct dri_drawable *drawable,
                    struct pipe_resource *ptex,
                    unsigned nrects,
                    struct pipe_box *boxes)
{
   kopper_present_texture(pipe, drawable, ptex, nrects, boxes);

   drawable->lastStamp++;
   p_atomic_inc(&drawable->base.stamp);
}

static bool
kopper_flush_frontbuffer(struct dri_context *ctx,
                         struct dri_drawable *drawable,
                         enum st_attachment_type statt)
{
   struct pipe_resource *ptex;

   if (!ctx || statt != ST_ATTACHMENT_FRONT_LEFT)
      return false;

   /* Wait for glthread to finish because we can't use pipe_context from
    * multiple threads.
    */
   _mesa_glthread_finish(ctx->st->ctx);

   /* prevent recursion */
   if (drawable->flushing)
      return true;

   drawable->flushing = true;

   if (drawable->stvis.samples > 1) {
      /* Resolve the front buffer. */
      dri_pipe_blit(ctx->st->pipe,
                    drawable->textures[ST_ATTACHMENT_FRONT_LEFT],
                    drawable->msaa_textures[ST_ATTACHMENT_FRONT_LEFT]);
   }
   ptex = drawable->textures[statt];

   if (ptex) {
      ctx->st->pipe->flush_resource(ctx->st->pipe, drawable->textures[ST_ATTACHMENT_FRONT_LEFT]);
      struct pipe_screen *screen = drawable->screen->base.screen;
      struct st_context *st;
      struct pipe_fence_handle *new_fence = NULL;

      st = ctx->st;

      st_context_flush(st, ST_FLUSH_FRONT, &new_fence, NULL, NULL);
      drawable->flushing = false;

      /* throttle on the previous fence */
      if (drawable->throttle_fence) {
         screen->fence_finish(screen, NULL, drawable->throttle_fence, OS_TIMEOUT_INFINITE);
         screen->fence_reference(screen, &drawable->throttle_fence, NULL);
      }
      drawable->throttle_fence = new_fence;
      kopper_copy_to_front(st->pipe, ctx->draw, ptex, 0, NULL);
   }

   return true;
}

static inline void
get_image(struct dri_drawable *drawable, int x, int y, int width, int height, void *data)
{
   const __DRIswrastLoaderExtension *loader = drawable->screen->swrast_loader;

   loader->getImage(drawable, x, y, width, height,
                    data, drawable->loaderPrivate);
}

static inline bool
get_image_shm(struct dri_drawable *drawable, int x, int y, int width, int height,
              struct pipe_resource *res)
{
   const __DRIswrastLoaderExtension *loader = drawable->screen->swrast_loader;
   struct winsys_handle whandle;

   whandle.type = WINSYS_HANDLE_TYPE_SHMID;

   if (loader->base.version < 4 || !loader->getImageShm)
      return false;

   if (!res->screen->resource_get_handle(res->screen, NULL, res, &whandle, PIPE_HANDLE_USAGE_FRAMEBUFFER_WRITE))
      return false;

   if (loader->base.version > 5 && loader->getImageShm2)
      return loader->getImageShm2(drawable, x, y, width, height, whandle.handle, drawable->loaderPrivate);

   loader->getImageShm(drawable, x, y, width, height, whandle.handle, drawable->loaderPrivate);
   return true;
}

static void
kopper_update_tex_buffer(struct dri_drawable *drawable,
                         struct dri_context *ctx,
                         struct pipe_resource *res)
{
   struct dri_screen *screen = drawable->screen;
   if (screen->has_dmabuf || drawable->is_window || drawable->info.bos.sType != VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR)
      return;
   drisw_update_tex_buffer(drawable, ctx, res);
}

static void
kopper_flush_swapbuffers(struct dri_context *ctx,
                         struct dri_drawable *drawable)
{
   /* does this actually need to do anything? */
}

static void
kopper_swap_buffers(struct dri_drawable *drawable);
static void
kopper_swap_buffers_with_damage(struct dri_drawable *drawable, int nrects, const int *rects);

void
kopper_init_drawable(struct dri_drawable *drawable, bool isPixmap, int alphaBits)
{
   struct dri_screen *screen = drawable->screen;

   drawable->allocate_textures = kopper_allocate_textures;
   drawable->update_drawable_info = kopper_update_drawable_info;
   drawable->flush_frontbuffer = kopper_flush_frontbuffer;
   drawable->update_tex_buffer = kopper_update_tex_buffer;
   drawable->flush_swapbuffers = kopper_flush_swapbuffers;
   drawable->swap_buffers = kopper_swap_buffers;
   drawable->swap_buffers_with_damage = kopper_swap_buffers_with_damage;

   drawable->info.has_alpha = alphaBits > 0;
   if (screen->kopper_loader->SetSurfaceCreateInfo)
      screen->kopper_loader->SetSurfaceCreateInfo(drawable->loaderPrivate,
                                                  &drawable->info);
   drawable->is_window = !isPixmap && drawable->info.bos.sType != 0;

#ifdef VK_USE_PLATFORM_XCB_KHR
   if (drawable->info.bos.sType == VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR) {
      VkXcbSurfaceCreateInfoKHR *xcb = (VkXcbSurfaceCreateInfoKHR *)&drawable->info.bos;
      xcb_connection_t *conn = xcb->connection;

      int32_t eid = xcb_generate_id(conn);
      if (drawable->is_window) {
         xcb_present_select_input(conn, eid, xcb->window,
                                  XCB_PRESENT_EVENT_MASK_COMPLETE_NOTIFY);
      }

      drawable->special_event =
         xcb_register_for_special_xge(conn, &xcb_present_id, eid, NULL);
   }
#endif
}

void
kopper_destroy_drawable(struct dri_drawable *drawable)
{
#ifdef VK_USE_PLATFORM_XCB_KHR
   if (drawable->info.bos.sType == VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR) {
      VkXcbSurfaceCreateInfoKHR *xcb = (VkXcbSurfaceCreateInfoKHR *)&drawable->info.bos;
      xcb_connection_t *conn = xcb->connection;
      xcb_unregister_for_special_event(conn, drawable->special_event);
   }
#endif
}

int64_t
kopperSwapBuffersWithDamage(struct dri_drawable *drawable, uint32_t flush_flags, int nrects, const int *rects)
{
   struct dri_context *ctx = dri_get_current();
   struct pipe_resource *ptex;

   if (!ctx)
      return 0;

   ptex = drawable->textures[ST_ATTACHMENT_BACK_LEFT];
   if (!ptex)
      return 0;

   /* ensure invalidation is applied before renderpass ends */
   if (flush_flags & __DRI2_FLUSH_INVALIDATE_ANCILLARY)
      _mesa_glthread_invalidate_zsbuf(ctx->st->ctx);

   /* Wait for glthread to finish because we can't use pipe_context from
    * multiple threads.
    */
   _mesa_glthread_finish(ctx->st->ctx);

   drawable->texture_stamp = drawable->lastStamp - 1;

   dri_flush(ctx, drawable,
             __DRI2_FLUSH_DRAWABLE | __DRI2_FLUSH_CONTEXT | flush_flags,
             __DRI2_THROTTLE_SWAPBUFFER);

   struct pipe_box stack_boxes[64];
   if (nrects > ARRAY_SIZE(stack_boxes))
      nrects = 0;
   if (nrects) {
      for (unsigned int i = 0; i < nrects; i++) {
         const int *rect = &rects[i * 4];

         u_box_2d(rect[0], rect[1], rect[2], rect[3], &stack_boxes[i]);
      }
   }

   kopper_copy_to_front(ctx->st->pipe, drawable, ptex, nrects, stack_boxes);
   if (drawable->is_window && !zink_kopper_check(ptex))
      return -1;
   if (!drawable->textures[ST_ATTACHMENT_FRONT_LEFT]) {
      return 0;
   }

   /* have to manually swap the pointers here to make frontbuffer readback work */
   drawable->textures[ST_ATTACHMENT_BACK_LEFT] = drawable->textures[ST_ATTACHMENT_FRONT_LEFT];
   drawable->textures[ST_ATTACHMENT_FRONT_LEFT] = ptex;

   return 0;
}

int64_t
kopperSwapBuffers(struct dri_drawable *dPriv, uint32_t flush_flags)
{
   return kopperSwapBuffersWithDamage(dPriv, flush_flags, 0, NULL);
}

static void
kopper_swap_buffers_with_damage(struct dri_drawable *drawable, int nrects, const int *rects)
{

   kopperSwapBuffersWithDamage(drawable, 0, nrects, rects);
}

static void
kopper_swap_buffers(struct dri_drawable *drawable)
{
   kopper_swap_buffers_with_damage(drawable, 0, NULL);
}

void
kopperSetSwapInterval(struct dri_drawable *drawable, int interval)
{
   struct dri_screen *screen = drawable->screen;
   struct pipe_resource *ptex = drawable->textures[ST_ATTACHMENT_BACK_LEFT] ?
                                drawable->textures[ST_ATTACHMENT_BACK_LEFT] :
                                drawable->textures[ST_ATTACHMENT_FRONT_LEFT];

   /* can't set swap interval on non-windows */
   if (!drawable->window_valid)
      return;
   /* the conditional is because we can be called before buffer allocation.  If
    * we're before allocation, then the initial_swap_interval will be used when
    * the swapchain is eventually created.
    */
   if (ptex) {
      struct pipe_screen *pscreen = kopper_get_zink_screen(screen->base.screen);
      zink_kopper_set_swap_interval(pscreen, ptex, interval);
   }
   drawable->info.initial_swap_interval = interval;
}

int
kopperQueryBufferAge(struct dri_drawable *drawable)
{
   struct dri_context *ctx = dri_get_current();
   struct pipe_resource *ptex = drawable->textures[ST_ATTACHMENT_BACK_LEFT] ?
                                drawable->textures[ST_ATTACHMENT_BACK_LEFT] :
                                drawable->textures[ST_ATTACHMENT_FRONT_LEFT];

   /* can't get buffer age from non-window swapchain */
   if (!drawable->window_valid)
      return 0;

   /* Wait for glthread to finish because we can't use pipe_context from
    * multiple threads.
    */
   _mesa_glthread_finish(ctx->st->ctx);

   return zink_kopper_query_buffer_age(ctx->st->pipe, ptex);
}

int
kopperGetSyncValues(struct dri_drawable *drawable, int64_t target_msc, int64_t divisor,
                    int64_t remainder, int64_t *ust, int64_t *msc, int64_t *sbc)
{
#ifdef VK_USE_PLATFORM_XCB_KHR
   struct kopper_loader_info *info = &drawable->info;

   assert(info->bos.sType == VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR);

   VkXcbSurfaceCreateInfoKHR *xcb = (VkXcbSurfaceCreateInfoKHR *)&info->bos;
   xcb_connection_t *conn = xcb->connection;

   xcb_void_cookie_t cookie =
      xcb_present_notify_msc(conn, xcb->window, 0, target_msc, divisor, remainder);

   xcb_generic_event_t *event;
   int ret = 0;

   xcb_flush(conn);

   while ((event = xcb_wait_for_special_event(conn, drawable->special_event)) != NULL) {
      xcb_present_generic_event_t *ev = (xcb_present_generic_event_t *)event;
      if (ev->evtype == XCB_PRESENT_COMPLETE_NOTIFY) {
         xcb_present_complete_notify_event_t *ce =
            (xcb_present_complete_notify_event_t *) event;

         if (ce->kind == XCB_PRESENT_COMPLETE_KIND_NOTIFY_MSC) {
            *ust = ce->ust;
            *msc = ce->msc;
            *sbc = ce->serial;

            if (event->full_sequence != cookie.sequence) {
               free(event);
               continue;
            }

            ret = 1;
            free(event);
            break;
         }
      }

      free(event);
   }

   return ret;
#else
   return 0;
#endif
}

/* vim: set sw=3 ts=8 sts=3 expandtab: */
