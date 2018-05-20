/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 1999-2005  Brian Paul   All Rights Reserved.
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

#include "c99_alloca.h"
#include "main/errors.h"
#include "main/bufferobj.h"
#include "math/m_eval.h"
#include "main/vtxfmt.h"
#include "main/api_arrayelt.h"
#include "main/arrayobj.h"
#include "main/varray.h"
#include "vbo.h"
#include "vbo_private.h"


static GLuint
check_size(const GLfloat *attr)
{
   if (attr[3] != 1.0F)
      return 4;
   if (attr[2] != 0.0F)
      return 3;
   if (attr[1] != 0.0F)
      return 2;
   return 1;
}


/**
 * Helper for initializing a vertex array.
 */
static void
init_array(struct gl_context *ctx, struct gl_array_attributes *attrib,
           unsigned size, const void *pointer)
{
   memset(attrib, 0, sizeof(*attrib));

   attrib->Size = size;
   attrib->Type = GL_FLOAT;
   attrib->Format = GL_RGBA;
   attrib->Stride = 0;
   attrib->_ElementSize = size * sizeof(GLfloat);
   attrib->Ptr = pointer;
}


/**
 * Set up the vbo->currval arrays to point at the context's current
 * vertex attributes (with strides = 0).
 */
static void
init_legacy_currval(struct gl_context *ctx)
{
   struct vbo_context *vbo = vbo_context(ctx);
   GLuint i;

   /* Set up a constant (Stride == 0) array for each current
    * attribute:
    */
   for (i = 0; i < VERT_ATTRIB_FF_MAX; i++) {
      const unsigned attr = VERT_ATTRIB_FF(i);
      struct gl_array_attributes *attrib = &vbo->current[attr];

      init_array(ctx, attrib, check_size(ctx->Current.Attrib[attr]),
                 ctx->Current.Attrib[attr]);
   }
}


static void
init_generic_currval(struct gl_context *ctx)
{
   struct vbo_context *vbo = vbo_context(ctx);
   GLuint i;

   for (i = 0; i < VERT_ATTRIB_GENERIC_MAX; i++) {
      const unsigned attr = VBO_ATTRIB_GENERIC0 + i;
      struct gl_array_attributes *attrib = &vbo->current[attr];

      init_array(ctx, attrib, 1, ctx->Current.Attrib[attr]);
   }
}


static void
init_mat_currval(struct gl_context *ctx)
{
   struct vbo_context *vbo = vbo_context(ctx);
   GLuint i;

   /* Set up a constant (StrideB == 0) array for each current
    * attribute:
    */
   for (i = 0; i < MAT_ATTRIB_MAX; i++) {
      const unsigned attr = VBO_ATTRIB_MAT_FRONT_AMBIENT + i;
      struct gl_array_attributes *attrib = &vbo->current[attr];
      unsigned size;

      /* Size is fixed for the material attributes, for others will
       * be determined at runtime:
       */
      switch (i) {
      case MAT_ATTRIB_FRONT_SHININESS:
      case MAT_ATTRIB_BACK_SHININESS:
         size = 1;
         break;
      case MAT_ATTRIB_FRONT_INDEXES:
      case MAT_ATTRIB_BACK_INDEXES:
         size = 3;
         break;
      default:
         size = 4;
         break;
      }

      init_array(ctx, attrib, size, ctx->Light.Material.Attrib[i]);
   }
}


void
_vbo_install_exec_vtxfmt(struct gl_context *ctx)
{
   struct vbo_context *vbo = vbo_context(ctx);

   _mesa_install_exec_vtxfmt(ctx, &vbo->exec.vtxfmt);
}


void
vbo_exec_invalidate_state(struct gl_context *ctx)
{
   struct vbo_context *vbo = vbo_context(ctx);
   struct vbo_exec_context *exec = &vbo->exec;

   if (ctx->NewState & _NEW_ARRAY) {
      _ae_invalidate_state(ctx);
   }
   if (ctx->NewState & _NEW_EVAL)
      exec->eval.recalculate_maps = GL_TRUE;
}


GLboolean
_vbo_CreateContext(struct gl_context *ctx)
{
   struct vbo_context *vbo = CALLOC_STRUCT(vbo_context);

   ctx->vbo_context = vbo;

   /* Initialize the arrayelt helper
    */
   if (!ctx->aelt_context &&
       !_ae_create_context(ctx)) {
      return GL_FALSE;
   }

   vbo->binding.Offset = 0;
   vbo->binding.Stride = 0;
   vbo->binding.InstanceDivisor = 0;
   _mesa_reference_buffer_object(ctx, &vbo->binding.BufferObj,
                                 ctx->Shared->NullBufferObj);
   init_legacy_currval(ctx);
   init_generic_currval(ctx);
   init_mat_currval(ctx);

   /* make sure all VBO_ATTRIB_ values can fit in an unsigned byte */
   STATIC_ASSERT(VBO_ATTRIB_MAX <= 255);

   /* Hook our functions into exec and compile dispatch tables.  These
    * will pretty much be permanently installed, which means that the
    * vtxfmt mechanism can be removed now.
    */
   vbo_exec_init(ctx);
   if (ctx->API == API_OPENGL_COMPAT)
      vbo_save_init(ctx);

   vbo->VAO = _mesa_new_vao(ctx, ~((GLuint)0));
   /* The exec VAO assumes to have all arributes bound to binding 0 */
   for (unsigned i = 0; i < VERT_ATTRIB_MAX; ++i)
      _mesa_vertex_attrib_binding(ctx, vbo->VAO, i, 0);

   _math_init_eval();

   return GL_TRUE;
}


