/*
 * Copyright © 2020 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

/* Draw function marshalling for glthread.
 *
 * The purpose of these glDraw wrappers is to upload non-VBO vertex and
 * index data, so that glthread doesn't have to execute synchronously.
 */

#include "c99_alloca.h"

#include "main/glthread_marshal.h"
#include "main/dispatch.h"
#include "main/varray.h"

static inline unsigned
get_index_size(GLenum type)
{
   /* GL_UNSIGNED_BYTE  - GL_UNSIGNED_BYTE = 0
    * GL_UNSIGNED_SHORT - GL_UNSIGNED_BYTE = 2
    * GL_UNSIGNED_INT   - GL_UNSIGNED_BYTE = 4
    *
    * Divide by 2 to get n=0,1,2, then the index size is: 1 << n
    */
   return 1 << ((type - GL_UNSIGNED_BYTE) >> 1);
}

static inline bool
is_index_type_valid(GLenum type)
{
   /* GL_UNSIGNED_BYTE  = 0x1401
    * GL_UNSIGNED_SHORT = 0x1403
    * GL_UNSIGNED_INT   = 0x1405
    *
    * The trick is that bit 1 and bit 2 mean USHORT and UINT, respectively.
    * After clearing those two bits (with ~6), we should get UBYTE.
    * Both bits can't be set, because the enum would be greater than UINT.
    */
   return type <= GL_UNSIGNED_INT && (type & ~6) == GL_UNSIGNED_BYTE;
}

static ALWAYS_INLINE struct gl_buffer_object *
upload_indices(struct gl_context *ctx, unsigned count, unsigned index_size,
               const GLvoid **indices)
{
   struct gl_buffer_object *upload_buffer = NULL;
   unsigned upload_offset = 0;

   assert(count);

   _mesa_glthread_upload(ctx, *indices, index_size * count,
                         &upload_offset, &upload_buffer, NULL);
   assert(upload_buffer);
   *indices = (const GLvoid*)(intptr_t)upload_offset;

   return upload_buffer;
}

static ALWAYS_INLINE struct gl_buffer_object *
upload_multi_indices(struct gl_context *ctx, unsigned total_count,
                     unsigned index_size, unsigned draw_count,
                     const GLsizei *count, const GLvoid *const *indices,
                     const GLvoid **out_indices)
{
   struct gl_buffer_object *upload_buffer = NULL;
   unsigned upload_offset = 0;
   uint8_t *upload_ptr = NULL;

   assert(total_count);

   _mesa_glthread_upload(ctx, NULL, index_size * total_count,
                         &upload_offset, &upload_buffer, &upload_ptr);
   assert(upload_buffer);

   for (unsigned i = 0, offset = 0; i < draw_count; i++) {
      if (count[i] == 0)
         continue;

      unsigned size = count[i] * index_size;

      memcpy(upload_ptr + offset, indices[i], size);
      out_indices[i] = (const GLvoid*)(intptr_t)(upload_offset + offset);
      offset += size;
   }

   return upload_buffer;
}

static ALWAYS_INLINE bool
upload_vertices(struct gl_context *ctx, unsigned user_buffer_mask,
                unsigned start_vertex, unsigned num_vertices,
                unsigned start_instance, unsigned num_instances,
                struct glthread_attrib_binding *buffers)
{
   struct glthread_vao *vao = ctx->GLThread.CurrentVAO;
   unsigned attrib_mask_iter = vao->Enabled;
   unsigned num_buffers = 0;

   assert((num_vertices || !(user_buffer_mask & ~vao->NonZeroDivisorMask)) &&
          (num_instances || !(user_buffer_mask & vao->NonZeroDivisorMask)));

   if (unlikely(vao->BufferInterleaved & user_buffer_mask)) {
      /* Slower upload path where some buffers reference multiple attribs,
       * so we have to use 2 while loops instead of 1.
       */
      unsigned start_offset[VERT_ATTRIB_MAX];
      unsigned end_offset[VERT_ATTRIB_MAX];
      uint32_t buffer_mask = 0;

      while (attrib_mask_iter) {
         unsigned i = u_bit_scan(&attrib_mask_iter);
         unsigned binding_index = vao->Attrib[i].BufferIndex;

         if (!(user_buffer_mask & (1 << binding_index)))
            continue;

         unsigned stride = vao->Attrib[binding_index].Stride;
         unsigned instance_div = vao->Attrib[binding_index].Divisor;
         unsigned element_size = vao->Attrib[i].ElementSize;
         unsigned offset = vao->Attrib[i].RelativeOffset;
         unsigned size;

         if (instance_div) {
            /* Per-instance attrib. */

            /* Figure out how many instances we'll render given instance_div.  We
             * can't use the typical div_round_up() pattern because the CTS uses
             * instance_div = ~0 for a test, which overflows div_round_up()'s
             * addition.
             */
            unsigned count = num_instances / instance_div;
            if (count * instance_div != num_instances)
               count++;

            offset += stride * start_instance;
            size = stride * (count - 1) + element_size;
         } else {
            /* Per-vertex attrib. */
            offset += stride * start_vertex;
            size = stride * (num_vertices - 1) + element_size;
         }

         unsigned binding_index_bit = 1u << binding_index;

         /* Update upload offsets. */
         if (!(buffer_mask & binding_index_bit)) {
            start_offset[binding_index] = offset;
            end_offset[binding_index] = offset + size;
         } else {
            if (offset < start_offset[binding_index])
               start_offset[binding_index] = offset;
            if (offset + size > end_offset[binding_index])
               end_offset[binding_index] = offset + size;
         }

         buffer_mask |= binding_index_bit;
      }

      /* Upload buffers. */
      while (buffer_mask) {
         struct gl_buffer_object *upload_buffer = NULL;
         unsigned upload_offset = 0;
         unsigned start, end;

         unsigned binding_index = u_bit_scan(&buffer_mask);

         start = start_offset[binding_index];
         end = end_offset[binding_index];
         assert(start < end);

         const void *ptr = vao->Attrib[binding_index].Pointer;
         _mesa_glthread_upload(ctx, (uint8_t*)ptr + start,
                               end - start, &upload_offset,
                               &upload_buffer, NULL);
         assert(upload_buffer);

         buffers[num_buffers].buffer = upload_buffer;
         buffers[num_buffers].offset = upload_offset - start;
         buffers[num_buffers].original_pointer = ptr;
         num_buffers++;
      }

      return true;
   }

