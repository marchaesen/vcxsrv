/*
 * Copyright 2007 VMware, Inc.
 * Copyright 2016 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/**
 * \file
 *
 * Common helper functions for PBO up- and downloads.
 */

#include "state_tracker/st_context.h"
#include "state_tracker/st_pbo.h"
#include "state_tracker/st_cb_bufferobjects.h"

#include "pipe/p_context.h"
#include "pipe/p_defines.h"
#include "pipe/p_screen.h"
#include "cso_cache/cso_context.h"
#include "tgsi/tgsi_ureg.h"
#include "util/u_inlines.h"
#include "util/u_upload_mgr.h"

/* Final setup of buffer addressing information.
 *
 * buf_offset is in pixels.
 *
 * Returns false if something (e.g. alignment) prevents PBO upload/download.
 */
bool
st_pbo_addresses_setup(struct st_context *st,
                       struct pipe_resource *buf, intptr_t buf_offset,
                       struct st_pbo_addresses *addr)
{
   unsigned skip_pixels;

   /* Check alignment against texture buffer requirements. */
   {
      unsigned ofs = (buf_offset * addr->bytes_per_pixel) % st->ctx->Const.TextureBufferOffsetAlignment;
      if (ofs != 0) {
         if (ofs % addr->bytes_per_pixel != 0)
            return false;

         skip_pixels = ofs / addr->bytes_per_pixel;
         buf_offset -= skip_pixels;
      } else {
         skip_pixels = 0;
      }
   }

   assert(buf_offset >= 0);

   addr->buffer = buf;
   addr->first_element = buf_offset;
   addr->last_element = buf_offset + skip_pixels + addr->width - 1
         + (addr->height - 1 + (addr->depth - 1) * addr->image_height) * addr->pixels_per_row;

   if (addr->last_element - addr->first_element > st->ctx->Const.MaxTextureBufferSize - 1)
      return false;

   /* This should be ensured by Mesa before calling our callbacks */
   assert((addr->last_element + 1) * addr->bytes_per_pixel <= buf->width0);

   addr->constants.xoffset = -addr->xoffset + skip_pixels;
   addr->constants.yoffset = -addr->yoffset;
   addr->constants.stride = addr->pixels_per_row;
   addr->constants.image_size = addr->pixels_per_row * addr->image_height;
   addr->constants.layer_offset = 0;

   return true;
}

/* Validate and fill buffer addressing information based on GL pixelstore
 * attributes.
 *
 * Returns false if some aspect of the addressing (e.g. alignment) prevents
 * PBO upload/download.
 */
bool
st_pbo_addresses_pixelstore(struct st_context *st,
                            GLenum gl_target, bool skip_images,
                            const struct gl_pixelstore_attrib *store,
                            const void *pixels,
                            struct st_pbo_addresses *addr)
{
   struct pipe_resource *buf = st_buffer_object(store->BufferObj)->buffer;
   intptr_t buf_offset = (intptr_t) pixels;

   if (buf_offset % addr->bytes_per_pixel)
      return false;

   /* Convert to texels */
   buf_offset = buf_offset / addr->bytes_per_pixel;

   /* Determine image height */
   if (gl_target == GL_TEXTURE_1D_ARRAY) {
      addr->image_height = 1;
   } else {
      addr->image_height = store->ImageHeight > 0 ? store->ImageHeight : addr->height;
   }

   /* Compute the stride, taking store->Alignment into account */
   {
       unsigned pixels_per_row = store->RowLength > 0 ?
                           store->RowLength : addr->width;
       unsigned bytes_per_row = pixels_per_row * addr->bytes_per_pixel;
       unsigned remainder = bytes_per_row % store->Alignment;
       unsigned offset_rows;

       if (remainder > 0)
          bytes_per_row += store->Alignment - remainder;

       if (bytes_per_row % addr->bytes_per_pixel)
          return false;

       addr->pixels_per_row = bytes_per_row / addr->bytes_per_pixel;

       offset_rows = store->SkipRows;
       if (skip_images)
          offset_rows += addr->image_height * store->SkipImages;

       buf_offset += store->SkipPixels + addr->pixels_per_row * offset_rows;
   }

   if (!st_pbo_addresses_setup(st, buf, buf_offset, addr))
      return false;

   /* Support GL_PACK_INVERT_MESA */
   if (store->Invert) {
      addr->constants.xoffset += (addr->height - 1) * addr->constants.stride;
      addr->constants.stride = -addr->constants.stride;
   }

   return true;
}

