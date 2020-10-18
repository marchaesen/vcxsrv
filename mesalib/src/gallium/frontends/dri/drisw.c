/**************************************************************************
 *
 * Copyright 2009, VMware, Inc.
 * All Rights Reserved.
 * Copyright 2010 George Sapountzis <gsapountzis@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#include "util/format/u_format.h"
#include "util/u_memory.h"
#include "util/u_inlines.h"
#include "util/u_box.h"
#include "pipe/p_context.h"
#include "pipe-loader/pipe_loader.h"
#include "frontend/drisw_api.h"
#include "state_tracker/st_context.h"

#include "dri_screen.h"
#include "dri_context.h"
#include "dri_drawable.h"
#include "dri_helpers.h"
#include "dri_query_renderer.h"

DEBUG_GET_ONCE_BOOL_OPTION(swrast_no_present, "SWRAST_NO_PRESENT", FALSE);

static inline void
get_drawable_info(__DRIdrawable *dPriv, int *x, int *y, int *w, int *h)
{
   __DRIscreen *sPriv = dPriv->driScreenPriv;
   const __DRIswrastLoaderExtension *loader = sPriv->swrast_loader;

   loader->getDrawableInfo(dPriv,
                           x, y, w, h,
                           dPriv->loaderPrivate);
}

static inline void
put_image(__DRIdrawable *dPriv, void *data, unsigned width, unsigned height)
{
   __DRIscreen *sPriv = dPriv->driScreenPriv;
   const __DRIswrastLoaderExtension *loader = sPriv->swrast_loader;

   loader->putImage(dPriv, __DRI_SWRAST_IMAGE_OP_SWAP,
                    0, 0, width, height,
                    data, dPriv->loaderPrivate);
}

static inline void
put_image2(__DRIdrawable *dPriv, void *data, int x, int y,
           unsigned width, unsigned height, unsigned stride)
{
   __DRIscreen *sPriv = dPriv->driScreenPriv;
   const __DRIswrastLoaderExtension *loader = sPriv->swrast_loader;

   loader->putImage2(dPriv, __DRI_SWRAST_IMAGE_OP_SWAP,
                     x, y, width, height, stride,
                     data, dPriv->loaderPrivate);
}

static inline void
put_image_shm(__DRIdrawable *dPriv, int shmid, char *shmaddr,
              unsigned offset, unsigned offset_x, int x, int y,
              unsigned width, unsigned height, unsigned stride)
{
   __DRIscreen *sPriv = dPriv->driScreenPriv;
   const __DRIswrastLoaderExtension *loader = sPriv->swrast_loader;

   /* if we have the newer interface, don't have to add the offset_x here. */
   if (loader->base.version > 4 && loader->putImageShm2)
     loader->putImageShm2(dPriv, __DRI_SWRAST_IMAGE_OP_SWAP,
                          x, y, width, height, stride,
                          shmid, shmaddr, offset, dPriv->loaderPrivate);
   else
     loader->putImageShm(dPriv, __DRI_SWRAST_IMAGE_OP_SWAP,
                         x, y, width, height, stride,
                         shmid, shmaddr, offset + offset_x, dPriv->loaderPrivate);
}

static inline void
get_image(__DRIdrawable *dPriv, int x, int y, int width, int height, void *data)
{
   __DRIscreen *sPriv = dPriv->driScreenPriv;
   const __DRIswrastLoaderExtension *loader = sPriv->swrast_loader;

   loader->getImage(dPriv,
                    x, y, width, height,
                    data, dPriv->loaderPrivate);
}

static inline void
get_image2(__DRIdrawable *dPriv, int x, int y, int width, int height, int stride, void *data)
{
   __DRIscreen *sPriv = dPriv->driScreenPriv;
   const __DRIswrastLoaderExtension *loader = sPriv->swrast_loader;

   /* getImage2 support is only in version 3 or newer */
   if (loader->base.version < 3)
      return;

   loader->getImage2(dPriv,
                     x, y, width, height, stride,
                     data, dPriv->loaderPrivate);
}

