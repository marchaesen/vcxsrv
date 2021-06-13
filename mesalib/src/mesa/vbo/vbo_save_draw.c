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
 */

/* Author:
 *    Keith Whitwell <keithw@vmware.com>
 */

#include <stdbool.h>
#include "main/arrayobj.h"
#include "main/glheader.h"
#include "main/bufferobj.h"
#include "main/context.h"
#include "main/enable.h"
#include "main/mesa_private.h"
#include "main/macros.h"
#include "main/light.h"
#include "main/state.h"
#include "main/varray.h"
#include "util/bitscan.h"

#include "vbo_private.h"


static void
copy_vao(struct gl_context *ctx, const struct gl_vertex_array_object *vao,
         GLbitfield mask, GLbitfield state, GLbitfield pop_state,
         int shift, fi_type **data, bool *color0_changed)
{
   struct vbo_context *vbo = vbo_context(ctx);

   mask &= vao->Enabled;
   while (mask) {
      const int i = u_bit_scan(&mask);
      const struct gl_array_attributes *attrib = &vao->VertexAttrib[i];
      unsigned current_index = shift + i;
      struct gl_array_attributes *currval = &vbo->current[current_index];
      const GLubyte size = attrib->Format.Size;
      const GLenum16 type = attrib->Format.Type;
      fi_type tmp[8];
      int dmul_shift = 0;

      if (type == GL_DOUBLE ||
          type == GL_UNSIGNED_INT64_ARB) {
         dmul_shift = 1;
         memcpy(tmp, *data, size * 2 * sizeof(GLfloat));
      } else {
         COPY_CLEAN_4V_TYPE_AS_UNION(tmp, size, *data, type);
      }

      if (memcmp(currval->Ptr, tmp, 4 * sizeof(GLfloat) << dmul_shift) != 0) {
         memcpy((fi_type*)currval->Ptr, tmp, 4 * sizeof(GLfloat) << dmul_shift);

         if (current_index == VBO_ATTRIB_COLOR0)
            *color0_changed = true;

         /* The fixed-func vertex program uses this. */
         if (current_index == VBO_ATTRIB_MAT_FRONT_SHININESS ||
             current_index == VBO_ATTRIB_MAT_BACK_SHININESS)
            ctx->NewState |= _NEW_FF_VERT_PROGRAM;

         ctx->NewState |= state;
         ctx->PopAttribState |= pop_state;
      }

      if (type != currval->Format.Type ||
          (size >> dmul_shift) != currval->Format.Size)
         vbo_set_vertex_format(&currval->Format, size >> dmul_shift, type);

      *data += size;
   }
}

/**
 * After playback, copy everything but the position from the
 * last vertex to the saved state
 */
static void
playback_copy_to_current(struct gl_context *ctx,
                         const struct vbo_save_vertex_list *node)
{
   if (!node->current_data)
      return;

   fi_type *data = node->current_data;
   bool color0_changed = false;

   /* Copy conventional attribs and generics except pos */
   copy_vao(ctx, node->VAO[VP_MODE_SHADER], ~VERT_BIT_POS & VERT_BIT_ALL,
            _NEW_CURRENT_ATTRIB, GL_CURRENT_BIT, 0, &data, &color0_changed);
   /* Copy materials */
   copy_vao(ctx, node->VAO[VP_MODE_FF], VERT_BIT_MAT_ALL,
            _NEW_MATERIAL, GL_LIGHTING_BIT,
            VBO_MATERIAL_SHIFT, &data, &color0_changed);

   if (color0_changed && ctx->Light.ColorMaterialEnabled) {
      _mesa_update_color_material(ctx, ctx->Current.Attrib[VBO_ATTRIB_COLOR0]);
   }

   /* CurrentExecPrimitive
    */
   if (node->prim_count) {
      const struct _mesa_prim *prim = &node->prims[node->prim_count - 1];
      if (prim->end)
         ctx->Driver.CurrentExecPrimitive = PRIM_OUTSIDE_BEGIN_END;
      else
         ctx->Driver.CurrentExecPrimitive = prim->mode;
   }
}



