
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

/* vertex_formats[gltype - GL_BYTE][integer*2 + normalized][size - 1] */
static const uint16_t vertex_formats[][4][4] = {
   { /* GL_BYTE */
      {
         PIPE_FORMAT_R8_SSCALED,
         PIPE_FORMAT_R8G8_SSCALED,
         PIPE_FORMAT_R8G8B8_SSCALED,
         PIPE_FORMAT_R8G8B8A8_SSCALED
      },
      {
         PIPE_FORMAT_R8_SNORM,
         PIPE_FORMAT_R8G8_SNORM,
         PIPE_FORMAT_R8G8B8_SNORM,
         PIPE_FORMAT_R8G8B8A8_SNORM
      },
      {
         PIPE_FORMAT_R8_SINT,
         PIPE_FORMAT_R8G8_SINT,
         PIPE_FORMAT_R8G8B8_SINT,
         PIPE_FORMAT_R8G8B8A8_SINT
      },
   },
   { /* GL_UNSIGNED_BYTE */
      {
         PIPE_FORMAT_R8_USCALED,
         PIPE_FORMAT_R8G8_USCALED,
         PIPE_FORMAT_R8G8B8_USCALED,
         PIPE_FORMAT_R8G8B8A8_USCALED
      },
      {
         PIPE_FORMAT_R8_UNORM,
         PIPE_FORMAT_R8G8_UNORM,
         PIPE_FORMAT_R8G8B8_UNORM,
         PIPE_FORMAT_R8G8B8A8_UNORM
      },
      {
         PIPE_FORMAT_R8_UINT,
         PIPE_FORMAT_R8G8_UINT,
         PIPE_FORMAT_R8G8B8_UINT,
         PIPE_FORMAT_R8G8B8A8_UINT
      },
   },
   { /* GL_SHORT */
      {
         PIPE_FORMAT_R16_SSCALED,
         PIPE_FORMAT_R16G16_SSCALED,
         PIPE_FORMAT_R16G16B16_SSCALED,
         PIPE_FORMAT_R16G16B16A16_SSCALED
      },
      {
         PIPE_FORMAT_R16_SNORM,
         PIPE_FORMAT_R16G16_SNORM,
         PIPE_FORMAT_R16G16B16_SNORM,
         PIPE_FORMAT_R16G16B16A16_SNORM
      },
      {
         PIPE_FORMAT_R16_SINT,
         PIPE_FORMAT_R16G16_SINT,
         PIPE_FORMAT_R16G16B16_SINT,
         PIPE_FORMAT_R16G16B16A16_SINT
      },
   },
   { /* GL_UNSIGNED_SHORT */
      {
         PIPE_FORMAT_R16_USCALED,
         PIPE_FORMAT_R16G16_USCALED,
         PIPE_FORMAT_R16G16B16_USCALED,
         PIPE_FORMAT_R16G16B16A16_USCALED
      },
      {
         PIPE_FORMAT_R16_UNORM,
         PIPE_FORMAT_R16G16_UNORM,
         PIPE_FORMAT_R16G16B16_UNORM,
         PIPE_FORMAT_R16G16B16A16_UNORM
      },
      {
         PIPE_FORMAT_R16_UINT,
         PIPE_FORMAT_R16G16_UINT,
         PIPE_FORMAT_R16G16B16_UINT,
         PIPE_FORMAT_R16G16B16A16_UINT
      },
   },
   { /* GL_INT */
      {
         PIPE_FORMAT_R32_SSCALED,
         PIPE_FORMAT_R32G32_SSCALED,
         PIPE_FORMAT_R32G32B32_SSCALED,
         PIPE_FORMAT_R32G32B32A32_SSCALED
      },
      {
         PIPE_FORMAT_R32_SNORM,
         PIPE_FORMAT_R32G32_SNORM,
         PIPE_FORMAT_R32G32B32_SNORM,
         PIPE_FORMAT_R32G32B32A32_SNORM
      },
      {
         PIPE_FORMAT_R32_SINT,
         PIPE_FORMAT_R32G32_SINT,
         PIPE_FORMAT_R32G32B32_SINT,
         PIPE_FORMAT_R32G32B32A32_SINT
      },
   },
   { /* GL_UNSIGNED_INT */
      {
         PIPE_FORMAT_R32_USCALED,
         PIPE_FORMAT_R32G32_USCALED,
         PIPE_FORMAT_R32G32B32_USCALED,
         PIPE_FORMAT_R32G32B32A32_USCALED
      },
      {
         PIPE_FORMAT_R32_UNORM,
         PIPE_FORMAT_R32G32_UNORM,
         PIPE_FORMAT_R32G32B32_UNORM,
         PIPE_FORMAT_R32G32B32A32_UNORM
      },
      {
         PIPE_FORMAT_R32_UINT,
         PIPE_FORMAT_R32G32_UINT,
         PIPE_FORMAT_R32G32B32_UINT,
         PIPE_FORMAT_R32G32B32A32_UINT
      },
   },
   { /* GL_FLOAT */
      {
         PIPE_FORMAT_R32_FLOAT,
         PIPE_FORMAT_R32G32_FLOAT,
         PIPE_FORMAT_R32G32B32_FLOAT,
         PIPE_FORMAT_R32G32B32A32_FLOAT
      },
      {
         PIPE_FORMAT_R32_FLOAT,
         PIPE_FORMAT_R32G32_FLOAT,
         PIPE_FORMAT_R32G32B32_FLOAT,
         PIPE_FORMAT_R32G32B32A32_FLOAT
      },
   },
   {{0}}, /* GL_2_BYTES */
   {{0}}, /* GL_3_BYTES */
   {{0}}, /* GL_4_BYTES */
   { /* GL_DOUBLE */
      {
         PIPE_FORMAT_R64_FLOAT,
         PIPE_FORMAT_R64G64_FLOAT,
         PIPE_FORMAT_R64G64B64_FLOAT,
         PIPE_FORMAT_R64G64B64A64_FLOAT
      },
      {
         PIPE_FORMAT_R64_FLOAT,
         PIPE_FORMAT_R64G64_FLOAT,
         PIPE_FORMAT_R64G64B64_FLOAT,
         PIPE_FORMAT_R64G64B64A64_FLOAT
      },
   },
   { /* GL_HALF_FLOAT */
      {
         PIPE_FORMAT_R16_FLOAT,
         PIPE_FORMAT_R16G16_FLOAT,
         PIPE_FORMAT_R16G16B16_FLOAT,
         PIPE_FORMAT_R16G16B16A16_FLOAT
      },
      {
         PIPE_FORMAT_R16_FLOAT,
         PIPE_FORMAT_R16G16_FLOAT,
         PIPE_FORMAT_R16G16B16_FLOAT,
         PIPE_FORMAT_R16G16B16A16_FLOAT
      },
   },
   { /* GL_FIXED */
      {
         PIPE_FORMAT_R32_FIXED,
         PIPE_FORMAT_R32G32_FIXED,
         PIPE_FORMAT_R32G32B32_FIXED,
         PIPE_FORMAT_R32G32B32A32_FIXED
      },
      {
         PIPE_FORMAT_R32_FIXED,
         PIPE_FORMAT_R32G32_FIXED,
         PIPE_FORMAT_R32G32B32_FIXED,
         PIPE_FORMAT_R32G32B32A32_FIXED
      },
   },
};


