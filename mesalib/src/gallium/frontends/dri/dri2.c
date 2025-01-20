/*
 * Mesa 3-D graphics library
 *
 * Copyright 2009, VMware, Inc.
 * All Rights Reserved.
 * Copyright (C) 2010 LunarG Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Keith Whitwell <keithw@vmware.com> Jakob Bornecrantz
 *    <wallbraker@gmail.com> Chia-I Wu <olv@lunarg.com>
 */

#include "util/libdrm.h"
#include "git_sha1.h"
#include "GL/mesa_glinterop.h"
#include "mesa_interface.h"
#include "util/disk_cache.h"
#include "util/u_memory.h"
#include "util/u_inlines.h"
#include "util/format/u_format.h"
#include "util/u_debug.h"
#include "util/libsync.h"
#include "util/os_file.h"
#include "util/log.h"
#include "frontend/drm_driver.h"
#include "state_tracker/st_format.h"
#include "state_tracker/st_cb_texture.h"
#include "state_tracker/st_texture.h"
#include "state_tracker/st_context.h"
#include "state_tracker/st_interop.h"
#include "pipe-loader/pipe_loader.h"
#include "main/bufferobj.h"
#include "main/texobj.h"

#include "dri_util.h"

#include "dri_helpers.h"
#include "dri_drawable.h"
#include "dri_query_renderer.h"
#include "loader_dri_helper.h"

#include "drm-uapi/drm_fourcc.h"

struct dri2_buffer
{
   __DRIbuffer base;
   struct pipe_resource *resource;
};

static inline struct dri2_buffer *
dri2_buffer(__DRIbuffer * driBufferPriv)
{
   return (struct dri2_buffer *) driBufferPriv;
}

/**
 * Invalidate the drawable.
 *
 * How we get here is listed below.
 *
 * 1. Called by these SwapBuffers implementations where the context is known:
 *       loader_dri3_swap_buffers_msc
 *       EGL: droid_swap_buffers
 *       EGL: dri2_drm_swap_buffers
 *       EGL: dri2_wl_swap_buffers_with_damage
 *       EGL: dri2_x11_swap_buffers_msc
 *
 * 2. Other callers where the context is known:
 *       st_manager_flush_frontbuffer -> dri2_flush_frontbuffer
 *          -> EGL droid_display_shared_buffer
 *
 * 3. Other callers where the context is unknown:
 *       loader: dri3_handle_present_event - XCB_PRESENT_CONFIGURE_NOTIFY
 *       eglQuerySurface -> dri3_query_surface
 *          -> loader_dri3_update_drawable_geometry
 *       EGL: wl_egl_window::resize_callback (called outside Mesa)
 */
void
dri_invalidate_drawable(struct dri_drawable *drawable)
{
   drawable->lastStamp++;
   drawable->texture_mask = 0; /* mark all attachments as invalid */

   p_atomic_inc(&drawable->base.stamp);
}

/**
 * Retrieve __DRIbuffer from the DRI loader.
 */
static __DRIbuffer *
dri2_drawable_get_buffers(struct dri_drawable *drawable,
                          const enum st_attachment_type *atts,
                          unsigned *count)
{
   const __DRIdri2LoaderExtension *loader = drawable->screen->dri2.loader;
   bool with_format;
   __DRIbuffer *buffers;
   int num_buffers;
   unsigned attachments[__DRI_BUFFER_COUNT];
   unsigned num_attachments, i;

   assert(loader);
   assert(*count <= __DRI_BUFFER_COUNT);
   with_format = dri_with_format(drawable->screen);

   num_attachments = 0;

   /* for Xserver 1.6.0 (DRI2 version 1) we always need to ask for the front */
   if (!with_format)
      attachments[num_attachments++] = __DRI_BUFFER_FRONT_LEFT;

   for (i = 0; i < *count; i++) {
      enum pipe_format format;
      unsigned bind;
      int att, depth;

      dri_drawable_get_format(drawable, atts[i], &format, &bind);
      if (format == PIPE_FORMAT_NONE)
         continue;

      switch (atts[i]) {
      case ST_ATTACHMENT_FRONT_LEFT:
         /* already added */
         if (!with_format)
            continue;
         att = __DRI_BUFFER_FRONT_LEFT;
         break;
      case ST_ATTACHMENT_BACK_LEFT:
         att = __DRI_BUFFER_BACK_LEFT;
         break;
      case ST_ATTACHMENT_FRONT_RIGHT:
         att = __DRI_BUFFER_FRONT_RIGHT;
         break;
      case ST_ATTACHMENT_BACK_RIGHT:
         att = __DRI_BUFFER_BACK_RIGHT;
         break;
      default:
         continue;
      }

      /*
       * In this switch statement we must support all formats that
       * may occur as the stvis->color_format.
       */
      switch(format) {
      case PIPE_FORMAT_R16G16B16A16_FLOAT:
         depth = 64;
         break;
      case PIPE_FORMAT_R16G16B16X16_FLOAT:
         depth = 48;
         break;
      case PIPE_FORMAT_B10G10R10A2_UNORM:
      case PIPE_FORMAT_R10G10B10A2_UNORM:
      case PIPE_FORMAT_BGRA8888_UNORM:
      case PIPE_FORMAT_RGBA8888_UNORM:
	 depth = 32;
	 break;
      case PIPE_FORMAT_R10G10B10X2_UNORM:
      case PIPE_FORMAT_B10G10R10X2_UNORM:
         depth = 30;
         break;
      case PIPE_FORMAT_BGRX8888_UNORM:
      case PIPE_FORMAT_RGBX8888_UNORM:
	 depth = 24;
	 break;
      case PIPE_FORMAT_B5G6R5_UNORM:
	 depth = 16;
	 break;
      default:
	 depth = util_format_get_blocksizebits(format);
	 assert(!"Unexpected format in dri2_drawable_get_buffers()");
      }

      attachments[num_attachments++] = att;
      if (with_format) {
         attachments[num_attachments++] = depth;
      }
   }

   if (with_format) {
      num_attachments /= 2;
      buffers = loader->getBuffersWithFormat(drawable,
            &drawable->w, &drawable->h,
            attachments, num_attachments,
            &num_buffers, drawable->loaderPrivate);
   }
   else {
      buffers = loader->getBuffers(drawable,
            &drawable->w, &drawable->h,
            attachments, num_attachments,
            &num_buffers, drawable->loaderPrivate);
   }

   if (buffers)
      *count = num_buffers;

   return buffers;
}

bool
dri_image_drawable_get_buffers(struct dri_drawable *drawable,
                               struct __DRIimageList *images,
                               const enum st_attachment_type *statts,
                               unsigned statts_count);
bool
dri_image_drawable_get_buffers(struct dri_drawable *drawable,
                               struct __DRIimageList *images,
                               const enum st_attachment_type *statts,
                               unsigned statts_count)
{
   enum pipe_format color_format = PIPE_FORMAT_NONE;
   uint32_t buffer_mask = 0;
   unsigned i;

   for (i = 0; i < statts_count; i++) {
      enum pipe_format pf;
      unsigned bind;

      dri_drawable_get_format(drawable, statts[i], &pf, &bind);
      if (pf == PIPE_FORMAT_NONE)
         continue;

      switch (statts[i]) {
      case ST_ATTACHMENT_FRONT_LEFT:
         buffer_mask |= __DRI_IMAGE_BUFFER_FRONT;
         color_format = pf;
         break;
      case ST_ATTACHMENT_BACK_LEFT:
         buffer_mask |= __DRI_IMAGE_BUFFER_BACK;
         color_format = pf;
         break;
      default:
         break;
      }
   }

   /* Stamp usage behavior in the getBuffers callback:
    *
    * 1. DRI3 (EGL and GLX):
    *       This calls loader_dri3_get_buffers, which saves the stamp pointer
    *       in loader_dri3_drawable::stamp, which is only changed (incremented)
    *       by loader_dri3_swap_buffers_msc.
    *
    * 2. EGL Android, Device, Surfaceless, Wayland:
    *       The stamp is unused.
    *
    * How do we get here:
    *    dri_set_tex_buffer2 (GLX_EXT_texture_from_pixmap)
    *    st_api_make_current
    *    st_manager_validate_framebuffers (part of st_validate_state)
    */
   return drawable->screen->image.loader->getBuffers(
                                          drawable,
                                          color_format,
                                          (uint32_t *)&drawable->base.stamp,
                                          drawable->loaderPrivate, buffer_mask,
                                          images);
}

static void
dri2_release_buffer(__DRIbuffer *bPriv)
{
   struct dri2_buffer *buffer = dri2_buffer(bPriv);

   pipe_resource_reference(&buffer->resource, NULL);
   FREE(buffer);
}

void
dri2_set_in_fence_fd(struct dri_image *img, int fd)
{
   validate_fence_fd(fd);
   validate_fence_fd(img->in_fence_fd);
   sync_accumulate("dri", &img->in_fence_fd, fd);
}

