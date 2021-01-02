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
 * @file multisample.h
 *
 ******************************************************************************/

#pragma once

#include "context.h"
#include "format_traits.h"

//////////////////////////////////////////////////////////////////////////
/// @brief convenience typedef for testing for single sample case
typedef std::integral_constant<int, 1> SingleSampleT;

INLINE
SWR_MULTISAMPLE_COUNT GetSampleCount(uint32_t numSamples)
{
    switch (numSamples)
    {
    case 1:
        return SWR_MULTISAMPLE_1X;
    case 2:
        return SWR_MULTISAMPLE_2X;
    case 4:
        return SWR_MULTISAMPLE_4X;
    case 8:
        return SWR_MULTISAMPLE_8X;
    case 16:
        return SWR_MULTISAMPLE_16X;
    default:
        assert(0);
        return SWR_MULTISAMPLE_1X;
    }
}

// hardcoded offsets based on Direct3d standard multisample positions
// 8 x 8 pixel grid ranging from (0, 0) to (15, 15), with (0, 0) = UL pixel corner
// coords are 0.8 fixed point offsets from (0, 0)
template <SWR_MULTISAMPLE_COUNT sampleCount, bool isCenter = false>
struct MultisampleTraits
{
    INLINE static float       X(uint32_t sampleNum) = delete;
    INLINE static float       Y(uint32_t sampleNum) = delete;
    INLINE static simdscalari FullSampleMask()      = delete;

    static const uint32_t numSamples = 0;
};

template <>
struct MultisampleTraits<SWR_MULTISAMPLE_1X, false>
{
    INLINE static float       X(uint32_t sampleNum) { return samplePosX[sampleNum]; };
    INLINE static float       Y(uint32_t sampleNum) { return samplePosY[sampleNum]; };
    INLINE static simdscalari FullSampleMask() { return _simd_set1_epi32(0x1); };

    static const uint32_t              numSamples         = 1;
    static const uint32_t              numCoverageSamples = 1;
    static const SWR_MULTISAMPLE_COUNT sampleCount        = SWR_MULTISAMPLE_1X;
    static constexpr uint32_t          samplePosXi[1]     = {0x80};
    static constexpr uint32_t          samplePosYi[1]     = {0x80};
    static constexpr float             samplePosX[1]      = {0.5f};
    static constexpr float             samplePosY[1]      = {0.5f};
};

template <>
struct MultisampleTraits<SWR_MULTISAMPLE_1X, true>
{
    INLINE static float       X(uint32_t sampleNum) { return 0.5f; };
    INLINE static float       Y(uint32_t sampleNum) { return 0.5f; };
    INLINE static simdscalari FullSampleMask() { return _simd_set1_epi32(0x1); };

    static const uint32_t              numSamples         = 1;
    static const uint32_t              numCoverageSamples = 1;
    static const SWR_MULTISAMPLE_COUNT sampleCount        = SWR_MULTISAMPLE_1X;
    static constexpr uint32_t          samplePosXi[1]     = {0x80};
    static constexpr uint32_t          samplePosYi[1]     = {0x80};
    static constexpr float             samplePosX[1]      = {0.5f};
    static constexpr float             samplePosY[1]      = {0.5f};
};

template <>
struct MultisampleTraits<SWR_MULTISAMPLE_2X, false>
{
    INLINE static float X(uint32_t sampleNum)
    {
        SWR_ASSERT(sampleNum < numSamples);
        return samplePosX[sampleNum];
    };
    INLINE static float Y(uint32_t sampleNum)
    {
        SWR_ASSERT(sampleNum < numSamples);
        return samplePosY[sampleNum];
    };
    INLINE static simdscalari FullSampleMask()
    {
        static const simdscalari mask = _simd_set1_epi32(0x3);
        return mask;
    }

    static const uint32_t              numSamples         = 2;
    static const uint32_t              numCoverageSamples = 2;
    static const SWR_MULTISAMPLE_COUNT sampleCount        = SWR_MULTISAMPLE_2X;
    static constexpr uint32_t          samplePosXi[2]     = {0xC0, 0x40};
    static constexpr uint32_t          samplePosYi[2]     = {0xC0, 0x40};
    static constexpr float             samplePosX[2]      = {0.75f, 0.25f};
    static constexpr float             samplePosY[2]      = {0.75f, 0.25f};
};