/**
 * Return a PIPE_FORMAT_x for the given GL datatype and size.
 */
enum pipe_format
st_pipe_vertex_format(const struct gl_array_attributes *attrib)
{
   const GLubyte size = attrib->Size;
   const GLenum16 format = attrib->Format;
   const bool normalized = attrib->Normalized;
   const bool integer = attrib->Integer;
   GLenum16 type = attrib->Type;
   unsigned index;

   assert(size >= 1 && size <= 4);
   assert(format == GL_RGBA || format == GL_BGRA);
   assert(attrib->_ElementSize == _mesa_bytes_per_vertex_attrib(size, type));

   switch (type) {
   case GL_HALF_FLOAT_OES:
      type = GL_HALF_FLOAT;
      break;

   case GL_INT_2_10_10_10_REV:
      assert(size == 4 && !integer);

      if (format == GL_BGRA) {
         if (normalized)
            return PIPE_FORMAT_B10G10R10A2_SNORM;
         else
            return PIPE_FORMAT_B10G10R10A2_SSCALED;
      } else {
         if (normalized)
            return PIPE_FORMAT_R10G10B10A2_SNORM;
         else
            return PIPE_FORMAT_R10G10B10A2_SSCALED;
      }
      break;

   case GL_UNSIGNED_INT_2_10_10_10_REV:
      assert(size == 4 && !integer);

      if (format == GL_BGRA) {
         if (normalized)
            return PIPE_FORMAT_B10G10R10A2_UNORM;
         else
            return PIPE_FORMAT_B10G10R10A2_USCALED;
      } else {
         if (normalized)
            return PIPE_FORMAT_R10G10B10A2_UNORM;
         else
            return PIPE_FORMAT_R10G10B10A2_USCALED;
      }
      break;

   case GL_UNSIGNED_INT_10F_11F_11F_REV:
      assert(size == 3 && !integer && format == GL_RGBA);
      return PIPE_FORMAT_R11G11B10_FLOAT;

   case GL_UNSIGNED_BYTE:
      if (format == GL_BGRA) {
         /* this is an odd-ball case */
         assert(normalized);
         return PIPE_FORMAT_B8G8R8A8_UNORM;
      }
      break;
   }

   index = integer*2 + normalized;
   assert(index <= 2);
   assert(type >= GL_BYTE && type <= GL_FIXED);
   return vertex_formats[type - GL_BYTE][index][size-1];
}

