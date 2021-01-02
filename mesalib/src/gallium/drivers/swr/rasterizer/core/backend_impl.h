/****************************************************************************
 * Copyright (C) 2014-2018 Intel Corporation.   All Rights Reserved.
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
 * @file backend.h
 *
 * @brief Backend handles rasterization, pixel shading and output merger
 *        operations.
 *
 ******************************************************************************/
#pragma once

#include "tilemgr.h"
#include "state.h"
#include "context.h"


void InitBackendSingleFuncTable(PFN_BACKEND_FUNC (&table)[SWR_INPUT_COVERAGE_COUNT][2][2]);
void InitBackendSampleFuncTable(
    PFN_BACKEND_FUNC (&table)[SWR_MULTISAMPLE_TYPE_COUNT][SWR_INPUT_COVERAGE_COUNT][2][2]);

static INLINE void CalcSampleBarycentrics(const BarycentricCoeffs& coeffs,
                                          SWR_PS_CONTEXT&          psContext);


enum SWR_BACKEND_FUNCS
{
    SWR_BACKEND_SINGLE_SAMPLE,
    SWR_BACKEND_MSAA_PIXEL_RATE,
    SWR_BACKEND_MSAA_SAMPLE_RATE,
    SWR_BACKEND_FUNCS_MAX,
};

#if KNOB_SIMD_WIDTH == 8
static const __m256 vCenterOffsetsX = __m256{0.5, 1.5, 0.5, 1.5, 2.5, 3.5, 2.5, 3.5};
static const __m256 vCenterOffsetsY = __m256{0.5, 0.5, 1.5, 1.5, 0.5, 0.5, 1.5, 1.5};
static const __m256 vULOffsetsX     = __m256{0.0, 1.0, 0.0, 1.0, 2.0, 3.0, 2.0, 3.0};
static const __m256 vULOffsetsY     = __m256{0.0, 0.0, 1.0, 1.0, 0.0, 0.0, 1.0, 1.0};
#define MASK 0xff
#endif

static INLINE simdmask ComputeUserClipMask(uint8_t           clipMask,
                                           float*            pUserClipBuffer,
                                           simdscalar const& vI,
                                           simdscalar const& vJ)
{
    simdscalar vClipMask       = _simd_setzero_ps();
    uint32_t   numClipDistance = _mm_popcnt_u32(clipMask);

    for (uint32_t i = 0; i < numClipDistance; ++i)
    {
        // pull triangle clip distance values from clip buffer
        simdscalar vA = _simd_broadcast_ss(pUserClipBuffer++);
        simdscalar vB = _simd_broadcast_ss(pUserClipBuffer++);
        simdscalar vC = _simd_broadcast_ss(pUserClipBuffer++);

        // interpolate
        simdscalar vInterp = vplaneps(vA, vB, vC, vI, vJ);

        // clip if interpolated clip distance is < 0 || NAN
        simdscalar vCull = _simd_cmp_ps(_simd_setzero_ps(), vInterp, _CMP_NLE_UQ);

        vClipMask = _simd_or_ps(vClipMask, vCull);
    }

    return _simd_movemask_ps(vClipMask);
}

INLINE static uint32_t RasterTileColorOffset(uint32_t sampleNum)
{
    static const uint32_t RasterTileColorOffsets[16]{
        0,
        (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_COLOR_HOT_TILE_FORMAT>::bpp / 8),
        (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_COLOR_HOT_TILE_FORMAT>::bpp / 8) * 2,
        (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_COLOR_HOT_TILE_FORMAT>::bpp / 8) * 3,
        (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_COLOR_HOT_TILE_FORMAT>::bpp / 8) * 4,
        (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_COLOR_HOT_TILE_FORMAT>::bpp / 8) * 5,
        (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_COLOR_HOT_TILE_FORMAT>::bpp / 8) * 6,
        (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_COLOR_HOT_TILE_FORMAT>::bpp / 8) * 7,
        (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_COLOR_HOT_TILE_FORMAT>::bpp / 8) * 8,
        (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_COLOR_HOT_TILE_FORMAT>::bpp / 8) * 9,
        (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_COLOR_HOT_TILE_FORMAT>::bpp / 8) *
            10,
        (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_COLOR_HOT_TILE_FORMAT>::bpp / 8) *
            11,
        (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_COLOR_HOT_TILE_FORMAT>::bpp / 8) *
            12,
        (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_COLOR_HOT_TILE_FORMAT>::bpp / 8) *
            13,
        (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_COLOR_HOT_TILE_FORMAT>::bpp / 8) *
            14,
        (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_COLOR_HOT_TILE_FORMAT>::bpp / 8) *
            15,
    };
    assert(sampleNum < 16);
    return RasterTileColorOffsets[sampleNum];
}

INLINE static uint32_t RasterTileDepthOffset(uint32_t sampleNum)
{
    static const uint32_t RasterTileDepthOffsets[16]{
        0,
        (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_DEPTH_HOT_TILE_FORMAT>::bpp / 8),
        (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_DEPTH_HOT_TILE_FORMAT>::bpp / 8) * 2,
        (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_DEPTH_HOT_TILE_FORMAT>::bpp / 8) * 3,
        (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_DEPTH_HOT_TILE_FORMAT>::bpp / 8) * 4,
        (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_DEPTH_HOT_TILE_FORMAT>::bpp / 8) * 5,
        (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_DEPTH_HOT_TILE_FORMAT>::bpp / 8) * 6,
        (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_DEPTH_HOT_TILE_FORMAT>::bpp / 8) * 7,
        (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_DEPTH_HOT_TILE_FORMAT>::bpp / 8) * 8,
        (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_DEPTH_HOT_TILE_FORMAT>::bpp / 8) * 9,
        (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_DEPTH_HOT_TILE_FORMAT>::bpp / 8) *
            10,
        (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_DEPTH_HOT_TILE_FORMAT>::bpp / 8) *
            11,
        (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_DEPTH_HOT_TILE_FORMAT>::bpp / 8) *
            12,
        (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_DEPTH_HOT_TILE_FORMAT>::bpp / 8) *
            13,
        (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_DEPTH_HOT_TILE_FORMAT>::bpp / 8) *
            14,
        (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_DEPTH_HOT_TILE_FORMAT>::bpp / 8) *
            15,
    };
    assert(sampleNum < 16);
    return RasterTileDepthOffsets[sampleNum];
}

INLINE static uint32_t RasterTileStencilOffset(uint32_t sampleNum)
{
    static const uint32_t RasterTileStencilOffsets[16]{
        0,
        (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_STENCIL_HOT_TILE_FORMAT>::bpp / 8),
        (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_STENCIL_HOT_TILE_FORMAT>::bpp / 8) *
            2,
        (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_STENCIL_HOT_TILE_FORMAT>::bpp / 8) *
            3,
        (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_STENCIL_HOT_TILE_FORMAT>::bpp / 8) *
            4,
        (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_STENCIL_HOT_TILE_FORMAT>::bpp / 8) *
            5,
        (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_STENCIL_HOT_TILE_FORMAT>::bpp / 8) *
            6,
        (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_STENCIL_HOT_TILE_FORMAT>::bpp / 8) *
            7,
        (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_STENCIL_HOT_TILE_FORMAT>::bpp / 8) *
            8,
        (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_STENCIL_HOT_TILE_FORMAT>::bpp / 8) *
            9,
        (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_STENCIL_HOT_TILE_FORMAT>::bpp / 8) *
            10,
        (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_STENCIL_HOT_TILE_FORMAT>::bpp / 8) *
            11,
        (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_STENCIL_HOT_TILE_FORMAT>::bpp / 8) *
            12,
        (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_STENCIL_HOT_TILE_FORMAT>::bpp / 8) *
            13,
        (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_STENCIL_HOT_TILE_FORMAT>::bpp / 8) *
            14,
        (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<KNOB_STENCIL_HOT_TILE_FORMAT>::bpp / 8) *
            15,
    };
    assert(sampleNum < 16);
    return RasterTileStencilOffsets[sampleNum];
}

