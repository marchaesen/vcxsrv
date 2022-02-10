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
#include "main/glthread_marshal.h"
#include "main/hash.h"
#include "util/u_atomic.h"
#include "util/u_thread.h"
#include "util/u_cpu_detect.h"

#include "state_tracker/st_context.h"

static void
glthread_unmarshal_batch(void *job, void *gdata, int thread_index)
{
   struct glthread_batch *batch = (struct glthread_batch*)job;
   struct gl_context *ctx = batch->ctx;
   unsigned pos = 0;
   unsigned used = batch->used;
   uint64_t *buffer = batch->buffer;
   const uint64_t *last = &buffer[used];

   _glapi_set_dispatch(ctx->CurrentServerDispatch);

   _mesa_HashLockMutex(ctx->Shared->BufferObjects);
   ctx->BufferObjectsLocked = true;
   simple_mtx_lock(&ctx->Shared->TexMutex);
   ctx->TexturesLocked = true;

   while (pos < used) {
      const struct marshal_cmd_base *cmd =
         (const struct marshal_cmd_base *)&buffer[pos];

      pos += _mesa_unmarshal_dispatch[cmd->cmd_id](ctx, cmd, last);
   }

   ctx->TexturesLocked = false;
   simple_mtx_unlock(&ctx->Shared->TexMutex);
   ctx->BufferObjectsLocked = false;
   _mesa_HashUnlockMutex(ctx->Shared->BufferObjects);

   assert(pos == used);
   batch->used = 0;

   unsigned batch_index = batch - ctx->GLThread.batches;
   /* Atomically set this to -1 if it's equal to batch_index. */
   p_atomic_cmpxchg(&ctx->GLThread.LastProgramChangeBatch, batch_index, -1);
   p_atomic_cmpxchg(&ctx->GLThread.LastDListChangeBatchIndex, batch_index, -1);
}

static void
glthread_thread_initialization(void *job, void *gdata, int thread_index)
{
   struct gl_context *ctx = (struct gl_context*)job;

   st_set_background_context(ctx, &ctx->GLThread.stats);
   _glapi_set_context(ctx);
}

void
_mesa_glthread_init(struct gl_context *ctx)
{
   struct glthread_state *glthread = &ctx->GLThread;

   assert(!glthread->enabled);

   if (!util_queue_init(&glthread->queue, "gl", MARSHAL_MAX_BATCHES - 2,
                        1, 0, NULL)) {
      return;
   }

   glthread->VAOs = _mesa_NewHashTable();
   if (!glthread->VAOs) {
      util_queue_destroy(&glthread->queue);
      return;
   }

   _mesa_glthread_reset_vao(&glthread->DefaultVAO);
   glthread->CurrentVAO = &glthread->DefaultVAO;

   if (!_mesa_create_marshal_tables(ctx)) {
      _mesa_DeleteHashTable(glthread->VAOs);
      util_queue_destroy(&glthread->queue);
      return;
   }

   for (unsigned i = 0; i < MARSHAL_MAX_BATCHES; i++) {
      glthread->batches[i].ctx = ctx;
      util_queue_fence_init(&glthread->batches[i].fence);
   }
   glthread->next_batch = &glthread->batches[glthread->next];
   glthread->used = 0;

   glthread->enabled = true;
   glthread->stats.queue = &glthread->queue;

   glthread->SupportsBufferUploads =
      ctx->Const.BufferCreateMapUnsynchronizedThreadSafe &&
      ctx->Const.AllowMappedBuffersDuringExecution;

   /* If the draw start index is non-zero, glthread can upload to offset 0,
    * which means the attrib offset has to be -(first * stride).
    * So require signed vertex buffer offsets.
    */
   glthread->SupportsNonVBOUploads = glthread->SupportsBufferUploads &&
                                     ctx->Const.VertexBufferOffsetIsInt32;

   ctx->CurrentClientDispatch = ctx->MarshalExec;

   glthread->LastDListChangeBatchIndex = -1;

   /* Execute the thread initialization function in the thread. */
   struct util_queue_fence fence;
   util_queue_fence_init(&fence);
   util_queue_add_job(&glthread->queue, ctx, &fence,
                      glthread_thread_initialization, NULL, 0);
   util_queue_fence_wait(&fence);
   util_queue_fence_destroy(&fence);
}

static void
free_vao(void *data, UNUSED void *userData)
{
   free(data);
}

void
_mesa_glthread_destroy(struct gl_context *ctx, const char *reason)
{
   struct glthread_state *glthread = &ctx->GLThread;

   if (!glthread->enabled)
      return;

   if (reason)
      _mesa_debug(ctx, "glthread destroy reason: %s\n", reason);

   _mesa_glthread_finish(ctx);
   util_queue_destroy(&glthread->queue);

   for (unsigned i = 0; i < MARSHAL_MAX_BATCHES; i++)
      util_queue_fence_destroy(&glthread->batches[i].fence);

   _mesa_HashDeleteAll(glthread->VAOs, free_vao, NULL);
   _mesa_DeleteHashTable(glthread->VAOs);

   ctx->GLThread.enabled = false;
   ctx->CurrentClientDispatch = ctx->CurrentServerDispatch;

   /* Update the dispatch only if the context is current. */
   if (_glapi_get_dispatch() == ctx->MarshalExec) {
       _glapi_set_dispatch(ctx->CurrentClientDispatch);
   }
}