/* For download from a framebuffer, we may have to invert the Y axis. The
 * setup is as follows:
 * - set viewport to inverted, so that the position sysval is correct for
 *   texel fetches
 * - this function adjusts the fragment shader's constant buffer to compute
 *   the correct destination addresses.
 */
void
st_pbo_addresses_invert_y(struct st_pbo_addresses *addr,
                          unsigned viewport_height)
{
   addr->constants.xoffset +=
      (viewport_height - 1 + 2 * addr->constants.yoffset) * addr->constants.stride;
   addr->constants.stride = -addr->constants.stride;
}

/* Setup all vertex pipeline state, rasterizer state, and fragment shader
 * constants, and issue the draw call for PBO upload/download.
 *
 * The caller is responsible for saving and restoring state, as well as for
 * setting other fragment shader state (fragment shader, samplers), and
 * framebuffer/viewport/DSA/blend state.
 */
bool
st_pbo_draw(struct st_context *st, const struct st_pbo_addresses *addr,
            unsigned surface_width, unsigned surface_height)
{
   struct cso_context *cso = st->cso_context;

   /* Setup vertex and geometry shaders */
   if (!st->pbo.vs) {
      st->pbo.vs = st_pbo_create_vs(st);
      if (!st->pbo.vs)
         return false;
   }

   if (addr->depth != 1 && st->pbo.use_gs && !st->pbo.gs) {
      st->pbo.gs = st_pbo_create_gs(st);
      if (!st->pbo.gs)
         return false;
   }

   cso_set_vertex_shader_handle(cso, st->pbo.vs);

   cso_set_geometry_shader_handle(cso, addr->depth != 1 ? st->pbo.gs : NULL);

   cso_set_tessctrl_shader_handle(cso, NULL);

   cso_set_tesseval_shader_handle(cso, NULL);

   /* Upload vertices */
   {
      struct pipe_vertex_buffer vbo;
      struct pipe_vertex_element velem;

      float x0 = (float) addr->xoffset / surface_width * 2.0f - 1.0f;
      float y0 = (float) addr->yoffset / surface_height * 2.0f - 1.0f;
      float x1 = (float) (addr->xoffset + addr->width) / surface_width * 2.0f - 1.0f;
      float y1 = (float) (addr->yoffset + addr->height) / surface_height * 2.0f - 1.0f;

      float *verts = NULL;

      vbo.user_buffer = NULL;
      vbo.buffer = NULL;
      vbo.stride = 2 * sizeof(float);

      u_upload_alloc(st->uploader, 0, 8 * sizeof(float), 4,
                     &vbo.buffer_offset, &vbo.buffer, (void **) &verts);
      if (!verts)
         return false;

      verts[0] = x0;
      verts[1] = y0;
      verts[2] = x0;
      verts[3] = y1;
      verts[4] = x1;
      verts[5] = y0;
      verts[6] = x1;
      verts[7] = y1;

      u_upload_unmap(st->uploader);

      velem.src_offset = 0;
      velem.instance_divisor = 0;
      velem.vertex_buffer_index = cso_get_aux_vertex_buffer_slot(cso);
      velem.src_format = PIPE_FORMAT_R32G32_FLOAT;

      cso_set_vertex_elements(cso, 1, &velem);

      cso_set_vertex_buffers(cso, velem.vertex_buffer_index, 1, &vbo);

      pipe_resource_reference(&vbo.buffer, NULL);
   }

   /* Upload constants */
   {
      struct pipe_constant_buffer cb;

      if (st->constbuf_uploader) {
         cb.buffer = NULL;
         cb.user_buffer = NULL;
         u_upload_data(st->constbuf_uploader, 0, sizeof(addr->constants),
                       st->ctx->Const.UniformBufferOffsetAlignment,
                       &addr->constants, &cb.buffer_offset, &cb.buffer);
         if (!cb.buffer)
            return false;

         u_upload_unmap(st->constbuf_uploader);
      } else {
         cb.buffer = NULL;
         cb.user_buffer = &addr->constants;
         cb.buffer_offset = 0;
      }
      cb.buffer_size = sizeof(addr->constants);

      cso_set_constant_buffer(cso, PIPE_SHADER_FRAGMENT, 0, &cb);

      pipe_resource_reference(&cb.buffer, NULL);
   }

   /* Rasterizer state */
   cso_set_rasterizer(cso, &st->pbo.raster);

   /* Disable stream output */
   cso_set_stream_outputs(cso, 0, NULL, 0);

   if (addr->depth == 1) {
      cso_draw_arrays(cso, PIPE_PRIM_TRIANGLE_STRIP, 0, 4);
   } else {
      cso_draw_arrays_instanced(cso, PIPE_PRIM_TRIANGLE_STRIP,
                                0, 4, 0, addr->depth);
   }

   return true;
}

