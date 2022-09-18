/*
 * Copyright (C) 2021 Alyssa Rosenzweig <alyssa@rosenzweig.io>
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
 */

#ifndef __AGX_MINIFLOAT_H_
#define __AGX_MINIFLOAT_H_

#include <math.h>
#include "util/macros.h"

/* AGX includes an 8-bit floating-point format for small dyadic immediates,
 * consisting of 3 bits for the exponent, 4 bits for the mantissa, and 1-bit
 * for sign, in the usual order. Zero exponent has special handling. */

static inline float
agx_minifloat_decode(uint8_t imm)
{
   float sign = (imm & 0x80) ? -1.0 : 1.0;
   signed exp = (imm & 0x70) >> 4;
   unsigned mantissa = (imm & 0xF);

   if (exp)
      return ldexpf(sign * (float) (mantissa | 0x10), exp - 7);
   else
      return ldexpf(sign * ((float) mantissa), -6);
}

/* Encodes a float. Results are only valid if the float can be represented
 * exactly, if not the result of this function is UNDEFINED. signbit() is used
 * to ensure -0.0 is handled correctly. */

static inline uint8_t
agx_minifloat_encode(float f)
{
   unsigned sign = signbit(f) ? 0x80 : 0;
   f = fabsf(f);

   /* frac is in [0.5, 1) and f = frac * 2^exp */
   int exp = 0;
   float frac = frexpf(f, &exp);

   if (f >= 0.25) {
      unsigned mantissa = (frac * 32.0);
      exp -= 5; /* 2^5 = 32 */
      exp = CLAMP(exp + 7, 0, 7);

      assert(mantissa >= 0x10 && mantissa < 0x20);
      assert(exp >= 1);

      return sign | (exp << 4) | (mantissa & 0xF);
   } else {
      unsigned mantissa = (f * 64.0f);
      assert(mantissa < 0x10);

      return sign | mantissa;
   }
}

static inline bool
agx_minifloat_exact(float f)
{
   float f_ = agx_minifloat_decode(agx_minifloat_encode(f));
   return memcmp(&f, &f_, sizeof(float)) == 0;
}

#endif
