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

#include "etnaviv_clear_blit.h"

#include "hw/common.xml.h"

#include "etnaviv_blt.h"
#include "etnaviv_context.h"
#include "etnaviv_emit.h"
#include "etnaviv_format.h"
#include "etnaviv_resource.h"
#include "etnaviv_rs.h"
#include "etnaviv_surface.h"
#include "etnaviv_translate.h"

#include "pipe/p_defines.h"
#include "pipe/p_state.h"
#include "util/compiler.h"
#include "util/u_blitter.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"
#include "util/u_surface.h"

/* Save current state for blitter operation */
void
etna_blit_save_state(struct etna_context *ctx)
{
   util_blitter_save_fragment_constant_buffer_slot(ctx->blitter,
                                                   ctx->constant_buffer[PIPE_SHADER_FRAGMENT].cb);
   util_blitter_save_vertex_buffer_slot(ctx->blitter, ctx->vertex_buffer.vb);
   util_blitter_save_vertex_elements(ctx->blitter, ctx->vertex_elements);
   util_blitter_save_vertex_shader(ctx->blitter, ctx->shader.bind_vs);
   util_blitter_save_rasterizer(ctx->blitter, ctx->rasterizer);
   util_blitter_save_viewport(ctx->blitter, &ctx->viewport_s);
   util_blitter_save_scissor(ctx->blitter, &ctx->scissor);
   util_blitter_save_fragment_shader(ctx->blitter, ctx->shader.bind_fs);
   util_blitter_save_blend(ctx->blitter, ctx->blend);
   util_blitter_save_depth_stencil_alpha(ctx->blitter, ctx->zsa);
   util_blitter_save_stencil_ref(ctx->blitter, &ctx->stencil_ref_s);
   util_blitter_save_sample_mask(ctx->blitter, ctx->sample_mask, 0);
   util_blitter_save_framebuffer(ctx->blitter, &ctx->framebuffer_s);
   util_blitter_save_fragment_sampler_states(ctx->blitter,
         ctx->num_fragment_samplers, (void **)ctx->sampler);
   util_blitter_save_fragment_sampler_views(ctx->blitter,
         ctx->num_fragment_sampler_views, ctx->sampler_view);
}

uint64_t
etna_clear_blit_pack_rgba(enum pipe_format format, const union pipe_color_union *color)
{
   union util_color uc;

   util_pack_color_union(format, &uc, color);

   switch (util_format_get_blocksize(format)) {
   case 1:
      uc.ui[0] = uc.ui[0] << 8 | (uc.ui[0] & 0xff);
      FALLTHROUGH;
   case 2:
      uc.ui[0] =  uc.ui[0] << 16 | (uc.ui[0] & 0xffff);
      FALLTHROUGH;
   case 4:
      uc.ui[1] = uc.ui[0];
      FALLTHROUGH;
   default:
      return (uint64_t) uc.ui[1] << 32 | uc.ui[0];
   }
}

static void
etna_blit(struct pipe_context *pctx, const struct pipe_blit_info *blit_info)
{
   struct etna_context *ctx = etna_context(pctx);
   struct pipe_blit_info info = *blit_info;

   if (ctx->blit(pctx, &info))
      return;

   if (util_try_blit_via_copy_region(pctx, &info, false))
      return;

   if (info.mask & PIPE_MASK_S) {
      DBG("cannot blit stencil, skipping");
      info.mask &= ~PIPE_MASK_S;
   }

   if (!util_blitter_is_blit_supported(ctx->blitter, &info)) {
      DBG("blit unsupported %s -> %s",
          util_format_short_name(info.src.resource->format),
          util_format_short_name(info.dst.resource->format));
      return;
   }

   etna_blit_save_state(ctx);
   util_blitter_blit(ctx->blitter, &info);
}

static void
etna_clear_render_target(struct pipe_context *pctx, struct pipe_surface *dst,
                         const union pipe_color_union *color, unsigned dstx,
                         unsigned dsty, unsigned width, unsigned height,
                         bool render_condition_enabled)
{
   struct etna_context *ctx = etna_context(pctx);

   /* XXX could fall back to RS when target area is full screen / resolveable
    * and no TS. */
   etna_blit_save_state(ctx);
   util_blitter_clear_render_target(ctx->blitter, dst, color, dstx, dsty, width, height);
}

static void
etna_clear_depth_stencil(struct pipe_context *pctx, struct pipe_surface *dst,
                         unsigned clear_flags, double depth, unsigned stencil,
                         unsigned dstx, unsigned dsty, unsigned width,
                         unsigned height, bool render_condition_enabled)
{
   struct etna_context *ctx = etna_context(pctx);

   /* XXX could fall back to RS when target area is full screen / resolveable
    * and no TS. */
   etna_blit_save_state(ctx);
   util_blitter_clear_depth_stencil(ctx->blitter, dst, clear_flags, depth,
                                    stencil, dstx, dsty, width, height);
}