/*
 * Backend functions for pipe_frontend_drawable.
 */

static void
dri2_allocate_textures(struct dri_context *ctx,
                       struct dri_drawable *drawable,
                       const enum st_attachment_type *statts,
                       unsigned statts_count)
{
   struct dri_screen *screen = drawable->screen;
   struct pipe_resource templ;
   bool alloc_depthstencil = false;
   unsigned i, j, bind;
   const __DRIimageLoaderExtension *image = screen->image.loader;
   /* Image specific variables */
   struct __DRIimageList images;
   /* Dri2 specific variables */
   __DRIbuffer *buffers = NULL;
   struct winsys_handle whandle;
   unsigned num_buffers = statts_count;

   assert(num_buffers <= __DRI_BUFFER_COUNT);

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
   else {
      buffers = dri2_drawable_get_buffers(drawable, statts, &num_buffers);
      if (!buffers || (drawable->old_num == num_buffers &&
                       drawable->old_w == drawable->w &&
                       drawable->old_h == drawable->h &&
                       memcmp(drawable->old, buffers,
                              sizeof(__DRIbuffer) * num_buffers) == 0))
         return;
   }

   /* Second clean useless resources*/

   /* See if we need a depth-stencil buffer. */
   for (i = 0; i < statts_count; i++) {
      if (statts[i] == ST_ATTACHMENT_DEPTH_STENCIL) {
         alloc_depthstencil = true;
         break;
      }
   }

   /* Delete the resources we won't need. */
   for (i = 0; i < ST_ATTACHMENT_COUNT; i++) {
      /* Don't delete the depth-stencil buffer, we can reuse it. */
      if (i == ST_ATTACHMENT_DEPTH_STENCIL && alloc_depthstencil)
         continue;

      /* Flush the texture before unreferencing, so that other clients can
       * see what the driver has rendered.
       */
      if (i != ST_ATTACHMENT_DEPTH_STENCIL && drawable->textures[i]) {
         struct pipe_context *pipe = ctx->st->pipe;
         pipe->flush_resource(pipe, drawable->textures[i]);
      }

      pipe_resource_reference(&drawable->textures[i], NULL);
   }

   if (drawable->stvis.samples > 1) {
      for (i = 0; i < ST_ATTACHMENT_COUNT; i++) {
         bool del = true;

         /* Don't delete MSAA resources for the attachments which are enabled,
          * we can reuse them. */
         for (j = 0; j < statts_count; j++) {
            if (i == statts[j]) {
               del = false;
               break;
            }
         }

         if (del) {
            pipe_resource_reference(&drawable->msaa_textures[i], NULL);
         }
      }
   }

   /* Third use the buffers retrieved to fill the drawable info */

   memset(&templ, 0, sizeof(templ));
   templ.target = screen->target;
   templ.last_level = 0;
   templ.depth0 = 1;
   templ.array_size = 1;

   if (image) {
      if (images.image_mask & __DRI_IMAGE_BUFFER_FRONT) {
         struct pipe_resource **buf =
            &drawable->textures[ST_ATTACHMENT_FRONT_LEFT];
         struct pipe_resource *texture = images.front->texture;

         drawable->w = texture->width0;
         drawable->h = texture->height0;

         pipe_resource_reference(buf, texture);
         dri_image_fence_sync(ctx, images.front);
      }

      if (images.image_mask & __DRI_IMAGE_BUFFER_BACK) {
         struct pipe_resource **buf =
            &drawable->textures[ST_ATTACHMENT_BACK_LEFT];
         struct pipe_resource *texture = images.back->texture;

         drawable->w = texture->width0;
         drawable->h = texture->height0;

         pipe_resource_reference(buf, texture);
         dri_image_fence_sync(ctx, images.back);
      }

      if (images.image_mask & __DRI_IMAGE_BUFFER_SHARED) {
         struct pipe_resource **buf =
            &drawable->textures[ST_ATTACHMENT_BACK_LEFT];
         struct pipe_resource *texture = images.back->texture;

         drawable->w = texture->width0;
         drawable->h = texture->height0;

         pipe_resource_reference(buf, texture);
         dri_image_fence_sync(ctx, images.back);

         ctx->is_shared_buffer_bound = true;
      } else {
         ctx->is_shared_buffer_bound = false;
      }

      /* Note: if there is both a back and a front buffer,
       * then they have the same size.
       */
      templ.width0 = drawable->w;
      templ.height0 = drawable->h;
   }
   else {
      memset(&whandle, 0, sizeof(whandle));

      /* Process DRI-provided buffers and get pipe_resources. */
      for (i = 0; i < num_buffers; i++) {
         __DRIbuffer *buf = &buffers[i];
         enum st_attachment_type statt;
         enum pipe_format format;

         switch (buf->attachment) {
         case __DRI_BUFFER_FRONT_LEFT:
            if (!screen->auto_fake_front) {
               continue; /* invalid attachment */
            }
            FALLTHROUGH;
         case __DRI_BUFFER_FAKE_FRONT_LEFT:
            statt = ST_ATTACHMENT_FRONT_LEFT;
            break;
         case __DRI_BUFFER_BACK_LEFT:
            statt = ST_ATTACHMENT_BACK_LEFT;
            break;
         default:
            continue; /* invalid attachment */
         }

         dri_drawable_get_format(drawable, statt, &format, &bind);
         if (format == PIPE_FORMAT_NONE)
            continue;

         /* dri2_drawable_get_buffers has already filled dri_drawable->w
          * and dri_drawable->h */
         templ.width0 = drawable->w;
         templ.height0 = drawable->h;
         templ.format = format;
         templ.bind = bind;
         whandle.handle = buf->name;
         whandle.stride = buf->pitch;
         whandle.offset = 0;
         whandle.format = format;
         whandle.modifier = DRM_FORMAT_MOD_INVALID;
         if (screen->can_share_buffer)
            whandle.type = WINSYS_HANDLE_TYPE_SHARED;
         else
            whandle.type = WINSYS_HANDLE_TYPE_KMS;
         drawable->textures[statt] =
            screen->base.screen->resource_from_handle(screen->base.screen,
                  &templ, &whandle,
                  PIPE_HANDLE_USAGE_EXPLICIT_FLUSH);
         assert(drawable->textures[statt]);
      }
   }

   /* Allocate private MSAA colorbuffers. */
   if (drawable->stvis.samples > 1) {
      for (i = 0; i < statts_count; i++) {
         enum st_attachment_type statt = statts[i];

         if (statt == ST_ATTACHMENT_DEPTH_STENCIL)
            continue;

         if (drawable->textures[statt]) {
            templ.format = drawable->textures[statt]->format;
            templ.bind = drawable->textures[statt]->bind &
                         ~(PIPE_BIND_SCANOUT | PIPE_BIND_SHARED);
            templ.nr_samples = drawable->stvis.samples;
            templ.nr_storage_samples = drawable->stvis.samples;

            /* Try to reuse the resource.
             * (the other resource parameters should be constant)
             */
            if (!drawable->msaa_textures[statt] ||
                drawable->msaa_textures[statt]->width0 != templ.width0 ||
                drawable->msaa_textures[statt]->height0 != templ.height0) {
               /* Allocate a new one. */
               pipe_resource_reference(&drawable->msaa_textures[statt], NULL);

               drawable->msaa_textures[statt] =
                  screen->base.screen->resource_create(screen->base.screen,
                                                       &templ);
               assert(drawable->msaa_textures[statt]);

               /* If there are any MSAA resources, we should initialize them
                * such that they contain the same data as the single-sample
                * resources we just got from the X server.
                *
                * The reason for this is that the gallium frontend (and
                * therefore the app) can access the MSAA resources only.
                * The single-sample resources are not exposed
                * to the gallium frontend.
                *
                */
               dri_pipe_blit(ctx->st->pipe,
                             drawable->msaa_textures[statt],
                             drawable->textures[statt]);
            }
         }
         else {
            pipe_resource_reference(&drawable->msaa_textures[statt], NULL);
         }
      }
   }

   /* Allocate a private depth-stencil buffer. */
   if (alloc_depthstencil) {
      enum st_attachment_type statt = ST_ATTACHMENT_DEPTH_STENCIL;
      struct pipe_resource **zsbuf;
      enum pipe_format format;
      unsigned bind;

      dri_drawable_get_format(drawable, statt, &format, &bind);

      if (format) {
         templ.format = format;
         templ.bind = bind & ~PIPE_BIND_SHARED;

         if (drawable->stvis.samples > 1) {
            templ.nr_samples = drawable->stvis.samples;
            templ.nr_storage_samples = drawable->stvis.samples;
            zsbuf = &drawable->msaa_textures[statt];
         }
         else {
            templ.nr_samples = 0;
            templ.nr_storage_samples = 0;
            zsbuf = &drawable->textures[statt];
         }

         /* Try to reuse the resource.
          * (the other resource parameters should be constant)
          */
         if (!*zsbuf ||
             (*zsbuf)->width0 != templ.width0 ||
             (*zsbuf)->height0 != templ.height0) {
            /* Allocate a new one. */
            pipe_resource_reference(zsbuf, NULL);
            *zsbuf = screen->base.screen->resource_create(screen->base.screen,
                                                          &templ);
            assert(*zsbuf);
         }
      }
      else {
         pipe_resource_reference(&drawable->msaa_textures[statt], NULL);
         pipe_resource_reference(&drawable->textures[statt], NULL);
      }
   }

   /* For DRI2, we may get the same buffers again from the server.
    * To prevent useless imports of gem names, drawable->old* is used
    * to bypass the import if we get the same buffers. This doesn't apply
    * to DRI3/Wayland, users of image.loader, since the buffer is managed
    * by the client (no import), and the back buffer is going to change
    * at every redraw.
    */
   if (!image) {
      drawable->old_num = num_buffers;
      drawable->old_w = drawable->w;
      drawable->old_h = drawable->h;
      memcpy(drawable->old, buffers, sizeof(__DRIbuffer) * num_buffers);
   }
}

