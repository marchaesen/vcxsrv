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
 * @file frontend.h
 *
 * @brief Definitions for Frontend which handles vertex processing,
 *        primitive assembly, clipping, binning, etc.
 *
 ******************************************************************************/
#pragma once
#include "context.h"
#include "common/simdintrin.h"
#include <type_traits>

//////////////////////////////////////////////////////////////////////////
/// @brief Helper macro to generate a bitmask
static INLINE uint32_t
              GenMask(uint32_t numBits)
{
    SWR_ASSERT(
        numBits <= (sizeof(uint32_t) * 8), "Too many bits (%d) for %s", numBits, __FUNCTION__);
    return ((1U << numBits) - 1);
}

// Calculates the A and B coefficients for the 3 edges of the triangle
//
// maths for edge equations:
//   standard form of a line in 2d
//   Ax + By + C = 0
//   A = y0 - y1
//   B = x1 - x0
//   C = x0y1 - x1y0
INLINE
void triangleSetupAB(const __m128 vX, const __m128 vY, __m128& vA, __m128& vB)
{
    // vYsub = y1 y2 y0 dc
    __m128 vYsub = _mm_shuffle_ps(vY, vY, _MM_SHUFFLE(3, 0, 2, 1));
    // vY =    y0 y1 y2 dc
    vA = _mm_sub_ps(vY, vYsub);

    // Result:
    // A[0] = y0 - y1
    // A[1] = y1 - y2
    // A[2] = y2 - y0

    // vXsub = x1 x2 x0 dc
    __m128 vXsub = _mm_shuffle_ps(vX, vX, _MM_SHUFFLE(3, 0, 2, 1));
    // vX =    x0 x1 x2 dc
    vB = _mm_sub_ps(vXsub, vX);

    // Result:
    // B[0] = x1 - x0
    // B[1] = x2 - x1
    // B[2] = x0 - x2
}

INLINE
void triangleSetupABInt(const __m128i vX, const __m128i vY, __m128i& vA, __m128i& vB)
{
    // generate edge equations
    // A = y0 - y1
    // B = x1 - x0
    // C = x0y1 - x1y0
    __m128i vYsub = _mm_shuffle_epi32(vY, _MM_SHUFFLE(3, 0, 2, 1));
    vA            = _mm_sub_epi32(vY, vYsub);

    __m128i vXsub = _mm_shuffle_epi32(vX, _MM_SHUFFLE(3, 0, 2, 1));
    vB            = _mm_sub_epi32(vXsub, vX);
}

INLINE
void triangleSetupABIntVertical(const simdscalari vX[3],
                                const simdscalari vY[3],
                                simdscalari (&vA)[3],
                                simdscalari (&vB)[3])
{
    // A = y0 - y1
    // B = x1 - x0
    vA[0] = _simd_sub_epi32(vY[0], vY[1]);
    vA[1] = _simd_sub_epi32(vY[1], vY[2]);
    vA[2] = _simd_sub_epi32(vY[2], vY[0]);

    vB[0] = _simd_sub_epi32(vX[1], vX[0]);
    vB[1] = _simd_sub_epi32(vX[2], vX[1]);
    vB[2] = _simd_sub_epi32(vX[0], vX[2]);
}

#if ENABLE_AVX512_SIMD16
INLINE
void triangleSetupABIntVertical(const simd16scalari vX[3],
                                const simd16scalari vY[3],
                                simd16scalari (&vA)[3],
                                simd16scalari (&vB)[3])
{
    // A = y0 - y1
    // B = x1 - x0
    vA[0] = _simd16_sub_epi32(vY[0], vY[1]);
    vA[1] = _simd16_sub_epi32(vY[1], vY[2]);
    vA[2] = _simd16_sub_epi32(vY[2], vY[0]);

    vB[0] = _simd16_sub_epi32(vX[1], vX[0]);
    vB[1] = _simd16_sub_epi32(vX[2], vX[1]);
    vB[2] = _simd16_sub_epi32(vX[0], vX[2]);
}