template <>
struct MultisampleTraits<SWR_MULTISAMPLE_2X, true>
{
    INLINE static float       X(uint32_t sampleNum) { return 0.5f; };
    INLINE static float       Y(uint32_t sampleNum) { return 0.5f; };
    INLINE static simdscalari FullSampleMask()
    {
        static const simdscalari mask = _simd_set1_epi32(0x3);
        return mask;
    }
    static const uint32_t              numSamples         = 2;
    static const uint32_t              numCoverageSamples = 1;
    static const SWR_MULTISAMPLE_COUNT sampleCount        = SWR_MULTISAMPLE_2X;
    static constexpr uint32_t          samplePosXi[2]     = {0x80, 0x80};
    static constexpr uint32_t          samplePosYi[2]     = {0x80, 0x80};
    static constexpr float             samplePosX[2]      = {0.5f, 0.5f};
    static constexpr float             samplePosY[2]      = {0.5f, 0.5f};
};

template <>
struct MultisampleTraits<SWR_MULTISAMPLE_4X, false>
{
    INLINE static float X(uint32_t sampleNum)
    {
        SWR_ASSERT(sampleNum < numSamples);
        return samplePosX[sampleNum];
    };
    INLINE static float Y(uint32_t sampleNum)
    {
        SWR_ASSERT(sampleNum < numSamples);
        return samplePosY[sampleNum];
    };
    INLINE static simdscalari FullSampleMask()
    {
        static const simdscalari mask = _simd_set1_epi32(0xF);
        return mask;
    }

    static const uint32_t              numSamples         = 4;
    static const uint32_t              numCoverageSamples = 4;
    static const SWR_MULTISAMPLE_COUNT sampleCount        = SWR_MULTISAMPLE_4X;
    static constexpr uint32_t          samplePosXi[4]     = {0x60, 0xE0, 0x20, 0xA0};
    static constexpr uint32_t          samplePosYi[4]     = {0x20, 0x60, 0xA0, 0xE0};
    static constexpr float             samplePosX[4]      = {0.375f, 0.875f, 0.125f, 0.625f};
    static constexpr float             samplePosY[4]      = {0.125f, 0.375f, 0.625f, 0.875f};
};

template <>
struct MultisampleTraits<SWR_MULTISAMPLE_4X, true>
{
    INLINE static float       X(uint32_t sampleNum) { return 0.5f; };
    INLINE static float       Y(uint32_t sampleNum) { return 0.5f; };
    INLINE static simdscalari FullSampleMask()
    {
        static const simdscalari mask = _simd_set1_epi32(0xF);
        return mask;
    }

    static const uint32_t              numSamples         = 4;
    static const uint32_t              numCoverageSamples = 1;
    static const SWR_MULTISAMPLE_COUNT sampleCount        = SWR_MULTISAMPLE_4X;
    static constexpr uint32_t          samplePosXi[4]     = {0x80, 0x80, 0x80, 0x80};
    static constexpr uint32_t          samplePosYi[4]     = {0x80, 0x80, 0x80, 0x80};
    static constexpr float             samplePosX[4]      = {0.5f, 0.5f, 0.5f, 0.5f};
    static constexpr float             samplePosY[4]      = {0.5f, 0.5f, 0.5f, 0.5f};
};

template <>
struct MultisampleTraits<SWR_MULTISAMPLE_8X, false>
{
    INLINE static float X(uint32_t sampleNum)
    {
        SWR_ASSERT(sampleNum < numSamples);
        return samplePosX[sampleNum];
    };
    INLINE static float Y(uint32_t sampleNum)
    {
        SWR_ASSERT(sampleNum < numSamples);
        return samplePosY[sampleNum];
    };
    INLINE static simdscalari FullSampleMask()
    {
        static const simdscalari mask = _simd_set1_epi32(0xFF);
        return mask;
    }