static bool
dri2_flush_frontbuffer(struct dri_context *ctx,
                       struct dri_drawable *drawable,
                       enum st_attachment_type statt)
{
   const __DRIimageLoaderExtension *image = drawable->screen->image.loader;
   const __DRIdri2LoaderExtension *loader = drawable->screen->dri2.loader;
   const __DRImutableRenderBufferLoaderExtension *shared_buffer_loader =
      drawable->screen->mutableRenderBuffer.loader;
   struct pipe_context *pipe = ctx->st->pipe;
   struct pipe_fence_handle *fence = NULL;
   int fence_fd = -1;

   /* We need to flush for front buffer rendering when either we're using the
    * front buffer at the GL API level, or when EGL_KHR_mutable_render_buffer
    * has redirected GL_BACK to the front buffer.
    */
   if (statt != ST_ATTACHMENT_FRONT_LEFT &&
       (!ctx->is_shared_buffer_bound || statt != ST_ATTACHMENT_BACK_LEFT))
         return false;

   /* Wait for glthread to finish because we can't use pipe_context from
    * multiple threads.
    */
   _mesa_glthread_finish(ctx->st->ctx);

   if (drawable->stvis.samples > 1) {
      /* Resolve the buffer used for front rendering. */
      dri_pipe_blit(ctx->st->pipe, drawable->textures[statt],
                    drawable->msaa_textures[statt]);
   }

   if (drawable->textures[statt]) {
      pipe->flush_resource(pipe, drawable->textures[statt]);
   }

   if (ctx->is_shared_buffer_bound) {
      /* is_shared_buffer_bound should only be true with image extension: */
      assert(image);
      pipe->flush(pipe, &fence, PIPE_FLUSH_FENCE_FD);
   } else {
      pipe->flush(pipe, NULL, 0);
   }

   if (image) {
      image->flushFrontBuffer(drawable, drawable->loaderPrivate);
      if (ctx->is_shared_buffer_bound) {
         if (fence)
            fence_fd = pipe->screen->fence_get_fd(pipe->screen, fence);

         shared_buffer_loader->displaySharedBuffer(drawable, fence_fd,
                                                   drawable->loaderPrivate);

         pipe->screen->fence_reference(pipe->screen, &fence, NULL);
      }
   }
   else if (loader->flushFrontBuffer) {
      loader->flushFrontBuffer(drawable, drawable->loaderPrivate);
   }

   return true;
}

/**
 * The struct dri_drawable flush_swapbuffers callback
 */
static void
dri2_flush_swapbuffers(struct dri_context *ctx,
                       struct dri_drawable *drawable)
{
   const __DRIimageLoaderExtension *image = drawable->screen->image.loader;

   if (image && image->flushSwapBuffers) {
      image->flushSwapBuffers(drawable, drawable->loaderPrivate);
   }
}

static void
dri2_update_tex_buffer(struct dri_drawable *drawable,
                       struct dri_context *ctx,
                       struct pipe_resource *res)
{
   /* no-op */
}

static const struct dri2_format_mapping r8_b8_g8_mapping = {
   DRM_FORMAT_YVU420,
   __DRI_IMAGE_FORMAT_NONE,
   __DRI_IMAGE_COMPONENTS_Y_U_V,
   PIPE_FORMAT_R8_B8_G8_420_UNORM,
   3,
   { { 0, 0, 0, __DRI_IMAGE_FORMAT_R8 },
     { 2, 1, 1, __DRI_IMAGE_FORMAT_R8 },
     { 1, 1, 1, __DRI_IMAGE_FORMAT_R8 } }
};

static const struct dri2_format_mapping r8_g8_b8_mapping = {
   DRM_FORMAT_YUV420,
   __DRI_IMAGE_FORMAT_NONE,
   __DRI_IMAGE_COMPONENTS_Y_U_V,
   PIPE_FORMAT_R8_G8_B8_420_UNORM,
   3,
   { { 0, 0, 0, __DRI_IMAGE_FORMAT_R8 },
     { 1, 1, 1, __DRI_IMAGE_FORMAT_R8 },
     { 2, 1, 1, __DRI_IMAGE_FORMAT_R8 } }
};

static const struct dri2_format_mapping r8_g8b8_mapping = {
   DRM_FORMAT_NV12,
   __DRI_IMAGE_FORMAT_NONE,
   __DRI_IMAGE_COMPONENTS_Y_UV,
   PIPE_FORMAT_R8_G8B8_420_UNORM,
   2,
   { { 0, 0, 0, __DRI_IMAGE_FORMAT_R8 },
     { 1, 1, 1, __DRI_IMAGE_FORMAT_GR88 } }
};

static const struct dri2_format_mapping r8_g8b8_mapping_422 = {
   DRM_FORMAT_NV16,
   __DRI_IMAGE_FORMAT_NONE,
   __DRI_IMAGE_COMPONENTS_Y_UV,
   PIPE_FORMAT_R8_G8B8_422_UNORM,
   2,
   { { 0, 0, 0, __DRI_IMAGE_FORMAT_R8 },
     { 1, 1, 0, __DRI_IMAGE_FORMAT_GR88 } }
};

static const struct dri2_format_mapping r8_b8g8_mapping = {
   DRM_FORMAT_NV21,
   __DRI_IMAGE_FORMAT_NONE,
   __DRI_IMAGE_COMPONENTS_Y_UV,
   PIPE_FORMAT_R8_B8G8_420_UNORM,
   2,
   { { 0, 0, 0, __DRI_IMAGE_FORMAT_R8 },
     { 1, 1, 1, __DRI_IMAGE_FORMAT_GR88 } }
};

static const struct dri2_format_mapping r8g8_r8b8_mapping = {
   DRM_FORMAT_YUYV,
   __DRI_IMAGE_FORMAT_NONE,
   __DRI_IMAGE_COMPONENTS_Y_XUXV,
   PIPE_FORMAT_R8G8_R8B8_UNORM, 2,
   { { 0, 0, 0, __DRI_IMAGE_FORMAT_GR88 },
     { 0, 1, 0, __DRI_IMAGE_FORMAT_ARGB8888 } }
};

static const struct dri2_format_mapping r8b8_r8g8_mapping = {
   DRM_FORMAT_YVYU,
   __DRI_IMAGE_FORMAT_NONE,
   __DRI_IMAGE_COMPONENTS_Y_XUXV,
   PIPE_FORMAT_R8B8_R8G8_UNORM, 2,
   { { 0, 0, 0, __DRI_IMAGE_FORMAT_GR88 },
     { 0, 1, 0, __DRI_IMAGE_FORMAT_ARGB8888 } }
};

static const struct dri2_format_mapping b8r8_g8r8_mapping = {
   DRM_FORMAT_VYUY,
   __DRI_IMAGE_FORMAT_NONE,
   __DRI_IMAGE_COMPONENTS_Y_XUXV,
   PIPE_FORMAT_B8R8_G8R8_UNORM, 2,
   { { 0, 0, 0, __DRI_IMAGE_FORMAT_GR88 },
     { 0, 1, 0, __DRI_IMAGE_FORMAT_ABGR8888 } }
};

static const struct dri2_format_mapping g8r8_b8r8_mapping = {
   DRM_FORMAT_UYVY,
   __DRI_IMAGE_FORMAT_NONE,
   __DRI_IMAGE_COMPONENTS_Y_XUXV,
   PIPE_FORMAT_G8R8_B8R8_UNORM, 2,
   { { 0, 0, 0, __DRI_IMAGE_FORMAT_GR88 },
     { 0, 1, 0, __DRI_IMAGE_FORMAT_ABGR8888 } }
};