   /* Faster path where all attribs are separate. */
   while (attrib_mask_iter) {
      unsigned i = u_bit_scan(&attrib_mask_iter);
      unsigned binding_index = vao->Attrib[i].BufferIndex;

      if (!(user_buffer_mask & (1 << binding_index)))
         continue;

      struct gl_buffer_object *upload_buffer = NULL;
      unsigned upload_offset = 0;
      unsigned stride = vao->Attrib[binding_index].Stride;
      unsigned instance_div = vao->Attrib[binding_index].Divisor;
      unsigned element_size = vao->Attrib[i].ElementSize;
      unsigned offset = vao->Attrib[i].RelativeOffset;
      unsigned size;

      if (instance_div) {
         /* Per-instance attrib. */

         /* Figure out how many instances we'll render given instance_div.  We
          * can't use the typical div_round_up() pattern because the CTS uses
          * instance_div = ~0 for a test, which overflows div_round_up()'s
          * addition.
          */
         unsigned count = num_instances / instance_div;
         if (count * instance_div != num_instances)
            count++;

         offset += stride * start_instance;
         size = stride * (count - 1) + element_size;
      } else {
         /* Per-vertex attrib. */
         offset += stride * start_vertex;
         size = stride * (num_vertices - 1) + element_size;
      }

      const void *ptr = vao->Attrib[binding_index].Pointer;
      _mesa_glthread_upload(ctx, (uint8_t*)ptr + offset,
                            size, &upload_offset, &upload_buffer, NULL);
      assert(upload_buffer);

      buffers[num_buffers].buffer = upload_buffer;
      buffers[num_buffers].offset = upload_offset - offset;
      buffers[num_buffers].original_pointer = ptr;
      num_buffers++;
   }

   return true;
}

struct marshal_cmd_DrawArraysInstancedBaseInstance
{
   struct marshal_cmd_base cmd_base;
   GLenum mode;
   GLint first;
   GLsizei count;
   GLsizei instance_count;
   GLuint baseinstance;
   GLuint user_buffer_mask;
};

void
_mesa_unmarshal_DrawArraysInstancedBaseInstance(struct gl_context *ctx,
                                                const struct marshal_cmd_DrawArraysInstancedBaseInstance *cmd)
{
   const GLenum mode = cmd->mode;
   const GLint first = cmd->first;
   const GLsizei count = cmd->count;
   const GLsizei instance_count = cmd->instance_count;
   const GLuint baseinstance = cmd->baseinstance;
   const GLuint user_buffer_mask = cmd->user_buffer_mask;
   const struct glthread_attrib_binding *buffers =
      (const struct glthread_attrib_binding *)(cmd + 1);

   /* Bind uploaded buffers if needed. */
   if (user_buffer_mask) {
      _mesa_InternalBindVertexBuffers(ctx, buffers, user_buffer_mask,
                                      false);
   }

   CALL_DrawArraysInstancedBaseInstance(ctx->CurrentServerDispatch,
                                        (mode, first, count, instance_count,
                                         baseinstance));

   /* Restore states. */
   if (user_buffer_mask) {
      _mesa_InternalBindVertexBuffers(ctx, buffers, user_buffer_mask,
                                      true);
   }
}

static ALWAYS_INLINE void
draw_arrays_async(struct gl_context *ctx, GLenum mode, GLint first,
                  GLsizei count, GLsizei instance_count, GLuint baseinstance,
                  unsigned user_buffer_mask,
                  const struct glthread_attrib_binding *buffers)
{
   int buffers_size = util_bitcount(user_buffer_mask) * sizeof(buffers[0]);
   int cmd_size = sizeof(struct marshal_cmd_DrawArraysInstancedBaseInstance) +
                  buffers_size;
   struct marshal_cmd_DrawArraysInstancedBaseInstance *cmd;

   cmd = _mesa_glthread_allocate_command(ctx, DISPATCH_CMD_DrawArraysInstancedBaseInstance,
                                         cmd_size);
   cmd->mode = mode;
   cmd->first = first;
   cmd->count = count;
   cmd->instance_count = instance_count;
   cmd->baseinstance = baseinstance;
   cmd->user_buffer_mask = user_buffer_mask;

   if (user_buffer_mask)
      memcpy(cmd + 1, buffers, buffers_size);
}

