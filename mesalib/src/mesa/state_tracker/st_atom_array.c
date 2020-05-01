
/**************************************************************************
 *
 * Copyright 2007 VMware, Inc.
 * Copyright 2012 Marek Olšák <maraeo@gmail.com>
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
 * IN NO EVENT SHALL AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

/*
 * This converts the VBO's vertex attribute/array information into
 * Gallium vertex state and binds it.
 *
 * Authors:
 *   Keith Whitwell <keithw@vmware.com>
 *   Marek Olšák <maraeo@gmail.com>
 */

#include "st_context.h"
#include "st_atom.h"
#include "st_cb_bufferobjects.h"
#include "st_draw.h"
#include "st_program.h"

#include "cso_cache/cso_context.h"
#include "util/u_math.h"
#include "util/u_upload_mgr.h"
#include "main/bufferobj.h"
#include "main/glformats.h"
#include "main/varray.h"
#include "main/arrayobj.h"

static void set_velement(struct pipe_vertex_element *velement,
                          int src_offset, int format,
                          int instance_divisor, int vbo_index)
{
   velement->src_offset = src_offset;
   velement->src_format = format;
   velement->instance_divisor = instance_divisor;
   velement->vertex_buffer_index = vbo_index;
   assert(velement->src_format);
}

static void init_velement_64bit(const struct st_vertex_program *vp,
                                struct pipe_vertex_element *velements,
                                const struct gl_vertex_format *vformat,
                                int src_offset, int instance_divisor,
                                int vbo_index, int idx)
{
   const GLubyte nr_components = vformat->Size;
   int lower_format;

   if (nr_components < 2)
      lower_format = PIPE_FORMAT_R32G32_UINT;
   else
      lower_format = PIPE_FORMAT_R32G32B32A32_UINT;

   set_velement(&velements[idx], src_offset,
                lower_format, instance_divisor, vbo_index);
   idx++;

   if (idx < vp->num_inputs &&
       vp->index_to_input[idx] == ST_DOUBLE_ATTRIB_PLACEHOLDER) {
      if (nr_components >= 3) {
         if (nr_components == 3)
            lower_format = PIPE_FORMAT_R32G32_UINT;
         else
            lower_format = PIPE_FORMAT_R32G32B32A32_UINT;

         set_velement(&velements[idx], src_offset + 4 * sizeof(float),
                      lower_format, instance_divisor, vbo_index);
      } else {
         /* The values here are undefined. Fill in some conservative
          * dummy values.
          */
         set_velement(&velements[idx], src_offset, PIPE_FORMAT_R32G32_UINT,
                      instance_divisor, vbo_index);
      }
   }
}

/* Always inline the non-64bit element code, so that the compiler can see
 * that velements is on the stack.
 */
static void ALWAYS_INLINE
init_velement(const struct st_vertex_program *vp,
              struct pipe_vertex_element *velements,
              const struct gl_vertex_format *vformat,
              int src_offset, int instance_divisor,
              int vbo_index, int idx)
{
   if (!vformat->Doubles) {
      velements[idx].src_offset = src_offset;
      velements[idx].src_format = vformat->_PipeFormat;
      velements[idx].instance_divisor = instance_divisor;
      velements[idx].vertex_buffer_index = vbo_index;
      assert(velements[idx].src_format);
      return;
   }

   init_velement_64bit(vp, velements, vformat, src_offset, instance_divisor,
                       vbo_index, idx);
}

/* ALWAYS_INLINE helps the compiler realize that most of the parameters are
 * on the stack.
 */
