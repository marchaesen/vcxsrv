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

#include <stdbool.h>
#include <stdio.h>
#include "main/arrayobj.h"
#include "main/glheader.h"
#include "main/bufferobj.h"
#include "main/context.h"
#include "main/enums.h"
#include "main/state.h"
#include "main/varray.h"
#include "main/vtxfmt.h"

#include "vbo_noop.h"
#include "vbo_private.h"


static void
vbo_exec_debug_verts(struct vbo_exec_context *exec)
{
   GLuint count = exec->vtx.vert_count;
   GLuint i;

   printf("%s: %u vertices %d primitives, %d vertsize\n",
          __func__,
          count,
          exec->vtx.prim_count,
          exec->vtx.vertex_size);

   for (i = 0 ; i < exec->vtx.prim_count ; i++) {
      struct _mesa_prim *prim = &exec->vtx.prim[i];
      printf("   prim %d: %s%s %d..%d %s %s\n",
             i,
             _mesa_lookup_prim_by_nr(prim->mode),
             prim->weak ? " (weak)" : "",
             prim->start,
             prim->start + prim->count,
             prim->begin ? "BEGIN" : "(wrap)",
             prim->end ? "END" : "(wrap)");
   }
}


/**
 * Copy zero, one or two vertices from the current vertex buffer into
 * the temporary "copy" buffer.
 * This is used when a single primitive overflows a vertex buffer and
 * we need to continue the primitive in a new vertex buffer.
 * The temporary "copy" buffer holds the vertices which need to get
 * copied from the old buffer to the new one.
 */
static GLuint
vbo_copy_vertices(struct vbo_exec_context *exec)
{
   struct _mesa_prim *last_prim = &exec->vtx.prim[exec->vtx.prim_count - 1];
   const GLuint nr = last_prim->count;
   GLuint ovf, i;
   const GLuint sz = exec->vtx.vertex_size;
   fi_type *dst = exec->vtx.copied.buffer;
   const fi_type *src = exec->vtx.buffer_map + last_prim->start * sz;

   switch (exec->ctx->Driver.CurrentExecPrimitive) {
   case GL_POINTS:
      return 0;
   case GL_LINES:
      ovf = nr&1;
      for (i = 0 ; i < ovf ; i++)
         memcpy(dst+i*sz, src+(nr-ovf+i)*sz, sz * sizeof(GLfloat));
      return i;
   case GL_TRIANGLES:
      ovf = nr%3;
      for (i = 0 ; i < ovf ; i++)
         memcpy(dst+i*sz, src+(nr-ovf+i)*sz, sz * sizeof(GLfloat));
      return i;
   case GL_QUADS:
      ovf = nr&3;
      for (i = 0 ; i < ovf ; i++)
         memcpy(dst+i*sz, src+(nr-ovf+i)*sz, sz * sizeof(GLfloat));
      return i;
   case GL_LINE_STRIP:
      if (nr == 0) {
         return 0;
      }
      else {
         memcpy(dst, src+(nr-1)*sz, sz * sizeof(GLfloat));
         return 1;
      }
   case GL_LINE_LOOP:
      if (last_prim->begin == 0) {
         /* We're dealing with the second or later section of a split/wrapped
          * GL_LINE_LOOP.  Since we're converting line loops to line strips,
          * we've already increment the last_prim->start counter by one to
          * skip the 0th vertex in the loop.  We need to undo that (effectively
          * subtract one from last_prim->start) so that we copy the 0th vertex
          * to the next vertex buffer.
          */
         assert(last_prim->start > 0);
         src -= sz;
      }
      /* fall-through */
   case GL_TRIANGLE_FAN:
   case GL_POLYGON:
      if (nr == 0) {
         return 0;
      }
      else if (nr == 1) {
         memcpy(dst, src+0, sz * sizeof(GLfloat));
         return 1;
      }
      else {
         memcpy(dst, src+0, sz * sizeof(GLfloat));
         memcpy(dst+sz, src+(nr-1)*sz, sz * sizeof(GLfloat));
         return 2;
      }
   case GL_TRIANGLE_STRIP:
      /* no parity issue, but need to make sure the tri is not drawn twice */
      if (nr & 1) {
         last_prim->count--;
      }
      /* fallthrough */
   case GL_QUAD_STRIP:
      switch (nr) {
      case 0:
         ovf = 0;
         break;
      case 1:
         ovf = 1;
         break;
      default:
         ovf = 2 + (nr & 1);
         break;
      }
      for (i = 0 ; i < ovf ; i++)
         memcpy(dst+i*sz, src+(nr-ovf+i)*sz, sz * sizeof(GLfloat));
      return i;
   case PRIM_OUTSIDE_BEGIN_END:
      return 0;
   default:
      unreachable("Unexpected primitive type");
      return 0;
   }
}



/* TODO: populate these as the vertex is defined:
 */