static inline bool
get_image_shm(__DRIdrawable *dPriv, int x, int y, int width, int height,
              struct pipe_resource *res)
{
   __DRIscreen *sPriv = dPriv->driScreenPriv;
   const __DRIswrastLoaderExtension *loader = sPriv->swrast_loader;
   struct winsys_handle whandle;

   whandle.type = WINSYS_HANDLE_TYPE_SHMID;

   if (loader->base.version < 4 || !loader->getImageShm)
      return FALSE;

   if (!res->screen->resource_get_handle(res->screen, NULL, res, &whandle, PIPE_HANDLE_USAGE_FRAMEBUFFER_WRITE))
      return FALSE;

   if (loader->base.version > 5 && loader->getImageShm2)
      return loader->getImageShm2(dPriv, x, y, width, height, whandle.handle, dPriv->loaderPrivate);

   loader->getImageShm(dPriv, x, y, width, height, whandle.handle, dPriv->loaderPrivate);
   return TRUE;
}

static void
drisw_update_drawable_info(struct dri_drawable *drawable)
{
   __DRIdrawable *dPriv = drawable->dPriv;
   int x, y;

   get_drawable_info(dPriv, &x, &y, &dPriv->w, &dPriv->h);
}

static void
drisw_get_image(struct dri_drawable *drawable,
                int x, int y, unsigned width, unsigned height, unsigned stride,
                void *data)
{
   __DRIdrawable *dPriv = drawable->dPriv;
   int draw_x, draw_y, draw_w, draw_h;

   get_drawable_info(dPriv, &draw_x, &draw_y, &draw_w, &draw_h);
   get_image2(dPriv, x, y, draw_w, draw_h, stride, data);
}

static void
drisw_put_image(struct dri_drawable *drawable,
                void *data, unsigned width, unsigned height)
{
   __DRIdrawable *dPriv = drawable->dPriv;

   put_image(dPriv, data, width, height);
}

static void
drisw_put_image2(struct dri_drawable *drawable,
                 void *data, int x, int y, unsigned width, unsigned height,
                 unsigned stride)
{
   __DRIdrawable *dPriv = drawable->dPriv;

   put_image2(dPriv, data, x, y, width, height, stride);
}

static inline void
drisw_put_image_shm(struct dri_drawable *drawable,
                    int shmid, char *shmaddr, unsigned offset,
                    unsigned offset_x,
                    int x, int y, unsigned width, unsigned height,
                    unsigned stride)
{
   __DRIdrawable *dPriv = drawable->dPriv;

   put_image_shm(dPriv, shmid, shmaddr, offset, offset_x, x, y, width, height, stride);
}

static inline void
drisw_present_texture(__DRIdrawable *dPriv,
                      struct pipe_resource *ptex, struct pipe_box *sub_box)
{
   struct dri_drawable *drawable = dri_drawable(dPriv);
   struct dri_screen *screen = dri_screen(drawable->sPriv);

   if (screen->swrast_no_present)
      return;

   screen->base.screen->flush_frontbuffer(screen->base.screen, ptex, 0, 0, drawable, sub_box);
}

static inline void
drisw_invalidate_drawable(__DRIdrawable *dPriv)
{
   struct dri_drawable *drawable = dri_drawable(dPriv);

   drawable->texture_stamp = dPriv->lastStamp - 1;

   p_atomic_inc(&drawable->base.stamp);
}

static inline void
drisw_copy_to_front(__DRIdrawable * dPriv,
                    struct pipe_resource *ptex)
{
   drisw_present_texture(dPriv, ptex, NULL);

   drisw_invalidate_drawable(dPriv);
}

/*
 * Backend functions for st_framebuffer interface and swap_buffers.
 */