void
#ifndef _MSC_VER /* MSVC doesn't like inlining public functions */
ALWAYS_INLINE
#endif
st_setup_arrays(struct st_context *st,
                const struct st_vertex_program *vp,
                const struct st_common_variant *vp_variant,
                struct cso_velems_state *velements,
                struct pipe_vertex_buffer *vbuffer, unsigned *num_vbuffers,
                bool *has_user_vertex_buffers)
{
   struct gl_context *ctx = st->ctx;
   const struct gl_vertex_array_object *vao = ctx->Array._DrawVAO;
   const GLbitfield inputs_read = vp_variant->vert_attrib_mask;
   const ubyte *input_to_index = vp->input_to_index;

   /* Process attribute array data. */
   GLbitfield mask = inputs_read & _mesa_draw_array_bits(ctx);
   GLbitfield userbuf_attribs = inputs_read & _mesa_draw_user_array_bits(ctx);

   *has_user_vertex_buffers = userbuf_attribs != 0;
   st->draw_needs_minmax_index =
      (userbuf_attribs & ~_mesa_draw_nonzero_divisor_bits(ctx)) != 0;

   if (vao->IsDynamic) {
      while (mask) {
         const gl_vert_attrib attr = u_bit_scan(&mask);
         const struct gl_array_attributes *const attrib =
            _mesa_draw_array_attrib(vao, attr);
         const struct gl_vertex_buffer_binding *const binding =
            &vao->BufferBinding[attrib->BufferBindingIndex];
         const unsigned bufidx = (*num_vbuffers)++;

         /* Set the vertex buffer. */
         if (binding->BufferObj) {
            struct st_buffer_object *stobj = st_buffer_object(binding->BufferObj);

            vbuffer[bufidx].buffer.resource = stobj ? stobj->buffer : NULL;
            vbuffer[bufidx].is_user_buffer = false;
            vbuffer[bufidx].buffer_offset = binding->Offset +
                                            attrib->RelativeOffset;
         } else {
            vbuffer[bufidx].buffer.user = attrib->Ptr;
            vbuffer[bufidx].is_user_buffer = true;
            vbuffer[bufidx].buffer_offset = 0;
         }
         vbuffer[bufidx].stride = binding->Stride; /* in bytes */

         /* Set the vertex element. */
         init_velement(vp, velements->velems, &attrib->Format, 0,
                       binding->InstanceDivisor, bufidx,
                       input_to_index[attr]);
      }
      return;
   }

   while (mask) {
      /* The attribute index to start pulling a binding */
      const gl_vert_attrib i = ffs(mask) - 1;
      const struct gl_vertex_buffer_binding *const binding
         = _mesa_draw_buffer_binding(vao, i);
      const unsigned bufidx = (*num_vbuffers)++;

      if (binding->BufferObj) {
         /* Set the binding */
         struct st_buffer_object *stobj = st_buffer_object(binding->BufferObj);

         vbuffer[bufidx].buffer.resource = stobj ? stobj->buffer : NULL;
         vbuffer[bufidx].is_user_buffer = false;
         vbuffer[bufidx].buffer_offset = _mesa_draw_binding_offset(binding);
      } else {
         /* Set the binding */
         const void *ptr = (const void *)_mesa_draw_binding_offset(binding);
         vbuffer[bufidx].buffer.user = ptr;
         vbuffer[bufidx].is_user_buffer = true;
         vbuffer[bufidx].buffer_offset = 0;
      }
      vbuffer[bufidx].stride = binding->Stride; /* in bytes */

      const GLbitfield boundmask = _mesa_draw_bound_attrib_bits(binding);
      GLbitfield attrmask = mask & boundmask;
      /* Mark the those attributes as processed */
      mask &= ~boundmask;
      /* We can assume that we have array for the binding */
      assert(attrmask);
      /* Walk attributes belonging to the binding */
      do {
         const gl_vert_attrib attr = u_bit_scan(&attrmask);
         const struct gl_array_attributes *const attrib
            = _mesa_draw_array_attrib(vao, attr);
         const GLuint off = _mesa_draw_attributes_relative_offset(attrib);
         init_velement(vp, velements->velems, &attrib->Format, off,
                       binding->InstanceDivisor, bufidx,
                       input_to_index[attr]);
      } while (attrmask);
   }
}

/* ALWAYS_INLINE helps the compiler realize that most of the parameters are
 * on the stack.
 *
 * Return the index of the vertex buffer where current attribs have been
 * uploaded.
 */
static int ALWAYS_INLINE
st_setup_current(struct st_context *st,
                 const struct st_vertex_program *vp,
                 const struct st_common_variant *vp_variant,
                 struct cso_velems_state *velements,
                 struct pipe_vertex_buffer *vbuffer, unsigned *num_vbuffers)
{
   struct gl_context *ctx = st->ctx;
   const GLbitfield inputs_read = vp_variant->vert_attrib_mask;