void *
st_pbo_create_vs(struct st_context *st)
{
   struct ureg_program *ureg;
   struct ureg_src in_pos;
   struct ureg_src in_instanceid;
   struct ureg_dst out_pos;
   struct ureg_dst out_layer;

   ureg = ureg_create(PIPE_SHADER_VERTEX);
   if (!ureg)
      return NULL;

   in_pos = ureg_DECL_vs_input(ureg, TGSI_SEMANTIC_POSITION);

   out_pos = ureg_DECL_output(ureg, TGSI_SEMANTIC_POSITION, 0);

   if (st->pbo.layers) {
      in_instanceid = ureg_DECL_system_value(ureg, TGSI_SEMANTIC_INSTANCEID, 0);

      if (!st->pbo.use_gs)
         out_layer = ureg_DECL_output(ureg, TGSI_SEMANTIC_LAYER, 0);
   }

   /* out_pos = in_pos */
   ureg_MOV(ureg, out_pos, in_pos);

   if (st->pbo.layers) {
      if (st->pbo.use_gs) {
         /* out_pos.z = i2f(gl_InstanceID) */
         ureg_I2F(ureg, ureg_writemask(out_pos, TGSI_WRITEMASK_Z),
                        ureg_scalar(in_instanceid, TGSI_SWIZZLE_X));
      } else {
         /* out_layer = gl_InstanceID */
         ureg_MOV(ureg, out_layer, in_instanceid);
      }
   }

   ureg_END(ureg);

   return ureg_create_shader_and_destroy(ureg, st->pipe);
}

void *
st_pbo_create_gs(struct st_context *st)
{
   static const int zero = 0;
   struct ureg_program *ureg;
   struct ureg_dst out_pos;
   struct ureg_dst out_layer;
   struct ureg_src in_pos;
   struct ureg_src imm;
   unsigned i;

   ureg = ureg_create(PIPE_SHADER_GEOMETRY);
   if (!ureg)
      return NULL;

   ureg_property(ureg, TGSI_PROPERTY_GS_INPUT_PRIM, PIPE_PRIM_TRIANGLES);
   ureg_property(ureg, TGSI_PROPERTY_GS_OUTPUT_PRIM, PIPE_PRIM_TRIANGLE_STRIP);
   ureg_property(ureg, TGSI_PROPERTY_GS_MAX_OUTPUT_VERTICES, 3);

   out_pos = ureg_DECL_output(ureg, TGSI_SEMANTIC_POSITION, 0);
   out_layer = ureg_DECL_output(ureg, TGSI_SEMANTIC_LAYER, 0);

   in_pos = ureg_DECL_input(ureg, TGSI_SEMANTIC_POSITION, 0, 0, 1);

   imm = ureg_DECL_immediate_int(ureg, &zero, 1);

   for (i = 0; i < 3; ++i) {
      struct ureg_src in_pos_vertex = ureg_src_dimension(in_pos, i);

      /* out_pos = in_pos[i] */
      ureg_MOV(ureg, out_pos, in_pos_vertex);

      /* out_layer.x = f2i(in_pos[i].z) */
      ureg_F2I(ureg, ureg_writemask(out_layer, TGSI_WRITEMASK_X),
                     ureg_scalar(in_pos_vertex, TGSI_SWIZZLE_Z));

      ureg_EMIT(ureg, ureg_scalar(imm, TGSI_SWIZZLE_X));
   }

   ureg_END(ureg);

   return ureg_create_shader_and_destroy(ureg, st->pipe);
}

