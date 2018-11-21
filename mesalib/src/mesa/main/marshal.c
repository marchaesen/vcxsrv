/*
 * Copyright © 2012 Intel Corporation
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

/** \file marshal.c
 *
 * Custom functions for marshalling GL calls from the main thread to a worker
 * thread when automatic code generation isn't appropriate.
 */

#include "main/enums.h"
#include "main/macros.h"
#include "marshal.h"
#include "dispatch.h"
#include "marshal_generated.h"

struct marshal_cmd_Flush
{
   struct marshal_cmd_base cmd_base;
};


void
_mesa_unmarshal_Flush(struct gl_context *ctx,
                      const struct marshal_cmd_Flush *cmd)
{
   CALL_Flush(ctx->CurrentServerDispatch, ());
}


void GLAPIENTRY
_mesa_marshal_Flush(void)
{
   GET_CURRENT_CONTEXT(ctx);
   struct marshal_cmd_Flush *cmd =
      _mesa_glthread_allocate_command(ctx, DISPATCH_CMD_Flush,
                                      sizeof(struct marshal_cmd_Flush));
   (void) cmd;
   _mesa_post_marshal_hook(ctx);

   /* Flush() needs to be handled specially.  In addition to telling the
    * background thread to flush, we need to ensure that our own buffer is
    * submitted to the background thread so that it will complete in a finite
    * amount of time.
    */
   _mesa_glthread_flush_batch(ctx);
}

/* Enable: marshalled asynchronously */
struct marshal_cmd_Enable
{
   struct marshal_cmd_base cmd_base;
   GLenum cap;
};

void
_mesa_unmarshal_Enable(struct gl_context *ctx,
                       const struct marshal_cmd_Enable *cmd)
{
   const GLenum cap = cmd->cap;
   CALL_Enable(ctx->CurrentServerDispatch, (cap));
}

void GLAPIENTRY
_mesa_marshal_Enable(GLenum cap)
{
   GET_CURRENT_CONTEXT(ctx);
   struct marshal_cmd_Enable *cmd;
   debug_print_marshal("Enable");

   if (cap == GL_DEBUG_OUTPUT_SYNCHRONOUS_ARB) {
      _mesa_glthread_finish(ctx);
      _mesa_glthread_restore_dispatch(ctx, "Enable(DEBUG_OUTPUT_SYNCHRONOUS)");
   } else {
      cmd = _mesa_glthread_allocate_command(ctx, DISPATCH_CMD_Enable,
                                            sizeof(*cmd));
      cmd->cap = cap;
      _mesa_post_marshal_hook(ctx);
      return;
   }

   _mesa_glthread_finish(ctx);
   debug_print_sync_fallback("Enable");
   CALL_Enable(ctx->CurrentServerDispatch, (cap));
}

struct marshal_cmd_ShaderSource
{
   struct marshal_cmd_base cmd_base;
   GLuint shader;
   GLsizei count;
   /* Followed by GLint length[count], then the contents of all strings,
    * concatenated.
    */
};


void
_mesa_unmarshal_ShaderSource(struct gl_context *ctx,
                             const struct marshal_cmd_ShaderSource *cmd)
{
   const GLint *cmd_length = (const GLint *) (cmd + 1);
   const GLchar *cmd_strings = (const GLchar *) (cmd_length + cmd->count);
   /* TODO: how to deal with malloc failure? */
   const GLchar * *string = malloc(cmd->count * sizeof(const GLchar *));
   int i;

   for (i = 0; i < cmd->count; ++i) {
      string[i] = cmd_strings;
      cmd_strings += cmd_length[i];
   }
   CALL_ShaderSource(ctx->CurrentServerDispatch,
                     (cmd->shader, cmd->count, string, cmd_length));
   free(string);
}


static size_t
measure_ShaderSource_strings(GLsizei count, const GLchar * const *string,
                             const GLint *length_in, GLint *length_out)
{
   int i;
   size_t total_string_length = 0;

   for (i = 0; i < count; ++i) {
      if (length_in == NULL || length_in[i] < 0) {
         if (string[i])
            length_out[i] = strlen(string[i]);
      } else {
         length_out[i] = length_in[i];
      }
      total_string_length += length_out[i];
   }
   return total_string_length;
}