template <typename T, uint32_t InputCoverage>
struct generateInputCoverage
{
    INLINE generateInputCoverage(const uint64_t* const coverageMask,
                                 uint32_t (&inputMask)[KNOB_SIMD_WIDTH],
                                 const uint32_t sampleMask)
    {
        // will need to update for avx512
        assert(KNOB_SIMD_WIDTH == 8);

        simdscalari mask[2];
        simdscalari sampleCoverage[2];

        if (T::bIsCenterPattern)
        {
            // center coverage is the same for all samples; just broadcast to the sample slots
            uint32_t centerCoverage = ((uint32_t)(*coverageMask) & MASK);
            if (T::MultisampleT::numSamples == 1)
            {
                sampleCoverage[0] = _simd_set_epi32(0, 0, 0, 0, 0, 0, 0, centerCoverage);
            }
            else if (T::MultisampleT::numSamples == 2)
            {
                sampleCoverage[0] =
                    _simd_set_epi32(0, 0, 0, 0, 0, 0, centerCoverage, centerCoverage);
            }
            else if (T::MultisampleT::numSamples == 4)
            {
                sampleCoverage[0] = _simd_set_epi32(
                    0, 0, 0, 0, centerCoverage, centerCoverage, centerCoverage, centerCoverage);
            }
            else if (T::MultisampleT::numSamples == 8)
            {
                sampleCoverage[0] = _simd_set1_epi32(centerCoverage);
            }
            else if (T::MultisampleT::numSamples == 16)
            {
                sampleCoverage[0] = _simd_set1_epi32(centerCoverage);
                sampleCoverage[1] = _simd_set1_epi32(centerCoverage);
            }
        }
        else
        {
            simdscalari src    = _simd_set1_epi32(0);
            simdscalari index0 = _simd_set_epi32(7, 6, 5, 4, 3, 2, 1, 0), index1;

            if (T::MultisampleT::numSamples == 1)
            {
                mask[0] = _simd_set_epi32(0, 0, 0, 0, 0, 0, 0, -1);
            }
            else if (T::MultisampleT::numSamples == 2)
            {
                mask[0] = _simd_set_epi32(0, 0, 0, 0, 0, 0, -1, -1);
            }
            else if (T::MultisampleT::numSamples == 4)
            {
                mask[0] = _simd_set_epi32(0, 0, 0, 0, -1, -1, -1, -1);
            }
            else if (T::MultisampleT::numSamples == 8)
            {
                mask[0] = _simd_set1_epi32(-1);
            }
            else if (T::MultisampleT::numSamples == 16)
            {
                mask[0] = _simd_set1_epi32(-1);
                mask[1] = _simd_set1_epi32(-1);
                index1  = _simd_set_epi32(15, 14, 13, 12, 11, 10, 9, 8);
            }

            // gather coverage for samples 0-7
            sampleCoverage[0] =
                _mm256_castps_si256(_simd_mask_i32gather_ps(_mm256_castsi256_ps(src),
                                                            (const float*)coverageMask,
                                                            index0,
                                                            _mm256_castsi256_ps(mask[0]),
                                                            8));
            if (T::MultisampleT::numSamples > 8)
            {
                // gather coverage for samples 8-15
                sampleCoverage[1] =
                    _mm256_castps_si256(_simd_mask_i32gather_ps(_mm256_castsi256_ps(src),
                                                                (const float*)coverageMask,
                                                                index1,
                                                                _mm256_castsi256_ps(mask[1]),
                                                                8));
            }
        }

        mask[0] = _mm256_set_epi8(-1,
                                  -1,
                                  -1,
                                  -1,
                                  -1,
                                  -1,
                                  -1,
                                  -1,
                                  -1,
                                  -1,
                                  -1,
                                  -1,
                                  0xC,
                                  0x8,
                                  0x4,
                                  0x0,
                                  -1,
                                  -1,
                                  -1,
                                  -1,
                                  -1,
                                  -1,
                                  -1,
                                  -1,
                                  -1,
                                  -1,
                                  -1,
                                  -1,
                                  0xC,
                                  0x8,
                                  0x4,
                                  0x0);
        // pull out the 8bit 4x2 coverage for samples 0-7 into the lower 32 bits of each 128bit lane
        simdscalari packedCoverage0 = _simd_shuffle_epi8(sampleCoverage[0], mask[0]);

        simdscalari packedCoverage1;
        if (T::MultisampleT::numSamples > 8)
        {
            // pull out the 8bit 4x2 coverage for samples 8-15 into the lower 32 bits of each 128bit
            // lane
            packedCoverage1 = _simd_shuffle_epi8(sampleCoverage[1], mask[0]);
        }

#if (KNOB_ARCH == KNOB_ARCH_AVX)
        // pack lower 32 bits of each 128 bit lane into lower 64 bits of single 128 bit lane
        simdscalari hiToLow = _mm256_permute2f128_si256(packedCoverage0, packedCoverage0, 0x83);
        simdscalar  shufRes = _mm256_shuffle_ps(
            _mm256_castsi256_ps(hiToLow), _mm256_castsi256_ps(hiToLow), _MM_SHUFFLE(1, 1, 0, 1));
        packedCoverage0 = _mm256_castps_si256(
            _mm256_blend_ps(_mm256_castsi256_ps(packedCoverage0), shufRes, 0xFE));

        simdscalari packedSampleCoverage;
        if (T::MultisampleT::numSamples > 8)
        {
            // pack lower 32 bits of each 128 bit lane into upper 64 bits of single 128 bit lane
            hiToLow         = _mm256_permute2f128_si256(packedCoverage1, packedCoverage1, 0x83);
            shufRes         = _mm256_shuffle_ps(_mm256_castsi256_ps(hiToLow),
                                        _mm256_castsi256_ps(hiToLow),
                                        _MM_SHUFFLE(1, 1, 0, 1));
            shufRes         = _mm256_blend_ps(_mm256_castsi256_ps(packedCoverage1), shufRes, 0xFE);
            packedCoverage1 = _mm256_castps_si256(_mm256_castpd_ps(
                _mm256_shuffle_pd(_mm256_castps_pd(shufRes), _mm256_castps_pd(shufRes), 0x01)));
            packedSampleCoverage = _mm256_castps_si256(_mm256_blend_ps(
                _mm256_castsi256_ps(packedCoverage0), _mm256_castsi256_ps(packedCoverage1), 0xFC));
        }
        else
        {
            packedSampleCoverage = packedCoverage0;
        }
#else
        simdscalari permMask = _simd_set_epi32(0x7, 0x7, 0x7, 0x7, 0x7, 0x7, 0x4, 0x0);
        // pack lower 32 bits of each 128 bit lane into lower 64 bits of single 128 bit lane
        packedCoverage0 = _mm256_permutevar8x32_epi32(packedCoverage0, permMask);

        simdscalari packedSampleCoverage;
        if (T::MultisampleT::numSamples > 8)
        {
            permMask = _simd_set_epi32(0x7, 0x7, 0x7, 0x7, 0x4, 0x0, 0x7, 0x7);
            // pack lower 32 bits of each 128 bit lane into upper 64 bits of single 128 bit lane
            packedCoverage1 = _mm256_permutevar8x32_epi32(packedCoverage1, permMask);

            // blend coverage masks for samples 0-7 and samples 8-15 into single 128 bit lane
            packedSampleCoverage = _mm256_blend_epi32(packedCoverage0, packedCoverage1, 0x0C);
        }
        else
        {
            packedSampleCoverage = packedCoverage0;
        }
#endif

        for (int32_t i = KNOB_SIMD_WIDTH - 1; i >= 0; i--)
        {
            // convert packed sample coverage masks into single coverage masks for all samples for
            // each pixel in the 4x2
            inputMask[i] = _simd_movemask_epi8(packedSampleCoverage);

            if (!T::bForcedSampleCount)
            {
                // input coverage has to be anded with sample mask if MSAA isn't forced on
                inputMask[i] &= sampleMask;
            }

            // shift to the next pixel in the 4x2
            packedSampleCoverage = _simd_slli_epi32(packedSampleCoverage, 1);
        }
    }

