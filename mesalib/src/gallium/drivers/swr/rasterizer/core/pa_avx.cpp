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
 * @file pa_avx.cpp
 *
 * @brief AVX implementation for primitive assembly.
 *        N primitives are assembled at a time, where N is the SIMD width.
 *        A state machine, that is specific for a given topology, drives the
 *        assembly of vertices into triangles.
 *
 ******************************************************************************/
#include "context.h"
#include "pa.h"
#include "frontend.h"

#if (KNOB_SIMD_WIDTH == 8)

INLINE simd4scalar swizzleLane0(const simdscalar& x,
                                const simdscalar& y,
                                const simdscalar& z,
                                const simdscalar& w)
{
    simdscalar tmp0 = _mm256_unpacklo_ps(x, z);
    simdscalar tmp1 = _mm256_unpacklo_ps(y, w);
    return _mm256_extractf128_ps(_mm256_unpacklo_ps(tmp0, tmp1), 0);
}

INLINE simd4scalar swizzleLane1(const simdscalar& x,
                                const simdscalar& y,
                                const simdscalar& z,
                                const simdscalar& w)
{
    simdscalar tmp0 = _mm256_unpacklo_ps(x, z);
    simdscalar tmp1 = _mm256_unpacklo_ps(y, w);
    return _mm256_extractf128_ps(_mm256_unpackhi_ps(tmp0, tmp1), 0);
}

INLINE simd4scalar swizzleLane2(const simdscalar& x,
                                const simdscalar& y,
                                const simdscalar& z,
                                const simdscalar& w)
{
    simdscalar tmp0 = _mm256_unpackhi_ps(x, z);
    simdscalar tmp1 = _mm256_unpackhi_ps(y, w);
    return _mm256_extractf128_ps(_mm256_unpacklo_ps(tmp0, tmp1), 0);
}

INLINE simd4scalar swizzleLane3(const simdscalar& x,
                                const simdscalar& y,
                                const simdscalar& z,
                                const simdscalar& w)
{
    simdscalar tmp0 = _mm256_unpackhi_ps(x, z);
    simdscalar tmp1 = _mm256_unpackhi_ps(y, w);
    return _mm256_extractf128_ps(_mm256_unpackhi_ps(tmp0, tmp1), 0);
}

INLINE simd4scalar swizzleLane4(const simdscalar& x,
                                const simdscalar& y,
                                const simdscalar& z,
                                const simdscalar& w)
{
    simdscalar tmp0 = _mm256_unpacklo_ps(x, z);
    simdscalar tmp1 = _mm256_unpacklo_ps(y, w);
    return _mm256_extractf128_ps(_mm256_unpacklo_ps(tmp0, tmp1), 1);
}

INLINE simd4scalar swizzleLane5(const simdscalar& x,
                                const simdscalar& y,
                                const simdscalar& z,
                                const simdscalar& w)
{
    simdscalar tmp0 = _mm256_unpacklo_ps(x, z);
    simdscalar tmp1 = _mm256_unpacklo_ps(y, w);
    return _mm256_extractf128_ps(_mm256_unpackhi_ps(tmp0, tmp1), 1);
}

INLINE simd4scalar swizzleLane6(const simdscalar& x,
                                const simdscalar& y,
                                const simdscalar& z,
                                const simdscalar& w)
{
    simdscalar tmp0 = _mm256_unpackhi_ps(x, z);
    simdscalar tmp1 = _mm256_unpackhi_ps(y, w);
    return _mm256_extractf128_ps(_mm256_unpacklo_ps(tmp0, tmp1), 1);
}

INLINE simd4scalar swizzleLane7(const simdscalar& x,
                                const simdscalar& y,
                                const simdscalar& z,
                                const simdscalar& w)
{
    simdscalar tmp0 = _mm256_unpackhi_ps(x, z);
    simdscalar tmp1 = _mm256_unpackhi_ps(y, w);
    return _mm256_extractf128_ps(_mm256_unpackhi_ps(tmp0, tmp1), 1);
}

INLINE simd4scalar swizzleLane0(const simdvector& v)
{
    return swizzleLane0(v.x, v.y, v.z, v.w);
}

INLINE simd4scalar swizzleLane1(const simdvector& v)
{
    return swizzleLane1(v.x, v.y, v.z, v.w);
}

INLINE simd4scalar swizzleLane2(const simdvector& v)
{
    return swizzleLane2(v.x, v.y, v.z, v.w);
}

INLINE simd4scalar swizzleLane3(const simdvector& v)
{
    return swizzleLane3(v.x, v.y, v.z, v.w);
}

INLINE simd4scalar swizzleLane4(const simdvector& v)
{
    return swizzleLane4(v.x, v.y, v.z, v.w);
}

INLINE simd4scalar swizzleLane5(const simdvector& v)
{
    return swizzleLane5(v.x, v.y, v.z, v.w);
}

INLINE simd4scalar swizzleLane6(const simdvector& v)
{
    return swizzleLane6(v.x, v.y, v.z, v.w);
}

INLINE simd4scalar swizzleLane7(const simdvector& v)
{
    return swizzleLane7(v.x, v.y, v.z, v.w);
}

INLINE simd4scalar swizzleLaneN(const simdvector& v, int lane)
{
    switch (lane)
    {
    case 0:
        return swizzleLane0(v);
    case 1:
        return swizzleLane1(v);
    case 2:
        return swizzleLane2(v);
    case 3:
        return swizzleLane3(v);
    case 4:
        return swizzleLane4(v);
    case 5:
        return swizzleLane5(v);
    case 6:
        return swizzleLane6(v);
    case 7:
        return swizzleLane7(v);
    default:
        return _mm_setzero_ps();
    }
}

#if ENABLE_AVX512_SIMD16
INLINE simd4scalar swizzleLane0(const simd16vector& v)
{
    return swizzleLane0(_simd16_extract_ps(v.x, 0),
                        _simd16_extract_ps(v.y, 0),
                        _simd16_extract_ps(v.z, 0),
                        _simd16_extract_ps(v.w, 0));
}

INLINE simd4scalar swizzleLane1(const simd16vector& v)
{
    return swizzleLane1(_simd16_extract_ps(v.x, 0),
                        _simd16_extract_ps(v.y, 0),
                        _simd16_extract_ps(v.z, 0),
                        _simd16_extract_ps(v.w, 0));
}

INLINE simd4scalar swizzleLane2(const simd16vector& v)
{
    return swizzleLane2(_simd16_extract_ps(v.x, 0),
                        _simd16_extract_ps(v.y, 0),
                        _simd16_extract_ps(v.z, 0),
                        _simd16_extract_ps(v.w, 0));
}

INLINE simd4scalar swizzleLane3(const simd16vector& v)
{
    return swizzleLane3(_simd16_extract_ps(v.x, 0),
                        _simd16_extract_ps(v.y, 0),
                        _simd16_extract_ps(v.z, 0),
                        _simd16_extract_ps(v.w, 0));
}

INLINE simd4scalar swizzleLane4(const simd16vector& v)
{
    return swizzleLane4(_simd16_extract_ps(v.x, 0),
                        _simd16_extract_ps(v.y, 0),
                        _simd16_extract_ps(v.z, 0),
                        _simd16_extract_ps(v.w, 0));
}

INLINE simd4scalar swizzleLane5(const simd16vector& v)
{
    return swizzleLane5(_simd16_extract_ps(v.x, 0),
                        _simd16_extract_ps(v.y, 0),
                        _simd16_extract_ps(v.z, 0),
                        _simd16_extract_ps(v.w, 0));
}

INLINE simd4scalar swizzleLane6(const simd16vector& v)
{
    return swizzleLane6(_simd16_extract_ps(v.x, 0),
                        _simd16_extract_ps(v.y, 0),
                        _simd16_extract_ps(v.z, 0),
                        _simd16_extract_ps(v.w, 0));
}

INLINE simd4scalar swizzleLane7(const simd16vector& v)
{
    return swizzleLane7(_simd16_extract_ps(v.x, 0),
                        _simd16_extract_ps(v.y, 0),
                        _simd16_extract_ps(v.z, 0),
                        _simd16_extract_ps(v.w, 0));
}

INLINE simd4scalar swizzleLane8(const simd16vector& v)
{
    return swizzleLane0(_simd16_extract_ps(v.x, 1),
                        _simd16_extract_ps(v.y, 1),
                        _simd16_extract_ps(v.z, 1),
                        _simd16_extract_ps(v.w, 1));
}

INLINE simd4scalar swizzleLane9(const simd16vector& v)
{
    return swizzleLane1(_simd16_extract_ps(v.x, 1),
                        _simd16_extract_ps(v.y, 1),
                        _simd16_extract_ps(v.z, 1),
                        _simd16_extract_ps(v.w, 1));
}

INLINE simd4scalar swizzleLaneA(const simd16vector& v)
{
    return swizzleLane2(_simd16_extract_ps(v.x, 1),
                        _simd16_extract_ps(v.y, 1),
                        _simd16_extract_ps(v.z, 1),
                        _simd16_extract_ps(v.w, 1));
}

INLINE simd4scalar swizzleLaneB(const simd16vector& v)
{
    return swizzleLane3(_simd16_extract_ps(v.x, 1),
                        _simd16_extract_ps(v.y, 1),
                        _simd16_extract_ps(v.z, 1),
                        _simd16_extract_ps(v.w, 1));
}

INLINE simd4scalar swizzleLaneC(const simd16vector& v)
{
    return swizzleLane4(_simd16_extract_ps(v.x, 1),
                        _simd16_extract_ps(v.y, 1),
                        _simd16_extract_ps(v.z, 1),
                        _simd16_extract_ps(v.w, 1));
}

INLINE simd4scalar swizzleLaneD(const simd16vector& v)
{
    return swizzleLane5(_simd16_extract_ps(v.x, 1),
                        _simd16_extract_ps(v.y, 1),
                        _simd16_extract_ps(v.z, 1),
                        _simd16_extract_ps(v.w, 1));
}

INLINE simd4scalar swizzleLaneE(const simd16vector& v)
{
    return swizzleLane6(_simd16_extract_ps(v.x, 1),
                        _simd16_extract_ps(v.y, 1),
                        _simd16_extract_ps(v.z, 1),
                        _simd16_extract_ps(v.w, 1));
}

INLINE simd4scalar swizzleLaneF(const simd16vector& v)
{
    return swizzleLane7(_simd16_extract_ps(v.x, 1),
                        _simd16_extract_ps(v.y, 1),
                        _simd16_extract_ps(v.z, 1),
                        _simd16_extract_ps(v.w, 1));
}

INLINE simd4scalar swizzleLaneN(const simd16vector& v, int lane)
{
    switch (lane)
    {
    case 0:
        return swizzleLane0(v);
    case 1:
        return swizzleLane1(v);
    case 2:
        return swizzleLane2(v);
    case 3:
        return swizzleLane3(v);
    case 4:
        return swizzleLane4(v);
    case 5:
        return swizzleLane5(v);
    case 6:
        return swizzleLane6(v);
    case 7:
        return swizzleLane7(v);
    case 8:
        return swizzleLane8(v);
    case 9:
        return swizzleLane9(v);
    case 10:
        return swizzleLaneA(v);
    case 11:
        return swizzleLaneB(v);
    case 12:
        return swizzleLaneC(v);
    case 13:
        return swizzleLaneD(v);
    case 14:
        return swizzleLaneE(v);
    case 15:
        return swizzleLaneF(v);
    default:
        return _mm_setzero_ps();
    }
}

#endif
bool PaTriList0(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[]);
bool PaTriList1(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[]);
bool PaTriList2(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[]);
#if ENABLE_AVX512_SIMD16
bool PaTriList0_simd16(PA_STATE_OPT& pa, uint32_t slot, simd16vector verts[]);
bool PaTriList1_simd16(PA_STATE_OPT& pa, uint32_t slot, simd16vector verts[]);
bool PaTriList2_simd16(PA_STATE_OPT& pa, uint32_t slot, simd16vector verts[]);
#endif
void PaTriListSingle0(PA_STATE_OPT& pa, uint32_t slot, uint32_t primIndex, simd4scalar verts[]);

bool PaTriStrip0(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[]);
bool PaTriStrip1(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[]);
#if ENABLE_AVX512_SIMD16
bool PaTriStrip0_simd16(PA_STATE_OPT& pa, uint32_t slot, simd16vector verts[]);
bool PaTriStrip1_simd16(PA_STATE_OPT& pa, uint32_t slot, simd16vector verts[]);
#endif
void PaTriStripSingle0(PA_STATE_OPT& pa, uint32_t slot, uint32_t primIndex, simd4scalar verts[]);

bool PaTriFan0(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[]);
bool PaTriFan1(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[]);
#if ENABLE_AVX512_SIMD16
bool PaTriFan0_simd16(PA_STATE_OPT& pa, uint32_t slot, simd16vector verts[]);
bool PaTriFan1_simd16(PA_STATE_OPT& pa, uint32_t slot, simd16vector verts[]);
#endif
void PaTriFanSingle0(PA_STATE_OPT& pa, uint32_t slot, uint32_t primIndex, simd4scalar verts[]);

bool PaQuadList0(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[]);
bool PaQuadList1(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[]);
#if ENABLE_AVX512_SIMD16
bool PaQuadList0_simd16(PA_STATE_OPT& pa, uint32_t slot, simd16vector verts[]);
bool PaQuadList1_simd16(PA_STATE_OPT& pa, uint32_t slot, simd16vector verts[]);
#endif
void PaQuadListSingle0(PA_STATE_OPT& pa, uint32_t slot, uint32_t primIndex, simd4scalar verts[]);

bool PaLineLoop0(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[]);
bool PaLineLoop1(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[]);
#if ENABLE_AVX512_SIMD16
bool PaLineLoop0_simd16(PA_STATE_OPT& pa, uint32_t slot, simd16vector verts[]);
bool PaLineLoop1_simd16(PA_STATE_OPT& pa, uint32_t slot, simd16vector verts[]);
#endif
void PaLineLoopSingle0(PA_STATE_OPT& pa, uint32_t slot, uint32_t primIndex, simd4scalar verts[]);

bool PaLineList0(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[]);
bool PaLineList1(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[]);
#if ENABLE_AVX512_SIMD16
bool PaLineList0_simd16(PA_STATE_OPT& pa, uint32_t slot, simd16vector verts[]);
bool PaLineList1_simd16(PA_STATE_OPT& pa, uint32_t slot, simd16vector verts[]);
#endif
void PaLineListSingle0(PA_STATE_OPT& pa, uint32_t slot, uint32_t primIndex, simd4scalar verts[]);

bool PaLineStrip0(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[]);
bool PaLineStrip1(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[]);
#if ENABLE_AVX512_SIMD16
bool PaLineStrip0_simd16(PA_STATE_OPT& pa, uint32_t slot, simd16vector verts[]);
bool PaLineStrip1_simd16(PA_STATE_OPT& pa, uint32_t slot, simd16vector verts[]);
#endif
void PaLineStripSingle0(PA_STATE_OPT& pa, uint32_t slot, uint32_t primIndex, simd4scalar verts[]);

bool PaPoints0(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[]);
#if ENABLE_AVX512_SIMD16
bool PaPoints0_simd16(PA_STATE_OPT& pa, uint32_t slot, simd16vector verts[]);
#endif
void PaPointsSingle0(PA_STATE_OPT& pa, uint32_t slot, uint32_t primIndex, simd4scalar verts[]);

bool PaRectList0(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[]);
bool PaRectList1(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[]);
bool PaRectList2(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[]);
#if ENABLE_AVX512_SIMD16
bool PaRectList0_simd16(PA_STATE_OPT& pa, uint32_t slot, simd16vector verts[]);
bool PaRectList1_simd16(PA_STATE_OPT& pa, uint32_t slot, simd16vector verts[]);
bool PaRectList2_simd16(PA_STATE_OPT& pa, uint32_t slot, simd16vector verts[]);
#endif
void PaRectListSingle0(PA_STATE_OPT& pa, uint32_t slot, uint32_t primIndex, simd4scalar verts[]);

