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
#include "util/u_thread.h"

#ifdef HAVE_PTHREAD

static void
glthread_allocate_batch(struct gl_context *ctx)
{
   struct glthread_state *glthread = ctx->GLThread;

   /* TODO: handle memory allocation failure. */
   glthread->batch = malloc(sizeof(*glthread->batch));
   if (!glthread->batch)
      return;
   memset(glthread->batch, 0, offsetof(struct glthread_batch, buffer));
}

static void
glthread_unmarshal_batch(struct gl_context *ctx, struct glthread_batch *batch,
                         const bool release_batch)
{
   size_t pos = 0;

   _glapi_set_dispatch(ctx->CurrentServerDispatch);

   while (pos < batch->used)
      pos += _mesa_unmarshal_dispatch_cmd(ctx, &batch->buffer[pos]);

   assert(pos == batch->used);

   if (release_batch)
      free(batch);
   else
      batch->used = 0;
}

static void *
glthread_worker(void *data)
{
   struct gl_context *ctx = data;
   struct glthread_state *glthread = ctx->GLThread;

   ctx->Driver.SetBackgroundContext(ctx);
   _glapi_set_context(ctx);

   u_thread_setname("mesa_glthread");

   pthread_mutex_lock(&glthread->mutex);

   while (true) {
      struct glthread_batch *batch;

      /* Block (dropping the lock) until new work arrives for us. */
      while (!glthread->batch_queue && !glthread->shutdown) {
         pthread_cond_broadcast(&glthread->work_done);
         pthread_cond_wait(&glthread->new_work, &glthread->mutex);
      }

      batch = glthread->batch_queue;

      if (glthread->shutdown && !batch) {
         pthread_cond_broadcast(&glthread->work_done);
         pthread_mutex_unlock(&glthread->mutex);
         return NULL;
      }
      glthread->batch_queue = batch->next;
      if (glthread->batch_queue_tail == &batch->next)
         glthread->batch_queue_tail = &glthread->batch_queue;

      glthread->busy = true;
      pthread_mutex_unlock(&glthread->mutex);

      glthread_unmarshal_batch(ctx, batch, true);

      pthread_mutex_lock(&glthread->mutex);
      glthread->busy = false;
   }

   /* UNREACHED */
   return NULL;
}

void
_mesa_glthread_init(struct gl_context *ctx)
{
   struct glthread_state *glthread = calloc(1, sizeof(*glthread));

   if (!glthread)
      return;

   ctx->MarshalExec = _mesa_create_marshal_table(ctx);
   if (!ctx->MarshalExec) {
      free(glthread);
      return;
   }

   ctx->CurrentClientDispatch = ctx->MarshalExec;

   pthread_mutex_init(&glthread->mutex, NULL);
   pthread_cond_init(&glthread->new_work, NULL);
   pthread_cond_init(&glthread->work_done, NULL);

   glthread->batch_queue_tail = &glthread->batch_queue;
   ctx->GLThread = glthread;

   glthread_allocate_batch(ctx);

   pthread_create(&glthread->thread, NULL, glthread_worker, ctx);
}

void
_mesa_glthread_destroy(struct gl_context *ctx)
{
   struct glthread_state *glthread = ctx->GLThread;

   if (!glthread)
      return;

   _mesa_glthread_flush_batch(ctx);

   pthread_mutex_lock(&glthread->mutex);
   glthread->shutdown = true;
   pthread_cond_broadcast(&glthread->new_work);
   pthread_mutex_unlock(&glthread->mutex);

   /* Since this waits for the thread to exit, it means that all queued work
    * will have been completed.
    */
   pthread_join(glthread->thread, NULL);

   pthread_cond_destroy(&glthread->new_work);
   pthread_cond_destroy(&glthread->work_done);
   pthread_mutex_destroy(&glthread->mutex);

   /* Due to the join above, there should be one empty batch allocated at this
    * point, and no batches queued.
    */
   assert(!glthread->batch->used);
   assert(!glthread->batch->next);
   free(glthread->batch);
   assert(!glthread->batch_queue);

   free(glthread);
   ctx->GLThread = NULL;

   _mesa_glthread_restore_dispatch(ctx);
}

void
_mesa_glthread_restore_dispatch(struct gl_context *ctx)
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
   }
}

static void
_mesa_glthread_flush_batch_locked(struct gl_context *ctx)
{
   struct glthread_state *glthread = ctx->GLThread;
   struct glthread_batch *batch = glthread->batch;

   if (!batch->used)
      return;

   /* Immediately reallocate a new batch, since the next marshalled call would
    * just do it.
    */
   glthread_allocate_batch(ctx);

   /* Debug: execute the batch immediately from this thread.
    *
    * Note that glthread_unmarshal_batch() changes the dispatch table so we'll
    * need to restore it when it returns.
    */
   if (false) {
      glthread_unmarshal_batch(ctx, batch, true);
      _glapi_set_dispatch(ctx->CurrentClientDispatch);
      return;
   }

   *glthread->batch_queue_tail = batch;
   glthread->batch_queue_tail = &batch->next;
   pthread_cond_broadcast(&glthread->new_work);
}

void
_mesa_glthread_flush_batch(struct gl_context *ctx)
{
   struct glthread_state *glthread = ctx->GLThread;
   struct glthread_batch *batch;

   if (!glthread)
      return;

   batch = glthread->batch;
   if (!batch->used)
      return;

   pthread_mutex_lock(&glthread->mutex);
   _mesa_glthread_flush_batch_locked(ctx);
   pthread_mutex_unlock(&glthread->mutex);
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
   if (pthread_equal(pthread_self(), glthread->thread))
      return;

   pthread_mutex_lock(&glthread->mutex);

   if (!(glthread->batch_queue || glthread->busy)) {
      if (glthread->batch && glthread->batch->used) {
         struct _glapi_table *dispatch = _glapi_get_dispatch();
         glthread_unmarshal_batch(ctx, glthread->batch, false);
         _glapi_set_dispatch(dispatch);
      }
   } else {
      _mesa_glthread_flush_batch_locked(ctx);
      while (glthread->batch_queue || glthread->busy)
         pthread_cond_wait(&glthread->work_done, &glthread->mutex);
   }

   pthread_mutex_unlock(&glthread->mutex);
}

#endif
