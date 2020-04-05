/*
 * Copyright © 2020 Advanced Micro Devices, Inc.
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

/* This implements vertex array state tracking for glthread. It's separate
 * from the rest of Mesa. Only minimum functionality is implemented here
 * to serve glthread.
 */

#include "main/glthread.h"
#include "main/mtypes.h"
#include "main/hash.h"
#include "main/dispatch.h"

/* TODO:
 *   - Implement better tracking of user pointers
 *   - These can unbind user pointers:
 *       ARB_vertex_attrib_binding
 *       ARB_direct_state_access
 *       EXT_direct_state_access
 */

static struct glthread_vao *
lookup_vao(struct gl_context *ctx, GLuint id)
{
   struct glthread_state *glthread = &ctx->GLThread;
   struct glthread_vao *vao;

   assert(id != 0);

   if (glthread->LastLookedUpVAO &&
       glthread->LastLookedUpVAO->Name == id) {
      vao = glthread->LastLookedUpVAO;
   } else {
      vao = _mesa_HashLookupLocked(glthread->VAOs, id);
      if (!vao)
         return NULL;

      glthread->LastLookedUpVAO = vao;
   }

   return vao;
}

void
_mesa_glthread_BindVertexArray(struct gl_context *ctx, GLuint id)
{
   struct glthread_state *glthread = &ctx->GLThread;

   if (id == 0) {
      glthread->CurrentVAO = &glthread->DefaultVAO;
   } else {
      struct glthread_vao *vao = lookup_vao(ctx, id);

      if (vao)
         glthread->CurrentVAO = vao;
   }
}

void
_mesa_glthread_DeleteVertexArrays(struct gl_context *ctx,
                                  GLsizei n, const GLuint *ids)
{
   struct glthread_state *glthread = &ctx->GLThread;

   if (!ids)
      return;

   for (int i = 0; i < n; i++) {
      /* IDs equal to 0 should be silently ignored. */
      if (!ids[i])
         continue;

      struct glthread_vao *vao = lookup_vao(ctx, ids[i]);
      if (!vao)
         continue;

      /* If the array object is currently bound, the spec says "the binding
       * for that object reverts to zero and the default vertex array
       * becomes current."
       */
      if (glthread->CurrentVAO == vao)
         glthread->CurrentVAO = &glthread->DefaultVAO;

      if (glthread->LastLookedUpVAO == vao)
         glthread->LastLookedUpVAO = NULL;

      /* The ID is immediately freed for re-use */
      _mesa_HashRemoveLocked(glthread->VAOs, vao->Name);
      free(vao);
   }
}

void
_mesa_glthread_GenVertexArrays(struct gl_context *ctx,
                               GLsizei n, GLuint *arrays)
{
   struct glthread_state *glthread = &ctx->GLThread;

   if (!arrays)
      return;

   /* The IDs have been generated at this point. Create VAOs for glthread. */
   for (int i = 0; i < n; i++) {
      GLuint id = arrays[i];
      struct glthread_vao *vao;

      vao = calloc(1, sizeof(*vao));
      if (!vao)
         continue; /* Is that all we can do? */

      vao->Name = id;
      _mesa_HashInsertLocked(glthread->VAOs, id, vao);
   }
}

void
_mesa_glthread_ClientState(struct gl_context *ctx, GLuint *vaobj,
                           gl_vert_attrib attrib, bool enable)
{
   struct glthread_state *glthread = &ctx->GLThread;
   struct glthread_vao *vao;

   if (attrib >= VERT_ATTRIB_MAX)
      return;

   if (vaobj) {
      vao = lookup_vao(ctx, *vaobj);
      if (!vao)
         return;
   } else {
      vao = glthread->CurrentVAO;
   }

   if (enable)
      vao->Enabled |= 1u << attrib;
   else
      vao->Enabled &= ~(1u << attrib);
}

void
_mesa_glthread_AttribPointer(struct gl_context *ctx, gl_vert_attrib attrib)
{
   struct glthread_state *glthread = &ctx->GLThread;
   struct glthread_vao *vao = glthread->CurrentVAO;

   if (glthread->CurrentArrayBufferName != 0)
      vao->UserPointerMask &= ~(1u << attrib);
   else
      vao->UserPointerMask |= 1u << attrib;
}