static void
vbo_exec_bind_arrays(struct gl_context *ctx)
{
   struct vbo_context *vbo = vbo_context(ctx);
   struct gl_vertex_array_object *vao = vbo->VAO;
   struct vbo_exec_context *exec = &vbo->exec;

   GLintptr buffer_offset;
   if (_mesa_is_bufferobj(exec->vtx.bufferobj)) {
      assert(exec->vtx.bufferobj->Mappings[MAP_INTERNAL].Pointer);
      buffer_offset = exec->vtx.bufferobj->Mappings[MAP_INTERNAL].Offset;
   } else {
      /* Ptr into ordinary app memory */
      buffer_offset = (GLbyte *)exec->vtx.buffer_map - (GLbyte *)NULL;
   }

   const gl_vertex_processing_mode mode = ctx->VertexProgram._VPMode;

   /* Compute the bitmasks of vao_enabled arrays */
   GLbitfield vao_enabled = _vbo_get_vao_enabled_from_vbo(mode, exec->vtx.enabled);

   /* At first disable arrays no longer needed */
   GLbitfield mask = vao->_Enabled & ~vao_enabled;
   while (mask) {
      const int vao_attr = u_bit_scan(&mask);
      _mesa_disable_vertex_array_attrib(ctx, vao, vao_attr, false);
   }
   assert((~vao_enabled & vao->_Enabled) == 0);

   /* Bind the buffer object */
   _mesa_bind_vertex_buffer(ctx, vao, 0, exec->vtx.bufferobj, buffer_offset,
                            exec->vtx.vertex_size*sizeof(GLfloat), false);

   /* Retrieve the mapping from VBO_ATTRIB to VERT_ATTRIB space
    * Note that the position/generic0 aliasing is done in the VAO.
    */
   const GLubyte *const vao_to_vbo_map = _vbo_attribute_alias_map[mode];
   /* Now set the enabled arrays */
   mask = vao_enabled;
   while (mask) {
      const int vao_attr = u_bit_scan(&mask);
      const GLubyte vbo_attr = vao_to_vbo_map[vao_attr];

      const GLubyte size = exec->vtx.attrsz[vbo_attr];
      const GLenum16 type = exec->vtx.attrtype[vbo_attr];
      const GLuint offset = (GLuint)((GLbyte *)exec->vtx.attrptr[vbo_attr] -
                                     (GLbyte *)exec->vtx.vertex);

      /* Set and enable */
      _vbo_set_attrib_format(ctx, vao, vao_attr, buffer_offset,
                             size, type, offset);
      if ((vao->_Enabled & VERT_BIT(vao_attr)) == 0)
         _mesa_enable_vertex_array_attrib(ctx, vao, vao_attr, false);

      /* The vao is initially created with all bindings set to 0. */
      assert(vao->VertexAttrib[vao_attr].BufferBindingIndex == 0);
   }
   assert(vao_enabled == vao->_Enabled);
   assert(!_mesa_is_bufferobj(exec->vtx.bufferobj) ||
          (vao_enabled & ~vao->VertexAttribBufferMask) == 0);

   _mesa_update_vao_derived_arrays(ctx, vao);
   vao->NewArrays = 0;

   _mesa_set_draw_vao(ctx, vao, _vbo_get_vao_filter(mode));
   /* The exec VAO is not immutable, so we need to set manually */
   ctx->NewDriverState |= ctx->DriverFlags.NewArray;

   _mesa_set_drawing_arrays(ctx, vbo->draw_arrays.inputs);
   /* Finally update the inputs array */
   _vbo_update_inputs(ctx, &vbo->draw_arrays);
}


/**
 * Unmap the VBO.  This is called before drawing.
 */
static void
vbo_exec_vtx_unmap(struct vbo_exec_context *exec)
{
   if (_mesa_is_bufferobj(exec->vtx.bufferobj)) {
      struct gl_context *ctx = exec->ctx;

      if (ctx->Driver.FlushMappedBufferRange) {
         GLintptr offset = exec->vtx.buffer_used -
                           exec->vtx.bufferobj->Mappings[MAP_INTERNAL].Offset;
         GLsizeiptr length = (exec->vtx.buffer_ptr - exec->vtx.buffer_map) *
                             sizeof(float);

         if (length)
            ctx->Driver.FlushMappedBufferRange(ctx, offset, length,
                                               exec->vtx.bufferobj,
                                               MAP_INTERNAL);
      }

      exec->vtx.buffer_used += (exec->vtx.buffer_ptr -
                                exec->vtx.buffer_map) * sizeof(float);

      assert(exec->vtx.buffer_used <= VBO_VERT_BUFFER_SIZE);
      assert(exec->vtx.buffer_ptr != NULL);

      ctx->Driver.UnmapBuffer(ctx, exec->vtx.bufferobj, MAP_INTERNAL);
      exec->vtx.buffer_map = NULL;
      exec->vtx.buffer_ptr = NULL;
      exec->vtx.max_vert = 0;
   }
}


/**
 * Map the vertex buffer to begin storing glVertex, glColor, etc data.
 */