   /* Process values that should have better been uniforms in the application */
   GLbitfield curmask = inputs_read & _mesa_draw_current_bits(ctx);
   if (curmask) {
      const ubyte *input_to_index = vp->input_to_index;
      /* For each attribute, upload the maximum possible size. */
      GLubyte data[VERT_ATTRIB_MAX * sizeof(GLdouble) * 4];
      GLubyte *cursor = data;
      const unsigned bufidx = (*num_vbuffers)++;
      unsigned max_alignment = 1;

      do {
         const gl_vert_attrib attr = u_bit_scan(&curmask);
         const struct gl_array_attributes *const attrib
            = _mesa_draw_current_attrib(ctx, attr);
         const unsigned size = attrib->Format._ElementSize;
         const unsigned alignment = util_next_power_of_two(size);
         max_alignment = MAX2(max_alignment, alignment);
         memcpy(cursor, attrib->Ptr, size);
         if (alignment != size)
            memset(cursor + size, 0, alignment - size);

         init_velement(vp, velements->velems, &attrib->Format, cursor - data,
                       0, bufidx, input_to_index[attr]);

         cursor += alignment;
      } while (curmask);

      vbuffer[bufidx].is_user_buffer = false;
      vbuffer[bufidx].buffer.resource = NULL;
      /* vbuffer[bufidx].buffer_offset is set below */
      vbuffer[bufidx].stride = 0;

      /* Use const_uploader for zero-stride vertex attributes, because
       * it may use a better memory placement than stream_uploader.
       * The reason is that zero-stride attributes can be fetched many
       * times (thousands of times), so a better placement is going to
       * perform better.
       */
      struct u_upload_mgr *uploader = st->can_bind_const_buffer_as_vertex ?
                                      st->pipe->const_uploader :
                                      st->pipe->stream_uploader;
      u_upload_data(uploader,
                    0, cursor - data, max_alignment, data,
                    &vbuffer[bufidx].buffer_offset,
                    &vbuffer[bufidx].buffer.resource);
      /* Always unmap. The uploader might use explicit flushes. */
      u_upload_unmap(uploader);
      return bufidx;
   }
   return -1;
}

void
st_setup_current_user(struct st_context *st,
                      const struct st_vertex_program *vp,
                      const struct st_common_variant *vp_variant,
                      struct cso_velems_state *velements,
                      struct pipe_vertex_buffer *vbuffer, unsigned *num_vbuffers)
{
   struct gl_context *ctx = st->ctx;
   const GLbitfield inputs_read = vp_variant->vert_attrib_mask;
   const ubyte *input_to_index = vp->input_to_index;

   /* Process values that should have better been uniforms in the application */
   GLbitfield curmask = inputs_read & _mesa_draw_current_bits(ctx);
   /* For each attribute, make an own user buffer binding. */
   while (curmask) {
      const gl_vert_attrib attr = u_bit_scan(&curmask);
      const struct gl_array_attributes *const attrib
         = _mesa_draw_current_attrib(ctx, attr);
      const unsigned bufidx = (*num_vbuffers)++;

      init_velement(vp, velements->velems, &attrib->Format, 0, 0,
                    bufidx, input_to_index[attr]);

      vbuffer[bufidx].is_user_buffer = true;
      vbuffer[bufidx].buffer.user = attrib->Ptr;
      vbuffer[bufidx].buffer_offset = 0;
      vbuffer[bufidx].stride = 0;
   }
}

void
st_update_array(struct st_context *st)
{
   /* vertex program validation must be done before this */
   /* _NEW_PROGRAM, ST_NEW_VS_STATE */
   const struct st_vertex_program *vp = (struct st_vertex_program *)st->vp;
   const struct st_common_variant *vp_variant = st->vp_variant;

   struct pipe_vertex_buffer vbuffer[PIPE_MAX_ATTRIBS];
   unsigned num_vbuffers = 0;
   struct cso_velems_state velements;
   bool uses_user_vertex_buffers;

   /* ST_NEW_VERTEX_ARRAYS alias ctx->DriverFlags.NewArray */
   /* Setup arrays */
   st_setup_arrays(st, vp, vp_variant, &velements, vbuffer, &num_vbuffers,
                   &uses_user_vertex_buffers);

   /* _NEW_CURRENT_ATTRIB */
   /* Setup zero-stride attribs. */
   int current_attrib_buffer =
      st_setup_current(st, vp, vp_variant, &velements, vbuffer, &num_vbuffers);

   velements.count = vp->num_inputs + vp_variant->key.passthrough_edgeflags;

   /* Set vertex buffers and elements. */
   struct cso_context *cso = st->cso_context;
   unsigned unbind_trailing_vbuffers =
      st->last_num_vbuffers > num_vbuffers ?
         st->last_num_vbuffers - num_vbuffers : 0;
   cso_set_vertex_buffers_and_elements(cso, &velements,
                                       num_vbuffers,
                                       unbind_trailing_vbuffers,
                                       vbuffer, uses_user_vertex_buffers);
   st->last_num_vbuffers = num_vbuffers;

   /* Unreference uploaded current attrib buffer. */
   if (current_attrib_buffer >= 0)
      pipe_resource_reference(&vbuffer[current_attrib_buffer].buffer.resource, NULL);
}
