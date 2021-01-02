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

#include "pipe/p_screen.h"
#include "util/u_memory.h"
#include "util/os_time.h"

#include "swr_context.h"
#include "swr_screen.h"
#include "swr_fence.h"

#ifdef __APPLE__
#include <sched.h>
#endif

#if defined(PIPE_CC_MSVC) // portable thread yield
   #define sched_yield SwitchToThread
#endif

/*
 * Fence callback, called by back-end thread on completion of all rendering up
 * to SwrSync call.
 */
static void
swr_fence_cb(uint64_t userData, uint64_t userData2, uint64_t userData3)
{
   struct swr_fence *fence = (struct swr_fence *)userData;

   /* Complete all work attached to the fence */
   swr_fence_do_work(fence);

   /* Correct value is in SwrSync data, and not the fence write field. */
   /* Contexts may not finish in order, but fence value always increases */
   if (fence->read < userData2)
      fence->read = userData2;
}

/*
 * Submit an existing fence.
 */
void
swr_fence_submit(struct swr_context *ctx, struct pipe_fence_handle *fh)
{
   struct swr_fence *fence = swr_fence(fh);

   fence->write++;
   fence->pending = TRUE;
   ctx->api.pfnSwrSync(ctx->swrContext, swr_fence_cb, (uint64_t)fence, fence->write, 0);
}

/*
 * Create a new fence object.
 */
struct pipe_fence_handle *
swr_fence_create()
{
   static int fence_id = 0;
   struct swr_fence *fence = CALLOC_STRUCT(swr_fence);
   if (!fence)
      return NULL;

   pipe_reference_init(&fence->reference, 1);
   fence->id = fence_id++;
   fence->work.tail = &fence->work.head;

   return (struct pipe_fence_handle *)fence;
}

/** Destroy a fence.  Called when refcount hits zero. */
static void
swr_fence_destroy(struct swr_fence *fence)
{
   /* Complete any work left if fence was not submitted */
   swr_fence_do_work(fence);
   FREE(fence);
}

/**
 * Set ptr = fence, with reference counting
 */
void
swr_fence_reference(struct pipe_screen *screen,
                    struct pipe_fence_handle **ptr,
                    struct pipe_fence_handle *f)
{
   struct swr_fence *fence = swr_fence(f);
   struct swr_fence *old;

   if (likely(ptr)) {
      old = swr_fence(*ptr);
      *ptr = f;
   } else {
      old = NULL;
   }

   if (pipe_reference(&old->reference, &fence->reference)) {
      swr_fence_finish(screen, NULL, (struct pipe_fence_handle *) old, 0);
      swr_fence_destroy(old);
   }
}


/*
 * Wait for the fence to finish.
 */
bool
swr_fence_finish(struct pipe_screen *screen,
                 struct pipe_context *ctx,
                 struct pipe_fence_handle *fence_handle,
                 uint64_t timeout)
{
   while (!swr_is_fence_done(fence_handle))
      sched_yield();

   swr_fence(fence_handle)->pending = FALSE;

   return TRUE;
}


uint64_t
swr_get_timestamp(struct pipe_screen *screen)
{
   return os_time_get_nano();
}


void
swr_fence_init(struct pipe_screen *p_screen)
{
   p_screen->fence_reference = swr_fence_reference;
   p_screen->fence_finish = swr_fence_finish;
   p_screen->get_timestamp = swr_get_timestamp;

   /* Create persistant StoreTiles "flush" fence, used to signal completion
    * of flushing tile state back to resource texture, via StoreTiles. */
   struct swr_screen *screen = swr_screen(p_screen);
   screen->flush_fence = swr_fence_create();
}
