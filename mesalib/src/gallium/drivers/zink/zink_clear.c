/*
 * Copyright 2018 Collabora Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "zink_context.h"
#include "zink_query.h"
#include "zink_resource.h"
#include "zink_screen.h"

#include "util/u_blitter.h"
#include "util/u_dynarray.h"
#include "util/format/u_format.h"
#include "util/format_srgb.h"
#include "util/u_framebuffer.h"
#include "util/u_inlines.h"
#include "util/u_rect.h"
#include "util/u_surface.h"
#include "util/u_helpers.h"

static inline bool
check_3d_layers(struct pipe_surface *psurf)
{
   if (psurf->texture->target != PIPE_TEXTURE_3D)
      return true;
   /* SPEC PROBLEM:
    * though the vk spec doesn't seem to explicitly address this, currently drivers
    * are claiming that all 3D images have a single "3D" layer regardless of layercount,
    * so we can never clear them if we aren't trying to clear only layer 0
    */
   if (psurf->u.tex.first_layer)
      return false;
      
   if (psurf->u.tex.last_layer - psurf->u.tex.first_layer > 0)
      return false;
   return true;
}

static inline bool
scissor_states_equal(const struct pipe_scissor_state *a, const struct pipe_scissor_state *b)
{
   return a->minx == b->minx && a->miny == b->miny && a->maxx == b->maxx && a->maxy == b->maxy;
}

static void
clear_in_rp(struct pipe_context *pctx,
           unsigned buffers,
           const struct pipe_scissor_state *scissor_state,
           const union pipe_color_union *pcolor,
           double depth, unsigned stencil)
{
   struct zink_context *ctx = zink_context(pctx);
   struct pipe_framebuffer_state *fb = &ctx->fb_state;

   VkClearAttachment attachments[1 + PIPE_MAX_COLOR_BUFS];
   int num_attachments = 0;

   if (buffers & PIPE_CLEAR_COLOR) {
      VkClearColorValue color;
      color.float32[0] = pcolor->f[0];
      color.float32[1] = pcolor->f[1];
      color.float32[2] = pcolor->f[2];
      color.float32[3] = pcolor->f[3];

      for (unsigned i = 0; i < fb->nr_cbufs; i++) {
         if (!(buffers & (PIPE_CLEAR_COLOR0 << i)) || !fb->cbufs[i])
            continue;

         attachments[num_attachments].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
         attachments[num_attachments].colorAttachment = i;
         attachments[num_attachments].clearValue.color = color;
         ++num_attachments;
      }
   }

   if (buffers & PIPE_CLEAR_DEPTHSTENCIL && fb->zsbuf) {
      VkImageAspectFlags aspect = 0;
      if (buffers & PIPE_CLEAR_DEPTH)
         aspect |= VK_IMAGE_ASPECT_DEPTH_BIT;
      if (buffers & PIPE_CLEAR_STENCIL)
         aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;

      attachments[num_attachments].aspectMask = aspect;
      attachments[num_attachments].clearValue.depthStencil.depth = depth;
      attachments[num_attachments].clearValue.depthStencil.stencil = stencil;
      ++num_attachments;
   }

   VkClearRect cr = {0};
   if (scissor_state) {
      cr.rect.offset.x = scissor_state->minx;
      cr.rect.offset.y = scissor_state->miny;
      cr.rect.extent.width = MIN2(fb->width, scissor_state->maxx - scissor_state->minx);
      cr.rect.extent.height = MIN2(fb->height, scissor_state->maxy - scissor_state->miny);
   } else {
      cr.rect.extent.width = fb->width;
      cr.rect.extent.height = fb->height;
   }
   cr.baseArrayLayer = 0;
   cr.layerCount = util_framebuffer_get_num_layers(fb);
   struct zink_batch *batch = &ctx->batch;
   zink_batch_rp(ctx);
   VKCTX(CmdClearAttachments)(batch->state->cmdbuf, num_attachments, attachments, 1, &cr);
}

