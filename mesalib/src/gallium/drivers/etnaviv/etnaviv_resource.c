/*
 * Copyright (c) 2012-2015 Etnaviv Project
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Wladimir J. van der Laan <laanwj@gmail.com>
 */

#include "etnaviv_resource.h"

#include "hw/common.xml.h"

#include "etnaviv_context.h"
#include "etnaviv_debug.h"
#include "etnaviv_screen.h"
#include "etnaviv_translate.h"

#include "util/hash_table.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"

#include "drm-uapi/drm_fourcc.h"

static enum etna_surface_layout modifier_to_layout(uint64_t modifier)
{
   switch (modifier) {
   case DRM_FORMAT_MOD_VIVANTE_TILED:
      return ETNA_LAYOUT_TILED;
   case DRM_FORMAT_MOD_VIVANTE_SUPER_TILED:
      return ETNA_LAYOUT_SUPER_TILED;
   case DRM_FORMAT_MOD_VIVANTE_SPLIT_TILED:
      return ETNA_LAYOUT_MULTI_TILED;
   case DRM_FORMAT_MOD_VIVANTE_SPLIT_SUPER_TILED:
      return ETNA_LAYOUT_MULTI_SUPERTILED;
   case DRM_FORMAT_MOD_LINEAR:
   default:
      return ETNA_LAYOUT_LINEAR;
   }
}

static uint64_t layout_to_modifier(enum etna_surface_layout layout)
{
   switch (layout) {
   case ETNA_LAYOUT_TILED:
      return DRM_FORMAT_MOD_VIVANTE_TILED;
   case ETNA_LAYOUT_SUPER_TILED:
      return DRM_FORMAT_MOD_VIVANTE_SUPER_TILED;
   case ETNA_LAYOUT_MULTI_TILED:
      return DRM_FORMAT_MOD_VIVANTE_SPLIT_TILED;
   case ETNA_LAYOUT_MULTI_SUPERTILED:
      return DRM_FORMAT_MOD_VIVANTE_SPLIT_SUPER_TILED;
   case ETNA_LAYOUT_LINEAR:
      return DRM_FORMAT_MOD_LINEAR;
   default:
      return DRM_FORMAT_MOD_INVALID;
   }
}

/* A tile is 4x4 pixels, having 'screen->specs.bits_per_tile' of tile status.
 * So, in a buffer of N pixels, there are N / (4 * 4) tiles.
 * We need N * screen->specs.bits_per_tile / (4 * 4) bits of tile status, or
 * N * screen->specs.bits_per_tile / (4 * 4 * 8) bytes.
 */
bool
etna_screen_resource_alloc_ts(struct pipe_screen *pscreen,
                              struct etna_resource *rsc)
{
   struct etna_screen *screen = etna_screen(pscreen);
   size_t rt_ts_size, ts_layer_stride;
   size_t ts_bits_per_tile, bytes_per_tile;
   uint8_t ts_mode = TS_MODE_128B; /* only used by halti5 */
   int8_t ts_compress_fmt;

   assert(!rsc->ts_bo);

   /* pre-v4 compression is largely useless, so disable it when not wanted for MSAA
    * v4 compression can be enabled everywhere without any known drawback,
    * except that in-place resolve must go through a slower path
    */
   ts_compress_fmt = (screen->specs.v4_compression || rsc->base.nr_samples > 1) ?
                      translate_ts_format(rsc->base.format) : -1;

   if (screen->specs.halti >= 5) {
      /* enable 256B ts mode with compression, as it improves performance
       * the size of the resource might also determine if we want to use it or not
       */
      if (ts_compress_fmt >= 0)
         ts_mode = TS_MODE_256B;

      ts_bits_per_tile = 4;
      bytes_per_tile = ts_mode == TS_MODE_256B ? 256 : 128;
   } else {
      ts_bits_per_tile = screen->specs.bits_per_tile;
      bytes_per_tile = 64;
   }

   ts_layer_stride = align(DIV_ROUND_UP(rsc->levels[0].layer_stride,
                                        bytes_per_tile * 8 / ts_bits_per_tile),
                           0x100 * screen->specs.pixel_pipes);
   rt_ts_size = ts_layer_stride * rsc->base.array_size;
   if (rt_ts_size == 0)
      return true;

   DBG_F(ETNA_DBG_RESOURCE_MSGS, "%p: Allocating tile status of size %zu",
         rsc, rt_ts_size);