static ALWAYS_INLINE void
draw_arrays(GLenum mode, GLint first, GLsizei count, GLsizei instance_count,
            GLuint baseinstance, bool compiled_into_dlist)
{
   GET_CURRENT_CONTEXT(ctx);

   struct glthread_vao *vao = ctx->GLThread.CurrentVAO;
   unsigned user_buffer_mask = vao->UserPointerMask & vao->BufferEnabled;

   if (compiled_into_dlist && ctx->GLThread.inside_dlist) {
      _mesa_glthread_finish_before(ctx, "DrawArrays");
      /* Use the function that's compiled into a display list. */
      CALL_DrawArrays(ctx->CurrentServerDispatch, (mode, first, count));
      return;
   }

   /* Fast path when nothing needs to be done.
    *
    * This is also an error path. Zero counts should still call the driver
    * for possible GL errors.
    */
   if (ctx->API == API_OPENGL_CORE || !user_buffer_mask ||
       count <= 0 || instance_count <= 0) {
      draw_arrays_async(ctx, mode, first, count, instance_count, baseinstance,
                        0, NULL);
      return;
   }

   /* Upload and draw. */
   struct glthread_attrib_binding buffers[VERT_ATTRIB_MAX];
   if (!ctx->GLThread.SupportsNonVBOUploads ||
       !upload_vertices(ctx, user_buffer_mask, first, count, baseinstance,
                        instance_count, buffers)) {
      _mesa_glthread_finish_before(ctx, "DrawArrays");
      CALL_DrawArraysInstancedBaseInstance(ctx->CurrentServerDispatch,
                                           (mode, first, count, instance_count,
                                            baseinstance));
      return;
   }

   draw_arrays_async(ctx, mode, first, count, instance_count, baseinstance,
                     user_buffer_mask, buffers);
}

struct marshal_cmd_MultiDrawArrays
{
   struct marshal_cmd_base cmd_base;
   GLenum mode;
   GLsizei draw_count;
   GLuint user_buffer_mask;
};

void
_mesa_unmarshal_MultiDrawArrays(struct gl_context *ctx,
                                const struct marshal_cmd_MultiDrawArrays *cmd)
{
   const GLenum mode = cmd->mode;
   const GLsizei draw_count = cmd->draw_count;
   const GLuint user_buffer_mask = cmd->user_buffer_mask;

   const char *variable_data = (const char *)(cmd + 1);
   const GLint *first = (GLint *)variable_data;
   variable_data += sizeof(GLint) * draw_count;
   const GLsizei *count = (GLsizei *)variable_data;
   variable_data += sizeof(GLsizei) * draw_count;
   const struct glthread_attrib_binding *buffers =
      (const struct glthread_attrib_binding *)variable_data;

   /* Bind uploaded buffers if needed. */
   if (user_buffer_mask) {
      _mesa_InternalBindVertexBuffers(ctx, buffers, user_buffer_mask,
                                      false);
   }

   CALL_MultiDrawArrays(ctx->CurrentServerDispatch,
                        (mode, first, count, draw_count));

   /* Restore states. */
   if (user_buffer_mask) {
      _mesa_InternalBindVertexBuffers(ctx, buffers, user_buffer_mask,
                                      true);
   }
}

static ALWAYS_INLINE void
multi_draw_arrays_async(struct gl_context *ctx, GLenum mode,
                        const GLint *first, const GLsizei *count,
                        GLsizei draw_count, unsigned user_buffer_mask,
                        const struct glthread_attrib_binding *buffers)
{
   int first_size = sizeof(GLint) * draw_count;
   int count_size = sizeof(GLsizei) * draw_count;
   int buffers_size = util_bitcount(user_buffer_mask) * sizeof(buffers[0]);
   int cmd_size = sizeof(struct marshal_cmd_MultiDrawArrays) +
                  first_size + count_size + buffers_size;
   struct marshal_cmd_MultiDrawArrays *cmd;

   cmd = _mesa_glthread_allocate_command(ctx, DISPATCH_CMD_MultiDrawArrays,
                                         cmd_size);
   cmd->mode = mode;
   cmd->draw_count = draw_count;
   cmd->user_buffer_mask = user_buffer_mask;

   char *variable_data = (char*)(cmd + 1);
   memcpy(variable_data, first, first_size);
   variable_data += first_size;
   memcpy(variable_data, count, count_size);

   if (user_buffer_mask) {
      variable_data += count_size;
      memcpy(variable_data, buffers, buffers_size);
   }
}

