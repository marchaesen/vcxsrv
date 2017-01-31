/**************************************************************************
 *
 * Copyright 2008 VMware, Inc.
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
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/


#ifndef BITSCAN_H
#define BITSCAN_H

#include <assert.h>
#include <stdint.h>
#include <string.h>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

#include "c99_compat.h"

#ifdef __cplusplus
extern "C" {
#endif


/**
 * Find first bit set in word.  Least significant bit is 1.
 * Return 0 if no bits set.
 */
#ifdef HAVE___BUILTIN_FFS
#define ffs __builtin_ffs
#elif defined(_MSC_VER) && (_M_IX86 || _M_ARM || _M_AMD64 || _M_IA64)
static inline
int ffs(unsigned i)
{
   unsigned long index;
   if (_BitScanForward(&index, i))
      return index + 1;
   else
      return 0;
}
#else
extern
int ffs(unsigned i);
#endif

#ifdef HAVE___BUILTIN_FFSLL
#define ffsll __builtin_ffsll
#elif defined(_MSC_VER) && (_M_AMD64 || _M_ARM || _M_IA64)
static inline int
ffsll(uint64_t i)
{
   unsigned long index;
   if (_BitScanForward64(&index, i))
      return index + 1;
   else
      return 0;
}
#else
extern int
ffsll(uint64_t val);
#endif


/* Destructively loop over all of the bits in a mask as in:
 *
 * while (mymask) {
 *   int i = u_bit_scan(&mymask);
 *   ... process element i
 * }
 *
 */
static inline int
u_bit_scan(unsigned *mask)
{
   const int i = ffs(*mask) - 1;
   *mask ^= (1u << i);
   return i;
}

static inline int
u_bit_scan64(uint64_t *mask)
{
   const int i = ffsll(*mask) - 1;
   *mask ^= (((uint64_t)1) << i);
   return i;
}

/* For looping over a bitmask when you want to loop over consecutive bits
 * manually, for example:
 *
 * while (mask) {
 *    int start, count, i;
 *
 *    u_bit_scan_consecutive_range(&mask, &start, &count);
 *
 *    for (i = 0; i < count; i++)
 *       ... process element (start+i)
 * }
 */
static inline void
u_bit_scan_consecutive_range(unsigned *mask, int *start, int *count)
{
   if (*mask == 0xffffffff) {
      *start = 0;
      *count = 32;
      *mask = 0;
      return;
   }
   *start = ffs(*mask) - 1;
   *count = ffs(~(*mask >> *start)) - 1;
   *mask &= ~(((1u << *count) - 1) << *start);
}

static inline void
u_bit_scan_consecutive_range64(uint64_t *mask, int *start, int *count)
{
   if (*mask == ~0llu) {
      *start = 0;
      *count = 64;
      *mask = 0;
      return;
   }
   *start = ffsll(*mask) - 1;
   *count = ffsll(~(*mask >> *start)) - 1;
   *mask &= ~(((((uint64_t)1) << *count) - 1) << *start);
}


/**
 * Find last bit set in a word.  The least significant bit is 1.
 * Return 0 if no bits are set.
 * Essentially ffs() in the reverse direction.
 */
static inline unsigned
util_last_bit(unsigned u)
{
#if defined(HAVE___BUILTIN_CLZ)
   return u == 0 ? 0 : 32 - __builtin_clz(u);
#elif defined(_MSC_VER) && (_M_IX86 || _M_ARM || _M_AMD64 || _M_IA64)
   unsigned long index;
   if (_BitScanReverse(&index, u))
      return index + 1;
   else
      return 0;
#else
   unsigned r = 0;
   while (u) {
      r++;
      u >>= 1;
   }
   return r;
#endif
}

/**
 * Find last bit set in a word.  The least significant bit is 1.
 * Return 0 if no bits are set.
 * Essentially ffsll() in the reverse direction.
 */
static inline unsigned
util_last_bit64(uint64_t u)
{
#if defined(HAVE___BUILTIN_CLZLL)
   return u == 0 ? 0 : 64 - __builtin_clzll(u);
#elif defined(_MSC_VER) && (_M_AMD64 || _M_ARM || _M_IA64)
   unsigned long index;
   if (_BitScanReverse64(&index, u))
      return index + 1;
   else
      return 0;
#else
   unsigned r = 0;
   while (u) {
      r++;
      u >>= 1;
   }
   return r;
#endif
}

/**
 * Find last bit in a word that does not match the sign bit. The least
 * significant bit is 1.
 * Return 0 if no bits are set.
 */
static inline unsigned
util_last_bit_signed(int i)
{
   if (i >= 0)
      return util_last_bit(i);
   else
      return util_last_bit(~(unsigned)i);
}

/* Returns a bitfield in which the first count bits starting at start are
 * set.
 */
static inline unsigned
u_bit_consecutive(unsigned start, unsigned count)
{
   assert(start + count <= 32);
   if (count == 32)
      return ~0;
   return ((1u << count) - 1) << start;
}

static inline uint64_t
u_bit_consecutive64(unsigned start, unsigned count)
{
   assert(start + count <= 64);
   if (count == 64)
      return ~(uint64_t)0;
   return (((uint64_t)1 << count) - 1) << start;
}


#ifdef __cplusplus
}
#endif

#endif /* BITSCAN_H */