   struct etna_bo *rt_ts;
   rt_ts = etna_bo_new(screen->dev, rt_ts_size, DRM_ETNA_GEM_CACHE_WC);

   if (unlikely(!rt_ts)) {
      BUG("Problem allocating tile status for resource");
      return false;
   }

   rsc->ts_bo = rt_ts;
   rsc->levels[0].ts_offset = 0;
   rsc->levels[0].ts_layer_stride = ts_layer_stride;
   rsc->levels[0].ts_size = rt_ts_size;
   rsc->levels[0].ts_mode = ts_mode;
   rsc->levels[0].ts_compress_fmt = ts_compress_fmt;

   return true;
}

static bool
etna_screen_can_create_resource(struct pipe_screen *pscreen,
                                const struct pipe_resource *templat)
{
   struct etna_screen *screen = etna_screen(pscreen);
   if (!translate_samples_to_xyscale(templat->nr_samples, NULL, NULL))
      return false;

   /* templat->bind is not set here, so we must use the minimum sizes */
   uint max_size =
      MIN2(screen->specs.max_rendertarget_size, screen->specs.max_texture_size);

   if (templat->width0 > max_size || templat->height0 > max_size)
      return false;

   return true;
}

static unsigned
setup_miptree(struct etna_resource *rsc, unsigned paddingX, unsigned paddingY,
              unsigned msaa_xscale, unsigned msaa_yscale)
{
   struct pipe_resource *prsc = &rsc->base;
   unsigned level, size = 0;
   unsigned width = prsc->width0;
   unsigned height = prsc->height0;
   unsigned depth = prsc->depth0;

   for (level = 0; level <= prsc->last_level; level++) {
      struct etna_resource_level *mip = &rsc->levels[level];

      mip->width = width;
      mip->height = height;
      mip->depth = depth;
      mip->padded_width = align(width * msaa_xscale, paddingX);
      mip->padded_height = align(height * msaa_yscale, paddingY);
      mip->stride = util_format_get_stride(prsc->format, mip->padded_width);
      mip->offset = size;
      mip->layer_stride = mip->stride * util_format_get_nblocksy(prsc->format, mip->padded_height);
      mip->size = prsc->array_size * mip->layer_stride;

      /* align levels to 64 bytes to be able to render to them */
      size += align(mip->size, ETNA_PE_ALIGNMENT) * depth;

      width = u_minify(width, 1);
      height = u_minify(height, 1);
      depth = u_minify(depth, 1);
   }

   return size;
}

/* Is rs alignment needed? */
static bool is_rs_align(struct etna_screen *screen,
                        const struct pipe_resource *tmpl)
{
   return screen->specs.use_blt ? false : (
      VIV_FEATURE(screen, chipMinorFeatures1, TEXTURE_HALIGN) ||
      !etna_resource_sampler_only(tmpl));
}

