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
#include "main/imports.h"
#include "main/mtypes.h"
#include "main/macros.h"
#include "main/light.h"
#include "main/state.h"
#include "main/varray.h"
#include "util/bitscan.h"

#include "vbo_private.h"


/**
 * After playback, copy everything but the position from the
 * last vertex to the saved state
 */
static void
playback_copy_to_current(struct gl_context *ctx,
                         const struct vbo_save_vertex_list *node)
{
   struct vbo_context *vbo = vbo_context(ctx);
   fi_type vertex[VBO_ATTRIB_MAX * 4];
   fi_type *data;
   GLbitfield64 mask;

   if (node->current_size == 0)
      return;

   if (node->current_data) {
      data = node->current_data;
   }
   else {
      /* Position of last vertex */
      const GLuint pos = node->vertex_count > 0 ? node->vertex_count - 1 : 0;
      /* Offset to last vertex in the vertex buffer */
      const GLuint offset = node->buffer_offset
         + pos * node->vertex_size * sizeof(GLfloat);

      data = vertex;

      ctx->Driver.GetBufferSubData(ctx, offset,
                                   node->vertex_size * sizeof(GLfloat),
                                   data, node->vertex_store->bufferobj);

      data += node->attrsz[0]; /* skip vertex position */
   }

   mask = node->enabled & (~BITFIELD64_BIT(VBO_ATTRIB_POS));
   while (mask) {
      const int i = u_bit_scan64(&mask);
      fi_type *current = (fi_type *)vbo->currval[i].Ptr;
      fi_type tmp[4];
      assert(node->attrsz[i]);

      COPY_CLEAN_4V_TYPE_AS_UNION(tmp,
                                  node->attrsz[i],
                                  data,
                                  node->attrtype[i]);

      if (node->attrtype[i] != vbo->currval[i].Type ||
          memcmp(current, tmp, 4 * sizeof(GLfloat)) != 0) {
         memcpy(current, tmp, 4 * sizeof(GLfloat));

         vbo->currval[i].Size = node->attrsz[i];
         vbo->currval[i]._ElementSize = vbo->currval[i].Size * sizeof(GLfloat);
         vbo->currval[i].Type = node->attrtype[i];
         vbo->currval[i].Integer =
            vbo_attrtype_to_integer_flag(node->attrtype[i]);

         if (i >= VBO_ATTRIB_FIRST_MATERIAL &&
             i <= VBO_ATTRIB_LAST_MATERIAL)
            ctx->NewState |= _NEW_LIGHT;

         ctx->NewState |= _NEW_CURRENT_ATTRIB;
      }

      data += node->attrsz[i];
   }

   /* Colormaterial -- this kindof sucks.
    */
   if (ctx->Light.ColorMaterialEnabled) {
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
   const char *buffer =
      ctx->Driver.MapBufferRange(ctx, 0,
                                 list->vertex_store->bufferobj->Size,
                                 GL_MAP_READ_BIT, /* ? */
                                 list->vertex_store->bufferobj,
                                 MAP_INTERNAL);

   unsigned buffer_offset =
      aligned_vertex_buffer_offset(list) ? 0 : list->buffer_offset;

   vbo_loopback_vertex_list(ctx,
                            (const GLfloat *) (buffer + buffer_offset),
                            list->attrsz,
                            list->prims,
                            list->prim_count,
                            list->wrap_count,
                            list->vertex_size);

   ctx->Driver.UnmapBuffer(ctx, list->vertex_store->bufferobj,
                           MAP_INTERNAL);
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

   FLUSH_CURRENT(ctx, 0);

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

      /* Finally update the inputs array */
      _vbo_update_inputs(ctx, &vbo->draw_arrays);
      _mesa_set_drawing_arrays(ctx, vbo->draw_arrays.inputs);

      assert(ctx->NewState == 0);

      if (node->vertex_count > 0) {
         GLuint min_index = node->start_vertex;
         GLuint max_index = min_index + node->vertex_count - 1;
         vbo->draw_prims(ctx,
                         node->prims,
                         node->prim_count,
                         NULL,
                         GL_TRUE,
                         min_index, max_index,
                         NULL, 0, NULL);
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