template <uint32_t TotalControlPoints>
void PaPatchListSingle(PA_STATE_OPT& pa, uint32_t slot, uint32_t primIndex, simd4scalar verts[])
{
    // We have an input of KNOB_SIMD_WIDTH * TotalControlPoints and we output
    // KNOB_SIMD_WIDTH * 1 patch.  This function is called once per attribute.
    // Each attribute has 4 components.

    /// @todo Optimize this

#if USE_SIMD16_FRONTEND
    if (pa.useAlternateOffset)
    {
        primIndex += KNOB_SIMD_WIDTH;
    }

#endif
    float* pOutVec = (float*)verts;

    for (uint32_t cp = 0; cp < TotalControlPoints; ++cp)
    {
        uint32_t input_cp = primIndex * TotalControlPoints + cp;
#if USE_SIMD16_FRONTEND
        uint32_t input_vec  = input_cp / KNOB_SIMD16_WIDTH;
        uint32_t input_lane = input_cp % KNOB_SIMD16_WIDTH;

#else
        uint32_t input_vec  = input_cp / KNOB_SIMD_WIDTH;
        uint32_t input_lane = input_cp % KNOB_SIMD_WIDTH;

#endif
        // Loop over all components of the attribute
        for (uint32_t i = 0; i < 4; ++i)
        {
#if USE_SIMD16_FRONTEND
            const float* pInputVec =
                (const float*)(&PaGetSimdVector_simd16(pa, input_vec, slot)[i]);
#else
            const float* pInputVec = (const float*)(&PaGetSimdVector(pa, input_vec, slot)[i]);
#endif
            pOutVec[cp * 4 + i] = pInputVec[input_lane];
        }
    }
}

template <uint32_t TotalControlPoints, uint32_t CurrentControlPoints = 1>
static bool PaPatchList(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[])
{
    SetNextPaState(pa,
                   PaPatchList<TotalControlPoints, CurrentControlPoints + 1>,
                   PaPatchListSingle<TotalControlPoints>);

    return false;
}

template <uint32_t TotalControlPoints>
static bool PaPatchListTerm(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[])
{
    // We have an input of KNOB_SIMD_WIDTH * TotalControlPoints and we output
    // KNOB_SIMD_WIDTH * 1 patch.  This function is called once per attribute.
    // Each attribute has 4 components.

    /// @todo Optimize this

#if USE_SIMD16_FRONTEND
    uint32_t lane_offset = 0;

    if (pa.useAlternateOffset)
    {
        lane_offset = KNOB_SIMD_WIDTH;
    }

#endif
    // Loop over all components of the attribute
    for (uint32_t i = 0; i < 4; ++i)
    {
        for (uint32_t cp = 0; cp < TotalControlPoints; ++cp)
        {
            float vec[KNOB_SIMD_WIDTH];
            for (uint32_t lane = 0; lane < KNOB_SIMD_WIDTH; ++lane)
            {
#if USE_SIMD16_FRONTEND
                uint32_t input_cp   = (lane + lane_offset) * TotalControlPoints + cp;
                uint32_t input_vec  = input_cp / KNOB_SIMD16_WIDTH;
                uint32_t input_lane = input_cp % KNOB_SIMD16_WIDTH;

                const float* pInputVec =
                    (const float*)(&PaGetSimdVector_simd16(pa, input_vec, slot)[i]);
#else
                uint32_t input_cp   = lane * TotalControlPoints + cp;
                uint32_t input_vec  = input_cp / KNOB_SIMD_WIDTH;
                uint32_t input_lane = input_cp % KNOB_SIMD_WIDTH;

                const float* pInputVec = (const float*)(&PaGetSimdVector(pa, input_vec, slot)[i]);
#endif
                vec[lane] = pInputVec[input_lane];
            }
            verts[cp][i] = _simd_loadu_ps(vec);
        }
    }

    SetNextPaState(pa,
                   PaPatchList<TotalControlPoints>,
                   PaPatchListSingle<TotalControlPoints>,
                   0,
                   PA_STATE_OPT::SIMD_WIDTH,
                   true);

    return true;
}

#if ENABLE_AVX512_SIMD16
template <uint32_t TotalControlPoints, uint32_t CurrentControlPoints = 1>
static bool PaPatchList_simd16(PA_STATE_OPT& pa, uint32_t slot, simd16vector verts[])
{
    SetNextPaState_simd16(pa,
                          PaPatchList_simd16<TotalControlPoints, CurrentControlPoints + 1>,
                          PaPatchList<TotalControlPoints, CurrentControlPoints + 1>,
                          PaPatchListSingle<TotalControlPoints>);

    return false;
}

template <uint32_t TotalControlPoints>
static bool PaPatchListTerm_simd16(PA_STATE_OPT& pa, uint32_t slot, simd16vector verts[])
{
    // We have an input of KNOB_SIMD_WIDTH * TotalControlPoints and we output
    // KNOB_SIMD16_WIDTH * 1 patch.  This function is called once per attribute.
    // Each attribute has 4 components.

    /// @todo Optimize this

    // Loop over all components of the attribute
    for (uint32_t i = 0; i < 4; ++i)
    {
        for (uint32_t cp = 0; cp < TotalControlPoints; ++cp)
        {
            float vec[KNOB_SIMD16_WIDTH];
            for (uint32_t lane = 0; lane < KNOB_SIMD16_WIDTH; ++lane)
            {
                uint32_t input_cp   = lane * TotalControlPoints + cp;
                uint32_t input_vec  = input_cp / KNOB_SIMD16_WIDTH;
                uint32_t input_lane = input_cp % KNOB_SIMD16_WIDTH;

                const float* pInputVec = (const float*)(&PaGetSimdVector(pa, input_vec, slot)[i]);
                vec[lane]              = pInputVec[input_lane];
            }
            verts[cp][i] = _simd16_loadu_ps(vec);
        }
    }

    SetNextPaState_simd16(pa,
                          PaPatchList_simd16<TotalControlPoints>,
                          PaPatchList<TotalControlPoints>,
                          PaPatchListSingle<TotalControlPoints>,
                          0,
                          PA_STATE_OPT::SIMD_WIDTH,
                          true);

    return true;
}

#endif
#define PA_PATCH_LIST_TERMINATOR(N)                                              \
    template <>                                                                  \
    bool PaPatchList<N, N>(PA_STATE_OPT & pa, uint32_t slot, simdvector verts[]) \
    {                                                                            \
        return PaPatchListTerm<N>(pa, slot, verts);                              \
    }
PA_PATCH_LIST_TERMINATOR(1)
PA_PATCH_LIST_TERMINATOR(2)
PA_PATCH_LIST_TERMINATOR(3)
PA_PATCH_LIST_TERMINATOR(4)
PA_PATCH_LIST_TERMINATOR(5)
PA_PATCH_LIST_TERMINATOR(6)
PA_PATCH_LIST_TERMINATOR(7)
PA_PATCH_LIST_TERMINATOR(8)
PA_PATCH_LIST_TERMINATOR(9)
PA_PATCH_LIST_TERMINATOR(10)
PA_PATCH_LIST_TERMINATOR(11)
PA_PATCH_LIST_TERMINATOR(12)
PA_PATCH_LIST_TERMINATOR(13)
PA_PATCH_LIST_TERMINATOR(14)
PA_PATCH_LIST_TERMINATOR(15)
PA_PATCH_LIST_TERMINATOR(16)
PA_PATCH_LIST_TERMINATOR(17)
PA_PATCH_LIST_TERMINATOR(18)
PA_PATCH_LIST_TERMINATOR(19)
PA_PATCH_LIST_TERMINATOR(20)
PA_PATCH_LIST_TERMINATOR(21)
PA_PATCH_LIST_TERMINATOR(22)
PA_PATCH_LIST_TERMINATOR(23)
PA_PATCH_LIST_TERMINATOR(24)
PA_PATCH_LIST_TERMINATOR(25)
PA_PATCH_LIST_TERMINATOR(26)
PA_PATCH_LIST_TERMINATOR(27)
PA_PATCH_LIST_TERMINATOR(28)
PA_PATCH_LIST_TERMINATOR(29)
PA_PATCH_LIST_TERMINATOR(30)
PA_PATCH_LIST_TERMINATOR(31)
PA_PATCH_LIST_TERMINATOR(32)
#undef PA_PATCH_LIST_TERMINATOR

#if ENABLE_AVX512_SIMD16
#define PA_PATCH_LIST_TERMINATOR_SIMD16(N)                                                \
    template <>                                                                           \
    bool PaPatchList_simd16<N, N>(PA_STATE_OPT & pa, uint32_t slot, simd16vector verts[]) \
    {                                                                                     \
        return PaPatchListTerm_simd16<N>(pa, slot, verts);                                \
    }
PA_PATCH_LIST_TERMINATOR_SIMD16(1)
PA_PATCH_LIST_TERMINATOR_SIMD16(2)
PA_PATCH_LIST_TERMINATOR_SIMD16(3)
PA_PATCH_LIST_TERMINATOR_SIMD16(4)
PA_PATCH_LIST_TERMINATOR_SIMD16(5)
PA_PATCH_LIST_TERMINATOR_SIMD16(6)
PA_PATCH_LIST_TERMINATOR_SIMD16(7)
PA_PATCH_LIST_TERMINATOR_SIMD16(8)
PA_PATCH_LIST_TERMINATOR_SIMD16(9)
PA_PATCH_LIST_TERMINATOR_SIMD16(10)
PA_PATCH_LIST_TERMINATOR_SIMD16(11)
PA_PATCH_LIST_TERMINATOR_SIMD16(12)
PA_PATCH_LIST_TERMINATOR_SIMD16(13)
PA_PATCH_LIST_TERMINATOR_SIMD16(14)
PA_PATCH_LIST_TERMINATOR_SIMD16(15)
PA_PATCH_LIST_TERMINATOR_SIMD16(16)
PA_PATCH_LIST_TERMINATOR_SIMD16(17)
PA_PATCH_LIST_TERMINATOR_SIMD16(18)
PA_PATCH_LIST_TERMINATOR_SIMD16(19)
PA_PATCH_LIST_TERMINATOR_SIMD16(20)
PA_PATCH_LIST_TERMINATOR_SIMD16(21)
PA_PATCH_LIST_TERMINATOR_SIMD16(22)
PA_PATCH_LIST_TERMINATOR_SIMD16(23)
PA_PATCH_LIST_TERMINATOR_SIMD16(24)
PA_PATCH_LIST_TERMINATOR_SIMD16(25)
PA_PATCH_LIST_TERMINATOR_SIMD16(26)
PA_PATCH_LIST_TERMINATOR_SIMD16(27)
PA_PATCH_LIST_TERMINATOR_SIMD16(28)
PA_PATCH_LIST_TERMINATOR_SIMD16(29)
PA_PATCH_LIST_TERMINATOR_SIMD16(30)
PA_PATCH_LIST_TERMINATOR_SIMD16(31)
PA_PATCH_LIST_TERMINATOR_SIMD16(32)
#undef PA_PATCH_LIST_TERMINATOR_SIMD16

#endif
bool PaTriList0(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[])
{
    SetNextPaState(pa, PaTriList1, PaTriListSingle0);
    return false; // Not enough vertices to assemble 4 or 8 triangles.
}

bool PaTriList1(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[])
{
    SetNextPaState(pa, PaTriList2, PaTriListSingle0);
    return false; // Not enough vertices to assemble 8 triangles.
}

bool PaTriList2(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[])
{
#if KNOB_ARCH == KNOB_ARCH_AVX
#if USE_SIMD16_FRONTEND
    simdvector a;
    simdvector b;
    simdvector c;

    if (!pa.useAlternateOffset)
    {
        const simd16vector& a_16 = PaGetSimdVector_simd16(pa, 0, slot);
        const simd16vector& b_16 = PaGetSimdVector_simd16(pa, 1, slot);

        for (uint32_t i = 0; i < 4; i += 1)
        {
            a[i] = _simd16_extract_ps(a_16[i], 0);
            b[i] = _simd16_extract_ps(a_16[i], 1);
            c[i] = _simd16_extract_ps(b_16[i], 0);
        }
    }
    else
    {
        const simd16vector& b_16 = PaGetSimdVector_simd16(pa, 1, slot);
        const simd16vector& c_16 = PaGetSimdVector_simd16(pa, 2, slot);

        for (uint32_t i = 0; i < 4; i += 1)
        {
            a[i] = _simd16_extract_ps(b_16[i], 1);
            b[i] = _simd16_extract_ps(c_16[i], 0);
            c[i] = _simd16_extract_ps(c_16[i], 1);
        }
    }

#else
    simdvector& a = PaGetSimdVector(pa, 0, slot);
    simdvector& b = PaGetSimdVector(pa, 1, slot);
    simdvector& c = PaGetSimdVector(pa, 2, slot);

#endif
    simdscalar s;

    // Tri Pattern - provoking vertex is always v0
    //  v0 -> 0 3 6 9  12 15 18 21
    //  v1 -> 1 4 7 10 13 16 19 22
    //  v2 -> 2 5 8 11 14 17 20 23

    for (int i = 0; i < 4; ++i)
    {
        simdvector& v0 = verts[0];
        v0[i]          = _simd_blend_ps(a[i], b[i], 0x92);
        v0[i]          = _simd_blend_ps(v0[i], c[i], 0x24);
        v0[i]          = _simd_permute_ps_i(v0[i], 0x6C);
        s              = _simd_permute2f128_ps(v0[i], v0[i], 0x21);
        v0[i]          = _simd_blend_ps(v0[i], s, 0x44);

        simdvector& v1 = verts[1];
        v1[i]          = _simd_blend_ps(a[i], b[i], 0x24);
        v1[i]          = _simd_blend_ps(v1[i], c[i], 0x49);
        v1[i]          = _simd_permute_ps_i(v1[i], 0xB1);
        s              = _simd_permute2f128_ps(v1[i], v1[i], 0x21);
        v1[i]          = _simd_blend_ps(v1[i], s, 0x66);

        simdvector& v2 = verts[2];
        v2[i]          = _simd_blend_ps(a[i], b[i], 0x49);
        v2[i]          = _simd_blend_ps(v2[i], c[i], 0x92);
        v2[i]          = _simd_permute_ps_i(v2[i], 0xC6);
        s              = _simd_permute2f128_ps(v2[i], v2[i], 0x21);
        v2[i]          = _simd_blend_ps(v2[i], s, 0x22);
    }

#elif KNOB_ARCH >= KNOB_ARCH_AVX2
    const simdscalari perm0 = _simd_set_epi32(5, 2, 7, 4, 1, 6, 3, 0);
    const simdscalari perm1 = _simd_set_epi32(6, 3, 0, 5, 2, 7, 4, 1);
    const simdscalari perm2 = _simd_set_epi32(7, 4, 1, 6, 3, 0, 5, 2);

#if USE_SIMD16_FRONTEND
    simdvector a;
    simdvector b;
    simdvector c;

    if (!pa.useAlternateOffset)
    {
        const simd16vector& a_16 = PaGetSimdVector_simd16(pa, 0, slot);
        const simd16vector& b_16 = PaGetSimdVector_simd16(pa, 1, slot);

        for (uint32_t i = 0; i < 4; i += 1)
        {
            a[i] = _simd16_extract_ps(a_16[i], 0);
            b[i] = _simd16_extract_ps(a_16[i], 1);
            c[i] = _simd16_extract_ps(b_16[i], 0);
        }
    }
    else
    {
        const simd16vector& b_16 = PaGetSimdVector_simd16(pa, 1, slot);
        const simd16vector& c_16 = PaGetSimdVector_simd16(pa, 2, slot);

        for (uint32_t i = 0; i < 4; i += 1)
        {
            a[i] = _simd16_extract_ps(b_16[i], 1);
            b[i] = _simd16_extract_ps(c_16[i], 0);
            c[i] = _simd16_extract_ps(c_16[i], 1);
        }
    }

#else
    const simdvector& a = PaGetSimdVector(pa, 0, slot);
    const simdvector& b = PaGetSimdVector(pa, 1, slot);
    const simdvector& c = PaGetSimdVector(pa, 2, slot);

#endif
    //  v0 -> a0 a3 a6 b1 b4 b7 c2 c5
    //  v1 -> a1 a4 a7 b2 b5 c0 c3 c6
    //  v2 -> a2 a5 b0 b3 b6 c1 c4 c7

    simdvector& v0 = verts[0];
    simdvector& v1 = verts[1];
    simdvector& v2 = verts[2];

    // for simd x, y, z, and w
    for (int i = 0; i < 4; ++i)
    {
        simdscalar temp0 = _simd_blend_ps(_simd_blend_ps(a[i], b[i], 0x92), c[i], 0x24);
        simdscalar temp1 = _simd_blend_ps(_simd_blend_ps(a[i], b[i], 0x24), c[i], 0x49);
        simdscalar temp2 = _simd_blend_ps(_simd_blend_ps(a[i], b[i], 0x49), c[i], 0x92);

        v0[i] = _simd_permute_ps(temp0, perm0);
        v1[i] = _simd_permute_ps(temp1, perm1);
        v2[i] = _simd_permute_ps(temp2, perm2);
    }

#endif
    SetNextPaState(pa, PaTriList0, PaTriListSingle0, 0, PA_STATE_OPT::SIMD_WIDTH, true);
    return true;
}