static void
drisw_swap_buffers(__DRIdrawable *dPriv)
{
   struct dri_context *ctx = dri_get_current(dPriv->driScreenPriv);
   struct dri_drawable *drawable = dri_drawable(dPriv);
   struct pipe_resource *ptex;

   if (!ctx)
      return;

   ptex = drawable->textures[ST_ATTACHMENT_BACK_LEFT];

   if (ptex) {
      if (ctx->pp)
         pp_run(ctx->pp, ptex, ptex, drawable->textures[ST_ATTACHMENT_DEPTH_STENCIL]);

      if (ctx->hud)
         hud_run(ctx->hud, ctx->st->cso_context, ptex);

      ctx->st->flush(ctx->st, ST_FLUSH_FRONT, NULL, NULL, NULL);

      if (drawable->stvis.samples > 1) {
         /* Resolve the back buffer. */
         dri_pipe_blit(ctx->st->pipe,
                       drawable->textures[ST_ATTACHMENT_BACK_LEFT],
                       drawable->msaa_textures[ST_ATTACHMENT_BACK_LEFT]);
      }

      drisw_copy_to_front(dPriv, ptex);
   }
}

static void
drisw_copy_sub_buffer(__DRIdrawable *dPriv, int x, int y,
                      int w, int h)
{
   struct dri_context *ctx = dri_get_current(dPriv->driScreenPriv);
   struct dri_drawable *drawable = dri_drawable(dPriv);
   struct pipe_resource *ptex;
   struct pipe_box box;
   if (!ctx)
      return;

   ptex = drawable->textures[ST_ATTACHMENT_BACK_LEFT];

   if (ptex) {
      if (ctx->pp && drawable->textures[ST_ATTACHMENT_DEPTH_STENCIL])
         pp_run(ctx->pp, ptex, ptex, drawable->textures[ST_ATTACHMENT_DEPTH_STENCIL]);

      ctx->st->flush(ctx->st, ST_FLUSH_FRONT, NULL, NULL, NULL);

      u_box_2d(x, dPriv->h - y - h, w, h, &box);
      drisw_present_texture(dPriv, ptex, &box);
   }
}

static void
drisw_flush_frontbuffer(struct dri_context *ctx,
                        struct dri_drawable *drawable,
                        enum st_attachment_type statt)
{
   struct pipe_resource *ptex;

   if (!ctx)
      return;

   if (drawable->stvis.samples > 1) {
      /* Resolve the front buffer. */
      dri_pipe_blit(ctx->st->pipe,
                    drawable->textures[ST_ATTACHMENT_FRONT_LEFT],
                    drawable->msaa_textures[ST_ATTACHMENT_FRONT_LEFT]);
   }
   ptex = drawable->textures[statt];

   if (ptex) {
      drisw_copy_to_front(ctx->dPriv, ptex);
   }
}

/**
 * Allocate framebuffer attachments.
 *
 * During fixed-size operation, the function keeps allocating new attachments
 * as they are requested. Unused attachments are not removed, not until the
 * framebuffer is resized or destroyed.
 */