void GLAPIENTRY
_mesa_marshal_ShaderSource(GLuint shader, GLsizei count,
                           const GLchar * const *string, const GLint *length)
{
   /* TODO: how to report an error if count < 0? */

   GET_CURRENT_CONTEXT(ctx);
   /* TODO: how to deal with malloc failure? */
   const size_t fixed_cmd_size = sizeof(struct marshal_cmd_ShaderSource);
   STATIC_ASSERT(sizeof(struct marshal_cmd_ShaderSource) % sizeof(GLint) == 0);
   size_t length_size = count * sizeof(GLint);
   GLint *length_tmp = malloc(length_size);
   size_t total_string_length =
      measure_ShaderSource_strings(count, string, length, length_tmp);
   size_t total_cmd_size = fixed_cmd_size + length_size + total_string_length;

   if (total_cmd_size <= MARSHAL_MAX_CMD_SIZE) {
      struct marshal_cmd_ShaderSource *cmd =
         _mesa_glthread_allocate_command(ctx, DISPATCH_CMD_ShaderSource,
                                         total_cmd_size);
      GLint *cmd_length = (GLint *) (cmd + 1);
      GLchar *cmd_strings = (GLchar *) (cmd_length + count);
      int i;

      cmd->shader = shader;
      cmd->count = count;
      memcpy(cmd_length, length_tmp, length_size);
      for (i = 0; i < count; ++i) {
         memcpy(cmd_strings, string[i], cmd_length[i]);
         cmd_strings += cmd_length[i];
      }
      _mesa_post_marshal_hook(ctx);
   } else {
      _mesa_glthread_finish(ctx);
      CALL_ShaderSource(ctx->CurrentServerDispatch,
                        (shader, count, string, length_tmp));
   }
   free(length_tmp);
}


/* BindBufferBase: marshalled asynchronously */
struct marshal_cmd_BindBufferBase
{
   struct marshal_cmd_base cmd_base;
   GLenum target;
   GLuint index;
   GLuint buffer;
};

/** Tracks the current bindings for the vertex array and index array buffers.
 *
 * This is part of what we need to enable glthread on compat-GL contexts that
 * happen to use VBOs, without also supporting the full tracking of VBO vs
 * user vertex array bindings per attribute on each vertex array for
 * determining what to upload at draw call time.
 *
 * Note that GL core makes it so that a buffer binding with an invalid handle
 * in the "buffer" parameter will throw an error, and then a
 * glVertexAttribPointer() that followsmight not end up pointing at a VBO.
 * However, in GL core the draw call would throw an error as well, so we don't
 * really care if our tracking is wrong for this case -- we never need to
 * marshal user data for draw calls, and the unmarshal will just generate an
 * error or not as appropriate.
 *
 * For compatibility GL, we do need to accurately know whether the draw call
 * on the unmarshal side will dereference a user pointer or load data from a
 * VBO per vertex.  That would make it seem like we need to track whether a
 * "buffer" is valid, so that we can know when an error will be generated
 * instead of updating the binding.  However, compat GL has the ridiculous
 * feature that if you pass a bad name, it just gens a buffer object for you,
 * so we escape without having to know if things are valid or not.
 */
static void
track_vbo_binding(struct gl_context *ctx, GLenum target, GLuint buffer)
{
   struct glthread_state *glthread = ctx->GLThread;

   switch (target) {
   case GL_ARRAY_BUFFER:
      glthread->vertex_array_is_vbo = (buffer != 0);
      break;
   case GL_ELEMENT_ARRAY_BUFFER:
      /* The current element array buffer binding is actually tracked in the
       * vertex array object instead of the context, so this would need to
       * change on vertex array object updates.
       */
      glthread->element_array_is_vbo = (buffer != 0);
      break;
   }
}


struct marshal_cmd_BindBuffer
{
   struct marshal_cmd_base cmd_base;
   GLenum target;
   GLuint buffer;
};

/**
 * This is just like the code-generated glBindBuffer() support, except that we
 * call track_vbo_binding().
 */
void
_mesa_unmarshal_BindBuffer(struct gl_context *ctx,
                           const struct marshal_cmd_BindBuffer *cmd)
{
   const GLenum target = cmd->target;
   const GLuint buffer = cmd->buffer;
   CALL_BindBuffer(ctx->CurrentServerDispatch, (target, buffer));
}
void GLAPIENTRY
_mesa_marshal_BindBuffer(GLenum target, GLuint buffer)
{
   GET_CURRENT_CONTEXT(ctx);
   size_t cmd_size = sizeof(struct marshal_cmd_BindBuffer);
   struct marshal_cmd_BindBuffer *cmd;
   debug_print_marshal("BindBuffer");

   track_vbo_binding(ctx, target, buffer);

   if (cmd_size <= MARSHAL_MAX_CMD_SIZE) {
      cmd = _mesa_glthread_allocate_command(ctx, DISPATCH_CMD_BindBuffer,
                                            cmd_size);
      cmd->target = target;
      cmd->buffer = buffer;
      _mesa_post_marshal_hook(ctx);
   } else {
      _mesa_glthread_finish(ctx);
      CALL_BindBuffer(ctx->CurrentServerDispatch, (target, buffer));
   }
}