#if ENABLE_AVX512_SIMD16
bool PaTriList0_simd16(PA_STATE_OPT& pa, uint32_t slot, simd16vector verts[])
{
    SetNextPaState_simd16(pa, PaTriList1_simd16, PaTriList1, PaTriListSingle0);
    return false; // Not enough vertices to assemble 16 triangles
}

bool PaTriList1_simd16(PA_STATE_OPT& pa, uint32_t slot, simd16vector verts[])
{
    SetNextPaState_simd16(pa, PaTriList2_simd16, PaTriList2, PaTriListSingle0);
    return false; // Not enough vertices to assemble 16 triangles
}

bool PaTriList2_simd16(PA_STATE_OPT& pa, uint32_t slot, simd16vector verts[])
{
    // clang-format off

#if KNOB_ARCH >= KNOB_ARCH_AVX2
    const simd16scalari perm0 = _simd16_set_epi32(13, 10, 7, 4, 1, 14, 11,  8, 5, 2, 15, 12,  9, 6, 3, 0);
    const simd16scalari perm1 = _simd16_set_epi32(14, 11, 8, 5, 2, 15, 12,  9, 6, 3,  0, 13, 10, 7, 4, 1);
    const simd16scalari perm2 = _simd16_set_epi32(15, 12, 9, 6, 3,  0, 13, 10, 7, 4,  1, 14, 11, 8, 5, 2);
#else // KNOB_ARCH == KNOB_ARCH_AVX
    simd16scalar perm0 = _simd16_setzero_ps();
    simd16scalar perm1 = _simd16_setzero_ps();
    simd16scalar perm2 = _simd16_setzero_ps();
#endif

    const simd16vector& a = PaGetSimdVector_simd16(pa, 0, slot);
    const simd16vector& b = PaGetSimdVector_simd16(pa, 1, slot);
    const simd16vector& c = PaGetSimdVector_simd16(pa, 2, slot);

    const simd16mask mask0 = 0x4924;
    const simd16mask mask1 = 0x2492;
    const simd16mask mask2 = 0x9249;

    //  v0 -> a0 a3 a6 a9 aC aF b2 b5 b8 bB bE c1 c4 c7 cA cD
    //  v1 -> a1 a4 a7 aA aD b0 b3 b6 b9 bC bF c2 c5 c8 cB cE
    //  v2 -> a2 a5 a8 aB aE b1 b4 b7 bA bD c0 c3 c6 c9 cC cF

    simd16vector& v0 = verts[0];
    simd16vector& v1 = verts[1];
    simd16vector& v2 = verts[2];

    // for simd16 x, y, z, and w
    for (int i = 0; i < 4; i += 1)
    {
        simd16scalar tempa = _simd16_loadu_ps(reinterpret_cast<const float*>(&a[i]));
        simd16scalar tempb = _simd16_loadu_ps(reinterpret_cast<const float*>(&b[i]));
        simd16scalar tempc = _simd16_loadu_ps(reinterpret_cast<const float*>(&c[i]));

        simd16scalar temp0 = _simd16_blend_ps(_simd16_blend_ps(tempa, tempb, mask0), tempc, mask1);
        simd16scalar temp1 = _simd16_blend_ps(_simd16_blend_ps(tempa, tempb, mask2), tempc, mask0);
        simd16scalar temp2 = _simd16_blend_ps(_simd16_blend_ps(tempa, tempb, mask1), tempc, mask2);

#if KNOB_ARCH >= KNOB_ARCH_AVX2
        v0[i] = _simd16_permute_ps(temp0, perm0);
        v1[i] = _simd16_permute_ps(temp1, perm1);
        v2[i] = _simd16_permute_ps(temp2, perm2);
#else // #if KNOB_ARCH == KNOB_ARCH_AVX

        // the general permutes (above) are prohibitively slow to emulate on AVX (its scalar code)

        temp0 = _simd16_permute_ps_i(temp0, 0x6C);           // (0, 3, 2, 1) => 00 11 01 10 => 0x6C
        perm0 = _simd16_permute2f128_ps(temp0, temp0, 0xB1); // (1, 0, 3, 2) => 01 00 11 10 => 0xB1
        temp0 = _simd16_blend_ps(temp0, perm0, 0x4444);      // 0010 0010 0010 0010
        perm0 = _simd16_permute2f128_ps(temp0, temp0, 0x4E); // (2, 3, 0, 1) => 10 11 00 01 => 0x4E
        v0[i] = _simd16_blend_ps(temp0, perm0, 0x3838);      // 0001 1100 0001 1100

        temp1 = _simd16_permute_ps_i(temp1, 0xB1);           // (1, 0, 3, 2) => 01 00 11 10 => 0xB1
        perm1 = _simd16_permute2f128_ps(temp1, temp1, 0xB1); // (1, 0, 3, 2) => 01 00 11 10 => 0xB1
        temp1 = _simd16_blend_ps(temp1, perm1, 0x6666);      // 0010 0010 0010 0010
        perm1 = _simd16_permute2f128_ps(temp1, temp1, 0x4E); // (2, 3, 0, 1) => 10 11 00 01 => 0x4E
        v1[i] = _simd16_blend_ps(temp1, perm1, 0x1818);      // 0001 1000 0001 1000

        temp2 = _simd16_permute_ps_i(temp2, 0xC6);           // (2, 1, 0, 3) => 01 10 00 11 => 0xC6
        perm2 = _simd16_permute2f128_ps(temp2, temp2, 0xB1); // (1, 0, 3, 2) => 01 00 11 10 => 0xB1
        temp2 = _simd16_blend_ps(temp2, perm2, 0x2222);      // 0100 0100 0100 0100
        perm2 = _simd16_permute2f128_ps(temp2, temp2, 0x4E); // (2, 3, 0, 1) => 10 11 00 01 => 0x4E
        v2[i] = _simd16_blend_ps(temp2, perm2, 0x1C1C);      // 0011 1000 0011 1000
#endif
    }

    SetNextPaState_simd16(pa, PaTriList0_simd16, PaTriList0, PaTriListSingle0, 0, PA_STATE_OPT::SIMD_WIDTH, true);
    return true;

    // clang-format on
}

#endif
void PaTriListSingle0(PA_STATE_OPT& pa, uint32_t slot, uint32_t primIndex, simd4scalar verts[])
{
#if USE_SIMD16_FRONTEND
    const simd16vector& a = PaGetSimdVector_simd16(pa, 0, slot);
    const simd16vector& b = PaGetSimdVector_simd16(pa, 1, slot);
    const simd16vector& c = PaGetSimdVector_simd16(pa, 2, slot);

    if (pa.useAlternateOffset)
    {
        primIndex += KNOB_SIMD_WIDTH;
    }

    //  v0 -> a0 a3 a6 a9 aC aF b2 b5 b8 bB bE c1 c4 c7 cA cD
    //  v1 -> a1 a4 a7 aA aD b0 b3 b6 b9 bC bF c2 c5 c8 cB cE
    //  v2 -> a2 a5 a8 aB aE b1 b4 b7 bA bD c0 c3 c6 c9 cC cF

    switch (primIndex)
    {
    case 0:
        verts[0] = swizzleLane0(a);
        verts[1] = swizzleLane1(a);
        verts[2] = swizzleLane2(a);
        break;
    case 1:
        verts[0] = swizzleLane3(a);
        verts[1] = swizzleLane4(a);
        verts[2] = swizzleLane5(a);
        break;
    case 2:
        verts[0] = swizzleLane6(a);
        verts[1] = swizzleLane7(a);
        verts[2] = swizzleLane8(a);
        break;
    case 3:
        verts[0] = swizzleLane9(a);
        verts[1] = swizzleLaneA(a);
        verts[2] = swizzleLaneB(a);
        break;
    case 4:
        verts[0] = swizzleLaneC(a);
        verts[1] = swizzleLaneD(a);
        verts[2] = swizzleLaneE(a);
        break;
    case 5:
        verts[0] = swizzleLaneF(a);
        verts[1] = swizzleLane0(b);
        verts[2] = swizzleLane1(b);
        break;
    case 6:
        verts[0] = swizzleLane2(b);
        verts[1] = swizzleLane3(b);
        verts[2] = swizzleLane4(b);
        break;
    case 7:
        verts[0] = swizzleLane5(b);
        verts[1] = swizzleLane6(b);
        verts[2] = swizzleLane7(b);
        break;
    case 8:
        verts[0] = swizzleLane8(b);
        verts[1] = swizzleLane9(b);
        verts[2] = swizzleLaneA(b);
        break;
    case 9:
        verts[0] = swizzleLaneB(b);
        verts[1] = swizzleLaneC(b);
        verts[2] = swizzleLaneD(b);
        break;
    case 10:
        verts[0] = swizzleLaneE(b);
        verts[1] = swizzleLaneF(b);
        verts[2] = swizzleLane0(c);
        break;
    case 11:
        verts[0] = swizzleLane1(c);
        verts[1] = swizzleLane2(c);
        verts[2] = swizzleLane3(c);
        break;
    case 12:
        verts[0] = swizzleLane4(c);
        verts[1] = swizzleLane5(c);
        verts[2] = swizzleLane6(c);
        break;
    case 13:
        verts[0] = swizzleLane7(c);
        verts[1] = swizzleLane8(c);
        verts[2] = swizzleLane9(c);
        break;
    case 14:
        verts[0] = swizzleLaneA(c);
        verts[1] = swizzleLaneB(c);
        verts[2] = swizzleLaneC(c);
        break;
    case 15:
        verts[0] = swizzleLaneD(c);
        verts[1] = swizzleLaneE(c);
        verts[2] = swizzleLaneF(c);
        break;
    };
#else
    // We have 12 simdscalars contained within 3 simdvectors which
    // hold at least 8 triangles worth of data. We want to assemble a single
    // triangle with data in horizontal form.

    const simdvector& a = PaGetSimdVector(pa, 0, slot);
    const simdvector& b = PaGetSimdVector(pa, 1, slot);
    const simdvector& c = PaGetSimdVector(pa, 2, slot);

    // Convert from vertical to horizontal.
    // Tri Pattern - provoking vertex is always v0
    //  v0 -> 0 3 6 9  12 15 18 21
    //  v1 -> 1 4 7 10 13 16 19 22
    //  v2 -> 2 5 8 11 14 17 20 23

    switch (primIndex)
    {
    case 0:
        verts[0] = swizzleLane0(a);
        verts[1] = swizzleLane1(a);
        verts[2] = swizzleLane2(a);
        break;
    case 1:
        verts[0] = swizzleLane3(a);
        verts[1] = swizzleLane4(a);
        verts[2] = swizzleLane5(a);
        break;
    case 2:
        verts[0] = swizzleLane6(a);
        verts[1] = swizzleLane7(a);
        verts[2] = swizzleLane0(b);
        break;
    case 3:
        verts[0] = swizzleLane1(b);
        verts[1] = swizzleLane2(b);
        verts[2] = swizzleLane3(b);
        break;
    case 4:
        verts[0] = swizzleLane4(b);
        verts[1] = swizzleLane5(b);
        verts[2] = swizzleLane6(b);
        break;
    case 5:
        verts[0] = swizzleLane7(b);
        verts[1] = swizzleLane0(c);
        verts[2] = swizzleLane1(c);
        break;
    case 6:
        verts[0] = swizzleLane2(c);
        verts[1] = swizzleLane3(c);
        verts[2] = swizzleLane4(c);
        break;
    case 7:
        verts[0] = swizzleLane5(c);
        verts[1] = swizzleLane6(c);
        verts[2] = swizzleLane7(c);
        break;
    };
#endif
}

bool PaTriStrip0(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[])
{
    SetNextPaState(pa, PaTriStrip1, PaTriStripSingle0);
    return false; // Not enough vertices to assemble 8 triangles.
}

bool PaTriStrip1(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[])
{
#if USE_SIMD16_FRONTEND
    simdvector a;
    simdvector b;

    if (!pa.useAlternateOffset)
    {
        const simd16vector& a_16 = PaGetSimdVector_simd16(pa, pa.prev, slot);

        for (uint32_t i = 0; i < 4; i += 1)
        {
            a[i] = _simd16_extract_ps(a_16[i], 0);
            b[i] = _simd16_extract_ps(a_16[i], 1);
        }
    }
    else
    {
        const simd16vector& b_16 = PaGetSimdVector_simd16(pa, pa.cur, slot);

        for (uint32_t i = 0; i < 4; i += 1)
        {
            a[i] = _simd16_extract_ps(b_16[i], 0);
            b[i] = _simd16_extract_ps(b_16[i], 1);
        }
    }

#else
    simdvector& a = PaGetSimdVector(pa, pa.prev, slot);
    simdvector& b = PaGetSimdVector(pa, pa.cur, slot);

#endif
    simdscalar s;

    for (int i = 0; i < 4; ++i)
    {
        simdscalar a0 = a[i];
        simdscalar b0 = b[i];

        // Tri Pattern - provoking vertex is always v0
        //  v0 -> 01234567
        //  v1 -> 13355779
        //  v2 -> 22446688
        simdvector& v0 = verts[0];
        v0[i]          = a0;

        //  s -> 4567891011
        s = _simd_permute2f128_ps(a0, b0, 0x21);
        //  s -> 23456789
        s = _simd_shuffle_ps(a0, s, _MM_SHUFFLE(1, 0, 3, 2));

        simdvector& v1 = verts[1];
        //  v1 -> 13355779
        v1[i] = _simd_shuffle_ps(a0, s, _MM_SHUFFLE(3, 1, 3, 1));

        simdvector& v2 = verts[2];
        //  v2 -> 22446688
        v2[i] = _simd_shuffle_ps(a0, s, _MM_SHUFFLE(2, 2, 2, 2));
    }

    SetNextPaState(pa, PaTriStrip1, PaTriStripSingle0, 0, PA_STATE_OPT::SIMD_WIDTH);
    return true;
}

#if ENABLE_AVX512_SIMD16
bool PaTriStrip0_simd16(PA_STATE_OPT& pa, uint32_t slot, simd16vector verts[])
{
    SetNextPaState_simd16(pa, PaTriStrip1_simd16, PaTriStrip1, PaTriStripSingle0);
    return false; // Not enough vertices to assemble 16 triangles.
}

