/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 1999-2006  Brian Paul   All Rights Reserved.
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
 */


#include "glheader.h"

#include "context.h"
#include "fbobject.h"
#include "formats.h"
#include "mtypes.h"
#include "renderbuffer.h"
#include "util/u_memory.h"


/**
 * Initialize the fields of a gl_renderbuffer to default values.
 */
void
_mesa_init_renderbuffer(struct gl_renderbuffer *rb, GLuint name)
{
   GET_CURRENT_CONTEXT(ctx);

   simple_mtx_init(&rb->Mutex, mtx_plain);

   rb->ClassID = 0;
   rb->Name = name;
   rb->RefCount = 1;
   rb->Delete = _mesa_delete_renderbuffer;

   /* The rest of these should be set later by the caller of this function or
    * the AllocStorage method:
    */
   rb->AllocStorage = NULL;

   rb->Width = 0;
   rb->Height = 0;
   rb->Depth = 0;

   /* In GL 3, the initial format is GL_RGBA according to Table 6.26
    * on page 302 of the GL 3.3 spec.
    *
    * In GLES 3, the initial format is GL_RGBA4 according to Table 6.15
    * on page 258 of the GLES 3.0.4 spec.
    *
    * If the context is current, set the initial format based on the
    * specs. If the context is not current, we cannot determine the
    * API, so default to GL_RGBA.
    */
   if (ctx && _mesa_is_gles(ctx)) {
      rb->InternalFormat = GL_RGBA4;
   } else {
      rb->InternalFormat = GL_RGBA;
   }

   rb->Format = MESA_FORMAT_NONE;
}


/**
 * Allocate a new gl_renderbuffer object.  This can be used for user-created
 * renderbuffers or window-system renderbuffers.
 */
struct gl_renderbuffer *
_mesa_new_renderbuffer(struct gl_context *ctx, GLuint name)
{
   struct gl_renderbuffer *rb = CALLOC_STRUCT(gl_renderbuffer);
   if (rb) {
      _mesa_init_renderbuffer(rb, name);
   }
   return rb;
}


/**
 * Delete a gl_framebuffer.
 * This is the default function for renderbuffer->Delete().
 * Drivers which subclass gl_renderbuffer should probably implement their
 * own delete function.  But the driver might also call this function to
 * free the object in the end.
 */
void
_mesa_delete_renderbuffer(struct gl_context *ctx, struct gl_renderbuffer *rb)
{
   simple_mtx_destroy(&rb->Mutex);
   free(rb->Label);
   free(rb);
}

static void
validate_and_init_renderbuffer_attachment(struct gl_framebuffer *fb,
                                          gl_buffer_index bufferName,
                                          struct gl_renderbuffer *rb)
{
   assert(fb);
   assert(rb);
   assert(bufferName < BUFFER_COUNT);

   /* There should be no previous renderbuffer on this attachment point,
    * with the exception of depth/stencil since the same renderbuffer may
    * be used for both.
    */
   assert(bufferName == BUFFER_DEPTH ||
          bufferName == BUFFER_STENCIL ||
          fb->Attachment[bufferName].Renderbuffer == NULL);

   /* winsys vs. user-created buffer cross check */
   if (_mesa_is_user_fbo(fb)) {
      assert(rb->Name);
   }
   else {
      assert(!rb->Name);
   }

   fb->Attachment[bufferName].Type = GL_RENDERBUFFER_EXT;
   fb->Attachment[bufferName].Complete = GL_TRUE;
}


/**
 * Attach a renderbuffer to a framebuffer.
 * \param bufferName  one of the BUFFER_x tokens
 *
 * This function avoids adding a reference and is therefore intended to be
 * used with a freshly created renderbuffer.
 */
void
_mesa_attach_and_own_rb(struct gl_framebuffer *fb,
                        gl_buffer_index bufferName,
                        struct gl_renderbuffer *rb)
{
   assert(rb->RefCount == 1);

   validate_and_init_renderbuffer_attachment(fb, bufferName, rb);

   _mesa_reference_renderbuffer(&fb->Attachment[bufferName].Renderbuffer,
                                NULL);
   fb->Attachment[bufferName].Renderbuffer = rb;
}

/**
 * Attach a renderbuffer to a framebuffer.
 * \param bufferName  one of the BUFFER_x tokens
 */
void
_mesa_attach_and_reference_rb(struct gl_framebuffer *fb,
                              gl_buffer_index bufferName,
                              struct gl_renderbuffer *rb)
{
   validate_and_init_renderbuffer_attachment(fb, bufferName, rb);
   _mesa_reference_renderbuffer(&fb->Attachment[bufferName].Renderbuffer, rb);
}


/**
 * Remove the named renderbuffer from the given framebuffer.
 * \param bufferName  one of the BUFFER_x tokens
 */
void
_mesa_remove_renderbuffer(struct gl_framebuffer *fb,
                          gl_buffer_index bufferName)
{
   assert(bufferName < BUFFER_COUNT);
   _mesa_reference_renderbuffer(&fb->Attachment[bufferName].Renderbuffer,
                                NULL);
}


/**
 * Set *ptr to point to rb.  If *ptr points to another renderbuffer,
 * dereference that buffer first.  The new renderbuffer's refcount will
 * be incremented.  The old renderbuffer's refcount will be decremented.
 * This is normally only called from the _mesa_reference_renderbuffer() macro
 * when there's a real pointer change.
 */
void
_mesa_reference_renderbuffer_(struct gl_renderbuffer **ptr,
                              struct gl_renderbuffer *rb)
{
   if (*ptr) {
      /* Unreference the old renderbuffer */
      GLboolean deleteFlag = GL_FALSE;
      struct gl_renderbuffer *oldRb = *ptr;

      simple_mtx_lock(&oldRb->Mutex);
      assert(oldRb->RefCount > 0);
      oldRb->RefCount--;
      deleteFlag = (oldRb->RefCount == 0);
      simple_mtx_unlock(&oldRb->Mutex);

      if (deleteFlag) {
         GET_CURRENT_CONTEXT(ctx);
         oldRb->Delete(ctx, oldRb);
      }

      *ptr = NULL;
   }
   assert(!*ptr);

   if (rb) {
      /* reference new renderbuffer */
      simple_mtx_lock(&rb->Mutex);
      rb->RefCount++;
      simple_mtx_unlock(&rb->Mutex);
      *ptr = rb;
   }
}