static void
clear_color_no_rp(struct zink_context *ctx, struct zink_resource *res, const union pipe_color_union *pcolor, unsigned level, unsigned layer, unsigned layerCount)
{
   struct zink_batch *batch = &ctx->batch;
   zink_batch_no_rp(ctx);
   VkImageSubresourceRange range = {0};
   range.baseMipLevel = level;
   range.levelCount = 1;
   range.baseArrayLayer = layer;
   range.layerCount = layerCount;
   range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

   VkClearColorValue color;
   color.float32[0] = pcolor->f[0];
   color.float32[1] = pcolor->f[1];
   color.float32[2] = pcolor->f[2];
   color.float32[3] = pcolor->f[3];

   if (zink_resource_image_needs_barrier(res, VK_IMAGE_LAYOUT_GENERAL, 0, 0) &&
       zink_resource_image_needs_barrier(res, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, 0))
      zink_resource_image_barrier(ctx, res, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, 0);
   zink_batch_reference_resource_rw(batch, res, true);
   VKCTX(CmdClearColorImage)(batch->state->cmdbuf, res->obj->image, res->layout, &color, 1, &range);
}

static void
clear_zs_no_rp(struct zink_context *ctx, struct zink_resource *res, VkImageAspectFlags aspects, double depth, unsigned stencil, unsigned level, unsigned layer, unsigned layerCount)
{
   struct zink_batch *batch = &ctx->batch;
   zink_batch_no_rp(ctx);
   VkImageSubresourceRange range = {0};
   range.baseMipLevel = level;
   range.levelCount = 1;
   range.baseArrayLayer = layer;
   range.layerCount = layerCount;
   range.aspectMask = aspects;

   VkClearDepthStencilValue zs_value = {depth, stencil};

   if (zink_resource_image_needs_barrier(res, VK_IMAGE_LAYOUT_GENERAL, 0, 0) &&
       zink_resource_image_needs_barrier(res, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, 0))
      zink_resource_image_barrier(ctx, res, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, 0);
   zink_batch_reference_resource_rw(batch, res, true);
   VKCTX(CmdClearDepthStencilImage)(batch->state->cmdbuf, res->obj->image, res->layout, &zs_value, 1, &range);
}



static struct zink_framebuffer_clear_data *
get_clear_data(struct zink_context *ctx, struct zink_framebuffer_clear *fb_clear, const struct pipe_scissor_state *scissor_state)
{
   struct zink_framebuffer_clear_data *clear = NULL;
   unsigned num_clears = zink_fb_clear_count(fb_clear);
   if (num_clears) {
      struct zink_framebuffer_clear_data *last_clear = zink_fb_clear_element(fb_clear, num_clears - 1);
      /* if we're completely overwriting the previous clear, merge this into the previous clear */
      if (!scissor_state || (last_clear->has_scissor && scissor_states_equal(&last_clear->scissor, scissor_state)))
         clear = last_clear;
   }
   if (!clear) {
      struct zink_framebuffer_clear_data cd = {0};
      util_dynarray_append(&fb_clear->clears, struct zink_framebuffer_clear_data, cd);
      clear = zink_fb_clear_element(fb_clear, zink_fb_clear_count(fb_clear) - 1);
   }
   return clear;
}

