/*
 * Copyright © 2012 Intel Corporation
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
 */

/** @file glthread.c
 *
 * Support functions for the glthread feature of Mesa.
 *
 * In multicore systems, many applications end up CPU-bound with about half
 * their time spent inside their rendering thread and half inside Mesa.  To
 * alleviate this, we put a shim layer in Mesa at the GL dispatch level that
 * quickly logs the GL commands to a buffer to be processed by a worker
 * thread.
 */

#include "main/mtypes.h"
#include "main/glthread.h"
#include "main/marshal.h"
#include "main/marshal_generated.h"
#include "util/u_atomic.h"
#include "util/u_thread.h"


static void
glthread_unmarshal_batch(void *job, int thread_index)
{
   struct glthread_batch *batch = (struct glthread_batch*)job;
   struct gl_context *ctx = batch->ctx;
   size_t pos = 0;

   _glapi_set_dispatch(ctx->CurrentServerDispatch);

   while (pos < batch->used)
      pos += _mesa_unmarshal_dispatch_cmd(ctx, &batch->buffer[pos]);

   assert(pos == batch->used);
   batch->used = 0;
}

static void
glthread_thread_initialization(void *job, int thread_index)
{
   struct gl_context *ctx = (struct gl_context*)job;

   ctx->Driver.SetBackgroundContext(ctx, &ctx->GLThread->stats);
   _glapi_set_context(ctx);
}

void
_mesa_glthread_init(struct gl_context *ctx)
{
   struct glthread_state *glthread = calloc(1, sizeof(*glthread));

   if (!glthread)
      return;

   if (!util_queue_init(&glthread->queue, "gl", MARSHAL_MAX_BATCHES - 2,
                        1, 0)) {
      free(glthread);
      return;
   }

   ctx->MarshalExec = _mesa_create_marshal_table(ctx);
   if (!ctx->MarshalExec) {
      util_queue_destroy(&glthread->queue);
      free(glthread);
      return;
   }

   for (unsigned i = 0; i < MARSHAL_MAX_BATCHES; i++) {
      glthread->batches[i].ctx = ctx;
      util_queue_fence_init(&glthread->batches[i].fence);
   }

   glthread->stats.queue = &glthread->queue;
   ctx->CurrentClientDispatch = ctx->MarshalExec;
   ctx->GLThread = glthread;

   /* Execute the thread initialization function in the thread. */
   struct util_queue_fence fence;
   util_queue_fence_init(&fence);
   util_queue_add_job(&glthread->queue, ctx, &fence,
                      glthread_thread_initialization, NULL);
   util_queue_fence_wait(&fence);
   util_queue_fence_destroy(&fence);
}

void
_mesa_glthread_destroy(struct gl_context *ctx)
{
   struct glthread_state *glthread = ctx->GLThread;

   if (!glthread)
      return;

   _mesa_glthread_finish(ctx);
   util_queue_destroy(&glthread->queue);

   for (unsigned i = 0; i < MARSHAL_MAX_BATCHES; i++)
      util_queue_fence_destroy(&glthread->batches[i].fence);

   free(glthread);
   ctx->GLThread = NULL;

   _mesa_glthread_restore_dispatch(ctx, "destroy");
}

void
_mesa_glthread_restore_dispatch(struct gl_context *ctx, const char *func)
{
   /* Remove ourselves from the dispatch table except if another ctx/thread
    * already installed a new dispatch table.
    *
    * Typically glxMakeCurrent will bind a new context (install new table) then
    * old context might be deleted.
    */
   if (_glapi_get_dispatch() == ctx->MarshalExec) {
       ctx->CurrentClientDispatch = ctx->CurrentServerDispatch;
       _glapi_set_dispatch(ctx->CurrentClientDispatch);
#if 0
       printf("glthread disabled: %s\n", func);
#endif
   }
}

void
_mesa_glthread_flush_batch(struct gl_context *ctx)
{
   struct glthread_state *glthread = ctx->GLThread;
   if (!glthread)
      return;

   struct glthread_batch *next = &glthread->batches[glthread->next];
   if (!next->used)
      return;

   /* Debug: execute the batch immediately from this thread.
    *
    * Note that glthread_unmarshal_batch() changes the dispatch table so we'll
    * need to restore it when it returns.
    */
   if (false) {
      glthread_unmarshal_batch(next, 0);
      _glapi_set_dispatch(ctx->CurrentClientDispatch);
      return;
   }

   p_atomic_add(&glthread->stats.num_offloaded_items, next->used);

   util_queue_add_job(&glthread->queue, next, &next->fence,
                      glthread_unmarshal_batch, NULL);
   glthread->last = glthread->next;
   glthread->next = (glthread->next + 1) % MARSHAL_MAX_BATCHES;
}

/**
 * Waits for all pending batches have been unmarshaled.
 *
 * This can be used by the main thread to synchronize access to the context,
 * since the worker thread will be idle after this.
 */
void
_mesa_glthread_finish(struct gl_context *ctx)
{
   struct glthread_state *glthread = ctx->GLThread;
   if (!glthread)
      return;

   /* If this is called from the worker thread, then we've hit a path that
    * might be called from either the main thread or the worker (such as some
    * dri interface entrypoints), in which case we don't need to actually
    * synchronize against ourself.
    */
   if (u_thread_is_self(glthread->queue.threads[0]))
      return;

   struct glthread_batch *last = &glthread->batches[glthread->last];
   struct glthread_batch *next = &glthread->batches[glthread->next];
   bool synced = false;

   if (!util_queue_fence_is_signalled(&last->fence)) {
      util_queue_fence_wait(&last->fence);
      synced = true;
   }

   if (next->used) {
      p_atomic_add(&glthread->stats.num_direct_items, next->used);

      /* Since glthread_unmarshal_batch changes the dispatch to direct,
       * restore it after it's done.
       */
      struct _glapi_table *dispatch = _glapi_get_dispatch();
      glthread_unmarshal_batch(next, 0);
      _glapi_set_dispatch(dispatch);

      /* It's not a sync because we don't enqueue partial batches, but
       * it would be a sync if we did. So count it anyway.
       */
      synced = true;
   }

   if (synced)
      p_atomic_inc(&glthread->stats.num_syncs);
}