void GLAPIENTRY
_mesa_marshal_MultiDrawArrays(GLenum mode, const GLint *first,
                              const GLsizei *count, GLsizei draw_count)
{
   GET_CURRENT_CONTEXT(ctx);

   struct glthread_vao *vao = ctx->GLThread.CurrentVAO;
   unsigned user_buffer_mask = vao->UserPointerMask & vao->BufferEnabled;

   if (ctx->GLThread.inside_dlist)
      goto sync;

   if (draw_count >= 0 &&
       (ctx->API == API_OPENGL_CORE || !user_buffer_mask)) {
      multi_draw_arrays_async(ctx, mode, first, count, draw_count, 0, NULL);
      return;
   }

   /* If the draw count is too high or negative, the queue can't be used. */
   if (!ctx->GLThread.SupportsNonVBOUploads ||
       draw_count < 0 || draw_count > MARSHAL_MAX_CMD_SIZE / 16)
      goto sync;

   unsigned min_index = ~0;
   unsigned max_index_exclusive = 0;

   for (unsigned i = 0; i < draw_count; i++) {
      GLsizei vertex_count = count[i];

      if (vertex_count < 0) {
         /* Just call the driver to set the error. */
         multi_draw_arrays_async(ctx, mode, first, count, draw_count, 0, NULL);
         return;
      }
      if (vertex_count == 0)
         continue;

      min_index = MIN2(min_index, first[i]);
      max_index_exclusive = MAX2(max_index_exclusive, first[i] + vertex_count);
   }

   unsigned num_vertices = max_index_exclusive - min_index;
   if (num_vertices == 0) {
      /* Nothing to do, but call the driver to set possible GL errors. */
      multi_draw_arrays_async(ctx, mode, first, count, draw_count, 0, NULL);
      return;
   }

   /* Upload and draw. */
   struct glthread_attrib_binding buffers[VERT_ATTRIB_MAX];
   if (!upload_vertices(ctx, user_buffer_mask, min_index, num_vertices,
                        0, 1, buffers))
      goto sync;

   multi_draw_arrays_async(ctx, mode, first, count, draw_count,
                           user_buffer_mask, buffers);
   return;

sync:
   _mesa_glthread_finish_before(ctx, "MultiDrawArrays");
   CALL_MultiDrawArrays(ctx->CurrentServerDispatch,
                        (mode, first, count, draw_count));
}

struct marshal_cmd_DrawElementsInstancedBaseVertexBaseInstance
{
   struct marshal_cmd_base cmd_base;
   bool index_bounds_valid;
   GLenum mode;
   GLenum type;
   GLsizei count;
   GLsizei instance_count;
   GLint basevertex;
   GLuint baseinstance;
   GLuint min_index;
   GLuint max_index;
   GLuint user_buffer_mask;
   const GLvoid *indices;
   struct gl_buffer_object *index_buffer;
};

void
_mesa_unmarshal_DrawElementsInstancedBaseVertexBaseInstance(struct gl_context *ctx,
                                                            const struct marshal_cmd_DrawElementsInstancedBaseVertexBaseInstance *cmd)
{
   const GLenum mode = cmd->mode;
   const GLsizei count = cmd->count;
   const GLenum type = cmd->type;
   const GLvoid *indices = cmd->indices;
   const GLsizei instance_count = cmd->instance_count;
   const GLint basevertex = cmd->basevertex;
   const GLuint baseinstance = cmd->baseinstance;
   const GLuint min_index = cmd->min_index;
   const GLuint max_index = cmd->max_index;
   const GLuint user_buffer_mask = cmd->user_buffer_mask;
   struct gl_buffer_object *index_buffer = cmd->index_buffer;
   const struct glthread_attrib_binding *buffers =
      (const struct glthread_attrib_binding *)(cmd + 1);

   /* Bind uploaded buffers if needed. */
   if (user_buffer_mask) {
      _mesa_InternalBindVertexBuffers(ctx, buffers, user_buffer_mask,
                                      false);
   }
   if (index_buffer) {
      _mesa_InternalBindElementBuffer(ctx, index_buffer);
   }

   /* Draw. */
   if (cmd->index_bounds_valid && instance_count == 1 && baseinstance == 0) {
      CALL_DrawRangeElementsBaseVertex(ctx->CurrentServerDispatch,
                                       (mode, min_index, max_index, count,
                                        type, indices, basevertex));
   } else {
      CALL_DrawElementsInstancedBaseVertexBaseInstance(ctx->CurrentServerDispatch,
                                                       (mode, count, type, indices,
                                                        instance_count, basevertex,
                                                        baseinstance));
   }

   /* Restore states. */
   if (index_buffer) {
      _mesa_InternalBindElementBuffer(ctx, NULL);
   }
   if (user_buffer_mask) {
      _mesa_InternalBindVertexBuffers(ctx, buffers, user_buffer_mask,
                                      true);
   }
}