void
zink_clear(struct pipe_context *pctx,
           unsigned buffers,
           const struct pipe_scissor_state *scissor_state,
           const union pipe_color_union *pcolor,
           double depth, unsigned stencil)
{
   struct zink_context *ctx = zink_context(pctx);
   struct pipe_framebuffer_state *fb = &ctx->fb_state;
   struct zink_batch *batch = &ctx->batch;
   bool needs_rp = false;

   if (unlikely(!zink_screen(pctx->screen)->info.have_EXT_conditional_rendering && !zink_check_conditional_render(ctx)))
      return;

   if (scissor_state) {
      struct u_rect scissor = {scissor_state->minx, scissor_state->maxx, scissor_state->miny, scissor_state->maxy};
      needs_rp = !zink_blit_region_fills(scissor, fb->width, fb->height);
   }


   if (batch->in_rp) {
      clear_in_rp(pctx, buffers, scissor_state, pcolor, depth, stencil);
      return;
   }

   if (buffers & PIPE_CLEAR_COLOR) {
      for (unsigned i = 0; i < fb->nr_cbufs; i++) {
         if ((buffers & (PIPE_CLEAR_COLOR0 << i)) && fb->cbufs[i]) {
            struct pipe_surface *psurf = fb->cbufs[i];
            struct zink_framebuffer_clear *fb_clear = &ctx->fb_clears[i];
            struct zink_framebuffer_clear_data *clear = get_clear_data(ctx, fb_clear, needs_rp ? scissor_state : NULL);

            ctx->clears_enabled |= PIPE_CLEAR_COLOR0 << i;
            clear->conditional = ctx->render_condition_active;
            clear->has_scissor = needs_rp;
            if (scissor_state && needs_rp)
               clear->scissor = *scissor_state;
            clear->color.color = *pcolor;
            clear->color.srgb = psurf->format != psurf->texture->format &&
                                !util_format_is_srgb(psurf->format) && util_format_is_srgb(psurf->texture->format);
            if (zink_fb_clear_first_needs_explicit(fb_clear))
               ctx->rp_clears_enabled &= ~(PIPE_CLEAR_COLOR0 << i);
            else
               ctx->rp_clears_enabled |= PIPE_CLEAR_COLOR0 << i;
         }
      }
   }

   if (buffers & PIPE_CLEAR_DEPTHSTENCIL && fb->zsbuf) {
      struct zink_framebuffer_clear *fb_clear = &ctx->fb_clears[PIPE_MAX_COLOR_BUFS];
      struct zink_framebuffer_clear_data *clear = get_clear_data(ctx, fb_clear, needs_rp ? scissor_state : NULL);
      ctx->clears_enabled |= PIPE_CLEAR_DEPTHSTENCIL;
      clear->conditional = ctx->render_condition_active;
      clear->has_scissor = needs_rp;
      if (scissor_state && needs_rp)
         clear->scissor = *scissor_state;
      if (buffers & PIPE_CLEAR_DEPTH)
         clear->zs.depth = depth;
      if (buffers & PIPE_CLEAR_STENCIL)
         clear->zs.stencil = stencil;
      clear->zs.bits |= (buffers & PIPE_CLEAR_DEPTHSTENCIL);
      if (zink_fb_clear_first_needs_explicit(fb_clear))
         ctx->rp_clears_enabled &= ~PIPE_CLEAR_DEPTHSTENCIL;
      else
         ctx->rp_clears_enabled |= (buffers & PIPE_CLEAR_DEPTHSTENCIL);
   }
}

static inline bool
colors_equal(union pipe_color_union *a, union pipe_color_union *b)
{
   return a->ui[0] == b->ui[0] && a->ui[1] == b->ui[1] && a->ui[2] == b->ui[2] && a->ui[3] == b->ui[3];
}

