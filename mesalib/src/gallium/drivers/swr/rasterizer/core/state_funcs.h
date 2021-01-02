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
 *
 * @file state.h
 *
 * @brief Definitions for API state - complex function implementation.
 *
 ******************************************************************************/
#pragma once

#include "core/state.h"
#include "common/simdintrin.h"

template <typename MaskT>
INLINE __m128i SWR_MULTISAMPLE_POS::expandThenBlend4(uint32_t* min, uint32_t* max)
{
    __m128i vMin = _mm_set1_epi32(*min);
    __m128i vMax = _mm_set1_epi32(*max);
    return _simd_blend4_epi32<MaskT::value>(vMin, vMax);
}

INLINE void SWR_MULTISAMPLE_POS::PrecalcSampleData(int numSamples)
{
    for (int i = 0; i < numSamples; i++)
    {
        _vXi[i] = _mm_set1_epi32(_xi[i]);
        _vYi[i] = _mm_set1_epi32(_yi[i]);
        _vX[i]  = _simd_set1_ps(_x[i]);
        _vY[i]  = _simd_set1_ps(_y[i]);
    }
    // precalculate the raster tile BB for the rasterizer.
    CalcTileSampleOffsets(numSamples);
}

INLINE void SWR_MULTISAMPLE_POS::CalcTileSampleOffsets(int numSamples)
{
    auto minXi  = std::min_element(std::begin(_xi), &_xi[numSamples]);
    auto maxXi  = std::max_element(std::begin(_xi), &_xi[numSamples]);
    using xMask = std::integral_constant<int, 0xA>;
    // BR(max),    BL(min),    UR(max),    UL(min)
    tileSampleOffsetsX = expandThenBlend4<xMask>(minXi, maxXi);

    auto minYi  = std::min_element(std::begin(_yi), &_yi[numSamples]);
    auto maxYi  = std::max_element(std::begin(_yi), &_yi[numSamples]);
    using yMask = std::integral_constant<int, 0xC>;
    // BR(max),    BL(min),    UR(max),    UL(min)
    tileSampleOffsetsY = expandThenBlend4<yMask>(minYi, maxYi);
};