static ALWAYS_INLINE void
draw_elements_async(struct gl_context *ctx, GLenum mode, GLsizei count,
                    GLenum type, const GLvoid *indices, GLsizei instance_count,
                    GLint basevertex, GLuint baseinstance,
                    bool index_bounds_valid, GLuint min_index, GLuint max_index,
                    struct gl_buffer_object *index_buffer,
                    unsigned user_buffer_mask,
                    const struct glthread_attrib_binding *buffers)
{
   int buffers_size = util_bitcount(user_buffer_mask) * sizeof(buffers[0]);
   int cmd_size = sizeof(struct marshal_cmd_DrawElementsInstancedBaseVertexBaseInstance) +
                  buffers_size;
   struct marshal_cmd_DrawElementsInstancedBaseVertexBaseInstance *cmd;

   cmd = _mesa_glthread_allocate_command(ctx, DISPATCH_CMD_DrawElementsInstancedBaseVertexBaseInstance, cmd_size);
   cmd->mode = mode;
   cmd->count = count;
   cmd->type = type;
   cmd->indices = indices;
   cmd->instance_count = instance_count;
   cmd->basevertex = basevertex;
   cmd->baseinstance = baseinstance;
   cmd->min_index = min_index;
   cmd->max_index = max_index;
   cmd->user_buffer_mask = user_buffer_mask;
   cmd->index_bounds_valid = index_bounds_valid;
   cmd->index_buffer = index_buffer;

   if (user_buffer_mask)
      memcpy(cmd + 1, buffers, buffers_size);
}

static void
draw_elements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices,
              GLsizei instance_count, GLint basevertex, GLuint baseinstance,
              bool index_bounds_valid, GLuint min_index, GLuint max_index,
              bool compiled_into_dlist)
{
   GET_CURRENT_CONTEXT(ctx);

   struct glthread_vao *vao = ctx->GLThread.CurrentVAO;
   unsigned user_buffer_mask = vao->UserPointerMask & vao->BufferEnabled;
   bool has_user_indices = vao->CurrentElementBufferName == 0;

   if (compiled_into_dlist && ctx->GLThread.inside_dlist)
      goto sync;

   /* Fast path when nothing needs to be done.
    *
    * This is also an error path. Zero counts should still call the driver
    * for possible GL errors.
    */
   if (ctx->API == API_OPENGL_CORE ||
       count <= 0 || instance_count <= 0 || max_index < min_index ||
       !is_index_type_valid(type) ||
       (!user_buffer_mask && !has_user_indices)) {
      draw_elements_async(ctx, mode, count, type, indices, instance_count,
                          basevertex, baseinstance, index_bounds_valid,
                          min_index, max_index, 0, 0, NULL);
      return;
   }

   if (!ctx->GLThread.SupportsNonVBOUploads)
      goto sync;

   bool need_index_bounds = user_buffer_mask & ~vao->NonZeroDivisorMask;
   unsigned index_size = get_index_size(type);

   if (need_index_bounds && !index_bounds_valid) {
      /* Sync if indices come from a buffer and vertices come from memory
       * and index bounds are not valid.
       *
       * We would have to map the indices to compute the index bounds, and
       * for that we would have to sync anyway.
       */
      if (!has_user_indices)
         goto sync;

      /* Compute the index bounds. */
      min_index = ~0;
      max_index = 0;
      vbo_get_minmax_index_mapped(count, index_size,
                                  ctx->GLThread._RestartIndex[index_size - 1],
                                  ctx->GLThread._PrimitiveRestart, indices,
                                  &min_index, &max_index);
      index_bounds_valid = true;
   }

   unsigned start_vertex = min_index + basevertex;
   unsigned num_vertices = max_index + 1 - min_index;

   /* If there is too much data to upload, sync and let the driver unroll
    * indices. */
   if (util_is_vbo_upload_ratio_too_large(count, num_vertices))
      goto sync;

   struct glthread_attrib_binding buffers[VERT_ATTRIB_MAX];
   if (user_buffer_mask &&
       !upload_vertices(ctx, user_buffer_mask, start_vertex, num_vertices,
                        baseinstance, instance_count, buffers))
      goto sync;

   /* Upload indices. */
   struct gl_buffer_object *index_buffer = NULL;
   if (has_user_indices)
      index_buffer = upload_indices(ctx, count, index_size, &indices);

   /* Draw asynchronously. */
   draw_elements_async(ctx, mode, count, type, indices, instance_count,
                       basevertex, baseinstance, index_bounds_valid,
                       min_index, max_index, index_buffer,
                       user_buffer_mask, buffers);
   return;

sync:
   _mesa_glthread_finish_before(ctx, "DrawElements");

   if (compiled_into_dlist && ctx->GLThread.inside_dlist) {
      /* Only use the ones that are compiled into display lists. */
      if (basevertex) {
         CALL_DrawElementsBaseVertex(ctx->CurrentServerDispatch,
                                     (mode, count, type, indices, basevertex));
      } else if (index_bounds_valid) {
         CALL_DrawRangeElements(ctx->CurrentServerDispatch,
                                (mode, min_index, max_index, count, type, indices));
      } else {
         CALL_DrawElements(ctx->CurrentServerDispatch, (mode, count, type, indices));
      }
   } else if (index_bounds_valid && instance_count == 1 && baseinstance == 0) {
      CALL_DrawRangeElementsBaseVertex(ctx->CurrentServerDispatch,
                                       (mode, min_index, max_index, count,
                                        type, indices, basevertex));
   } else {
      CALL_DrawElementsInstancedBaseVertexBaseInstance(ctx->CurrentServerDispatch,
                                                       (mode, count, type, indices,
                                                        instance_count, basevertex,
                                                        baseinstance));
   }
}

struct marshal_cmd_MultiDrawElementsBaseVertex
{
   struct marshal_cmd_base cmd_base;
   bool has_base_vertex;
   GLenum mode;
   GLenum type;
   GLsizei draw_count;
   GLuint user_buffer_mask;
   struct gl_buffer_object *index_buffer;
};