static const struct dri2_format_mapping r10_g10b10_mapping = {
   DRM_FORMAT_NV15,
   __DRI_IMAGE_FORMAT_NONE,
   __DRI_IMAGE_COMPONENTS_Y_UV,
   PIPE_FORMAT_R10_G10B10_420_UNORM,
   2,
   { { 0, 0, 0, __DRI_IMAGE_FORMAT_NONE },
     { 1, 1, 1, __DRI_IMAGE_FORMAT_NONE } }
};

static const struct dri2_format_mapping r10_g10b10_mapping_422 = {
   DRM_FORMAT_NV20,
   __DRI_IMAGE_FORMAT_NONE,
   __DRI_IMAGE_COMPONENTS_Y_UV,
   PIPE_FORMAT_R10_G10B10_422_UNORM,
   2,
   { { 0, 0, 0, __DRI_IMAGE_FORMAT_NONE },
     { 1, 1, 0, __DRI_IMAGE_FORMAT_NONE } }
};

static enum __DRIFixedRateCompression
to_dri_compression_rate(uint32_t rate)
{
   switch (rate) {
   case PIPE_COMPRESSION_FIXED_RATE_NONE:
      return __DRI_FIXED_RATE_COMPRESSION_NONE;
   case PIPE_COMPRESSION_FIXED_RATE_DEFAULT:
      return __DRI_FIXED_RATE_COMPRESSION_DEFAULT;
   case 1: return __DRI_FIXED_RATE_COMPRESSION_1BPC;
   case 2: return __DRI_FIXED_RATE_COMPRESSION_2BPC;
   case 3: return __DRI_FIXED_RATE_COMPRESSION_3BPC;
   case 4: return __DRI_FIXED_RATE_COMPRESSION_4BPC;
   case 5: return __DRI_FIXED_RATE_COMPRESSION_5BPC;
   case 6: return __DRI_FIXED_RATE_COMPRESSION_6BPC;
   case 7: return __DRI_FIXED_RATE_COMPRESSION_7BPC;
   case 8: return __DRI_FIXED_RATE_COMPRESSION_8BPC;
   case 9: return __DRI_FIXED_RATE_COMPRESSION_9BPC;
   case 10: return __DRI_FIXED_RATE_COMPRESSION_10BPC;
   case 11: return __DRI_FIXED_RATE_COMPRESSION_11BPC;
   case 12: return __DRI_FIXED_RATE_COMPRESSION_12BPC;
   default:
      unreachable("invalid compression fixed-rate value");
   }
}

static uint32_t
from_dri_compression_rate(enum __DRIFixedRateCompression rate)
{
   switch (rate) {
   case __DRI_FIXED_RATE_COMPRESSION_NONE:
      return PIPE_COMPRESSION_FIXED_RATE_NONE;
   case __DRI_FIXED_RATE_COMPRESSION_DEFAULT:
      return PIPE_COMPRESSION_FIXED_RATE_DEFAULT;
   case __DRI_FIXED_RATE_COMPRESSION_1BPC: return 1;
   case __DRI_FIXED_RATE_COMPRESSION_2BPC: return 2;
   case __DRI_FIXED_RATE_COMPRESSION_3BPC: return 3;
   case __DRI_FIXED_RATE_COMPRESSION_4BPC: return 4;
   case __DRI_FIXED_RATE_COMPRESSION_5BPC: return 5;
   case __DRI_FIXED_RATE_COMPRESSION_6BPC: return 6;
   case __DRI_FIXED_RATE_COMPRESSION_7BPC: return 7;
   case __DRI_FIXED_RATE_COMPRESSION_8BPC: return 8;
   case __DRI_FIXED_RATE_COMPRESSION_9BPC: return 9;
   case __DRI_FIXED_RATE_COMPRESSION_10BPC: return 10;
   case __DRI_FIXED_RATE_COMPRESSION_11BPC: return 11;
   case __DRI_FIXED_RATE_COMPRESSION_12BPC: return 12;
   default:
      unreachable("invalid compression fixed-rate value");
   }
}

static struct dri_image *
dri_create_image_from_winsys(struct dri_screen *screen,
                              int width, int height, const struct dri2_format_mapping *map,
                              int num_handles, struct winsys_handle *whandle,
                              unsigned bind,
                              void *loaderPrivate)
{
   struct pipe_screen *pscreen = screen->base.screen;
   struct dri_image *img;
   struct pipe_resource templ;
   unsigned tex_usage = 0;
   int i;
   bool use_lowered = false;
   const unsigned format_planes = util_format_get_num_planes(map->pipe_format);

   if (pscreen->is_format_supported(pscreen, map->pipe_format, screen->target, 0, 0,
                                    PIPE_BIND_RENDER_TARGET))
      tex_usage |= PIPE_BIND_RENDER_TARGET;
   if (pscreen->is_format_supported(pscreen, map->pipe_format, screen->target, 0, 0,
                                    PIPE_BIND_SAMPLER_VIEW))
      tex_usage |= PIPE_BIND_SAMPLER_VIEW;

   /* For NV12, see if we have support for sampling r8_g8b8 */
   if (!tex_usage && map->pipe_format == PIPE_FORMAT_NV12 &&
       pscreen->is_format_supported(pscreen, PIPE_FORMAT_R8_G8B8_420_UNORM,
                                    screen->target, 0, 0, PIPE_BIND_SAMPLER_VIEW)) {
      map = &r8_g8b8_mapping;
      tex_usage |= PIPE_BIND_SAMPLER_VIEW;
   }

   /* For NV21, see if we have support for sampling r8_b8g8 */
   if (!tex_usage && map->pipe_format == PIPE_FORMAT_NV21 &&
       pscreen->is_format_supported(pscreen, PIPE_FORMAT_R8_B8G8_420_UNORM,
                                    screen->target, 0, 0, PIPE_BIND_SAMPLER_VIEW)) {
      map = &r8_b8g8_mapping;
      tex_usage |= PIPE_BIND_SAMPLER_VIEW;
   }

   /* For NV16, see if we have support for sampling r8_g8b8 */
   if (!tex_usage && map->pipe_format == PIPE_FORMAT_NV16 &&
       pscreen->is_format_supported(pscreen, PIPE_FORMAT_R8_G8B8_422_UNORM,
                                    screen->target, 0, 0, PIPE_BIND_SAMPLER_VIEW)) {
      map = &r8_g8b8_mapping_422;
      tex_usage |= PIPE_BIND_SAMPLER_VIEW;
   }

   /* For NV15, see if we have support for sampling r10_g10b10 */
   if (!tex_usage && map->pipe_format == PIPE_FORMAT_NV15 &&
       pscreen->is_format_supported(pscreen, PIPE_FORMAT_R10_G10B10_420_UNORM,
                                    screen->target, 0, 0, PIPE_BIND_SAMPLER_VIEW)) {
      map = &r10_g10b10_mapping;
      tex_usage |= PIPE_BIND_SAMPLER_VIEW;
   }

   if (!tex_usage && map->pipe_format == PIPE_FORMAT_NV20 &&
       pscreen->is_format_supported(pscreen, PIPE_FORMAT_R10_G10B10_422_UNORM,
                                    screen->target, 0, 0, PIPE_BIND_SAMPLER_VIEW)) {
      map = &r10_g10b10_mapping_422;
      tex_usage |= PIPE_BIND_SAMPLER_VIEW;
   }

   /* For YV12 and I420, see if we have support for sampling r8_b8_g8 or r8_g8_b8 */
   if (!tex_usage && map->pipe_format == PIPE_FORMAT_IYUV) {
      if (map->dri_fourcc == DRM_FORMAT_YUV420 &&
          pscreen->is_format_supported(pscreen, PIPE_FORMAT_R8_G8_B8_420_UNORM,
                                       screen->target, 0, 0, PIPE_BIND_SAMPLER_VIEW)) {
         map = &r8_g8_b8_mapping;
         tex_usage |= PIPE_BIND_SAMPLER_VIEW;
      } else if (map->dri_fourcc == DRM_FORMAT_YVU420 &&
          pscreen->is_format_supported(pscreen, PIPE_FORMAT_R8_B8_G8_420_UNORM,
                                       screen->target, 0, 0, PIPE_BIND_SAMPLER_VIEW)) {
         map = &r8_b8_g8_mapping;
         tex_usage |= PIPE_BIND_SAMPLER_VIEW;
      }
   }

   /* If the hardware supports R8G8_R8B8 style subsampled RGB formats, these
    * can be used for YUYV and UYVY formats.
    */
   if (!tex_usage && map->pipe_format == PIPE_FORMAT_YUYV &&
       pscreen->is_format_supported(pscreen, PIPE_FORMAT_R8G8_R8B8_UNORM,
                                    screen->target, 0, 0, PIPE_BIND_SAMPLER_VIEW)) {
      map = &r8g8_r8b8_mapping;
      tex_usage |= PIPE_BIND_SAMPLER_VIEW;
   }

   if (!tex_usage && map->pipe_format == PIPE_FORMAT_YVYU &&
       pscreen->is_format_supported(pscreen, PIPE_FORMAT_R8B8_R8G8_UNORM,
                                    screen->target, 0, 0, PIPE_BIND_SAMPLER_VIEW)) {
      map = &r8b8_r8g8_mapping;
      tex_usage |= PIPE_BIND_SAMPLER_VIEW;
   }

   if (!tex_usage && map->pipe_format == PIPE_FORMAT_UYVY &&
       pscreen->is_format_supported(pscreen, PIPE_FORMAT_G8R8_B8R8_UNORM,
                                    screen->target, 0, 0, PIPE_BIND_SAMPLER_VIEW)) {
      map = &g8r8_b8r8_mapping;
      tex_usage |= PIPE_BIND_SAMPLER_VIEW;
   }

   if (!tex_usage && map->pipe_format == PIPE_FORMAT_VYUY &&
       pscreen->is_format_supported(pscreen, PIPE_FORMAT_B8R8_G8R8_UNORM,
                                    screen->target, 0, 0, PIPE_BIND_SAMPLER_VIEW)) {
      map = &b8r8_g8r8_mapping;
      tex_usage |= PIPE_BIND_SAMPLER_VIEW;
   }

   if (!tex_usage && util_format_is_yuv(map->pipe_format)) {
      /* YUV format sampling can be emulated by the GL gallium frontend by
       * using multiple samplers of varying formats.
       * If no tex_usage is set and we detect a YUV format,
       * test for support of all planes' sampler formats and
       * add sampler view usage.
       */
      use_lowered = true;
      if (dri2_yuv_dma_buf_supported(screen, map))
         tex_usage |= PIPE_BIND_SAMPLER_VIEW;
   }

   if (!tex_usage)
      return NULL;

   img = CALLOC_STRUCT(dri_image);
   if (!img)
      return NULL;

   memset(&templ, 0, sizeof(templ));
   templ.bind = tex_usage | bind;
   templ.target = screen->target;
   templ.last_level = 0;
   templ.depth0 = 1;
   templ.array_size = 1;
   templ.width0 = width;
   templ.height0 = height;

   for (i = num_handles - 1; i >= format_planes; i--) {
      struct pipe_resource *tex;

      templ.next = img->texture;

      tex = pscreen->resource_from_handle(pscreen, &templ, &whandle[i],
                                          PIPE_HANDLE_USAGE_FRAMEBUFFER_WRITE);
      if (!tex) {
         pipe_resource_reference(&img->texture, NULL);
         FREE(img);
         return NULL;
      }

      img->texture = tex;
   }

   for (i = (use_lowered ? map->nplanes : format_planes) - 1; i >= 0; i--) {
      struct pipe_resource *tex;

      templ.next = img->texture;
      templ.width0 = width >> map->planes[i].width_shift;
      templ.height0 = height >> map->planes[i].height_shift;
      if (use_lowered)
         templ.format = dri2_get_pipe_format_for_dri_format(map->planes[i].dri_format);
      else
         templ.format = map->pipe_format;
      assert(templ.format != PIPE_FORMAT_NONE);

      tex = pscreen->resource_from_handle(pscreen,
               &templ, &whandle[use_lowered ? map->planes[i].buffer_index : i],
               PIPE_HANDLE_USAGE_FRAMEBUFFER_WRITE);
      if (!tex) {
         pipe_resource_reference(&img->texture, NULL);
         FREE(img);
         return NULL;
      }

      /* Reject image creation if there's an inconsistency between
       * content protection status of tex and img.
       */
      const struct driOptionCache *optionCache = &screen->dev->option_cache;
      if (driQueryOptionb(optionCache, "force_protected_content_check") &&
          (tex->bind & PIPE_BIND_PROTECTED) != (bind & PIPE_BIND_PROTECTED)) {
         pipe_resource_reference(&img->texture, NULL);
         pipe_resource_reference(&tex, NULL);
         FREE(img);
         return NULL;
      }

      img->texture = tex;
   }

   img->level = 0;
   img->layer = 0;
   img->use = 0;
   img->in_fence_fd = -1;
   img->loader_private = loaderPrivate;
   img->screen = screen;

   return img;
}

