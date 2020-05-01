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
#include "main/glformats.h"
#include "main/mtypes.h"
#include "main/hash.h"
#include "main/dispatch.h"
#include "main/varray.h"

/* TODO:
 *   - Handle ARB_vertex_attrib_binding (incl. EXT_dsa and ARB_dsa)
 */

void
_mesa_glthread_reset_vao(struct glthread_vao *vao)
{
   static unsigned default_elem_size[VERT_ATTRIB_MAX] = {
      [VERT_ATTRIB_NORMAL] = 12,
      [VERT_ATTRIB_COLOR1] = 12,
      [VERT_ATTRIB_FOG] = 4,
      [VERT_ATTRIB_COLOR_INDEX] = 4,
      [VERT_ATTRIB_EDGEFLAG] = 1,
      [VERT_ATTRIB_POINT_SIZE] = 4,
   };

   vao->CurrentElementBufferName = 0;
   vao->UserEnabled = 0;
   vao->Enabled = 0;
   vao->UserPointerMask = 0;
   vao->NonZeroDivisorMask = 0;

   for (unsigned i = 0; i < ARRAY_SIZE(vao->Attrib); i++) {
      unsigned elem_size = default_elem_size[i];
      if (!elem_size)
         elem_size = 16;

      vao->Attrib[i].ElementSize = elem_size;
      vao->Attrib[i].Stride = elem_size;
      vao->Attrib[i].Divisor = 0;
      vao->Attrib[i].Pointer = NULL;
   }
}

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
      _mesa_glthread_reset_vao(vao);
      _mesa_HashInsertLocked(glthread->VAOs, id, vao);
   }
}

/* If vaobj is NULL, use the currently-bound VAO. */
static inline struct glthread_vao *
get_vao(struct gl_context *ctx, const GLuint *vaobj)
{
   if (vaobj)
      return lookup_vao(ctx, *vaobj);

   return ctx->GLThread.CurrentVAO;
}

static void
update_primitive_restart(struct gl_context *ctx)
{
   struct glthread_state *glthread = &ctx->GLThread;

   glthread->_PrimitiveRestart = glthread->PrimitiveRestart ||
                                 glthread->PrimitiveRestartFixedIndex;
   glthread->_RestartIndex[0] =
      _mesa_get_prim_restart_index(glthread->PrimitiveRestartFixedIndex,
                                   glthread->RestartIndex, 1);
   glthread->_RestartIndex[1] =
      _mesa_get_prim_restart_index(glthread->PrimitiveRestartFixedIndex,
                                   glthread->RestartIndex, 2);
   glthread->_RestartIndex[3] =
      _mesa_get_prim_restart_index(glthread->PrimitiveRestartFixedIndex,
                                   glthread->RestartIndex, 4);
}

void
_mesa_glthread_set_prim_restart(struct gl_context *ctx, GLenum cap, bool value)
{
   switch (cap) {
   case GL_PRIMITIVE_RESTART:
      ctx->GLThread.PrimitiveRestart = value;
      break;
   case GL_PRIMITIVE_RESTART_FIXED_INDEX:
      ctx->GLThread.PrimitiveRestartFixedIndex = value;
      break;
   }

   update_primitive_restart(ctx);
}

void
_mesa_glthread_PrimitiveRestartIndex(struct gl_context *ctx, GLuint index)
{
   ctx->GLThread.RestartIndex = index;
   update_primitive_restart(ctx);
}

void
_mesa_glthread_ClientState(struct gl_context *ctx, GLuint *vaobj,
                           gl_vert_attrib attrib, bool enable)
{
   /* The primitive restart client state uses a special value. */
   if (attrib == VERT_ATTRIB_PRIMITIVE_RESTART_NV) {
      ctx->GLThread.PrimitiveRestart = enable;
      update_primitive_restart(ctx);
      return;
   }

   if (attrib >= VERT_ATTRIB_MAX)
      return;

   struct glthread_vao *vao = get_vao(ctx, vaobj);
   if (!vao)
      return;

   if (enable)
      vao->UserEnabled |= 1u << attrib;
   else
      vao->UserEnabled &= ~(1u << attrib);

   /* The generic0 attribute superseeds the position attribute */
   vao->Enabled = vao->UserEnabled;
   if (vao->Enabled & VERT_BIT_GENERIC0)
      vao->Enabled &= ~VERT_BIT_POS;
}

void _mesa_glthread_AttribDivisor(struct gl_context *ctx, const GLuint *vaobj,
                                  gl_vert_attrib attrib, GLuint divisor)
{
   if (attrib >= VERT_ATTRIB_MAX)
      return;

   struct glthread_vao *vao = get_vao(ctx, vaobj);
   if (!vao)
      return;

   vao->Attrib[attrib].Divisor = divisor;

   if (divisor)
      vao->NonZeroDivisorMask |= 1u << attrib;
   else
      vao->NonZeroDivisorMask &= ~(1u << attrib);
}

