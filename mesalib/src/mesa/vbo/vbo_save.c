/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 1999-2008  Brian Paul   All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Keith Whitwell <keithw@vmware.com>
 */


#include "main/mtypes.h"
#include "main/arrayobj.h"
#include "main/bufferobj.h"

#include "vbo_private.h"


/**
 * Called at context creation time.
 */
void vbo_save_init( struct gl_context *ctx )
{
   struct vbo_context *vbo = vbo_context(ctx);
   struct vbo_save_context *save = &vbo->save;

   save->ctx = ctx;

   vbo_save_api_init( save );

   for (gl_vertex_processing_mode vpm = VP_MODE_FF; vpm < VP_MODE_MAX; ++vpm)
      save->VAO[vpm] = NULL;

   ctx->Driver.CurrentSavePrimitive = PRIM_OUTSIDE_BEGIN_END;
}


void vbo_save_destroy( struct gl_context *ctx )
{
   struct vbo_context *vbo = vbo_context(ctx);
   struct vbo_save_context *save = &vbo->save;

   for (gl_vertex_processing_mode vpm = VP_MODE_FF; vpm < VP_MODE_MAX; ++vpm)
      _mesa_reference_vao(ctx, &save->VAO[vpm], NULL);

   if (save->prim_store) {
      if ( --save->prim_store->refcount == 0 ) {
         free(save->prim_store);
         save->prim_store = NULL;
      }
   }
   if (save->vertex_store) {
      _mesa_reference_buffer_object(ctx, &save->vertex_store->bufferobj, NULL);
      free(save->vertex_store);
      save->vertex_store = NULL;
   }
}




/* Note that this can occur during the playback of a display list:
 */
void vbo_save_fallback( struct gl_context *ctx, GLboolean fallback )
{
   struct vbo_save_context *save = &vbo_context(ctx)->save;

   if (fallback)
      save->replay_flags |= VBO_SAVE_FALLBACK;
   else
      save->replay_flags &= ~VBO_SAVE_FALLBACK;
}