    static const uint32_t              numSamples         = 8;
    static const uint32_t              numCoverageSamples = 8;
    static const SWR_MULTISAMPLE_COUNT sampleCount        = SWR_MULTISAMPLE_8X;
    static constexpr uint32_t samplePosXi[8] = {0x90, 0x70, 0xD0, 0x50, 0x30, 0x10, 0xB0, 0xF0};
    static constexpr uint32_t samplePosYi[8] = {0x50, 0xB0, 0x90, 0x30, 0xD0, 0x70, 0xF0, 0x10};
    static constexpr float    samplePosX[8]  = {
        0.5625f, 0.4375f, 0.8125f, 0.3125f, 0.1875f, 0.0625f, 0.6875f, 0.9375f};
    static constexpr float samplePosY[8] = {
        0.3125f, 0.6875f, 0.5625f, 0.1875f, 0.8125f, 0.4375f, 0.9375f, 0.0625f};
};

template <>
struct MultisampleTraits<SWR_MULTISAMPLE_8X, true>
{
    INLINE static float       X(uint32_t sampleNum) { return 0.5f; };
    INLINE static float       Y(uint32_t sampleNum) { return 0.5f; };
    INLINE static simdscalari FullSampleMask()
    {
        static const simdscalari mask = _simd_set1_epi32(0xFF);
        return mask;
    }
    static const uint32_t              numSamples         = 8;
    static const uint32_t              numCoverageSamples = 1;
    static const SWR_MULTISAMPLE_COUNT sampleCount        = SWR_MULTISAMPLE_8X;
    static constexpr uint32_t samplePosXi[8] = {0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80};
    static constexpr uint32_t samplePosYi[8] = {0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80};
    static constexpr float    samplePosX[8]  = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
    static constexpr float    samplePosY[8]  = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
};

template <>
struct MultisampleTraits<SWR_MULTISAMPLE_16X, false>
{
    INLINE static float X(uint32_t sampleNum)
    {
        SWR_ASSERT(sampleNum < numSamples);
        return samplePosX[sampleNum];
    };
    INLINE static float Y(uint32_t sampleNum)
    {
        SWR_ASSERT(sampleNum < numSamples);
        return samplePosY[sampleNum];
    };
    INLINE static simdscalari FullSampleMask()
    {
        static const simdscalari mask = _simd_set1_epi32(0xFFFF);
        return mask;
    }

    static const uint32_t              numSamples         = 16;
    static const uint32_t              numCoverageSamples = 16;
    static const SWR_MULTISAMPLE_COUNT sampleCount        = SWR_MULTISAMPLE_16X;
    static constexpr uint32_t          samplePosXi[16]    = {0x90,
                                                 0x70,
                                                 0x50,
                                                 0xC0,
                                                 0x30,
                                                 0xA0,
                                                 0xD0,
                                                 0xB0,
                                                 0x60,
                                                 0x80,
                                                 0x40,
                                                 0x20,
                                                 0x00,
                                                 0xF0,
                                                 0xE0,
                                                 0x10};
    static constexpr uint32_t          samplePosYi[16]    = {0x90,
                                                 0x50,
                                                 0xA0,
                                                 0x70,
                                                 0x60,
                                                 0xD0,
                                                 0xB0,
                                                 0x30,
                                                 0xE0,
                                                 0x10,
                                                 0x20,
                                                 0xC0,
                                                 0x80,
                                                 0x40,
                                                 0xF0,
                                                 0x00};
    static constexpr float             samplePosX[16]     = {0.5625f,
                                             0.4375f,
                                             0.3125f,
                                             0.7500f,
                                             0.1875f,
                                             0.6250f,
                                             0.8125f,
                                             0.6875f,
                                             0.3750f,
                                             0.5000f,
                                             0.2500f,
                                             0.1250f,
                                             0.0000f,
                                             0.9375f,
                                             0.8750f,
                                             0.0625f};
    static constexpr float             samplePosY[16]     = {0.5625f,
                                             0.3125f,
                                             0.6250f,
                                             0.4375f,
                                             0.3750f,
                                             0.8125f,
                                             0.6875f,
                                             0.1875f,
                                             0.8750f,
                                             0.0625f,
                                             0.1250f,
                                             0.7500f,
                                             0.5000f,
                                             0.2500f,
                                             0.9375f,
                                             0.0000f};
};