void
zink_clear_framebuffer(struct zink_context *ctx, unsigned clear_buffers)
{
   unsigned to_clear = 0;
   struct pipe_framebuffer_state *fb_state = &ctx->fb_state;
#ifndef NDEBUG
   assert(!(clear_buffers & PIPE_CLEAR_DEPTHSTENCIL) || zink_fb_clear_enabled(ctx, PIPE_MAX_COLOR_BUFS));
   for (int i = 0; i < fb_state->nr_cbufs && clear_buffers >= PIPE_CLEAR_COLOR0; i++) {
      assert(!(clear_buffers & (PIPE_CLEAR_COLOR0 << i)) || zink_fb_clear_enabled(ctx, i));
   }
#endif
   while (clear_buffers) {
      struct zink_framebuffer_clear *color_clear = NULL;
      struct zink_framebuffer_clear *zs_clear = NULL;
      unsigned num_clears = 0;
      for (int i = 0; i < fb_state->nr_cbufs && clear_buffers >= PIPE_CLEAR_COLOR0; i++) {
         struct zink_framebuffer_clear *fb_clear = &ctx->fb_clears[i];
         /* these need actual clear calls inside the rp */
         if (!(clear_buffers & (PIPE_CLEAR_COLOR0 << i)))
            continue;
         if (color_clear) {
            /* different number of clears -> do another clear */
            //XXX: could potentially merge "some" of the clears into this one for a very, very small optimization
            if (num_clears != zink_fb_clear_count(fb_clear))
               goto out;
            /* compare all the clears to determine if we can batch these buffers together */
            for (int j = !zink_fb_clear_first_needs_explicit(fb_clear); j < num_clears; j++) {
               struct zink_framebuffer_clear_data *a = zink_fb_clear_element(color_clear, j);
               struct zink_framebuffer_clear_data *b = zink_fb_clear_element(fb_clear, j);
               /* scissors don't match, fire this one off */
               if (a->has_scissor != b->has_scissor || (a->has_scissor && !scissor_states_equal(&a->scissor, &b->scissor)))
                  goto out;

               /* colors don't match, fire this one off */
               if (!colors_equal(&a->color.color, &b->color.color))
                  goto out;
            }
         } else {
            color_clear = fb_clear;
            num_clears = zink_fb_clear_count(fb_clear);
         }

         clear_buffers &= ~(PIPE_CLEAR_COLOR0 << i);
         to_clear |= (PIPE_CLEAR_COLOR0 << i);
      }
      clear_buffers &= ~PIPE_CLEAR_COLOR;
      if (clear_buffers & PIPE_CLEAR_DEPTHSTENCIL) {
         struct zink_framebuffer_clear *fb_clear = &ctx->fb_clears[PIPE_MAX_COLOR_BUFS];
         if (color_clear) {
            if (num_clears != zink_fb_clear_count(fb_clear))
               goto out;
            /* compare all the clears to determine if we can batch these buffers together */
            for (int j = !zink_fb_clear_first_needs_explicit(fb_clear); j < zink_fb_clear_count(color_clear); j++) {
               struct zink_framebuffer_clear_data *a = zink_fb_clear_element(color_clear, j);
               struct zink_framebuffer_clear_data *b = zink_fb_clear_element(fb_clear, j);
               /* scissors don't match, fire this one off */
               if (a->has_scissor != b->has_scissor || (a->has_scissor && !scissor_states_equal(&a->scissor, &b->scissor)))
                  goto out;
            }
         }
         zs_clear = fb_clear;
         to_clear |= (clear_buffers & PIPE_CLEAR_DEPTHSTENCIL);
         clear_buffers &= ~PIPE_CLEAR_DEPTHSTENCIL;
      }
out:
      if (to_clear) {
         if (num_clears) {
            for (int j = !zink_fb_clear_first_needs_explicit(color_clear); j < num_clears; j++) {
               struct zink_framebuffer_clear_data *clear = zink_fb_clear_element(color_clear, j);
               struct zink_framebuffer_clear_data *zsclear = NULL;
               /* zs bits are both set here if those aspects should be cleared at some point */
               unsigned clear_bits = to_clear & ~PIPE_CLEAR_DEPTHSTENCIL;
               if (zs_clear) {
                  zsclear = zink_fb_clear_element(zs_clear, j);
                  clear_bits |= zsclear->zs.bits;
               }
               zink_clear(&ctx->base, clear_bits,
                          clear->has_scissor ? &clear->scissor : NULL,
                          &clear->color.color,
                          zsclear ? zsclear->zs.depth : 0,
                          zsclear ? zsclear->zs.stencil : 0);
            }
         } else {
            for (int j = !zink_fb_clear_first_needs_explicit(zs_clear); j < zink_fb_clear_count(zs_clear); j++) {
               struct zink_framebuffer_clear_data *clear = zink_fb_clear_element(zs_clear, j);
               zink_clear(&ctx->base, clear->zs.bits,
                          clear->has_scissor ? &clear->scissor : NULL,
                          NULL,
                          clear->zs.depth,
                          clear->zs.stencil);
            }
         }
      }
      to_clear = 0;
   }
   for (int i = 0; i < ARRAY_SIZE(ctx->fb_clears); i++)
       zink_fb_clear_reset(ctx, i);
}

static struct pipe_surface *
create_clear_surface(struct pipe_context *pctx, struct pipe_resource *pres, unsigned level, const struct pipe_box *box)
{
   struct pipe_surface tmpl = {{0}};

   tmpl.format = pres->format;
   tmpl.u.tex.first_layer = box->z;
   tmpl.u.tex.last_layer = box->z + box->depth - 1;
   tmpl.u.tex.level = level;
   return pctx->create_surface(pctx, pres, &tmpl);
}

