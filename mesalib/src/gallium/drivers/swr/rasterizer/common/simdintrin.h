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

#ifndef __SWR_SIMDINTRIN_H__
#define __SWR_SIMDINTRIN_H__

#include "common/intrin.h"
#include "common/simdlib.hpp"

#if KNOB_SIMD_WIDTH == 8
typedef SIMD256 SIMD;
#else
#error Unsupported vector width
#endif // KNOB_SIMD16_WIDTH == 16

#define _simd128_maskstore_ps SIMD128::maskstore_ps
#define _simd128_fmadd_ps SIMD128::fmadd_ps

#define _simd_load_ps SIMD::load_ps
#define _simd_load1_ps SIMD::broadcast_ss
#define _simd_loadu_ps SIMD::loadu_ps
#define _simd_setzero_ps SIMD::setzero_ps
#define _simd_set1_ps SIMD::set1_ps
#define _simd_blend_ps(a, b, i) SIMD::blend_ps<i>(a, b)
#define _simd_blend_epi32(a, b, i) SIMD::blend_epi32<i>(a, b)
#define _simd_blendv_ps SIMD::blendv_ps
#define _simd_store_ps SIMD::store_ps
#define _simd_mul_ps SIMD::mul_ps
#define _simd_add_ps SIMD::add_ps
#define _simd_sub_ps SIMD::sub_ps
#define _simd_rsqrt_ps SIMD::rsqrt_ps
#define _simd_min_ps SIMD::min_ps
#define _simd_max_ps SIMD::max_ps
#define _simd_movemask_ps SIMD::movemask_ps
#define _simd_cvtps_epi32 SIMD::cvtps_epi32
#define _simd_cvttps_epi32 SIMD::cvttps_epi32
#define _simd_cvtepi32_ps SIMD::cvtepi32_ps
#define _simd_cmplt_ps SIMD::cmplt_ps
#define _simd_cmpgt_ps SIMD::cmpgt_ps
#define _simd_cmpneq_ps SIMD::cmpneq_ps
#define _simd_cmpeq_ps SIMD::cmpeq_ps
#define _simd_cmpge_ps SIMD::cmpge_ps
#define _simd_cmple_ps SIMD::cmple_ps
#define _simd_cmp_ps(a, b, imm) SIMD::cmp_ps<SIMD::CompareType(imm)>(a, b)
#define _simd_and_ps SIMD::and_ps
#define _simd_or_ps SIMD::or_ps
#define _simd_rcp_ps SIMD::rcp_ps
#define _simd_div_ps SIMD::div_ps
#define _simd_castsi_ps SIMD::castsi_ps
#define _simd_castps_pd SIMD::castps_pd
#define _simd_castpd_ps SIMD::castpd_ps
#define _simd_andnot_ps SIMD::andnot_ps
#define _simd_round_ps(a, i) SIMD::round_ps<SIMD::RoundMode(i)>(a)
#define _simd_castpd_ps SIMD::castpd_ps
#define _simd_broadcast_ps(a) SIMD::broadcast_ps((SIMD128::Float const*)(a))
#define _simd_stream_ps SIMD::stream_ps

#define _simd_movemask_pd SIMD::movemask_pd
#define _simd_castsi_pd SIMD::castsi_pd