static unsigned
dri2_get_modifier_num_planes(struct dri_screen *screen,
                             uint64_t modifier, int fourcc)
{
   struct pipe_screen *pscreen = screen->base.screen;
   const struct dri2_format_mapping *map = dri2_get_mapping_by_fourcc(fourcc);

   if (!map)
      return 0;

   switch (modifier) {
   case DRM_FORMAT_MOD_LINEAR:
   /* DRM_FORMAT_MOD_NONE is the same as LINEAR */
   case DRM_FORMAT_MOD_INVALID:
      return util_format_get_num_planes(map->pipe_format);
   default:
      if (!pscreen->is_dmabuf_modifier_supported ||
          !pscreen->is_dmabuf_modifier_supported(pscreen, modifier,
                                                 map->pipe_format, NULL)) {
         return 0;
      }

      if (pscreen->get_dmabuf_modifier_planes) {
         return pscreen->get_dmabuf_modifier_planes(pscreen, modifier,
                                                    map->pipe_format);
      }

      return map->nplanes;
   }
}

struct dri_image *
dri_create_image(struct dri_screen *screen,
                  int width, int height,
                  int format,
                  const uint64_t *modifiers,
                  const unsigned _count,
                  unsigned int use,
                  void *loaderPrivate)
{
   const struct dri2_format_mapping *map = dri2_get_mapping_by_format(format);
   struct pipe_screen *pscreen = screen->base.screen;
   struct dri_image *img;
   struct pipe_resource templ;
   unsigned tex_usage = 0;
   unsigned count = _count;

   if (!map)
      return NULL;

   if (!pscreen->resource_create_with_modifiers && count > 0)
      return NULL;

   if (pscreen->is_format_supported(pscreen, map->pipe_format, screen->target,
                                    0, 0, PIPE_BIND_RENDER_TARGET))
      tex_usage |= PIPE_BIND_RENDER_TARGET;
   if (pscreen->is_format_supported(pscreen, map->pipe_format, screen->target,
                                    0, 0, PIPE_BIND_SAMPLER_VIEW))
      tex_usage |= PIPE_BIND_SAMPLER_VIEW;

   if (!tex_usage)
      return NULL;

   if (use & __DRI_IMAGE_USE_SCANOUT)
      tex_usage |= PIPE_BIND_SCANOUT;
   if (use & __DRI_IMAGE_USE_SHARE)
      tex_usage |= PIPE_BIND_SHARED;
   if (use & __DRI_IMAGE_USE_LINEAR)
      tex_usage |= PIPE_BIND_LINEAR;
   if (use & __DRI_IMAGE_USE_CURSOR) {
      if (width != 64 || height != 64)
         return NULL;
      tex_usage |= PIPE_BIND_CURSOR;
   }
   if (use & __DRI_IMAGE_USE_PROTECTED)
      tex_usage |= PIPE_BIND_PROTECTED;
   if (use & __DRI_IMAGE_USE_PRIME_BUFFER)
      tex_usage |= PIPE_BIND_PRIME_BLIT_DST;
   if (use & __DRI_IMAGE_USE_FRONT_RENDERING)
      tex_usage |= PIPE_BIND_USE_FRONT_RENDERING;

   img = CALLOC_STRUCT(dri_image);
   if (!img)
      return NULL;

   memset(&templ, 0, sizeof(templ));
   templ.bind = tex_usage;
   templ.format = map->pipe_format;
   templ.target = PIPE_TEXTURE_2D;
   templ.last_level = 0;
   templ.width0 = width;
   templ.height0 = height;
   templ.depth0 = 1;
   templ.array_size = 1;

   if (modifiers)
      img->texture =
         screen->base.screen
            ->resource_create_with_modifiers(screen->base.screen,
                                             &templ,
                                             modifiers,
                                             count);
   else
      img->texture =
         screen->base.screen->resource_create(screen->base.screen, &templ);
   if (!img->texture) {
      FREE(img);
      return NULL;
   }

   img->level = 0;
   img->layer = 0;
   img->dri_format = format;
   img->dri_fourcc = map->dri_fourcc;
   img->dri_components = 0;
   img->use = use;
   img->in_fence_fd = -1;

   img->loader_private = loaderPrivate;
   img->screen = screen;
   return img;
}

