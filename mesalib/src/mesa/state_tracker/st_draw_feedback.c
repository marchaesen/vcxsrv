/**************************************************************************
 * 
 * Copyright 2007 VMware, Inc.
 * All Rights Reserved.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 * 
 **************************************************************************/

#include "main/imports.h"
#include "main/arrayobj.h"
#include "main/image.h"
#include "main/macros.h"
#include "main/varray.h"

#include "vbo/vbo.h"

#include "st_context.h"
#include "st_atom.h"
#include "st_cb_bitmap.h"
#include "st_cb_bufferobjects.h"
#include "st_draw.h"
#include "st_program.h"

#include "pipe/p_context.h"
#include "pipe/p_defines.h"
#include "util/u_inlines.h"
#include "util/u_draw.h"

#include "draw/draw_private.h"
#include "draw/draw_context.h"


/**
 * Set the (private) draw module's post-transformed vertex format when in
 * GL_SELECT or GL_FEEDBACK mode or for glRasterPos.
 */
static void
set_feedback_vertex_format(struct gl_context *ctx)
{
#if 0
   struct st_context *st = st_context(ctx);
   struct vertex_info vinfo;
   GLuint i;

   memset(&vinfo, 0, sizeof(vinfo));

   if (ctx->RenderMode == GL_SELECT) {
      assert(ctx->RenderMode == GL_SELECT);
      vinfo.num_attribs = 1;
      vinfo.format[0] = FORMAT_4F;
      vinfo.interp_mode[0] = INTERP_LINEAR;
   }
   else {
      /* GL_FEEDBACK, or glRasterPos */
      /* emit all attribs (pos, color, texcoord) as GLfloat[4] */
      vinfo.num_attribs = st->state.vs->cso->state.num_outputs;
      for (i = 0; i < vinfo.num_attribs; i++) {
         vinfo.format[i] = FORMAT_4F;
         vinfo.interp_mode[i] = INTERP_LINEAR;
      }
   }

   draw_set_vertex_info(st->draw, &vinfo);
#endif
}


/**
 * Called by VBO to draw arrays when in selection or feedback mode and
 * to implement glRasterPos.
 * This function mirrors the normal st_draw_vbo().
 * Look at code refactoring some day.
 */
void
st_feedback_draw_vbo(struct gl_context *ctx,
                     const struct _mesa_prim *prims,
                     GLuint nr_prims,
                     const struct _mesa_index_buffer *ib,
		     GLboolean index_bounds_valid,
                     GLuint min_index,
                     GLuint max_index,
                     struct gl_transform_feedback_object *tfb_vertcount,
                     unsigned stream,
                     struct gl_buffer_object *indirect)
{
   struct st_context *st = st_context(ctx);
   struct pipe_context *pipe = st->pipe;
   struct draw_context *draw = st_get_draw_context(st);
   const struct st_vertex_program *vp;
   struct st_vp_variant *vp_variant;
   const struct pipe_shader_state *vs;
   struct pipe_vertex_buffer vbuffers[PIPE_MAX_SHADER_INPUTS];
   unsigned num_vbuffers = 0;
   struct pipe_vertex_element velements[PIPE_MAX_ATTRIBS];
   struct pipe_transfer *vb_transfer[PIPE_MAX_ATTRIBS] = {NULL};
   struct pipe_transfer *ib_transfer = NULL;
   GLuint i;
   const void *mapped_indices = NULL;
   struct pipe_draw_info info;

   if (!draw)
      return;

   /* Initialize pipe_draw_info. */
   info.primitive_restart = false;
   info.vertices_per_patch = ctx->TessCtrlProgram.patch_vertices;
   info.indirect = NULL;
   info.count_from_stream_output = NULL;
   info.restart_index = 0;

   st_flush_bitmap_cache(st);
   st_invalidate_readpix_cache(st);

   st_validate_state(st, ST_PIPELINE_RENDER);

   if (!index_bounds_valid)
      vbo_get_minmax_indices(ctx, prims, ib, &min_index, &max_index, nr_prims);

   /* must get these after state validation! */
   vp = st->vp;
   vp_variant = st->vp_variant;
   vs = &vp_variant->tgsi;

   if (!vp_variant->draw_shader) {
      vp_variant->draw_shader = draw_create_vertex_shader(draw, vs);
   }