bool PaTriStrip1_simd16(PA_STATE_OPT& pa, uint32_t slot, simd16vector verts[])
{
    // clang-format off

    const simd16vector& a = PaGetSimdVector_simd16(pa, pa.prev, slot);
    const simd16vector& b = PaGetSimdVector_simd16(pa, pa.cur, slot);

    const simd16mask mask0 = 0xF000;

    //  v0 -> a0 a1 a2 a3 a4 a5 a6 a7 a8 a9 aA aB aC aD aE aF
    //  v1 -> a1 a3 a3 a5 a5 a7 a7 a9 a9 aB aB aD aD aF aF b1
    //  v2 -> a2 a2 a4 a4 a6 a6 a8 a8 aA aA aC aC aE aE b0 b0

    simd16vector& v0 = verts[0];
    simd16vector& v1 = verts[1];
    simd16vector& v2 = verts[2];

    // for simd16 x, y, z, and w
    for (int i = 0; i < 4; i += 1)
    {
        simd16scalar tempa = _simd16_loadu_ps(reinterpret_cast<const float*>(&a[i]));
        simd16scalar tempb = _simd16_loadu_ps(reinterpret_cast<const float*>(&b[i]));

        simd16scalar perm0 = _simd16_permute2f128_ps(tempa, tempa, 0x39); // (0 3 2 1) = 00 11 10 01 // a4 a5 a6 a7 a8 a9 aA aB aC aD aE aF a0 a1 a2 a3
        simd16scalar perm1 = _simd16_permute2f128_ps(tempb, tempb, 0x39); // (0 3 2 1) = 00 11 10 01 // b4 b5 b6 b7 b8 b9 bA bB bC bD bE bF b0 b1 b2 b3

        simd16scalar blend = _simd16_blend_ps(perm0, perm1, mask0);                                  // a4 a5 a6 a7 a8 a9 aA aB aC aD aE aF b0 b1 b2 b3
        simd16scalar shuff = _simd16_shuffle_ps(tempa, blend, _MM_SHUFFLE(1, 0, 3, 2));              // a2 a3 a4 a5 a6 a7 a8 a9 aA aB aC aD aE aF b0 b1

        v0[i] = tempa;                                                                               // a0 a1 a2 a3 a4 a5 a6 a7 a8 a9 aA aB aC aD aE aF
        v1[i] = _simd16_shuffle_ps(tempa, shuff, _MM_SHUFFLE(3, 1, 3, 1));                           // a1 a3 a3 a5 a5 a7 a7 a9 a9 aB aB aD aD aF aF b1
        v2[i] = _simd16_shuffle_ps(tempa, shuff, _MM_SHUFFLE(2, 2, 2, 2));                           // a2 a2 a4 a4 a6 a6 a8 a8 aA aA aC aC aE aE b0 b0
    }

    SetNextPaState_simd16(pa, PaTriStrip1_simd16, PaTriStrip1, PaTriStripSingle0, 0, PA_STATE_OPT::SIMD_WIDTH);
    return true;

    // clang-format on
}

#endif
void PaTriStripSingle0(PA_STATE_OPT& pa, uint32_t slot, uint32_t primIndex, simd4scalar verts[])
{
#if USE_SIMD16_FRONTEND
    const simd16vector& a = PaGetSimdVector_simd16(pa, pa.prev, slot);
    const simd16vector& b = PaGetSimdVector_simd16(pa, pa.cur, slot);

    if (pa.useAlternateOffset)
    {
        primIndex += KNOB_SIMD_WIDTH;
    }

    //  v0 -> a0 a1 a2 a3 a4 a5 a6 a7 a8 a9 aA aB aC aD aE aF
    //  v1 -> a1 a3 a3 a5 a5 a7 a7 a9 a9 aB aB aD aD aF aF b1
    //  v2 -> a2 a2 a4 a4 a6 a6 a8 a8 aA aA aC aC aE aE b0 b0

    switch (primIndex)
    {
    case 0:
        verts[0] = swizzleLane0(a);
        verts[1] = swizzleLane1(a);
        verts[2] = swizzleLane2(a);
        break;
    case 1:
        verts[0] = swizzleLane1(a);
        verts[1] = swizzleLane3(a);
        verts[2] = swizzleLane2(a);
        break;
    case 2:
        verts[0] = swizzleLane2(a);
        verts[1] = swizzleLane3(a);
        verts[2] = swizzleLane4(a);
        break;
    case 3:
        verts[0] = swizzleLane3(a);
        verts[1] = swizzleLane5(a);
        verts[2] = swizzleLane4(a);
        break;
    case 4:
        verts[0] = swizzleLane4(a);
        verts[1] = swizzleLane5(a);
        verts[2] = swizzleLane6(a);
        break;
    case 5:
        verts[0] = swizzleLane5(a);
        verts[1] = swizzleLane7(a);
        verts[2] = swizzleLane6(a);
        break;
    case 6:
        verts[0] = swizzleLane6(a);
        verts[1] = swizzleLane7(a);
        verts[2] = swizzleLane8(a);
        break;
    case 7:
        verts[0] = swizzleLane7(a);
        verts[1] = swizzleLane9(a);
        verts[2] = swizzleLane8(a);
        break;
    case 8:
        verts[0] = swizzleLane8(a);
        verts[1] = swizzleLane9(a);
        verts[2] = swizzleLaneA(a);
        break;
    case 9:
        verts[0] = swizzleLane9(a);
        verts[1] = swizzleLaneB(a);
        verts[2] = swizzleLaneA(a);
        break;
    case 10:
        verts[0] = swizzleLaneA(a);
        verts[1] = swizzleLaneB(a);
        verts[2] = swizzleLaneC(a);
        break;
    case 11:
        verts[0] = swizzleLaneB(a);
        verts[1] = swizzleLaneD(a);
        verts[2] = swizzleLaneC(a);
        break;
    case 12:
        verts[0] = swizzleLaneC(a);
        verts[1] = swizzleLaneD(a);
        verts[2] = swizzleLaneE(a);
        break;
    case 13:
        verts[0] = swizzleLaneD(a);
        verts[1] = swizzleLaneF(a);
        verts[2] = swizzleLaneE(a);
        break;
    case 14:
        verts[0] = swizzleLaneE(a);
        verts[1] = swizzleLaneF(a);
        verts[2] = swizzleLane0(b);
        break;
    case 15:
        verts[0] = swizzleLaneF(a);
        verts[1] = swizzleLane1(b);
        verts[2] = swizzleLane0(b);
        break;
    };
#else
    const simdvector& a = PaGetSimdVector(pa, pa.prev, slot);
    const simdvector& b = PaGetSimdVector(pa, pa.cur, slot);

    // Convert from vertical to horizontal.
    // Tri Pattern - provoking vertex is always v0
    //  v0 -> 01234567
    //  v1 -> 13355779
    //  v2 -> 22446688

    switch (primIndex)
    {
    case 0:
        verts[0] = swizzleLane0(a);
        verts[1] = swizzleLane1(a);
        verts[2] = swizzleLane2(a);
        break;
    case 1:
        verts[0] = swizzleLane1(a);
        verts[1] = swizzleLane3(a);
        verts[2] = swizzleLane2(a);
        break;
    case 2:
        verts[0] = swizzleLane2(a);
        verts[1] = swizzleLane3(a);
        verts[2] = swizzleLane4(a);
        break;
    case 3:
        verts[0] = swizzleLane3(a);
        verts[1] = swizzleLane5(a);
        verts[2] = swizzleLane4(a);
        break;
    case 4:
        verts[0] = swizzleLane4(a);
        verts[1] = swizzleLane5(a);
        verts[2] = swizzleLane6(a);
        break;
    case 5:
        verts[0] = swizzleLane5(a);
        verts[1] = swizzleLane7(a);
        verts[2] = swizzleLane6(a);
        break;
    case 6:
        verts[0] = swizzleLane6(a);
        verts[1] = swizzleLane7(a);
        verts[2] = swizzleLane0(b);
        break;
    case 7:
        verts[0] = swizzleLane7(a);
        verts[1] = swizzleLane1(b);
        verts[2] = swizzleLane0(b);
        break;
    };
#endif
}

bool PaTriFan0(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[])
{
    SetNextPaState(pa, PaTriFan1, PaTriFanSingle0);
    return false; // Not enough vertices to assemble 8 triangles.
}

bool PaTriFan1(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[])
{
#if USE_SIMD16_FRONTEND
    simdvector leadVert;
    simdvector a;
    simdvector b;

    const simd16vector& leadvert_16 = PaGetSimdVector_simd16(pa, pa.first, slot);

    if (!pa.useAlternateOffset)
    {
        const simd16vector& a_16 = PaGetSimdVector_simd16(pa, pa.prev, slot);

        for (uint32_t i = 0; i < 4; i += 1)
        {
            leadVert[i] = _simd16_extract_ps(leadvert_16[i], 0);

            a[i] = _simd16_extract_ps(a_16[i], 0);
            b[i] = _simd16_extract_ps(a_16[i], 1);
        }
    }
    else
    {
        const simd16vector& b_16 = PaGetSimdVector_simd16(pa, pa.cur, slot);

        for (uint32_t i = 0; i < 4; i += 1)
        {
            leadVert[i] = _simd16_extract_ps(leadvert_16[i], 0);

            a[i] = _simd16_extract_ps(b_16[i], 0);
            b[i] = _simd16_extract_ps(b_16[i], 1);
        }
    }

#else
    const simdvector& leadVert = PaGetSimdVector(pa, pa.first, slot);
    const simdvector& a = PaGetSimdVector(pa, pa.prev, slot);
    const simdvector& b = PaGetSimdVector(pa, pa.cur, slot);

#endif
    simdscalar s;

    // need to fill vectors 1/2 with new verts, and v0 with anchor vert.
    for (int i = 0; i < 4; ++i)
    {
        simdscalar a0 = a[i];
        simdscalar b0 = b[i];

        simdscalar comp = leadVert[i];

        simdvector& v0 = verts[0];
        v0[i]          = _simd_shuffle_ps(comp, comp, _MM_SHUFFLE(0, 0, 0, 0));
        v0[i]          = _simd_permute2f128_ps(v0[i], comp, 0x00);

        simdvector& v2 = verts[2];
        s              = _simd_permute2f128_ps(a0, b0, 0x21);
        v2[i]          = _simd_shuffle_ps(a0, s, _MM_SHUFFLE(1, 0, 3, 2));

        simdvector& v1 = verts[1];
        v1[i]          = _simd_shuffle_ps(a0, v2[i], _MM_SHUFFLE(2, 1, 2, 1));
    }

    SetNextPaState(pa, PaTriFan1, PaTriFanSingle0, 0, PA_STATE_OPT::SIMD_WIDTH);
    return true;
}

#if ENABLE_AVX512_SIMD16
bool PaTriFan0_simd16(PA_STATE_OPT& pa, uint32_t slot, simd16vector verts[])
{
    SetNextPaState_simd16(pa, PaTriFan1_simd16, PaTriFan1, PaTriFanSingle0);
    return false; // Not enough vertices to assemble 16 triangles.
}

bool PaTriFan1_simd16(PA_STATE_OPT& pa, uint32_t slot, simd16vector verts[])
{
    // clang-format off

    const simd16vector& a = PaGetSimdVector_simd16(pa, pa.first, slot);
    const simd16vector& b = PaGetSimdVector_simd16(pa, pa.prev, slot);
    const simd16vector& c = PaGetSimdVector_simd16(pa, pa.cur, slot);

    const simd16mask mask0 = 0xF000;

    //  v0 -> a0 a0 a0 a0 a0 a0 a0 a0 a0 a0 a0 a0 a0 a0 a0 a0
    //  v1 -> b1 b2 b3 b4 b5 b6 b7 b8 b9 bA bB bC bD bE bF c0
    //  v2 -> b2 b3 b4 b5 b6 b7 b8 b9 bA bB bC bD bE bF c0 c1

    simd16vector& v0 = verts[0];
    simd16vector& v1 = verts[1];
    simd16vector& v2 = verts[2];

    // for simd16 x, y, z, and w
    for (uint32_t i = 0; i < 4; i += 1)
    {
        simd16scalar tempa = _simd16_loadu_ps(reinterpret_cast<const float*>(&a[i]));
        simd16scalar tempb = _simd16_loadu_ps(reinterpret_cast<const float*>(&b[i]));
        simd16scalar tempc = _simd16_loadu_ps(reinterpret_cast<const float*>(&c[i]));

        simd16scalar shuff = _simd16_shuffle_ps(tempa, tempa, _MM_SHUFFLE(0, 0, 0, 0));              // a0 a0 a0 a0 a4 a4 a4 a4 a0 a0 a0 a0 a4 a4 a4 a4

        v0[i] = _simd16_permute2f128_ps(shuff, shuff, 0x00);                                         // a0 a0 a0 a0 a0 a0 a0 a0 a0 a0 a0 a0 a0 a0 a0 a0

        simd16scalar temp0 = _simd16_permute2f128_ps(tempb, tempb, 0x39); // (0 3 2 1) = 00 11 10 01 // b4 b5 b6 b7 b8 b9 bA bB bC bD bE bF b0 b1 b2 b3
        simd16scalar temp1 = _simd16_permute2f128_ps(tempc, tempc, 0x39); // (0 3 2 1) = 00 11 10 01 // c4 c5 c6 c7 c8 c9 cA cB cC cD cE cF c0 c1 c2 c3

        simd16scalar blend = _simd16_blend_ps(temp0, temp1, mask0);                                  // b4 b5 b6 b7 b8 b9 bA bB bC bD bE bF c0 c1 c2 c3

        simd16scalar temp2 = _simd16_shuffle_ps(tempb, blend, _MM_SHUFFLE(1, 0, 3, 2));              // b2 b3 b4 b5 b6 b7 b8 b9 bA bB bC bD bE bF c0 c1

        v1[i] = _simd16_shuffle_ps(tempb, temp2, _MM_SHUFFLE(2, 1, 2, 1));                           // b1 b2 b3 b4 b5 b6 b7 b8 b9 bA bB bC bD bE bF c0
        v2[i] = temp2;                                                                               // b2 b3 b4 b5 b6 b7 b8 b9 bA bB bC bD bE bF c0 c1
    }

    SetNextPaState_simd16(pa, PaTriFan1_simd16, PaTriFan1, PaTriFanSingle0, 0, PA_STATE_OPT::SIMD_WIDTH);
    return true;

    // clang-format on
}

#endif
void PaTriFanSingle0(PA_STATE_OPT& pa, uint32_t slot, uint32_t primIndex, simd4scalar verts[])
{
#if USE_SIMD16_FRONTEND
    const simd16vector& a = PaGetSimdVector_simd16(pa, pa.first, slot);
    const simd16vector& b = PaGetSimdVector_simd16(pa, pa.prev, slot);
    const simd16vector& c = PaGetSimdVector_simd16(pa, pa.cur, slot);

    if (pa.useAlternateOffset)
    {
        primIndex += KNOB_SIMD_WIDTH;
    }

    //  v0 -> a0 a0 a0 a0 a0 a0 a0 a0 a0 a0 a0 a0 a0 a0 a0 a0
    //  v1 -> b1 b2 b3 b4 b5 b6 b7 b8 b9 bA bB bC bD bE bF c0
    //  v2 -> b2 b3 b4 b5 b6 b7 b8 b9 bA bB bC bD bE bF c0 c1

    // vert 0 from leading vertex
    verts[0] = swizzleLane0(a);

    // vert 1
    if (primIndex < 15)
    {
        verts[1] = swizzleLaneN(b, primIndex + 1);
    }
    else
    {
        verts[1] = swizzleLane0(c);
    }

    // vert 2
    if (primIndex < 14)
    {
        verts[2] = swizzleLaneN(b, primIndex + 2);
    }
    else
    {
        verts[2] = swizzleLaneN(c, primIndex - 14);
    }
#else
    const simdvector& a = PaGetSimdVector(pa, pa.first, slot);
    const simdvector& b = PaGetSimdVector(pa, pa.prev, slot);
    const simdvector& c = PaGetSimdVector(pa, pa.cur, slot);

    // vert 0 from leading vertex
    verts[0] = swizzleLane0(a);

    // vert 1
    if (primIndex < 7)
    {
        verts[1] = swizzleLaneN(b, primIndex + 1);
    }
    else
    {
        verts[1] = swizzleLane0(c);
    }

    // vert 2
    if (primIndex < 6)
    {
        verts[2] = swizzleLaneN(b, primIndex + 2);
    }
    else
    {
        verts[2] = swizzleLaneN(c, primIndex - 6);
    }
#endif
}

bool PaQuadList0(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[])
{
    SetNextPaState(pa, PaQuadList1, PaQuadListSingle0);
    return false; // Not enough vertices to assemble 8 triangles.
}

