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

/** \file marshal.h
 *
 * Declarations of functions related to marshalling GL calls from a client
 * thread to a server thread.
 */

#ifndef MARSHAL_H
#define MARSHAL_H

#include "main/glthread.h"
#include "main/context.h"
#include "main/macros.h"

struct marshal_cmd_base
{
   /**
    * Type of command.  See enum marshal_dispatch_cmd_id.
    */
   uint16_t cmd_id;

   /**
    * Size of command, in multiples of 4 bytes, including cmd_base.
    */
   uint16_t cmd_size;
};

static inline void *
_mesa_glthread_allocate_command(struct gl_context *ctx,
                                uint16_t cmd_id,
                                size_t size)
{
   struct glthread_state *glthread = ctx->GLThread;
   struct glthread_batch *next = &glthread->batches[glthread->next];
   struct marshal_cmd_base *cmd_base;
   const size_t aligned_size = ALIGN(size, 8);

   if (unlikely(next->used + size > MARSHAL_MAX_CMD_SIZE)) {
      _mesa_glthread_flush_batch(ctx);
      next = &glthread->batches[glthread->next];
   }

   cmd_base = (struct marshal_cmd_base *)&next->buffer[next->used];
   next->used += aligned_size;
   cmd_base->cmd_id = cmd_id;
   cmd_base->cmd_size = aligned_size;
   return cmd_base;
}

/**
 * Instead of conditionally handling marshaling previously-bound user vertex
 * array data in draw calls (deprecated and removed in GL core), we just
 * disable threading at the point where the user sets a user vertex array.
 */
static inline bool
_mesa_glthread_is_non_vbo_vertex_attrib_pointer(const struct gl_context *ctx)
{
   struct glthread_state *glthread = ctx->GLThread;

   return ctx->API != API_OPENGL_CORE && !glthread->vertex_array_is_vbo;
}

/**
 * Instead of conditionally handling marshaling immediate index data in draw
 * calls (deprecated and removed in GL core), we just disable threading.
 */
static inline bool
_mesa_glthread_is_non_vbo_draw_elements(const struct gl_context *ctx)
{
   struct glthread_state *glthread = ctx->GLThread;

   return ctx->API != API_OPENGL_CORE && !glthread->element_array_is_vbo;
}

#define DEBUG_MARSHAL_PRINT_CALLS 0

/**
 * This is printed when we have fallen back to a sync. This can happen when
 * MARSHAL_MAX_CMD_SIZE is exceeded.
 */
static inline void
debug_print_sync_fallback(const char *func)
{
#if DEBUG_MARSHAL_PRINT_CALLS
   printf("fallback to sync: %s\n", func);
#endif
}


static inline void
debug_print_sync(const char *func)
{
#if DEBUG_MARSHAL_PRINT_CALLS
   printf("sync: %s\n", func);
#endif
}

static inline void
debug_print_marshal(const char *func)
{
#if DEBUG_MARSHAL_PRINT_CALLS
   printf("marshal: %s\n", func);
#endif
}

static inline void
debug_print_unmarshal(const char *func)
{
#if DEBUG_MARSHAL_PRINT_CALLS
   printf("unmarshal: %s\n", func);
#endif
}

struct _glapi_table *
_mesa_create_marshal_table(const struct gl_context *ctx);

size_t
_mesa_unmarshal_dispatch_cmd(struct gl_context *ctx, const void *cmd);

static inline void
_mesa_post_marshal_hook(struct gl_context *ctx)
{
   /* This can be enabled for debugging whether a failure is a synchronization
    * problem between the main thread and the worker thread, or a failure in
    * how we actually marshal.
    */
   if (false)
      _mesa_glthread_finish(ctx);
}


/**
 * Checks whether we're on a compat context for code-generated
 * glBindVertexArray().
 *
 * In order to decide whether a draw call uses only VBOs for vertex and index
 * buffers, we track the current vertex and index buffer bindings by
 * glBindBuffer().  However, the index buffer binding is stored in the vertex
 * array as opposed to the context.  If we were to accurately track whether
 * the index buffer was a user pointer ot not, we'd have to track it per
 * vertex array, which would mean synchronizing with the client thread and
 * looking into the hash table to find the actual vertex array object.  That's
 * more tracking than we'd like to do in the main thread, if possible.
 *
 * Instead, just punt for now and disable threading on apps using vertex
 * arrays and compat contexts.  Apps using vertex arrays can probably use a
 * core context.
 */