void
zink_clear_texture(struct pipe_context *pctx,
                   struct pipe_resource *pres,
                   unsigned level,
                   const struct pipe_box *box,
                   const void *data)
{
   struct zink_context *ctx = zink_context(pctx);
   struct zink_resource *res = zink_resource(pres);
   struct pipe_screen *pscreen = pctx->screen;
   struct u_rect region = zink_rect_from_box(box);
   bool needs_rp = !zink_blit_region_fills(region, pres->width0, pres->height0) || ctx->render_condition_active;
   struct pipe_surface *surf = NULL;

   if (res->aspect & VK_IMAGE_ASPECT_COLOR_BIT) {
      union pipe_color_union color;

      util_format_unpack_rgba(pres->format, color.ui, data, 1);

      if (pscreen->is_format_supported(pscreen, pres->format, pres->target, 0, 0,
                                      PIPE_BIND_RENDER_TARGET) && !needs_rp) {
         zink_batch_no_rp(ctx);
         clear_color_no_rp(ctx, res, &color, level, box->z, box->depth);
      } else {
         surf = create_clear_surface(pctx, pres, level, box);
         zink_blit_begin(ctx, ZINK_BLIT_SAVE_FB | ZINK_BLIT_SAVE_FS);
         util_blitter_clear_render_target(ctx->blitter, surf, &color, box->x, box->y, box->width, box->height);
      }
      if (res->base.b.target == PIPE_BUFFER)
         util_range_add(&res->base.b, &res->valid_buffer_range, box->x, box->x + box->width);
   } else {
      float depth = 0.0;
      uint8_t stencil = 0;

      if (res->aspect & VK_IMAGE_ASPECT_DEPTH_BIT)
         util_format_unpack_z_float(pres->format, &depth, data, 1);

      if (res->aspect & VK_IMAGE_ASPECT_STENCIL_BIT)
         util_format_unpack_s_8uint(pres->format, &stencil, data, 1);

      if (!needs_rp) {
         zink_batch_no_rp(ctx);
         clear_zs_no_rp(ctx, res, res->aspect, depth, stencil, level, box->z, box->depth);
      } else {
         unsigned flags = 0;
         if (res->aspect & VK_IMAGE_ASPECT_DEPTH_BIT)
            flags |= PIPE_CLEAR_DEPTH;
         if (res->aspect & VK_IMAGE_ASPECT_STENCIL_BIT)
            flags |= PIPE_CLEAR_STENCIL;
         surf = create_clear_surface(pctx, pres, level, box);
         zink_blit_begin(ctx, ZINK_BLIT_SAVE_FB | ZINK_BLIT_SAVE_FS);
         util_blitter_clear_depth_stencil(ctx->blitter, surf, flags, depth, stencil, box->x, box->y, box->width, box->height);
      }
   }
   /* this will never destroy the surface */
   pipe_surface_reference(&surf, NULL);
}

void
zink_clear_buffer(struct pipe_context *pctx,
                  struct pipe_resource *pres,
                  unsigned offset,
                  unsigned size,
                  const void *clear_value,
                  int clear_value_size)
{
   struct zink_context *ctx = zink_context(pctx);
   struct zink_resource *res = zink_resource(pres);

   uint32_t clamped;
   if (util_lower_clearsize_to_dword(clear_value, &clear_value_size, &clamped))
      clear_value = &clamped;
   if (offset % 4 == 0 && size % 4 == 0 && clear_value_size == sizeof(uint32_t)) {
      /*
         - dstOffset is the byte offset into the buffer at which to start filling,
           and must be a multiple of 4.

         - size is the number of bytes to fill, and must be either a multiple of 4,
           or VK_WHOLE_SIZE to fill the range from offset to the end of the buffer
       */
      struct zink_batch *batch = &ctx->batch;
      zink_batch_no_rp(ctx);
      zink_batch_reference_resource_rw(batch, res, true);
      util_range_add(&res->base.b, &res->valid_buffer_range, offset, offset + size);
      VKCTX(CmdFillBuffer)(batch->state->cmdbuf, res->obj->buffer, offset, size, *(uint32_t*)clear_value);
      return;
   }
   struct pipe_transfer *xfer;
   uint8_t *map = pipe_buffer_map_range(pctx, pres, offset, size,
                                        PIPE_MAP_WRITE | PIPE_MAP_ONCE | PIPE_MAP_DISCARD_RANGE, &xfer);
   if (!map)
      return;
   unsigned rem = size % clear_value_size;
   uint8_t *ptr = map;
   for (unsigned i = 0; i < (size - rem) / clear_value_size; i++) {
      memcpy(ptr, clear_value, clear_value_size);
      ptr += clear_value_size;
   }
   if (rem)
      memcpy(map + size - rem, clear_value, rem);
   pipe_buffer_unmap(pctx, xfer);
}

