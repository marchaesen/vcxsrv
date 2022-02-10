
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
#include "st_draw.h"
#include "st_program.h"

#include "cso_cache/cso_context.h"
#include "util/u_math.h"
#include "util/u_upload_mgr.h"
#include "main/bufferobj.h"
#include "main/glformats.h"
#include "main/varray.h"
#include "main/arrayobj.h"

enum st_update_flag {
   UPDATE_ALL,
   UPDATE_BUFFERS_ONLY,
};

/* Always inline the non-64bit element code, so that the compiler can see
 * that velements is on the stack.
 */
static void ALWAYS_INLINE
init_velement(struct pipe_vertex_element *velements,
              const struct gl_vertex_format *vformat,
              int src_offset, unsigned instance_divisor,
              int vbo_index, bool dual_slot, int idx)
{
   velements[idx].src_offset = src_offset;
   velements[idx].src_format = vformat->_PipeFormat;
   velements[idx].instance_divisor = instance_divisor;
   velements[idx].vertex_buffer_index = vbo_index;
   velements[idx].dual_slot = dual_slot;
   assert(velements[idx].src_format);
}

/* ALWAYS_INLINE helps the compiler realize that most of the parameters are
 * on the stack.
 */
template<util_popcnt POPCNT, st_update_flag UPDATE> void ALWAYS_INLINE
setup_arrays(struct st_context *st,
             const struct gl_vertex_array_object *vao,
             const GLbitfield dual_slot_inputs,
             const GLbitfield inputs_read,
             const GLbitfield nonzero_divisor_attribs,
             const GLbitfield enabled_attribs,
             const GLbitfield enabled_user_attribs,
             struct cso_velems_state *velements,
             struct pipe_vertex_buffer *vbuffer, unsigned *num_vbuffers,
             bool *has_user_vertex_buffers)
{
   struct gl_context *ctx = st->ctx;

   /* Process attribute array data. */
   GLbitfield mask = inputs_read & enabled_attribs;
   GLbitfield userbuf_attribs = inputs_read & enabled_user_attribs;

   *has_user_vertex_buffers = userbuf_attribs != 0;
   st->draw_needs_minmax_index =
      (userbuf_attribs & ~nonzero_divisor_attribs) != 0;

   if (vao->IsDynamic) {
      while (mask) {
         const gl_vert_attrib attr = (gl_vert_attrib)u_bit_scan(&mask);
         const struct gl_array_attributes *const attrib =
            _mesa_draw_array_attrib(vao, attr);
         const struct gl_vertex_buffer_binding *const binding =
            &vao->BufferBinding[attrib->BufferBindingIndex];
         const unsigned bufidx = (*num_vbuffers)++;

         /* Set the vertex buffer. */
         if (binding->BufferObj) {
            vbuffer[bufidx].buffer.resource =
               _mesa_get_bufferobj_reference(ctx, binding->BufferObj);
            vbuffer[bufidx].is_user_buffer = false;
            vbuffer[bufidx].buffer_offset = binding->Offset +
                                            attrib->RelativeOffset;
         } else {
            vbuffer[bufidx].buffer.user = attrib->Ptr;
            vbuffer[bufidx].is_user_buffer = true;
            vbuffer[bufidx].buffer_offset = 0;
         }
         vbuffer[bufidx].stride = binding->Stride; /* in bytes */

         if (UPDATE == UPDATE_BUFFERS_ONLY)
            continue;

         /* Set the vertex element. */
         init_velement(velements->velems, &attrib->Format, 0,
                       binding->InstanceDivisor, bufidx,
                       dual_slot_inputs & BITFIELD_BIT(attr),
                       util_bitcount_fast<POPCNT>(inputs_read & BITFIELD_MASK(attr)));
      }
      return;
   }

   while (mask) {
      /* The attribute index to start pulling a binding */
      const gl_vert_attrib i = (gl_vert_attrib)(ffs(mask) - 1);
      const struct gl_vertex_buffer_binding *const binding
         = _mesa_draw_buffer_binding(vao, i);
      const unsigned bufidx = (*num_vbuffers)++;

      if (binding->BufferObj) {
         /* Set the binding */
         vbuffer[bufidx].buffer.resource =
            _mesa_get_bufferobj_reference(ctx, binding->BufferObj);
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

      if (UPDATE == UPDATE_BUFFERS_ONLY)
         continue;

      /* Walk attributes belonging to the binding */
      do {
         const gl_vert_attrib attr = (gl_vert_attrib)u_bit_scan(&attrmask);
         const struct gl_array_attributes *const attrib
            = _mesa_draw_array_attrib(vao, attr);
         const GLuint off = _mesa_draw_attributes_relative_offset(attrib);
         init_velement(velements->velems, &attrib->Format, off,
                       binding->InstanceDivisor, bufidx,
                       dual_slot_inputs & BITFIELD_BIT(attr),
                       util_bitcount_fast<POPCNT>(inputs_read & BITFIELD_MASK(attr)));
      } while (attrmask);
   }
}

/* Only used by the select/feedback mode. */
void
st_setup_arrays(struct st_context *st,
                const struct gl_vertex_program *vp,
                const struct st_common_variant *vp_variant,
                struct cso_velems_state *velements,
                struct pipe_vertex_buffer *vbuffer, unsigned *num_vbuffers,
                bool *has_user_vertex_buffers)
{
   struct gl_context *ctx = st->ctx;

   setup_arrays<POPCNT_NO, UPDATE_ALL>
      (st, ctx->Array._DrawVAO, vp->Base.DualSlotInputs,
       vp_variant->vert_attrib_mask, _mesa_draw_nonzero_divisor_bits(ctx),
       _mesa_draw_array_bits(ctx), _mesa_draw_user_array_bits(ctx),
       velements, vbuffer, num_vbuffers, has_user_vertex_buffers);
}

/* ALWAYS_INLINE helps the compiler realize that most of the parameters are
 * on the stack.
 *
 * Return the index of the vertex buffer where current attribs have been
 * uploaded.
 */
template<util_popcnt POPCNT, st_update_flag UPDATE> void ALWAYS_INLINE
st_setup_current(struct st_context *st,
                 const struct gl_vertex_program *vp,
                 const struct st_common_variant *vp_variant,
                 struct cso_velems_state *velements,
                 struct pipe_vertex_buffer *vbuffer, unsigned *num_vbuffers)
{
   struct gl_context *ctx = st->ctx;
   const GLbitfield inputs_read = vp_variant->vert_attrib_mask;
   const GLbitfield dual_slot_inputs = vp->Base.DualSlotInputs;

   /* Process values that should have better been uniforms in the application */
   GLbitfield curmask = inputs_read & _mesa_draw_current_bits(ctx);
   if (curmask) {
      /* For each attribute, upload the maximum possible size. */
      GLubyte data[VERT_ATTRIB_MAX * sizeof(GLdouble) * 4];
      GLubyte *cursor = data;
      const unsigned bufidx = (*num_vbuffers)++;
      unsigned max_alignment = 1;

      do {
         const gl_vert_attrib attr = (gl_vert_attrib)u_bit_scan(&curmask);
         const struct gl_array_attributes *const attrib
            = _mesa_draw_current_attrib(ctx, attr);
         const unsigned size = attrib->Format._ElementSize;
         const unsigned alignment = util_next_power_of_two(size);
         max_alignment = MAX2(max_alignment, alignment);
         memcpy(cursor, attrib->Ptr, size);
         if (alignment != size)
            memset(cursor + size, 0, alignment - size);

         if (UPDATE == UPDATE_ALL) {
            init_velement(velements->velems, &attrib->Format, cursor - data,
                          0, bufidx, dual_slot_inputs & BITFIELD_BIT(attr),
                          util_bitcount_fast<POPCNT>(inputs_read & BITFIELD_MASK(attr)));
         }

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
   }
}

/* Only used by the select/feedback mode. */
void
st_setup_current_user(struct st_context *st,
                      const struct gl_vertex_program *vp,
                      const struct st_common_variant *vp_variant,
                      struct cso_velems_state *velements,
                      struct pipe_vertex_buffer *vbuffer, unsigned *num_vbuffers)
{
   struct gl_context *ctx = st->ctx;
   const GLbitfield inputs_read = vp_variant->vert_attrib_mask;
   const GLbitfield dual_slot_inputs = vp->Base.DualSlotInputs;

   /* Process values that should have better been uniforms in the application */
   GLbitfield curmask = inputs_read & _mesa_draw_current_bits(ctx);
   /* For each attribute, make an own user buffer binding. */
   while (curmask) {
      const gl_vert_attrib attr = (gl_vert_attrib)u_bit_scan(&curmask);
      const struct gl_array_attributes *const attrib
         = _mesa_draw_current_attrib(ctx, attr);
      const unsigned bufidx = (*num_vbuffers)++;

      init_velement(velements->velems, &attrib->Format, 0, 0,
                    bufidx, dual_slot_inputs & BITFIELD_BIT(attr),
                    util_bitcount(inputs_read & BITFIELD_MASK(attr)));

      vbuffer[bufidx].is_user_buffer = true;
      vbuffer[bufidx].buffer.user = attrib->Ptr;
      vbuffer[bufidx].buffer_offset = 0;
      vbuffer[bufidx].stride = 0;
   }
}

template<util_popcnt POPCNT, st_update_flag UPDATE> void ALWAYS_INLINE
st_update_array_templ(struct st_context *st)
{
   struct gl_context *ctx = st->ctx;

   /* vertex program validation must be done before this */
   /* _NEW_PROGRAM, ST_NEW_VS_STATE */
   const struct gl_vertex_program *vp = (struct gl_vertex_program *)st->vp;
   const struct st_common_variant *vp_variant = st->vp_variant;

   struct pipe_vertex_buffer vbuffer[PIPE_MAX_ATTRIBS];
   unsigned num_vbuffers = 0;
   struct cso_velems_state velements;
   bool uses_user_vertex_buffers;

   /* ST_NEW_VERTEX_ARRAYS */
   /* Setup arrays */
   setup_arrays<POPCNT, UPDATE>
      (st, ctx->Array._DrawVAO, vp->Base.DualSlotInputs,
       vp_variant->vert_attrib_mask, _mesa_draw_nonzero_divisor_bits(ctx),
       _mesa_draw_array_bits(ctx), _mesa_draw_user_array_bits(ctx),
       &velements, vbuffer, &num_vbuffers, &uses_user_vertex_buffers);

   /* _NEW_CURRENT_ATTRIB */
   /* Setup zero-stride attribs. */
   st_setup_current<POPCNT, UPDATE>(st, vp, vp_variant, &velements, vbuffer,
                                    &num_vbuffers);

   unsigned unbind_trailing_vbuffers =
      st->last_num_vbuffers > num_vbuffers ?
         st->last_num_vbuffers - num_vbuffers : 0;
   st->last_num_vbuffers = num_vbuffers;

   struct cso_context *cso = st->cso_context;

   if (UPDATE == UPDATE_ALL) {
      velements.count = vp->num_inputs + vp_variant->key.passthrough_edgeflags;

      /* Set vertex buffers and elements. */
      cso_set_vertex_buffers_and_elements(cso, &velements,
                                          num_vbuffers,
                                          unbind_trailing_vbuffers,
                                          true,
                                          uses_user_vertex_buffers,
                                          vbuffer);
      /* The driver should clear this after it has processed the update. */
      ctx->Array.NewVertexElements = false;
      st->uses_user_vertex_buffers = uses_user_vertex_buffers;
   } else {
      /* Only vertex buffers. */
      cso_set_vertex_buffers(cso, 0, num_vbuffers, unbind_trailing_vbuffers,
                             true, vbuffer);
      /* This can change only when we update vertex elements. */
      assert(st->uses_user_vertex_buffers == uses_user_vertex_buffers);
   }
}

template<util_popcnt POPCNT> void ALWAYS_INLINE
st_update_array_impl(struct st_context *st)
{
   struct gl_context *ctx = st->ctx;

   /* Changing from user to non-user buffers and vice versa can switch between
    * cso and u_vbuf, which means that we need to update vertex elements even
    * when they have not changed.
    */
   if (ctx->Array.NewVertexElements ||
       st->uses_user_vertex_buffers !=
       !!(st->vp_variant->vert_attrib_mask & _mesa_draw_user_array_bits(ctx))) {
      st_update_array_templ<POPCNT, UPDATE_ALL>(st);
   } else {
      st_update_array_templ<POPCNT, UPDATE_BUFFERS_ONLY>(st);
   }
}

void
st_update_array(struct st_context *st)
{
   st_update_array_impl<POPCNT_NO>(st);
}

void
st_update_array_with_popcnt(struct st_context *st)
{
   st_update_array_impl<POPCNT_YES>(st);
}

struct pipe_vertex_state *
st_create_gallium_vertex_state(struct gl_context *ctx,
                               const struct gl_vertex_array_object *vao,
                               struct gl_buffer_object *indexbuf,
                               uint32_t enabled_attribs)
{
   struct st_context *st = st_context(ctx);
   const GLbitfield inputs_read = enabled_attribs;
   const GLbitfield dual_slot_inputs = 0; /* always zero */
   struct pipe_vertex_buffer vbuffer[PIPE_MAX_ATTRIBS];
   unsigned num_vbuffers = 0;
   struct cso_velems_state velements;
   bool uses_user_vertex_buffers;

   setup_arrays<POPCNT_NO, UPDATE_ALL>(st, vao, dual_slot_inputs, inputs_read, 0,
                                inputs_read, 0, &velements, vbuffer, &num_vbuffers,
                                &uses_user_vertex_buffers);

   if (num_vbuffers != 1 || uses_user_vertex_buffers) {
      assert(!"this should never happen with display lists");
      return NULL;
   }

   velements.count = util_bitcount(inputs_read);

   struct pipe_screen *screen = st->screen;
   struct pipe_vertex_state *state =
      screen->create_vertex_state(screen, &vbuffer[0], velements.velems,
                                  velements.count,
                                  indexbuf ?
                                  indexbuf->buffer : NULL,
                                  enabled_attribs);

   for (unsigned i = 0; i < num_vbuffers; i++)
      pipe_vertex_buffer_unreference(&vbuffer[i]);
   return state;
}