    INLINE generateInputCoverage(const uint64_t* const coverageMask,
                                 simdscalar&           inputCoverage,
                                 const uint32_t        sampleMask)
    {
        uint32_t inputMask[KNOB_SIMD_WIDTH];
        generateInputCoverage<T, T::InputCoverage>(coverageMask, inputMask, sampleMask);
        inputCoverage = _simd_castsi_ps(_simd_set_epi32(inputMask[7],
                                                        inputMask[6],
                                                        inputMask[5],
                                                        inputMask[4],
                                                        inputMask[3],
                                                        inputMask[2],
                                                        inputMask[1],
                                                        inputMask[0]));
    }
};

template <typename T>
struct generateInputCoverage<T, SWR_INPUT_COVERAGE_INNER_CONSERVATIVE>
{
    INLINE generateInputCoverage(const uint64_t* const coverageMask,
                                 simdscalar&           inputCoverage,
                                 const uint32_t        sampleMask)
    {
        // will need to update for avx512
        assert(KNOB_SIMD_WIDTH == 8);
        simdscalari       vec = _simd_set1_epi32(coverageMask[0]);
        const simdscalari bit = _simd_set_epi32(0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01);
        vec                   = _simd_and_si(vec, bit);
        vec                   = _simd_cmplt_epi32(_simd_setzero_si(), vec);
        vec                   = _simd_blendv_epi32(_simd_setzero_si(), _simd_set1_epi32(1), vec);
        inputCoverage         = _simd_castsi_ps(vec);
    }