/* BufferData: marshalled asynchronously */
struct marshal_cmd_BufferData
{
   struct marshal_cmd_base cmd_base;
   GLenum target;
   GLsizeiptr size;
   GLenum usage;
   bool data_null; /* If set, no data follows for "data" */
   /* Next size bytes are GLubyte data[size] */
};

void
_mesa_unmarshal_BufferData(struct gl_context *ctx,
                           const struct marshal_cmd_BufferData *cmd)
{
   const GLenum target = cmd->target;
   const GLsizeiptr size = cmd->size;
   const GLenum usage = cmd->usage;
   const void *data;

   if (cmd->data_null)
      data = NULL;
   else
      data = (const void *) (cmd + 1);

   CALL_BufferData(ctx->CurrentServerDispatch, (target, size, data, usage));
}

void GLAPIENTRY
_mesa_marshal_BufferData(GLenum target, GLsizeiptr size, const GLvoid * data,
                         GLenum usage)
{
   GET_CURRENT_CONTEXT(ctx);
   size_t cmd_size =
      sizeof(struct marshal_cmd_BufferData) + (data ? size : 0);
   debug_print_marshal("BufferData");

   if (unlikely(size < 0)) {
      _mesa_glthread_finish(ctx);
      _mesa_error(ctx, GL_INVALID_VALUE, "BufferData(size < 0)");
      return;
   }

   if (target != GL_EXTERNAL_VIRTUAL_MEMORY_BUFFER_AMD &&
       cmd_size <= MARSHAL_MAX_CMD_SIZE) {
      struct marshal_cmd_BufferData *cmd =
         _mesa_glthread_allocate_command(ctx, DISPATCH_CMD_BufferData,
                                         cmd_size);

      cmd->target = target;
      cmd->size = size;
      cmd->usage = usage;
      cmd->data_null = !data;
      if (data) {
         char *variable_data = (char *) (cmd + 1);
         memcpy(variable_data, data, size);
      }
      _mesa_post_marshal_hook(ctx);
   } else {
      _mesa_glthread_finish(ctx);
      CALL_BufferData(ctx->CurrentServerDispatch,
                      (target, size, data, usage));
   }
}

/* BufferSubData: marshalled asynchronously */
struct marshal_cmd_BufferSubData
{
   struct marshal_cmd_base cmd_base;
   GLenum target;
   GLintptr offset;
   GLsizeiptr size;
   /* Next size bytes are GLubyte data[size] */
};

void
_mesa_unmarshal_BufferSubData(struct gl_context *ctx,
                              const struct marshal_cmd_BufferSubData *cmd)
{
   const GLenum target = cmd->target;
   const GLintptr offset = cmd->offset;
   const GLsizeiptr size = cmd->size;
   const void *data = (const void *) (cmd + 1);

   CALL_BufferSubData(ctx->CurrentServerDispatch,
                      (target, offset, size, data));
}

void GLAPIENTRY
_mesa_marshal_BufferSubData(GLenum target, GLintptr offset, GLsizeiptr size,
                            const GLvoid * data)
{
   GET_CURRENT_CONTEXT(ctx);
   size_t cmd_size = sizeof(struct marshal_cmd_BufferSubData) + size;

   debug_print_marshal("BufferSubData");
   if (unlikely(size < 0)) {
      _mesa_glthread_finish(ctx);
      _mesa_error(ctx, GL_INVALID_VALUE, "BufferSubData(size < 0)");
      return;
   }

   if (target != GL_EXTERNAL_VIRTUAL_MEMORY_BUFFER_AMD &&
       cmd_size <= MARSHAL_MAX_CMD_SIZE) {
      struct marshal_cmd_BufferSubData *cmd =
         _mesa_glthread_allocate_command(ctx, DISPATCH_CMD_BufferSubData,
                                         cmd_size);
      cmd->target = target;
      cmd->offset = offset;
      cmd->size = size;
      char *variable_data = (char *) (cmd + 1);
      memcpy(variable_data, data, size);
      _mesa_post_marshal_hook(ctx);
   } else {
      _mesa_glthread_finish(ctx);
      CALL_BufferSubData(ctx->CurrentServerDispatch,
                         (target, offset, size, data));
   }
}