static const struct gl_vertex_array *
get_client_array(const struct gl_vertex_array *arrays,
                 unsigned mesaAttr)
{
   /* st_program uses 0xffffffff to denote a double placeholder attribute */
   if (mesaAttr == ST_DOUBLE_ATTRIB_PLACEHOLDER)
      return NULL;
   return &arrays[mesaAttr];
}

/**
 * Examine the active arrays to determine if we have interleaved
 * vertex arrays all living in one VBO, or all living in user space.
 */
static GLboolean
is_interleaved_arrays(const struct st_vertex_program *vp,
                      const struct gl_vertex_array *arrays,
                      unsigned num_inputs)
{
   GLuint attr;
   const struct gl_buffer_object *firstBufObj = NULL;
   GLint firstStride = -1;
   const GLubyte *firstPtr = NULL;
   GLboolean userSpaceBuffer = GL_FALSE;

   for (attr = 0; attr < num_inputs; attr++) {
      const struct gl_vertex_array *array;
      const struct gl_vertex_buffer_binding *binding;
      const struct gl_array_attributes *attrib;
      const GLubyte *ptr;
      const struct gl_buffer_object *bufObj;
      GLsizei stride;

      array = get_client_array(arrays, vp->index_to_input[attr]);
      if (!array)
	 continue;

      binding = array->BufferBinding;
      attrib = array->VertexAttrib;
      stride = binding->Stride; /* in bytes */
      ptr = _mesa_vertex_attrib_address(attrib, binding);

      /* To keep things simple, don't allow interleaved zero-stride attribs. */
      if (stride == 0)
         return false;

      bufObj = binding->BufferObj;
      if (attr == 0) {
         /* save info about the first array */
         firstStride = stride;
         firstPtr = ptr;
         firstBufObj = bufObj;
         userSpaceBuffer = !_mesa_is_bufferobj(bufObj);
      }
      else {
         /* check if other arrays interleave with the first, in same buffer */
         if (stride != firstStride)
            return GL_FALSE; /* strides don't match */

         if (bufObj != firstBufObj)
            return GL_FALSE; /* arrays in different VBOs */

         if (llabs(ptr - firstPtr) > firstStride)
            return GL_FALSE; /* arrays start too far apart */

         if ((!_mesa_is_bufferobj(bufObj)) != userSpaceBuffer)
            return GL_FALSE; /* mix of VBO and user-space arrays */
      }
   }

   return GL_TRUE;
}