void
_mesa_glthread_flush_batch(struct gl_context *ctx)
{
   struct glthread_state *glthread = &ctx->GLThread;
   if (!glthread->enabled)
      return;

   if (ctx->CurrentServerDispatch == ctx->ContextLost) {
      _mesa_glthread_destroy(ctx, "context lost");
      return;
   }

   if (!glthread->used)
      return; /* the batch is empty */

   /* Pin threads regularly to the same Zen CCX that the main thread is
    * running on. The main thread can move between CCXs.
    */
   if (util_get_cpu_caps()->num_L3_caches > 1 &&
       /* driver support */
       ctx->pipe->set_context_param &&
       ++glthread->pin_thread_counter % 128 == 0) {
      int cpu = util_get_current_cpu();

      if (cpu >= 0) {
         uint16_t L3_cache = util_get_cpu_caps()->cpu_to_L3[cpu];
         if (L3_cache != U_CPU_INVALID_L3) {
            util_set_thread_affinity(glthread->queue.threads[0],
                                     util_get_cpu_caps()->L3_affinity_mask[L3_cache],
                                     NULL, util_get_cpu_caps()->num_cpu_mask_bits);
            ctx->pipe->set_context_param(ctx->pipe,
                                         PIPE_CONTEXT_PARAM_PIN_THREADS_TO_L3_CACHE,
                                         L3_cache);
         }
      }
   }

   struct glthread_batch *next = glthread->next_batch;

   /* Debug: execute the batch immediately from this thread.
    *
    * Note that glthread_unmarshal_batch() changes the dispatch table so we'll
    * need to restore it when it returns.
    */
   if (false) {
      glthread_unmarshal_batch(next, NULL, 0);
      _glapi_set_dispatch(ctx->CurrentClientDispatch);
      return;
   }

   p_atomic_add(&glthread->stats.num_offloaded_items, glthread->used);
   next->used = glthread->used;

   util_queue_add_job(&glthread->queue, next, &next->fence,
                      glthread_unmarshal_batch, NULL, 0);
   glthread->last = glthread->next;
   glthread->next = (glthread->next + 1) % MARSHAL_MAX_BATCHES;
   glthread->next_batch = &glthread->batches[glthread->next];
   glthread->used = 0;
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
   struct glthread_state *glthread = &ctx->GLThread;
   if (!glthread->enabled)
      return;

   /* If this is called from the worker thread, then we've hit a path that
    * might be called from either the main thread or the worker (such as some
    * dri interface entrypoints), in which case we don't need to actually
    * synchronize against ourself.
    */
   if (u_thread_is_self(glthread->queue.threads[0]))
      return;

   struct glthread_batch *last = &glthread->batches[glthread->last];
   struct glthread_batch *next = glthread->next_batch;
   bool synced = false;

   if (!util_queue_fence_is_signalled(&last->fence)) {
      util_queue_fence_wait(&last->fence);
      synced = true;
   }

   if (glthread->used) {
      p_atomic_add(&glthread->stats.num_direct_items, glthread->used);
      next->used = glthread->used;
      glthread->used = 0;

      /* Since glthread_unmarshal_batch changes the dispatch to direct,
       * restore it after it's done.
       */
      struct _glapi_table *dispatch = _glapi_get_dispatch();
      glthread_unmarshal_batch(next, NULL, 0);
      _glapi_set_dispatch(dispatch);

      /* It's not a sync because we don't enqueue partial batches, but
       * it would be a sync if we did. So count it anyway.
       */
      synced = true;
   }

   if (synced)
      p_atomic_inc(&glthread->stats.num_syncs);
}

void
_mesa_glthread_finish_before(struct gl_context *ctx, const char *func)
{
   _mesa_glthread_finish(ctx);

   /* Uncomment this if you want to know where glthread syncs. */
   /*printf("fallback to sync: %s\n", func);*/
}

void
_mesa_error_glthread_safe(struct gl_context *ctx, GLenum error, bool glthread,
                          const char *format, ...)
{
   if (glthread) {
      _mesa_marshal_InternalSetError(error);
   } else {
      char s[MAX_DEBUG_MESSAGE_LENGTH];
      va_list args;

      va_start(args, format);
      ASSERTED size_t len = vsnprintf(s, MAX_DEBUG_MESSAGE_LENGTH, format, args);
      va_end(args);

      /* Whoever calls _mesa_error should use shorter strings. */
      assert(len < MAX_DEBUG_MESSAGE_LENGTH);

      _mesa_error(ctx, error, "%s", s);
   }
}
