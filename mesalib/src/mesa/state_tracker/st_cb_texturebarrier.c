/**************************************************************************
 *
 * Copyright 2011 Marek Olšák <maraeo@gmail.com>
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
 * IN NO EVENT SHALL THE AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/


/**
 * glTextureBarrierNV function
 *
 * \author Marek Olšák
 */


#include "main/imports.h"
#include "main/context.h"

#include "pipe/p_context.h"
#include "pipe/p_defines.h"
#include "st_context.h"
#include "st_cb_texturebarrier.h"


/**
 * Called via ctx->Driver.TextureBarrier()
 */
static void
st_TextureBarrier(struct gl_context *ctx)
{
   struct pipe_context *pipe = st_context(ctx)->pipe;

   pipe->texture_barrier(pipe, PIPE_TEXTURE_BARRIER_SAMPLER);
}


/**
 * Called via ctx->Driver.FramebufferFetchBarrier()
 */
static void
st_FramebufferFetchBarrier(struct gl_context *ctx)
{
   struct pipe_context *pipe = st_context(ctx)->pipe;

   pipe->texture_barrier(pipe, PIPE_TEXTURE_BARRIER_FRAMEBUFFER);
}


/**
 * Called via ctx->Driver.MemoryBarrier()
 */
static void
st_MemoryBarrier(struct gl_context *ctx, GLbitfield barriers)
{
   struct pipe_context *pipe = st_context(ctx)->pipe;
   unsigned flags = 0;

   if (barriers & GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT)
      flags |= PIPE_BARRIER_VERTEX_BUFFER;
   if (barriers & GL_ELEMENT_ARRAY_BARRIER_BIT)
      flags |= PIPE_BARRIER_INDEX_BUFFER;
   if (barriers & GL_UNIFORM_BARRIER_BIT)
      flags |= PIPE_BARRIER_CONSTANT_BUFFER;
   if (barriers & GL_TEXTURE_FETCH_BARRIER_BIT)
      flags |= PIPE_BARRIER_TEXTURE;
   if (barriers & GL_SHADER_IMAGE_ACCESS_BARRIER_BIT)
      flags |= PIPE_BARRIER_IMAGE;
   if (barriers & GL_COMMAND_BARRIER_BIT)
      flags |= PIPE_BARRIER_INDIRECT_BUFFER;
   if (barriers & GL_PIXEL_BUFFER_BARRIER_BIT) {
      /* The PBO may be
       *  (1) bound as a texture for PBO uploads, or
       *  (2) accessed by the CPU via transfer ops.
       * For case (2), we assume automatic flushing by the driver.
       */
      flags |= PIPE_BARRIER_TEXTURE;
   }
   /* GL_TEXTURE_UPDATE_BARRIER_BIT:
    * Texture updates translate to:
    *  (1) texture transfers to/from the CPU,
    *  (2) texture as blit destination, or
    *  (3) texture as framebuffer.
    * In all cases, we assume the driver does the required flushing
    * automatically.
    */
   /* GL_BUFFER_UPDATE_BARRIER_BIT:
    * Buffer updates translate to
    *  (1) buffer transfers to/from the CPU,
    *  (2) resource copies and clears.
    * In all cases, we assume the driver does the required flushing
    * automatically.
    */
   if (barriers & GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT)
      flags |= PIPE_BARRIER_MAPPED_BUFFER;
   if (barriers & GL_QUERY_BUFFER_BARRIER_BIT)
      flags |= PIPE_BARRIER_QUERY_BUFFER;
   if (barriers & GL_FRAMEBUFFER_BARRIER_BIT)
      flags |= PIPE_BARRIER_FRAMEBUFFER;
   if (barriers & GL_TRANSFORM_FEEDBACK_BARRIER_BIT)
      flags |= PIPE_BARRIER_STREAMOUT_BUFFER;
   if (barriers & GL_ATOMIC_COUNTER_BARRIER_BIT)
      flags |= PIPE_BARRIER_SHADER_BUFFER;
   if (barriers & GL_SHADER_STORAGE_BARRIER_BIT)
      flags |= PIPE_BARRIER_SHADER_BUFFER;

   if (flags && pipe->memory_barrier)
      pipe->memory_barrier(pipe, flags);
}

void st_init_texture_barrier_functions(struct dd_function_table *functions)
{
   functions->TextureBarrier = st_TextureBarrier;
   functions->FramebufferFetchBarrier = st_FramebufferFetchBarrier;
   functions->MemoryBarrier = st_MemoryBarrier;
}