/* Create a new resource object, using the given template info */
struct pipe_resource *
etna_resource_alloc(struct pipe_screen *pscreen, unsigned layout,
                    uint64_t modifier, const struct pipe_resource *templat)
{
   struct etna_screen *screen = etna_screen(pscreen);
   struct etna_resource *rsc;
   unsigned size;

   DBG_F(ETNA_DBG_RESOURCE_MSGS,
         "target=%d, format=%s, %ux%ux%u, array_size=%u, "
         "last_level=%u, nr_samples=%u, usage=%u, bind=%x, flags=%x",
         templat->target, util_format_name(templat->format), templat->width0,
         templat->height0, templat->depth0, templat->array_size,
         templat->last_level, templat->nr_samples, templat->usage,
         templat->bind, templat->flags);

   /* Determine scaling for antialiasing, allow override using debug flag */
   int nr_samples = templat->nr_samples;
   if ((templat->bind & (PIPE_BIND_RENDER_TARGET | PIPE_BIND_DEPTH_STENCIL)) &&
       !(templat->bind & PIPE_BIND_SAMPLER_VIEW)) {
      if (DBG_ENABLED(ETNA_DBG_MSAA_2X))
         nr_samples = 2;
      if (DBG_ENABLED(ETNA_DBG_MSAA_4X))
         nr_samples = 4;
   }

   int msaa_xscale = 1, msaa_yscale = 1;
   if (!translate_samples_to_xyscale(nr_samples, &msaa_xscale, &msaa_yscale)) {
      /* Number of samples not supported */
      return NULL;
   }

   /* Determine needed padding (alignment of height/width) */
   unsigned paddingX = 0, paddingY = 0;
   unsigned halign = TEXTURE_HALIGN_FOUR;
   if (!util_format_is_compressed(templat->format)) {
      /* If we have the TEXTURE_HALIGN feature, we can always align to the
       * resolve engine's width.  If not, we must not align resources used
       * only for textures. If this GPU uses the BLT engine, never do RS align.
       */
      etna_layout_multiple(layout, screen->specs.pixel_pipes,
                           is_rs_align (screen, templat),
                           &paddingX, &paddingY, &halign);
      assert(paddingX && paddingY);
   } else {
      /* Compressed textures are padded to their block size, but we don't have
       * to do anything special for that. */
      paddingX = 1;
      paddingY = 1;
   }

   if (!screen->specs.use_blt && templat->target != PIPE_BUFFER && layout == ETNA_LAYOUT_LINEAR)
      paddingY = align(paddingY, ETNA_RS_HEIGHT_MASK + 1);

   rsc = CALLOC_STRUCT(etna_resource);
   if (!rsc)
      return NULL;

   rsc->base = *templat;
   rsc->base.screen = pscreen;
   rsc->base.nr_samples = nr_samples;
   rsc->layout = layout;
   rsc->halign = halign;

   pipe_reference_init(&rsc->base.reference, 1);
   util_range_init(&rsc->valid_buffer_range);

   size = setup_miptree(rsc, paddingX, paddingY, msaa_xscale, msaa_yscale);

   if (unlikely(templat->bind & PIPE_BIND_SCANOUT) && screen->ro->kms_fd >= 0) {
      struct pipe_resource scanout_templat = *templat;
      struct winsys_handle handle;

      scanout_templat.width0 = align(scanout_templat.width0, paddingX);
      scanout_templat.height0 = align(scanout_templat.height0, paddingY);

      rsc->scanout = renderonly_scanout_for_resource(&scanout_templat,
                                                     screen->ro, &handle);
      if (!rsc->scanout) {
         BUG("Problem allocating kms memory for resource");
         goto free_rsc;
      }

      assert(handle.type == WINSYS_HANDLE_TYPE_FD);
      rsc->levels[0].stride = handle.stride;
      rsc->bo = etna_screen_bo_from_handle(pscreen, &handle);
      close(handle.handle);
      if (unlikely(!rsc->bo))
         goto free_rsc;
   } else {
      uint32_t flags = DRM_ETNA_GEM_CACHE_WC;

      if (templat->bind & PIPE_BIND_VERTEX_BUFFER)
         flags |= DRM_ETNA_GEM_FORCE_MMU;

      rsc->bo = etna_bo_new(screen->dev, size, flags);
      if (unlikely(!rsc->bo)) {
         BUG("Problem allocating video memory for resource");
         goto free_rsc;
      }
   }

   if (DBG_ENABLED(ETNA_DBG_ZERO)) {
      void *map = etna_bo_map(rsc->bo);
      etna_bo_cpu_prep(rsc->bo, DRM_ETNA_PREP_WRITE);
      memset(map, 0, size);
      etna_bo_cpu_fini(rsc->bo);
   }

   mtx_init(&rsc->lock, mtx_recursive);
   rsc->pending_ctx = _mesa_set_create(NULL, _mesa_hash_pointer,
                                       _mesa_key_pointer_equal);
   if (!rsc->pending_ctx)
      goto free_rsc;

   return &rsc->base;

free_rsc:
   FREE(rsc);
   return NULL;
}

static struct pipe_resource *
etna_resource_create(struct pipe_screen *pscreen,
                     const struct pipe_resource *templat)
{
   struct etna_screen *screen = etna_screen(pscreen);
   unsigned layout = ETNA_LAYOUT_TILED;

   /* At this point we don't know if the resource will be used as a texture,
    * render target, or both, because gallium sets the bits whenever possible
    * This matters because on some GPUs (GC2000) there is no tiling that is
    * compatible with both TE and PE.
    *
    * We expect that depth/stencil buffers will always be used by PE (rendering),
    * and any other non-scanout resource will be used as a texture at some point,
    * So allocate a render-compatible base buffer for scanout/depthstencil buffers,
    * and a texture-compatible base buffer in other cases
    *
    */
   if (templat->bind & PIPE_BIND_DEPTH_STENCIL) {
      if (screen->specs.pixel_pipes > 1 && !screen->specs.single_buffer)
         layout |= ETNA_LAYOUT_BIT_MULTI;
      if (screen->specs.can_supertile)
         layout |= ETNA_LAYOUT_BIT_SUPER;
   } else if (VIV_FEATURE(screen, chipMinorFeatures2, SUPERTILED_TEXTURE) &&
              etna_resource_hw_tileable(screen->specs.use_blt, templat)) {
      layout |= ETNA_LAYOUT_BIT_SUPER;
   }

   if (/* linear base or scanout without modifier requested */
       (templat->bind & (PIPE_BIND_LINEAR | PIPE_BIND_SCANOUT)) ||
       templat->target == PIPE_BUFFER || /* buffer always linear */
       /* compressed textures don't use tiling, they have their own "tiles" */
       util_format_is_compressed(templat->format)) {
      layout = ETNA_LAYOUT_LINEAR;
   }

   /* modifier is only used for scanout surfaces, so safe to use LINEAR here */
   return etna_resource_alloc(pscreen, layout, DRM_FORMAT_MOD_LINEAR, templat);
}

