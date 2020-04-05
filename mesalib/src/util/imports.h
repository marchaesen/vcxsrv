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


/**
 * \file imports.h
 * Standard C library function wrappers.
 *
 * This file provides wrappers for all the standard C library functions
 * like malloc(), free(), printf(), getenv(), etc.
 */


#ifndef IMPORTS_H
#define IMPORTS_H


#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "util/compiler.h"
#include "util/bitscan.h"
#include "util/u_memory.h"

#ifdef __cplusplus
extern "C" {
#endif


/*
 * For GL_ARB_vertex_buffer_object we need to treat vertex array pointers
 * as offsets into buffer stores.  Since the vertex array pointer and
 * buffer store pointer are both pointers and we need to add them, we use
 * this macro.
 * Both pointers/offsets are expressed in bytes.
 */
#define ADD_POINTERS(A, B)  ( (uint8_t *) (A) + (uintptr_t) (B) )


/**
 * Sometimes we treat floats as ints.  On x86 systems, moving a float
 * as an int (thereby using integer registers instead of FP registers) is
 * a performance win.  Typically, this can be done with ordinary casts.
 * But with gcc's -fstrict-aliasing flag (which defaults to on in gcc 3.0)
 * these casts generate warnings.
 * The following union typedef is used to solve that.
 */
typedef union { float f; int i; unsigned u; } fi_type;



/*@}*/


/***
 *** LOG2: Log base 2 of float
 ***/
static inline float LOG2(float x)
{
#if 0
   /* This is pretty fast, but not accurate enough (only 2 fractional bits).
    * Based on code from http://www.stereopsis.com/log2.html
    */
   const float y = x * x * x * x;
   const unsigned ix = *((unsigned *) &y);
   const unsigned exp = (ix >> 23) & 0xFF;
   const int log2 = ((int) exp) - 127;
   return (float) log2 * (1.0 / 4.0);  /* 4, because of x^4 above */
#endif
   /* Pretty fast, and accurate.
    * Based on code from http://www.flipcode.com/totd/
    */
   fi_type num;
   int log_2;
   num.f = x;
   log_2 = ((num.i >> 23) & 255) - 128;
   num.i &= ~(255 << 23);
   num.i += 127 << 23;
   num.f = ((-1.0f/3) * num.f + 2) * num.f - 2.0f/3;
   return num.f + log_2;
}



/**
 * finite macro.
 */
#if defined(_MSC_VER)
#  define finite _finite
#endif


/***
 *** IS_INF_OR_NAN: test if float is infinite or NaN
 ***/
#if defined(isfinite)
#define IS_INF_OR_NAN(x)        (!isfinite(x))
#elif defined(finite)
#define IS_INF_OR_NAN(x)        (!finite(x))
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#define IS_INF_OR_NAN(x)        (!isfinite(x))
#else
#define IS_INF_OR_NAN(x)        (!finite(x))
#endif


/**
 * Convert float to int by rounding to nearest integer, away from zero.
 */
static inline int IROUND(float f)
{
   return (int) ((f >= 0.0F) ? (f + 0.5F) : (f - 0.5F));
}

/**
 * Convert double to int by rounding to nearest integer, away from zero.
 */
static inline int IROUNDD(double d)
{
   return (int) ((d >= 0.0) ? (d + 0.5) : (d - 0.5));
}

/**
 * Convert float to int64 by rounding to nearest integer.
 */
static inline int64_t IROUND64(float f)
{
   return (int64_t) ((f >= 0.0F) ? (f + 0.5F) : (f - 0.5F));
}


/**
 * Convert positive float to int by rounding to nearest integer.
 */
static inline int IROUND_POS(float f)
{
   assert(f >= 0.0F);
   return (int) (f + 0.5F);
}

/** Return (as an integer) floor of float */
static inline int IFLOOR(float f)
{
#if defined(USE_X86_ASM) && defined(__GNUC__) && defined(__i386__)
   /*
    * IEEE floor for computers that round to nearest or even.
    * 'f' must be between -4194304 and 4194303.
    * This floor operation is done by "(iround(f + .5) + iround(f - .5)) >> 1",
    * but uses some IEEE specific tricks for better speed.
    * Contributed by Josh Vanderhoof
    */
   int ai, bi;
   double af, bf;
   af = (3 << 22) + 0.5 + (double)f;
   bf = (3 << 22) + 0.5 - (double)f;
   /* GCC generates an extra fstp/fld without this. */
   __asm__ ("fstps %0" : "=m" (ai) : "t" (af) : "st");
   __asm__ ("fstps %0" : "=m" (bi) : "t" (bf) : "st");
   return (ai - bi) >> 1;
#else
   int ai, bi;
   double af, bf;
   fi_type u;
   af = (3 << 22) + 0.5 + (double)f;
   bf = (3 << 22) + 0.5 - (double)f;
   u.f = (float) af;  ai = u.i;
   u.f = (float) bf;  bi = u.i;
   return (ai - bi) >> 1;
#endif
}


/**
 * Is x a power of two?
 */
static inline int
_mesa_is_pow_two(int x)
{
   return !(x & (x - 1));
}

/**
 * Round given integer to next higer power of two
 * If X is zero result is undefined.
 *
 * Source for the fallback implementation is
 * Sean Eron Anderson's webpage "Bit Twiddling Hacks"
 * http://graphics.stanford.edu/~seander/bithacks.html
 *
 * When using builtin function have to do some work
 * for case when passed values 1 to prevent hiting
 * undefined result from __builtin_clz. Undefined
 * results would be different depending on optimization
 * level used for build.
 */
static inline int32_t
_mesa_next_pow_two_32(uint32_t x)
{
#ifdef HAVE___BUILTIN_CLZ
	uint32_t y = (x != 1);
	return (1 + y) << ((__builtin_clz(x - y) ^ 31) );
#else
	x--;
	x |= x >> 1;
	x |= x >> 2;
	x |= x >> 4;
	x |= x >> 8;
	x |= x >> 16;
	x++;
	return x;
#endif
}

static inline int64_t
_mesa_next_pow_two_64(uint64_t x)
{
#ifdef HAVE___BUILTIN_CLZLL
	uint64_t y = (x != 1);
	STATIC_ASSERT(sizeof(x) == sizeof(long long));
	return (1 + y) << ((__builtin_clzll(x - y) ^ 63));
#else
	x--;
	x |= x >> 1;
	x |= x >> 2;
	x |= x >> 4;
	x |= x >> 8;
	x |= x >> 16;
	x |= x >> 32;
	x++;
	return x;
#endif
}


/*
 * Returns the floor form of binary logarithm for a 32-bit integer.
 */
static inline unsigned
_mesa_logbase2(unsigned n)
{
#ifdef HAVE___BUILTIN_CLZ
   return (31 - __builtin_clz(n | 1));
#else
   unsigned pos = 0;
   if (n >= 1<<16) { n >>= 16; pos += 16; }
   if (n >= 1<< 8) { n >>=  8; pos +=  8; }
   if (n >= 1<< 4) { n >>=  4; pos +=  4; }
   if (n >= 1<< 2) { n >>=  2; pos +=  2; }
   if (n >= 1<< 1) {           pos +=  1; }
   return pos;
#endif
}


/**********************************************************************
 * Functions
 */

extern void *
_mesa_align_malloc( size_t bytes, unsigned long alignment );

extern void *
_mesa_align_calloc( size_t bytes, unsigned long alignment );

extern void
_mesa_align_free( void *ptr );

extern void *
_mesa_align_realloc(void *oldBuffer, size_t oldSize, size_t newSize,
                    unsigned long alignment);

extern int
_mesa_snprintf( char *str, size_t size, const char *fmt, ... ) PRINTFLIKE(3, 4);

extern int
_mesa_vsnprintf(char *str, size_t size, const char *fmt, va_list arg);


#if defined(_WIN32) && !defined(HAVE_STRTOK_R)
#define strtok_r strtok_s
#endif

#ifdef __cplusplus
}
#endif


#endif /* IMPORTS_H */