void
_mesa_unmarshal_MultiDrawElementsBaseVertex(struct gl_context *ctx,
                                            const struct marshal_cmd_MultiDrawElementsBaseVertex *cmd)
{
   const GLenum mode = cmd->mode;
   const GLenum type = cmd->type;
   const GLsizei draw_count = cmd->draw_count;
   const GLuint user_buffer_mask = cmd->user_buffer_mask;
   struct gl_buffer_object *index_buffer = cmd->index_buffer;
   const bool has_base_vertex = cmd->has_base_vertex;

   const char *variable_data = (const char *)(cmd + 1);
   const GLsizei *count = (GLsizei *)variable_data;
   variable_data += sizeof(GLsizei) * draw_count;
   const GLvoid *const *indices = (const GLvoid *const *)variable_data;
   variable_data += sizeof(const GLvoid *const *) * draw_count;
   const GLsizei *basevertex = NULL;
   if (has_base_vertex) {
      basevertex = (GLsizei *)variable_data;
      variable_data += sizeof(GLsizei) * draw_count;
   }
   const struct glthread_attrib_binding *buffers =
      (const struct glthread_attrib_binding *)variable_data;

   /* Bind uploaded buffers if needed. */
   if (user_buffer_mask) {
      _mesa_InternalBindVertexBuffers(ctx, buffers, user_buffer_mask,
                                      false);
   }
   if (index_buffer) {
      _mesa_InternalBindElementBuffer(ctx, index_buffer);
   }

   /* Draw. */
   if (has_base_vertex) {
      CALL_MultiDrawElementsBaseVertex(ctx->CurrentServerDispatch,
                                       (mode, count, type, indices, draw_count,
                                        basevertex));
   } else {
      CALL_MultiDrawElementsEXT(ctx->CurrentServerDispatch,
                                (mode, count, type, indices, draw_count));
   }

   /* Restore states. */
   if (index_buffer) {
      _mesa_InternalBindElementBuffer(ctx, NULL);
   }
   if (user_buffer_mask) {
      _mesa_InternalBindVertexBuffers(ctx, buffers, user_buffer_mask,
                                      true);
   }
}

static ALWAYS_INLINE void
multi_draw_elements_async(struct gl_context *ctx, GLenum mode,
                          const GLsizei *count, GLenum type,
                          const GLvoid *const *indices, GLsizei draw_count,
                          const GLsizei *basevertex,
                          struct gl_buffer_object *index_buffer,
                          unsigned user_buffer_mask,
                          const struct glthread_attrib_binding *buffers)
{
   int count_size = sizeof(GLsizei) * draw_count;
   int indices_size = sizeof(indices[0]) * draw_count;
   int basevertex_size = basevertex ? sizeof(GLsizei) * draw_count : 0;
   int buffers_size = util_bitcount(user_buffer_mask) * sizeof(buffers[0]);
   int cmd_size = sizeof(struct marshal_cmd_MultiDrawElementsBaseVertex) +
                  count_size + indices_size + basevertex_size + buffers_size;
   struct marshal_cmd_MultiDrawElementsBaseVertex *cmd;

   cmd = _mesa_glthread_allocate_command(ctx, DISPATCH_CMD_MultiDrawElementsBaseVertex, cmd_size);
   cmd->mode = mode;
   cmd->type = type;
   cmd->draw_count = draw_count;
   cmd->user_buffer_mask = user_buffer_mask;
   cmd->index_buffer = index_buffer;
   cmd->has_base_vertex = basevertex != NULL;

   char *variable_data = (char*)(cmd + 1);
   memcpy(variable_data, count, count_size);
   variable_data += count_size;
   memcpy(variable_data, indices, indices_size);
   variable_data += indices_size;

   if (basevertex) {
      memcpy(variable_data, basevertex, basevertex_size);
      variable_data += basevertex_size;
   }

   if (user_buffer_mask)
      memcpy(variable_data, buffers, buffers_size);
}