static void *
create_fs(struct st_context *st, bool download, enum pipe_texture_target target)
{
   struct pipe_context *pipe = st->pipe;
   struct pipe_screen *screen = pipe->screen;
   struct ureg_program *ureg;
   bool have_layer;
   struct ureg_dst out;
   struct ureg_src sampler;
   struct ureg_src pos;
   struct ureg_src layer;
   struct ureg_src const0;
   struct ureg_src const1;
   struct ureg_dst temp0;

   have_layer =
      st->pbo.layers &&
      (!download || target == PIPE_TEXTURE_1D_ARRAY
                 || target == PIPE_TEXTURE_2D_ARRAY
                 || target == PIPE_TEXTURE_3D
                 || target == PIPE_TEXTURE_CUBE
                 || target == PIPE_TEXTURE_CUBE_ARRAY);

   ureg = ureg_create(PIPE_SHADER_FRAGMENT);
   if (!ureg)
      return NULL;

   if (!download) {
      out = ureg_DECL_output(ureg, TGSI_SEMANTIC_COLOR, 0);
   } else {
      struct ureg_src image;

      /* writeonly images do not require an explicitly given format. */
      image = ureg_DECL_image(ureg, 0, TGSI_TEXTURE_BUFFER, PIPE_FORMAT_NONE,
                                    true, false);
      out = ureg_dst(image);
   }

   sampler = ureg_DECL_sampler(ureg, 0);
   if (screen->get_param(screen, PIPE_CAP_TGSI_FS_POSITION_IS_SYSVAL)) {
      pos = ureg_DECL_system_value(ureg, TGSI_SEMANTIC_POSITION, 0);
   } else {
      pos = ureg_DECL_fs_input(ureg, TGSI_SEMANTIC_POSITION, 0,
                               TGSI_INTERPOLATE_LINEAR);
   }
   if (have_layer) {
      layer = ureg_DECL_fs_input(ureg, TGSI_SEMANTIC_LAYER, 0,
                                       TGSI_INTERPOLATE_CONSTANT);
   }
   const0  = ureg_DECL_constant(ureg, 0);
   const1  = ureg_DECL_constant(ureg, 1);
   temp0   = ureg_DECL_temporary(ureg);

   /* Note: const0 = [ -xoffset + skip_pixels, -yoffset, stride, image_height ] */

   /* temp0.xy = f2i(temp0.xy) */
   ureg_F2I(ureg, ureg_writemask(temp0, TGSI_WRITEMASK_XY),
                  ureg_swizzle(pos,
                               TGSI_SWIZZLE_X, TGSI_SWIZZLE_Y,
                               TGSI_SWIZZLE_Y, TGSI_SWIZZLE_Y));

   /* temp0.xy = temp0.xy + const0.xy */
   ureg_UADD(ureg, ureg_writemask(temp0, TGSI_WRITEMASK_XY),
                   ureg_swizzle(ureg_src(temp0),
                                TGSI_SWIZZLE_X, TGSI_SWIZZLE_Y,
                                TGSI_SWIZZLE_Y, TGSI_SWIZZLE_Y),
                   ureg_swizzle(const0,
                                TGSI_SWIZZLE_X, TGSI_SWIZZLE_Y,
                                TGSI_SWIZZLE_Y, TGSI_SWIZZLE_Y));

   /* temp0.x = const0.z * temp0.y + temp0.x */
   ureg_UMAD(ureg, ureg_writemask(temp0, TGSI_WRITEMASK_X),
                   ureg_scalar(const0, TGSI_SWIZZLE_Z),
                   ureg_scalar(ureg_src(temp0), TGSI_SWIZZLE_Y),
                   ureg_scalar(ureg_src(temp0), TGSI_SWIZZLE_X));

   if (have_layer) {
      /* temp0.x = const0.w * layer + temp0.x */
      ureg_UMAD(ureg, ureg_writemask(temp0, TGSI_WRITEMASK_X),
                      ureg_scalar(const0, TGSI_SWIZZLE_W),
                      ureg_scalar(layer, TGSI_SWIZZLE_X),
                      ureg_scalar(ureg_src(temp0), TGSI_SWIZZLE_X));
   }

   /* temp0.w = 0 */
   ureg_MOV(ureg, ureg_writemask(temp0, TGSI_WRITEMASK_W), ureg_imm1u(ureg, 0));

   if (download) {
      struct ureg_dst temp1;
      struct ureg_src op[2];

      temp1 = ureg_DECL_temporary(ureg);

      /* temp1.xy = pos.xy */
      ureg_F2I(ureg, ureg_writemask(temp1, TGSI_WRITEMASK_XY), pos);

      /* temp1.zw = 0 */
      ureg_MOV(ureg, ureg_writemask(temp1, TGSI_WRITEMASK_ZW), ureg_imm1u(ureg, 0));

      if (have_layer) {
         struct ureg_dst temp1_layer =
            ureg_writemask(temp1, target == PIPE_TEXTURE_1D_ARRAY ? TGSI_WRITEMASK_Y
                                                                  : TGSI_WRITEMASK_Z);

         /* temp1.y/z = layer */
         ureg_MOV(ureg, temp1_layer, ureg_scalar(layer, TGSI_SWIZZLE_X));

         if (target == PIPE_TEXTURE_3D) {
            /* temp1.z += layer_offset */
            ureg_UADD(ureg, temp1_layer,
                            ureg_scalar(ureg_src(temp1), TGSI_SWIZZLE_Z),
                            ureg_scalar(const1, TGSI_SWIZZLE_X));
         }
      }

      /* temp1 = txf(sampler, temp1) */
      ureg_TXF(ureg, temp1, util_pipe_tex_to_tgsi_tex(target, 1),
                     ureg_src(temp1), sampler);

      /* store(out, temp0, temp1) */
      op[0] = ureg_src(temp0);
      op[1] = ureg_src(temp1);
      ureg_memory_insn(ureg, TGSI_OPCODE_STORE, &out, 1, op, 2, 0,
                             TGSI_TEXTURE_BUFFER, PIPE_FORMAT_NONE);

      ureg_release_temporary(ureg, temp1);
   } else {
      /* out = txf(sampler, temp0.x) */
      ureg_TXF(ureg, out, TGSI_TEXTURE_BUFFER, ureg_src(temp0), sampler);
   }

   ureg_release_temporary(ureg, temp0);

   ureg_END(ureg);

   return ureg_create_shader_and_destroy(ureg, pipe);
}