static void
etna_resource_copy_region(struct pipe_context *pctx, struct pipe_resource *dst,
                          unsigned dst_level, unsigned dstx, unsigned dsty,
                          unsigned dstz, struct pipe_resource *src,
                          unsigned src_level, const struct pipe_box *src_box)
{
   struct etna_context *ctx = etna_context(pctx);

   if (src->target != PIPE_BUFFER && dst->target != PIPE_BUFFER &&
       util_blitter_is_copy_supported(ctx->blitter, dst, src)) {
      etna_blit_save_state(ctx);
      util_blitter_copy_texture(ctx->blitter, dst, dst_level, dstx, dsty, dstz,
                                src, src_level, src_box);
   } else {
      util_resource_copy_region(pctx, dst, dst_level, dstx, dsty, dstz, src,
                                src_level, src_box);
   }
}

static void
etna_flush_resource(struct pipe_context *pctx, struct pipe_resource *prsc)
{
   struct etna_resource *rsc = etna_resource(prsc);

   if (rsc->render) {
      if (etna_resource_older(rsc, etna_resource(rsc->render))) {
         etna_copy_resource(pctx, prsc, rsc->render, 0, 0);
         rsc->seqno = etna_resource(rsc->render)->seqno;
      }
   } else if (etna_resource_needs_flush(rsc)) {
      etna_copy_resource(pctx, prsc, prsc, 0, 0);
      rsc->flush_seqno = rsc->seqno;
   }
}

void
etna_copy_resource(struct pipe_context *pctx, struct pipe_resource *dst,
                   struct pipe_resource *src, int first_level, int last_level)
{
   struct etna_resource *src_priv = etna_resource(src);
   struct etna_resource *dst_priv = etna_resource(dst);

   assert(src->format == dst->format);
   assert(src->array_size == dst->array_size);
   assert(last_level <= dst->last_level && last_level <= src->last_level);

   struct pipe_blit_info blit = {};
   blit.mask = util_format_get_mask(dst->format);
   blit.filter = PIPE_TEX_FILTER_NEAREST;
   blit.src.resource = src;
   blit.src.format = src->format;
   blit.dst.resource = dst;
   blit.dst.format = dst->format;
   blit.dst.box.depth = blit.src.box.depth = 1;

   /* Copy each level and each layer */
   for (int level = first_level; level <= last_level; level++) {
      blit.src.level = blit.dst.level = level;
      blit.src.box.width = blit.dst.box.width =
         MIN2(src_priv->levels[level].padded_width, dst_priv->levels[level].padded_width);
      blit.src.box.height = blit.dst.box.height =
         MIN2(src_priv->levels[level].padded_height, dst_priv->levels[level].padded_height);
      unsigned depth = MIN2(src_priv->levels[level].depth, dst_priv->levels[level].depth);
      if (dst->array_size > 1) {
         assert(depth == 1); /* no array of 3d texture */
         depth = dst->array_size;
      }

      for (int z = 0; z < depth; z++) {
         blit.src.box.z = blit.dst.box.z = z;
         pctx->blit(pctx, &blit);
      }
   }
}

void
etna_copy_resource_box(struct pipe_context *pctx, struct pipe_resource *dst,
                       struct pipe_resource *src, int level,
                       struct pipe_box *box)
{
   assert(src->format == dst->format);
   assert(src->array_size == dst->array_size);

   struct pipe_blit_info blit = {};
   blit.mask = util_format_get_mask(dst->format);
   blit.filter = PIPE_TEX_FILTER_NEAREST;
   blit.src.resource = src;
   blit.src.format = src->format;
   blit.src.box = *box;
   blit.dst.resource = dst;
   blit.dst.format = dst->format;
   blit.dst.box = *box;

   blit.dst.box.depth = blit.src.box.depth = 1;
   blit.src.level = blit.dst.level = level;

   for (int z = 0; z < box->depth; z++) {
      blit.src.box.z = blit.dst.box.z = box->z + z;
      pctx->blit(pctx, &blit);
   }
}

void
etna_clear_blit_init(struct pipe_context *pctx)
{
   struct etna_context *ctx = etna_context(pctx);
   struct etna_screen *screen = ctx->screen;

   pctx->blit = etna_blit;
   pctx->clear_render_target = etna_clear_render_target;
   pctx->clear_depth_stencil = etna_clear_depth_stencil;
   pctx->resource_copy_region = etna_resource_copy_region;
   pctx->flush_resource = etna_flush_resource;

   if (screen->specs.use_blt)
      etna_clear_blit_blt_init(pctx);
   else
      etna_clear_blit_rs_init(pctx);
}