#define _simd_mul_epi32 SIMD::mul_epi32
#define _simd_mullo_epi32 SIMD::mullo_epi32
#define _simd_sub_epi32 SIMD::sub_epi32
#define _simd_sub_epi64 SIMD::sub_epi64
#define _simd_min_epi32 SIMD::min_epi32
#define _simd_min_epu32 SIMD::min_epu32
#define _simd_max_epi32 SIMD::max_epi32
#define _simd_max_epu32 SIMD::max_epu32
#define _simd_add_epi32 SIMD::add_epi32
#define _simd_and_si SIMD::and_si
#define _simd_andnot_si SIMD::andnot_si
#define _simd_cmpeq_epi32 SIMD::cmpeq_epi32
#define _simd_cmplt_epi32 SIMD::cmplt_epi32
#define _simd_cmpgt_epi32 SIMD::cmpgt_epi32
#define _simd_or_si SIMD::or_si
#define _simd_xor_si SIMD::xor_si
#define _simd_castps_si SIMD::castps_si
#define _simd_adds_epu8 SIMD::adds_epu8
#define _simd_subs_epu8 SIMD::subs_epu8
#define _simd_add_epi8 SIMD::add_epi8
#define _simd_cmpeq_epi64 SIMD::cmpeq_epi64
#define _simd_cmpgt_epi64 SIMD::cmpgt_epi64
#define _simd_cmpgt_epi8 SIMD::cmpgt_epi8
#define _simd_cmpeq_epi8 SIMD::cmpeq_epi8
#define _simd_cmpgt_epi16 SIMD::cmpgt_epi16
#define _simd_cmpeq_epi16 SIMD::cmpeq_epi16
#define _simd_movemask_epi8 SIMD::movemask_epi8
#define _simd_permute_ps_i(a, i) SIMD::permute_ps<i>(a)
#define _simd_permute_ps SIMD::permute_ps
#define _simd_permute_epi32 SIMD::permute_epi32
#define _simd_srlv_epi32 SIMD::srlv_epi32
#define _simd_sllv_epi32 SIMD::sllv_epi32

#define _simd_unpacklo_epi8 SIMD::unpacklo_epi8
#define _simd_unpackhi_epi8 SIMD::unpackhi_epi8
#define _simd_unpacklo_epi16 SIMD::unpacklo_epi16
#define _simd_unpackhi_epi16 SIMD::unpackhi_epi16
#define _simd_unpacklo_epi32 SIMD::unpacklo_epi32
#define _simd_unpackhi_epi32 SIMD::unpackhi_epi32
#define _simd_unpacklo_epi64 SIMD::unpacklo_epi64
#define _simd_unpackhi_epi64 SIMD::unpackhi_epi64

#define _simd_slli_epi32(a, i) SIMD::slli_epi32<i>(a)
#define _simd_srai_epi32(a, i) SIMD::srai_epi32<i>(a)
#define _simd_srli_epi32(a, i) SIMD::srli_epi32<i>(a)
#define _simd_srlisi_ps(a, i) SIMD::srlisi_ps<i>(a)

#define _simd_fmadd_ps SIMD::fmadd_ps
#define _simd_fmsub_ps SIMD::fmsub_ps
#define _simd_shuffle_epi8 SIMD::shuffle_epi8

#define _simd_i32gather_ps(p, o, s) SIMD::i32gather_ps<SIMD::ScaleFactor(s)>(p, o)
#define _simd_mask_i32gather_ps(r, p, o, m, s) \
    SIMD::mask_i32gather_ps<SIMD::ScaleFactor(s)>(r, p, o, m)
#define _simd_abs_epi32 SIMD::abs_epi32

#define _simd_cvtepu8_epi16 SIMD::cvtepu8_epi16
#define _simd_cvtepu8_epi32 SIMD::cvtepu8_epi32
#define _simd_cvtepu16_epi32 SIMD::cvtepu16_epi32
#define _simd_cvtepu16_epi64 SIMD::cvtepu16_epi64
#define _simd_cvtepu32_epi64 SIMD::cvtepu32_epi64

#define _simd_packus_epi16 SIMD::packus_epi16
#define _simd_packs_epi16 SIMD::packs_epi16
#define _simd_packus_epi32 SIMD::packus_epi32
#define _simd_packs_epi32 SIMD::packs_epi32