static bool
dri2_query_image_common(struct dri_image *image, int attrib, int *value)
{
   switch (attrib) {
   case __DRI_IMAGE_ATTRIB_WIDTH:
      *value = image->texture->width0;
      return true;
   case __DRI_IMAGE_ATTRIB_HEIGHT:
      *value = image->texture->height0;
      return true;
   case __DRI_IMAGE_ATTRIB_COMPONENTS:
      if (image->dri_components == 0)
         return false;
      *value = image->dri_components;
      return true;
   case __DRI_IMAGE_ATTRIB_FOURCC:
      if (image->dri_fourcc) {
         *value = image->dri_fourcc;
      } else {
         const struct dri2_format_mapping *map;

         map = dri2_get_mapping_by_format(image->dri_format);
         if (!map)
            return false;

         *value = map->dri_fourcc;
      }
      return true;
   case __DRI_IMAGE_ATTRIB_COMPRESSION_RATE:
      if (!image->texture)
         *value = __DRI_FIXED_RATE_COMPRESSION_NONE;
      else
         *value = to_dri_compression_rate(image->texture->compression_rate);
      return true;

   default:
      return false;
   }
}

static bool
dri2_query_image_by_resource_handle(struct dri_image *image, int attrib, int *value)
{
   struct pipe_screen *pscreen = image->texture->screen;
   struct winsys_handle whandle;
   struct pipe_resource *tex;
   unsigned usage;
   memset(&whandle, 0, sizeof(whandle));
   whandle.plane = image->plane;
   int i;

   switch (attrib) {
   case __DRI_IMAGE_ATTRIB_STRIDE:
   case __DRI_IMAGE_ATTRIB_OFFSET:
   case __DRI_IMAGE_ATTRIB_HANDLE:
      whandle.type = WINSYS_HANDLE_TYPE_KMS;
      break;
   case __DRI_IMAGE_ATTRIB_NAME:
      whandle.type = WINSYS_HANDLE_TYPE_SHARED;
      break;
   case __DRI_IMAGE_ATTRIB_FD:
      whandle.type = WINSYS_HANDLE_TYPE_FD;
      break;
   case __DRI_IMAGE_ATTRIB_NUM_PLANES:
      for (i = 0, tex = image->texture; tex; tex = tex->next)
         i++;
      *value = i;
      return true;
   case __DRI_IMAGE_ATTRIB_MODIFIER_UPPER:
   case __DRI_IMAGE_ATTRIB_MODIFIER_LOWER:
      whandle.type = WINSYS_HANDLE_TYPE_KMS;
      whandle.modifier = DRM_FORMAT_MOD_INVALID;
      break;
   default:
      return false;
   }

   usage = PIPE_HANDLE_USAGE_FRAMEBUFFER_WRITE;

   if (image->use & __DRI_IMAGE_USE_BACKBUFFER)
      usage |= PIPE_HANDLE_USAGE_EXPLICIT_FLUSH;

   if (!pscreen->resource_get_handle(pscreen, NULL, image->texture,
                                     &whandle, usage))
      return false;

   switch (attrib) {
   case __DRI_IMAGE_ATTRIB_STRIDE:
      *value = whandle.stride;
      return true;
   case __DRI_IMAGE_ATTRIB_OFFSET:
      *value = whandle.offset;
      return true;
   case __DRI_IMAGE_ATTRIB_HANDLE:
   case __DRI_IMAGE_ATTRIB_NAME:
   case __DRI_IMAGE_ATTRIB_FD:
      *value = whandle.handle;
      return true;
   case __DRI_IMAGE_ATTRIB_MODIFIER_UPPER:
      if (whandle.modifier == DRM_FORMAT_MOD_INVALID)
         return false;
      *value = (whandle.modifier >> 32) & 0xffffffff;
      return true;
   case __DRI_IMAGE_ATTRIB_MODIFIER_LOWER:
      if (whandle.modifier == DRM_FORMAT_MOD_INVALID)
         return false;
      *value = whandle.modifier & 0xffffffff;
      return true;
   default:
      return false;
   }
}

static bool
dri2_resource_get_param(struct dri_image *image, enum pipe_resource_param param,
                        unsigned handle_usage, uint64_t *value)
{
   struct pipe_screen *pscreen = image->texture->screen;
   if (!pscreen->resource_get_param)
      return false;

   if (image->use & __DRI_IMAGE_USE_BACKBUFFER)
      handle_usage |= PIPE_HANDLE_USAGE_EXPLICIT_FLUSH;

   return pscreen->resource_get_param(pscreen, NULL, image->texture,
                                      image->plane, 0, 0, param, handle_usage,
                                      value);
}

static bool
dri2_query_image_by_resource_param(struct dri_image *image, int attrib, int *value)
{
   enum pipe_resource_param param;
   uint64_t res_param;
   unsigned handle_usage;

   if (!image->texture->screen->resource_get_param)
      return false;

   switch (attrib) {
   case __DRI_IMAGE_ATTRIB_STRIDE:
      param = PIPE_RESOURCE_PARAM_STRIDE;
      break;
   case __DRI_IMAGE_ATTRIB_OFFSET:
      param = PIPE_RESOURCE_PARAM_OFFSET;
      break;
   case __DRI_IMAGE_ATTRIB_NUM_PLANES:
      param = PIPE_RESOURCE_PARAM_NPLANES;
      break;
   case __DRI_IMAGE_ATTRIB_MODIFIER_UPPER:
   case __DRI_IMAGE_ATTRIB_MODIFIER_LOWER:
      param = PIPE_RESOURCE_PARAM_MODIFIER;
      break;
   case __DRI_IMAGE_ATTRIB_HANDLE:
      param = PIPE_RESOURCE_PARAM_HANDLE_TYPE_KMS;
      break;
   case __DRI_IMAGE_ATTRIB_NAME:
      param = PIPE_RESOURCE_PARAM_HANDLE_TYPE_SHARED;
      break;
   case __DRI_IMAGE_ATTRIB_FD:
      param = PIPE_RESOURCE_PARAM_HANDLE_TYPE_FD;
      break;
   default:
      return false;
   }

   handle_usage = PIPE_HANDLE_USAGE_FRAMEBUFFER_WRITE;

   if (!dri2_resource_get_param(image, param, handle_usage, &res_param))
      return false;

   switch (attrib) {
   case __DRI_IMAGE_ATTRIB_STRIDE:
   case __DRI_IMAGE_ATTRIB_OFFSET:
   case __DRI_IMAGE_ATTRIB_NUM_PLANES:
      if (res_param > INT_MAX)
         return false;
      *value = (int)res_param;
      return true;
   case __DRI_IMAGE_ATTRIB_HANDLE:
   case __DRI_IMAGE_ATTRIB_NAME:
   case __DRI_IMAGE_ATTRIB_FD:
      if (res_param > UINT_MAX)
         return false;
      *value = (int)res_param;
      return true;
   case __DRI_IMAGE_ATTRIB_MODIFIER_UPPER:
      if (res_param == DRM_FORMAT_MOD_INVALID)
         return false;
      *value = (res_param >> 32) & 0xffffffff;
      return true;
   case __DRI_IMAGE_ATTRIB_MODIFIER_LOWER:
      if (res_param == DRM_FORMAT_MOD_INVALID)
         return false;
      *value = res_param & 0xffffffff;
      return true;
   default:
      return false;
   }
}

GLboolean
dri2_query_image(struct dri_image *image, int attrib, int *value)
{
   if (dri2_query_image_common(image, attrib, value))
      return GL_TRUE;
   else if (dri2_query_image_by_resource_param(image, attrib, value))
      return GL_TRUE;
   else if (dri2_query_image_by_resource_handle(image, attrib, value))
      return GL_TRUE;
   else
      return GL_FALSE;
}

struct dri_image *
dri2_dup_image(struct dri_image *image, void *loaderPrivate)
{
   struct dri_image *img;

   img = CALLOC_STRUCT(dri_image);
   if (!img)
      return NULL;

   img->texture = NULL;
   pipe_resource_reference(&img->texture, image->texture);
   img->level = image->level;
   img->layer = image->layer;
   img->dri_format = image->dri_format;
   img->internal_format = image->internal_format;
   /* This should be 0 for sub images, but dup is also used for base images. */
   img->dri_components = image->dri_components;
   img->use = image->use;
   img->in_fence_fd = (image->in_fence_fd > 0) ?
         os_dupfd_cloexec(image->in_fence_fd) : -1;
   img->loader_private = loaderPrivate;
   img->screen = image->screen;

   return img;
}

GLboolean
dri2_validate_usage(struct dri_image *image, unsigned int use)
{
   if (!image || !image->texture)
      return false;

   struct pipe_screen *screen = image->texture->screen;
   if (!screen->check_resource_capability)
      return true;

   /* We don't want to check these:
    *   __DRI_IMAGE_USE_SHARE (all images are shareable)
    *   __DRI_IMAGE_USE_BACKBUFFER (all images support this)
    */
   unsigned bind = 0;
   if (use & __DRI_IMAGE_USE_SCANOUT)
      bind |= PIPE_BIND_SCANOUT;
   if (use & __DRI_IMAGE_USE_LINEAR)
      bind |= PIPE_BIND_LINEAR;
   if (use & __DRI_IMAGE_USE_CURSOR)
      bind |= PIPE_BIND_CURSOR;

   if (!bind)
      return true;

   return screen->check_resource_capability(screen, image->texture, bind);
}

