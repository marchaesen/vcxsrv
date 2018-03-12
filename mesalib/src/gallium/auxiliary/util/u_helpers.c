/**************************************************************************
 *
 * Copyright 2012 Marek Olšák <maraeo@gmail.com>
 * All Rights Reserved.
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
 * IN NO EVENT SHALL THE AUTHORS AND/OR THEIR SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#include "util/u_helpers.h"
#include "util/u_inlines.h"
#include "util/u_upload_mgr.h"
#include <inttypes.h>

/**
 * This function is used to copy an array of pipe_vertex_buffer structures,
 * while properly referencing the pipe_vertex_buffer::buffer member.
 *
 * enabled_buffers is updated such that the bits corresponding to the indices
 * of disabled buffers are set to 0 and the enabled ones are set to 1.
 *
 * \sa util_copy_framebuffer_state
 */
void util_set_vertex_buffers_mask(struct pipe_vertex_buffer *dst,
                                  uint32_t *enabled_buffers,
                                  const struct pipe_vertex_buffer *src,
                                  unsigned start_slot, unsigned count)
{
   unsigned i;
   uint32_t bitmask = 0;

   dst += start_slot;

   if (src) {
      for (i = 0; i < count; i++) {
         if (src[i].buffer.resource)
            bitmask |= 1 << i;

         pipe_vertex_buffer_unreference(&dst[i]);

         if (!src[i].is_user_buffer)
            pipe_resource_reference(&dst[i].buffer.resource, src[i].buffer.resource);
      }

      /* Copy over the other members of pipe_vertex_buffer. */
      memcpy(dst, src, count * sizeof(struct pipe_vertex_buffer));

      *enabled_buffers &= ~(((1ull << count) - 1) << start_slot);
      *enabled_buffers |= bitmask << start_slot;
   }
   else {
      /* Unreference the buffers. */
      for (i = 0; i < count; i++)
         pipe_vertex_buffer_unreference(&dst[i]);

      *enabled_buffers &= ~(((1ull << count) - 1) << start_slot);
   }
}

/**
 * Same as util_set_vertex_buffers_mask, but it only returns the number
 * of bound buffers.
 */
void util_set_vertex_buffers_count(struct pipe_vertex_buffer *dst,
                                   unsigned *dst_count,
                                   const struct pipe_vertex_buffer *src,
                                   unsigned start_slot, unsigned count)
{
   unsigned i;
   uint32_t enabled_buffers = 0;

   for (i = 0; i < *dst_count; i++) {
      if (dst[i].buffer.resource)
         enabled_buffers |= (1ull << i);
   }

   util_set_vertex_buffers_mask(dst, &enabled_buffers, src, start_slot,
                                count);

   *dst_count = util_last_bit(enabled_buffers);
}

/**
 * Given a user index buffer, save the structure to "saved", and upload it.
 */
bool
util_upload_index_buffer(struct pipe_context *pipe,
                         const struct pipe_draw_info *info,
                         struct pipe_resource **out_buffer,
                         unsigned *out_offset)
{
   unsigned start_offset = info->start * info->index_size;

   u_upload_data(pipe->stream_uploader, start_offset,
                 info->count * info->index_size, 4,
                 (char*)info->index.user + start_offset,
                 out_offset, out_buffer);
   u_upload_unmap(pipe->stream_uploader);
   *out_offset -= start_offset;
   return *out_buffer != NULL;
}

/* This is a helper for hardware bring-up. Don't remove. */
struct pipe_query *
util_begin_pipestat_query(struct pipe_context *ctx)
{
   struct pipe_query *q =
      ctx->create_query(ctx, PIPE_QUERY_PIPELINE_STATISTICS, 0);
   if (!q)
      return NULL;

   ctx->begin_query(ctx, q);
   return q;
}

/* This is a helper for hardware bring-up. Don't remove. */
void
util_end_pipestat_query(struct pipe_context *ctx, struct pipe_query *q,
                        FILE *f)
{
   static unsigned counter;
   struct pipe_query_data_pipeline_statistics stats;

   ctx->end_query(ctx, q);
   ctx->get_query_result(ctx, q, true, (void*)&stats);
   ctx->destroy_query(ctx, q);

   fprintf(f,
           "Draw call %u:\n"
           "    ia_vertices    = %"PRIu64"\n"
           "    ia_primitives  = %"PRIu64"\n"
           "    vs_invocations = %"PRIu64"\n"
           "    gs_invocations = %"PRIu64"\n"
           "    gs_primitives  = %"PRIu64"\n"
           "    c_invocations  = %"PRIu64"\n"
           "    c_primitives   = %"PRIu64"\n"
           "    ps_invocations = %"PRIu64"\n"
           "    hs_invocations = %"PRIu64"\n"
           "    ds_invocations = %"PRIu64"\n"
           "    cs_invocations = %"PRIu64"\n",
           p_atomic_inc_return(&counter),
           stats.ia_vertices,
           stats.ia_primitives,
           stats.vs_invocations,
           stats.gs_invocations,
           stats.gs_primitives,
           stats.c_invocations,
           stats.c_primitives,
           stats.ps_invocations,
           stats.hs_invocations,
           stats.ds_invocations,
           stats.cs_invocations);
}

/* This is a helper for hardware bring-up. Don't remove. */
void
util_wait_for_idle(struct pipe_context *ctx)
{
   struct pipe_fence_handle *fence = NULL;

   ctx->flush(ctx, &fence, 0);
   ctx->screen->fence_finish(ctx->screen, NULL, fence, PIPE_TIMEOUT_INFINITE);
}