#endif
// Calculate the determinant of the triangle
// 2 vectors between the 3 points: P, Q
// Px = x0-x2, Py = y0-y2
// Qx = x1-x2, Qy = y1-y2
//       |Px Qx|
// det = |     | = PxQy - PyQx
//       |Py Qy|
// simplifies to : (x0-x2)*(y1-y2) - (y0-y2)*(x1-x2)
//               try to reuse our A & B coef's already calculated. factor out a -1 from Py and Qx
//               : B[2]*A[1] - (-(y2-y0))*(-(x2-x1))
//               : B[2]*A[1] - (-1)(-1)(y2-y0)*(x2-x1)
//               : B[2]*A[1] - A[2]*B[1]
INLINE
float calcDeterminantInt(const __m128i vA, const __m128i vB)
{
    // vAShuf = [A1, A0, A2, A0]
    __m128i vAShuf = _mm_shuffle_epi32(vA, _MM_SHUFFLE(0, 2, 0, 1));
    // vBShuf = [B2, B0, B1, B0]
    __m128i vBShuf = _mm_shuffle_epi32(vB, _MM_SHUFFLE(0, 1, 0, 2));
    // vMul = [A1*B2, B1*A2]
    __m128i vMul = _mm_mul_epi32(vAShuf, vBShuf);

    // shuffle upper to lower
    // vMul2 = [B1*A2, B1*A2]
    __m128i vMul2 = _mm_shuffle_epi32(vMul, _MM_SHUFFLE(3, 2, 3, 2));
    // vMul = [A1*B2 - B1*A2]
    vMul = _mm_sub_epi64(vMul, vMul2);

    int64_t result;
    _mm_store_sd((double*)&result, _mm_castsi128_pd(vMul));

    double dResult = (double)result;
    dResult        = dResult * (1.0 / FIXED_POINT16_SCALE);

    return (float)dResult;
}

INLINE
void calcDeterminantIntVertical(const simdscalari vA[3],
                                const simdscalari vB[3],
                                simdscalari*      pvDet)
{
    // refer to calcDeterminantInt comment for calculation explanation

    // A1*B2
    simdscalari vA1Lo = _simd_unpacklo_epi32(vA[1], vA[1]); // 0 0 1 1 4 4 5 5
    simdscalari vA1Hi = _simd_unpackhi_epi32(vA[1], vA[1]); // 2 2 3 3 6 6 7 7

    simdscalari vB2Lo = _simd_unpacklo_epi32(vB[2], vB[2]);
    simdscalari vB2Hi = _simd_unpackhi_epi32(vB[2], vB[2]);

    simdscalari vA1B2Lo = _simd_mul_epi32(vA1Lo, vB2Lo); // 0 1 4 5
    simdscalari vA1B2Hi = _simd_mul_epi32(vA1Hi, vB2Hi); // 2 3 6 7

    // B1*A2
    simdscalari vA2Lo = _simd_unpacklo_epi32(vA[2], vA[2]);
    simdscalari vA2Hi = _simd_unpackhi_epi32(vA[2], vA[2]);

    simdscalari vB1Lo = _simd_unpacklo_epi32(vB[1], vB[1]);
    simdscalari vB1Hi = _simd_unpackhi_epi32(vB[1], vB[1]);

    simdscalari vA2B1Lo = _simd_mul_epi32(vA2Lo, vB1Lo);
    simdscalari vA2B1Hi = _simd_mul_epi32(vA2Hi, vB1Hi);

    // A1*B2 - A2*B1
    simdscalari detLo = _simd_sub_epi64(vA1B2Lo, vA2B1Lo);
    simdscalari detHi = _simd_sub_epi64(vA1B2Hi, vA2B1Hi);

    // shuffle 0 1 4 5 2 3 6 7 -> 0 1 2 3
    simdscalari vResultLo = _simd_permute2f128_si(detLo, detHi, 0x20);

    // shuffle 0 1 4 5 2 3 6 7 -> 4 5 6 7
    simdscalari vResultHi = _simd_permute2f128_si(detLo, detHi, 0x31);

    pvDet[0] = vResultLo;
    pvDet[1] = vResultHi;
}