void GLAPIENTRY
_mesa_marshal_MultiDrawElementsBaseVertex(GLenum mode, const GLsizei *count,
                                          GLenum type,
                                          const GLvoid *const *indices,
                                          GLsizei draw_count,
                                          const GLsizei *basevertex)
{
   GET_CURRENT_CONTEXT(ctx);

   struct glthread_vao *vao = ctx->GLThread.CurrentVAO;
   unsigned user_buffer_mask = vao->UserPointerMask & vao->BufferEnabled;
   bool has_user_indices = vao->CurrentElementBufferName == 0;

   if (ctx->GLThread.inside_dlist)
      goto sync;

   /* Fast path when nothing needs to be done. */
   if (draw_count >= 0 &&
       (ctx->API == API_OPENGL_CORE ||
        !is_index_type_valid(type) ||
        (!user_buffer_mask && !has_user_indices))) {
      multi_draw_elements_async(ctx, mode, count, type, indices, draw_count,
                                basevertex, 0, 0, NULL);
      return;
   }

   bool need_index_bounds = user_buffer_mask & ~vao->NonZeroDivisorMask;

   /* If the draw count is too high or negative, the queue can't be used.
    *
    * Sync if indices come from a buffer and vertices come from memory
    * and index bounds are not valid. We would have to map the indices
    * to compute the index bounds, and for that we would have to sync anyway.
    */
   if (!ctx->GLThread.SupportsNonVBOUploads ||
       draw_count < 0 || draw_count > MARSHAL_MAX_CMD_SIZE / 32 ||
       (need_index_bounds && !has_user_indices))
      goto sync;

   unsigned index_size = get_index_size(type);
   unsigned min_index = ~0;
   unsigned max_index = 0;
   unsigned total_count = 0;
   unsigned num_vertices = 0;

   /* This is always true if there is per-vertex data that needs to be
    * uploaded.
    */
   if (need_index_bounds) {
      /* Compute the index bounds. */
      for (unsigned i = 0; i < draw_count; i++) {
         GLsizei vertex_count = count[i];

         if (vertex_count < 0) {
            /* Just call the driver to set the error. */
            multi_draw_elements_async(ctx, mode, count, type, indices, draw_count,
                                      basevertex, 0, 0, NULL);
            return;
         }
         if (vertex_count == 0)
            continue;

         unsigned min = ~0, max = 0;
         vbo_get_minmax_index_mapped(vertex_count, index_size,
                                     ctx->GLThread._RestartIndex[index_size - 1],
                                     ctx->GLThread._PrimitiveRestart, indices[i],
                                     &min, &max);
         if (basevertex) {
            min += basevertex[i];
            max += basevertex[i];
         }
         min_index = MIN2(min_index, min);
         max_index = MAX2(max_index, max);
         total_count += vertex_count;
      }

      num_vertices = max_index + 1 - min_index;

      if (total_count == 0 || num_vertices == 0) {
         /* Nothing to do, but call the driver to set possible GL errors. */
         multi_draw_elements_async(ctx, mode, count, type, indices, draw_count,
                                   basevertex, 0, 0, NULL);
         return;
      }

      /* If there is too much data to upload, sync and let the driver unroll
       * indices. */
      if (util_is_vbo_upload_ratio_too_large(total_count, num_vertices))
         goto sync;
   } else if (has_user_indices) {
      /* Only compute total_count for the upload of indices. */
      for (unsigned i = 0; i < draw_count; i++) {
         GLsizei vertex_count = count[i];

         if (vertex_count < 0) {
            /* Just call the driver to set the error. */
            multi_draw_elements_async(ctx, mode, count, type, indices, draw_count,
                                      basevertex, 0, 0, NULL);
            return;
         }
         if (vertex_count == 0)
            continue;

         total_count += vertex_count;
      }

      if (total_count == 0) {
         /* Nothing to do, but call the driver to set possible GL errors. */
         multi_draw_elements_async(ctx, mode, count, type, indices, draw_count,
                                   basevertex, 0, 0, NULL);
         return;
      }
   }

   /* Upload vertices. */
   struct glthread_attrib_binding buffers[VERT_ATTRIB_MAX];
   if (user_buffer_mask &&
       !upload_vertices(ctx, user_buffer_mask, min_index, num_vertices,
                        0, 1, buffers))
      goto sync;

   /* Upload indices. */
   struct gl_buffer_object *index_buffer = NULL;
   if (has_user_indices) {
      const GLvoid **out_indices = alloca(sizeof(indices[0]) * draw_count);

      index_buffer = upload_multi_indices(ctx, total_count, index_size,
                                          draw_count, count, indices,
                                          out_indices);
      indices = out_indices;
   }

   /* Draw asynchronously. */
   multi_draw_elements_async(ctx, mode, count, type, indices, draw_count,
                             basevertex, index_buffer, user_buffer_mask,
                             buffers);
   return;

sync:
   _mesa_glthread_finish_before(ctx, "DrawElements");

   if (basevertex) {
      CALL_MultiDrawElementsBaseVertex(ctx->CurrentServerDispatch,
                                       (mode, count, type, indices, draw_count,
                                        basevertex));
   } else {
      CALL_MultiDrawElementsEXT(ctx->CurrentServerDispatch,
                                (mode, count, type, indices, draw_count));
   }
}

void GLAPIENTRY
_mesa_marshal_DrawArrays(GLenum mode, GLint first, GLsizei count)
{
   draw_arrays(mode, first, count, 1, 0, true);
}

void GLAPIENTRY
_mesa_marshal_DrawArraysInstancedARB(GLenum mode, GLint first, GLsizei count,
                                     GLsizei instance_count)
{
   draw_arrays(mode, first, count, instance_count, 0, false);
}

void GLAPIENTRY
_mesa_marshal_DrawArraysInstancedBaseInstance(GLenum mode, GLint first,
                                              GLsizei count, GLsizei instance_count,
                                              GLuint baseinstance)
{
   draw_arrays(mode, first, count, instance_count, baseinstance, false);
}

void GLAPIENTRY
_mesa_marshal_DrawElements(GLenum mode, GLsizei count, GLenum type,
                           const GLvoid *indices)
{
   draw_elements(mode, count, type, indices, 1, 0, 0, false, 0, 0, true);
}