#define _simd_unpacklo_ps SIMD::unpacklo_ps
#define _simd_unpackhi_ps SIMD::unpackhi_ps
#define _simd_unpacklo_pd SIMD::unpacklo_pd
#define _simd_unpackhi_pd SIMD::unpackhi_pd
#define _simd_insertf128_ps SIMD::insertf128_ps
#define _simd_insertf128_pd SIMD::insertf128_pd
#define _simd_insertf128_si(a, b, i) SIMD::insertf128_si<i>(a, b)
#define _simd_extractf128_ps(a, i) SIMD::extractf128_ps<i>(a)
#define _simd_extractf128_pd(a, i) SIMD::extractf128_pd<i>(a)
#define _simd_extractf128_si(a, i) SIMD::extractf128_si<i>(a)
#define _simd_permute2f128_ps(a, b, i) SIMD::permute2f128_ps<i>(a, b)
#define _simd_permute2f128_pd(a, b, i) SIMD::permute2f128_pd<i>(a, b)
#define _simd_permute2f128_si(a, b, i) SIMD::permute2f128_si<i>(a, b)
#define _simd_shuffle_ps(a, b, i) SIMD::shuffle_ps<i>(a, b)
#define _simd_shuffle_pd(a, b, i) SIMD::shuffle_pd<i>(a, b)
#define _simd_shuffle_epi32(a, b, imm8) SIMD::shuffle_epi32<imm8>(a, b)
#define _simd_shuffle_epi64(a, b, imm8) SIMD::shuffle_epi64<imm8>(a, b)
#define _simd_set1_epi32 SIMD::set1_epi32
#define _simd_set_epi32 SIMD::set_epi32
#define _simd_set_ps SIMD::set_ps
#define _simd_set1_epi8 SIMD::set1_epi8
#define _simd_setzero_si SIMD::setzero_si
#define _simd_cvttps_epi32 SIMD::cvttps_epi32
#define _simd_store_si SIMD::store_si
#define _simd_broadcast_ss SIMD::broadcast_ss
#define _simd_maskstore_ps SIMD::maskstore_ps
#define _simd_load_si SIMD::load_si
#define _simd_loadu_si SIMD::loadu_si
#define _simd_sub_ps SIMD::sub_ps
#define _simd_testz_ps SIMD::testz_ps
#define _simd_testz_si SIMD::testz_si
#define _simd_xor_ps SIMD::xor_ps

#define _simd_loadu2_si SIMD::loadu2_si
#define _simd_storeu2_si SIMD::storeu2_si

#define _simd_blendv_epi32 SIMD::blendv_epi32
#define _simd_vmask_ps SIMD::vmask_ps

template <int mask>
SIMDINLINE SIMD128::Integer _simd_blend4_epi32(SIMD128::Integer const& a, SIMD128::Integer const& b)
{
    return SIMD128::castps_si(
        SIMD128::blend_ps<mask>(SIMD128::castsi_ps(a), SIMD128::castsi_ps(b)));
}

//////////////////////////////////////////////////////////////////////////
/// @brief Compute plane equation vA * vX + vB * vY + vC
SIMDINLINE simdscalar vplaneps(simdscalar const& vA,
                               simdscalar const& vB,
                               simdscalar const& vC,
                               simdscalar const& vX,
                               simdscalar const& vY)
{
    simdscalar vOut = _simd_fmadd_ps(vA, vX, vC);
    vOut            = _simd_fmadd_ps(vB, vY, vOut);
    return vOut;
}

//////////////////////////////////////////////////////////////////////////
/// @brief Compute plane equation vA * vX + vB * vY + vC
SIMDINLINE simd4scalar vplaneps(simd4scalar const& vA,
                                simd4scalar const& vB,
                                simd4scalar const& vC,
                                simd4scalar const& vX,
                                simd4scalar const& vY)
{
    simd4scalar vOut = _simd128_fmadd_ps(vA, vX, vC);
    vOut             = _simd128_fmadd_ps(vB, vY, vOut);
    return vOut;
}

//////////////////////////////////////////////////////////////////////////
/// @brief Interpolates a single component.
/// @param vI - barycentric I
/// @param vJ - barycentric J
/// @param pInterpBuffer - pointer to attribute barycentric coeffs
template <UINT Attrib, UINT Comp, UINT numComponents = 4>
static SIMDINLINE simdscalar InterpolateComponent(simdscalar const& vI,
                                                  simdscalar const& vJ,
                                                  const float*      pInterpBuffer)
{
    const float* pInterpA = &pInterpBuffer[Attrib * 3 * numComponents + 0 + Comp];
    const float* pInterpB = &pInterpBuffer[Attrib * 3 * numComponents + numComponents + Comp];
    const float* pInterpC = &pInterpBuffer[Attrib * 3 * numComponents + numComponents * 2 + Comp];

    if ((pInterpA[0] == pInterpB[0]) && (pInterpA[0] == pInterpC[0]))
    {
        // Ensure constant attribs are constant.  Required for proper
        // 3D resource copies.
        return _simd_broadcast_ss(pInterpA);
    }

    simdscalar vA = _simd_broadcast_ss(pInterpA);
    simdscalar vB = _simd_broadcast_ss(pInterpB);
    simdscalar vC = _simd_broadcast_ss(pInterpC);

    simdscalar vk = _simd_sub_ps(_simd_sub_ps(_simd_set1_ps(1.0f), vI), vJ);
    vC            = _simd_mul_ps(vk, vC);

    return vplaneps(vA, vB, vC, vI, vJ);
}