struct dri_image *
dri2_from_names(struct dri_screen *screen, int width, int height, int fourcc,
                int *names, int num_names, int *strides, int *offsets,
                void *loaderPrivate)
{
   const struct dri2_format_mapping *map = dri2_get_mapping_by_fourcc(fourcc);
   struct dri_image *img;
   struct winsys_handle whandle;

   if (!map)
      return NULL;

   if (num_names != 1)
      return NULL;

   memset(&whandle, 0, sizeof(whandle));
   whandle.type = WINSYS_HANDLE_TYPE_SHARED;
   whandle.handle = names[0];
   whandle.stride = strides[0];
   whandle.offset = offsets[0];
   whandle.format = map->pipe_format;
   whandle.modifier = DRM_FORMAT_MOD_INVALID;

   img = dri_create_image_from_winsys(screen, width, height, map,
                                       1, &whandle, 0, loaderPrivate);
   if (img == NULL)
      return NULL;

   img->dri_components = map->dri_components;
   img->dri_fourcc = map->dri_fourcc;
   img->dri_format = map->dri_format;

   return img;
}

struct dri_image *
dri2_from_planar(struct dri_image *image, int plane, void *loaderPrivate)
{
   struct dri_image *img;

   if (plane < 0) {
      return NULL;
   } else if (plane > 0) {
      uint64_t planes;
      if (!dri2_resource_get_param(image, PIPE_RESOURCE_PARAM_NPLANES, 0,
                                   &planes) ||
          plane >= planes) {
         return NULL;
      }
   }

   if (image->dri_components == 0) {
      uint64_t modifier;
      if (!dri2_resource_get_param(image, PIPE_RESOURCE_PARAM_MODIFIER, 0,
                                   &modifier) ||
          modifier == DRM_FORMAT_MOD_INVALID) {
         return NULL;
      }
   }

   img = dri2_dup_image(image, loaderPrivate);
   if (img == NULL)
      return NULL;

   if (img->texture->screen->resource_changed)
      img->texture->screen->resource_changed(img->texture->screen,
                                             img->texture);

   /* set this to 0 for sub images. */
   img->dri_components = 0;
   img->plane = plane;
   return img;
}

bool
dri_query_dma_buf_modifiers(struct dri_screen *screen, int fourcc, int max,
                             uint64_t *modifiers, unsigned int *external_only,
                             int *count)
{
   struct pipe_screen *pscreen = screen->base.screen;
   const struct dri2_format_mapping *map = dri2_get_mapping_by_fourcc(fourcc);
   enum pipe_format format;

   if (!map)
      return false;

   format = map->pipe_format;

   bool native_sampling = pscreen->is_format_supported(pscreen, format, screen->target, 0, 0,
                                                       PIPE_BIND_SAMPLER_VIEW);
   if (pscreen->is_format_supported(pscreen, format, screen->target, 0, 0,
                                    PIPE_BIND_RENDER_TARGET) ||
       native_sampling ||
       dri2_yuv_dma_buf_supported(screen, map))  {
      if (pscreen->query_dmabuf_modifiers != NULL) {
         pscreen->query_dmabuf_modifiers(pscreen, format, max, modifiers,
                                         external_only, count);
         if (!native_sampling && external_only) {
            /* To support it using YUV lowering, we need it to be samplerExternalOES.
             */
            for (int i = 0; i < *count; i++)
               external_only[i] = true;
         }
      } else {
         *count = 0;
      }
      return true;
   }
   return false;
}

bool
dri2_query_dma_buf_format_modifier_attribs(struct dri_screen *screen,
                                           uint32_t fourcc, uint64_t modifier,
                                           int attrib, uint64_t *value)
{
   struct pipe_screen *pscreen = screen->base.screen;

   if (!pscreen->query_dmabuf_modifiers)
      return false;

   switch (attrib) {
   case __DRI_IMAGE_FORMAT_MODIFIER_ATTRIB_PLANE_COUNT: {
      uint64_t mod_planes = dri2_get_modifier_num_planes(screen, modifier,
                                                         fourcc);
      if (mod_planes > 0)
         *value = mod_planes;
      return mod_planes > 0;
   }
   default:
      return false;
   }
}

struct dri_image *
dri2_from_dma_bufs(struct dri_screen *screen,
                    int width, int height, int fourcc,
                    uint64_t modifier, int *fds, int num_fds,
                    int *strides, int *offsets,
                    enum __DRIYUVColorSpace yuv_color_space,
                    enum __DRISampleRange sample_range,
                    enum __DRIChromaSiting horizontal_siting,
                    enum __DRIChromaSiting vertical_siting,
                    uint32_t dri_flags,
                    unsigned *error,
                    void *loaderPrivate)
{
   struct dri_image *img;
   const struct dri2_format_mapping *map = dri2_get_mapping_by_fourcc(fourcc);

   if (!screen->dmabuf_import) {
      if (error)
         *error = __DRI_IMAGE_ERROR_BAD_PARAMETER;
      return NULL;
   }

   unsigned err = __DRI_IMAGE_ERROR_SUCCESS;
   /* Allow a NULL error arg since many callers don't care. */
   unsigned unused_error;
   if (!error)
      error = &unused_error;

   uint32_t flags = 0;
   if (dri_flags & __DRI_IMAGE_PROTECTED_CONTENT_FLAG)
      flags |= PIPE_BIND_PROTECTED;
   if (dri_flags & __DRI_IMAGE_PRIME_LINEAR_BUFFER)
      flags |= PIPE_BIND_PRIME_BLIT_DST;

   const int expected_num_fds = dri2_get_modifier_num_planes(screen, modifier, fourcc);
   if (!map || expected_num_fds == 0) {
      err = __DRI_IMAGE_ERROR_BAD_MATCH;
      goto exit;
   }

   if (num_fds != expected_num_fds) {
      err = __DRI_IMAGE_ERROR_BAD_MATCH;
      goto exit;
   }

   struct winsys_handle whandles[4];
   memset(whandles, 0, sizeof(whandles));

   for (int i = 0; i < num_fds; i++) {
      if (fds[i] < 0) {
         err = __DRI_IMAGE_ERROR_BAD_ALLOC;
         goto exit;
      }

      whandles[i].type = WINSYS_HANDLE_TYPE_FD;
      whandles[i].handle = (unsigned)fds[i];
      whandles[i].stride = (unsigned)strides[i];
      whandles[i].offset = (unsigned)offsets[i];
      whandles[i].format = map->pipe_format;
      whandles[i].modifier = modifier;
      whandles[i].plane = i;
   }

   img = dri_create_image_from_winsys(screen, width, height, map,
                                       num_fds, whandles, flags,
                                       loaderPrivate);
   if (img == NULL) {
      err = __DRI_IMAGE_ERROR_BAD_ALLOC;
      goto exit;
   }

   img->dri_components = map->dri_components;
   img->dri_fourcc = fourcc;
   img->dri_format = map->dri_format;
   img->imported_dmabuf = true;
   img->yuv_color_space = yuv_color_space;
   img->sample_range = sample_range;
   img->horizontal_siting = horizontal_siting;
   img->vertical_siting = vertical_siting;

   *error = __DRI_IMAGE_ERROR_SUCCESS;
   return img;

exit:
   *error = err;
   return NULL;
}

bool
dri2_query_compression_rates(struct dri_screen *screen, const struct dri_config *config, int max,
                             enum __DRIFixedRateCompression *rates, int *count)
{
   struct pipe_screen *pscreen = screen->base.screen;
   struct gl_config *gl_config = (struct gl_config *) config;
   enum pipe_format format = gl_config->color_format;
   uint32_t pipe_rates[max];

   if (!pscreen->is_format_supported(pscreen, format, screen->target, 0, 0,
                                     PIPE_BIND_RENDER_TARGET))
      return false;

   if (pscreen->query_compression_rates != NULL) {
      pscreen->query_compression_rates(pscreen, format, max, pipe_rates, count);
      for (int i = 0; i < *count && i < max; ++i)
         rates[i] = to_dri_compression_rate(pipe_rates[i]);
   } else {
      *count = 0;
   }

   return true;
}

bool
dri2_query_compression_modifiers(struct dri_screen *screen, uint32_t fourcc,
                                 enum __DRIFixedRateCompression rate, int max,
                                 uint64_t *modifiers, int *count)
{
   struct pipe_screen *pscreen = screen->base.screen;
   const struct dri2_format_mapping *map = dri2_get_mapping_by_fourcc(fourcc);
   uint32_t pipe_rate = from_dri_compression_rate(rate);

   if (!map)
      return false;

   if (!pscreen->is_format_supported(pscreen, map->pipe_format, screen->target,
                                     0, 0, PIPE_BIND_RENDER_TARGET))
      return false;

   if (pscreen->query_compression_modifiers != NULL) {
      pscreen->query_compression_modifiers(pscreen, map->pipe_format, pipe_rate,
                                           max, modifiers, count);
   } else {
      *count = 0;
   }

   return true;
}