#if ENABLE_AVX512_SIMD16
INLINE
void calcDeterminantIntVertical(const simd16scalari vA[3],
                                const simd16scalari vB[3],
                                simd16scalari*      pvDet)
{
    // refer to calcDeterminantInt comment for calculation explanation

    // A1*B2
    simd16scalari vA1_lo =
        _simd16_unpacklo_epi32(vA[1], vA[1]); // X 0 X 1 X 4 X 5 X 8 X 9 X C X D (32b)
    simd16scalari vA1_hi = _simd16_unpackhi_epi32(vA[1], vA[1]); // X 2 X 3 X 6 X 7 X A X B X E X F

    simd16scalari vB2_lo = _simd16_unpacklo_epi32(vB[2], vB[2]);
    simd16scalari vB2_hi = _simd16_unpackhi_epi32(vB[2], vB[2]);

    simd16scalari vA1B2_lo = _simd16_mul_epi32(vA1_lo, vB2_lo); // 0 1 4 5 8 9 C D (64b)
    simd16scalari vA1B2_hi = _simd16_mul_epi32(vA1_hi, vB2_hi); // 2 3 6 7 A B E F

    // B1*A2
    simd16scalari vA2_lo = _simd16_unpacklo_epi32(vA[2], vA[2]);
    simd16scalari vA2_hi = _simd16_unpackhi_epi32(vA[2], vA[2]);

    simd16scalari vB1_lo = _simd16_unpacklo_epi32(vB[1], vB[1]);
    simd16scalari vB1_hi = _simd16_unpackhi_epi32(vB[1], vB[1]);

    simd16scalari vA2B1_lo = _simd16_mul_epi32(vA2_lo, vB1_lo);
    simd16scalari vA2B1_hi = _simd16_mul_epi32(vA2_hi, vB1_hi);

    // A1*B2 - A2*B1
    simd16scalari difflo = _simd16_sub_epi64(vA1B2_lo, vA2B1_lo); // 0 1 4 5 8 9 C D (64b)
    simd16scalari diffhi = _simd16_sub_epi64(vA1B2_hi, vA2B1_hi); // 2 3 6 7 A B E F

    // (1, 0, 1, 0) = 01 00 01 00 = 0x44, (3, 2, 3, 2) = 11 10 11 10 = 0xEE
    simd16scalari templo = _simd16_permute2f128_si(difflo, diffhi, 0x44); // 0 1 4 5 2 3 6 7 (64b)
    simd16scalari temphi = _simd16_permute2f128_si(difflo, diffhi, 0xEE); // 8 9 C D A B E F

    // (3, 1, 2, 0) = 11 01 10 00 = 0xD8
    pvDet[0] = _simd16_permute2f128_si(templo, templo, 0xD8); // 0 1 2 3 4 5 6 7 (64b)
    pvDet[1] = _simd16_permute2f128_si(temphi, temphi, 0xD8); // 8 9 A B C D E F
}

#endif
INLINE
void triangleSetupC(const __m128 vX, const __m128 vY, const __m128 vA, const __m128& vB, __m128& vC)
{
    // C = -Ax - By
    vC         = _mm_mul_ps(vA, vX);
    __m128 vCy = _mm_mul_ps(vB, vY);
    vC         = _mm_mul_ps(vC, _mm_set1_ps(-1.0f));
    vC         = _mm_sub_ps(vC, vCy);
}

template <uint32_t NumVerts>
INLINE void viewportTransform(simdvector* v, const SWR_VIEWPORT_MATRICES& vpMatrices)
{
    simdscalar m00 = _simd_load1_ps(&vpMatrices.m00[0]);
    simdscalar m30 = _simd_load1_ps(&vpMatrices.m30[0]);
    simdscalar m11 = _simd_load1_ps(&vpMatrices.m11[0]);
    simdscalar m31 = _simd_load1_ps(&vpMatrices.m31[0]);
    simdscalar m22 = _simd_load1_ps(&vpMatrices.m22[0]);
    simdscalar m32 = _simd_load1_ps(&vpMatrices.m32[0]);

    for (uint32_t i = 0; i < NumVerts; ++i)
    {
        v[i].x = _simd_fmadd_ps(v[i].x, m00, m30);
        v[i].y = _simd_fmadd_ps(v[i].y, m11, m31);
        v[i].z = _simd_fmadd_ps(v[i].z, m22, m32);
    }
}