/**
 * Set the appropriate VAO to draw.
 */
static void
bind_vertex_list(struct gl_context *ctx,
                 const struct vbo_save_vertex_list *node)
{
   const gl_vertex_processing_mode mode = ctx->VertexProgram._VPMode;
   _mesa_set_draw_vao(ctx, node->VAO[mode], _vbo_get_vao_filter(mode));
}


static void
loopback_vertex_list(struct gl_context *ctx,
                     const struct vbo_save_vertex_list *list)
{
   struct gl_buffer_object *bo = list->VAO[0]->BufferBinding[0].BufferObj;
   ctx->Driver.MapBufferRange(ctx, 0, bo->Size, GL_MAP_READ_BIT, /* ? */
                              bo, MAP_INTERNAL);

   /* Note that the range of referenced vertices must be mapped already */
   _vbo_loopback_vertex_list(ctx, list);

   ctx->Driver.UnmapBuffer(ctx, bo, MAP_INTERNAL);
}


/**
 * Execute the buffer and save copied verts.
 * This is called from the display list code when executing
 * a drawing command.
 */
void
vbo_save_playback_vertex_list(struct gl_context *ctx, void *data)
{
   const struct vbo_save_vertex_list *node =
      (const struct vbo_save_vertex_list *) data;
   struct vbo_context *vbo = vbo_context(ctx);
   struct vbo_save_context *save = &vbo->save;
   GLboolean remap_vertex_store = GL_FALSE;

   if (save->vertex_store && save->vertex_store->buffer_map) {
      /* The vertex store is currently mapped but we're about to replay
       * a display list.  This can happen when a nested display list is
       * being build with GL_COMPILE_AND_EXECUTE.
       * We never want to have mapped vertex buffers when we're drawing.
       * Unmap the vertex store, execute the list, then remap the vertex
       * store.
       */
      vbo_save_unmap_vertex_store(ctx, save->vertex_store);
      remap_vertex_store = GL_TRUE;
   }

   FLUSH_FOR_DRAW(ctx);

   if (node->prim_count > 0) {

      if (_mesa_inside_begin_end(ctx) && node->prims[0].begin) {
         /* Error: we're about to begin a new primitive but we're already
          * inside a glBegin/End pair.
          */
         _mesa_error(ctx, GL_INVALID_OPERATION,
                     "draw operation inside glBegin/End");
         goto end;
      }
      else if (save->replay_flags) {
         /* Various degenerate cases: translate into immediate mode
          * calls rather than trying to execute in place.
          */
         loopback_vertex_list(ctx, node);

         goto end;
      }

      bind_vertex_list(ctx, node);

      /* Need that at least one time. */
      if (ctx->NewState)
         _mesa_update_state(ctx);

      /* XXX also need to check if shader enabled, but invalid */
      if ((ctx->VertexProgram.Enabled &&
           !_mesa_arb_vertex_program_enabled(ctx)) ||
          (ctx->FragmentProgram.Enabled &&
           !_mesa_arb_fragment_program_enabled(ctx))) {
         _mesa_error(ctx, GL_INVALID_OPERATION,
                     "glBegin (invalid vertex/fragment program)");
         return;
      }

      assert(ctx->NewState == 0);

      if (node->vertex_count > 0) {
         bool draw_using_merged_prim = (ctx->Const.AllowIncorrectPrimitiveId ||
                                        ctx->_PrimitiveIDIsUnused) &&
                                       node->merged.prims;
         if (!draw_using_merged_prim) {
            ctx->Driver.Draw(ctx, node->prims, node->prim_count,
                             NULL, true,
                             false, 0, node->min_index, node->max_index, 1, 0);
         } else {
            ctx->Driver.Draw(ctx, node->merged.prims, node->merged.prim_count,
                             &node->merged.ib, true,
                             false, 0, node->merged.min_index, node->merged.max_index, 1, 0);
         }
      }
   }

   /* Copy to current?
    */
   playback_copy_to_current(ctx, node);

end:
   if (remap_vertex_store) {
      save->buffer_ptr = vbo_save_map_vertex_store(ctx, save->vertex_store);
   }
}