enum modifier_priority {
   MODIFIER_PRIORITY_INVALID = 0,
   MODIFIER_PRIORITY_LINEAR,
   MODIFIER_PRIORITY_SPLIT_TILED,
   MODIFIER_PRIORITY_SPLIT_SUPER_TILED,
   MODIFIER_PRIORITY_TILED,
   MODIFIER_PRIORITY_SUPER_TILED,
};

static const uint64_t priority_to_modifier[] = {
   [MODIFIER_PRIORITY_INVALID] = DRM_FORMAT_MOD_INVALID,
   [MODIFIER_PRIORITY_LINEAR] = DRM_FORMAT_MOD_LINEAR,
   [MODIFIER_PRIORITY_SPLIT_TILED] = DRM_FORMAT_MOD_VIVANTE_SPLIT_TILED,
   [MODIFIER_PRIORITY_SPLIT_SUPER_TILED] = DRM_FORMAT_MOD_VIVANTE_SPLIT_SUPER_TILED,
   [MODIFIER_PRIORITY_TILED] = DRM_FORMAT_MOD_VIVANTE_TILED,
   [MODIFIER_PRIORITY_SUPER_TILED] = DRM_FORMAT_MOD_VIVANTE_SUPER_TILED,
};

static uint64_t
select_best_modifier(const struct etna_screen * screen,
                     const uint64_t *modifiers, const unsigned count)
{
   enum modifier_priority prio = MODIFIER_PRIORITY_INVALID;

   for (int i = 0; i < count; i++) {
      switch (modifiers[i]) {
      case DRM_FORMAT_MOD_VIVANTE_SUPER_TILED:
         if ((screen->specs.pixel_pipes > 1 && !screen->specs.single_buffer) ||
             !screen->specs.can_supertile)
            break;
         prio = MAX2(prio, MODIFIER_PRIORITY_SUPER_TILED);
         break;
      case DRM_FORMAT_MOD_VIVANTE_TILED:
         if (screen->specs.pixel_pipes > 1 && !screen->specs.single_buffer)
            break;
         prio = MAX2(prio, MODIFIER_PRIORITY_TILED);
         break;
      case DRM_FORMAT_MOD_VIVANTE_SPLIT_SUPER_TILED:
         if ((screen->specs.pixel_pipes < 2) || !screen->specs.can_supertile)
            break;
         prio = MAX2(prio, MODIFIER_PRIORITY_SPLIT_SUPER_TILED);
         break;
      case DRM_FORMAT_MOD_VIVANTE_SPLIT_TILED:
         if (screen->specs.pixel_pipes < 2)
            break;
         prio = MAX2(prio, MODIFIER_PRIORITY_SPLIT_TILED);
         break;
      case DRM_FORMAT_MOD_LINEAR:
         prio = MAX2(prio, MODIFIER_PRIORITY_LINEAR);
         break;
      case DRM_FORMAT_MOD_INVALID:
      default:
         break;
      }
   }

   return priority_to_modifier[prio];
}

static struct pipe_resource *
etna_resource_create_modifiers(struct pipe_screen *pscreen,
                               const struct pipe_resource *templat,
                               const uint64_t *modifiers, int count)
{
   struct etna_screen *screen = etna_screen(pscreen);
   struct pipe_resource tmpl = *templat;
   uint64_t modifier = select_best_modifier(screen, modifiers, count);

   if (modifier == DRM_FORMAT_MOD_INVALID)
      return NULL;

   /*
    * We currently assume that all buffers allocated through this interface
    * should be scanout enabled.
    */
   tmpl.bind |= PIPE_BIND_SCANOUT;

   return etna_resource_alloc(pscreen, modifier_to_layout(modifier), modifier, &tmpl);
}