bool PaQuadList1(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[])
{
#if USE_SIMD16_FRONTEND
    simdvector a;
    simdvector b;

    if (!pa.useAlternateOffset)
    {
        const simd16vector& a_16 = PaGetSimdVector_simd16(pa, 0, slot);

        for (uint32_t i = 0; i < 4; i += 1)
        {
            a[i] = _simd16_extract_ps(a_16[i], 0);
            b[i] = _simd16_extract_ps(a_16[i], 1);
        }
    }
    else
    {
        const simd16vector& b_16 = PaGetSimdVector_simd16(pa, 1, slot);

        for (uint32_t i = 0; i < 4; i += 1)
        {
            a[i] = _simd16_extract_ps(b_16[i], 0);
            b[i] = _simd16_extract_ps(b_16[i], 1);
        }
    }

#else
    simdvector& a = PaGetSimdVector(pa, 0, slot);
    simdvector& b = PaGetSimdVector(pa, 1, slot);

#endif
    simdscalar s1, s2;

    for (int i = 0; i < 4; ++i)
    {
        simdscalar a0 = a[i];
        simdscalar b0 = b[i];

        s1 = _mm256_permute2f128_ps(a0, b0, 0x20);
        s2 = _mm256_permute2f128_ps(a0, b0, 0x31);

        simdvector& v0 = verts[0];
        v0[i]          = _simd_shuffle_ps(s1, s2, _MM_SHUFFLE(0, 0, 0, 0));

        simdvector& v1 = verts[1];
        v1[i]          = _simd_shuffle_ps(s1, s2, _MM_SHUFFLE(2, 1, 2, 1));

        simdvector& v2 = verts[2];
        v2[i]          = _simd_shuffle_ps(s1, s2, _MM_SHUFFLE(3, 2, 3, 2));
    }

    SetNextPaState(pa, PaQuadList0, PaQuadListSingle0, 0, PA_STATE_OPT::SIMD_WIDTH, true);
    return true;
}

#if ENABLE_AVX512_SIMD16
bool PaQuadList0_simd16(PA_STATE_OPT& pa, uint32_t slot, simd16vector verts[])
{
    SetNextPaState_simd16(pa, PaQuadList1_simd16, PaQuadList1, PaQuadListSingle0);
    return false; // Not enough vertices to assemble 16 triangles.
}

bool PaQuadList1_simd16(PA_STATE_OPT& pa, uint32_t slot, simd16vector verts[])
{
    // clang-format off

    const simd16vector& a = PaGetSimdVector_simd16(pa, 0, slot);
    const simd16vector& b = PaGetSimdVector_simd16(pa, 1, slot);

    //  v0 -> a0 a0 a4 a4 a8 a8 aC aC b0 b0 b0 b0 b0 b0 bC bC
    //  v1 -> a1 a2 a5 a6 a9 aA aD aE b1 b2 b5 b6 b9 bA bD bE
    //  v2 -> a2 a3 a6 a7 aA aB aE aF b2 b3 b6 b7 bA bB bE bF

    simd16vector& v0 = verts[0];
    simd16vector& v1 = verts[1];
    simd16vector& v2 = verts[2];

    // for simd16 x, y, z, and w
    for (uint32_t i = 0; i < 4; i += 1)
    {
        simd16scalar tempa = _simd16_loadu_ps(reinterpret_cast<const float*>(&a[i]));
        simd16scalar tempb = _simd16_loadu_ps(reinterpret_cast<const float*>(&b[i]));

        simd16scalar temp0 = _simd16_permute2f128_ps(tempa, tempb, 0x88); // (2 0 2 0) = 10 00 10 00 // a0 a1 a2 a3 a8 a9 aA aB b0 b1 b2 b3 b8 b9 bA bB
        simd16scalar temp1 = _simd16_permute2f128_ps(tempa, tempb, 0xDD); // (3 1 3 1) = 11 01 11 01 // a4 a5 a6 a7 aC aD aE aF b4 b5 b6 b7 bC bD bE bF

        v0[i] = _simd16_shuffle_ps(temp0, temp1, _MM_SHUFFLE(0, 0, 0, 0));                           // a0 a0 a4 a4 a8 a8 aC aC b0 b0 b4 b4 b8 b8 bC bC
        v1[i] = _simd16_shuffle_ps(temp0, temp1, _MM_SHUFFLE(2, 1, 2, 1));                           // a1 a2 a5 a6 a9 aA aD aE b1 b2 b6 b6 b9 bA bD bE
        v2[i] = _simd16_shuffle_ps(temp0, temp1, _MM_SHUFFLE(3, 2, 3, 2));                           // a2 a3 a6 a7 aA aB aE aF b2 b3 b6 b7 bA bB bE bF
    }

    SetNextPaState_simd16(pa, PaQuadList0_simd16, PaQuadList0, PaQuadListSingle0, 0, PA_STATE_OPT::SIMD_WIDTH, true);
    return true;

    // clang-format on
}

#endif
void PaQuadListSingle0(PA_STATE_OPT& pa, uint32_t slot, uint32_t primIndex, simd4scalar verts[])
{
#if USE_SIMD16_FRONTEND
    const simd16vector& a = PaGetSimdVector_simd16(pa, 0, slot);
    const simd16vector& b = PaGetSimdVector_simd16(pa, 1, slot);

    if (pa.useAlternateOffset)
    {
        primIndex += KNOB_SIMD_WIDTH;
    }

    switch (primIndex)
    {
    case 0:
        // triangle 0 - 0 1 2
        verts[0] = swizzleLane0(a);
        verts[1] = swizzleLane1(a);
        verts[2] = swizzleLane2(a);
        break;
    case 1:
        // triangle 1 - 0 2 3
        verts[0] = swizzleLane0(a);
        verts[1] = swizzleLane2(a);
        verts[2] = swizzleLane3(a);
        break;
    case 2:
        // triangle 2 - 4 5 6
        verts[0] = swizzleLane4(a);
        verts[1] = swizzleLane5(a);
        verts[2] = swizzleLane6(a);
        break;
    case 3:
        // triangle 3 - 4 6 7
        verts[0] = swizzleLane4(a);
        verts[1] = swizzleLane6(a);
        verts[2] = swizzleLane7(a);
        break;
    case 4:
        // triangle 4 - 8 9 A
        verts[0] = swizzleLane8(a);
        verts[1] = swizzleLane9(a);
        verts[2] = swizzleLaneA(a);
        break;
    case 5:
        // triangle 5 - 8 A B
        verts[0] = swizzleLane8(a);
        verts[1] = swizzleLaneA(a);
        verts[2] = swizzleLaneB(a);
        break;
    case 6:
        // triangle 6 - C D E
        verts[0] = swizzleLaneC(a);
        verts[1] = swizzleLaneD(a);
        verts[2] = swizzleLaneE(a);
        break;
    case 7:
        // triangle 7 - C E F
        verts[0] = swizzleLaneC(a);
        verts[1] = swizzleLaneE(a);
        verts[2] = swizzleLaneF(a);
        break;
    case 8:
        // triangle 0 - 0 1 2
        verts[0] = swizzleLane0(b);
        verts[1] = swizzleLane1(b);
        verts[2] = swizzleLane2(b);
        break;
    case 9:
        // triangle 1 - 0 2 3
        verts[0] = swizzleLane0(b);
        verts[1] = swizzleLane2(b);
        verts[2] = swizzleLane3(b);
        break;
    case 10:
        // triangle 2 - 4 5 6
        verts[0] = swizzleLane4(b);
        verts[1] = swizzleLane5(b);
        verts[2] = swizzleLane6(b);
        break;
    case 11:
        // triangle 3 - 4 6 7
        verts[0] = swizzleLane4(b);
        verts[1] = swizzleLane6(b);
        verts[2] = swizzleLane7(b);
        break;
    case 12:
        // triangle 4 - 8 9 A
        verts[0] = swizzleLane8(b);
        verts[1] = swizzleLane9(b);
        verts[2] = swizzleLaneA(b);
        break;
    case 13:
        // triangle 5 - 8 A B
        verts[0] = swizzleLane8(b);
        verts[1] = swizzleLaneA(b);
        verts[2] = swizzleLaneB(b);
        break;
    case 14:
        // triangle 6 - C D E
        verts[0] = swizzleLaneC(b);
        verts[1] = swizzleLaneD(b);
        verts[2] = swizzleLaneE(b);
        break;
    case 15:
        // triangle 7 - C E F
        verts[0] = swizzleLaneC(b);
        verts[1] = swizzleLaneE(b);
        verts[2] = swizzleLaneF(b);
        break;
    }
#else
    const simdvector& a = PaGetSimdVector(pa, 0, slot);
    const simdvector& b = PaGetSimdVector(pa, 1, slot);

    switch (primIndex)
    {
    case 0:
        // triangle 0 - 0 1 2
        verts[0] = swizzleLane0(a);
        verts[1] = swizzleLane1(a);
        verts[2] = swizzleLane2(a);
        break;
    case 1:
        // triangle 1 - 0 2 3
        verts[0] = swizzleLane0(a);
        verts[1] = swizzleLane2(a);
        verts[2] = swizzleLane3(a);
        break;
    case 2:
        // triangle 2 - 4 5 6
        verts[0] = swizzleLane4(a);
        verts[1] = swizzleLane5(a);
        verts[2] = swizzleLane6(a);
        break;
    case 3:
        // triangle 3 - 4 6 7
        verts[0] = swizzleLane4(a);
        verts[1] = swizzleLane6(a);
        verts[2] = swizzleLane7(a);
        break;
    case 4:
        // triangle 4 - 8 9 10 (0 1 2)
        verts[0] = swizzleLane0(b);
        verts[1] = swizzleLane1(b);
        verts[2] = swizzleLane2(b);
        break;
    case 5:
        // triangle 1 - 0 2 3
        verts[0] = swizzleLane0(b);
        verts[1] = swizzleLane2(b);
        verts[2] = swizzleLane3(b);
        break;
    case 6:
        // triangle 2 - 4 5 6
        verts[0] = swizzleLane4(b);
        verts[1] = swizzleLane5(b);
        verts[2] = swizzleLane6(b);
        break;
    case 7:
        // triangle 3 - 4 6 7
        verts[0] = swizzleLane4(b);
        verts[1] = swizzleLane6(b);
        verts[2] = swizzleLane7(b);
        break;
    }
#endif
}

bool PaLineLoop0(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[])
{
    SetNextPaState(pa, PaLineLoop1, PaLineLoopSingle0);
    return false;
}

bool PaLineLoop1(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[])
{
    PaLineStrip1(pa, slot, verts);

    if (pa.numPrimsComplete + KNOB_SIMD_WIDTH > pa.numPrims - 1)
    {
        // loop reconnect now
        const int lane = pa.numPrims - pa.numPrimsComplete - 1;

#if USE_SIMD16_FRONTEND
        simdvector first;

        const simd16vector& first_16 = PaGetSimdVector_simd16(pa, pa.first, slot);

        if (!pa.useAlternateOffset)
        {
            for (uint32_t i = 0; i < 4; i += 1)
            {
                first[i] = _simd16_extract_ps(first_16[i], 0);
            }
        }
        else
        {
            for (uint32_t i = 0; i < 4; i += 1)
            {
                first[i] = _simd16_extract_ps(first_16[i], 1);
            }
        }

#else
        simdvector& first = PaGetSimdVector(pa, pa.first, slot);

#endif
        for (int i = 0; i < 4; i++)
        {
            float* firstVtx  = (float*)&(first[i]);
            float* targetVtx = (float*)&(verts[1][i]);
            targetVtx[lane]  = firstVtx[0];
        }
    }

    SetNextPaState(pa, PaLineLoop1, PaLineLoopSingle0, 0, PA_STATE_OPT::SIMD_WIDTH);
    return true;
}

#if ENABLE_AVX512_SIMD16
bool PaLineLoop0_simd16(PA_STATE_OPT& pa, uint32_t slot, simd16vector verts[])
{
    SetNextPaState_simd16(pa, PaLineLoop1_simd16, PaLineLoop1, PaLineLoopSingle0);
    return false;
}

bool PaLineLoop1_simd16(PA_STATE_OPT& pa, uint32_t slot, simd16vector verts[])
{
    PaLineStrip1_simd16(pa, slot, verts);

    if (pa.numPrimsComplete + KNOB_SIMD16_WIDTH > pa.numPrims - 1)
    {
        // loop reconnect now
        const int lane = pa.numPrims - pa.numPrimsComplete - 1;

        const simd16vector& first = PaGetSimdVector_simd16(pa, pa.first, slot);

        for (int i = 0; i < 4; i++)
        {
            float* firstVtx  = (float*)&(first[i]);
            float* targetVtx = (float*)&(verts[1][i]);
            targetVtx[lane]  = firstVtx[0];
        }
    }

    SetNextPaState_simd16(
        pa, PaLineLoop1_simd16, PaLineLoop1, PaLineLoopSingle0, 0, PA_STATE_OPT::SIMD_WIDTH);
    return true;
}

#endif
void PaLineLoopSingle0(PA_STATE_OPT& pa, uint32_t slot, uint32_t primIndex, simd4scalar verts[])
{
    PaLineStripSingle0(pa, slot, primIndex, verts);

    if (pa.numPrimsComplete + primIndex == pa.numPrims - 1)
    {
#if USE_SIMD16_FRONTEND
        const simd16vector& first = PaGetSimdVector_simd16(pa, pa.first, slot);

        verts[1] = swizzleLane0(first);
#else
        const simdvector& first = PaGetSimdVector(pa, pa.first, slot);

        verts[1] = swizzleLane0(first);
#endif
    }
}

bool PaLineList0(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[])
{
    SetNextPaState(pa, PaLineList1, PaLineListSingle0);
    return false; // Not enough vertices to assemble 8 lines
}

bool PaLineList1(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[])
{
#if USE_SIMD16_FRONTEND
    simdvector a;
    simdvector b;

    if (!pa.useAlternateOffset)
    {
        const simd16vector& a_16 = PaGetSimdVector_simd16(pa, 0, slot);

        for (uint32_t i = 0; i < 4; i += 1)
        {
            a[i] = _simd16_extract_ps(a_16[i], 0);
            b[i] = _simd16_extract_ps(a_16[i], 1);
        }
    }
    else
    {
        const simd16vector& b_16 = PaGetSimdVector_simd16(pa, 1, slot);

        for (uint32_t i = 0; i < 4; i += 1)
        {
            a[i] = _simd16_extract_ps(b_16[i], 0);
            b[i] = _simd16_extract_ps(b_16[i], 1);
        }
    }

#else
    simdvector& a = PaGetSimdVector(pa, 0, slot);
    simdvector& b = PaGetSimdVector(pa, 1, slot);

#endif
    /// @todo: verify provoking vertex is correct
    // Line list 0  1  2  3  4  5  6  7
    //           8  9 10 11 12 13 14 15

    // shuffle:
    //           0 2 4 6 8 10 12 14
    //           1 3 5 7 9 11 13 15

    for (uint32_t i = 0; i < 4; ++i)
    {
        // 0 1 2 3 8 9 10 11
        __m256 vALowBLow = _mm256_permute2f128_ps(a.v[i], b.v[i], 0x20);
        // 4 5 6 7 12 13 14 15
        __m256 vAHighBHigh = _mm256_permute2f128_ps(a.v[i], b.v[i], 0x31);

        // 0 2 4 6 8 10 12 14
        verts[0].v[i] = _mm256_shuffle_ps(vALowBLow, vAHighBHigh, _MM_SHUFFLE(2, 0, 2, 0));
        // 1 3 5 7 9 11 13 15
        verts[1].v[i] = _mm256_shuffle_ps(vALowBLow, vAHighBHigh, _MM_SHUFFLE(3, 1, 3, 1));
    }

    SetNextPaState(pa, PaLineList0, PaLineListSingle0, 0, PA_STATE_OPT::SIMD_WIDTH, true);
    return true;
}

#if ENABLE_AVX512_SIMD16
bool PaLineList0_simd16(PA_STATE_OPT& pa, uint32_t slot, simd16vector verts[])
{
    SetNextPaState_simd16(pa, PaLineList1_simd16, PaLineList1, PaLineListSingle0);
    return false; // Not enough vertices to assemble 16 lines
}

