/*
 * Copyright (C) 2011 Marek Olšák <maraeo@gmail.com>
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

/* Copied from EXT_texture_shared_exponent and edited, getting rid of
 * expensive float math bits too. */

#ifndef RGB9E5_H
#define RGB9E5_H

#include <assert.h>

#include "c99_math.h"

#define RGB9E5_EXPONENT_BITS          5
#define RGB9E5_MANTISSA_BITS          9
#define RGB9E5_EXP_BIAS               15
#define RGB9E5_MAX_VALID_BIASED_EXP   31

#define MAX_RGB9E5_EXP               (RGB9E5_MAX_VALID_BIASED_EXP - RGB9E5_EXP_BIAS)
#define RGB9E5_MANTISSA_VALUES       (1<<RGB9E5_MANTISSA_BITS)
#define MAX_RGB9E5_MANTISSA          (RGB9E5_MANTISSA_VALUES-1)
#define MAX_RGB9E5                   (((float)MAX_RGB9E5_MANTISSA)/RGB9E5_MANTISSA_VALUES * (1<<MAX_RGB9E5_EXP))

typedef union {
   unsigned int raw;
   float value;
   struct {
#if defined(MESA_BIG_ENDIAN) || defined(PIPE_ARCH_BIG_ENDIAN)
      unsigned int negative:1;
      unsigned int biasedexponent:8;
      unsigned int mantissa:23;
#else
      unsigned int mantissa:23;
      unsigned int biasedexponent:8;
      unsigned int negative:1;
#endif
   } field;
} float754;

typedef union {
   unsigned int raw;
   struct {
#if defined(MESA_BIG_ENDIAN) || defined(PIPE_ARCH_BIG_ENDIAN)
      unsigned int biasedexponent:RGB9E5_EXPONENT_BITS;
      unsigned int b:RGB9E5_MANTISSA_BITS;
      unsigned int g:RGB9E5_MANTISSA_BITS;
      unsigned int r:RGB9E5_MANTISSA_BITS;
#else
      unsigned int r:RGB9E5_MANTISSA_BITS;
      unsigned int g:RGB9E5_MANTISSA_BITS;
      unsigned int b:RGB9E5_MANTISSA_BITS;
      unsigned int biasedexponent:RGB9E5_EXPONENT_BITS;
#endif
   } field;
} rgb9e5;


static inline int rgb9e5_ClampRange(float x)
{
   float754 f;
   float754 max;
   f.value = x;
   max.value = MAX_RGB9E5;

   if (f.raw > 0x7f800000)
  /* catches neg, NaNs */
      return 0;
   else if (f.raw >= max.raw)
      return max.raw;
   else
      return f.raw;
}

static inline unsigned float3_to_rgb9e5(const float rgb[3])
{
   rgb9e5 retval;
   int rm, gm, bm, exp_shared;
   float754 revdenom = {0};
   float754 rc, bc, gc, maxrgb;

   rc.raw = rgb9e5_ClampRange(rgb[0]);
   gc.raw = rgb9e5_ClampRange(rgb[1]);
   bc.raw = rgb9e5_ClampRange(rgb[2]);
   maxrgb.raw = MAX3(rc.raw, gc.raw, bc.raw);

   /*
    * Compared to what the spec suggests, instead of conditionally adjusting
    * the exponent after the fact do it here by doing the equivalent of +0.5 -
    * the int add will spill over into the exponent in this case.
    */
   maxrgb.raw += maxrgb.raw & (1 << (23-9));
   exp_shared = MAX2((maxrgb.raw >> 23), -RGB9E5_EXP_BIAS - 1 + 127) +
                1 + RGB9E5_EXP_BIAS - 127;
   revdenom.field.biasedexponent = 127 - (exp_shared - RGB9E5_EXP_BIAS -
                                          RGB9E5_MANTISSA_BITS) + 1;
   assert(exp_shared <= RGB9E5_MAX_VALID_BIASED_EXP);

   /*
    * The spec uses strict round-up behavior (d3d10 disagrees, but in any case
    * must match what is done above for figuring out exponent).
    * We avoid the doubles ((int) rc * revdenom + 0.5) by doing the rounding
    * ourselves (revdenom was adjusted by +1, above).
    */
   rm = (int) (rc.value * revdenom.value);
   gm = (int) (gc.value * revdenom.value);
   bm = (int) (bc.value * revdenom.value);
   rm = (rm & 1) + (rm >> 1);
   gm = (gm & 1) + (gm >> 1);
   bm = (bm & 1) + (bm >> 1);

   assert(rm <= MAX_RGB9E5_MANTISSA);
   assert(gm <= MAX_RGB9E5_MANTISSA);
   assert(bm <= MAX_RGB9E5_MANTISSA);
   assert(rm >= 0);
   assert(gm >= 0);
   assert(bm >= 0);

   retval.field.r = rm;
   retval.field.g = gm;
   retval.field.b = bm;
   retval.field.biasedexponent = exp_shared;

   return retval.raw;
}

static inline void rgb9e5_to_float3(unsigned rgb, float retval[3])
{
   rgb9e5 v;
   int exponent;
   float754 scale = {0};

   v.raw = rgb;
   exponent = v.field.biasedexponent - RGB9E5_EXP_BIAS - RGB9E5_MANTISSA_BITS;
   scale.field.biasedexponent = exponent + 127;

   retval[0] = v.field.r * scale.value;
   retval[1] = v.field.g * scale.value;
   retval[2] = v.field.b * scale.value;
}

#endif
