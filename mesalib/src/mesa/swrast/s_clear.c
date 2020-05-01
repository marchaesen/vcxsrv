/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 1999-2008  Brian Paul   All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "main/glheader.h"
#include "main/accum.h"
#include "main/condrender.h"
#include "main/format_pack.h"
#include "main/macros.h"

#include "main/mtypes.h"

#include "s_context.h"
#include "s_depth.h"
#include "s_stencil.h"


/**
 * Convert a boolean color mask to a packed color where each channel of
 * the packed value at dst will be 0 or ~0 depending on the colorMask.
 */
static void
_pack_colormask(mesa_format format, const uint8_t colorMask[4], void *dst)
{
   float maskColor[4];

   switch (_mesa_get_format_datatype(format)) {
   case GL_UNSIGNED_NORMALIZED:
      /* simple: 1.0 will convert to ~0 in the right bit positions */
      maskColor[0] = colorMask[0] ? 1.0f : 0.0f;
      maskColor[1] = colorMask[1] ? 1.0f : 0.0f;
      maskColor[2] = colorMask[2] ? 1.0f : 0.0f;
      maskColor[3] = colorMask[3] ? 1.0f : 0.0f;
      _mesa_pack_float_rgba_row(format, 1,
                                (const float (*)[4]) maskColor, dst);
      break;
   case GL_SIGNED_NORMALIZED:
   case GL_FLOAT:
      /* These formats are harder because it's hard to know the floating
       * point values that will convert to ~0 for each color channel's bits.
       * This solution just generates a non-zero value for each color channel
       * then fixes up the non-zero values to be ~0.
       * Note: we'll need to add special case code if we ever have to deal
       * with formats with unequal color channel sizes, like R11_G11_B10.
       * We issue a warning below for channel sizes other than 8,16,32.
       */
      {
         uint32_t bits = _mesa_get_format_max_bits(format); /* bits per chan */
         uint32_t bytes = _mesa_get_format_bytes(format);
         uint32_t i;

         /* this should put non-zero values into the channels of dst */
         maskColor[0] = colorMask[0] ? -1.0f : 0.0f;
         maskColor[1] = colorMask[1] ? -1.0f : 0.0f;
         maskColor[2] = colorMask[2] ? -1.0f : 0.0f;
         maskColor[3] = colorMask[3] ? -1.0f : 0.0f;
         _mesa_pack_float_rgba_row(format, 1,
                                   (const float (*)[4]) maskColor, dst);

         /* fix-up the dst channels by converting non-zero values to ~0 */
         if (bits == 8) {
            uint8_t *d = (uint8_t *) dst;
            for (i = 0; i < bytes; i++) {
               d[i] = d[i] ? 0xff : 0x0;
            }
         }
         else if (bits == 16) {
            uint16_t *d = (uint16_t *) dst;
            for (i = 0; i < bytes / 2; i++) {
               d[i] = d[i] ? 0xffff : 0x0;
            }
         }
         else if (bits == 32) {
            uint32_t *d = (uint32_t *) dst;
            for (i = 0; i < bytes / 4; i++) {
               d[i] = d[i] ? 0xffffffffU : 0x0;
            }
         }
         else {
            unreachable("unexpected size in _mesa_pack_colormask()");
         }
      }
      break;
   default:
      unreachable("unexpected format data type in gen_color_mask()");
   }
}

/**
 * Clear an rgba color buffer with masking if needed.
 */