bool PaLineList1_simd16(PA_STATE_OPT& pa, uint32_t slot, simd16vector verts[])
{
    // clang-format off

    const simd16vector& a = PaGetSimdVector_simd16(pa, 0, slot);
    const simd16vector& b = PaGetSimdVector_simd16(pa, 1, slot);

    // v0 -> a0 a2 a4 a6 a8 aA aC aE b0 b2 b4 b6 b8 bA bC bE
    // v1 -> a1 a3 a5 a7 a9 aB aD aF b1 b3 b4 b7 b9 bB bD bF

    simd16vector& v0 = verts[0];
    simd16vector& v1 = verts[1];

    // for simd16 x, y, z, and w
    for (int i = 0; i < 4; i += 1)
    {
        simd16scalar tempa = _simd16_loadu_ps(reinterpret_cast<const float*>(&a[i]));
        simd16scalar tempb = _simd16_loadu_ps(reinterpret_cast<const float*>(&b[i]));

        simd16scalar temp0 = _simd16_permute2f128_ps(tempa, tempb, 0x88); // (2 0 2 0) 10 00 10 00   // a0 a1 a2 a3 a8 a9 aA aB b0 b1 b2 b3 b9 b9 bA bB
        simd16scalar temp1 = _simd16_permute2f128_ps(tempa, tempb, 0xDD); // (3 1 3 1) 11 01 11 01   // a4 a5 a6 a7 aC aD aE aF b4 b5 b6 b7 bC bD bE bF

        v0[i] = _simd16_shuffle_ps(temp0, temp1, _MM_SHUFFLE(2, 0, 2, 0));                           // a0 a2 a4 a6 a8 aA aC aE b0 b2 b4 b6 b8 bA bC bE
        v1[i] = _simd16_shuffle_ps(temp0, temp1, _MM_SHUFFLE(3, 1, 3, 1));                           // a1 a3 a5 a7 a9 aB aD aF b1 b3 b5 b7 b9 bB bD bF
    }

    SetNextPaState_simd16(pa, PaLineList0_simd16, PaLineList0, PaLineListSingle0, 0, PA_STATE_OPT::SIMD_WIDTH, true);
    return true;

    // clang-format on
}

#endif
void PaLineListSingle0(PA_STATE_OPT& pa, uint32_t slot, uint32_t primIndex, simd4scalar verts[])
{
#if USE_SIMD16_FRONTEND
    const simd16vector& a = PaGetSimdVector_simd16(pa, 0, slot);
    const simd16vector& b = PaGetSimdVector_simd16(pa, 1, slot);

    if (pa.useAlternateOffset)
    {
        primIndex += KNOB_SIMD_WIDTH;
    }

    switch (primIndex)
    {
    case 0:
        verts[0] = swizzleLane0(a);
        verts[1] = swizzleLane1(a);
        break;
    case 1:
        verts[0] = swizzleLane2(a);
        verts[1] = swizzleLane3(a);
        break;
    case 2:
        verts[0] = swizzleLane4(a);
        verts[1] = swizzleLane5(a);
        break;
    case 3:
        verts[0] = swizzleLane6(a);
        verts[1] = swizzleLane7(a);
        break;
    case 4:
        verts[0] = swizzleLane8(a);
        verts[1] = swizzleLane9(a);
        break;
    case 5:
        verts[0] = swizzleLaneA(a);
        verts[1] = swizzleLaneB(a);
        break;
    case 6:
        verts[0] = swizzleLaneC(a);
        verts[1] = swizzleLaneD(a);
        break;
    case 7:
        verts[0] = swizzleLaneE(a);
        verts[1] = swizzleLaneF(a);
        break;
    case 8:
        verts[0] = swizzleLane0(b);
        verts[1] = swizzleLane1(b);
        break;
    case 9:
        verts[0] = swizzleLane2(b);
        verts[1] = swizzleLane3(b);
        break;
    case 10:
        verts[0] = swizzleLane4(b);
        verts[1] = swizzleLane5(b);
        break;
    case 11:
        verts[0] = swizzleLane6(b);
        verts[1] = swizzleLane7(b);
        break;
    case 12:
        verts[0] = swizzleLane8(b);
        verts[1] = swizzleLane9(b);
        break;
    case 13:
        verts[0] = swizzleLaneA(b);
        verts[1] = swizzleLaneB(b);
        break;
    case 14:
        verts[0] = swizzleLaneC(b);
        verts[1] = swizzleLaneD(b);
        break;
    case 15:
        verts[0] = swizzleLaneE(b);
        verts[1] = swizzleLaneF(b);
        break;
    }
#else
    const simdvector& a = PaGetSimdVector(pa, 0, slot);
    const simdvector& b = PaGetSimdVector(pa, 1, slot);

    switch (primIndex)
    {
    case 0:
        verts[0] = swizzleLane0(a);
        verts[1] = swizzleLane1(a);
        break;
    case 1:
        verts[0] = swizzleLane2(a);
        verts[1] = swizzleLane3(a);
        break;
    case 2:
        verts[0] = swizzleLane4(a);
        verts[1] = swizzleLane5(a);
        break;
    case 3:
        verts[0] = swizzleLane6(a);
        verts[1] = swizzleLane7(a);
        break;
    case 4:
        verts[0] = swizzleLane0(b);
        verts[1] = swizzleLane1(b);
        break;
    case 5:
        verts[0] = swizzleLane2(b);
        verts[1] = swizzleLane3(b);
        break;
    case 6:
        verts[0] = swizzleLane4(b);
        verts[1] = swizzleLane5(b);
        break;
    case 7:
        verts[0] = swizzleLane6(b);
        verts[1] = swizzleLane7(b);
        break;
    }
#endif
}

bool PaLineStrip0(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[])
{
    SetNextPaState(pa, PaLineStrip1, PaLineStripSingle0);
    return false; // Not enough vertices to assemble 8 lines
}

bool PaLineStrip1(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[])
{
#if USE_SIMD16_FRONTEND
    simdvector a;
    simdvector b;

    if (!pa.useAlternateOffset)
    {
        const simd16vector& a_16 = PaGetSimdVector_simd16(pa, pa.prev, slot);

        for (uint32_t i = 0; i < 4; i += 1)
        {
            a[i] = _simd16_extract_ps(a_16[i], 0);
            b[i] = _simd16_extract_ps(a_16[i], 1);
        }
    }
    else
    {
        const simd16vector& b_16 = PaGetSimdVector_simd16(pa, pa.cur, slot);

        for (uint32_t i = 0; i < 4; i += 1)
        {
            a[i] = _simd16_extract_ps(b_16[i], 0);
            b[i] = _simd16_extract_ps(b_16[i], 1);
        }
    }

#else
    simdvector& a = PaGetSimdVector(pa, pa.prev, slot);
    simdvector& b = PaGetSimdVector(pa, pa.cur, slot);

#endif
    /// @todo: verify provoking vertex is correct
    // Line list 0  1  2  3  4  5  6  7
    //           8  9 10 11 12 13 14 15

    // shuffle:
    //           0  1  2  3  4  5  6  7
    //           1  2  3  4  5  6  7  8

    verts[0] = a;

    for (uint32_t i = 0; i < 4; ++i)
    {
        // 1 2 3 x 5 6 7 x
        __m256 vPermA = _mm256_permute_ps(a.v[i], 0x39); // indices hi->low 00 11 10 01 (0 3 2 1)
        // 4 5 6 7 8 9 10 11
        __m256 vAHighBLow = _mm256_permute2f128_ps(a.v[i], b.v[i], 0x21);

        // x x x 4 x x x 8
        __m256 vPermB = _mm256_permute_ps(vAHighBLow, 0); // indices hi->low  (0 0 0 0)

        verts[1].v[i] = _mm256_blend_ps(vPermA, vPermB, 0x88);
    }

    SetNextPaState(pa, PaLineStrip1, PaLineStripSingle0, 0, PA_STATE_OPT::SIMD_WIDTH);
    return true;
}

#if ENABLE_AVX512_SIMD16
bool PaLineStrip0_simd16(PA_STATE_OPT& pa, uint32_t slot, simd16vector verts[])
{
    SetNextPaState_simd16(pa, PaLineStrip1_simd16, PaLineStrip1, PaLineStripSingle0);
    return false; // Not enough vertices to assemble 16 lines
}

bool PaLineStrip1_simd16(PA_STATE_OPT& pa, uint32_t slot, simd16vector verts[])
{
    // clang-format off

    const simd16scalari perm = _simd16_set_epi32(0, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1);

    const simd16vector& a = PaGetSimdVector_simd16(pa, pa.prev, slot);
    const simd16vector& b = PaGetSimdVector_simd16(pa, pa.cur, slot);

    const simd16mask mask0 = 0x0001;

    // v0 -> a0 a1 a2 a3 a4 a5 a6 a7 a8 a9 aA aB aC aD aE aF
    // v1 -> a1 a2 a3 a4 a5 a6 a7 a8 a9 aA aB aC aD aE aF b0

    simd16vector& v0 = verts[0];
    simd16vector& v1 = verts[1];

    v0 = a; // a0 a1 a2 a3 a4 a5 a6 a7 a8 a9 aA aB aC aD aE aF

    // for simd16 x, y, z, and w
    for (int i = 0; i < 4; i += 1)
    {
        simd16scalar tempa = _simd16_loadu_ps(reinterpret_cast<const float*>(&a[i]));
        simd16scalar tempb = _simd16_loadu_ps(reinterpret_cast<const float*>(&b[i]));

        simd16scalar temp = _simd16_blend_ps(tempa, tempb, mask0); // b0 a1 a2 a3 a4 a5 a6 a7 a8 a9 aA aB aC aD aE aF

        v1[i] = _simd16_permute_ps(temp, perm);                    // a1 a2 a3 a4 a5 a6 a7 a8 a9 aA aB aC aD aE aF b0
    }

    SetNextPaState_simd16(pa, PaLineStrip1_simd16, PaLineStrip1, PaLineStripSingle0, 0, PA_STATE_OPT::SIMD_WIDTH);
    return true;

    // clang-format on
}

#endif
void PaLineStripSingle0(PA_STATE_OPT& pa, uint32_t slot, uint32_t primIndex, simd4scalar verts[])
{
#if USE_SIMD16_FRONTEND
    const simd16vector& a = PaGetSimdVector_simd16(pa, pa.prev, slot);
    const simd16vector& b = PaGetSimdVector_simd16(pa, pa.cur, slot);

    if (pa.useAlternateOffset)
    {
        primIndex += KNOB_SIMD_WIDTH;
    }

    switch (primIndex)
    {
    case 0:
        verts[0] = swizzleLane0(a);
        verts[1] = swizzleLane1(a);
        break;
    case 1:
        verts[0] = swizzleLane1(a);
        verts[1] = swizzleLane2(a);
        break;
    case 2:
        verts[0] = swizzleLane2(a);
        verts[1] = swizzleLane3(a);
        break;
    case 3:
        verts[0] = swizzleLane3(a);
        verts[1] = swizzleLane4(a);
        break;
    case 4:
        verts[0] = swizzleLane4(a);
        verts[1] = swizzleLane5(a);
        break;
    case 5:
        verts[0] = swizzleLane5(a);
        verts[1] = swizzleLane6(a);
        break;
    case 6:
        verts[0] = swizzleLane6(a);
        verts[1] = swizzleLane7(a);
        break;
    case 7:
        verts[0] = swizzleLane7(a);
        verts[1] = swizzleLane8(a);
        break;
    case 8:
        verts[0] = swizzleLane8(a);
        verts[1] = swizzleLane9(a);
        break;
    case 9:
        verts[0] = swizzleLane9(a);
        verts[1] = swizzleLaneA(a);
        break;
    case 10:
        verts[0] = swizzleLaneA(a);
        verts[1] = swizzleLaneB(a);
        break;
    case 11:
        verts[0] = swizzleLaneB(a);
        verts[1] = swizzleLaneC(a);
        break;
    case 12:
        verts[0] = swizzleLaneC(a);
        verts[1] = swizzleLaneD(a);
        break;
    case 13:
        verts[0] = swizzleLaneD(a);
        verts[1] = swizzleLaneE(a);
        break;
    case 14:
        verts[0] = swizzleLaneE(a);
        verts[1] = swizzleLaneF(a);
        break;
    case 15:
        verts[0] = swizzleLaneF(a);
        verts[1] = swizzleLane0(b);
        break;
    }
#else
    const simdvector& a = PaGetSimdVector(pa, pa.prev, slot);
    const simdvector& b = PaGetSimdVector(pa, pa.cur, slot);

    switch (primIndex)
    {
    case 0:
        verts[0] = swizzleLane0(a);
        verts[1] = swizzleLane1(a);
        break;
    case 1:
        verts[0] = swizzleLane1(a);
        verts[1] = swizzleLane2(a);
        break;
    case 2:
        verts[0] = swizzleLane2(a);
        verts[1] = swizzleLane3(a);
        break;
    case 3:
        verts[0] = swizzleLane3(a);
        verts[1] = swizzleLane4(a);
        break;
    case 4:
        verts[0] = swizzleLane4(a);
        verts[1] = swizzleLane5(a);
        break;
    case 5:
        verts[0] = swizzleLane5(a);
        verts[1] = swizzleLane6(a);
        break;
    case 6:
        verts[0] = swizzleLane6(a);
        verts[1] = swizzleLane7(a);
        break;
    case 7:
        verts[0] = swizzleLane7(a);
        verts[1] = swizzleLane0(b);
        break;
    }
#endif
}

bool PaPoints0(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[])
{
#if USE_SIMD16_FRONTEND
    simdvector a;

    const simd16vector& a_16 = PaGetSimdVector_simd16(pa, 0, slot);

    if (!pa.useAlternateOffset)
    {
        for (uint32_t i = 0; i < 4; i += 1)
        {
            a[i] = _simd16_extract_ps(a_16[i], 0);
        }
    }
    else
    {
        for (uint32_t i = 0; i < 4; i += 1)
        {
            a[i] = _simd16_extract_ps(a_16[i], 1);
        }
    }

#else
    simdvector& a = PaGetSimdVector(pa, 0, slot);

#endif
    verts[0] = a; // points only have 1 vertex.

    SetNextPaState(pa, PaPoints0, PaPointsSingle0, 0, PA_STATE_OPT::SIMD_WIDTH, true);
    return true;
}

#if ENABLE_AVX512_SIMD16
bool PaPoints0_simd16(PA_STATE_OPT& pa, uint32_t slot, simd16vector verts[])
{
    simd16vector& a = PaGetSimdVector_simd16(pa, pa.cur, slot);

    verts[0] = a; // points only have 1 vertex.

    SetNextPaState_simd16(
        pa, PaPoints0_simd16, PaPoints0, PaPointsSingle0, 0, PA_STATE_OPT::SIMD_WIDTH, true);
    return true;
}

#endif
void PaPointsSingle0(PA_STATE_OPT& pa, uint32_t slot, uint32_t primIndex, simd4scalar verts[])
{
#if USE_SIMD16_FRONTEND
    const simd16vector& a = PaGetSimdVector_simd16(pa, 0, slot);

    if (pa.useAlternateOffset)
    {
        primIndex += KNOB_SIMD_WIDTH;
    }

    verts[0] = swizzleLaneN(a, primIndex);
#else
    const simdvector& a = PaGetSimdVector(pa, 0, slot);

    verts[0] = swizzleLaneN(a, primIndex);
#endif
}

//////////////////////////////////////////////////////////////////////////
/// @brief State 1 for RECT_LIST topology.
///        There is not enough to assemble 8 triangles.
bool PaRectList0(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[])
{
    SetNextPaState(pa, PaRectList1, PaRectListSingle0);
    return false;
}

