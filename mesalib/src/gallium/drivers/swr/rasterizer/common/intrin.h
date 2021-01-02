/****************************************************************************
 * Copyright (C) 2014-2015 Intel Corporation.   All Rights Reserved.
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 ****************************************************************************/

#ifndef __SWR_INTRIN_H__
#define __SWR_INTRIN_H__

#include "os.h"

#if !defined(SIMD_ARCH)
#define SIMD_ARCH KNOB_ARCH
#endif

#include "simdlib_types.hpp"

typedef SIMDImpl::SIMD128Impl::Float   simd4scalar;
typedef SIMDImpl::SIMD128Impl::Double  simd4scalard;
typedef SIMDImpl::SIMD128Impl::Integer simd4scalari;
typedef SIMDImpl::SIMD128Impl::Vec4    simd4vector;
typedef SIMDImpl::SIMD128Impl::Mask    simd4mask;

typedef SIMDImpl::SIMD256Impl::Float   simd8scalar;
typedef SIMDImpl::SIMD256Impl::Double  simd8scalard;
typedef SIMDImpl::SIMD256Impl::Integer simd8scalari;
typedef SIMDImpl::SIMD256Impl::Vec4    simd8vector;
typedef SIMDImpl::SIMD256Impl::Mask    simd8mask;

typedef SIMDImpl::SIMD512Impl::Float   simd16scalar;
typedef SIMDImpl::SIMD512Impl::Double  simd16scalard;
typedef SIMDImpl::SIMD512Impl::Integer simd16scalari;
typedef SIMDImpl::SIMD512Impl::Vec4    simd16vector;
typedef SIMDImpl::SIMD512Impl::Mask    simd16mask;

#if KNOB_SIMD_WIDTH == 8
typedef simd8scalar  simdscalar;
typedef simd8scalard simdscalard;
typedef simd8scalari simdscalari;
typedef simd8vector  simdvector;
typedef simd8mask    simdmask;
#else
#error Unsupported vector width
#endif

INLINE
UINT pdep_u32(UINT a, UINT mask)
{
#if KNOB_ARCH >= KNOB_ARCH_AVX2
    return _pdep_u32(a, mask);
#else
    UINT result = 0;

    // copied from http://wm.ite.pl/articles/pdep-soft-emu.html
    // using bsf instead of funky loop
    unsigned long maskIndex = 0;
    while (_BitScanForward(&maskIndex, mask))
    {
        // 1. isolate lowest set bit of mask
        const UINT lowest = 1 << maskIndex;

        // 2. populate LSB from src
        const UINT LSB = (UINT)((int)(a << 31) >> 31);

        // 3. copy bit from mask
        result |= LSB & lowest;

        // 4. clear lowest bit
        mask &= ~lowest;

        // 5. prepare for next iteration
        a >>= 1;
    }

    return result;
#endif
}

INLINE
UINT pext_u32(UINT a, UINT mask)
{
#if KNOB_ARCH >= KNOB_ARCH_AVX2
    return _pext_u32(a, mask);
#else
    UINT     result = 0;
    unsigned long maskIndex;
    uint32_t currentBit = 0;
    while (_BitScanForward(&maskIndex, mask))
    {
        // 1. isolate lowest set bit of mask
        const UINT lowest = 1 << maskIndex;

        // 2. copy bit from mask
        result |= ((a & lowest) > 0) << currentBit++;

        // 3. clear lowest bit
        mask &= ~lowest;
    }
    return result;
#endif
}

#endif //__SWR_INTRIN_H__
