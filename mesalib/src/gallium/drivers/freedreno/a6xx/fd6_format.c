/*
 * Copyright (C) 2016 Rob Clark <robclark@freedesktop.org>
 * Copyright © 2018 Google, Inc.
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#include "pipe/p_defines.h"
#include "util/format/u_format.h"

#include "fd6_format.h"
#include "freedreno_resource.h"

enum a6xx_tex_swiz
fd6_pipe2swiz(unsigned swiz)
{
   switch (swiz) {
   default:
   case PIPE_SWIZZLE_X:
      return A6XX_TEX_X;
   case PIPE_SWIZZLE_Y:
      return A6XX_TEX_Y;
   case PIPE_SWIZZLE_Z:
      return A6XX_TEX_Z;
   case PIPE_SWIZZLE_W:
      return A6XX_TEX_W;
   case PIPE_SWIZZLE_0:
      return A6XX_TEX_ZERO;
   case PIPE_SWIZZLE_1:
      return A6XX_TEX_ONE;
   }
}

void
fd6_tex_swiz(enum pipe_format format, unsigned char *swiz, unsigned swizzle_r,
             unsigned swizzle_g, unsigned swizzle_b, unsigned swizzle_a)
{
   const struct util_format_description *desc = util_format_description(format);
   const unsigned char uswiz[4] = {swizzle_r, swizzle_g, swizzle_b, swizzle_a};

   /* Gallium expects stencil sampler to return (s,s,s,s), so massage
    * the swizzle to do so.
    */
   if (format == PIPE_FORMAT_X24S8_UINT) {
      const unsigned char stencil_swiz[4] = {PIPE_SWIZZLE_W, PIPE_SWIZZLE_W,
                                             PIPE_SWIZZLE_W, PIPE_SWIZZLE_W};
      util_format_compose_swizzles(stencil_swiz, uswiz, swiz);
   } else if (format == PIPE_FORMAT_R8G8_R8B8_UNORM || format == PIPE_FORMAT_G8R8_B8R8_UNORM) {
      unsigned char fswiz[4] = {PIPE_SWIZZLE_Z, PIPE_SWIZZLE_X, PIPE_SWIZZLE_Y, PIPE_SWIZZLE_1};
      util_format_compose_swizzles(fswiz, uswiz, swiz);
   } else if (fd6_texture_swap(format, TILE6_LINEAR) != WZYX || format == PIPE_FORMAT_A1R5G5B5_UNORM) {
      /* Formats with a non-pass-through swap are permutations of RGBA
       * formats. We program the permutation using the swap and don't
       * need to compose the format swizzle with the user swizzle.
       */
      memcpy(swiz, uswiz, sizeof(uswiz));
   } else {
      /* Otherwise, it's an unswapped RGBA format or a format like L8 where
       * we need the XXX1 swizzle from the gallium format description.
       */
      util_format_compose_swizzles(desc->swizzle, uswiz, swiz);
   }
}
