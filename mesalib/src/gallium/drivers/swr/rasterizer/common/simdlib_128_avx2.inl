/****************************************************************************
 * Copyright (C) 2017 Intel Corporation.   All Rights Reserved.
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
#if !defined(__SIMD_LIB_AVX2_HPP__)
#error Do not include this file directly, use "simdlib.hpp" instead.
#endif

//============================================================================
// SIMD4 AVX (2) implementation
//
// Since this implementation inherits from the AVX (1) implementation,
// the only operations below ones that replace AVX (1) operations.
// Only 2 shifts and 2 gathers were introduced with AVX 2
// Also, add native support for FMA operations
//============================================================================
#define SIMD_WRAPPER_3(op) \
    static SIMDINLINE Float SIMDCALL op(Float a, Float b, Float c) { return _mm_##op(a, b, c); }

SIMD_WRAPPER_3(fmadd_ps); // return (a * b) + c
SIMD_WRAPPER_3(fmsub_ps); // return (a * b) - c

static SIMDINLINE Integer SIMDCALL sllv_epi32(Integer vA, Integer vB) // return a << b      (uint32)
{
    return _mm_sllv_epi32(vA, vB);
}

static SIMDINLINE Integer SIMDCALL srlv_epi32(Integer vA, Integer vB) // return a >> b      (uint32)
{
    return _mm_srlv_epi32(vA, vB);
}

template <ScaleFactor ScaleT = ScaleFactor::SF_1>
static SIMDINLINE Float SIMDCALL
                        i32gather_ps(float const* p, Integer idx) // return *(float*)(((int8*)p) + (idx * ScaleT))
{
    return _mm_i32gather_ps(p, idx, static_cast<const int>(ScaleT));
}

// for each element: (mask & (1 << 31)) ? (i32gather_ps<ScaleT>(p, idx), mask = 0) : old
template <ScaleFactor ScaleT = ScaleFactor::SF_1>
static SIMDINLINE Float SIMDCALL
                        mask_i32gather_ps(Float old, float const* p, Integer idx, Float mask)
{
    return _mm_mask_i32gather_ps(old, p, idx, mask, static_cast<const int>(ScaleT));
}

#undef SIMD_WRAPPER_3