static void init_velement(struct pipe_vertex_element *velement,
                          int src_offset, int format,
                          int instance_divisor, int vbo_index)
{
   velement->src_offset = src_offset;
   velement->src_format = format;
   velement->instance_divisor = instance_divisor;
   velement->vertex_buffer_index = vbo_index;
   assert(velement->src_format);
}

static void init_velement_lowered(const struct st_vertex_program *vp,
                                  struct pipe_vertex_element *velements,
                                  int src_offset, int format,
                                  int instance_divisor, int vbo_index,
                                  int nr_components, GLboolean doubles,
                                  GLuint *attr_idx)
{
   int idx = *attr_idx;
   if (doubles) {
      int lower_format;

      if (nr_components < 2)
         lower_format = PIPE_FORMAT_R32G32_UINT;
      else
         lower_format = PIPE_FORMAT_R32G32B32A32_UINT;

      init_velement(&velements[idx], src_offset,
                    lower_format, instance_divisor, vbo_index);
      idx++;

      if (idx < vp->num_inputs &&
          vp->index_to_input[idx] == ST_DOUBLE_ATTRIB_PLACEHOLDER) {
         if (nr_components >= 3) {
            if (nr_components == 3)
               lower_format = PIPE_FORMAT_R32G32_UINT;
            else
               lower_format = PIPE_FORMAT_R32G32B32A32_UINT;

            init_velement(&velements[idx], src_offset + 4 * sizeof(float),
                        lower_format, instance_divisor, vbo_index);
         } else {
            /* The values here are undefined. Fill in some conservative
             * dummy values.
             */
            init_velement(&velements[idx], src_offset, PIPE_FORMAT_R32G32_UINT,
                          instance_divisor, vbo_index);
         }

         idx++;
      }
   } else {
      init_velement(&velements[idx], src_offset,
                    format, instance_divisor, vbo_index);
      idx++;
   }
   *attr_idx = idx;
}

static void
set_vertex_attribs(struct st_context *st,
                   struct pipe_vertex_buffer *vbuffers,
                   unsigned num_vbuffers,
                   struct pipe_vertex_element *velements,
                   unsigned num_velements)
{
   struct cso_context *cso = st->cso_context;

   cso_set_vertex_buffers(cso, 0, num_vbuffers, vbuffers);
   if (st->last_num_vbuffers > num_vbuffers) {
      /* Unbind remaining buffers, if any. */
      cso_set_vertex_buffers(cso, num_vbuffers,
                             st->last_num_vbuffers - num_vbuffers, NULL);
   }
   st->last_num_vbuffers = num_vbuffers;
   cso_set_vertex_elements(cso, num_velements, velements);
}

/**
 * Set up for drawing interleaved arrays that all live in one VBO
 * or all live in user space.
 * \param vbuffer  returns vertex buffer info
 * \param velements  returns vertex element info
 */