    INLINE generateInputCoverage(const uint64_t* const coverageMask,
                                 uint32_t (&inputMask)[KNOB_SIMD_WIDTH],
                                 const uint32_t sampleMask)
    {
        uint32_t              simdCoverage     = (coverageMask[0] & MASK);
        static const uint32_t FullCoverageMask = (1 << T::MultisampleT::numSamples) - 1;
        for (int i = 0; i < KNOB_SIMD_WIDTH; i++)
        {
            // set all samples to covered if conservative coverage mask is set for that pixel
            inputMask[i] = (((1 << i) & simdCoverage) > 0) ? FullCoverageMask : 0;
        }
    }
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Centroid behaves exactly as follows :
// (1) If all samples in the primitive are covered, the attribute is evaluated at the pixel center
// (even if the sample pattern does not happen to
//     have a sample location there).
// (2) Else the attribute is evaluated at the first covered sample, in increasing order of sample
// index, where sample coverage is after ANDing the
//     coverage with the SampleMask Rasterizer State.
// (3) If no samples are covered, such as on helper pixels executed off the bounds of a primitive to
// fill out 2x2 pixel stamps, the attribute is
//     evaluated as follows : If the SampleMask Rasterizer state is a subset of the samples in the
//     pixel, then the first sample covered by the SampleMask Rasterizer State is the evaluation
//     point.Otherwise (full SampleMask), the pixel center is the evaluation point.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
template <typename T>
INLINE void CalcCentroidPos(SWR_PS_CONTEXT&            psContext,
                            const SWR_MULTISAMPLE_POS& samplePos,
                            const uint64_t* const      coverageMask,
                            const uint32_t             sampleMask,
                            simdscalar const&          vXSamplePosUL,
                            simdscalar const&          vYSamplePosUL)
{
    uint32_t inputMask[KNOB_SIMD_WIDTH];
    generateInputCoverage<T, T::InputCoverage>(coverageMask, inputMask, sampleMask);

    // Case (2) - partially covered pixel

    // scan for first covered sample per pixel in the 4x2 span
    unsigned long sampleNum[KNOB_SIMD_WIDTH];
    (inputMask[0] > 0) ? (_BitScanForward(&sampleNum[0], inputMask[0])) : (sampleNum[0] = 0);
    (inputMask[1] > 0) ? (_BitScanForward(&sampleNum[1], inputMask[1])) : (sampleNum[1] = 0);
    (inputMask[2] > 0) ? (_BitScanForward(&sampleNum[2], inputMask[2])) : (sampleNum[2] = 0);
    (inputMask[3] > 0) ? (_BitScanForward(&sampleNum[3], inputMask[3])) : (sampleNum[3] = 0);
    (inputMask[4] > 0) ? (_BitScanForward(&sampleNum[4], inputMask[4])) : (sampleNum[4] = 0);
    (inputMask[5] > 0) ? (_BitScanForward(&sampleNum[5], inputMask[5])) : (sampleNum[5] = 0);
    (inputMask[6] > 0) ? (_BitScanForward(&sampleNum[6], inputMask[6])) : (sampleNum[6] = 0);
    (inputMask[7] > 0) ? (_BitScanForward(&sampleNum[7], inputMask[7])) : (sampleNum[7] = 0);

    // look up and set the sample offsets from UL pixel corner for first covered sample
    simdscalar vXSample = _simd_set_ps(samplePos.X(sampleNum[7]),
                                       samplePos.X(sampleNum[6]),
                                       samplePos.X(sampleNum[5]),
                                       samplePos.X(sampleNum[4]),
                                       samplePos.X(sampleNum[3]),
                                       samplePos.X(sampleNum[2]),
                                       samplePos.X(sampleNum[1]),
                                       samplePos.X(sampleNum[0]));

    simdscalar vYSample = _simd_set_ps(samplePos.Y(sampleNum[7]),
                                       samplePos.Y(sampleNum[6]),
                                       samplePos.Y(sampleNum[5]),
                                       samplePos.Y(sampleNum[4]),
                                       samplePos.Y(sampleNum[3]),
                                       samplePos.Y(sampleNum[2]),
                                       samplePos.Y(sampleNum[1]),
                                       samplePos.Y(sampleNum[0]));
    // add sample offset to UL pixel corner
    vXSample = _simd_add_ps(vXSamplePosUL, vXSample);
    vYSample = _simd_add_ps(vYSamplePosUL, vYSample);

    // Case (1) and case (3b) - All samples covered or not covered with full SampleMask
    static const simdscalari vFullyCoveredMask = T::MultisampleT::FullSampleMask();
    simdscalari              vInputCoveragei   = _simd_set_epi32(inputMask[7],
                                                  inputMask[6],
                                                  inputMask[5],
                                                  inputMask[4],
                                                  inputMask[3],
                                                  inputMask[2],
                                                  inputMask[1],
                                                  inputMask[0]);
    simdscalari vAllSamplesCovered = _simd_cmpeq_epi32(vInputCoveragei, vFullyCoveredMask);

    static const simdscalari vZero = _simd_setzero_si();
    const simdscalari vSampleMask  = _simd_and_si(_simd_set1_epi32(sampleMask), vFullyCoveredMask);
    simdscalari       vNoSamplesCovered = _simd_cmpeq_epi32(vInputCoveragei, vZero);
    simdscalari       vIsFullSampleMask = _simd_cmpeq_epi32(vSampleMask, vFullyCoveredMask);
    simdscalari       vCase3b           = _simd_and_si(vNoSamplesCovered, vIsFullSampleMask);

    simdscalari vEvalAtCenter = _simd_or_si(vAllSamplesCovered, vCase3b);

    // set the centroid position based on results from above
    psContext.vX.centroid =
        _simd_blendv_ps(vXSample, psContext.vX.center, _simd_castsi_ps(vEvalAtCenter));
    psContext.vY.centroid =
        _simd_blendv_ps(vYSample, psContext.vY.center, _simd_castsi_ps(vEvalAtCenter));

    // Case (3a) No samples covered and partial sample mask
    simdscalari vSomeSampleMaskSamples = _simd_cmplt_epi32(vSampleMask, vFullyCoveredMask);
    // sample mask should never be all 0's for this case, but handle it anyways
    unsigned long firstCoveredSampleMaskSample = 0;
    (sampleMask > 0) ? (_BitScanForward(&firstCoveredSampleMaskSample, sampleMask))
                     : (firstCoveredSampleMaskSample = 0);

    simdscalari vCase3a = _simd_and_si(vNoSamplesCovered, vSomeSampleMaskSamples);

    vXSample = _simd_set1_ps(samplePos.X(firstCoveredSampleMaskSample));
    vYSample = _simd_set1_ps(samplePos.Y(firstCoveredSampleMaskSample));

    // blend in case 3a pixel locations
    psContext.vX.centroid =
        _simd_blendv_ps(psContext.vX.centroid, vXSample, _simd_castsi_ps(vCase3a));
    psContext.vY.centroid =
        _simd_blendv_ps(psContext.vY.centroid, vYSample, _simd_castsi_ps(vCase3a));
}

INLINE void CalcCentroidBarycentrics(const BarycentricCoeffs& coeffs,
                                     SWR_PS_CONTEXT&          psContext,
                                     const simdscalar&        vXSamplePosUL,
                                     const simdscalar&        vYSamplePosUL)
{
    // evaluate I,J
    psContext.vI.centroid =
        vplaneps(coeffs.vIa, coeffs.vIb, coeffs.vIc, psContext.vX.centroid, psContext.vY.centroid);
    psContext.vJ.centroid =
        vplaneps(coeffs.vJa, coeffs.vJb, coeffs.vJc, psContext.vX.centroid, psContext.vY.centroid);
    psContext.vI.centroid = _simd_mul_ps(psContext.vI.centroid, coeffs.vRecipDet);
    psContext.vJ.centroid = _simd_mul_ps(psContext.vJ.centroid, coeffs.vRecipDet);

    // interpolate 1/w
    psContext.vOneOverW.centroid = vplaneps(coeffs.vAOneOverW,
                                            coeffs.vBOneOverW,
                                            coeffs.vCOneOverW,
                                            psContext.vI.centroid,
                                            psContext.vJ.centroid);
}

INLINE simdmask CalcDepthBoundsAcceptMask(simdscalar const& z, float minz, float maxz)
{
    const simdscalar minzMask = _simd_cmpge_ps(z, _simd_set1_ps(minz));
    const simdscalar maxzMask = _simd_cmple_ps(z, _simd_set1_ps(maxz));

    return _simd_movemask_ps(_simd_and_ps(minzMask, maxzMask));
}

template <typename T>
INLINE uint32_t GetNumOMSamples(SWR_MULTISAMPLE_COUNT blendSampleCount)
{
    // RT has to be single sample if we're in forcedMSAA mode
    if (T::bForcedSampleCount && (T::MultisampleT::sampleCount > SWR_MULTISAMPLE_1X))
    {
        return 1;
    }
    // unless we're forced to single sample, in which case we run the OM at the sample count of the
    // RT
    else if (T::bForcedSampleCount && (T::MultisampleT::sampleCount == SWR_MULTISAMPLE_1X))
    {
        return GetNumSamples(blendSampleCount);
    }
    // else we're in normal MSAA mode and rasterizer and OM are running at the same sample count
    else
    {
        return T::MultisampleT::numSamples;
    }
}

inline void SetupBarycentricCoeffs(BarycentricCoeffs* coeffs, const SWR_TRIANGLE_DESC& work)
{
    // broadcast scalars

    coeffs->vIa = _simd_broadcast_ss(&work.I[0]);
    coeffs->vIb = _simd_broadcast_ss(&work.I[1]);
    coeffs->vIc = _simd_broadcast_ss(&work.I[2]);

    coeffs->vJa = _simd_broadcast_ss(&work.J[0]);
    coeffs->vJb = _simd_broadcast_ss(&work.J[1]);
    coeffs->vJc = _simd_broadcast_ss(&work.J[2]);

    coeffs->vZa = _simd_broadcast_ss(&work.Z[0]);
    coeffs->vZb = _simd_broadcast_ss(&work.Z[1]);
    coeffs->vZc = _simd_broadcast_ss(&work.Z[2]);

    coeffs->vRecipDet = _simd_broadcast_ss(&work.recipDet);

    coeffs->vAOneOverW = _simd_broadcast_ss(&work.OneOverW[0]);
    coeffs->vBOneOverW = _simd_broadcast_ss(&work.OneOverW[1]);
    coeffs->vCOneOverW = _simd_broadcast_ss(&work.OneOverW[2]);
}

inline void SetupRenderBuffers(uint8_t*             pColorBuffer[SWR_NUM_RENDERTARGETS],
                               uint8_t**            pDepthBuffer,
                               uint8_t**            pStencilBuffer,
                               uint32_t             colorHotTileMask,
                               RenderOutputBuffers& renderBuffers)
{
    unsigned long index;
    while (_BitScanForward(&index, colorHotTileMask))
    {
        assert(index < SWR_NUM_RENDERTARGETS);
        colorHotTileMask &= ~(1 << index);
        pColorBuffer[index] = renderBuffers.pColor[index];
    }

    if (pDepthBuffer)
    {
        *pDepthBuffer = renderBuffers.pDepth;
    }

    if (pStencilBuffer)
    {
        *pStencilBuffer = renderBuffers.pStencil;
        ;
    }
}

INLINE void SetRenderHotTilesDirty(DRAW_CONTEXT* pDC, RenderOutputBuffers& renderBuffers)
{
    const API_STATE& state = GetApiState(pDC);

    unsigned long rtSlot                 = 0;
    uint32_t      colorHottileEnableMask = state.colorHottileEnable;
    while (_BitScanForward(&rtSlot, colorHottileEnableMask))
    {
        colorHottileEnableMask &= ~(1 << rtSlot);
        renderBuffers.pColorHotTile[rtSlot]->state = HOTTILE_DIRTY;
    }
}

template <typename T>
void SetupPixelShaderContext(SWR_PS_CONTEXT*            psContext,
                             const SWR_MULTISAMPLE_POS& samplePos,
                             SWR_TRIANGLE_DESC&         work)
{
    psContext->pAttribs               = work.pAttribs;
    psContext->pPerspAttribs          = work.pPerspAttribs;
    psContext->frontFace              = work.triFlags.frontFacing;
    psContext->renderTargetArrayIndex = work.triFlags.renderTargetArrayIndex;
    psContext->viewportIndex          = work.triFlags.viewportIndex;

    // save Ia/Ib/Ic and Ja/Jb/Jc if we need to reevaluate i/j/k in the shader because of pull
    // attribs
    psContext->I = work.I;
    psContext->J = work.J;

    psContext->recipDet = work.recipDet;
    psContext->pRecipW  = work.pRecipW;
    psContext->pSamplePosX =
        samplePos.X(); // reinterpret_cast<const float *>(&T::MultisampleT::samplePosX);
    psContext->pSamplePosY =
        samplePos.Y(); // reinterpret_cast<const float *>(&T::MultisampleT::samplePosY);
    psContext->rasterizerSampleCount = T::MultisampleT::numSamples;
    psContext->sampleIndex           = 0;
}

template <typename T, bool IsSingleSample>
void CalcCentroid(SWR_PS_CONTEXT*            psContext,
                  const SWR_MULTISAMPLE_POS& samplePos,
                  const BarycentricCoeffs&   coeffs,
                  const uint64_t* const      coverageMask,
                  uint32_t                   sampleMask)
{
    if (IsSingleSample) // if (T::MultisampleT::numSamples == 1) // doesn't cut it, the centroid
                        // positions are still different
    {
        // for 1x case, centroid is pixel center
        psContext->vX.centroid        = psContext->vX.center;
        psContext->vY.centroid        = psContext->vY.center;
        psContext->vI.centroid        = psContext->vI.center;
        psContext->vJ.centroid        = psContext->vJ.center;
        psContext->vOneOverW.centroid = psContext->vOneOverW.center;
    }
    else
    {
        if (T::bCentroidPos)
        {
            ///@ todo: don't need to genererate input coverage 2x if input coverage and centroid
            if (T::bIsCenterPattern)
            {
                psContext->vX.centroid = _simd_add_ps(psContext->vX.UL, _simd_set1_ps(0.5f));
                psContext->vY.centroid = _simd_add_ps(psContext->vY.UL, _simd_set1_ps(0.5f));
            }
            else
            {
                // add param: const uint32_t inputMask[KNOB_SIMD_WIDTH] to eliminate 'generate
                // coverage 2X'..
                CalcCentroidPos<T>(*psContext,
                                   samplePos,
                                   coverageMask,
                                   sampleMask,
                                   psContext->vX.UL,
                                   psContext->vY.UL);
            }

            CalcCentroidBarycentrics(coeffs, *psContext, psContext->vX.UL, psContext->vY.UL);
        }
        else
        {
            psContext->vX.centroid = psContext->vX.sample;
            psContext->vY.centroid = psContext->vY.sample;
        }
    }
}

template <typename T>
struct PixelRateZTestLoop
{
    PixelRateZTestLoop(DRAW_CONTEXT*            DC,
                       uint32_t                 _workerId,
                       const SWR_TRIANGLE_DESC& Work,
                       const BarycentricCoeffs& Coeffs,
                       const API_STATE&         apiState,
                       uint8_t*&                depthBuffer,
                       uint8_t*&                stencilBuffer,
                       const uint8_t            ClipDistanceMask) :
        pDC(DC),
        workerId(_workerId), work(Work), coeffs(Coeffs), state(apiState), psState(apiState.psState),
        samplePos(state.rastState.samplePositions), clipDistanceMask(ClipDistanceMask),
        pDepthBuffer(depthBuffer), pStencilBuffer(stencilBuffer){};

