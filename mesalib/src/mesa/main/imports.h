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
#include "compiler.h"
#include "glheader.h"
#include "errors.h"

#ifdef __cplusplus
extern "C" {
#endif


/**********************************************************************/
/** Memory macros */
/*@{*/

/** Allocate a structure of type \p T */
#define MALLOC_STRUCT(T)   (struct T *) malloc(sizeof(struct T))
/** Allocate and zero a structure of type \p T */
#define CALLOC_STRUCT(T)   (struct T *) calloc(1, sizeof(struct T))

/*@}*/


/*
 * For GL_ARB_vertex_buffer_object we need to treat vertex array pointers
 * as offsets into buffer stores.  Since the vertex array pointer and
 * buffer store pointer are both pointers and we need to add them, we use
 * this macro.
 * Both pointers/offsets are expressed in bytes.
 */
#define ADD_POINTERS(A, B)  ( (GLubyte *) (A) + (uintptr_t) (B) )


/**
 * Sometimes we treat GLfloats as GLints.  On x86 systems, moving a float
 * as a int (thereby using integer registers instead of FP registers) is
 * a performance win.  Typically, this can be done with ordinary casts.
 * But with gcc's -fstrict-aliasing flag (which defaults to on in gcc 3.0)
 * these casts generate warnings.
 * The following union typedef is used to solve that.
 */
typedef union { GLfloat f; GLint i; GLuint u; } fi_type;



#if defined(_MSC_VER)
#define strcasecmp(s1, s2) _stricmp(s1, s2)
#endif
/*@}*/


/***
 *** LOG2: Log base 2 of float
 ***/
static inline GLfloat LOG2(GLfloat x)
{
#if 0
   /* This is pretty fast, but not accurate enough (only 2 fractional bits).
    * Based on code from http://www.stereopsis.com/log2.html
    */
   const GLfloat y = x * x * x * x;
   const GLuint ix = *((GLuint *) &y);
   const GLuint exp = (ix >> 23) & 0xFF;
   const GLint log2 = ((GLint) exp) - 127;
   return (GLfloat) log2 * (1.0 / 4.0);  /* 4, because of x^4 above */
#endif
   /* Pretty fast, and accurate.
    * Based on code from http://www.flipcode.com/totd/
    */
   fi_type num;
   GLint log_2;
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
static inline GLint64 IROUND64(float f)
{
   return (GLint64) ((f >= 0.0F) ? (f + 0.5F) : (f - 0.5F));
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
static inline GLuint
_mesa_logbase2(GLuint n)
{
#ifdef HAVE___BUILTIN_CLZ
   return (31 - __builtin_clz(n | 1));
#else
   GLuint pos = 0;
   if (n >= 1<<16) { n >>= 16; pos += 16; }
   if (n >= 1<< 8) { n >>=  8; pos +=  8; }
   if (n >= 1<< 4) { n >>=  4; pos +=  4; }
   if (n >= 1<< 2) { n >>=  2; pos +=  2; }
   if (n >= 1<< 1) {           pos +=  1; }
   return pos;
#endif
}


/**
 * Return 1 if this is a little endian machine, 0 if big endian.
 */
static inline GLboolean
_mesa_little_endian(void)
{
   const GLuint ui = 1; /* intentionally not static */
   return *((const GLubyte *) &ui);
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

extern void *
_mesa_exec_malloc( GLuint size );

extern void 
_mesa_exec_free( void *addr );


#ifndef FFS_DEFINED
#define FFS_DEFINED 1
#ifdef HAVE___BUILTIN_FFS
#define ffs __builtin_ffs
#else
extern int ffs(int i);
#endif

#ifdef HAVE___BUILTIN_FFSLL
#define ffsll __builtin_ffsll
#else
extern int ffsll(long long int i);
#endif
#endif /* FFS_DEFINED */


#ifdef HAVE___BUILTIN_POPCOUNT
#define _mesa_bitcount(i) __builtin_popcount(i)
#else
extern unsigned int
_mesa_bitcount(unsigned int n);
#endif

#ifdef HAVE___BUILTIN_POPCOUNTLL
#define _mesa_bitcount_64(i) __builtin_popcountll(i)
#else
extern unsigned int
_mesa_bitcount_64(uint64_t n);
#endif

/**
 * Find the last (most significant) bit set in a word.
 *
 * Essentially ffs() in the reverse direction.
 */
static inline unsigned int
_mesa_fls(unsigned int n)
{
#ifdef HAVE___BUILTIN_CLZ
   return n == 0 ? 0 : 32 - __builtin_clz(n);
#else
   unsigned int v = 1;

   if (n == 0)
      return 0;

   while (n >>= 1)
       v++;

   return v;
#endif
}

/**
 * Find the last (most significant) bit set in a uint64_t value.
 *
 * Essentially ffsll() in the reverse direction.
 */
static inline unsigned int
_mesa_flsll(uint64_t n)
{
#ifdef HAVE___BUILTIN_CLZLL
   return n == 0 ? 0 : 64 - __builtin_clzll(n);
#else
   unsigned int v = 1;

   if (n == 0)
      return 0;

   while (n >>= 1)
       v++;

   return v;
#endif
}

static inline bool
_mesa_half_is_negative(GLhalfARB h)
{
   return h & 0x8000;
}

extern unsigned int
_mesa_str_checksum(const char *str);

extern int
_mesa_snprintf( char *str, size_t size, const char *fmt, ... ) PRINTFLIKE(3, 4);

extern int
_mesa_vsnprintf(char *str, size_t size, const char *fmt, va_list arg);


#if defined(_MSC_VER) && !defined(snprintf)
#define snprintf _snprintf
#endif

#if defined(_WIN32) && !defined(strtok_r)
#define strtok_r strtok_s
#endif

#ifdef __cplusplus
}
#endif


#endif /* IMPORTS_H */