void
_vbo_DestroyContext(struct gl_context *ctx)
{
   struct vbo_context *vbo = vbo_context(ctx);

   if (ctx->aelt_context) {
      _ae_destroy_context(ctx);
      ctx->aelt_context = NULL;
   }

   if (vbo) {

      _mesa_reference_buffer_object(ctx, &vbo->binding.BufferObj, NULL);

      vbo_exec_destroy(ctx);
      if (ctx->API == API_OPENGL_COMPAT)
         vbo_save_destroy(ctx);
      _mesa_reference_vao(ctx, &vbo->VAO, NULL);
      free(vbo);
      ctx->vbo_context = NULL;
   }
}


const struct gl_array_attributes *
_vbo_current_attrib(const struct gl_context *ctx, gl_vert_attrib attr)
{
   const struct vbo_context *vbo = vbo_context_const(ctx);
   const gl_vertex_processing_mode vmp = ctx->VertexProgram._VPMode;
   return &vbo->current[_vbo_attribute_alias_map[vmp][attr]];
}


const struct gl_vertex_buffer_binding *
_vbo_current_binding(const struct gl_context *ctx)
{
   const struct vbo_context *vbo = vbo_context_const(ctx);
   return &vbo->binding;
}


/*
 * Helper function for _vbo_draw_indirect below that additionally takes a zero
 * initialized array of _mesa_prim scratch space memory as the last argument.
 */
static void
draw_indirect(struct gl_context *ctx, GLuint mode,
              struct gl_buffer_object *indirect_data,
              GLsizeiptr indirect_offset, unsigned draw_count,
              unsigned stride,
              struct gl_buffer_object *indirect_draw_count_buffer,
              GLsizeiptr indirect_draw_count_offset,
              const struct _mesa_index_buffer *ib,
              struct _mesa_prim *prim)
{
   prim[0].begin = 1;
   prim[draw_count - 1].end = 1;
   for (unsigned i = 0; i < draw_count; ++i, indirect_offset += stride) {
      prim[i].mode = mode;
      prim[i].indexed = !!ib;
      prim[i].indirect_offset = indirect_offset;
      prim[i].is_indirect = 1;
      prim[i].draw_id = i;
   }

   /* This should always be true at this time */
   assert(indirect_data == ctx->DrawIndirectBuffer);

   ctx->Driver.Draw(ctx, prim, draw_count, ib, false, 0u, ~0u,
                    NULL, 0, indirect_data);
}


/*
 * Function to be put into dd_function_table::DrawIndirect as fallback.
 * Calls into dd_function_table::Draw past adapting call arguments.
 * See dd_function_table::DrawIndirect for call argument documentation.
 */
void
_vbo_draw_indirect(struct gl_context *ctx, GLuint mode,
                   struct gl_buffer_object *indirect_data,
                   GLsizeiptr indirect_offset, unsigned draw_count,
                   unsigned stride,
                   struct gl_buffer_object *indirect_draw_count_buffer,
                   GLsizeiptr indirect_draw_count_offset,
                   const struct _mesa_index_buffer *ib)
{
   /* Use alloca for the prim space if we are somehow in bounds. */
   if (draw_count*sizeof(struct _mesa_prim) < 1024) {
      struct _mesa_prim *space = alloca(draw_count*sizeof(struct _mesa_prim));
      memset(space, 0, draw_count*sizeof(struct _mesa_prim));

      draw_indirect(ctx, mode, indirect_data, indirect_offset, draw_count,
                    stride, indirect_draw_count_buffer,
                    indirect_draw_count_offset, ib, space);
   } else {
      struct _mesa_prim *space = calloc(draw_count, sizeof(struct _mesa_prim));
      if (space == NULL) {
         _mesa_error(ctx, GL_OUT_OF_MEMORY, "gl%sDraw%sIndirect%s",
                     (draw_count > 1) ? "Multi" : "",
                     ib ? "Elements" : "Arrays",
                     indirect_data ? "CountARB" : "");
         return;
      }

      draw_indirect(ctx, mode, indirect_data, indirect_offset, draw_count,
                    stride, indirect_draw_count_buffer,
                    indirect_draw_count_offset, ib, space);

      free(space);
   }
}