static void
drisw_allocate_textures(struct dri_context *stctx,
                        struct dri_drawable *drawable,
                        const enum st_attachment_type *statts,
                        unsigned count)
{
   struct dri_screen *screen = dri_screen(drawable->sPriv);
   const __DRIswrastLoaderExtension *loader = drawable->dPriv->driScreenPriv->swrast_loader;
   struct pipe_resource templ;
   unsigned width, height;
   boolean resized;
   unsigned i;

   width  = drawable->dPriv->w;
   height = drawable->dPriv->h;

   resized = (drawable->old_w != width ||
              drawable->old_h != height);

   /* remove outdated textures */
   if (resized) {
      for (i = 0; i < ST_ATTACHMENT_COUNT; i++) {
         pipe_resource_reference(&drawable->textures[i], NULL);
         pipe_resource_reference(&drawable->msaa_textures[i], NULL);
      }
   }

   memset(&templ, 0, sizeof(templ));
   templ.target = screen->target;
   templ.width0 = width;
   templ.height0 = height;
   templ.depth0 = 1;
   templ.array_size = 1;
   templ.last_level = 0;

   for (i = 0; i < count; i++) {
      enum pipe_format format;
      unsigned bind;

      /* the texture already exists or not requested */
      if (drawable->textures[statts[i]])
         continue;

      dri_drawable_get_format(drawable, statts[i], &format, &bind);

      /* if we don't do any present, no need for display targets */
      if (statts[i] != ST_ATTACHMENT_DEPTH_STENCIL && !screen->swrast_no_present)
         bind |= PIPE_BIND_DISPLAY_TARGET | PIPE_BIND_LINEAR;

      if (format == PIPE_FORMAT_NONE)
         continue;

      templ.format = format;
      templ.bind = bind;
      templ.nr_samples = 0;
      templ.nr_storage_samples = 0;

      if (statts[i] == ST_ATTACHMENT_FRONT_LEFT &&
          screen->base.screen->resource_create_front &&
          loader->base.version >= 3) {
         drawable->textures[statts[i]] =
            screen->base.screen->resource_create_front(screen->base.screen, &templ, (const void *)drawable);
      } else
         drawable->textures[statts[i]] =
            screen->base.screen->resource_create(screen->base.screen, &templ);

      if (drawable->stvis.samples > 1) {
         templ.bind = templ.bind &
            ~(PIPE_BIND_SCANOUT | PIPE_BIND_SHARED | PIPE_BIND_DISPLAY_TARGET);
         templ.nr_samples = drawable->stvis.samples;
         templ.nr_storage_samples = drawable->stvis.samples;
         drawable->msaa_textures[statts[i]] =
            screen->base.screen->resource_create(screen->base.screen, &templ);

         dri_pipe_blit(stctx->st->pipe,
                       drawable->msaa_textures[statts[i]],
                       drawable->textures[statts[i]]);
      }
   }

   drawable->old_w = width;
   drawable->old_h = height;
}

static void
drisw_update_tex_buffer(struct dri_drawable *drawable,
                        struct dri_context *ctx,
                        struct pipe_resource *res)
{
   __DRIdrawable *dPriv = drawable->dPriv;

   struct st_context *st_ctx = (struct st_context *)ctx->st;
   struct pipe_context *pipe = st_ctx->pipe;
   struct pipe_transfer *transfer;
   char *map;
   int x, y, w, h;
   int ximage_stride, line;
   int cpp = util_format_get_blocksize(res->format);

   get_drawable_info(dPriv, &x, &y, &w, &h);

   map = pipe_transfer_map(pipe, res,
                           0, 0, // level, layer,
                           PIPE_MAP_WRITE,
                           x, y, w, h, &transfer);

   /* Copy the Drawable content to the mapped texture buffer */
   if (!get_image_shm(dPriv, x, y, w, h, res))
      get_image(dPriv, x, y, w, h, map);

   /* The pipe transfer has a pitch rounded up to the nearest 64 pixels.
      get_image() has a pitch rounded up to 4 bytes.  */
   ximage_stride = ((w * cpp) + 3) & -4;
   for (line = h-1; line; --line) {
      memmove(&map[line * transfer->stride],
              &map[line * ximage_stride],
              ximage_stride);
   }

   pipe_transfer_unmap(pipe, transfer);
}

static __DRIimageExtension driSWImageExtension = {
    .base = { __DRI_IMAGE, 6 },

    .createImageFromRenderbuffer  = dri2_create_image_from_renderbuffer,
    .createImageFromTexture = dri2_create_from_texture,
    .destroyImage = dri2_destroy_image,
};

static const __DRIrobustnessExtension dri2Robustness = {
   .base = { __DRI2_ROBUSTNESS, 1 }
};

/*
 * Backend function for init_screen.
 */

static const __DRIextension *drisw_screen_extensions[] = {
   &driTexBufferExtension.base,
   &dri2RendererQueryExtension.base,
   &dri2ConfigQueryExtension.base,
   &dri2FenceExtension.base,
   &dri2NoErrorExtension.base,
   &driSWImageExtension.base,
   &dri2FlushControlExtension.base,
   NULL
};

