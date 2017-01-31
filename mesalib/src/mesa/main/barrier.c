/*
 * Copyright © 2011 Marek Olšák <maraeo@gmail.com>
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/**
 * \file barrier.c
 * Implementation of various pipeline barrier entry points.
 *
 * \author Marek Olšák <maraeo@gmail.com>
 */

#include "context.h"
#include "barrier.h"


static void
_mesa_texture_barrier(struct gl_context *ctx)
{
   /* no-op */
}

void
_mesa_init_barrier_functions(struct dd_function_table *driver)
{
   driver->TextureBarrier = _mesa_texture_barrier;
}

void GLAPIENTRY
_mesa_TextureBarrierNV(void)
{
   GET_CURRENT_CONTEXT(ctx);

   if (!ctx->Extensions.NV_texture_barrier) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "glTextureBarrier(not supported)");
      return;
   }

   ctx->Driver.TextureBarrier(ctx);
}

void GLAPIENTRY
_mesa_MemoryBarrier(GLbitfield barriers)
{
   GET_CURRENT_CONTEXT(ctx);

   if (ctx->Driver.MemoryBarrier)
      ctx->Driver.MemoryBarrier(ctx, barriers);
}

void GLAPIENTRY
_mesa_MemoryBarrierByRegion(GLbitfield barriers)
{
   GET_CURRENT_CONTEXT(ctx);

   GLbitfield all_allowed_bits = GL_ATOMIC_COUNTER_BARRIER_BIT |
                                 GL_FRAMEBUFFER_BARRIER_BIT |
                                 GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
                                 GL_SHADER_STORAGE_BARRIER_BIT |
                                 GL_TEXTURE_FETCH_BARRIER_BIT |
                                 GL_UNIFORM_BARRIER_BIT;

   if (ctx->Driver.MemoryBarrier) {
      /* From section 7.11.2 of the OpenGL ES 3.1 specification:
       *
       *    "When barriers is ALL_BARRIER_BITS, shader memory accesses will be
       *     synchronized relative to all these barrier bits, but not to other
       *     barrier bits specific to MemoryBarrier."
       *
       * That is, if barriers is the special value GL_ALL_BARRIER_BITS, then all
       * barriers allowed by glMemoryBarrierByRegion should be activated."
       */
      if (barriers == GL_ALL_BARRIER_BITS) {
         ctx->Driver.MemoryBarrier(ctx, all_allowed_bits);
         return;
      }

      /* From section 7.11.2 of the OpenGL ES 3.1 specification:
       *
       *    "An INVALID_VALUE error is generated if barriers is not the special
       *     value ALL_BARRIER_BITS, and has any bits set other than those
       *     described above."
       */
      if ((barriers & ~all_allowed_bits) != 0) {
         _mesa_error(ctx, GL_INVALID_VALUE,
                     "glMemoryBarrierByRegion(unsupported barrier bit");
      }

      ctx->Driver.MemoryBarrier(ctx, barriers);
   }
}

void GLAPIENTRY
_mesa_BlendBarrier(void)
{
   GET_CURRENT_CONTEXT(ctx);

   if (!ctx->Extensions.MESA_shader_framebuffer_fetch_non_coherent &&
       !ctx->Extensions.KHR_blend_equation_advanced) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "glBlendBarrier(not supported)");
      return;
   }

   ctx->Driver.BlendBarrier(ctx);
}