void
vbo_exec_vtx_map(struct vbo_exec_context *exec)
{
   struct gl_context *ctx = exec->ctx;
   const GLenum accessRange = GL_MAP_WRITE_BIT |  /* for MapBufferRange */
                              GL_MAP_INVALIDATE_RANGE_BIT |
                              GL_MAP_UNSYNCHRONIZED_BIT |
                              GL_MAP_FLUSH_EXPLICIT_BIT |
                              MESA_MAP_NOWAIT_BIT;
   const GLenum usage = GL_STREAM_DRAW_ARB;

   if (!_mesa_is_bufferobj(exec->vtx.bufferobj))
      return;

   assert(!exec->vtx.buffer_map);
   assert(!exec->vtx.buffer_ptr);

   if (VBO_VERT_BUFFER_SIZE > exec->vtx.buffer_used + 1024) {
      /* The VBO exists and there's room for more */
      if (exec->vtx.bufferobj->Size > 0) {
         exec->vtx.buffer_map = (fi_type *)
            ctx->Driver.MapBufferRange(ctx,
                                       exec->vtx.buffer_used,
                                       VBO_VERT_BUFFER_SIZE
                                       - exec->vtx.buffer_used,
                                       accessRange,
                                       exec->vtx.bufferobj,
                                       MAP_INTERNAL);
         exec->vtx.buffer_ptr = exec->vtx.buffer_map;
      }
      else {
         exec->vtx.buffer_ptr = exec->vtx.buffer_map = NULL;
      }
   }

   if (!exec->vtx.buffer_map) {
      /* Need to allocate a new VBO */
      exec->vtx.buffer_used = 0;

      if (ctx->Driver.BufferData(ctx, GL_ARRAY_BUFFER_ARB,
                                 VBO_VERT_BUFFER_SIZE,
                                 NULL, usage,
                                 GL_MAP_WRITE_BIT |
                                 GL_DYNAMIC_STORAGE_BIT |
                                 GL_CLIENT_STORAGE_BIT,
                                 exec->vtx.bufferobj)) {
         /* buffer allocation worked, now map the buffer */
         exec->vtx.buffer_map =
            (fi_type *)ctx->Driver.MapBufferRange(ctx,
                                                  0, VBO_VERT_BUFFER_SIZE,
                                                  accessRange,
                                                  exec->vtx.bufferobj,
                                                  MAP_INTERNAL);
      }
      else {
         _mesa_error(ctx, GL_OUT_OF_MEMORY, "VBO allocation");
         exec->vtx.buffer_map = NULL;
      }
   }

   exec->vtx.buffer_ptr = exec->vtx.buffer_map;

   if (!exec->vtx.buffer_map) {
      /* out of memory */
      _mesa_install_exec_vtxfmt(ctx, &exec->vtxfmt_noop);
   }
   else {
      if (_mesa_using_noop_vtxfmt(ctx->Exec)) {
         /* The no-op functions are installed so switch back to regular
          * functions.  We do this test just to avoid frequent and needless
          * calls to _mesa_install_exec_vtxfmt().
          */
         _mesa_install_exec_vtxfmt(ctx, &exec->vtxfmt);
      }
   }

   if (0)
      printf("map %d..\n", exec->vtx.buffer_used);
}



/**
 * Execute the buffer and save copied verts.
 * \param keep_unmapped  if true, leave the VBO unmapped when we're done.
 */
void
vbo_exec_vtx_flush(struct vbo_exec_context *exec, GLboolean keepUnmapped)
{
   if (0)
      vbo_exec_debug_verts(exec);

   if (exec->vtx.prim_count &&
       exec->vtx.vert_count) {

      exec->vtx.copied.nr = vbo_copy_vertices(exec);

      if (exec->vtx.copied.nr != exec->vtx.vert_count) {
         struct gl_context *ctx = exec->ctx;

         /* Before the update_state() as this may raise _NEW_VARYING_VP_INPUTS
          * from _mesa_set_varying_vp_inputs().
          */
         vbo_exec_bind_arrays(ctx);

         if (ctx->NewState)
            _mesa_update_state(ctx);

         vbo_exec_vtx_unmap(exec);

         assert(ctx->NewState == 0);

         if (0)
            printf("%s %d %d\n", __func__, exec->vtx.prim_count,
                   exec->vtx.vert_count);

         vbo_context(ctx)->draw_prims(ctx,
                                      exec->vtx.prim,
                                      exec->vtx.prim_count,
                                      NULL,
                                      GL_TRUE,
                                      0,
                                      exec->vtx.vert_count - 1,
                                      NULL, 0, NULL);

         /* Get new storage -- unless asked not to. */
         if (!keepUnmapped)
            vbo_exec_vtx_map(exec);
      }
   }

   /* May have to unmap explicitly if we didn't draw:
    */
   if (keepUnmapped && exec->vtx.buffer_map) {
      vbo_exec_vtx_unmap(exec);
   }

   if (keepUnmapped || exec->vtx.vertex_size == 0)
      exec->vtx.max_vert = 0;
   else
      exec->vtx.max_vert = vbo_compute_max_verts(exec);

   exec->vtx.buffer_ptr = exec->vtx.buffer_map;
   exec->vtx.prim_count = 0;
   exec->vtx.vert_count = 0;
}