template <>
struct MultisampleTraits<SWR_MULTISAMPLE_16X, true>
{
    INLINE static float       X(uint32_t sampleNum) { return 0.5f; };
    INLINE static float       Y(uint32_t sampleNum) { return 0.5f; };
    INLINE static simdscalari FullSampleMask()
    {
        static const simdscalari mask = _simd_set1_epi32(0xFFFF);
        return mask;
    }
    static const uint32_t              numSamples         = 16;
    static const uint32_t              numCoverageSamples = 1;
    static const SWR_MULTISAMPLE_COUNT sampleCount        = SWR_MULTISAMPLE_16X;
    static constexpr uint32_t          samplePosXi[16]    = {0x80,
                                                 0x80,
                                                 0x80,
                                                 0x80,
                                                 0x80,
                                                 0x80,
                                                 0x80,
                                                 0x80,
                                                 0x80,
                                                 0x80,
                                                 0x80,
                                                 0x80,
                                                 0x80,
                                                 0x80,
                                                 0x80,
                                                 0x80};
    static constexpr uint32_t          samplePosYi[16]    = {0x80,
                                                 0x80,
                                                 0x80,
                                                 0x80,
                                                 0x80,
                                                 0x80,
                                                 0x80,
                                                 0x80,
                                                 0x80,
                                                 0x80,
                                                 0x80,
                                                 0x80,
                                                 0x80,
                                                 0x80,
                                                 0x80,
                                                 0x80};
    static constexpr float             samplePosX[16]     = {0.5f,
                                             0.5f,
                                             0.5f,
                                             0.5f,
                                             0.5f,
                                             0.5f,
                                             0.5f,
                                             0.5f,
                                             0.5f,
                                             0.5f,
                                             0.5f,
                                             0.5f,
                                             0.5f,
                                             0.5f,
                                             0.5f,
                                             0.5f};
    static constexpr float             samplePosY[16]     = {0.5f,
                                             0.5f,
                                             0.5f,
                                             0.5f,
                                             0.5f,
                                             0.5f,
                                             0.5f,
                                             0.5f,
                                             0.5f,
                                             0.5f,
                                             0.5f,
                                             0.5f,
                                             0.5f,
                                             0.5f,
                                             0.5f,
                                             0.5f};
};

INLINE
bool isNonStandardPattern(const SWR_MULTISAMPLE_COUNT sampleCount,
                          const SWR_MULTISAMPLE_POS&  samplePos)
{
    // detect if we're using standard or center sample patterns
    const uint32_t *standardPosX, *standardPosY;
    switch (sampleCount)
    {
    case SWR_MULTISAMPLE_1X:
        standardPosX = MultisampleTraits<SWR_MULTISAMPLE_1X>::samplePosXi;
        standardPosY = MultisampleTraits<SWR_MULTISAMPLE_1X>::samplePosYi;
        break;
    case SWR_MULTISAMPLE_2X:
        standardPosX = MultisampleTraits<SWR_MULTISAMPLE_2X>::samplePosXi;
        standardPosY = MultisampleTraits<SWR_MULTISAMPLE_2X>::samplePosYi;
        break;
    case SWR_MULTISAMPLE_4X:
        standardPosX = MultisampleTraits<SWR_MULTISAMPLE_4X>::samplePosXi;
        standardPosY = MultisampleTraits<SWR_MULTISAMPLE_4X>::samplePosYi;
        break;
    case SWR_MULTISAMPLE_8X:
        standardPosX = MultisampleTraits<SWR_MULTISAMPLE_8X>::samplePosXi;
        standardPosY = MultisampleTraits<SWR_MULTISAMPLE_8X>::samplePosYi;
        break;
    case SWR_MULTISAMPLE_16X:
        standardPosX = MultisampleTraits<SWR_MULTISAMPLE_16X>::samplePosXi;
        standardPosY = MultisampleTraits<SWR_MULTISAMPLE_16X>::samplePosYi;
        break;
    default:
        break;
    }

    // scan sample pattern for standard or center
    uint32_t numSamples  = GetNumSamples(sampleCount);
    bool     bIsStandard = true;
    if (numSamples > 1)
    {
        for (uint32_t i = 0; i < numSamples; i++)
        {
            bIsStandard =
                (standardPosX[i] == samplePos.Xi(i)) || (standardPosY[i] == samplePos.Yi(i));
            if (!bIsStandard)
                break;
        }
    }
    return !bIsStandard;
}