static void
attrib_pointer(struct glthread_state *glthread, struct glthread_vao *vao,
               GLuint buffer, gl_vert_attrib attrib,
               GLint size, GLenum type, GLsizei stride,
               const void *pointer)
{
   if (attrib >= VERT_ATTRIB_MAX)
      return;

   unsigned elem_size = _mesa_bytes_per_vertex_attrib(size, type);

   vao->Attrib[attrib].ElementSize = elem_size;
   vao->Attrib[attrib].Stride = stride ? stride : elem_size;
   vao->Attrib[attrib].Pointer = pointer;

   if (buffer != 0)
      vao->UserPointerMask &= ~(1u << attrib);
   else
      vao->UserPointerMask |= 1u << attrib;
}

void
_mesa_glthread_AttribPointer(struct gl_context *ctx, gl_vert_attrib attrib,
                             GLint size, GLenum type, GLsizei stride,
                             const void *pointer)
{
   struct glthread_state *glthread = &ctx->GLThread;

   attrib_pointer(glthread, glthread->CurrentVAO,
                  glthread->CurrentArrayBufferName,
                  attrib, size, type, stride, pointer);
}

void
_mesa_glthread_DSAAttribPointer(struct gl_context *ctx, GLuint vaobj,
                                GLuint buffer, gl_vert_attrib attrib,
                                GLint size, GLenum type, GLsizei stride,
                                GLintptr offset)
{
   struct glthread_state *glthread = &ctx->GLThread;
   struct glthread_vao *vao;

   vao = lookup_vao(ctx, vaobj);
   if (!vao)
      return;

   attrib_pointer(glthread, vao, buffer, attrib, size, type, stride,
                  (const void*)offset);
}

void
_mesa_glthread_PushClientAttrib(struct gl_context *ctx, GLbitfield mask,
                                bool set_default)
{
   struct glthread_state *glthread = &ctx->GLThread;

   if (glthread->ClientAttribStackTop >= MAX_CLIENT_ATTRIB_STACK_DEPTH)
      return;

   struct glthread_client_attrib *top =
      &glthread->ClientAttribStack[glthread->ClientAttribStackTop];

   if (mask & GL_CLIENT_VERTEX_ARRAY_BIT) {
      top->VAO = *glthread->CurrentVAO;
      top->CurrentArrayBufferName = glthread->CurrentArrayBufferName;
      top->ClientActiveTexture = glthread->ClientActiveTexture;
      top->RestartIndex = glthread->RestartIndex;
      top->PrimitiveRestart = glthread->PrimitiveRestart;
      top->PrimitiveRestartFixedIndex = glthread->PrimitiveRestartFixedIndex;
      top->Valid = true;
   } else {
      top->Valid = false;
   }

   glthread->ClientAttribStackTop++;

   if (set_default)
      _mesa_glthread_ClientAttribDefault(ctx, mask);
}

void
_mesa_glthread_PopClientAttrib(struct gl_context *ctx)
{
   struct glthread_state *glthread = &ctx->GLThread;

   if (glthread->ClientAttribStackTop == 0)
      return;

   glthread->ClientAttribStackTop--;

   struct glthread_client_attrib *top =
      &glthread->ClientAttribStack[glthread->ClientAttribStackTop];

   if (!top->Valid)
      return;

   /* Popping a delete VAO is an error. */
   struct glthread_vao *vao = NULL;
   if (top->VAO.Name) {
      vao = lookup_vao(ctx, top->VAO.Name);
      if (!vao)
         return;
   }

   /* Restore states. */
   glthread->CurrentArrayBufferName = top->CurrentArrayBufferName;
   glthread->ClientActiveTexture = top->ClientActiveTexture;
   glthread->RestartIndex = top->RestartIndex;
   glthread->PrimitiveRestart = top->PrimitiveRestart;
   glthread->PrimitiveRestartFixedIndex = top->PrimitiveRestartFixedIndex;

   if (!vao)
      vao = &glthread->DefaultVAO;

   assert(top->VAO.Name == vao->Name);
   *vao = top->VAO; /* Copy all fields. */
   glthread->CurrentVAO = vao;
}

void
_mesa_glthread_ClientAttribDefault(struct gl_context *ctx, GLbitfield mask)
{
   struct glthread_state *glthread = &ctx->GLThread;

   if (!(mask & GL_CLIENT_VERTEX_ARRAY_BIT))
      return;

   glthread->CurrentArrayBufferName = 0;
   glthread->ClientActiveTexture = 0;
   glthread->RestartIndex = 0;
   glthread->PrimitiveRestart = false;
   glthread->PrimitiveRestartFixedIndex = false;
   glthread->CurrentVAO = &glthread->DefaultVAO;
   _mesa_glthread_reset_vao(glthread->CurrentVAO);
}