static void
setup_interleaved_attribs(struct st_context *st,
                          const struct st_vertex_program *vp,
                          const struct gl_vertex_array *arrays,
                          unsigned num_inputs)
{
   struct pipe_vertex_buffer vbuffer;
   struct pipe_vertex_element velements[PIPE_MAX_ATTRIBS] = {{0}};
   GLuint attr;
   const GLubyte *low_addr = NULL;
   GLboolean usingVBO;      /* all arrays in a VBO? */
   struct gl_buffer_object *bufobj;
   GLsizei stride;

   /* Find the lowest address of the arrays we're drawing,
    * Init bufobj and stride.
    */
   if (num_inputs) {
      const struct gl_vertex_array *array;
      const struct gl_vertex_buffer_binding *binding;
      const struct gl_array_attributes *attrib;

      array = get_client_array(arrays, vp->index_to_input[0]);
      assert(array);

      binding = array->BufferBinding;
      attrib = array->VertexAttrib;

      /* Since we're doing interleaved arrays, we know there'll be at most
       * one buffer object and the stride will be the same for all arrays.
       * Grab them now.
       */
      bufobj = binding->BufferObj;
      stride = binding->Stride;

      low_addr = _mesa_vertex_attrib_address(attrib, binding);

      for (attr = 1; attr < num_inputs; attr++) {
         const GLubyte *start;
         array = get_client_array(arrays, vp->index_to_input[attr]);
         if (!array)
            continue;
         binding = array->BufferBinding;
         attrib = array->VertexAttrib;
         start = _mesa_vertex_attrib_address(attrib, binding);
         low_addr = MIN2(low_addr, start);
      }
   }
   else {
      /* not sure we'll ever have zero inputs, but play it safe */
      bufobj = NULL;
      stride = 0;
      low_addr = 0;
   }

   /* are the arrays in user space? */
   usingVBO = _mesa_is_bufferobj(bufobj);

   for (attr = 0; attr < num_inputs;) {
      const struct gl_vertex_array *array;
      const struct gl_vertex_buffer_binding *binding;
      const struct gl_array_attributes *attrib;
      const GLubyte *ptr;
      unsigned src_offset;
      unsigned src_format;

      array = get_client_array(arrays, vp->index_to_input[attr]);
      assert(array);

      binding = array->BufferBinding;
      attrib = array->VertexAttrib;
      ptr = _mesa_vertex_attrib_address(attrib, binding);

      src_offset = (unsigned) (ptr - low_addr);

      src_format = st_pipe_vertex_format(attrib);

      init_velement_lowered(vp, velements, src_offset, src_format,
                            binding->InstanceDivisor, 0,
                            attrib->Size, attrib->Doubles, &attr);
   }

   /*
    * Return the vbuffer info and setup user-space attrib info, if needed.
    */
   if (num_inputs == 0) {
      /* just defensive coding here */
      vbuffer.buffer.resource = NULL;
      vbuffer.is_user_buffer = false;
      vbuffer.buffer_offset = 0;
      vbuffer.stride = 0;
   }
   else if (usingVBO) {
      /* all interleaved arrays in a VBO */
      struct st_buffer_object *stobj = st_buffer_object(bufobj);

      if (!stobj || !stobj->buffer) {
         st->vertex_array_out_of_memory = true;
         return; /* out-of-memory error probably */
      }

      vbuffer.buffer.resource = stobj->buffer;
      vbuffer.is_user_buffer = false;
      vbuffer.buffer_offset = pointer_to_offset(low_addr);
      vbuffer.stride = stride;
   }
   else {
      /* all interleaved arrays in user memory */
      vbuffer.buffer.user = low_addr;
      vbuffer.is_user_buffer = !!low_addr; /* if NULL, then unbind */
      vbuffer.buffer_offset = 0;
      vbuffer.stride = stride;

      if (low_addr)
         st->draw_needs_minmax_index = true;
   }

   set_vertex_attribs(st, &vbuffer, num_inputs ? 1 : 0,
                      velements, num_inputs);
}

/**
 * Set up a separate pipe_vertex_buffer and pipe_vertex_element for each
 * vertex attribute.
 * \param vbuffer  returns vertex buffer info
 * \param velements  returns vertex element info
 */
