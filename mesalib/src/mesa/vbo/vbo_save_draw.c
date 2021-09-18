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
   if (!node->cold->current_data)
      return;

   fi_type *data = node->cold->current_data;
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
   if (node->cold->prim_count) {
      const struct _mesa_prim *prim = &node->cold->prims[node->cold->prim_count - 1];
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
   void *buffer = ctx->Driver.MapBufferRange(ctx, 0, bo->Size, GL_MAP_READ_BIT, /* ? */
                                             bo, MAP_INTERNAL);

   /* TODO: in this case, we shouldn't create a bo at all and instead keep
    * the in-RAM buffer. */
   _vbo_loopback_vertex_list(ctx, list, buffer);

   ctx->Driver.UnmapBuffer(ctx, bo, MAP_INTERNAL);
}


void
vbo_save_playback_vertex_list_loopback(struct gl_context *ctx, void *data)
{
   const struct vbo_save_vertex_list *node =
      (const struct vbo_save_vertex_list *) data;

   FLUSH_FOR_DRAW(ctx);

   if (_mesa_inside_begin_end(ctx) && node->cold->prims[0].begin) {
      /* Error: we're about to begin a new primitive but we're already
       * inside a glBegin/End pair.
       */
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "draw operation inside glBegin/End");
      return;
   }
   /* Various degenerate cases: translate into immediate mode
    * calls rather than trying to execute in place.
    */
   loopback_vertex_list(ctx, node);
}

/**
 * Execute the buffer and save copied verts.
 * This is called from the display list code when executing
 * a drawing command.
 */
void
vbo_save_playback_vertex_list(struct gl_context *ctx, void *data, bool copy_to_current)
{
   const struct vbo_save_vertex_list *node =
      (const struct vbo_save_vertex_list *) data;

   FLUSH_FOR_DRAW(ctx);

   if (_mesa_inside_begin_end(ctx) && node->cold->prims[0].begin) {
      /* Error: we're about to begin a new primitive but we're already
       * inside a glBegin/End pair.
       */
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "draw operation inside glBegin/End");
      return;
   }

   bind_vertex_list(ctx, node);

   /* Need that at least one time. */
   if (ctx->NewState)
      _mesa_update_state(ctx);

   /* Return precomputed GL errors such as invalid shaders. */
   if (!ctx->ValidPrimMask) {
      _mesa_error(ctx, ctx->DrawGLError, "glCallList");
      return;
   }

   assert(ctx->NewState == 0);

   struct pipe_draw_info *info = (struct pipe_draw_info *) &node->merged.info;
   void *gl_bo = info->index.gl_bo;
   if (node->merged.mode) {
      ctx->Driver.DrawGalliumMultiMode(ctx, info,
                                       node->merged.start_counts,
                                       node->merged.mode,
                                       node->merged.num_draws);
   } else if (node->merged.num_draws == 1) {
      ctx->Driver.DrawGallium(ctx, info, 0, &node->merged.start_count, 1);
   } else if (node->merged.num_draws) {
      ctx->Driver.DrawGallium(ctx, info, 0, node->merged.start_counts,
                              node->merged.num_draws);
   }
   info->index.gl_bo = gl_bo;

   if (copy_to_current)
      playback_copy_to_current(ctx, node);
}
