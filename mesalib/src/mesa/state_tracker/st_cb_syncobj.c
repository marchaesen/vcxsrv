/**************************************************************************
 *
 * Copyright 2011 Marek Olšák <maraeo@gmail.com>
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
 * IN NO EVENT SHALL AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

 /*
  * Authors:
  *   Marek Olšák <maraeo@gmail.com>
  */

#include "main/glheader.h"
#include "main/macros.h"
#include "pipe/p_context.h"
#include "pipe/p_screen.h"
#include "util/u_memory.h"
#include "st_context.h"
#include "st_cb_syncobj.h"

struct st_sync_object {
   struct gl_sync_object b;

   struct pipe_fence_handle *fence;
   simple_mtx_t mutex; /**< protects "fence" */
};


static struct gl_sync_object *st_new_sync_object(struct gl_context *ctx)
{
   struct st_sync_object *so = CALLOC_STRUCT(st_sync_object);

   simple_mtx_init(&so->mutex, mtx_plain);
   return &so->b;
}

static void st_delete_sync_object(struct gl_context *ctx,
                                  struct gl_sync_object *obj)
{
   struct pipe_screen *screen = st_context(ctx)->pipe->screen;
   struct st_sync_object *so = (struct st_sync_object*)obj;

   screen->fence_reference(screen, &so->fence, NULL);
   simple_mtx_destroy(&so->mutex);
   free(so->b.Label);
   free(so);
}

static void st_fence_sync(struct gl_context *ctx, struct gl_sync_object *obj,
                          GLenum condition, GLbitfield flags)
{
   struct pipe_context *pipe = st_context(ctx)->pipe;
   struct st_sync_object *so = (struct st_sync_object*)obj;

   assert(condition == GL_SYNC_GPU_COMMANDS_COMPLETE && flags == 0);
   assert(so->fence == NULL);

   /* Deferred flush are only allowed when there's a single context. See issue 1430 */
   pipe->flush(pipe, &so->fence, ctx->Shared->RefCount == 1 ? PIPE_FLUSH_DEFERRED : 0);
}

static void st_client_wait_sync(struct gl_context *ctx,
                                struct gl_sync_object *obj,
                                GLbitfield flags, GLuint64 timeout)
{
   struct pipe_context *pipe = st_context(ctx)->pipe;
   struct pipe_screen *screen = pipe->screen;
   struct st_sync_object *so = (struct st_sync_object*)obj;
   struct pipe_fence_handle *fence = NULL;

   /* If the fence doesn't exist, assume it's signalled. */
   simple_mtx_lock(&so->mutex);
   if (!so->fence) {
      simple_mtx_unlock(&so->mutex);
      so->b.StatusFlag = GL_TRUE;
      return;
   }

   /* We need a local copy of the fence pointer, so that we can call
    * fence_finish unlocked.
    */
   screen->fence_reference(screen, &fence, so->fence);
   simple_mtx_unlock(&so->mutex);

   /* Section 4.1.2 of OpenGL 4.5 (Compatibility Profile) says:
    *    [...] if ClientWaitSync is called and all of the following are true:
    *    - the SYNC_FLUSH_COMMANDS_BIT bit is set in flags,
    *    - sync is unsignaled when ClientWaitSync is called,
    *    - and the calls to ClientWaitSync and FenceSync were issued from
    *      the same context,
    *    then the GL will behave as if the equivalent of Flush were inserted
    *    immediately after the creation of sync.
    *
    * Assume GL_SYNC_FLUSH_COMMANDS_BIT is always set, because applications
    * forget to set it.
    */
   if (screen->fence_finish(screen, pipe, fence, timeout)) {
      simple_mtx_lock(&so->mutex);
      screen->fence_reference(screen, &so->fence, NULL);
      simple_mtx_unlock(&so->mutex);
      so->b.StatusFlag = GL_TRUE;
   }
   screen->fence_reference(screen, &fence, NULL);
}

static void st_check_sync(struct gl_context *ctx, struct gl_sync_object *obj)
{
   st_client_wait_sync(ctx, obj, 0, 0);
}

static void st_server_wait_sync(struct gl_context *ctx,
                                struct gl_sync_object *obj,
                                GLbitfield flags, GLuint64 timeout)
{
   struct pipe_context *pipe = st_context(ctx)->pipe;
   struct pipe_screen *screen = pipe->screen;
   struct st_sync_object *so = (struct st_sync_object*)obj;
   struct pipe_fence_handle *fence = NULL;

   /* Nothing needs to be done here if the driver does not support async
    * flushes. */
   if (!pipe->fence_server_sync)
      return;

   /* If the fence doesn't exist, assume it's signalled. */
   simple_mtx_lock(&so->mutex);
   if (!so->fence) {
      simple_mtx_unlock(&so->mutex);
      so->b.StatusFlag = GL_TRUE;
      return;
   }

   /* We need a local copy of the fence pointer. */
   screen->fence_reference(screen, &fence, so->fence);
   simple_mtx_unlock(&so->mutex);

   pipe->fence_server_sync(pipe, fence);
   screen->fence_reference(screen, &fence, NULL);
}

void st_init_syncobj_functions(struct dd_function_table *functions)
{
   functions->NewSyncObject = st_new_sync_object;
   functions->FenceSync = st_fence_sync;
   functions->DeleteSyncObject = st_delete_sync_object;
   functions->CheckSync = st_check_sync;
   functions->ClientWaitSync = st_client_wait_sync;
   functions->ServerWaitSync = st_server_wait_sync;
}