void
dri2_blit_image(struct dri_context *ctx, struct dri_image *dst, struct dri_image *src,
                int dstx0, int dsty0, int dstwidth, int dstheight,
                int srcx0, int srcy0, int srcwidth, int srcheight,
                int flush_flag)
{
   struct pipe_context *pipe = ctx->st->pipe;
   struct pipe_screen *screen;
   struct pipe_fence_handle *fence;
   struct pipe_blit_info blit;

   if (!dst || !src)
      return;

   /* Wait for glthread to finish because we can't use pipe_context from
    * multiple threads.
    */
   _mesa_glthread_finish(ctx->st->ctx);

   dri_image_fence_sync(ctx, dst);

   memset(&blit, 0, sizeof(blit));
   blit.dst.resource = dst->texture;
   blit.dst.box.x = dstx0;
   blit.dst.box.y = dsty0;
   blit.dst.box.width = dstwidth;
   blit.dst.box.height = dstheight;
   blit.dst.box.depth = 1;
   blit.dst.format = dst->texture->format;
   blit.src.resource = src->texture;
   blit.src.box.x = srcx0;
   blit.src.box.y = srcy0;
   blit.src.box.width = srcwidth;
   blit.src.box.height = srcheight;
   blit.src.box.depth = 1;
   blit.src.format = src->texture->format;
   blit.mask = PIPE_MASK_RGBA;
   blit.filter = PIPE_TEX_FILTER_NEAREST;

   pipe->blit(pipe, &blit);

   if (flush_flag == __BLIT_FLAG_FLUSH) {
      pipe->flush_resource(pipe, dst->texture);
      st_context_flush(ctx->st, 0, NULL, NULL, NULL);
   } else if (flush_flag == __BLIT_FLAG_FINISH) {
      screen = ctx->screen->base.screen;
      pipe->flush_resource(pipe, dst->texture);
      st_context_flush(ctx->st, 0, &fence, NULL, NULL);
      (void) screen->fence_finish(screen, NULL, fence, OS_TIMEOUT_INFINITE);
      screen->fence_reference(screen, &fence, NULL);
   }
}

void *
dri2_map_image(struct dri_context *ctx, struct dri_image *image,
                int x0, int y0, int width, int height,
                unsigned int flags, int *stride, void **data)
{
   struct pipe_context *pipe = ctx->st->pipe;
   enum pipe_map_flags pipe_access = 0;
   struct pipe_transfer *trans;
   void *map;

   if (!image || !data || *data)
      return NULL;

   unsigned plane = image->plane;
   if (plane >= dri2_get_mapping_by_format(image->dri_format)->nplanes)
      return NULL;

   /* Wait for glthread to finish because we can't use pipe_context from
    * multiple threads.
    */
   _mesa_glthread_finish(ctx->st->ctx);

   dri_image_fence_sync(ctx, image);

   struct pipe_resource *resource = image->texture;
   while (plane--)
      resource = resource->next;

   if (flags & __DRI_IMAGE_TRANSFER_READ)
         pipe_access |= PIPE_MAP_READ;
   if (flags & __DRI_IMAGE_TRANSFER_WRITE)
         pipe_access |= PIPE_MAP_WRITE;

   map = pipe_texture_map(pipe, resource, 0, 0, pipe_access, x0, y0,
                           width, height, &trans);
   if (map) {
      *data = trans;
      *stride = trans->stride;
   }

   return map;
}

void
dri2_unmap_image(struct dri_context *ctx, struct dri_image *image, void *data)
{
   struct pipe_context *pipe = ctx->st->pipe;

   /* Wait for glthread to finish because we can't use pipe_context from
    * multiple threads.
    */
   _mesa_glthread_finish(ctx->st->ctx);

   pipe_texture_unmap(pipe, (struct pipe_transfer *)data);
}

int
dri2_get_capabilities(struct dri_screen *screen)
{
   return (screen->can_share_buffer ? __DRI_IMAGE_CAP_GLOBAL_NAMES : 0);
}

int
dri_interop_query_device_info(struct dri_context *ctx,
                               struct mesa_glinterop_device_info *out)
{
   return st_interop_query_device_info(ctx->st, out);
}

int
dri_interop_export_object(struct dri_context *ctx,
                           struct mesa_glinterop_export_in *in,
                           struct mesa_glinterop_export_out *out)
{
   return st_interop_export_object(ctx->st, in, out);
}

int
dri_interop_flush_objects(struct dri_context *ctx,
                           unsigned count, struct mesa_glinterop_export_in *objects,
                           struct mesa_glinterop_flush_out *out)
{
   return st_interop_flush_objects(ctx->st, count, objects, out);
}

/**
 * \brief the DRI2bufferDamageExtension set_damage_region method
 */
void
dri_set_damage_region(struct dri_drawable *drawable, unsigned int nrects, int *rects)
{
   struct pipe_box *boxes = NULL;

   if (nrects) {
      boxes = CALLOC(nrects, sizeof(*boxes));
      assert(boxes);

      for (unsigned int i = 0; i < nrects; i++) {
         int *rect = &rects[i * 4];

         u_box_2d(rect[0], rect[1], rect[2], rect[3], &boxes[i]);
      }
   }

   FREE(drawable->damage_rects);
   drawable->damage_rects = boxes;
   drawable->num_damage_rects = nrects;

   /* Only apply the damage region if the BACK_LEFT texture is up-to-date. */
   if (drawable->texture_stamp == drawable->lastStamp &&
       (drawable->texture_mask & (1 << ST_ATTACHMENT_BACK_LEFT))) {
      struct pipe_screen *screen = drawable->screen->base.screen;
      struct pipe_resource *resource;

      if (drawable->stvis.samples > 1)
         resource = drawable->msaa_textures[ST_ATTACHMENT_BACK_LEFT];
      else
         resource = drawable->textures[ST_ATTACHMENT_BACK_LEFT];

      screen->set_damage_region(screen, resource,
                                drawable->num_damage_rects,
                                drawable->damage_rects);
   }
}

/**
 * \brief the DRI2blobExtension set_cache_funcs method
 */
void
dri_set_blob_cache_funcs(struct dri_screen *screen, __DRIblobCacheSet set,
                         __DRIblobCacheGet get)
{
   struct pipe_screen *pscreen = screen->base.screen;

   if (!pscreen->get_disk_shader_cache)
      return;

   struct disk_cache *cache = pscreen->get_disk_shader_cache(pscreen);

   if (!cache)
      return;

   disk_cache_set_callbacks(cache, set, get);
}

/*
 * Backend function init_screen.
 */

void
dri2_init_drawable(struct dri_drawable *drawable, bool isPixmap, int alphaBits)
{
   drawable->allocate_textures = dri2_allocate_textures;
   drawable->flush_frontbuffer = dri2_flush_frontbuffer;
   drawable->update_tex_buffer = dri2_update_tex_buffer;
   drawable->flush_swapbuffers = dri2_flush_swapbuffers;
}

/**
 * This is the driver specific part of the createNewScreen entry point.
 *
 * Returns the struct gl_config supported by this driver.
 */
struct pipe_screen *
dri2_init_screen(struct dri_screen *screen, bool driver_name_is_inferred)
{
   struct pipe_screen *pscreen = NULL;

   screen->can_share_buffer = true;
   screen->auto_fake_front = dri_with_format(screen);

#ifdef HAVE_LIBDRM
   if (pipe_loader_drm_probe_fd(&screen->dev, screen->fd, false))
      pscreen = pipe_loader_create_screen(screen->dev, driver_name_is_inferred);
#endif

   return pscreen;
}

/**
 * This is the driver specific part of the createNewScreen entry point.
 *
 * Returns the struct gl_config supported by this driver.
 */
struct pipe_screen *
dri_swrast_kms_init_screen(struct dri_screen *screen, bool driver_name_is_inferred)
{
   struct pipe_screen *pscreen = NULL;
   screen->can_share_buffer = false;
   screen->auto_fake_front = dri_with_format(screen);

#if defined(HAVE_DRISW_KMS) && defined(HAVE_SWRAST)
   if (pipe_loader_sw_probe_kms(&screen->dev, screen->fd))
      pscreen = pipe_loader_create_screen(screen->dev, driver_name_is_inferred);
#endif

   return pscreen;
}

int
dri_query_compatible_render_only_device_fd(int kms_only_fd)
{
#ifdef HAVE_LIBDRM
   return pipe_loader_get_compatible_render_capable_device_fd(kms_only_fd);
#else
   return -1;
#endif
}
/* vim: set sw=3 ts=8 sts=3 expandtab: */