void
zink_clear_render_target(struct pipe_context *pctx, struct pipe_surface *dst,
                         const union pipe_color_union *color, unsigned dstx,
                         unsigned dsty, unsigned width, unsigned height,
                         bool render_condition_enabled)
{
   struct zink_context *ctx = zink_context(pctx);
   zink_blit_begin(ctx, ZINK_BLIT_SAVE_FB | ZINK_BLIT_SAVE_FS | (render_condition_enabled ? 0 : ZINK_BLIT_NO_COND_RENDER));
   util_blitter_clear_render_target(ctx->blitter, dst, color, dstx, dsty, width, height);
   if (!render_condition_enabled && ctx->render_condition_active)
      zink_start_conditional_render(ctx);
}

void
zink_clear_depth_stencil(struct pipe_context *pctx, struct pipe_surface *dst,
                         unsigned clear_flags, double depth, unsigned stencil,
                         unsigned dstx, unsigned dsty, unsigned width, unsigned height,
                         bool render_condition_enabled)
{
   struct zink_context *ctx = zink_context(pctx);
   zink_blit_begin(ctx, ZINK_BLIT_SAVE_FB | ZINK_BLIT_SAVE_FS | (render_condition_enabled ? 0 : ZINK_BLIT_NO_COND_RENDER));
   util_blitter_clear_depth_stencil(ctx->blitter, dst, clear_flags, depth, stencil, dstx, dsty, width, height);
   if (!render_condition_enabled && ctx->render_condition_active)
      zink_start_conditional_render(ctx);
}

bool
zink_fb_clear_needs_explicit(struct zink_framebuffer_clear *fb_clear)
{
   if (zink_fb_clear_count(fb_clear) != 1)
      return true;
   return zink_fb_clear_element_needs_explicit(zink_fb_clear_element(fb_clear, 0));
}

bool
zink_fb_clear_first_needs_explicit(struct zink_framebuffer_clear *fb_clear)
{
   if (!zink_fb_clear_count(fb_clear))
      return false;
   return zink_fb_clear_element_needs_explicit(zink_fb_clear_element(fb_clear, 0));
}

void
zink_fb_clear_util_unpack_clear_color(struct zink_framebuffer_clear_data *clear, enum pipe_format format, union pipe_color_union *color)
{
   const struct util_format_description *desc = util_format_description(format);
   if (clear->color.srgb) {
      /* if SRGB mode is disabled for the fb with a backing srgb image then we have to
       * convert this to srgb color
       */
      for (unsigned j = 0; j < MIN2(3, desc->nr_channels); j++) {
         assert(desc->channel[j].normalized);
         color->f[j] = util_format_srgb_to_linear_float(clear->color.color.f[j]);
      }
      color->f[3] = clear->color.color.f[3];
   } else {
      for (unsigned i = 0; i < 4; i++)
         color->f[i] = clear->color.color.f[i];
   }
}