//////////////////////////////////////////////////////////////////////////
/// @brief State 1 for RECT_LIST topology.
///   Rect lists has the following format.
///             w          x          y           z
///      v2 o---o   v5 o---o   v8 o---o   v11 o---o
///         | \ |      | \ |      | \ |       | \ |
///      v1 o---o   v4 o---o   v7 o---o   v10 o---o
///            v0         v3         v6          v9
///
///   Only 3 vertices of the rectangle are supplied. The 4th vertex is implied.
///
///   tri0 = { v0, v1, v2 }  tri1 = { v0, v2, w } <-- w = v0 - v1 + v2
///   tri2 = { v3, v4, v5 }  tri3 = { v3, v5, x } <-- x = v3 - v4 + v5
///   etc.
///
///   PA outputs 3 simdvectors for each of the triangle vertices v0, v1, v2
///   where v0 contains all the first vertices for 8 triangles.
///
///     Result:
///      verts[0] = { v0, v0, v3, v3, v6, v6, v9, v9 }
///      verts[1] = { v1, v2, v4, v5, v7, v8, v10, v11 }
///      verts[2] = { v2,  w, v5,  x, v8,  y, v11, z }
///
/// @param pa - State for PA state machine.
/// @param slot - Index into VS output which is either a position (slot 0) or attribute.
/// @param verts - triangle output for binner. SOA - Array of v0 for 8 triangles, followed by v1,
/// etc.
bool PaRectList1(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[])
{
// SIMD vectors a and b are the last two vertical outputs from the vertex shader.
#if USE_SIMD16_FRONTEND
    simdvector a;
    simdvector b;

    if (!pa.useAlternateOffset)
    {
        const simd16vector& a_16 = PaGetSimdVector_simd16(pa, 0, slot);

        for (uint32_t i = 0; i < 4; i += 1)
        {
            a[i] = _simd16_extract_ps(a_16[i], 0);
            b[i] = _simd16_extract_ps(a_16[i], 1);
        }
    }
    else
    {
        const simd16vector& b_16 = PaGetSimdVector_simd16(pa, 1, slot);

        for (uint32_t i = 0; i < 4; i += 1)
        {
            a[i] = _simd16_extract_ps(b_16[i], 0);
            b[i] = _simd16_extract_ps(b_16[i], 1);
            ;
        }
    }

#else
    simdvector& a = PaGetSimdVector(pa, 0, slot); // a[] = { v0, v1,  v2,  v3,  v4,  v5,  v6,  v7 }
    simdvector& b = PaGetSimdVector(pa, 1, slot); // b[] = { v8, v9, v10, v11, v12, v13, v14, v15 }

#endif
    __m256 tmp0, tmp1, tmp2;

    // Loop over each component in the simdvector.
    for (int i = 0; i < 4; ++i)
    {
        simdvector& v0 = verts[0]; // verts[0] needs to be { v0, v0, v3, v3, v6, v6, v9, v9 }
        tmp0           = _mm256_permute2f128_ps(
            b[i], b[i], 0x01); // tmp0 = { v12, v13, v14, v15, v8, v9, v10, v11 }
        v0[i] = _mm256_blend_ps(
            a[i],
            tmp0,
            0x20); //   v0 = {  v0,   *,   *,  v3,  *, v9,  v6, * } where * is don't care.
        tmp1  = _mm256_permute_ps(v0[i], 0xF0); // tmp1 = {  v0,  v0,  v3,  v3,  *,  *,  *, * }
        v0[i] = _mm256_permute_ps(v0[i], 0x5A); //   v0 = {   *,   *,   *,   *,  v6, v6, v9, v9 }
        v0[i] =
            _mm256_blend_ps(tmp1, v0[i], 0xF0); //   v0 = {  v0,  v0,  v3,  v3,  v6, v6, v9, v9 }

        /// NOTE This is a bit expensive due to conflicts between vertices in 'a' and 'b'.
        ///      AVX2 should make this much cheaper.
        simdvector& v1 = verts[1]; // verts[1] needs to be { v1, v2, v4, v5, v7, v8, v10, v11 }
        v1[i]          = _mm256_permute_ps(a[i], 0x09);  //   v1 = { v1, v2,  *,  *,  *, *,  *, * }
        tmp1           = _mm256_permute_ps(a[i], 0x43);  // tmp1 = {  *,  *,  *,  *, v7, *, v4, v5 }
        tmp2  = _mm256_blend_ps(v1[i], tmp1, 0xF0);      // tmp2 = { v1, v2,  *,  *, v7, *, v4, v5 }
        tmp1  = _mm256_permute2f128_ps(tmp2, tmp2, 0x1); // tmp1 = { v7,  *, v4,  v5, *, *,  *,  * }
        v1[i] = _mm256_permute_ps(tmp0, 0xE0);      //   v1 = {  *,  *,  *,  *,  *, v8, v10, v11 }
        v1[i] = _mm256_blend_ps(tmp2, v1[i], 0xE0); //   v1 = { v1, v2,  *,  *, v7, v8, v10, v11 }
        v1[i] = _mm256_blend_ps(v1[i], tmp1, 0x0C); //   v1 = { v1, v2, v4, v5, v7, v8, v10, v11 }

        // verts[2] = { v2,  w, v5,  x, v8,  y, v11, z }
        simdvector& v2 = verts[2]; // verts[2] needs to be { v2,  w, v5,  x, v8,  y, v11, z }
        v2[i]          = _mm256_permute_ps(tmp0, 0x30); //   v2 = { *, *, *, *, v8, *, v11, * }
        tmp1           = _mm256_permute_ps(tmp2, 0x31); // tmp1 = { v2, *, v5, *, *, *, *, * }
        v2[i]          = _mm256_blend_ps(tmp1, v2[i], 0xF0);

        // Need to compute 4th implied vertex for the rectangle.
        tmp2  = _mm256_sub_ps(v0[i], v1[i]);
        tmp2  = _mm256_add_ps(tmp2, v2[i]);         // tmp2 = {  w,  *,  x, *, y,  *,  z,  * }
        tmp2  = _mm256_permute_ps(tmp2, 0xA0);      // tmp2 = {  *,  w,  *, x, *,   y,  *,  z }
        v2[i] = _mm256_blend_ps(v2[i], tmp2, 0xAA); //   v2 = { v2,  w, v5, x, v8,  y, v11, z }
    }

    SetNextPaState(pa, PaRectList1, PaRectListSingle0, 0, PA_STATE_OPT::SIMD_WIDTH, true);
    return true;
}

//////////////////////////////////////////////////////////////////////////
/// @brief State 2 for RECT_LIST topology.
///        Not implemented unless there is a use case for more then 8 rects.
/// @param pa - State for PA state machine.
/// @param slot - Index into VS output which is either a position (slot 0) or attribute.
/// @param verts - triangle output for binner. SOA - Array of v0 for 8 triangles, followed by v1,
/// etc.
bool PaRectList2(PA_STATE_OPT& pa, uint32_t slot, simdvector verts[])
{
    SWR_INVALID("Is rect list used for anything other then clears?");
    SetNextPaState(pa, PaRectList0, PaRectListSingle0, 0, PA_STATE_OPT::SIMD_WIDTH, true);
    return true;
}

#if ENABLE_AVX512_SIMD16
//////////////////////////////////////////////////////////////////////////
/// @brief State 1 for RECT_LIST topology.
///        There is not enough to assemble 8 triangles.
bool PaRectList0_simd16(PA_STATE_OPT& pa, uint32_t slot, simd16vector verts[])
{
    SetNextPaState_simd16(pa, PaRectList1_simd16, PaRectList1, PaRectListSingle0);
    return false;
}

//////////////////////////////////////////////////////////////////////////
/// @brief State 1 for RECT_LIST topology.
///   Rect lists has the following format.
///             w          x          y           z
///      v2 o---o   v5 o---o   v8 o---o   v11 o---o
///         | \ |      | \ |      | \ |       | \ |
///      v1 o---o   v4 o---o   v7 o---o   v10 o---o
///            v0         v3         v6          v9
///
///   Only 3 vertices of the rectangle are supplied. The 4th vertex is implied.
///
///   tri0 = { v0, v1, v2 }  tri1 = { v0, v2, w } <-- w = v0 - v1 + v2
///   tri2 = { v3, v4, v5 }  tri3 = { v3, v5, x } <-- x = v3 - v4 + v5
///   etc.
///
///   PA outputs 3 simdvectors for each of the triangle vertices v0, v1, v2
///   where v0 contains all the first vertices for 8 triangles.
///
///     Result:
///      verts[0] = { v0, v0, v3, v3, v6, v6, v9, v9 }
///      verts[1] = { v1, v2, v4, v5, v7, v8, v10, v11 }
///      verts[2] = { v2,  w, v5,  x, v8,  y, v11, z }
///
/// @param pa - State for PA state machine.
/// @param slot - Index into VS output which is either a position (slot 0) or attribute.
/// @param verts - triangle output for binner. SOA - Array of v0 for 8 triangles, followed by v1,
/// etc.
bool PaRectList1_simd16(PA_STATE_OPT& pa, uint32_t slot, simd16vector verts[])
{
    // clang-format off

    simdvector a;
    simdvector b;

    if (!pa.useAlternateOffset)
    {
        const simd16vector& a_16 = PaGetSimdVector_simd16(pa, 0, slot); // a[] = { v0, v1,  v2,  v3,  v4,  v5,  v6,  v7,
                                                                        //         v8, v9, v10, v11, v12, v13, v14, v15 }

        for (uint32_t i = 0; i < 4; i += 1)
        {
            a[i] = _simd16_extract_ps(a_16[i], 0);
            b[i] = _simd16_extract_ps(a_16[i], 1);
        }
    }
    else
    {
        const simd16vector& b_16 = PaGetSimdVector_simd16(pa, 1, slot); // b[] = { v16...but not used by this implementation.. }

        for (uint32_t i = 0; i < 4; i += 1)
        {
            a[i] = _simd16_extract_ps(b_16[i], 0);
            b[i] = _simd16_extract_ps(b_16[i], 1);
        }
    }

    simd16vector& v0 = verts[0]; // verts[0] needs to be { v0, v0, v3, v3, v6, v6,  v9,  v9 }
    simd16vector& v1 = verts[1]; // verts[1] needs to be { v1, v2, v4, v5, v7, v8, v10, v11 }
    simd16vector& v2 = verts[2]; // verts[2] needs to be { v2,  w, v5,  x, v8,  y, v11,   z }

    // Loop over each component in the simdvector.
    for (int i = 0; i < 4; i += 1)
    {
        simdscalar v0_lo; // verts[0] needs to be { v0, v0, v3, v3, v6, v6, v9, v9 }
        simdscalar v1_lo; // verts[1] needs to be { v1, v2, v4, v5, v7, v8, v10, v11 }
        simdscalar v2_lo; // verts[2] needs to be { v2,  w, v5,  x, v8,  y, v11, z }

        __m256 tmp0, tmp1, tmp2;

        tmp0  = _mm256_permute2f128_ps(b[i], b[i], 0x01); // tmp0 = { v12, v13, v14, v15, v8, v9, v10, v11 }
        v0_lo = _mm256_blend_ps(a[i], tmp0, 0x20);        //   v0 = {  v0,   *,   *,  v3,  *, v9,  v6,   * } where * is don't care.
        tmp1  = _mm256_permute_ps(v0_lo, 0xF0);           // tmp1 = {  v0,  v0,  v3,  v3,  *,  *,   *,   * }
        v0_lo = _mm256_permute_ps(v0_lo, 0x5A);           //   v0 = {   *,   *,   *,   *,  v6, v6, v9,  v9 }
        v0_lo = _mm256_blend_ps(tmp1, v0_lo, 0xF0);       //   v0 = {  v0,  v0,  v3,  v3,  v6, v6, v9,  v9 }

        /// NOTE This is a bit expensive due to conflicts between vertices in 'a' and 'b'.
        ///      AVX2 should make this much cheaper.
        v1_lo = _mm256_permute_ps(a[i], 0x09);            //   v1 = { v1, v2,  *,  *,  *,  *,   *,   * }
        tmp1  = _mm256_permute_ps(a[i], 0x43);            // tmp1 = {  *,  *,  *,  *, v7,  *,  v4,  v5 }
        tmp2  = _mm256_blend_ps(v1_lo, tmp1, 0xF0);       // tmp2 = { v1, v2,  *,  *, v7,  *,  v4,  v5 }
        tmp1  = _mm256_permute2f128_ps(tmp2, tmp2, 0x1);  // tmp1 = { v7,  *, v4,  v5, *,  *,   *,   * }
        v1_lo = _mm256_permute_ps(tmp0, 0xE0);            //   v1 = {  *,  *,  *,  *,  *, v8, v10, v11 }
        v1_lo = _mm256_blend_ps(tmp2, v1_lo, 0xE0);       //   v1 = { v1, v2,  *,  *, v7, v8, v10, v11 }
        v1_lo = _mm256_blend_ps(v1_lo, tmp1, 0x0C);       //   v1 = { v1, v2, v4, v5, v7, v8, v10, v11 }

        // verts[2] = { v2,  w, v5,  x, v8,  y, v11, z }
        v2_lo = _mm256_permute_ps(tmp0, 0x30);            //   v2 = { *,  *,  *, *, v8, *, v11, * }
        tmp1  = _mm256_permute_ps(tmp2, 0x31);            // tmp1 = { v2, *, v5, *,  *, *,   *, * }
        v2_lo = _mm256_blend_ps(tmp1, v2_lo, 0xF0);

        // Need to compute 4th implied vertex for the rectangle.
        tmp2  = _mm256_sub_ps(v0_lo, v1_lo);
        tmp2  = _mm256_add_ps(tmp2, v2_lo);               // tmp2 = {  w,  *,  x, *, y,  *,  z,  * }
        tmp2  = _mm256_permute_ps(tmp2, 0xA0);            // tmp2 = {  *,  w,  *, x, *,  y,  *,  z }
        v2_lo = _mm256_blend_ps(v2_lo, tmp2, 0xAA);       //   v2 = { v2,  w, v5, x, v8, y, v11, z }

        v0[i] = _simd16_insert_ps(_simd16_setzero_ps(), v0_lo, 0);
        v1[i] = _simd16_insert_ps(_simd16_setzero_ps(), v1_lo, 0);
        v2[i] = _simd16_insert_ps(_simd16_setzero_ps(), v2_lo, 0);
    }

    SetNextPaState_simd16(pa, PaRectList1_simd16, PaRectList1, PaRectListSingle0, 0, PA_STATE_OPT::SIMD_WIDTH, true);
    return true;

    // clang-format on
}

//////////////////////////////////////////////////////////////////////////
/// @brief State 2 for RECT_LIST topology.
///        Not implemented unless there is a use case for more then 8 rects.
/// @param pa - State for PA state machine.
/// @param slot - Index into VS output which is either a position (slot 0) or attribute.
/// @param verts - triangle output for binner. SOA - Array of v0 for 8 triangles, followed by v1,
/// etc.
bool PaRectList2_simd16(PA_STATE_OPT& pa, uint32_t slot, simd16vector verts[])
{
    SWR_INVALID("Is rect list used for anything other then clears?");
    SetNextPaState_simd16(
        pa, PaRectList0_simd16, PaRectList0, PaRectListSingle0, 0, PA_STATE_OPT::SIMD_WIDTH, true);
    return true;
}

#endif
//////////////////////////////////////////////////////////////////////////
/// @brief This procedure is called by the Binner to assemble the attributes.
///        Unlike position, which is stored vertically, the attributes are
///        stored horizontally. The outputs from the VS, labeled as 'a' and
///        'b' are vertical. This function needs to transpose the lanes
///        containing the vertical attribute data into horizontal form.
/// @param pa - State for PA state machine.
/// @param slot - Index into VS output for a given attribute.
/// @param primIndex - Binner processes each triangle individually.
/// @param verts - triangle output for binner. SOA - Array of v0 for 8 triangles, followed by v1,
/// etc.
void PaRectListSingle0(PA_STATE_OPT& pa, uint32_t slot, uint32_t primIndex, simd4scalar verts[])
{
// We have 12 simdscalars contained within 3 simdvectors which
// hold at least 8 triangles worth of data. We want to assemble a single
// triangle with data in horizontal form.
#if USE_SIMD16_FRONTEND
    simdvector a;
    simdvector b;

    if (!pa.useAlternateOffset)
    {
        const simd16vector& a_16 = PaGetSimdVector_simd16(pa, 0, slot);

        for (uint32_t i = 0; i < 4; i += 1)
        {
            a[i] = _simd16_extract_ps(a_16[i], 0);
            b[i] = _simd16_extract_ps(a_16[i], 1);
        }
    }
    else
    {
        const simd16vector& b_16 = PaGetSimdVector_simd16(pa, 1, slot);

        for (uint32_t i = 0; i < 4; i += 1)
        {
            a[i] = _simd16_extract_ps(b_16[i], 0);
            b[i] = _simd16_extract_ps(b_16[i], 1);
            ;
        }
    }

#else
    simdvector& a = PaGetSimdVector(pa, 0, slot);

#endif
    // Convert from vertical to horizontal.
    switch (primIndex)
    {
    case 0:
        verts[0] = swizzleLane0(a);
        verts[1] = swizzleLane1(a);
        verts[2] = swizzleLane2(a);
        break;
    case 1:
        verts[0] = swizzleLane0(a);
        verts[1] = swizzleLane2(a);
        verts[2] = _mm_blend_ps(verts[0], verts[1], 0xA);
        break;
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
        SWR_INVALID("Invalid primIndex: %d", primIndex);
        break;
    };
}