/* NamedBufferData: marshalled asynchronously */
struct marshal_cmd_NamedBufferData
{
   struct marshal_cmd_base cmd_base;
   GLuint name;
   GLsizei size;
   GLenum usage;
   bool data_null; /* If set, no data follows for "data" */
   /* Next size bytes are GLubyte data[size] */
};

void
_mesa_unmarshal_NamedBufferData(struct gl_context *ctx,
                                const struct marshal_cmd_NamedBufferData *cmd)
{
   const GLuint name = cmd->name;
   const GLsizei size = cmd->size;
   const GLenum usage = cmd->usage;
   const void *data;

   if (cmd->data_null)
      data = NULL;
   else
      data = (const void *) (cmd + 1);

   CALL_NamedBufferData(ctx->CurrentServerDispatch,
                        (name, size, data, usage));
}

void GLAPIENTRY
_mesa_marshal_NamedBufferData(GLuint buffer, GLsizeiptr size,
                              const GLvoid * data, GLenum usage)
{
   GET_CURRENT_CONTEXT(ctx);
   size_t cmd_size = sizeof(struct marshal_cmd_NamedBufferData) + (data ? size : 0);

   debug_print_marshal("NamedBufferData");
   if (unlikely(size < 0)) {
      _mesa_glthread_finish(ctx);
      _mesa_error(ctx, GL_INVALID_VALUE, "NamedBufferData(size < 0)");
      return;
   }

   if (buffer > 0 && cmd_size <= MARSHAL_MAX_CMD_SIZE) {
      struct marshal_cmd_NamedBufferData *cmd =
         _mesa_glthread_allocate_command(ctx, DISPATCH_CMD_NamedBufferData,
                                         cmd_size);
      cmd->name = buffer;
      cmd->size = size;
      cmd->usage = usage;
      cmd->data_null = !data;
      if (data) {
         char *variable_data = (char *) (cmd + 1);
         memcpy(variable_data, data, size);
      }
      _mesa_post_marshal_hook(ctx);
   } else {
      _mesa_glthread_finish(ctx);
      CALL_NamedBufferData(ctx->CurrentServerDispatch,
                           (buffer, size, data, usage));
   }
}

/* NamedBufferSubData: marshalled asynchronously */
struct marshal_cmd_NamedBufferSubData
{
   struct marshal_cmd_base cmd_base;
   GLuint name;
   GLintptr offset;
   GLsizei size;
   /* Next size bytes are GLubyte data[size] */
};

void
_mesa_unmarshal_NamedBufferSubData(struct gl_context *ctx,
                                   const struct marshal_cmd_NamedBufferSubData *cmd)
{
   const GLuint name = cmd->name;
   const GLintptr offset = cmd->offset;
   const GLsizei size = cmd->size;
   const void *data = (const void *) (cmd + 1);

   CALL_NamedBufferSubData(ctx->CurrentServerDispatch,
                           (name, offset, size, data));
}

void GLAPIENTRY
_mesa_marshal_NamedBufferSubData(GLuint buffer, GLintptr offset,
                                 GLsizeiptr size, const GLvoid * data)
{
   GET_CURRENT_CONTEXT(ctx);
   size_t cmd_size = sizeof(struct marshal_cmd_NamedBufferSubData) + size;

   debug_print_marshal("NamedBufferSubData");
   if (unlikely(size < 0)) {
      _mesa_glthread_finish(ctx);
      _mesa_error(ctx, GL_INVALID_VALUE, "NamedBufferSubData(size < 0)");
      return;
   }

   if (buffer > 0 && cmd_size <= MARSHAL_MAX_CMD_SIZE) {
      struct marshal_cmd_NamedBufferSubData *cmd =
         _mesa_glthread_allocate_command(ctx, DISPATCH_CMD_NamedBufferSubData,
                                         cmd_size);
      cmd->name = buffer;
      cmd->offset = offset;
      cmd->size = size;
      char *variable_data = (char *) (cmd + 1);
      memcpy(variable_data, data, size);
      _mesa_post_marshal_hook(ctx);
   } else {
      _mesa_glthread_finish(ctx);
      CALL_NamedBufferSubData(ctx->CurrentServerDispatch,
                              (buffer, offset, size, data));
   }
}