static void
fb_clears_apply_internal(struct zink_context *ctx, struct pipe_resource *pres, int i)
{
   struct zink_framebuffer_clear *fb_clear = &ctx->fb_clears[i];
   struct zink_resource *res = zink_resource(pres);

   if (!zink_fb_clear_enabled(ctx, i))
      return;
   if (ctx->batch.in_rp)
      zink_clear_framebuffer(ctx, BITFIELD_BIT(i));
   else if (res->aspect == VK_IMAGE_ASPECT_COLOR_BIT) {
      if (zink_fb_clear_needs_explicit(fb_clear) || !check_3d_layers(ctx->fb_state.cbufs[i]))
         /* this will automatically trigger all the clears */
         zink_batch_rp(ctx);
      else {
         struct pipe_surface *psurf = ctx->fb_state.cbufs[i];
         struct zink_framebuffer_clear_data *clear = zink_fb_clear_element(fb_clear, 0);
         union pipe_color_union color;
         zink_fb_clear_util_unpack_clear_color(clear, psurf->format, &color);

         clear_color_no_rp(ctx, res, &color,
                                psurf->u.tex.level, psurf->u.tex.first_layer,
                                psurf->u.tex.last_layer - psurf->u.tex.first_layer + 1);
      }
      zink_fb_clear_reset(ctx, i);
      return;
   } else {
      if (zink_fb_clear_needs_explicit(fb_clear) || !check_3d_layers(ctx->fb_state.zsbuf))
         /* this will automatically trigger all the clears */
         zink_batch_rp(ctx);
      else {
         struct pipe_surface *psurf = ctx->fb_state.zsbuf;
         struct zink_framebuffer_clear_data *clear = zink_fb_clear_element(fb_clear, 0);
         VkImageAspectFlags aspects = 0;
         if (clear->zs.bits & PIPE_CLEAR_DEPTH)
            aspects |= VK_IMAGE_ASPECT_DEPTH_BIT;
         if (clear->zs.bits & PIPE_CLEAR_STENCIL)
            aspects |= VK_IMAGE_ASPECT_STENCIL_BIT;
         clear_zs_no_rp(ctx, res, aspects, clear->zs.depth, clear->zs.stencil,
                             psurf->u.tex.level, psurf->u.tex.first_layer,
                             psurf->u.tex.last_layer - psurf->u.tex.first_layer + 1);
      }
   }
   zink_fb_clear_reset(ctx, i);
}

void
zink_fb_clear_reset(struct zink_context *ctx, unsigned i)
{
   util_dynarray_clear(&ctx->fb_clears[i].clears);
   if (i == PIPE_MAX_COLOR_BUFS) {
      ctx->clears_enabled &= ~PIPE_CLEAR_DEPTHSTENCIL;
      ctx->rp_clears_enabled &= ~PIPE_CLEAR_DEPTHSTENCIL;
   } else {
      ctx->clears_enabled &= ~(PIPE_CLEAR_COLOR0 << i);
      ctx->rp_clears_enabled &= ~(PIPE_CLEAR_COLOR0 << i);
   }
}

void
zink_fb_clears_apply(struct zink_context *ctx, struct pipe_resource *pres)
{
   if (zink_resource(pres)->aspect == VK_IMAGE_ASPECT_COLOR_BIT) {
      for (int i = 0; i < ctx->fb_state.nr_cbufs; i++) {
         if (ctx->fb_state.cbufs[i] && ctx->fb_state.cbufs[i]->texture == pres) {
            fb_clears_apply_internal(ctx, pres, i);
         }
      }
   } else {
      if (ctx->fb_state.zsbuf && ctx->fb_state.zsbuf->texture == pres) {
         fb_clears_apply_internal(ctx, pres, PIPE_MAX_COLOR_BUFS);
      }
   }
}

void
zink_fb_clears_discard(struct zink_context *ctx, struct pipe_resource *pres)
{
   if (zink_resource(pres)->aspect == VK_IMAGE_ASPECT_COLOR_BIT) {
      for (int i = 0; i < ctx->fb_state.nr_cbufs; i++) {
         if (ctx->fb_state.cbufs[i] && ctx->fb_state.cbufs[i]->texture == pres) {
            if (zink_fb_clear_enabled(ctx, i)) {
               zink_fb_clear_reset(ctx, i);
            }
         }
      }
   } else {
      if (zink_fb_clear_enabled(ctx, PIPE_MAX_COLOR_BUFS) && ctx->fb_state.zsbuf && ctx->fb_state.zsbuf->texture == pres) {
         int i = PIPE_MAX_COLOR_BUFS;
         zink_fb_clear_reset(ctx, i);
      }
   }
}

