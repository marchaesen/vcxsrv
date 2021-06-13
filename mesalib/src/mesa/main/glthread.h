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

#ifndef _GLTHREAD_H
#define _GLTHREAD_H

/* The size of one batch and the maximum size of one call.
 *
 * This should be as low as possible, so that:
 * - multiple synchronizations within a frame don't slow us down much
 * - a smaller number of calls per frame can still get decent parallelism
 * - the memory footprint of the queue is low, and with that comes a lower
 *   chance of experiencing CPU cache thrashing
 * but it should be high enough so that u_queue overhead remains negligible.
 */
#define MARSHAL_MAX_CMD_SIZE (8 * 1024)

/* The number of batch slots in memory.
 *
 * One batch is being executed, one batch is being filled, the rest are
 * waiting batches. There must be at least 1 slot for a waiting batch,
 * so the minimum number of batches is 3.
 */
#define MARSHAL_MAX_BATCHES 8

/* Special value for glEnableClientState(GL_PRIMITIVE_RESTART_NV). */
#define VERT_ATTRIB_PRIMITIVE_RESTART_NV -1

#include <inttypes.h>
#include <stdbool.h>
#include "util/u_queue.h"
#include "GL/gl.h"
#include "compiler/shader_enums.h"
#include "main/config.h"

