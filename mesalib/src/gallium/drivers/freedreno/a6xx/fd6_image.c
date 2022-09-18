/*
 * Copyright (C) 2017 Rob Clark <robclark@freedesktop.org>
 * Copyright © 2018 Google, Inc.
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#include "pipe/p_state.h"

#include "freedreno_resource.h"
#include "freedreno_state.h"

#include "fd6_image.h"
#include "fd6_resource.h"
#include "fd6_texture.h"

static const uint8_t swiz_identity[4] = {PIPE_SWIZZLE_X, PIPE_SWIZZLE_Y,
                                         PIPE_SWIZZLE_Z, PIPE_SWIZZLE_W};

static void
fd6_emit_single_plane_descriptor(struct fd_ringbuffer *ring,
                                 struct pipe_resource *prsc,
                                 uint32_t *descriptor)
{
   /* If the resource isn't present (holes are allowed), zero-fill the slot. */
   if (!prsc) {
      for (int i = 0; i < 16; i++)
         OUT_RING(ring, 0);
      return;
   }

   struct fd_resource *rsc = fd_resource(prsc);
   for (int i = 0; i < 4; i++)
      OUT_RING(ring, descriptor[i]);

   OUT_RELOC(ring, rsc->bo, descriptor[4], (uint64_t)descriptor[5] << 32, 0);

   OUT_RING(ring, descriptor[6]);

   OUT_RELOC(ring, rsc->bo, descriptor[7], (uint64_t)descriptor[8] << 32, 0);

   for (int i = 9; i < FDL6_TEX_CONST_DWORDS; i++)
      OUT_RING(ring, descriptor[i]);
}

static void
fd6_ssbo_descriptor(struct fd_context *ctx,
                    const struct pipe_shader_buffer *buf, uint32_t *descriptor)
{
   fdl6_buffer_view_init(
      descriptor,
      ctx->screen->info->a6xx.storage_16bit ? PIPE_FORMAT_R16_UINT
                                            : PIPE_FORMAT_R32_UINT,
      swiz_identity, buf->buffer_offset, /* Using relocs for addresses */
      buf->buffer_size);
}

static void
fd6_emit_image_descriptor(struct fd_context *ctx, struct fd_ringbuffer *ring, const struct pipe_image_view *buf, bool ibo)
{
   struct fd_resource *rsc = fd_resource(buf->resource);
   if (!rsc) {
      for (int i = 0; i < FDL6_TEX_CONST_DWORDS; i++)
         OUT_RING(ring, 0);
      return;
   }

   if (buf->resource->target == PIPE_BUFFER) {
   uint32_t descriptor[FDL6_TEX_CONST_DWORDS];
      fdl6_buffer_view_init(descriptor, buf->format, swiz_identity,
                           buf->u.buf.offset, /* Using relocs for addresses */
                           buf->u.buf.size);
   fd6_emit_single_plane_descriptor(ring, buf->resource, descriptor);
   } else {
      struct fdl_view_args args = {
         /* Using relocs for addresses */
         .iova = 0,

         .base_miplevel = buf->u.tex.level,
         .level_count = 1,

         .base_array_layer = buf->u.tex.first_layer,
         .layer_count = buf->u.tex.last_layer - buf->u.tex.first_layer + 1,

         .format = buf->format,
         .swiz = {PIPE_SWIZZLE_X, PIPE_SWIZZLE_Y, PIPE_SWIZZLE_Z,
                  PIPE_SWIZZLE_W},

         .type = fdl_type_from_pipe_target(buf->resource->target),
         .chroma_offsets = {FDL_CHROMA_LOCATION_COSITED_EVEN,
                            FDL_CHROMA_LOCATION_COSITED_EVEN},
      };

      /* fdl6_view makes the storage descriptor treat cubes like a 2D array (so
       * you can reference a specific layer), but we need to do that for the
       * texture descriptor as well to get our layer.
       */
      if (args.type == FDL_VIEW_TYPE_CUBE)
         args.type = FDL_VIEW_TYPE_2D;

      struct fdl6_view view;
      const struct fdl_layout *layouts[3] = {&rsc->layout, NULL, NULL};
      fdl6_view_init(&view, layouts, &args,
                     ctx->screen->info->a6xx.has_z24uint_s8uint);
      if (ibo)
         fd6_emit_single_plane_descriptor(ring, buf->resource, view.storage_descriptor);
      else
         fd6_emit_single_plane_descriptor(ring, buf->resource, view.descriptor);
   }
}

void
fd6_emit_image_tex(struct fd_context *ctx, struct fd_ringbuffer *ring,
                   const struct pipe_image_view *pimg)
{
   fd6_emit_image_descriptor(ctx, ring, pimg, false);
}

void
fd6_emit_ssbo_tex(struct fd_context *ctx, struct fd_ringbuffer *ring,
                  const struct pipe_shader_buffer *pbuf)
{
   uint32_t descriptor[FDL6_TEX_CONST_DWORDS];
   fd6_ssbo_descriptor(ctx, pbuf, descriptor);
   fd6_emit_single_plane_descriptor(ring, pbuf->buffer, descriptor);
}

/* Build combined image/SSBO "IBO" state, returns ownership of state reference */
struct fd_ringbuffer *
fd6_build_ibo_state(struct fd_context *ctx, const struct ir3_shader_variant *v,
                    enum pipe_shader_type shader)
{
   struct fd_shaderbuf_stateobj *bufso = &ctx->shaderbuf[shader];
   struct fd_shaderimg_stateobj *imgso = &ctx->shaderimg[shader];

   struct fd_ringbuffer *state = fd_submit_new_ringbuffer(
      ctx->batch->submit,
      ir3_shader_nibo(v) * 16 * 4,
      FD_RINGBUFFER_STREAMING);

   assert(shader == PIPE_SHADER_COMPUTE || shader == PIPE_SHADER_FRAGMENT);

   uint32_t descriptor[FDL6_TEX_CONST_DWORDS];
   for (unsigned i = 0; i < v->num_ssbos; i++) {
      fd6_ssbo_descriptor(ctx, &bufso->sb[i], descriptor);
      fd6_emit_single_plane_descriptor(state, bufso->sb[i].buffer, descriptor);
   }

   for (unsigned i = v->num_ssbos; i < v->num_ibos; i++) {
      fd6_emit_image_descriptor(ctx, state, &imgso->si[i - v->num_ssbos], true);
   }

   return state;
}

static void
fd6_set_shader_images(struct pipe_context *pctx, enum pipe_shader_type shader,
                      unsigned start, unsigned count,
                      unsigned unbind_num_trailing_slots,
                      const struct pipe_image_view *images) in_dt
{
   struct fd_context *ctx = fd_context(pctx);
   struct fd_shaderimg_stateobj *so = &ctx->shaderimg[shader];

   fd_set_shader_images(pctx, shader, start, count, unbind_num_trailing_slots,
                        images);

   if (!images)
      return;

   for (unsigned i = 0; i < count; i++) {
      unsigned n = i + start;
      struct pipe_image_view *buf = &so->si[n];

      if (!buf->resource)
         continue;

      fd6_validate_format(ctx, fd_resource(buf->resource), buf->format);
   }
}

void
fd6_image_init(struct pipe_context *pctx)
{
   pctx->set_shader_images = fd6_set_shader_images;
}
