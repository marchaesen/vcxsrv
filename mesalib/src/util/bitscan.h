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

#include <stdint.h>

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


#ifdef __cplusplus
}
#endif

#endif /* BITSCAN_H */