static void
etna_resource_changed(struct pipe_screen *pscreen, struct pipe_resource *prsc)
{
   etna_resource(prsc)->seqno++;
}

static void
etna_resource_destroy(struct pipe_screen *pscreen, struct pipe_resource *prsc)
{
   struct etna_resource *rsc = etna_resource(prsc);

   mtx_lock(&rsc->lock);
   assert(!_mesa_set_next_entry(rsc->pending_ctx, NULL));
   _mesa_set_destroy(rsc->pending_ctx, NULL);
   mtx_unlock(&rsc->lock);

   if (rsc->bo)
      etna_bo_del(rsc->bo);

   if (rsc->ts_bo)
      etna_bo_del(rsc->ts_bo);

   if (rsc->scanout)
      renderonly_scanout_destroy(rsc->scanout, etna_screen(pscreen)->ro);

   util_range_destroy(&rsc->valid_buffer_range);

   pipe_resource_reference(&rsc->texture, NULL);
   pipe_resource_reference(&rsc->render, NULL);

   for (unsigned i = 0; i < ETNA_NUM_LOD; i++)
      FREE(rsc->levels[i].patch_offsets);

   mtx_destroy(&rsc->lock);

   FREE(rsc);
}

static struct pipe_resource *
etna_resource_from_handle(struct pipe_screen *pscreen,
                          const struct pipe_resource *tmpl,
                          struct winsys_handle *handle, unsigned usage)
{
   struct etna_screen *screen = etna_screen(pscreen);
   struct etna_resource *rsc;
   struct etna_resource_level *level;
   struct pipe_resource *prsc;

   DBG("target=%d, format=%s, %ux%ux%u, array_size=%u, last_level=%u, "
       "nr_samples=%u, usage=%u, bind=%x, flags=%x",
       tmpl->target, util_format_name(tmpl->format), tmpl->width0,
       tmpl->height0, tmpl->depth0, tmpl->array_size, tmpl->last_level,
       tmpl->nr_samples, tmpl->usage, tmpl->bind, tmpl->flags);

   rsc = CALLOC_STRUCT(etna_resource);
   if (!rsc)
      return NULL;

   level = &rsc->levels[0];
   prsc = &rsc->base;

   *prsc = *tmpl;

   pipe_reference_init(&prsc->reference, 1);
   util_range_init(&rsc->valid_buffer_range);
   prsc->screen = pscreen;

   rsc->bo = etna_screen_bo_from_handle(pscreen, handle);
   if (!rsc->bo)
      goto fail;

   rsc->seqno = 1;
   rsc->layout = modifier_to_layout(handle->modifier);
   rsc->halign = TEXTURE_HALIGN_FOUR;

   level->width = tmpl->width0;
   level->height = tmpl->height0;
   level->depth = tmpl->depth0;
   level->stride = handle->stride;
   level->offset = handle->offset;

   /* Determine padding of the imported resource. */
   unsigned paddingX = 0, paddingY = 0;
   etna_layout_multiple(rsc->layout, screen->specs.pixel_pipes,
                        is_rs_align(screen, tmpl),
                        &paddingX, &paddingY, &rsc->halign);

   if (!screen->specs.use_blt && rsc->layout == ETNA_LAYOUT_LINEAR)
      paddingY = align(paddingY, ETNA_RS_HEIGHT_MASK + 1);
   level->padded_width = align(level->width, paddingX);
   level->padded_height = align(level->height, paddingY);

   level->layer_stride = level->stride * util_format_get_nblocksy(prsc->format,
                                                                  level->padded_height);
   level->size = level->layer_stride;

   /* The DDX must give us a BO which conforms to our padding size.
    * The stride of the BO must be greater or equal to our padded
    * stride. The size of the BO must accomodate the padded height. */
   if (level->stride < util_format_get_stride(tmpl->format, level->padded_width)) {
      BUG("BO stride %u is too small for RS engine width padding (%zu, format %s)",
          level->stride, util_format_get_stride(tmpl->format, level->padded_width),
          util_format_name(tmpl->format));
      goto fail;
   }
   if (etna_bo_size(rsc->bo) < level->stride * level->padded_height) {
      BUG("BO size %u is too small for RS engine height padding (%u, format %s)",
          etna_bo_size(rsc->bo), level->stride * level->padded_height,
          util_format_name(tmpl->format));
      goto fail;
   }

