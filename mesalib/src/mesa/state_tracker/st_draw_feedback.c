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
#include "st_util.h"

#include "pipe/p_context.h"
#include "pipe/p_defines.h"
#include "util/u_inlines.h"
#include "util/u_draw.h"
#include "util/format/u_format.h"

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
                     GLuint num_instances,
                     GLuint base_instance,
                     struct gl_transform_feedback_object *tfb_vertcount,
                     unsigned stream)
{
   struct st_context *st = st_context(ctx);
   struct pipe_context *pipe = st->pipe;
   struct draw_context *draw = st_get_draw_context(st);
   const struct st_vertex_program *vp;
   struct st_common_variant *vp_variant;
   struct pipe_vertex_buffer vbuffers[PIPE_MAX_SHADER_INPUTS];
   unsigned num_vbuffers = 0;
   struct cso_velems_state velements;
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

   if (ib && !index_bounds_valid)
      vbo_get_minmax_indices(ctx, prims, ib, &min_index, &max_index, nr_prims);

   /* must get these after state validation! */
   struct st_common_variant_key key;
   /* We have to use memcpy to make sure that all bits are copied. */
   memcpy(&key, &st->vp_variant->key, sizeof(key));
   key.is_draw_shader = true;

   vp = (struct st_vertex_program *)st->vp;
   vp_variant = st_get_vp_variant(st, st->vp, &key);

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
   draw_bind_vertex_shader(draw, vp_variant->base.driver_shader);
   set_feedback_vertex_format(ctx);

   /* Must setup these after state validation! */
   /* Setup arrays */
   bool uses_user_vertex_buffers;
   st_setup_arrays(st, vp, vp_variant, &velements, vbuffers, &num_vbuffers,
                   &uses_user_vertex_buffers);
   /* Setup current values as userspace arrays */
   st_setup_current_user(st, vp, vp_variant, &velements, vbuffers, &num_vbuffers);

   /* Map all buffers and tell draw about their mapping */
   for (unsigned buf = 0; buf < num_vbuffers; ++buf) {
      struct pipe_vertex_buffer *vbuffer = &vbuffers[buf];

      if (vbuffer->is_user_buffer) {
         draw_set_mapped_vertex_buffer(draw, buf, vbuffer->buffer.user, ~0);
      } else {
         void *map = pipe_buffer_map(pipe, vbuffer->buffer.resource,
                                     PIPE_MAP_READ, &vb_transfer[buf]);
         draw_set_mapped_vertex_buffer(draw, buf, map,
                                       vbuffer->buffer.resource->width0);
      }
   }

   draw_set_vertex_buffers(draw, 0, num_vbuffers, vbuffers);
   draw_set_vertex_elements(draw, vp->num_inputs, velements.velems);

   unsigned start = 0;

   if (ib) {
      struct gl_buffer_object *bufobj = ib->obj;
      unsigned index_size = 1 << ib->index_size_shift;

      if (index_size == 0)
         goto out_unref_vertex;

      if (bufobj && bufobj->Name) {
         struct st_buffer_object *stobj = st_buffer_object(bufobj);

         start = pointer_to_offset(ib->ptr) >> ib->index_size_shift;
         mapped_indices = pipe_buffer_map(pipe, stobj->buffer,
                                          PIPE_MAP_READ, &ib_transfer);
      }
      else {
         mapped_indices = ib->ptr;
      }

      info.index_size = index_size;
      info.min_index = min_index;
      info.max_index = max_index;
      info.has_user_indices = true;
      info.index.user = mapped_indices;

      draw_set_indexes(draw,
                       (ubyte *) mapped_indices,
                       index_size, ~0);

      if (ctx->Array._PrimitiveRestart) {
         info.primitive_restart = true;
         info.restart_index = ctx->Array._RestartIndex[index_size - 1];
      }
   } else {
      info.index_size = 0;
      info.has_user_indices = false;
   }

   /* set constant buffers */
   draw_set_mapped_constant_buffer(draw, PIPE_SHADER_VERTEX, 0,
                                   st->state.constants[PIPE_SHADER_VERTEX].ptr,
                                   st->state.constants[PIPE_SHADER_VERTEX].size);

   const struct gl_program *prog = &vp->Base.Base;
   struct pipe_transfer *ubo_transfer[PIPE_MAX_CONSTANT_BUFFERS] = {0};
   assert(prog->info.num_ubos <= ARRAY_SIZE(ubo_transfer));

   for (unsigned i = 0; i < prog->sh.NumUniformBlocks; i++) {
      struct gl_buffer_binding *binding =
         &st->ctx->UniformBufferBindings[prog->sh.UniformBlocks[i]->Binding];
      struct st_buffer_object *st_obj = st_buffer_object(binding->BufferObject);
      struct pipe_resource *buf = st_obj->buffer;

      if (!buf)
         continue;

      unsigned offset = binding->Offset;
      unsigned size = buf->width0 - offset;

      /* AutomaticSize is FALSE if the buffer was set with BindBufferRange.
       * Take the minimum just to be sure.
       */
      if (!binding->AutomaticSize)
         size = MIN2(size, (unsigned) binding->Size);

      void *ptr = pipe_buffer_map_range(pipe, buf, offset, size,
                                        PIPE_MAP_READ, &ubo_transfer[i]);

      draw_set_mapped_constant_buffer(draw, PIPE_SHADER_VERTEX, 1 + i, ptr,
                                      size);
   }

   /* shader buffers */
   /* TODO: atomic counter buffers */
   struct pipe_transfer *ssbo_transfer[PIPE_MAX_SHADER_BUFFERS] = {0};

   for (unsigned i = 0; i < prog->info.num_ssbos; i++) {
      struct gl_buffer_binding *binding =
         &st->ctx->ShaderStorageBufferBindings[
            prog->sh.ShaderStorageBlocks[i]->Binding];
      struct st_buffer_object *st_obj = st_buffer_object(binding->BufferObject);
      struct pipe_resource *buf = st_obj->buffer;

      if (!buf)
         continue;

      unsigned offset = binding->Offset;
      unsigned size = buf->width0 - binding->Offset;

      /* AutomaticSize is FALSE if the buffer was set with BindBufferRange.
       * Take the minimum just to be sure.
       */
      if (!binding->AutomaticSize)
         size = MIN2(size, (unsigned) binding->Size);

      void *ptr = pipe_buffer_map_range(pipe, buf, offset, size,
                                        PIPE_MAP_READ, &ssbo_transfer[i]);

      draw_set_mapped_shader_buffer(draw, PIPE_SHADER_VERTEX,
                                    i, ptr, size);
   }

   /* samplers */
   struct pipe_sampler_state *samplers[PIPE_MAX_SAMPLERS];
   for (unsigned i = 0; i < st->state.num_vert_samplers; i++)
      samplers[i] = &st->state.vert_samplers[i];

   draw_set_samplers(draw, PIPE_SHADER_VERTEX, samplers,
                     st->state.num_vert_samplers);

   /* sampler views */
   draw_set_sampler_views(draw, PIPE_SHADER_VERTEX,
                          st->state.vert_sampler_views,
                          st->state.num_sampler_views[PIPE_SHADER_VERTEX]);

   struct pipe_transfer *sv_transfer[PIPE_MAX_SAMPLERS][PIPE_MAX_TEXTURE_LEVELS];

   for (unsigned i = 0; i < st->state.num_sampler_views[PIPE_SHADER_VERTEX]; i++) {
      struct pipe_sampler_view *view = st->state.vert_sampler_views[i];
      if (!view)
         continue;

      struct pipe_resource *res = view->texture;
      unsigned width0 = res->width0;
      unsigned num_layers = res->depth0;
      unsigned first_level = 0;
      unsigned last_level = 0;
      uint32_t row_stride[PIPE_MAX_TEXTURE_LEVELS];
      uint32_t img_stride[PIPE_MAX_TEXTURE_LEVELS];
      uint32_t mip_offset[PIPE_MAX_TEXTURE_LEVELS];
      uintptr_t mip_addr[PIPE_MAX_TEXTURE_LEVELS];
      uintptr_t base_addr;

      if (res->target != PIPE_BUFFER) {
         first_level = view->u.tex.first_level;
         last_level = view->u.tex.last_level;
         num_layers = view->u.tex.last_layer - view->u.tex.first_layer + 1;
         base_addr = UINTPTR_MAX;

         for (unsigned j = first_level; j <= last_level; j++) {
            unsigned map_layers = res->target == PIPE_TEXTURE_3D ?
                                     util_num_layers(res, j) : num_layers;

            sv_transfer[i][j] = NULL;
            mip_addr[j] = (uintptr_t)
                          pipe_transfer_map_3d(pipe, res, j,
                                               PIPE_MAP_READ, 0, 0,
                                               view->u.tex.first_layer,
                                               u_minify(res->width0, j),
                                               u_minify(res->height0, j),
                                               map_layers, &sv_transfer[i][j]);
            row_stride[j] = sv_transfer[i][j]->stride;
            img_stride[j] = sv_transfer[i][j]->layer_stride;

            /* Get the minimum address, because the draw module takes only
             * 1 address for the whole texture + uint32 offsets for mip levels,
             * so we need to convert mapped resource pointers into that scheme.
             */
            base_addr = MIN2(base_addr, mip_addr[j]);
         }
         for (unsigned j = first_level; j <= last_level; j++) {
            /* TODO: The draw module should accept pointers for mipmap levels
             * instead of offsets. This is unlikely to work on 64-bit archs.
             */
            assert(mip_addr[j] - base_addr <= UINT32_MAX);
            mip_offset[j] = mip_addr[j] - base_addr;
         }
      } else {
         width0 = view->u.buf.size / util_format_get_blocksize(view->format);

         /* probably don't really need to fill that out */
         mip_offset[0] = 0;
         row_stride[0] = 0;
         img_stride[0] = 0;

         sv_transfer[i][0] = NULL;
         base_addr = (uintptr_t)
                     pipe_buffer_map_range(pipe, res, view->u.buf.offset,
                                           view->u.buf.size,
                                           PIPE_MAP_READ,
                                           &sv_transfer[i][0]);
      }

      draw_set_mapped_texture(draw, PIPE_SHADER_VERTEX, i, width0,
                              res->height0, num_layers, first_level,
                              last_level, 0, 0, (void*)base_addr, row_stride,
                              img_stride, mip_offset);
   }

   /* shader images */
   struct pipe_image_view images[PIPE_MAX_SHADER_IMAGES];
   struct pipe_transfer *img_transfer[PIPE_MAX_SHADER_IMAGES] = {0};

   for (unsigned i = 0; i < prog->info.num_images; i++) {
      struct pipe_image_view *img = &images[i];

      st_convert_image_from_unit(st, img, prog->sh.ImageUnits[i],
                                 prog->sh.ImageAccess[i]);

      struct pipe_resource *res = img->resource;
      if (!res)
         continue;

      unsigned width, height, num_layers, row_stride, img_stride;
      void *addr;

      if (res->target != PIPE_BUFFER) {
         width = u_minify(res->width0, img->u.tex.level);
         height = u_minify(res->height0, img->u.tex.level);
         num_layers = img->u.tex.last_layer - img->u.tex.first_layer + 1;

         addr = pipe_transfer_map_3d(pipe, res, img->u.tex.level,
                                     PIPE_MAP_READ, 0, 0,
                                     img->u.tex.first_layer,
                                     width, height, num_layers,
                                     &img_transfer[i]);
         row_stride = img_transfer[i]->stride;
         img_stride = img_transfer[i]->layer_stride;
      } else {
         width = img->u.buf.size / util_format_get_blocksize(img->format);

         /* probably don't really need to fill that out */
         row_stride = 0;
         img_stride = 0;
         height = num_layers = 1;

         addr = pipe_buffer_map_range(pipe, res, img->u.buf.offset,
                                      img->u.buf.size, PIPE_MAP_READ,
                                      &img_transfer[i]);
      }

      draw_set_mapped_image(draw, PIPE_SHADER_VERTEX, i, width, height,
                            num_layers, addr, row_stride, img_stride, 0, 0);
   }
   draw_set_images(draw, PIPE_SHADER_VERTEX, images, prog->info.num_images);

   info.start_instance = base_instance;
   info.instance_count = num_instances;

   /* draw here */
   for (i = 0; i < nr_prims; i++) {
      info.count = prims[i].count;

      if (!info.count)
         continue;

      info.mode = prims[i].mode;
      info.start = start + prims[i].start;
      info.index_bias = prims[i].basevertex;
      info.drawid = prims[i].draw_id;
      if (!ib) {
         info.min_index = info.start;
         info.max_index = info.start + info.count - 1;
      }

      draw_vbo(draw, &info);
   }

   /* unmap images */
   for (unsigned i = 0; i < prog->info.num_images; i++) {
      if (img_transfer[i]) {
         draw_set_mapped_image(draw, PIPE_SHADER_VERTEX, i, 0, 0, 0, NULL, 0, 0, 0, 0);
         pipe_transfer_unmap(pipe, img_transfer[i]);
      }
   }

   /* unmap sampler views */
   for (unsigned i = 0; i < st->state.num_sampler_views[PIPE_SHADER_VERTEX]; i++) {
      struct pipe_sampler_view *view = st->state.vert_sampler_views[i];

      if (view) {
         if (view->texture->target != PIPE_BUFFER) {
            for (unsigned j = view->u.tex.first_level;
                 j <= view->u.tex.last_level; j++) {
               pipe_transfer_unmap(pipe, sv_transfer[i][j]);
            }
         } else {
            pipe_transfer_unmap(pipe, sv_transfer[i][0]);
         }
      }
   }

   draw_set_samplers(draw, PIPE_SHADER_VERTEX, NULL, 0);
   draw_set_sampler_views(draw, PIPE_SHADER_VERTEX, NULL, 0);

   for (unsigned i = 0; i < prog->info.num_ssbos; i++) {
      if (ssbo_transfer[i]) {
         draw_set_mapped_constant_buffer(draw, PIPE_SHADER_VERTEX, 1 + i,
                                         NULL, 0);
         pipe_buffer_unmap(pipe, ssbo_transfer[i]);
      }
   }

   for (unsigned i = 0; i < prog->info.num_ubos; i++) {
      if (ubo_transfer[i]) {
         draw_set_mapped_constant_buffer(draw, PIPE_SHADER_VERTEX, 1 + i,
                                         NULL, 0);
         pipe_buffer_unmap(pipe, ubo_transfer[i]);
      }
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