static const __DRIextension *drisw_robust_screen_extensions[] = {
   &driTexBufferExtension.base,
   &dri2RendererQueryExtension.base,
   &dri2ConfigQueryExtension.base,
   &dri2FenceExtension.base,
   &dri2NoErrorExtension.base,
   &dri2Robustness.base,
   &driSWImageExtension.base,
   &dri2FlushControlExtension.base,
   NULL
};

static const struct drisw_loader_funcs drisw_lf = {
   .get_image = drisw_get_image,
   .put_image = drisw_put_image,
   .put_image2 = drisw_put_image2
};

static const struct drisw_loader_funcs drisw_shm_lf = {
   .get_image = drisw_get_image,
   .put_image = drisw_put_image,
   .put_image2 = drisw_put_image2,
   .put_image_shm = drisw_put_image_shm
};

static const __DRIconfig **
drisw_init_screen(__DRIscreen * sPriv)
{
   const __DRIswrastLoaderExtension *loader = sPriv->swrast_loader;
   const __DRIconfig **configs;
   struct dri_screen *screen;
   struct pipe_screen *pscreen = NULL;
   const struct drisw_loader_funcs *lf = &drisw_lf;

   screen = CALLOC_STRUCT(dri_screen);
   if (!screen)
      return NULL;

   screen->sPriv = sPriv;
   screen->fd = -1;

   screen->swrast_no_present = debug_get_option_swrast_no_present();

   sPriv->driverPrivate = (void *)screen;

   if (loader->base.version >= 4) {
      if (loader->putImageShm)
         lf = &drisw_shm_lf;
   }

   if (pipe_loader_sw_probe_dri(&screen->dev, lf)) {
      dri_init_options(screen);

      pscreen = pipe_loader_create_screen(screen->dev);
   }

   if (!pscreen)
      goto fail;

   configs = dri_init_screen_helper(screen, pscreen);
   if (!configs)
      goto fail;

   if (pscreen->get_param(pscreen, PIPE_CAP_DEVICE_RESET_STATUS_QUERY)) {
      sPriv->extensions = drisw_robust_screen_extensions;
      screen->has_reset_status_query = true;
   }
   else
      sPriv->extensions = drisw_screen_extensions;
   screen->lookup_egl_image = dri2_lookup_egl_image;

   return configs;
fail:
   dri_destroy_screen_helper(screen);
   if (screen->dev)
      pipe_loader_release(&screen->dev, 1);
   FREE(screen);
   return NULL;
}

static boolean
drisw_create_buffer(__DRIscreen * sPriv,
                    __DRIdrawable * dPriv,
                    const struct gl_config * visual, boolean isPixmap)
{
   struct dri_drawable *drawable = NULL;

   if (!dri_create_buffer(sPriv, dPriv, visual, isPixmap))
      return FALSE;

   drawable = dPriv->driverPrivate;

   drawable->allocate_textures = drisw_allocate_textures;
   drawable->update_drawable_info = drisw_update_drawable_info;
   drawable->flush_frontbuffer = drisw_flush_frontbuffer;
   drawable->update_tex_buffer = drisw_update_tex_buffer;

   return TRUE;
}

/**
 * DRI driver virtual function table.
 *
 * DRI versions differ in their implementation of init_screen and swap_buffers.
 */
const struct __DriverAPIRec galliumsw_driver_api = {
   .InitScreen = drisw_init_screen,
   .DestroyScreen = dri_destroy_screen,
   .CreateContext = dri_create_context,
   .DestroyContext = dri_destroy_context,
   .CreateBuffer = drisw_create_buffer,
   .DestroyBuffer = dri_destroy_buffer,
   .SwapBuffers = drisw_swap_buffers,
   .MakeCurrent = dri_make_current,
   .UnbindContext = dri_unbind_context,
   .CopySubBuffer = drisw_copy_sub_buffer,
};

/* This is the table of extensions that the loader will dlsym() for. */
const __DRIextension *galliumsw_driver_extensions[] = {
    &driCoreExtension.base,
    &driSWRastExtension.base,
    &driCopySubBufferExtension.base,
    &gallium_config_options.base,
    NULL
};

/* vim: set sw=3 ts=8 sts=3 expandtab: */
