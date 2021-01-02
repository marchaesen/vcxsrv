/****************************************************************************
 * Copyright (C) 2015 Intel Corporation.   All Rights Reserved.
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 ***************************************************************************/

#include "util/u_memory.h"
#include "swr_context.h"
#include "swr_screen.h"
#include "swr_scratch.h"
#include "swr_fence.h"
#include "swr_fence_work.h"
#include "api.h"

void *
swr_copy_to_scratch_space(struct swr_context *ctx,
                          struct swr_scratch_space *space,
                          const void *user_buffer,
                          unsigned int size)
{
   void *ptr;
   assert(space);
   assert(size);

   /* Allocate enough so that MAX_DRAWS_IN_FLIGHT sets fit. */
   uint32_t max_size_in_flight = size * ctx->max_draws_in_flight;

   /* Need to grow space */
   if (max_size_in_flight > space->current_size) {
      space->current_size = max_size_in_flight;

      if (space->base) {
         /* defer delete, use aligned-free, fence finish enforces the defer
          * delete will be on the *next* fence */
         struct swr_screen *screen = swr_screen(ctx->pipe.screen);
         swr_fence_finish(ctx->pipe.screen, NULL, screen->flush_fence, 0);
         swr_fence_work_free(screen->flush_fence, space->base, true);
         space->base = NULL;
      }

      if (!space->base) {
         space->base = (uint8_t *)AlignedMalloc(space->current_size,
                                                sizeof(void *));
         space->head = (void *)space->base;
      }
   }

   /* Wrap */
   if (((uint8_t *)space->head + size)
       >= ((uint8_t *)space->base + space->current_size)) {
      space->head = space->base;
   }

   ptr = space->head;
   space->head = (uint8_t *)space->head + size;

   /* Copy user_buffer to scratch */
   if (user_buffer)
      memcpy(ptr, user_buffer, size);

   return ptr;
}


void
swr_init_scratch_buffers(struct swr_context *ctx)
{
   struct swr_scratch_buffers *scratch;

   scratch = CALLOC_STRUCT(swr_scratch_buffers);
   ctx->scratch = scratch;
}

void
swr_destroy_scratch_buffers(struct swr_context *ctx)
{
   struct swr_scratch_buffers *scratch = ctx->scratch;

   if (scratch) {
      AlignedFree(scratch->vs_constants.base);
      AlignedFree(scratch->fs_constants.base);
      AlignedFree(scratch->gs_constants.base);
      AlignedFree(scratch->tcs_constants.base);
      AlignedFree(scratch->tes_constants.base);
      AlignedFree(scratch->vertex_buffer.base);
      AlignedFree(scratch->index_buffer.base);
      FREE(scratch);
   }
}