   /*
    * Set up the draw module's state.
    *
    * We'd like to do this less frequently, but the normal state-update
    * code sends state updates to the pipe, not to our private draw module.
    */
   assert(draw);
   draw_set_viewport_states(draw, 0, 1, &st->state.viewport[0]);
   draw_set_clip_state(draw, &st->state.clip);
   draw_set_rasterizer_state(draw, &st->state.rasterizer, NULL);
   draw_bind_vertex_shader(draw, vp_variant->draw_shader);
   set_feedback_vertex_format(ctx);

   /* Must setup these after state validation! */
   /* Setup arrays */
   st_setup_arrays(st, vp, vp_variant, velements, vbuffers, &num_vbuffers);
   /* Setup current values as userspace arrays */
   st_setup_current_user(st, vp, vp_variant, velements, vbuffers, &num_vbuffers);

   /* Map all buffers and tell draw about their mapping */
   for (unsigned buf = 0; buf < num_vbuffers; ++buf) {
      struct pipe_vertex_buffer *vbuffer = &vbuffers[buf];

      if (vbuffer->is_user_buffer) {
         draw_set_mapped_vertex_buffer(draw, buf, vbuffer->buffer.user, ~0);
      } else {
         void *map = pipe_buffer_map(pipe, vbuffer->buffer.resource,
                                     PIPE_TRANSFER_READ, &vb_transfer[buf]);
         draw_set_mapped_vertex_buffer(draw, buf, map,
                                       vbuffer->buffer.resource->width0);
      }
   }

   draw_set_vertex_buffers(draw, 0, num_vbuffers, vbuffers);
   draw_set_vertex_elements(draw, vp->num_inputs, velements);

   unsigned start = 0;

   if (ib) {
      struct gl_buffer_object *bufobj = ib->obj;
      unsigned index_size = ib->index_size;

      if (index_size == 0)
         goto out_unref_vertex;

      if (bufobj && bufobj->Name) {
         struct st_buffer_object *stobj = st_buffer_object(bufobj);

         start = pointer_to_offset(ib->ptr) / index_size;
         mapped_indices = pipe_buffer_map(pipe, stobj->buffer,
                                          PIPE_TRANSFER_READ, &ib_transfer);
      }
      else {
         mapped_indices = ib->ptr;
      }

      info.index_size = ib->index_size;
      info.min_index = min_index;
      info.max_index = max_index;
      info.has_user_indices = true;
      info.index.user = mapped_indices;

      draw_set_indexes(draw,
                       (ubyte *) mapped_indices,
                       index_size, ~0);

      if (ctx->Array._PrimitiveRestart) {
         info.primitive_restart = true;
         info.restart_index = _mesa_primitive_restart_index(ctx, info.index_size);
      }
   } else {
      info.index_size = 0;
      info.has_user_indices = false;
   }

   /* set the constant buffer */
   draw_set_mapped_constant_buffer(st->draw, PIPE_SHADER_VERTEX, 0,
                                   st->state.constants[PIPE_SHADER_VERTEX].ptr,
                                   st->state.constants[PIPE_SHADER_VERTEX].size);


   /* draw here */
   for (i = 0; i < nr_prims; i++) {
      info.count = prims[i].count;

      if (!info.count)
         continue;

      info.mode = prims[i].mode;
      info.start = start + prims[i].start;
      info.start_instance = prims[i].base_instance;
      info.instance_count = prims[i].num_instances;
      info.index_bias = prims[i].basevertex;
      info.drawid = prims[i].draw_id;
      if (!ib) {
         info.min_index = info.start;
         info.max_index = info.start + info.count - 1;
      }

      draw_vbo(draw, &info);
   }


   /*
    * unmap vertex/index buffers
    */
   if (ib) {
      draw_set_indexes(draw, NULL, 0, 0);
      if (ib_transfer)
         pipe_buffer_unmap(pipe, ib_transfer);
   }

 out_unref_vertex:
   for (unsigned buf = 0; buf < num_vbuffers; ++buf) {
      if (vb_transfer[buf])
         pipe_buffer_unmap(pipe, vb_transfer[buf]);
      draw_set_mapped_vertex_buffer(draw, buf, NULL, 0);
   }
   draw_set_vertex_buffers(draw, 0, num_vbuffers, NULL);
}