static void
clear_rgba_buffer(struct gl_context *ctx, struct gl_renderbuffer *rb,
                  const GLubyte colorMask[4])
{
   const GLint x = ctx->DrawBuffer->_Xmin;
   const GLint y = ctx->DrawBuffer->_Ymin;
   const GLint height = ctx->DrawBuffer->_Ymax - ctx->DrawBuffer->_Ymin;
   const GLint width  = ctx->DrawBuffer->_Xmax - ctx->DrawBuffer->_Xmin;
   const GLuint pixelSize = _mesa_get_format_bytes(rb->Format);
   const GLboolean doMasking = (colorMask[0] == 0 ||
                                colorMask[1] == 0 ||
                                colorMask[2] == 0 ||
                                colorMask[3] == 0);
   const GLfloat (*clearColor)[4] =
      (const GLfloat (*)[4]) ctx->Color.ClearColor.f;
   GLbitfield mapMode = GL_MAP_WRITE_BIT;
   GLubyte *map;
   GLint rowStride;
   GLint i, j;

   if (doMasking) {
      /* we'll need to read buffer values too */
      mapMode |= GL_MAP_READ_BIT;
   }

   /* map dest buffer */
   ctx->Driver.MapRenderbuffer(ctx, rb, x, y, width, height,
                               mapMode, &map, &rowStride,
                               ctx->DrawBuffer->FlipY);
   if (!map) {
      _mesa_error(ctx, GL_OUT_OF_MEMORY, "glClear(color)");
      return;
   }

   /* for 1, 2, 4-byte clearing */
#define SIMPLE_TYPE_CLEAR(TYPE)                                         \
   do {                                                                 \
      TYPE pixel, pixelMask;                                            \
      _mesa_pack_float_rgba_row(rb->Format, 1, clearColor, &pixel);     \
      if (doMasking) {                                                  \
         _pack_colormask(rb->Format, colorMask, &pixelMask);            \
         pixel &= pixelMask;                                            \
         pixelMask = ~pixelMask;                                        \
      }                                                                 \
      for (i = 0; i < height; i++) {                                    \
         TYPE *row = (TYPE *) map;                                      \
         if (doMasking) {                                               \
            for (j = 0; j < width; j++) {                               \
               row[j] = (row[j] & pixelMask) | pixel;                   \
            }                                                           \
         }                                                              \
         else {                                                         \
            for (j = 0; j < width; j++) {                               \
               row[j] = pixel;                                          \
            }                                                           \
         }                                                              \
         map += rowStride;                                              \
      }                                                                 \
   } while (0)


   /* for 3, 6, 8, 12, 16-byte clearing */
#define MULTI_WORD_CLEAR(TYPE, N)                                       \
   do {                                                                 \
      TYPE pixel[N], pixelMask[N];                                      \
      GLuint k;                                                         \
      _mesa_pack_float_rgba_row(rb->Format, 1, clearColor, pixel);      \
      if (doMasking) {                                                  \
         _pack_colormask(rb->Format, colorMask, pixelMask);             \
         for (k = 0; k < N; k++) {                                      \
            pixel[k] &= pixelMask[k];                                   \
            pixelMask[k] = ~pixelMask[k];                               \
         }                                                              \
      }                                                                 \
      for (i = 0; i < height; i++) {                                    \
         TYPE *row = (TYPE *) map;                                      \
         if (doMasking) {                                               \
            for (j = 0; j < width; j++) {                               \
               for (k = 0; k < N; k++) {                                \
                  row[j * N + k] =                                      \
                     (row[j * N + k] & pixelMask[k]) | pixel[k];        \
               }                                                        \
            }                                                           \
         }                                                              \
         else {                                                         \
            for (j = 0; j < width; j++) {                               \
               for (k = 0; k < N; k++) {                                \
                  row[j * N + k] = pixel[k];                            \
               }                                                        \
            }                                                           \
         }                                                              \
         map += rowStride;                                              \
      }                                                                 \
   } while(0)

   switch (pixelSize) {
   case 1:
      SIMPLE_TYPE_CLEAR(GLubyte);
      break;
   case 2:
      SIMPLE_TYPE_CLEAR(GLushort);
      break;
   case 3:
      MULTI_WORD_CLEAR(GLubyte, 3);
      break;
   case 4:
      SIMPLE_TYPE_CLEAR(GLuint);
      break;
   case 6:
      MULTI_WORD_CLEAR(GLushort, 3);
      break;
   case 8:
      MULTI_WORD_CLEAR(GLuint, 2);
      break;
   case 12:
      MULTI_WORD_CLEAR(GLuint, 3);
      break;
   case 16:
      MULTI_WORD_CLEAR(GLuint, 4);
      break;
   default:
      _mesa_problem(ctx, "bad pixel size in clear_rgba_buffer()");
   }

   /* unmap buffer */
   ctx->Driver.UnmapRenderbuffer(ctx, rb);
}


