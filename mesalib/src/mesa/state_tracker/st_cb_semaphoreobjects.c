/*
 * Copyright © 2017 Valve Corporation.
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */


#include "main/mtypes.h"
#include "main/context.h"

#include "main/externalobjects.h"

#include "st_context.h"
#include "st_texture.h"
#include "st_util.h"
#include "st_cb_bitmap.h"
#include "st_cb_bufferobjects.h"
#include "st_cb_semaphoreobjects.h"

#include "frontend/drm_driver.h"
#include "pipe/p_context.h"
#include "pipe/p_screen.h"

static struct gl_semaphore_object *
st_semaphoreobj_alloc(struct gl_context *ctx, GLuint name)
{
   struct st_semaphore_object *st_obj = ST_CALLOC_STRUCT(st_semaphore_object);
   if (!st_obj)
      return NULL;

   _mesa_initialize_semaphore_object(ctx, &st_obj->Base, name);
   return &st_obj->Base;
}

static void
st_semaphoreobj_free(struct gl_context *ctx,
                     struct gl_semaphore_object *semObj)
{
   _mesa_delete_semaphore_object(ctx, semObj);
}


static void
st_import_semaphoreobj_fd(struct gl_context *ctx,
                       struct gl_semaphore_object *semObj,
                       int fd)
{
   struct st_semaphore_object *st_obj = st_semaphore_object(semObj);
   struct st_context *st = st_context(ctx);
   struct pipe_context *pipe = st->pipe;

   pipe->create_fence_fd(pipe, &st_obj->fence, fd, PIPE_FD_TYPE_SYNCOBJ);

#if !defined(_WIN32)
   /* We own fd, but we no longer need it. So get rid of it */
   close(fd);
#endif
}

static void
st_server_wait_semaphore(struct gl_context *ctx,
                         struct gl_semaphore_object *semObj,
                         GLuint numBufferBarriers,
                         struct gl_buffer_object **bufObjs,
                         GLuint numTextureBarriers,
                         struct gl_texture_object **texObjs,
                         const GLenum *srcLayouts)
{
   struct st_semaphore_object *st_obj = st_semaphore_object(semObj);
   struct st_context *st = st_context(ctx);
   struct pipe_context *pipe = st->pipe;
   struct st_buffer_object *bufObj;
   struct st_texture_object *texObj;

   /* The driver is allowed to flush during fence_server_sync, be prepared */
   st_flush_bitmap_cache(st);
   pipe->fence_server_sync(pipe, st_obj->fence);

   /**
    * According to the EXT_external_objects spec, the memory operations must
    * follow the wait. This is to make sure the flush is executed after the
    * other party is done modifying the memory.
    *
    * Relevant excerpt from section "4.2.3 Waiting for Semaphores":
    *
    * Following completion of the semaphore wait operation, memory will also be
    * made visible in the specified buffer and texture objects.
    *
    */
   for (unsigned i = 0; i < numBufferBarriers; i++) {
      if (!bufObjs[i])
         continue;

      bufObj = st_buffer_object(bufObjs[i]);
      if (bufObj->buffer)
         pipe->flush_resource(pipe, bufObj->buffer);
   }

   for (unsigned i = 0; i < numTextureBarriers; i++) {
      if (!texObjs[i])
         continue;

      texObj = st_texture_object(texObjs[i]);
      if (texObj->pt)
         pipe->flush_resource(pipe, texObj->pt);
   }
}

static void
st_server_signal_semaphore(struct gl_context *ctx,
                           struct gl_semaphore_object *semObj,
                           GLuint numBufferBarriers,
                           struct gl_buffer_object **bufObjs,
                           GLuint numTextureBarriers,
                           struct gl_texture_object **texObjs,
                           const GLenum *dstLayouts)
{
   struct st_semaphore_object *st_obj = st_semaphore_object(semObj);
   struct st_context *st = st_context(ctx);
   struct pipe_context *pipe = st->pipe;
   struct st_buffer_object *bufObj;
   struct st_texture_object *texObj;

   for (unsigned i = 0; i < numBufferBarriers; i++) {
      if (!bufObjs[i])
         continue;

      bufObj = st_buffer_object(bufObjs[i]);
      if (bufObj->buffer)
         pipe->flush_resource(pipe, bufObj->buffer);
   }

   for (unsigned i = 0; i < numTextureBarriers; i++) {
      if (!texObjs[i])
         continue;

      texObj = st_texture_object(texObjs[i]);
      if (texObj->pt)
         pipe->flush_resource(pipe, texObj->pt);
   }

   /* The driver is allowed to flush during fence_server_signal, be prepared */
   st_flush_bitmap_cache(st);
   pipe->fence_server_signal(pipe, st_obj->fence);
}

void
st_init_semaphoreobject_functions(struct dd_function_table *functions)
{
   functions->NewSemaphoreObject = st_semaphoreobj_alloc;
   functions->DeleteSemaphoreObject = st_semaphoreobj_free;
   functions->ImportSemaphoreFd = st_import_semaphoreobj_fd;
   functions->ServerWaitSemaphoreObject = st_server_wait_semaphore;
   functions->ServerSignalSemaphoreObject = st_server_signal_semaphore;
}