#if USE_SIMD16_FRONTEND
template <uint32_t NumVerts>
INLINE void viewportTransform(simd16vector* v, const SWR_VIEWPORT_MATRICES& vpMatrices)
{
    const simd16scalar m00 = _simd16_broadcast_ss(&vpMatrices.m00[0]);
    const simd16scalar m30 = _simd16_broadcast_ss(&vpMatrices.m30[0]);
    const simd16scalar m11 = _simd16_broadcast_ss(&vpMatrices.m11[0]);
    const simd16scalar m31 = _simd16_broadcast_ss(&vpMatrices.m31[0]);
    const simd16scalar m22 = _simd16_broadcast_ss(&vpMatrices.m22[0]);
    const simd16scalar m32 = _simd16_broadcast_ss(&vpMatrices.m32[0]);

    for (uint32_t i = 0; i < NumVerts; ++i)
    {
        v[i].x = _simd16_fmadd_ps(v[i].x, m00, m30);
        v[i].y = _simd16_fmadd_ps(v[i].y, m11, m31);
        v[i].z = _simd16_fmadd_ps(v[i].z, m22, m32);
    }
}

#endif
template <uint32_t NumVerts>
INLINE void viewportTransform(simdvector*                  v,
                              const SWR_VIEWPORT_MATRICES& vpMatrices,
                              simdscalari const&           vViewportIdx)
{
    // perform a gather of each matrix element based on the viewport array indexes
    simdscalar m00 = _simd_i32gather_ps(&vpMatrices.m00[0], vViewportIdx, 4);
    simdscalar m30 = _simd_i32gather_ps(&vpMatrices.m30[0], vViewportIdx, 4);
    simdscalar m11 = _simd_i32gather_ps(&vpMatrices.m11[0], vViewportIdx, 4);
    simdscalar m31 = _simd_i32gather_ps(&vpMatrices.m31[0], vViewportIdx, 4);
    simdscalar m22 = _simd_i32gather_ps(&vpMatrices.m22[0], vViewportIdx, 4);
    simdscalar m32 = _simd_i32gather_ps(&vpMatrices.m32[0], vViewportIdx, 4);

    for (uint32_t i = 0; i < NumVerts; ++i)
    {
        v[i].x = _simd_fmadd_ps(v[i].x, m00, m30);
        v[i].y = _simd_fmadd_ps(v[i].y, m11, m31);
        v[i].z = _simd_fmadd_ps(v[i].z, m22, m32);
    }
}

#if USE_SIMD16_FRONTEND
template <uint32_t NumVerts>
INLINE void viewportTransform(simd16vector*                v,
                              const SWR_VIEWPORT_MATRICES& vpMatrices,
                              simd16scalari const&         vViewportIdx)
{
    // perform a gather of each matrix element based on the viewport array indexes
    const simd16scalar m00 = _simd16_i32gather_ps(&vpMatrices.m00[0], vViewportIdx, 4);
    const simd16scalar m30 = _simd16_i32gather_ps(&vpMatrices.m30[0], vViewportIdx, 4);
    const simd16scalar m11 = _simd16_i32gather_ps(&vpMatrices.m11[0], vViewportIdx, 4);
    const simd16scalar m31 = _simd16_i32gather_ps(&vpMatrices.m31[0], vViewportIdx, 4);
    const simd16scalar m22 = _simd16_i32gather_ps(&vpMatrices.m22[0], vViewportIdx, 4);
    const simd16scalar m32 = _simd16_i32gather_ps(&vpMatrices.m32[0], vViewportIdx, 4);

    for (uint32_t i = 0; i < NumVerts; ++i)
    {
        v[i].x = _simd16_fmadd_ps(v[i].x, m00, m30);
        v[i].y = _simd16_fmadd_ps(v[i].y, m11, m31);
        v[i].z = _simd16_fmadd_ps(v[i].z, m22, m32);
    }
}

#endif
INLINE
void calcBoundingBoxInt(const __m128i& vX, const __m128i& vY, SWR_RECT& bbox)
{
    // Need horizontal fp min here
    __m128i vX1 = _mm_shuffle_epi32(vX, _MM_SHUFFLE(3, 2, 0, 1));
    __m128i vX2 = _mm_shuffle_epi32(vX, _MM_SHUFFLE(3, 0, 1, 2));

    __m128i vY1 = _mm_shuffle_epi32(vY, _MM_SHUFFLE(3, 2, 0, 1));
    __m128i vY2 = _mm_shuffle_epi32(vY, _MM_SHUFFLE(3, 0, 1, 2));

    __m128i vMinX = _mm_min_epi32(vX, vX1);
    vMinX         = _mm_min_epi32(vMinX, vX2);

    __m128i vMaxX = _mm_max_epi32(vX, vX1);
    vMaxX         = _mm_max_epi32(vMaxX, vX2);

    __m128i vMinY = _mm_min_epi32(vY, vY1);
    vMinY         = _mm_min_epi32(vMinY, vY2);

    __m128i vMaxY = _mm_max_epi32(vY, vY1);
    vMaxY         = _mm_max_epi32(vMaxY, vY2);

    bbox.xmin = _mm_extract_epi32(vMinX, 0);
    bbox.xmax = _mm_extract_epi32(vMaxX, 0);
    bbox.ymin = _mm_extract_epi32(vMinY, 0);
    bbox.ymax = _mm_extract_epi32(vMaxY, 0);
}

