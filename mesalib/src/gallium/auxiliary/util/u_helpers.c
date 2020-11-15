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

#include "util/u_cpu_detect.h"
#include "util/u_helpers.h"
#include "util/u_inlines.h"
#include "util/u_upload_mgr.h"
#include "util/u_thread.h"
#include "util/os_time.h"
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

   *enabled_buffers &= ~u_bit_consecutive(start_slot, count);

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

      *enabled_buffers |= bitmask << start_slot;
   }
   else {
      /* Unreference the buffers. */
      for (i = 0; i < count; i++)
         pipe_vertex_buffer_unreference(&dst[i]);
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
 * This function is used to copy an array of pipe_shader_buffer structures,
 * while properly referencing the pipe_shader_buffer::buffer member.
 *
 * \sa util_set_vertex_buffer_mask
 */
void util_set_shader_buffers_mask(struct pipe_shader_buffer *dst,
                                  uint32_t *enabled_buffers,
                                  const struct pipe_shader_buffer *src,
                                  unsigned start_slot, unsigned count)
{
   unsigned i;

   dst += start_slot;

   if (src) {
      for (i = 0; i < count; i++) {
         pipe_resource_reference(&dst[i].buffer, src[i].buffer);

         if (src[i].buffer)
            *enabled_buffers |= (1ull << (start_slot + i));
         else
            *enabled_buffers &= ~(1ull << (start_slot + i));
      }

      /* Copy over the other members of pipe_shader_buffer. */
      memcpy(dst, src, count * sizeof(struct pipe_shader_buffer));
   }
   else {
      /* Unreference the buffers. */
      for (i = 0; i < count; i++)
         pipe_resource_reference(&dst[i].buffer, NULL);

      *enabled_buffers &= ~(((1ull << count) - 1) << start_slot);
   }
}

/**
 * Given a user index buffer, save the structure to "saved", and upload it.
 */
bool
util_upload_index_buffer(struct pipe_context *pipe,
                         const struct pipe_draw_info *info,
                         struct pipe_resource **out_buffer,
                         unsigned *out_offset, unsigned alignment)
{
   unsigned start_offset = info->start * info->index_size;

   u_upload_data(pipe->stream_uploader, start_offset,
                 info->count * info->index_size, alignment,
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
           (unsigned)p_atomic_inc_return(&counter),
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

void
util_throttle_init(struct util_throttle *t, uint64_t max_mem_usage)
{
   t->max_mem_usage = max_mem_usage;
}

void
util_throttle_deinit(struct pipe_screen *screen, struct util_throttle *t)
{
   for (unsigned i = 0; i < ARRAY_SIZE(t->ring); i++)
      screen->fence_reference(screen, &t->ring[i].fence, NULL);
}

static uint64_t
util_get_throttle_total_memory_usage(struct util_throttle *t)
{
   uint64_t total_usage = 0;

   for (unsigned i = 0; i < ARRAY_SIZE(t->ring); i++)
      total_usage += t->ring[i].mem_usage;
   return total_usage;
}

static void util_dump_throttle_ring(struct util_throttle *t)
{
   printf("Throttle:\n");
   for (unsigned i = 0; i < ARRAY_SIZE(t->ring); i++) {
      printf("  ring[%u]: fence = %s, mem_usage = %"PRIu64"%s%s\n",
             i, t->ring[i].fence ? "yes" : " no",
             t->ring[i].mem_usage,
             t->flush_index == i ? " [flush]" : "",
             t->wait_index == i ? " [wait]" : "");
   }
}

/**
 * Notify util_throttle that the next operation allocates memory.
 * util_throttle tracks memory usage and waits for fences until its tracked
 * memory usage decreases.
 *
 * Example:
 *   util_throttle_memory_usage(..., w*h*d*Bpp);
 *   TexSubImage(..., w, h, d, ...);
 *
 * This means that TexSubImage can't allocate more memory its maximum limit
 * set during initialization.
 */
void
util_throttle_memory_usage(struct pipe_context *pipe,
                           struct util_throttle *t, uint64_t memory_size)
{
   (void)util_dump_throttle_ring; /* silence warning */

   if (!t->max_mem_usage)
      return;

   struct pipe_screen *screen = pipe->screen;
   struct pipe_fence_handle **fence = NULL;
   unsigned ring_size = ARRAY_SIZE(t->ring);
   uint64_t total = util_get_throttle_total_memory_usage(t);

   /* If there is not enough memory, walk the list of fences and find
    * the latest one that we need to wait for.
    */
   while (t->wait_index != t->flush_index &&
          total && total + memory_size > t->max_mem_usage) {
      assert(t->ring[t->wait_index].fence);

      /* Release an older fence if we need to wait for a newer one. */
      if (fence)
         screen->fence_reference(screen, fence, NULL);

      fence = &t->ring[t->wait_index].fence;
      t->ring[t->wait_index].mem_usage = 0;
      t->wait_index = (t->wait_index + 1) % ring_size;

      total = util_get_throttle_total_memory_usage(t);
   }

   /* Wait for the fence to decrease memory usage. */
   if (fence) {
      screen->fence_finish(screen, pipe, *fence, PIPE_TIMEOUT_INFINITE);
      screen->fence_reference(screen, fence, NULL);
   }

   /* Flush and get a fence if we've exhausted memory usage for the current
    * slot.
    */
   if (t->ring[t->flush_index].mem_usage &&
       t->ring[t->flush_index].mem_usage + memory_size >
       t->max_mem_usage / (ring_size / 2)) {
      struct pipe_fence_handle **fence =
         &t->ring[t->flush_index].fence;

      /* Expect that the current flush slot doesn't have a fence yet. */
      assert(!*fence);

      pipe->flush(pipe, fence, PIPE_FLUSH_ASYNC);
      t->flush_index = (t->flush_index + 1) % ring_size;

      /* Vacate the next slot if it's occupied. This should be rare. */
      if (t->flush_index == t->wait_index) {
         struct pipe_fence_handle **fence =
            &t->ring[t->wait_index].fence;

         t->ring[t->wait_index].mem_usage = 0;
         t->wait_index = (t->wait_index + 1) % ring_size;

         assert(*fence);
         screen->fence_finish(screen, pipe, *fence, PIPE_TIMEOUT_INFINITE);
         screen->fence_reference(screen, fence, NULL);
      }

      assert(!t->ring[t->flush_index].mem_usage);
      assert(!t->ring[t->flush_index].fence);
   }

   t->ring[t->flush_index].mem_usage += memory_size;
}