    INLINE
    uint32_t operator()(simdscalar&        activeLanes,
                        SWR_PS_CONTEXT&    psContext,
                        const CORE_BUCKETS BEDepthBucket,
                        uint32_t           currentSimdIn8x8 = 0)
    {

        uint32_t   statCount            = 0;
        simdscalar anyDepthSamplePassed = _simd_setzero_ps();
        for (uint32_t sample = 0; sample < T::MultisampleT::numCoverageSamples; sample++)
        {
            const uint8_t* pCoverageMask = (uint8_t*)&work.coverageMask[sample];
            vCoverageMask[sample] =
                _simd_and_ps(activeLanes, _simd_vmask_ps(pCoverageMask[currentSimdIn8x8] & MASK));

            if (!_simd_movemask_ps(vCoverageMask[sample]))
            {
                vCoverageMask[sample] = depthPassMask[sample] = stencilPassMask[sample] =
                    _simd_setzero_ps();
                continue;
            }

            // offset depth/stencil buffers current sample
            uint8_t* pDepthSample   = pDepthBuffer + RasterTileDepthOffset(sample);
            uint8_t* pStencilSample = pStencilBuffer + RasterTileStencilOffset(sample);

            if (state.depthHottileEnable && state.depthBoundsState.depthBoundsTestEnable)
            {
                static_assert(KNOB_DEPTH_HOT_TILE_FORMAT == R32_FLOAT,
                              "Unsupported depth hot tile format");

                const simdscalar z = _simd_load_ps(reinterpret_cast<const float*>(pDepthSample));

                const float minz = state.depthBoundsState.depthBoundsTestMinValue;
                const float maxz = state.depthBoundsState.depthBoundsTestMaxValue;

                vCoverageMask[sample] =
                    _simd_and_ps(vCoverageMask[sample],
                                 _simd_vmask_ps(CalcDepthBoundsAcceptMask(z, minz, maxz)));
            }

            RDTSC_BEGIN(psContext.pBucketManager, BEBarycentric, pDC->drawId);

            // calculate per sample positions
            psContext.vX.sample = _simd_add_ps(psContext.vX.UL, samplePos.vX(sample));
            psContext.vY.sample = _simd_add_ps(psContext.vY.UL, samplePos.vY(sample));

            // calc I & J per sample
            CalcSampleBarycentrics(coeffs, psContext);

            if (psState.writesODepth)
            {
                {
                    // broadcast and test oDepth(psContext.vZ) written from the PS for each sample
                    vZ[sample] = psContext.vZ;
                }
            }
            else
            {
                vZ[sample] = vplaneps(
                    coeffs.vZa, coeffs.vZb, coeffs.vZc, psContext.vI.sample, psContext.vJ.sample);
                vZ[sample] = state.pfnQuantizeDepth(vZ[sample]);
            }

            RDTSC_END(psContext.pBucketManager, BEBarycentric, 0);

            ///@todo: perspective correct vs non-perspective correct clipping?
            // if clip distances are enabled, we need to interpolate for each sample
            if (clipDistanceMask)
            {
                uint8_t clipMask = ComputeUserClipMask(clipDistanceMask,
                                                       work.pUserClipBuffer,
                                                       psContext.vI.sample,
                                                       psContext.vJ.sample);

                vCoverageMask[sample] =
                    _simd_and_ps(vCoverageMask[sample], _simd_vmask_ps(~clipMask));
            }

            // ZTest for this sample
            ///@todo Need to uncomment out this bucket.
            // RDTSC_BEGIN(psContext.pBucketManager, BEDepthBucket, pDC->drawId);
            depthPassMask[sample]   = vCoverageMask[sample];
            stencilPassMask[sample] = vCoverageMask[sample];
            depthPassMask[sample]   = DepthStencilTest(&state,
                                                     work.triFlags.frontFacing,
                                                     work.triFlags.viewportIndex,
                                                     vZ[sample],
                                                     pDepthSample,
                                                     vCoverageMask[sample],
                                                     pStencilSample,
                                                     &stencilPassMask[sample]);
            // RDTSC_END(psContext.pBucketManager, BEDepthBucket, 0);

            // early-exit if no pixels passed depth or earlyZ is forced on
            if (psState.forceEarlyZ || !_simd_movemask_ps(depthPassMask[sample]))
            {
                DepthStencilWrite(&state.vp[work.triFlags.viewportIndex],
                                  &state.depthStencilState,
                                  work.triFlags.frontFacing,
                                  vZ[sample],
                                  pDepthSample,
                                  depthPassMask[sample],
                                  vCoverageMask[sample],
                                  pStencilSample,
                                  stencilPassMask[sample]);

                if (!_simd_movemask_ps(depthPassMask[sample]))
                {
                    continue;
                }
            }
            anyDepthSamplePassed = _simd_or_ps(anyDepthSamplePassed, depthPassMask[sample]);
            uint32_t statMask    = _simd_movemask_ps(depthPassMask[sample]);
            statCount += _mm_popcnt_u32(statMask);
        }

        activeLanes = _simd_and_ps(anyDepthSamplePassed, activeLanes);
        // return number of samples that passed depth and coverage
        return statCount;
    }