PA_STATE_OPT::PA_STATE_OPT(DRAW_CONTEXT*      in_pDC,
                           uint32_t           in_numPrims,
                           uint8_t*           pStream,
                           uint32_t           in_streamSizeInVerts,
                           uint32_t           in_vertexStride,
                           bool               in_isStreaming,
                           uint32_t           numVertsPerPrim,
                           PRIMITIVE_TOPOLOGY topo) :
    PA_STATE(in_pDC, pStream, in_streamSizeInVerts, in_vertexStride, numVertsPerPrim),
    numPrims(in_numPrims), numPrimsComplete(0), numSimdPrims(0), cur(0), prev(0), first(0),
    counter(0), reset(false), pfnPaFunc(nullptr), isStreaming(in_isStreaming)
{
    const API_STATE& state = GetApiState(pDC);

    this->binTopology = topo == TOP_UNKNOWN ? state.topology : topo;

#if ENABLE_AVX512_SIMD16
    pfnPaFunc_simd16 = nullptr;

#endif
    switch (this->binTopology)
    {
    case TOP_TRIANGLE_LIST:
        this->pfnPaFunc = PaTriList0;
#if ENABLE_AVX512_SIMD16
        this->pfnPaFunc_simd16 = PaTriList0_simd16;
#endif
        break;
    case TOP_TRIANGLE_STRIP:
        this->pfnPaFunc = PaTriStrip0;
#if ENABLE_AVX512_SIMD16
        this->pfnPaFunc_simd16 = PaTriStrip0_simd16;
#endif
        break;
    case TOP_TRIANGLE_FAN:
        this->pfnPaFunc = PaTriFan0;
#if ENABLE_AVX512_SIMD16
        this->pfnPaFunc_simd16 = PaTriFan0_simd16;
#endif
        break;
    case TOP_QUAD_LIST:
        this->pfnPaFunc = PaQuadList0;
#if ENABLE_AVX512_SIMD16
        this->pfnPaFunc_simd16 = PaQuadList0_simd16;
#endif
        this->numPrims = in_numPrims * 2; // Convert quad primitives into triangles
        break;
    case TOP_QUAD_STRIP:
        // quad strip pattern when decomposed into triangles is the same as verts strips
        this->pfnPaFunc = PaTriStrip0;
#if ENABLE_AVX512_SIMD16
        this->pfnPaFunc_simd16 = PaTriStrip0_simd16;
#endif
        this->numPrims = in_numPrims * 2; // Convert quad primitives into triangles
        break;
    case TOP_LINE_LIST:
        this->pfnPaFunc = PaLineList0;
#if ENABLE_AVX512_SIMD16
        this->pfnPaFunc_simd16 = PaLineList0_simd16;
#endif
        this->numPrims = in_numPrims;
        break;
    case TOP_LINE_STRIP:
        this->pfnPaFunc = PaLineStrip0;
#if ENABLE_AVX512_SIMD16
        this->pfnPaFunc_simd16 = PaLineStrip0_simd16;
#endif
        this->numPrims = in_numPrims;
        break;
    case TOP_LINE_LOOP:
        this->pfnPaFunc = PaLineLoop0;
#if ENABLE_AVX512_SIMD16
        this->pfnPaFunc_simd16 = PaLineLoop0_simd16;
#endif
        this->numPrims = in_numPrims;
        break;
    case TOP_POINT_LIST:
        this->pfnPaFunc = PaPoints0;
#if ENABLE_AVX512_SIMD16
        this->pfnPaFunc_simd16 = PaPoints0_simd16;
#endif
        this->numPrims = in_numPrims;
        break;
    case TOP_RECT_LIST:
        this->pfnPaFunc = PaRectList0;
#if ENABLE_AVX512_SIMD16
        this->pfnPaFunc_simd16 = PaRectList0_simd16;
#endif
        this->numPrims = in_numPrims * 2;
        break;

    case TOP_PATCHLIST_1:
        this->pfnPaFunc = PaPatchList<1>;
#if ENABLE_AVX512_SIMD16
        this->pfnPaFunc_simd16 = PaPatchList_simd16<1>;
#endif
        break;
    case TOP_PATCHLIST_2:
        this->pfnPaFunc = PaPatchList<2>;
#if ENABLE_AVX512_SIMD16
        this->pfnPaFunc_simd16 = PaPatchList_simd16<2>;
#endif
        break;
    case TOP_PATCHLIST_3:
        this->pfnPaFunc = PaPatchList<3>;
#if ENABLE_AVX512_SIMD16
        this->pfnPaFunc_simd16 = PaPatchList_simd16<3>;
#endif
        break;
    case TOP_PATCHLIST_4:
        this->pfnPaFunc = PaPatchList<4>;
#if ENABLE_AVX512_SIMD16
        this->pfnPaFunc_simd16 = PaPatchList_simd16<4>;
#endif
        break;
    case TOP_PATCHLIST_5:
        this->pfnPaFunc = PaPatchList<5>;
#if ENABLE_AVX512_SIMD16
        this->pfnPaFunc_simd16 = PaPatchList_simd16<5>;
#endif
        break;
    case TOP_PATCHLIST_6:
        this->pfnPaFunc = PaPatchList<6>;
#if ENABLE_AVX512_SIMD16
        this->pfnPaFunc_simd16 = PaPatchList_simd16<6>;
#endif
        break;
    case TOP_PATCHLIST_7:
        this->pfnPaFunc = PaPatchList<7>;
#if ENABLE_AVX512_SIMD16
        this->pfnPaFunc_simd16 = PaPatchList_simd16<7>;
#endif
        break;
    case TOP_PATCHLIST_8:
        this->pfnPaFunc = PaPatchList<8>;
#if ENABLE_AVX512_SIMD16
        this->pfnPaFunc_simd16 = PaPatchList_simd16<8>;
#endif
        break;
    case TOP_PATCHLIST_9:
        this->pfnPaFunc = PaPatchList<9>;
#if ENABLE_AVX512_SIMD16
        this->pfnPaFunc_simd16 = PaPatchList_simd16<9>;
#endif
        break;
    case TOP_PATCHLIST_10:
        this->pfnPaFunc = PaPatchList<10>;
#if ENABLE_AVX512_SIMD16
        this->pfnPaFunc_simd16 = PaPatchList_simd16<10>;
#endif
        break;
    case TOP_PATCHLIST_11:
        this->pfnPaFunc = PaPatchList<11>;
#if ENABLE_AVX512_SIMD16
        this->pfnPaFunc_simd16 = PaPatchList_simd16<11>;
#endif
        break;
    case TOP_PATCHLIST_12:
        this->pfnPaFunc = PaPatchList<12>;
#if ENABLE_AVX512_SIMD16
        this->pfnPaFunc_simd16 = PaPatchList_simd16<12>;
#endif
        break;
    case TOP_PATCHLIST_13:
        this->pfnPaFunc = PaPatchList<13>;
#if ENABLE_AVX512_SIMD16
        this->pfnPaFunc_simd16 = PaPatchList_simd16<13>;
#endif
        break;
    case TOP_PATCHLIST_14:
        this->pfnPaFunc = PaPatchList<14>;
#if ENABLE_AVX512_SIMD16
        this->pfnPaFunc_simd16 = PaPatchList_simd16<14>;
#endif
        break;
    case TOP_PATCHLIST_15:
        this->pfnPaFunc = PaPatchList<15>;
#if ENABLE_AVX512_SIMD16
        this->pfnPaFunc_simd16 = PaPatchList_simd16<15>;
#endif
        break;
    case TOP_PATCHLIST_16:
        this->pfnPaFunc = PaPatchList<16>;
#if ENABLE_AVX512_SIMD16
        this->pfnPaFunc_simd16 = PaPatchList_simd16<16>;
#endif
        break;
    case TOP_PATCHLIST_17:
        this->pfnPaFunc = PaPatchList<17>;
#if ENABLE_AVX512_SIMD16
        this->pfnPaFunc_simd16 = PaPatchList_simd16<17>;
#endif
        break;
    case TOP_PATCHLIST_18:
        this->pfnPaFunc = PaPatchList<18>;
#if ENABLE_AVX512_SIMD16
        this->pfnPaFunc_simd16 = PaPatchList_simd16<18>;
#endif
        break;
    case TOP_PATCHLIST_19:
        this->pfnPaFunc = PaPatchList<19>;
#if ENABLE_AVX512_SIMD16
        this->pfnPaFunc_simd16 = PaPatchList_simd16<19>;
#endif
        break;
    case TOP_PATCHLIST_20:
        this->pfnPaFunc = PaPatchList<20>;
#if ENABLE_AVX512_SIMD16
        this->pfnPaFunc_simd16 = PaPatchList_simd16<20>;
#endif
        break;
    case TOP_PATCHLIST_21:
        this->pfnPaFunc = PaPatchList<21>;
#if ENABLE_AVX512_SIMD16
        this->pfnPaFunc_simd16 = PaPatchList_simd16<21>;
#endif
        break;
    case TOP_PATCHLIST_22:
        this->pfnPaFunc = PaPatchList<22>;
#if ENABLE_AVX512_SIMD16
        this->pfnPaFunc_simd16 = PaPatchList_simd16<22>;
#endif
        break;
    case TOP_PATCHLIST_23:
        this->pfnPaFunc = PaPatchList<23>;
#if ENABLE_AVX512_SIMD16
        this->pfnPaFunc_simd16 = PaPatchList_simd16<23>;
#endif
        break;
    case TOP_PATCHLIST_24:
        this->pfnPaFunc = PaPatchList<24>;
#if ENABLE_AVX512_SIMD16
        this->pfnPaFunc_simd16 = PaPatchList_simd16<24>;
#endif
        break;
    case TOP_PATCHLIST_25:
        this->pfnPaFunc = PaPatchList<25>;
#if ENABLE_AVX512_SIMD16
        this->pfnPaFunc_simd16 = PaPatchList_simd16<25>;
#endif
        break;
    case TOP_PATCHLIST_26:
        this->pfnPaFunc = PaPatchList<26>;
#if ENABLE_AVX512_SIMD16
        this->pfnPaFunc_simd16 = PaPatchList_simd16<26>;
#endif
        break;
    case TOP_PATCHLIST_27:
        this->pfnPaFunc = PaPatchList<27>;
#if ENABLE_AVX512_SIMD16
        this->pfnPaFunc_simd16 = PaPatchList_simd16<27>;
#endif
        break;
    case TOP_PATCHLIST_28:
        this->pfnPaFunc = PaPatchList<28>;
#if ENABLE_AVX512_SIMD16
        this->pfnPaFunc_simd16 = PaPatchList_simd16<28>;
#endif
        break;
    case TOP_PATCHLIST_29:
        this->pfnPaFunc = PaPatchList<29>;
#if ENABLE_AVX512_SIMD16
        this->pfnPaFunc_simd16 = PaPatchList_simd16<29>;
#endif
        break;
    case TOP_PATCHLIST_30:
        this->pfnPaFunc = PaPatchList<30>;
#if ENABLE_AVX512_SIMD16
        this->pfnPaFunc_simd16 = PaPatchList_simd16<30>;
#endif
        break;
    case TOP_PATCHLIST_31:
        this->pfnPaFunc = PaPatchList<31>;
#if ENABLE_AVX512_SIMD16
        this->pfnPaFunc_simd16 = PaPatchList_simd16<31>;
#endif
        break;
    case TOP_PATCHLIST_32:
        this->pfnPaFunc = PaPatchList<32>;
#if ENABLE_AVX512_SIMD16
        this->pfnPaFunc_simd16 = PaPatchList_simd16<32>;
#endif
        break;

    default:
        SWR_INVALID("Invalid topology: %d", this->binTopology);
        break;
    };

    this->pfnPaFuncReset = this->pfnPaFunc;
#if ENABLE_AVX512_SIMD16
    this->pfnPaFuncReset_simd16 = this->pfnPaFunc_simd16;
#endif

#if USE_SIMD16_FRONTEND
    simd16scalari id16 = _simd16_set_epi32(15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);
    simd16scalari id82 = _simd16_set_epi32(7, 7, 6, 6, 5, 5, 4, 4, 3, 3, 2, 2, 1, 1, 0, 0);

#else
    simdscalari id8 = _simd_set_epi32(7, 6, 5, 4, 3, 2, 1, 0);
    simdscalari id4 = _simd_set_epi32(3, 3, 2, 2, 1, 1, 0, 0);

#endif
    switch (this->binTopology)
    {
    case TOP_TRIANGLE_LIST:
    case TOP_TRIANGLE_STRIP:
    case TOP_TRIANGLE_FAN:
    case TOP_LINE_STRIP:
    case TOP_LINE_LIST:
    case TOP_LINE_LOOP:
#if USE_SIMD16_FRONTEND
        this->primIDIncr = 16;
        this->primID     = id16;
#else
        this->primIDIncr = 8;
        this->primID = id8;
#endif
        break;
    case TOP_QUAD_LIST:
    case TOP_QUAD_STRIP:
    case TOP_RECT_LIST:
#if USE_SIMD16_FRONTEND
        this->primIDIncr = 8;
        this->primID     = id82;
#else
        this->primIDIncr = 4;
        this->primID = id4;
#endif
        break;
    case TOP_POINT_LIST:
#if USE_SIMD16_FRONTEND
        this->primIDIncr = 16;
        this->primID     = id16;
#else
        this->primIDIncr = 8;
        this->primID = id8;
#endif
        break;
    case TOP_PATCHLIST_1:
    case TOP_PATCHLIST_2:
    case TOP_PATCHLIST_3:
    case TOP_PATCHLIST_4:
    case TOP_PATCHLIST_5:
    case TOP_PATCHLIST_6:
    case TOP_PATCHLIST_7:
    case TOP_PATCHLIST_8:
    case TOP_PATCHLIST_9:
    case TOP_PATCHLIST_10:
    case TOP_PATCHLIST_11:
    case TOP_PATCHLIST_12:
    case TOP_PATCHLIST_13:
    case TOP_PATCHLIST_14:
    case TOP_PATCHLIST_15:
    case TOP_PATCHLIST_16:
    case TOP_PATCHLIST_17:
    case TOP_PATCHLIST_18:
    case TOP_PATCHLIST_19:
    case TOP_PATCHLIST_20:
    case TOP_PATCHLIST_21:
    case TOP_PATCHLIST_22:
    case TOP_PATCHLIST_23:
    case TOP_PATCHLIST_24:
    case TOP_PATCHLIST_25:
    case TOP_PATCHLIST_26:
    case TOP_PATCHLIST_27:
    case TOP_PATCHLIST_28:
    case TOP_PATCHLIST_29:
    case TOP_PATCHLIST_30:
    case TOP_PATCHLIST_31:
    case TOP_PATCHLIST_32:
        // Always run KNOB_SIMD_WIDTH number of patches at a time.
#if USE_SIMD16_FRONTEND
        this->primIDIncr = 16;
        this->primID     = id16;
#else
        this->primIDIncr = 8;
        this->primID = id8;
#endif
        break;

    default:
        SWR_INVALID("Invalid topology: %d", this->binTopology);
        break;
    };
}
#endif