/**
 * Clear the front/back/left/right/aux color buffers.
 * This function is usually only called if the device driver can't
 * clear its own color buffers for some reason (such as with masking).
 */
static void
clear_color_buffers(struct gl_context *ctx)
{
   GLuint buf;

   for (buf = 0; buf < ctx->DrawBuffer->_NumColorDrawBuffers; buf++) {
      struct gl_renderbuffer *rb = ctx->DrawBuffer->_ColorDrawBuffers[buf];

      /* If this is an ES2 context or GL_ARB_ES2_compatibility is supported,
       * the framebuffer can be complete with some attachments be missing.  In
       * this case the _ColorDrawBuffers pointer will be NULL.
       */
      if (rb == NULL)
	 continue;

      const GLubyte colormask[4] = {
         GET_COLORMASK_BIT(ctx->Color.ColorMask, buf, 0) ? 0xff : 0,
         GET_COLORMASK_BIT(ctx->Color.ColorMask, buf, 1) ? 0xff : 0,
         GET_COLORMASK_BIT(ctx->Color.ColorMask, buf, 2) ? 0xff : 0,
         GET_COLORMASK_BIT(ctx->Color.ColorMask, buf, 3) ? 0xff : 0,
      };
      clear_rgba_buffer(ctx, rb, colormask);
   }
}


/**
 * Called via the device driver's ctx->Driver.Clear() function if the
 * device driver can't clear one or more of the buffers itself.
 * \param buffers  bitfield of BUFFER_BIT_* values indicating which
 *                 renderbuffers are to be cleared.
 * \param all  if GL_TRUE, clear whole buffer, else clear specified region.
 */
void
_swrast_Clear(struct gl_context *ctx, GLbitfield buffers)
{
   const GLbitfield BUFFER_DS = BUFFER_BIT_DEPTH | BUFFER_BIT_STENCIL;

#ifdef DEBUG_FOO
   {
      const GLbitfield legalBits =
         BUFFER_BIT_FRONT_LEFT |
	 BUFFER_BIT_FRONT_RIGHT |
	 BUFFER_BIT_BACK_LEFT |
	 BUFFER_BIT_BACK_RIGHT |
	 BUFFER_BIT_DEPTH |
	 BUFFER_BIT_STENCIL |
	 BUFFER_BIT_ACCUM |
         BUFFER_BIT_AUX0;
      assert((buffers & (~legalBits)) == 0);
   }
#endif

   if (!_mesa_check_conditional_render(ctx))
      return; /* don't clear */

   if (SWRAST_CONTEXT(ctx)->NewState)
      _swrast_validate_derived(ctx);

   if ((buffers & BUFFER_BITS_COLOR)
       && (ctx->DrawBuffer->_NumColorDrawBuffers > 0)) {
      clear_color_buffers(ctx);
   }

   if (buffers & BUFFER_BIT_ACCUM) {
      _mesa_clear_accum_buffer(ctx);
   }

   if (buffers & BUFFER_DS) {
      struct gl_renderbuffer *depthRb =
         ctx->DrawBuffer->Attachment[BUFFER_DEPTH].Renderbuffer;
      struct gl_renderbuffer *stencilRb =
         ctx->DrawBuffer->Attachment[BUFFER_STENCIL].Renderbuffer;

      if ((buffers & BUFFER_DS) == BUFFER_DS && depthRb == stencilRb) {
         /* clear depth and stencil together */
         _swrast_clear_depth_stencil_buffer(ctx);
      }
      else {
         /* clear depth, stencil separately */
         if (buffers & BUFFER_BIT_DEPTH) {
            _swrast_clear_depth_buffer(ctx);
         }
         if (buffers & BUFFER_BIT_STENCIL) {
            _swrast_clear_stencil_buffer(ctx);
         }
      }
   }
}