void
zink_clear_apply_conditionals(struct zink_context *ctx)
{
   for (int i = 0; i < ARRAY_SIZE(ctx->fb_clears); i++) {
      struct zink_framebuffer_clear *fb_clear = &ctx->fb_clears[i];
      if (!zink_fb_clear_enabled(ctx, i))
         continue;
      for (int j = 0; j < zink_fb_clear_count(fb_clear); j++) {
         struct zink_framebuffer_clear_data *clear = zink_fb_clear_element(fb_clear, j);
         if (clear->conditional) {
            struct pipe_surface *surf;
            if (i < PIPE_MAX_COLOR_BUFS)
               surf = ctx->fb_state.cbufs[i];
            else
               surf = ctx->fb_state.zsbuf;
            if (surf)
               fb_clears_apply_internal(ctx, surf->texture, i);
            else
               zink_fb_clear_reset(ctx, i);
            break;
         }
      }
   }
}

static void
fb_clears_apply_or_discard_internal(struct zink_context *ctx, struct pipe_resource *pres, struct u_rect region, bool discard_only, bool invert, int i)
{
   struct zink_framebuffer_clear *fb_clear = &ctx->fb_clears[i];
   if (zink_fb_clear_enabled(ctx, i)) {
      if (zink_blit_region_fills(region, pres->width0, pres->height0)) {
         if (invert)
            fb_clears_apply_internal(ctx, pres, i);
         else
            /* we know we can skip these */
            zink_fb_clears_discard(ctx, pres);
         return;
      }
      for (int j = 0; j < zink_fb_clear_count(fb_clear); j++) {
         struct zink_framebuffer_clear_data *clear = zink_fb_clear_element(fb_clear, j);
         struct u_rect scissor = {clear->scissor.minx, clear->scissor.maxx,
                                  clear->scissor.miny, clear->scissor.maxy};
         if (!clear->has_scissor || zink_blit_region_covers(region, scissor)) {
            /* this is a clear that isn't fully covered by our pending write */
            if (!discard_only)
               fb_clears_apply_internal(ctx, pres, i);
            return;
         }
      }
      if (!invert)
         /* if we haven't already returned, then we know we can discard */
         zink_fb_clears_discard(ctx, pres);
   }
}

void
zink_fb_clears_apply_or_discard(struct zink_context *ctx, struct pipe_resource *pres, struct u_rect region, bool discard_only)
{
   if (zink_resource(pres)->aspect == VK_IMAGE_ASPECT_COLOR_BIT) {
      for (int i = 0; i < ctx->fb_state.nr_cbufs; i++) {
         if (ctx->fb_state.cbufs[i] && ctx->fb_state.cbufs[i]->texture == pres) {
            fb_clears_apply_or_discard_internal(ctx, pres, region, discard_only, false, i);
         }
      }
   }  else {
      if (zink_fb_clear_enabled(ctx, PIPE_MAX_COLOR_BUFS) && ctx->fb_state.zsbuf && ctx->fb_state.zsbuf->texture == pres) {
         fb_clears_apply_or_discard_internal(ctx, pres, region, discard_only, false, PIPE_MAX_COLOR_BUFS);
      }
   }
}

void
zink_fb_clears_apply_region(struct zink_context *ctx, struct pipe_resource *pres, struct u_rect region)
{
   if (zink_resource(pres)->aspect == VK_IMAGE_ASPECT_COLOR_BIT) {
      for (int i = 0; i < ctx->fb_state.nr_cbufs; i++) {
         if (ctx->fb_state.cbufs[i] && ctx->fb_state.cbufs[i]->texture == pres) {
            fb_clears_apply_or_discard_internal(ctx, pres, region, false, true, i);
         }
      }
   }  else {
      if (ctx->fb_state.zsbuf && ctx->fb_state.zsbuf->texture == pres) {
         fb_clears_apply_or_discard_internal(ctx, pres, region, false, true, PIPE_MAX_COLOR_BUFS);
      }
   }
}
