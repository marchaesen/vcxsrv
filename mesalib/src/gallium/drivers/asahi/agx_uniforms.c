/*
 * Copyright 2021 Alyssa Rosenzweig
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
#include <stdio.h>
#include "agx_state.h"
#include "asahi/lib/agx_pack.h"

/* Computes the address for a push uniform, adding referenced BOs to the
 * current batch as necessary. Note anything uploaded via the batch's pool does
 * not require an update to the BO list, since the entire pool will be added
 * once at submit time. */

static uint64_t
agx_const_buffer_ptr(struct agx_batch *batch,
                     struct pipe_constant_buffer *cb)
{
   if (cb->buffer) {
      struct agx_bo *bo = agx_resource(cb->buffer)->bo;
      agx_batch_add_bo(batch, bo);

      return bo->ptr.gpu + cb->buffer_offset;
   } else {
      return agx_pool_upload_aligned(&batch->pool,
                                     ((uint8_t *) cb->user_buffer) + cb->buffer_offset,
                                     cb->buffer_size - cb->buffer_offset, 64);
   }
}

static uint64_t
agx_push_location_direct(struct agx_context *ctx, struct agx_push push,
                         enum pipe_shader_type stage)
{
   struct agx_batch *batch = ctx->batch;
   struct agx_stage *st = &ctx->stage[stage];

   switch (push.type) {
   case AGX_PUSH_UBO_BASES: {
      unsigned count = util_last_bit(st->cb_mask);
      struct agx_ptr ptr = agx_pool_alloc_aligned(&batch->pool, count * sizeof(uint64_t), 8);
      uint64_t *addresses = ptr.cpu;

      for (unsigned i = 0; i < count; ++i) {
         struct pipe_constant_buffer *cb = &st->cb[i];
         addresses[i] = agx_const_buffer_ptr(batch, cb);
      }

      return ptr.gpu;
   }

   case AGX_PUSH_VBO_BASES: {
      unsigned count = util_last_bit(ctx->vb_mask);
      struct agx_ptr ptr = agx_pool_alloc_aligned(&batch->pool, count * sizeof(uint64_t), 8);
      uint64_t *addresses = ptr.cpu;

      u_foreach_bit(i, ctx->vb_mask) {
         struct pipe_vertex_buffer vb = ctx->vertex_buffers[i];
         assert(!vb.is_user_buffer);

         struct agx_bo *bo = agx_resource(vb.buffer.resource)->bo;
         agx_batch_add_bo(batch, bo);

         addresses[i] = bo->ptr.gpu + vb.buffer_offset;
      }

      return ptr.gpu;
   }

   case AGX_PUSH_BLEND_CONST:
   {
      return agx_pool_upload_aligned(&batch->pool, &ctx->blend_color,
            sizeof(ctx->blend_color), 8);
   }

   default:
      unreachable("todo: push more");
   }
}

uint64_t
agx_push_location(struct agx_context *ctx, struct agx_push push,
                  enum pipe_shader_type stage)
{
   uint64_t direct = agx_push_location_direct(ctx, push, stage);
   struct agx_pool *pool = &ctx->batch->pool;

   if (push.indirect)
      return agx_pool_upload(pool, &direct, sizeof(direct));
   else
      return direct;
}