static inline bool
_mesa_glthread_is_compat_bind_vertex_array(const struct gl_context *ctx)
{
   return ctx->API != API_OPENGL_CORE;
}

struct marshal_cmd_Enable;
struct marshal_cmd_ShaderSource;
struct marshal_cmd_Flush;
struct marshal_cmd_BindBuffer;
struct marshal_cmd_BufferData;
struct marshal_cmd_BufferSubData;
struct marshal_cmd_NamedBufferData;
struct marshal_cmd_NamedBufferSubData;
struct marshal_cmd_ClearBuffer;
#define marshal_cmd_ClearBufferfv   marshal_cmd_ClearBuffer
#define marshal_cmd_ClearBufferiv   marshal_cmd_ClearBuffer
#define marshal_cmd_ClearBufferuiv  marshal_cmd_ClearBuffer
#define marshal_cmd_ClearBufferfi   marshal_cmd_ClearBuffer

void
_mesa_unmarshal_Enable(struct gl_context *ctx,
                       const struct marshal_cmd_Enable *cmd);

void GLAPIENTRY
_mesa_marshal_Enable(GLenum cap);

void GLAPIENTRY
_mesa_marshal_ShaderSource(GLuint shader, GLsizei count,
                           const GLchar * const *string, const GLint *length);

void
_mesa_unmarshal_ShaderSource(struct gl_context *ctx,
                             const struct marshal_cmd_ShaderSource *cmd);

void GLAPIENTRY
_mesa_marshal_Flush(void);

void
_mesa_unmarshal_Flush(struct gl_context *ctx,
                      const struct marshal_cmd_Flush *cmd);

void GLAPIENTRY
_mesa_marshal_BindBuffer(GLenum target, GLuint buffer);

void
_mesa_unmarshal_BindBuffer(struct gl_context *ctx,
                           const struct marshal_cmd_BindBuffer *cmd);

void
_mesa_unmarshal_BufferData(struct gl_context *ctx,
                           const struct marshal_cmd_BufferData *cmd);

void GLAPIENTRY
_mesa_marshal_BufferData(GLenum target, GLsizeiptr size, const GLvoid * data,
                         GLenum usage);

void
_mesa_unmarshal_BufferSubData(struct gl_context *ctx,
                              const struct marshal_cmd_BufferSubData *cmd);

void GLAPIENTRY
_mesa_marshal_BufferSubData(GLenum target, GLintptr offset, GLsizeiptr size,
                            const GLvoid * data);

void
_mesa_unmarshal_NamedBufferData(struct gl_context *ctx,
                                const struct marshal_cmd_NamedBufferData *cmd);

void GLAPIENTRY
_mesa_marshal_NamedBufferData(GLuint buffer, GLsizeiptr size,
                              const GLvoid * data, GLenum usage);

void
_mesa_unmarshal_NamedBufferSubData(struct gl_context *ctx,
                                   const struct marshal_cmd_NamedBufferSubData *cmd);

void GLAPIENTRY
_mesa_marshal_NamedBufferSubData(GLuint buffer, GLintptr offset, GLsizeiptr size,
                                 const GLvoid * data);

void
_mesa_unmarshal_ClearBufferfv(struct gl_context *ctx,
                              const struct marshal_cmd_ClearBuffer *cmd);

void GLAPIENTRY
_mesa_marshal_ClearBufferfv(GLenum buffer, GLint drawbuffer,
                            const GLfloat *value);

void
_mesa_unmarshal_ClearBufferiv(struct gl_context *ctx,
                              const struct marshal_cmd_ClearBuffer *cmd);

void GLAPIENTRY
_mesa_marshal_ClearBufferiv(GLenum buffer, GLint drawbuffer,
                            const GLint *value);

void
_mesa_unmarshal_ClearBufferuiv(struct gl_context *ctx,
                               const struct marshal_cmd_ClearBuffer *cmd);

void GLAPIENTRY
_mesa_marshal_ClearBufferuiv(GLenum buffer, GLint drawbuffer,
                             const GLuint *value);

void
_mesa_unmarshal_ClearBufferfi(struct gl_context *ctx,
                              const struct marshal_cmd_ClearBuffer *cmd);

void GLAPIENTRY
_mesa_marshal_ClearBufferfi(GLenum buffer, GLint drawbuffer,
                            const GLfloat depth, const GLint stencil);

#endif /* MARSHAL_H */