/* ClearBuffer* (all variants): marshalled asynchronously */
struct marshal_cmd_ClearBuffer
{
   struct marshal_cmd_base cmd_base;
   GLenum buffer;
   GLint drawbuffer;
};

void
_mesa_unmarshal_ClearBufferfv(struct gl_context *ctx,
                              const struct marshal_cmd_ClearBuffer *cmd)
{
   const GLenum buffer = cmd->buffer;
   const GLint drawbuffer = cmd->drawbuffer;
   const char *variable_data = (const char *) (cmd + 1);
   const GLfloat *value = (const GLfloat *) variable_data;

   CALL_ClearBufferfv(ctx->CurrentServerDispatch,
                      (buffer, drawbuffer, value));
}

void
_mesa_unmarshal_ClearBufferiv(struct gl_context *ctx,
                              const struct marshal_cmd_ClearBuffer *cmd)
{
   const GLenum buffer = cmd->buffer;
   const GLint drawbuffer = cmd->drawbuffer;
   const char *variable_data = (const char *) (cmd + 1);
   const GLint *value = (const GLint *) variable_data;

   CALL_ClearBufferiv(ctx->CurrentServerDispatch,
                      (buffer, drawbuffer, value));
}

void
_mesa_unmarshal_ClearBufferuiv(struct gl_context *ctx,
                               const struct marshal_cmd_ClearBuffer *cmd)
{
   const GLenum buffer = cmd->buffer;
   const GLint drawbuffer = cmd->drawbuffer;
   const char *variable_data = (const char *) (cmd + 1);
   const GLuint *value = (const GLuint *) variable_data;

   CALL_ClearBufferuiv(ctx->CurrentServerDispatch,
                       (buffer, drawbuffer, value));
}

void
_mesa_unmarshal_ClearBufferfi(struct gl_context *ctx,
                              const struct marshal_cmd_ClearBuffer *cmd)
{
   const GLenum buffer = cmd->buffer;
   const GLint drawbuffer = cmd->drawbuffer;
   const char *variable_data = (const char *) (cmd + 1);
   const GLfloat *depth = (const GLfloat *) variable_data;
   const GLint *stencil = (const GLint *) (variable_data + 4);

   CALL_ClearBufferfi(ctx->CurrentServerDispatch,
                      (buffer, drawbuffer, *depth, *stencil));
}

static inline size_t buffer_to_size(GLenum buffer)
{
   switch (buffer) {
   case GL_COLOR:
      return 4;
   case GL_DEPTH_STENCIL:
      return 2;
   case GL_STENCIL:
   case GL_DEPTH:
      return 1;
   default:
      return 0;
   }
}

static inline bool clear_buffer_add_command(struct gl_context *ctx, uint16_t id,
                                            GLenum buffer, GLint drawbuffer,
                                            const GLuint *value, size_t size)
{
   size_t cmd_size = sizeof(struct marshal_cmd_ClearBuffer) + 4 * size;
   if (cmd_size <= MARSHAL_MAX_CMD_SIZE) {
      struct marshal_cmd_ClearBuffer *cmd =
         _mesa_glthread_allocate_command(ctx, id,
                                         cmd_size);
      cmd->buffer = buffer;
      cmd->drawbuffer = drawbuffer;
      GLuint *variable_data = (GLuint *) (cmd + 1);
      if (size == 4)
         COPY_4V(variable_data,  value);
      else if (size == 2)
         COPY_2V(variable_data, value);
      else
         *variable_data = *value;

      _mesa_post_marshal_hook(ctx);
      return true;
   }

   return false;
}

void GLAPIENTRY
_mesa_marshal_ClearBufferfv(GLenum buffer, GLint drawbuffer,
                            const GLfloat *value)
{
   GET_CURRENT_CONTEXT(ctx);
   debug_print_marshal("ClearBufferfv");

   if (!(buffer == GL_DEPTH || buffer == GL_COLOR)) {
      _mesa_glthread_finish(ctx);

      /* Page 498 of the PDF, section '17.4.3.1 Clearing Individual Buffers'
       * of the OpenGL 4.5 spec states:
       *
       *    "An INVALID_ENUM error is generated by ClearBufferfv and
       *     ClearNamedFramebufferfv if buffer is not COLOR or DEPTH."
       */
      _mesa_error(ctx, GL_INVALID_ENUM, "glClearBufferfv(buffer=%s)",
                  _mesa_enum_to_string(buffer));
   }

   size_t size = buffer_to_size(buffer);
   if (!clear_buffer_add_command(ctx, DISPATCH_CMD_ClearBufferfv, buffer,
                                 drawbuffer, (GLuint *)value, size)) {
      debug_print_sync("ClearBufferfv");
      _mesa_glthread_finish(ctx);
      CALL_ClearBufferfv(ctx->CurrentServerDispatch,
                         (buffer, drawbuffer, value));
   }
}