static void
setup_non_interleaved_attribs(struct st_context *st,
                              const struct st_vertex_program *vp,
                              const struct gl_vertex_array *arrays,
                              unsigned num_inputs)
{
   struct gl_context *ctx = st->ctx;
   struct pipe_vertex_buffer vbuffer[PIPE_MAX_ATTRIBS];
   struct pipe_vertex_element velements[PIPE_MAX_ATTRIBS] = {{0}};
   unsigned num_vbuffers = 0;
   unsigned unref_buffers = 0;
   GLuint attr;

   for (attr = 0; attr < num_inputs;) {
      const unsigned mesaAttr = vp->index_to_input[attr];
      const struct gl_vertex_array *array;
      const struct gl_vertex_buffer_binding *binding;
      const struct gl_array_attributes *attrib;
      struct gl_buffer_object *bufobj;
      GLsizei stride;
      unsigned src_format;
      unsigned bufidx;

      array = get_client_array(arrays, mesaAttr);
      assert(array);

      bufidx = num_vbuffers++;

      binding = array->BufferBinding;
      attrib = array->VertexAttrib;
      stride = binding->Stride;
      bufobj = binding->BufferObj;

      if (_mesa_is_bufferobj(bufobj)) {
         /* Attribute data is in a VBO.
          * Recall that for VBOs, the gl_vertex_array->Ptr field is
          * really an offset from the start of the VBO, not a pointer.
          */
         struct st_buffer_object *stobj = st_buffer_object(bufobj);

         if (!stobj || !stobj->buffer) {
            st->vertex_array_out_of_memory = true;
            return; /* out-of-memory error probably */
         }

         vbuffer[bufidx].buffer.resource = stobj->buffer;
         vbuffer[bufidx].is_user_buffer = false;
         vbuffer[bufidx].buffer_offset =
            binding->Offset + attrib->RelativeOffset;
      }
      else {
         if (stride == 0) {
            unsigned size = attrib->_ElementSize;
            /* This is optimal for GPU cache line usage if the upload size
             * is <= cache line size.
             */
            unsigned alignment = util_next_power_of_two(size);

            assert(attrib->Ptr);
            vbuffer[bufidx].buffer.user = attrib->Ptr;
            void *ptr = attrib->Ptr ? (void*)attrib->Ptr :
                                      (void*)ctx->Current.Attrib[mesaAttr];

            vbuffer[bufidx].is_user_buffer = false;
            vbuffer[bufidx].buffer.resource = NULL;

            /* Use const_uploader for zero-stride vertex attributes, because
             * it may use a better memory placement than stream_uploader.
             * The reason is that zero-stride attributes can be fetched many
             * times (thousands of times), so a better placement is going to
             * perform better.
             *
             * Upload the maximum possible size, which is 4x GLdouble = 32.
             */
            u_upload_data(st->can_bind_const_buffer_as_vertex ?
                             st->pipe->const_uploader :
                             st->pipe->stream_uploader,
                          0, size, alignment, ptr,
                          &vbuffer[bufidx].buffer_offset,
                          &vbuffer[bufidx].buffer.resource);
            unref_buffers |= 1u << bufidx;
         } else {
            assert(attrib->Ptr);
            vbuffer[bufidx].buffer.user = attrib->Ptr;
            vbuffer[bufidx].is_user_buffer = true;
            vbuffer[bufidx].buffer_offset = 0;

            if (!binding->InstanceDivisor)
               st->draw_needs_minmax_index = true;
         }
      }

      /* common-case setup */
      vbuffer[bufidx].stride = stride; /* in bytes */

      src_format = st_pipe_vertex_format(attrib);

      init_velement_lowered(vp, velements, 0, src_format,
                            binding->InstanceDivisor, bufidx,
                            attrib->Size, attrib->Doubles, &attr);
   }

   if (!ctx->Const.AllowMappedBuffersDuringExecution) {
      u_upload_unmap(st->pipe->stream_uploader);
   }

   set_vertex_attribs(st, vbuffer, num_vbuffers, velements, num_inputs);

   /* Unreference uploaded zero-stride vertex buffers. */
   while (unref_buffers) {
      unsigned i = u_bit_scan(&unref_buffers);
      pipe_resource_reference(&vbuffer[i].buffer.resource, NULL);
   }
}

void st_update_array(struct st_context *st)
{
   struct gl_context *ctx = st->ctx;
   const struct gl_vertex_array *arrays = ctx->Array._DrawArrays;
   const struct st_vertex_program *vp;
   unsigned num_inputs;

   st->vertex_array_out_of_memory = FALSE;
   st->draw_needs_minmax_index = false;

   /* No drawing has been done yet, so do nothing. */
   if (!arrays)
      return;

   /* vertex program validation must be done before this */
   vp = st->vp;
   num_inputs = st->vp_variant->num_inputs;

   if (is_interleaved_arrays(vp, arrays, num_inputs))
      setup_interleaved_attribs(st, vp, arrays, num_inputs);
   else
      setup_non_interleaved_attribs(st, vp, arrays, num_inputs);
}