INLINE
bool CanUseSimplePoints(DRAW_CONTEXT* pDC)
{
    const API_STATE& state = GetApiState(pDC);

    return (state.rastState.sampleCount == SWR_MULTISAMPLE_1X &&
            state.rastState.pointSize == 1.0f && !state.rastState.pointParam &&
            !state.rastState.pointSpriteEnable && !state.backendState.clipDistanceMask);
}

INLINE
bool vHasNaN(const __m128& vec)
{
    const __m128  result = _mm_cmpunord_ps(vec, vec);
    const int32_t mask   = _mm_movemask_ps(result);
    return (mask != 0);
}

uint32_t GetNumPrims(PRIMITIVE_TOPOLOGY mode, uint32_t numElements);
uint32_t NumVertsPerPrim(PRIMITIVE_TOPOLOGY topology, bool includeAdjVerts);

// ProcessDraw front-end function.  All combinations of parameter values are available
PFN_FE_WORK_FUNC GetProcessDrawFunc(bool IsIndexed,
                                    bool IsCutIndexEnabled,
                                    bool HasTessellation,
                                    bool HasGeometryShader,
                                    bool HasStreamOut,
                                    bool HasRasterization);

void ProcessClear(SWR_CONTEXT* pContext, DRAW_CONTEXT* pDC, uint32_t workerId, void* pUserData);
void ProcessStoreTiles(SWR_CONTEXT*  pContext,
                       DRAW_CONTEXT* pDC,
                       uint32_t      workerId,
                       void*         pUserData);
void ProcessDiscardInvalidateTiles(SWR_CONTEXT*  pContext,
                                   DRAW_CONTEXT* pDC,
                                   uint32_t      workerId,
                                   void*         pUserData);
void ProcessSync(SWR_CONTEXT* pContext, DRAW_CONTEXT* pDC, uint32_t workerId, void* pUserData);
void ProcessShutdown(SWR_CONTEXT* pContext, DRAW_CONTEXT* pDC, uint32_t workerId, void* pUserData);

PFN_PROCESS_PRIMS GetBinTrianglesFunc(bool IsConservative);
#if USE_SIMD16_FRONTEND
PFN_PROCESS_PRIMS_SIMD16 GetBinTrianglesFunc_simd16(bool IsConservative);
#endif

struct PA_STATE_BASE; // forward decl
void BinPoints(DRAW_CONTEXT*      pDC,
               PA_STATE&          pa,
               uint32_t           workerId,
               simdvector         prims[3],
               uint32_t           primMask,
               simdscalari const& primID,
               simdscalari const& viewportIdx,
               simdscalari const& rtIdx);
void BinLines(DRAW_CONTEXT*      pDC,
              PA_STATE&          pa,
              uint32_t           workerId,
              simdvector         prims[3],
              uint32_t           primMask,
              simdscalari const& primID,
              simdscalari const& viewportIdx,
              simdscalari const& rtIdx);
#if USE_SIMD16_FRONTEND
void SIMDCALL BinPoints_simd16(DRAW_CONTEXT*        pDC,
                               PA_STATE&            pa,
                               uint32_t             workerId,
                               simd16vector         prims[3],
                               uint32_t             primMask,
                               simd16scalari const& primID,
                               simd16scalari const& viewportIdx,
                               simd16scalari const& rtIdx);
void SIMDCALL BinLines_simd16(DRAW_CONTEXT*        pDC,
                              PA_STATE&            pa,
                              uint32_t             workerId,
                              simd16vector         prims[3],
                              uint32_t             primMask,
                              simd16scalari const& primID,
                              simd16scalari const& viewportIdx,
                              simd16scalari const& rtIdx);
#endif