#ifdef __cplusplus
extern "C" {
#endif

struct gl_context;
struct gl_buffer_object;
struct _mesa_HashTable;

struct glthread_attrib_binding {
   struct gl_buffer_object *buffer; /**< where non-VBO data was uploaded */
   int offset;                      /**< offset to uploaded non-VBO data */
   const void *original_pointer;    /**< restore this pointer after the draw */
};

struct glthread_vao {
   GLuint Name;
   GLuint CurrentElementBufferName;
   GLbitfield UserEnabled; /**< Vertex attribs enabled by the user. */
   GLbitfield Enabled; /**< UserEnabled with POS vs GENERIC0 aliasing resolved. */
   GLbitfield BufferEnabled; /**< "Enabled" converted to buffer bindings. */
   GLbitfield BufferInterleaved; /**< Bitmask of buffers used by multiple attribs. */
   GLbitfield UserPointerMask; /**< Bitmask of buffer bindings. */
   GLbitfield NonZeroDivisorMask; /**< Bitmask of buffer bindings. */

   struct {
      /* Per attrib: */
      GLuint ElementSize;
      GLuint RelativeOffset;
      GLuint BufferIndex; /**< Referring to Attrib[BufferIndex]. */

      /* Per buffer binding: */
      GLsizei Stride;
      GLuint Divisor;
      int EnabledAttribCount; /**< Number of enabled attribs using this buffer. */
      const void *Pointer;
   } Attrib[VERT_ATTRIB_MAX];
};

/** A single batch of commands queued up for execution. */
struct glthread_batch
{
   /** Batch fence for waiting for the execution to finish. */
   struct util_queue_fence fence;

   /** The worker thread will access the context with this. */
   struct gl_context *ctx;

   /**
    * Number of uint64_t elements filled already.
    * This is 0 when it's being filled because glthread::used holds the real
    * value temporarily, and glthread::used is copied to this variable when
    * the batch is submitted.
    */
   unsigned used;

   /** Data contained in the command buffer. */
   uint64_t buffer[MARSHAL_MAX_CMD_SIZE / 8];
};

struct glthread_client_attrib {
   struct glthread_vao VAO;
   GLuint CurrentArrayBufferName;
   int ClientActiveTexture;
   GLuint RestartIndex;
   bool PrimitiveRestart;
   bool PrimitiveRestartFixedIndex;

   /** Whether this element of the client attrib stack contains saved state. */
   bool Valid;
};

/* For glPushAttrib / glPopAttrib. */
struct glthread_attrib_node {
   GLbitfield Mask;
   int ActiveTexture;
   GLenum MatrixMode;
};

typedef enum {
   M_MODELVIEW,
   M_PROJECTION,
   M_PROGRAM0,
   M_PROGRAM_LAST = M_PROGRAM0 + MAX_PROGRAM_MATRICES - 1,
   M_TEXTURE0,
   M_TEXTURE_LAST = M_TEXTURE0 + MAX_TEXTURE_UNITS - 1,
   M_DUMMY, /* used instead of reporting errors */
   M_NUM_MATRIX_STACKS,
} gl_matrix_index;

struct glthread_state
{
   /** Multithreaded queue. */
   struct util_queue queue;

   /** This is sent to the driver for framebuffer overlay / HUD. */
   struct util_queue_monitoring stats;

   /** Whether GLThread is enabled. */
   bool enabled;

   /** Display lists. */
   GLenum ListMode; /**< Zero if not inside display list, else list mode. */
   unsigned ListBase;
   unsigned ListCallDepth;

   /** For L3 cache pinning. */
   unsigned pin_thread_counter;

   /** The ring of batches in memory. */
   struct glthread_batch batches[MARSHAL_MAX_BATCHES];

   /** Pointer to the batch currently being filled. */
   struct glthread_batch *next_batch;

   /** Index of the last submitted batch. */
   unsigned last;

   /** Index of the batch being filled and about to be submitted. */
   unsigned next;

   /** Number of uint64_t elements filled already. */
   unsigned used;

   /** Upload buffer. */
   struct gl_buffer_object *upload_buffer;
   uint8_t *upload_ptr;
   unsigned upload_offset;
   int upload_buffer_private_refcount;

   /** Caps. */
   GLboolean SupportsBufferUploads;
   GLboolean SupportsNonVBOUploads;

   /** Primitive restart state. */
   bool PrimitiveRestart;
   bool PrimitiveRestartFixedIndex;
   bool _PrimitiveRestart;
   GLuint RestartIndex;
   GLuint _RestartIndex[4]; /**< Restart index for index_size = 1,2,4. */

   /** Vertex Array objects tracked by glthread independently of Mesa. */
   struct _mesa_HashTable *VAOs;
   struct glthread_vao *CurrentVAO;
   struct glthread_vao *LastLookedUpVAO;
   struct glthread_vao DefaultVAO;
   struct glthread_client_attrib ClientAttribStack[MAX_CLIENT_ATTRIB_STACK_DEPTH];
   int ClientAttribStackTop;
   int ClientActiveTexture;

   /** Currently-bound buffer object IDs. */
   GLuint CurrentArrayBufferName;
   GLuint CurrentDrawIndirectBufferName;
   GLuint CurrentPixelPackBufferName;
   GLuint CurrentPixelUnpackBufferName;

   /**
    * The batch index of the last occurence of glLinkProgram or
    * glDeleteProgram or -1 if there is no such enqueued call.
    */
   int LastProgramChangeBatch;

   /**
    * The batch index of the last occurence of glEndList or
    * glDeleteLists or -1 if there is no such enqueued call.
    */
   int LastDListChangeBatchIndex;

   /** Basic matrix state tracking. */
   int ActiveTexture;
   GLenum MatrixMode;
   gl_matrix_index MatrixIndex;
   struct glthread_attrib_node AttribStack[MAX_ATTRIB_STACK_DEPTH];
   int AttribStackDepth;
   int MatrixStackDepth[M_NUM_MATRIX_STACKS];
};

void _mesa_glthread_init(struct gl_context *ctx);
void _mesa_glthread_destroy(struct gl_context *ctx);

void _mesa_glthread_restore_dispatch(struct gl_context *ctx, const char *func);
void _mesa_glthread_disable(struct gl_context *ctx, const char *func);
void _mesa_glthread_flush_batch(struct gl_context *ctx);
void _mesa_glthread_finish(struct gl_context *ctx);
void _mesa_glthread_finish_before(struct gl_context *ctx, const char *func);
void _mesa_glthread_upload(struct gl_context *ctx, const void *data,
                           GLsizeiptr size, unsigned *out_offset,
                           struct gl_buffer_object **out_buffer,
                           uint8_t **out_ptr);
void _mesa_glthread_reset_vao(struct glthread_vao *vao);
void _mesa_error_glthread_safe(struct gl_context *ctx, GLenum error,
                               bool glthread, const char *format, ...);
void _mesa_glthread_execute_list(struct gl_context *ctx, GLuint list);

void _mesa_glthread_BindBuffer(struct gl_context *ctx, GLenum target,
                               GLuint buffer);
void _mesa_glthread_DeleteBuffers(struct gl_context *ctx, GLsizei n,
                                  const GLuint *buffers);

void _mesa_glthread_BindVertexArray(struct gl_context *ctx, GLuint id);
void _mesa_glthread_DeleteVertexArrays(struct gl_context *ctx,
                                       GLsizei n, const GLuint *ids);
void _mesa_glthread_GenVertexArrays(struct gl_context *ctx,
                                    GLsizei n, GLuint *arrays);
void _mesa_glthread_set_prim_restart(struct gl_context *ctx, GLenum cap,
                                     bool value);
void _mesa_glthread_PrimitiveRestartIndex(struct gl_context *ctx, GLuint index);
void _mesa_glthread_ClientState(struct gl_context *ctx, GLuint *vaobj,
                                gl_vert_attrib attrib, bool enable);
void _mesa_glthread_AttribDivisor(struct gl_context *ctx, const GLuint *vaobj,
                                  gl_vert_attrib attrib, GLuint divisor);
void _mesa_glthread_AttribPointer(struct gl_context *ctx, gl_vert_attrib attrib,
                                  GLint size, GLenum type, GLsizei stride,
                                  const void *pointer);
void _mesa_glthread_DSAAttribPointer(struct gl_context *ctx, GLuint vao,
                                     GLuint buffer, gl_vert_attrib attrib,
                                     GLint size, GLenum type, GLsizei stride,
                                     GLintptr offset);
void _mesa_glthread_AttribFormat(struct gl_context *ctx, GLuint attribindex,
                                 GLint size, GLenum type,  GLuint relativeoffset);
void _mesa_glthread_DSAAttribFormat(struct gl_context *ctx, GLuint vaobj,
                                    GLuint attribindex, GLint size, GLenum type,
                                    GLuint relativeoffset);
void _mesa_glthread_VertexBuffer(struct gl_context *ctx, GLuint bindingindex,
                                 GLuint buffer, GLintptr offset, GLsizei stride);
void _mesa_glthread_DSAVertexBuffer(struct gl_context *ctx, GLuint vaobj,
                                    GLuint bindingindex, GLuint buffer,
                                    GLintptr offset, GLsizei stride);
void _mesa_glthread_DSAVertexBuffers(struct gl_context *ctx, GLuint vaobj,
                                     GLuint first, GLsizei count,
                                     const GLuint *buffers,
                                     const GLintptr *offsets,
                                     const GLsizei *strides);
void _mesa_glthread_BindingDivisor(struct gl_context *ctx, GLuint bindingindex,
                                   GLuint divisor);
void _mesa_glthread_DSABindingDivisor(struct gl_context *ctx, GLuint vaobj,
                                      GLuint bindingindex, GLuint divisor);
void _mesa_glthread_AttribBinding(struct gl_context *ctx, GLuint attribindex,
                                  GLuint bindingindex);
void _mesa_glthread_DSAAttribBinding(struct gl_context *ctx, GLuint vaobj,
                                     GLuint attribindex, GLuint bindingindex);
void _mesa_glthread_DSAElementBuffer(struct gl_context *ctx, GLuint vaobj,
                                     GLuint buffer);
void _mesa_glthread_PushClientAttrib(struct gl_context *ctx, GLbitfield mask,
                                     bool set_default);
void _mesa_glthread_PopClientAttrib(struct gl_context *ctx);
void _mesa_glthread_ClientAttribDefault(struct gl_context *ctx, GLbitfield mask);
void _mesa_glthread_InterleavedArrays(struct gl_context *ctx, GLenum format,
                                      GLsizei stride, const GLvoid *pointer);
void _mesa_glthread_ProgramChanged(struct gl_context *ctx);

#ifdef __cplusplus
}
#endif

#endif /* _GLTHREAD_H*/