    // saved depth/stencil/coverage masks and interpolated Z used in OM and DepthWrite
    simdscalar vZ[T::MultisampleT::numCoverageSamples];
    simdscalar vCoverageMask[T::MultisampleT::numCoverageSamples];
    simdscalar depthPassMask[T::MultisampleT::numCoverageSamples];
    simdscalar stencilPassMask[T::MultisampleT::numCoverageSamples];

private:
    // functor inputs
    DRAW_CONTEXT* pDC;
    uint32_t      workerId;

    const SWR_TRIANGLE_DESC&   work;
    const BarycentricCoeffs&   coeffs;
    const API_STATE&           state;
    const SWR_PS_STATE&        psState;
    const SWR_MULTISAMPLE_POS& samplePos;
    const uint8_t              clipDistanceMask;
    uint8_t*&                  pDepthBuffer;
    uint8_t*&                  pStencilBuffer;
};

INLINE void CalcPixelBarycentrics(const BarycentricCoeffs& coeffs, SWR_PS_CONTEXT& psContext)
{
    // evaluate I,J
    psContext.vI.center =
        vplaneps(coeffs.vIa, coeffs.vIb, coeffs.vIc, psContext.vX.center, psContext.vY.center);
    psContext.vJ.center =
        vplaneps(coeffs.vJa, coeffs.vJb, coeffs.vJc, psContext.vX.center, psContext.vY.center);
    psContext.vI.center = _simd_mul_ps(psContext.vI.center, coeffs.vRecipDet);
    psContext.vJ.center = _simd_mul_ps(psContext.vJ.center, coeffs.vRecipDet);

    // interpolate 1/w
    psContext.vOneOverW.center = vplaneps(coeffs.vAOneOverW,
                                          coeffs.vBOneOverW,
                                          coeffs.vCOneOverW,
                                          psContext.vI.center,
                                          psContext.vJ.center);
}

static INLINE void CalcSampleBarycentrics(const BarycentricCoeffs& coeffs,
                                          SWR_PS_CONTEXT&          psContext)
{
    // evaluate I,J
    psContext.vI.sample =
        vplaneps(coeffs.vIa, coeffs.vIb, coeffs.vIc, psContext.vX.sample, psContext.vY.sample);
    psContext.vJ.sample =
        vplaneps(coeffs.vJa, coeffs.vJb, coeffs.vJc, psContext.vX.sample, psContext.vY.sample);
    psContext.vI.sample = _simd_mul_ps(psContext.vI.sample, coeffs.vRecipDet);
    psContext.vJ.sample = _simd_mul_ps(psContext.vJ.sample, coeffs.vRecipDet);

    // interpolate 1/w
    psContext.vOneOverW.sample = vplaneps(coeffs.vAOneOverW,
                                          coeffs.vBOneOverW,
                                          coeffs.vCOneOverW,
                                          psContext.vI.sample,
                                          psContext.vJ.sample);
}

// Merge Output to 8x2 SIMD16 Tile Format
INLINE void OutputMerger8x2(DRAW_CONTEXT*   pDC,
                            SWR_PS_CONTEXT& psContext,
                            uint8_t* (&pColorBase)[SWR_NUM_RENDERTARGETS],
                            uint32_t               sample,
                            const SWR_BLEND_STATE* pBlendState,
                            const PFN_BLEND_JIT_FUNC (&pfnBlendFunc)[SWR_NUM_RENDERTARGETS],
                            simdscalar&       coverageMask,
                            simdscalar const& depthPassMask,
                            uint32_t          renderTargetMask,
                            bool              useAlternateOffset,
                            uint32_t          workerId)
{
    // type safety guaranteed from template instantiation in BEChooser<>::GetFunc
    uint32_t rasterTileColorOffset = RasterTileColorOffset(sample);

    if (useAlternateOffset)
    {
        rasterTileColorOffset += sizeof(simdscalar);
    }

    simdvector blendSrc;
    simdvector blendOut;

    unsigned long rt;
    while (_BitScanForward(&rt, renderTargetMask))
    {
        renderTargetMask &= ~(1 << rt);

        const SWR_RENDER_TARGET_BLEND_STATE* pRTBlend = &pBlendState->renderTarget[rt];

        simdscalar* pColorSample;
        bool        hotTileEnable = !pRTBlend->writeDisableAlpha || !pRTBlend->writeDisableRed ||
                             !pRTBlend->writeDisableGreen || !pRTBlend->writeDisableBlue;
        if (hotTileEnable)
        {
            pColorSample = reinterpret_cast<simdscalar*>(pColorBase[rt] + rasterTileColorOffset);
            blendSrc[0]  = pColorSample[0];
            blendSrc[1]  = pColorSample[2];
            blendSrc[2]  = pColorSample[4];
            blendSrc[3]  = pColorSample[6];
        }
        else
        {
            pColorSample = nullptr;
        }

        SWR_BLEND_CONTEXT blendContext = {0};
        {
            // pfnBlendFunc may not update all channels.  Initialize with PS output.
            /// TODO: move this into the blend JIT.
            blendOut = psContext.shaded[rt];

            blendContext.pBlendState = pBlendState;
            blendContext.src         = &psContext.shaded[rt];
            blendContext.src1        = &psContext.shaded[1];
            blendContext.src0alpha   = reinterpret_cast<simdvector*>(&psContext.shaded[0].w);
            blendContext.sampleNum   = sample;
            blendContext.pDst        = &blendSrc;
            blendContext.result      = &blendOut;
            blendContext.oMask       = &psContext.oMask;
            blendContext.pMask       = reinterpret_cast<simdscalari*>(&coverageMask);

            // Blend outputs and update coverage mask for alpha test
            if (pfnBlendFunc[rt] != nullptr)
            {
                pfnBlendFunc[rt](&blendContext);
            }
        }

        // Track alpha events
        AR_EVENT(
            AlphaInfoEvent(pDC->drawId, blendContext.isAlphaTested, blendContext.isAlphaBlended));

        // final write mask
        simdscalari outputMask = _simd_castps_si(_simd_and_ps(coverageMask, depthPassMask));

        ///@todo can only use maskstore fast path if bpc is 32. Assuming hot tile is RGBA32_FLOAT.
        static_assert(KNOB_COLOR_HOT_TILE_FORMAT == R32G32B32A32_FLOAT,
                      "Unsupported hot tile format");

        // store with color mask
        if (!pRTBlend->writeDisableRed)
        {
            _simd_maskstore_ps(reinterpret_cast<float*>(&pColorSample[0]), outputMask, blendOut.x);
        }
        if (!pRTBlend->writeDisableGreen)
        {
            _simd_maskstore_ps(reinterpret_cast<float*>(&pColorSample[2]), outputMask, blendOut.y);
        }
        if (!pRTBlend->writeDisableBlue)
        {
            _simd_maskstore_ps(reinterpret_cast<float*>(&pColorSample[4]), outputMask, blendOut.z);
        }
        if (!pRTBlend->writeDisableAlpha)
        {
            _simd_maskstore_ps(reinterpret_cast<float*>(&pColorSample[6]), outputMask, blendOut.w);
        }
    }
}

template <typename T>
void BackendPixelRate(DRAW_CONTEXT*        pDC,
                      uint32_t             workerId,
                      uint32_t             x,
                      uint32_t             y,
                      SWR_TRIANGLE_DESC&   work,
                      RenderOutputBuffers& renderBuffers)
{
    ///@todo: Need to move locals off stack to prevent __chkstk's from being generated for the
    /// backend


    RDTSC_BEGIN(pDC->pContext->pBucketMgr, BEPixelRateBackend, pDC->drawId);
    RDTSC_BEGIN(pDC->pContext->pBucketMgr, BESetup, pDC->drawId);

    const API_STATE& state = GetApiState(pDC);

    BarycentricCoeffs coeffs;
    SetupBarycentricCoeffs(&coeffs, work);

    SWR_CONTEXT* pContext    = pDC->pContext;
    void*        pWorkerData = pContext->threadPool.pThreadData[workerId].pWorkerPrivateData;

    SWR_PS_CONTEXT             psContext;
    const SWR_MULTISAMPLE_POS& samplePos = state.rastState.samplePositions;
    SetupPixelShaderContext<T>(&psContext, samplePos, work);

    uint8_t *pDepthBuffer, *pStencilBuffer;
    SetupRenderBuffers(psContext.pColorBuffer,
                       &pDepthBuffer,
                       &pStencilBuffer,
                       state.colorHottileEnable,
                       renderBuffers);

    bool isTileDirty = false;

    RDTSC_END(pDC->pContext->pBucketMgr, BESetup, 0);

    PixelRateZTestLoop<T> PixelRateZTest(pDC,
                                         workerId,
                                         work,
                                         coeffs,
                                         state,
                                         pDepthBuffer,
                                         pStencilBuffer,
                                         state.backendState.clipDistanceMask);

    psContext.vY.UL     = _simd_add_ps(vULOffsetsY, _simd_set1_ps(static_cast<float>(y)));
    psContext.vY.center = _simd_add_ps(vCenterOffsetsY, _simd_set1_ps(static_cast<float>(y)));

    const simdscalar dy = _simd_set1_ps(static_cast<float>(SIMD_TILE_Y_DIM));

    for (uint32_t yy = y; yy < y + KNOB_TILE_Y_DIM; yy += SIMD_TILE_Y_DIM)
    {
        psContext.vX.UL     = _simd_add_ps(vULOffsetsX, _simd_set1_ps(static_cast<float>(x)));
        psContext.vX.center = _simd_add_ps(vCenterOffsetsX, _simd_set1_ps(static_cast<float>(x)));

        const simdscalar dx = _simd_set1_ps(static_cast<float>(SIMD_TILE_X_DIM));

        for (uint32_t xx = x; xx < x + KNOB_TILE_X_DIM; xx += SIMD_TILE_X_DIM)
        {
            const bool useAlternateOffset = ((xx & SIMD_TILE_X_DIM) != 0);


            simdscalar activeLanes;
            if (!(work.anyCoveredSamples & MASK))
            {
                goto Endtile;
            };
            activeLanes = _simd_vmask_ps(work.anyCoveredSamples & MASK);

            if (T::InputCoverage != SWR_INPUT_COVERAGE_NONE)
            {
                const uint64_t* pCoverageMask =
                    (T::InputCoverage == SWR_INPUT_COVERAGE_INNER_CONSERVATIVE)
                        ? &work.innerCoverageMask
                        : &work.coverageMask[0];

                generateInputCoverage<T, T::InputCoverage>(
                    pCoverageMask, psContext.inputMask, state.blendState.sampleMask);
            }

            RDTSC_BEGIN(pDC->pContext->pBucketMgr, BEBarycentric, pDC->drawId);

            CalcPixelBarycentrics(coeffs, psContext);

            CalcCentroid<T, false>(
                &psContext, samplePos, coeffs, work.coverageMask, state.blendState.sampleMask);

            RDTSC_END(pDC->pContext->pBucketMgr, BEBarycentric, 0);

            if (T::bForcedSampleCount)
            {
                // candidate pixels (that passed coverage) will cause shader invocation if any bits
                // in the samplemask are set
                const simdscalar vSampleMask = _simd_castsi_ps(_simd_cmpgt_epi32(
                    _simd_set1_epi32(state.blendState.sampleMask), _simd_setzero_si()));
                activeLanes                  = _simd_and_ps(activeLanes, vSampleMask);
            }

            // Early-Z?
            if (T::bCanEarlyZ && !T::bForcedSampleCount)
            {
                uint32_t depthPassCount = PixelRateZTest(activeLanes, psContext, BEEarlyDepthTest);
                UPDATE_STAT_BE(DepthPassCount, depthPassCount);
                AR_EVENT(EarlyDepthInfoPixelRate(depthPassCount, _simd_movemask_ps(activeLanes)));
            }

            // if we have no covered samples that passed depth at this point, go to next tile
            if (!_simd_movemask_ps(activeLanes))
            {
                goto Endtile;
            };

            if (state.psState.usesSourceDepth)
            {
                RDTSC_BEGIN(pDC->pContext->pBucketMgr, BEBarycentric, pDC->drawId);
                // interpolate and quantize z
                psContext.vZ = vplaneps(
                    coeffs.vZa, coeffs.vZb, coeffs.vZc, psContext.vI.center, psContext.vJ.center);
                psContext.vZ = state.pfnQuantizeDepth(psContext.vZ);
                RDTSC_END(pDC->pContext->pBucketMgr, BEBarycentric, 0);
            }

            // pixels that are currently active
            psContext.activeMask = _simd_castps_si(activeLanes);
            psContext.oMask      = T::MultisampleT::FullSampleMask();

            // execute pixel shader
            RDTSC_BEGIN(pDC->pContext->pBucketMgr, BEPixelShader, pDC->drawId);
            state.psState.pfnPixelShader(GetPrivateState(pDC), pWorkerData, &psContext);
            RDTSC_END(pDC->pContext->pBucketMgr, BEPixelShader, 0);

            // update stats
            UPDATE_STAT_BE(PsInvocations, _mm_popcnt_u32(_simd_movemask_ps(activeLanes)));
            AR_EVENT(PSStats((HANDLE)&psContext.stats));

            // update active lanes to remove any discarded or oMask'd pixels
            activeLanes = _simd_castsi_ps(_simd_and_si(
                psContext.activeMask, _simd_cmpgt_epi32(psContext.oMask, _simd_setzero_si())));
            if (!_simd_movemask_ps(activeLanes))
            {
                goto Endtile;
            };

            isTileDirty = true;

            // late-Z
            if (!T::bCanEarlyZ && !T::bForcedSampleCount)
            {
                uint32_t depthPassCount = PixelRateZTest(activeLanes, psContext, BELateDepthTest);
                UPDATE_STAT_BE(DepthPassCount, depthPassCount);
                AR_EVENT(LateDepthInfoPixelRate(depthPassCount, _simd_movemask_ps(activeLanes)));
            }

            // if we have no covered samples that passed depth at this point, skip OM and go to next
            // tile
            if (!_simd_movemask_ps(activeLanes))
            {
                goto Endtile;
            };

            // output merger
            // loop over all samples, broadcasting the results of the PS to all passing pixels
            for (uint32_t sample = 0; sample < GetNumOMSamples<T>(state.blendState.sampleCount);
                 sample++)
            {
                RDTSC_BEGIN(pDC->pContext->pBucketMgr, BEOutputMerger, pDC->drawId);
                // center pattern does a single coverage/depth/stencil test, standard pattern tests
                // all samples
                uint32_t   coverageSampleNum = (T::bIsCenterPattern) ? 0 : sample;
                simdscalar coverageMask, depthMask;
                if (T::bForcedSampleCount)
                {
                    coverageMask = depthMask = activeLanes;
                }
                else
                {
                    coverageMask = PixelRateZTest.vCoverageMask[coverageSampleNum];
                    depthMask = PixelRateZTest.depthPassMask[coverageSampleNum];
                    if (!_simd_movemask_ps(depthMask))
                    {
                        // stencil should already have been written in early/lateZ tests
                        RDTSC_END(pDC->pContext->pBucketMgr, BEOutputMerger, 0);
                        continue;
                    }
                }

                // broadcast the results of the PS to all passing pixels

                OutputMerger8x2(pDC,
                                psContext,
                                psContext.pColorBuffer,
                                sample,
                                &state.blendState,
                                state.pfnBlendFunc,
                                coverageMask,
                                depthMask,
                                state.psState.renderTargetMask,
                                useAlternateOffset,
                                workerId);


                if (!state.psState.forceEarlyZ && !T::bForcedSampleCount)
                {
                    uint8_t* pDepthSample   = pDepthBuffer + RasterTileDepthOffset(sample);
                    uint8_t* pStencilSample = pStencilBuffer + RasterTileStencilOffset(sample);

                    DepthStencilWrite(&state.vp[work.triFlags.viewportIndex],
                                      &state.depthStencilState,
                                      work.triFlags.frontFacing,
                                      PixelRateZTest.vZ[coverageSampleNum],
                                      pDepthSample,
                                      depthMask,
                                      coverageMask,
                                      pStencilSample,
                                      PixelRateZTest.stencilPassMask[coverageSampleNum]);
                }
                RDTSC_END(pDC->pContext->pBucketMgr, BEOutputMerger, 0);
            }
        Endtile:
            RDTSC_BEGIN(pDC->pContext->pBucketMgr, BEEndTile, pDC->drawId);

            for (uint32_t sample = 0; sample < T::MultisampleT::numCoverageSamples; sample++)
            {
                work.coverageMask[sample] >>= (SIMD_TILE_Y_DIM * SIMD_TILE_X_DIM);
            }

            if (T::InputCoverage == SWR_INPUT_COVERAGE_INNER_CONSERVATIVE)
            {
                work.innerCoverageMask >>= (SIMD_TILE_Y_DIM * SIMD_TILE_X_DIM);
            }
            work.anyCoveredSamples >>= (SIMD_TILE_Y_DIM * SIMD_TILE_X_DIM);

            if (useAlternateOffset)
            {
                unsigned long rt;
                uint32_t rtMask = state.colorHottileEnable;
                while (_BitScanForward(&rt, rtMask))
                {
                    rtMask &= ~(1 << rt);
                    psContext.pColorBuffer[rt] +=
                        (2 * KNOB_SIMD_WIDTH * FormatTraits<KNOB_COLOR_HOT_TILE_FORMAT>::bpp) / 8;
                }
            }

            pDepthBuffer += (KNOB_SIMD_WIDTH * FormatTraits<KNOB_DEPTH_HOT_TILE_FORMAT>::bpp) / 8;
            pStencilBuffer +=
                (KNOB_SIMD_WIDTH * FormatTraits<KNOB_STENCIL_HOT_TILE_FORMAT>::bpp) / 8;

            RDTSC_END(pDC->pContext->pBucketMgr, BEEndTile, 0);

            psContext.vX.UL     = _simd_add_ps(psContext.vX.UL, dx);
            psContext.vX.center = _simd_add_ps(psContext.vX.center, dx);
        }

        psContext.vY.UL     = _simd_add_ps(psContext.vY.UL, dy);
        psContext.vY.center = _simd_add_ps(psContext.vY.center, dy);
    }

    if (isTileDirty)
    {
        SetRenderHotTilesDirty(pDC, renderBuffers);
    }

    RDTSC_END(pDC->pContext->pBucketMgr, BEPixelRateBackend, 0);
}

template <uint32_t sampleCountT = SWR_MULTISAMPLE_1X,
          uint32_t isCenter     = 0,
          uint32_t coverage     = 0,
          uint32_t centroid     = 0,
          uint32_t forced       = 0,
          uint32_t canEarlyZ    = 0
          >
struct SwrBackendTraits
{
    static const bool     bIsCenterPattern   = (isCenter == 1);
    static const uint32_t InputCoverage      = coverage;
    static const bool     bCentroidPos       = (centroid == 1);
    static const bool     bForcedSampleCount = (forced == 1);
    static const bool     bCanEarlyZ         = (canEarlyZ == 1);
    typedef MultisampleTraits<(SWR_MULTISAMPLE_COUNT)sampleCountT, bIsCenterPattern> MultisampleT;
};