void *
st_pbo_create_upload_fs(struct st_context *st)
{
   return create_fs(st, false, 0);
}

void *
st_pbo_get_download_fs(struct st_context *st, enum pipe_texture_target target)
{
   assert(target < PIPE_MAX_TEXTURE_TYPES);

   if (!st->pbo.download_fs[target])
      st->pbo.download_fs[target] = create_fs(st, true, target);

   return st->pbo.download_fs[target];
}

void
st_init_pbo_helpers(struct st_context *st)
{
   struct pipe_context *pipe = st->pipe;
   struct pipe_screen *screen = pipe->screen;

   st->pbo.upload_enabled =
      screen->get_param(screen, PIPE_CAP_TEXTURE_BUFFER_OBJECTS) &&
      screen->get_param(screen, PIPE_CAP_TEXTURE_BUFFER_OFFSET_ALIGNMENT) >= 1 &&
      screen->get_shader_param(screen, PIPE_SHADER_FRAGMENT, PIPE_SHADER_CAP_INTEGERS);
   if (!st->pbo.upload_enabled)
      return;

   st->pbo.download_enabled =
      st->pbo.upload_enabled &&
      screen->get_param(screen, PIPE_CAP_SAMPLER_VIEW_TARGET) &&
      screen->get_param(screen, PIPE_CAP_FRAMEBUFFER_NO_ATTACHMENT) &&
      screen->get_shader_param(screen, PIPE_SHADER_FRAGMENT,
                                       PIPE_SHADER_CAP_MAX_SHADER_IMAGES) >= 1;

   st->pbo.rgba_only =
      screen->get_param(screen, PIPE_CAP_BUFFER_SAMPLER_VIEW_RGBA_ONLY);

   if (screen->get_param(screen, PIPE_CAP_TGSI_INSTANCEID)) {
      if (screen->get_param(screen, PIPE_CAP_TGSI_VS_LAYER_VIEWPORT)) {
         st->pbo.layers = true;
      } else if (screen->get_param(screen, PIPE_CAP_MAX_GEOMETRY_OUTPUT_VERTICES) >= 3) {
         st->pbo.layers = true;
         st->pbo.use_gs = true;
      }
   }

   /* Blend state */
   memset(&st->pbo.upload_blend, 0, sizeof(struct pipe_blend_state));
   st->pbo.upload_blend.rt[0].colormask = PIPE_MASK_RGBA;

   /* Rasterizer state */
   memset(&st->pbo.raster, 0, sizeof(struct pipe_rasterizer_state));
   st->pbo.raster.half_pixel_center = 1;
}

void
st_destroy_pbo_helpers(struct st_context *st)
{
   unsigned i;

   if (st->pbo.upload_fs) {
      cso_delete_fragment_shader(st->cso_context, st->pbo.upload_fs);
      st->pbo.upload_fs = NULL;
   }

   for (i = 0; i < ARRAY_SIZE(st->pbo.download_fs); ++i) {
      if (st->pbo.download_fs[i]) {
         cso_delete_fragment_shader(st->cso_context, st->pbo.download_fs[i]);
         st->pbo.download_fs[i] = NULL;
      }
   }

   if (st->pbo.gs) {
      cso_delete_geometry_shader(st->cso_context, st->pbo.gs);
      st->pbo.gs = NULL;
   }

   if (st->pbo.vs) {
      cso_delete_vertex_shader(st->cso_context, st->pbo.vs);
      st->pbo.vs = NULL;
   }
}