void GLAPIENTRY
_mesa_marshal_DrawRangeElements(GLenum mode, GLuint start, GLuint end,
                                GLsizei count, GLenum type,
                                const GLvoid *indices)
{
   draw_elements(mode, count, type, indices, 1, 0, 0, true, start, end, true);
}

void GLAPIENTRY
_mesa_marshal_DrawElementsInstancedARB(GLenum mode, GLsizei count, GLenum type,
                                       const GLvoid *indices, GLsizei instance_count)
{
   draw_elements(mode, count, type, indices, instance_count, 0, 0, false, 0, 0, false);
}

void GLAPIENTRY
_mesa_marshal_DrawElementsBaseVertex(GLenum mode, GLsizei count, GLenum type,
                                     const GLvoid *indices, GLint basevertex)
{
   draw_elements(mode, count, type, indices, 1, basevertex, 0, false, 0, 0, true);
}

void GLAPIENTRY
_mesa_marshal_DrawRangeElementsBaseVertex(GLenum mode, GLuint start, GLuint end,
                                          GLsizei count, GLenum type,
                                          const GLvoid *indices, GLint basevertex)
{
   draw_elements(mode, count, type, indices, 1, basevertex, 0, true, start, end, false);
}

void GLAPIENTRY
_mesa_marshal_DrawElementsInstancedBaseVertex(GLenum mode, GLsizei count,
                                              GLenum type, const GLvoid *indices,
                                              GLsizei instance_count, GLint basevertex)
{
   draw_elements(mode, count, type, indices, instance_count, basevertex, 0, false, 0, 0, false);
}

void GLAPIENTRY
_mesa_marshal_DrawElementsInstancedBaseInstance(GLenum mode, GLsizei count,
                                                GLenum type, const GLvoid *indices,
                                                GLsizei instance_count, GLuint baseinstance)
{
   draw_elements(mode, count, type, indices, instance_count, 0, baseinstance, false, 0, 0, false);
}

void GLAPIENTRY
_mesa_marshal_DrawElementsInstancedBaseVertexBaseInstance(GLenum mode, GLsizei count,
                                                          GLenum type, const GLvoid *indices,
                                                          GLsizei instance_count, GLint basevertex,
                                                          GLuint baseinstance)
{
   draw_elements(mode, count, type, indices, instance_count, basevertex, baseinstance, false, 0, 0, false);
}

void GLAPIENTRY
_mesa_marshal_MultiDrawElementsEXT(GLenum mode, const GLsizei *count,
                                   GLenum type, const GLvoid *const *indices,
                                   GLsizei draw_count)
{
   _mesa_marshal_MultiDrawElementsBaseVertex(mode, count, type, indices,
                                             draw_count, NULL);
}

void
_mesa_unmarshal_DrawArrays(struct gl_context *ctx, const struct marshal_cmd_DrawArrays *cmd)
{
   unreachable("never used - DrawArraysInstancedBaseInstance is used instead");
}

void
_mesa_unmarshal_DrawArraysInstancedARB(struct gl_context *ctx, const struct marshal_cmd_DrawArraysInstancedARB *cmd)
{
   unreachable("never used - DrawArraysInstancedBaseInstance is used instead");
}

void
_mesa_unmarshal_DrawElements(struct gl_context *ctx, const struct marshal_cmd_DrawElements *cmd)
{
   unreachable("never used - DrawElementsInstancedBaseVertexBaseInstance is used instead");
}

void
_mesa_unmarshal_DrawRangeElements(struct gl_context *ctx, const struct marshal_cmd_DrawRangeElements *cmd)
{
   unreachable("never used - DrawElementsInstancedBaseVertexBaseInstance is used instead");
}

void
_mesa_unmarshal_DrawElementsInstancedARB(struct gl_context *ctx, const struct marshal_cmd_DrawElementsInstancedARB *cmd)
{
   unreachable("never used - DrawElementsInstancedBaseVertexBaseInstance is used instead");
}

void
_mesa_unmarshal_DrawElementsBaseVertex(struct gl_context *ctx, const struct marshal_cmd_DrawElementsBaseVertex *cmd)
{
   unreachable("never used - DrawElementsInstancedBaseVertexBaseInstance is used instead");
}

void
_mesa_unmarshal_DrawRangeElementsBaseVertex(struct gl_context *ctx, const struct marshal_cmd_DrawRangeElementsBaseVertex *cmd)
{
   unreachable("never used - DrawElementsInstancedBaseVertexBaseInstance is used instead");
}

void
_mesa_unmarshal_DrawElementsInstancedBaseVertex(struct gl_context *ctx, const struct marshal_cmd_DrawElementsInstancedBaseVertex *cmd)
{
   unreachable("never used - DrawElementsInstancedBaseVertexBaseInstance is used instead");
}

void
_mesa_unmarshal_DrawElementsInstancedBaseInstance(struct gl_context *ctx, const struct marshal_cmd_DrawElementsInstancedBaseInstance *cmd)
{
   unreachable("never used - DrawElementsInstancedBaseVertexBaseInstance is used instead");
}

void
_mesa_unmarshal_MultiDrawElementsEXT(struct gl_context *ctx, const struct marshal_cmd_MultiDrawElementsEXT *cmd)
{
   unreachable("never used - MultiDrawElementsBaseVertex is used instead");
}