//////////////////////////////////////////////////////////////////////////
/// @brief Interpolates a single component (flat shade).
/// @param pInterpBuffer - pointer to attribute barycentric coeffs
template <UINT Attrib, UINT Comp, UINT numComponents = 4>
static SIMDINLINE simdscalar InterpolateComponentFlat(const float* pInterpBuffer)
{
    const float* pInterpA = &pInterpBuffer[Attrib * 3 * numComponents + 0 + Comp];

    simdscalar vA = _simd_broadcast_ss(pInterpA);

    return vA;
}

//////////////////////////////////////////////////////////////////////////
/// @brief Interpolates a single component (flat shade).
/// @param pInterpBuffer - pointer to attribute barycentric coeffs
template <UINT Attrib, UINT Comp, UINT numComponents = 4>
static SIMDINLINE simdscalari InterpolateComponentFlatInt(const uint32_t* pInterpBuffer)
{
    const uint32_t interpA = pInterpBuffer[Attrib * 3 * numComponents + 0 + Comp];

    simdscalari vA = _simd_set1_epi32(interpA);

    return vA;
}

//////////////////////////////////////////////////////////////////////////
/// @brief Interpolates a single component.
/// @param vI - barycentric I
/// @param vJ - barycentric J
/// @param pInterpBuffer - pointer to attribute barycentric coeffs
template <UINT Attrib, UINT Comp, UINT numComponents = 4>
static SIMDINLINE simd4scalar InterpolateComponent(simd4scalar const& vI,
                                                   simd4scalar const& vJ,
                                                   const float*       pInterpBuffer)
{
    const float* pInterpA = &pInterpBuffer[Attrib * 3 * numComponents + 0 + Comp];
    const float* pInterpB = &pInterpBuffer[Attrib * 3 * numComponents + numComponents + Comp];
    const float* pInterpC = &pInterpBuffer[Attrib * 3 * numComponents + numComponents * 2 + Comp];

    if ((pInterpA[0] == pInterpB[0]) && (pInterpA[0] == pInterpC[0]))
    {
        // Ensure constant attribs are constant.  Required for proper
        // 3D resource copies.
        return SIMD128::broadcast_ss(pInterpA);
    }

    simd4scalar vA = SIMD128::broadcast_ss(pInterpA);
    simd4scalar vB = SIMD128::broadcast_ss(pInterpB);
    simd4scalar vC = SIMD128::broadcast_ss(pInterpC);

    simd4scalar vk = SIMD128::sub_ps(SIMD128::sub_ps(SIMD128::set1_ps(1.0f), vI), vJ);
    vC             = SIMD128::mul_ps(vk, vC);

    return vplaneps(vA, vB, vC, vI, vJ);
}

static SIMDINLINE simd4scalar _simd128_abs_ps(simd4scalar const& a)
{
    simd4scalari ai = SIMD128::castps_si(a);
    return SIMD128::castsi_ps(SIMD128::and_si(ai, SIMD128::set1_epi32(0x7fffffff)));
}

static SIMDINLINE simdscalar _simd_abs_ps(simdscalar const& a)
{
    simdscalari ai = _simd_castps_si(a);
    return _simd_castsi_ps(_simd_and_si(ai, _simd_set1_epi32(0x7fffffff)));
}

#include "simd16intrin.h"

#endif //__SWR_SIMDINTRIN_H__