   mtx_init(&rsc->lock, mtx_recursive);
   rsc->pending_ctx = _mesa_set_create(NULL, _mesa_hash_pointer,
                                       _mesa_key_pointer_equal);
   if (!rsc->pending_ctx)
      goto fail;

   return prsc;

fail:
   etna_resource_destroy(pscreen, prsc);

   return NULL;
}

static bool
etna_resource_get_handle(struct pipe_screen *pscreen,
                         struct pipe_context *pctx,
                         struct pipe_resource *prsc,
                         struct winsys_handle *handle, unsigned usage)
{
   struct etna_resource *rsc = etna_resource(prsc);
   /* Scanout is always attached to the base resource */
   struct renderonly_scanout *scanout = rsc->scanout;

   handle->stride = rsc->levels[0].stride;
   handle->offset = rsc->levels[0].offset;
   handle->modifier = layout_to_modifier(rsc->layout);

   if (handle->type == WINSYS_HANDLE_TYPE_SHARED) {
      return etna_bo_get_name(rsc->bo, &handle->handle) == 0;
   } else if (handle->type == WINSYS_HANDLE_TYPE_KMS) {
      if (renderonly_get_handle(scanout, handle)) {
         return true;
      } else {
         handle->handle = etna_bo_handle(rsc->bo);
         return true;
      }
   } else if (handle->type == WINSYS_HANDLE_TYPE_FD) {
      handle->handle = etna_bo_dmabuf(rsc->bo);
      return true;
   } else {
      return false;
   }
}

void
etna_resource_used(struct etna_context *ctx, struct pipe_resource *prsc,
                   enum etna_resource_status status)
{
   struct pipe_resource *referenced = NULL;
   struct etna_resource *rsc;

   if (!prsc)
      return;

   mtx_lock(&ctx->lock);

   rsc = etna_resource(prsc);
again:
   mtx_lock(&rsc->lock);

   set_foreach(rsc->pending_ctx, entry) {
      struct etna_context *extctx = (struct etna_context *)entry->key;
      struct pipe_context *pctx = &extctx->base;
      bool need_flush = false;

      if (mtx_trylock(&extctx->lock) != thrd_success) {
         /*
	  * The other context could be locked in etna_flush() and
	  * stuck waiting for the resource lock, so release the
	  * resource lock here, let etna_flush() finish, and try
	  * again.
	  */
         mtx_unlock(&rsc->lock);
         thrd_yield();
         goto again;
      }

      set_foreach(extctx->used_resources_read, entry2) {
         struct etna_resource *rsc2 = (struct etna_resource *)entry2->key;
         if (ctx == extctx || rsc2 != rsc)
            continue;

         if (status & ETNA_PENDING_WRITE) {
            need_flush = true;
            break;
         }
      }

      if (need_flush) {
         pctx->flush(pctx, NULL, 0);
         mtx_unlock(&extctx->lock);
	 continue;
      }

      set_foreach(extctx->used_resources_write, entry2) {
         struct etna_resource *rsc2 = (struct etna_resource *)entry2->key;
         if (ctx == extctx || rsc2 != rsc)
            continue;

         need_flush = true;
         break;
      }

      if (need_flush)
         pctx->flush(pctx, NULL, 0);

      mtx_unlock(&extctx->lock);
   }

   rsc->status = status;

   if (!_mesa_set_search(rsc->pending_ctx, ctx)) {
      pipe_resource_reference(&referenced, prsc);
      _mesa_set_add((status & ETNA_PENDING_READ) ?
                    ctx->used_resources_read : ctx->used_resources_write, rsc);
      _mesa_set_add(rsc->pending_ctx, ctx);
   }

   mtx_unlock(&rsc->lock);
   mtx_unlock(&ctx->lock);
}

bool
etna_resource_has_valid_ts(struct etna_resource *rsc)
{
   if (!rsc->ts_bo)
      return false;

   for (int level = 0; level <= rsc->base.last_level; level++)
      if (rsc->levels[level].ts_valid)
         return true;

   return false;
}

void
etna_resource_screen_init(struct pipe_screen *pscreen)
{
   pscreen->can_create_resource = etna_screen_can_create_resource;
   pscreen->resource_create = etna_resource_create;
   pscreen->resource_create_with_modifiers = etna_resource_create_modifiers;
   pscreen->resource_from_handle = etna_resource_from_handle;
   pscreen->resource_get_handle = etna_resource_get_handle;
   pscreen->resource_changed = etna_resource_changed;
   pscreen->resource_destroy = etna_resource_destroy;
}
