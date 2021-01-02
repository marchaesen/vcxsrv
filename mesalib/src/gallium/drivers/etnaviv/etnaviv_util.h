/*
 * Copyright (c) 2012-2015 Etnaviv Project
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/* Misc util */
#ifndef H_ETNA_UTIL
#define H_ETNA_UTIL

#include <math.h>

/* for conditionally setting boolean flag(s): */
#define COND(bool, val) ((bool) ? (val) : 0)

#define foreach_bit(b, mask) \
   for (uint32_t _m = (mask); _m && ({(b) = u_bit_scan(&_m); 1;});)

/* align to a value divisable by granularity >= value, works only for powers of two */
static inline uint32_t
etna_align_up(uint32_t value, uint32_t granularity)
{
   return (value + (granularity - 1)) & (~(granularity - 1));
}

/* clamped float [0.0 .. 1.0] -> [0 .. 255] */
static inline uint8_t
etna_cfloat_to_uint8(float f)
{
   if (f <= 0.0f)
      return 0;

   if (f >= (1.0f - 1.0f / 256.0f))
      return 255;

   return f * 256.0f;
}

/* clamped float [0.0 .. 1.0] -> [0 .. (1<<bits)-1] */
static inline uint32_t
etna_cfloat_to_uintN(float f, int bits)
{
   if (f <= 0.0f)
      return 0;

   if (f >= (1.0f - 1.0f / (1 << bits)))
      return (1 << bits) - 1;

   return f * (1 << bits);
}

/* 1/log10(2) */
#define RCPLOG2 (1.4426950408889634f)

/* float to fixp 5.5 */
static inline uint32_t
etna_float_to_fixp55(float f)
{
   if (f >= 15.953125f)
      return 511;

   if (f < -16.0f)
      return 512;

   return (int32_t)(f * 32.0f + 0.5f);
}

/* float to fixp 8.8 */
static inline uint32_t
etna_float_to_fixp88(float f)
{
   if (f >= (32767.0 - 1.0f) / 256.0f)
      return 32767;

   if (f < -16.0f)
      return 32768;

   return (int32_t)(f * 256.0f + 0.5f);
}

/* texture size to log2 in fixp 5.5 format */
static inline uint32_t
etna_log2_fixp55(unsigned width)
{
   return etna_float_to_fixp55(logf((float)width) * RCPLOG2);
}

/* texture size to log2 in fixp 8.8 format */
static inline uint32_t
etna_log2_fixp88(unsigned width)
{
   return etna_float_to_fixp88(logf((float)width) * RCPLOG2);
}

/* float to fixp 16.16 */
static inline uint32_t
etna_f32_to_fixp16(float f)
{
   if (f >= (32768.0f - 1.0f / 65536.0f))
      return 0x7fffffff;

   if (f < -32768.0f)
      return 0x80000000;

   return (int32_t)(f * 65536.0f + 0.5f);
}

#endif