void GLAPIENTRY
_mesa_marshal_ClearBufferiv(GLenum buffer, GLint drawbuffer,
                            const GLint *value)
{
   GET_CURRENT_CONTEXT(ctx);
   debug_print_marshal("ClearBufferiv");

   if (!(buffer == GL_STENCIL || buffer == GL_COLOR)) {
      _mesa_glthread_finish(ctx);

      /* Page 498 of the PDF, section '17.4.3.1 Clearing Individual Buffers'
       * of the OpenGL 4.5 spec states:
       *
       *    "An INVALID_ENUM error is generated by ClearBufferiv and
       *     ClearNamedFramebufferiv if buffer is not COLOR or STENCIL."
       */
      _mesa_error(ctx, GL_INVALID_ENUM, "glClearBufferiv(buffer=%s)",
                  _mesa_enum_to_string(buffer));
   }

   size_t size = buffer_to_size(buffer);
   if (!clear_buffer_add_command(ctx, DISPATCH_CMD_ClearBufferiv, buffer,
                                 drawbuffer, (GLuint *)value, size)) {
      debug_print_sync("ClearBufferiv");
      _mesa_glthread_finish(ctx);
      CALL_ClearBufferiv(ctx->CurrentServerDispatch,
                         (buffer, drawbuffer, value));
   }
}

void GLAPIENTRY
_mesa_marshal_ClearBufferuiv(GLenum buffer, GLint drawbuffer,
                             const GLuint *value)
{
   GET_CURRENT_CONTEXT(ctx);
   debug_print_marshal("ClearBufferuiv");

   if (buffer != GL_COLOR) {
      _mesa_glthread_finish(ctx);

      /* Page 498 of the PDF, section '17.4.3.1 Clearing Individual Buffers'
       * of the OpenGL 4.5 spec states:
       *
       *    "An INVALID_ENUM error is generated by ClearBufferuiv and
       *     ClearNamedFramebufferuiv if buffer is not COLOR."
       */
      _mesa_error(ctx, GL_INVALID_ENUM, "glClearBufferuiv(buffer=%s)",
                  _mesa_enum_to_string(buffer));
   }

   if (!clear_buffer_add_command(ctx, DISPATCH_CMD_ClearBufferuiv, buffer,
                                 drawbuffer, (GLuint *)value, 4)) {
      debug_print_sync("ClearBufferuiv");
      _mesa_glthread_finish(ctx);
      CALL_ClearBufferuiv(ctx->CurrentServerDispatch,
                         (buffer, drawbuffer, value));
   }
}

void GLAPIENTRY
_mesa_marshal_ClearBufferfi(GLenum buffer, GLint drawbuffer,
                            const GLfloat depth, const GLint stencil)
{
   GET_CURRENT_CONTEXT(ctx);
   debug_print_marshal("ClearBufferfi");

   if (buffer != GL_DEPTH_STENCIL) {
      _mesa_glthread_finish(ctx);

      /* Page 498 of the PDF, section '17.4.3.1 Clearing Individual Buffers'
       * of the OpenGL 4.5 spec states:
       *
       *    "An INVALID_ENUM error is generated by ClearBufferfi and
       *     ClearNamedFramebufferfi if buffer is not DEPTH_STENCIL."
       */
      _mesa_error(ctx, GL_INVALID_ENUM, "glClearBufferfi(buffer=%s)",
                  _mesa_enum_to_string(buffer));
   }

   fi_type value[2];
   value[0].f = depth;
   value[1].i = stencil;
   if (!clear_buffer_add_command(ctx, DISPATCH_CMD_ClearBufferfi, buffer,
                                 drawbuffer, (GLuint *)value, 2)) {
      debug_print_sync("ClearBufferfi");
      _mesa_glthread_finish(ctx);
      CALL_ClearBufferfi(ctx->CurrentServerDispatch,
                         (buffer, drawbuffer, depth, stencil));
   }
}
