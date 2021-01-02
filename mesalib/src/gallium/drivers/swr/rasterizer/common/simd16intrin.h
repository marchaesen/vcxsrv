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

#ifndef __SWR_SIMD16INTRIN_H__
#define __SWR_SIMD16INTRIN_H__

#if KNOB_SIMD16_WIDTH == 16
typedef SIMD512 SIMD16;
#else
#error Unsupported vector width
#endif // KNOB_SIMD16_WIDTH == 16

#define _simd16_setzero_ps SIMD16::setzero_ps
#define _simd16_setzero_si SIMD16::setzero_si
#define _simd16_set1_ps SIMD16::set1_ps
#define _simd16_set1_epi8 SIMD16::set1_epi8
#define _simd16_set1_epi32 SIMD16::set1_epi32
#define _simd16_set_ps SIMD16::set_ps
#define _simd16_set_epi32 SIMD16::set_epi32
#define _simd16_load_ps SIMD16::load_ps
#define _simd16_loadu_ps SIMD16::loadu_ps
#if 1
#define _simd16_load1_ps SIMD16::broadcast_ss
#endif
#define _simd16_load_si SIMD16::load_si
#define _simd16_loadu_si SIMD16::loadu_si
#define _simd16_broadcast_ss(m) SIMD16::broadcast_ss((float const*)m)
#define _simd16_store_ps SIMD16::store_ps
#define _simd16_store_si SIMD16::store_si
#define _simd16_extract_ps(a, imm8) SIMD16::extract_ps<imm8>(a)
#define _simd16_extract_si(a, imm8) SIMD16::extract_si<imm8>(a)
#define _simd16_insert_ps(a, b, imm8) SIMD16::insert_ps<imm8>(a, b)
#define _simd16_insert_si(a, b, imm8) SIMD16::insert_si<imm8>(a, b)
#define _simd16_maskstore_ps SIMD16::maskstore_ps
#define _simd16_blend_ps(a, b, mask) SIMD16::blend_ps<mask>(a, b)
#define _simd16_blendv_ps SIMD16::blendv_ps
#define _simd16_blendv_epi32 SIMD16::blendv_epi32
#define _simd16_mul_ps SIMD16::mul_ps
#define _simd16_div_ps SIMD16::div_ps
#define _simd16_add_ps SIMD16::add_ps
#define _simd16_sub_ps SIMD16::sub_ps
#define _simd16_rsqrt_ps SIMD16::rsqrt_ps
#define _simd16_min_ps SIMD16::min_ps
#define _simd16_max_ps SIMD16::max_ps
#define _simd16_movemask_ps SIMD16::movemask_ps
#define _simd16_movemask_pd SIMD16::movemask_pd
#define _simd16_cvtps_epi32 SIMD16::cvtps_epi32
#define _simd16_cvttps_epi32 SIMD16::cvttps_epi32
#define _simd16_cvtepi32_ps SIMD16::cvtepi32_ps
#define _simd16_cmp_ps(a, b, comp) SIMD16::cmp_ps<SIMD16::CompareType(comp)>(a, b)
#define _simd16_cmplt_ps SIMD16::cmplt_ps
#define _simd16_cmpgt_ps SIMD16::cmpgt_ps
#define _simd16_cmpneq_ps SIMD16::cmpneq_ps
#define _simd16_cmpeq_ps SIMD16::cmpeq_ps
#define _simd16_cmpge_ps SIMD16::cmpge_ps
#define _simd16_cmple_ps SIMD16::cmple_ps
#define _simd16_castsi_ps SIMD16::castsi_ps
#define _simd16_castps_si SIMD16::castps_si
#define _simd16_castsi_pd SIMD16::castsi_pd
#define _simd16_castpd_si SIMD16::castpd_si
#define _simd16_castpd_ps SIMD16::castpd_ps
#define _simd16_castps_pd SIMD16::castps_pd
#define _simd16_and_ps SIMD16::and_ps
#define _simd16_andnot_ps SIMD16::andnot_ps
#define _simd16_or_ps SIMD16::or_ps
#define _simd16_xor_ps SIMD16::xor_ps
#define _simd16_round_ps(a, mode) SIMD16::round_ps<SIMD16::RoundMode(mode)>(a)
#define _simd16_mul_epi32 SIMD16::mul_epi32
#define _simd16_mullo_epi32 SIMD16::mullo_epi32
#define _simd16_sub_epi32 SIMD16::sub_epi32
#define _simd16_sub_epi64 SIMD16::sub_epi64
#define _simd16_min_epi32 SIMD16::min_epi32
#define _simd16_max_epi32 SIMD16::max_epi32
#define _simd16_min_epu32 SIMD16::min_epu32
#define _simd16_max_epu32 SIMD16::max_epu32
#define _simd16_add_epi32 SIMD16::add_epi32
#define _simd16_and_si SIMD16::and_si
#define _simd16_andnot_si SIMD16::andnot_si
#define _simd16_or_si SIMD16::or_si
#define _simd16_xor_si SIMD16::xor_si
#define _simd16_cmpeq_epi32 SIMD16::cmpeq_epi32
#define _simd16_cmpgt_epi32 SIMD16::cmpgt_epi32
#define _simd16_cmplt_epi32 SIMD16::cmplt_epi32
#define _simd16_testz_ps SIMD16::testz_ps
#define _simd16_unpacklo_ps SIMD16::unpacklo_ps
#define _simd16_unpackhi_ps SIMD16::unpackhi_ps
#define _simd16_unpacklo_pd SIMD16::unpacklo_pd
#define _simd16_unpackhi_pd SIMD16::unpackhi_pd
#define _simd16_unpacklo_epi8 SIMD16::unpacklo_epi8
#define _simd16_unpackhi_epi8 SIMD16::unpackhi_epi8
#define _simd16_unpacklo_epi16 SIMD16::unpacklo_epi16
#define _simd16_unpackhi_epi16 SIMD16::unpackhi_epi16
#define _simd16_unpacklo_epi32 SIMD16::unpacklo_epi32
#define _simd16_unpackhi_epi32 SIMD16::unpackhi_epi32
#define _simd16_unpacklo_epi64 SIMD16::unpacklo_epi64
#define _simd16_unpackhi_epi64 SIMD16::unpackhi_epi64
#define _simd16_slli_epi32(a, i) SIMD16::slli_epi32<i>(a)
#define _simd16_srli_epi32(a, i) SIMD16::srli_epi32<i>(a)
#define _simd16_srai_epi32(a, i) SIMD16::srai_epi32<i>(a)
#define _simd16_fmadd_ps SIMD16::fmadd_ps
#define _simd16_fmsub_ps SIMD16::fmsub_ps
#define _simd16_adds_epu8 SIMD16::adds_epu8
#define _simd16_subs_epu8 SIMD16::subs_epu8
#define _simd16_add_epi8 SIMD16::add_epi8
#define _simd16_shuffle_epi8 SIMD16::shuffle_epi8

#define _simd16_i32gather_ps(m, index, scale) \
    SIMD16::i32gather_ps<SIMD16::ScaleFactor(scale)>(m, index)
#define _simd16_mask_i32gather_ps(a, m, index, mask, scale) \
    SIMD16::mask_i32gather_ps<SIMD16::ScaleFactor(scale)>(a, m, index, mask)

#define _simd16_abs_epi32 SIMD16::abs_epi32

#define _simd16_cmpeq_epi64 SIMD16::cmpeq_epi64
#define _simd16_cmpgt_epi64 SIMD16::cmpgt_epi64
#define _simd16_cmpeq_epi16 SIMD16::cmpeq_epi16
#define _simd16_cmpgt_epi16 SIMD16::cmpgt_epi16
#define _simd16_cmpeq_epi8 SIMD16::cmpeq_epi8
#define _simd16_cmpgt_epi8 SIMD16::cmpgt_epi8

#define _simd16_permute_ps_i(a, i) SIMD16::permute_ps<i>(a)
#define _simd16_permute_ps SIMD16::permute_ps
#define _simd16_permute_epi32 SIMD16::permute_epi32
#define _simd16_sllv_epi32 SIMD16::sllv_epi32
#define _simd16_srlv_epi32 SIMD16::sllv_epi32
#define _simd16_permute2f128_ps(a, b, i) SIMD16::permute2f128_ps<i>(a, b)
#define _simd16_permute2f128_pd(a, b, i) SIMD16::permute2f128_pd<i>(a, b)
#define _simd16_permute2f128_si(a, b, i) SIMD16::permute2f128_si<i>(a, b)
#define _simd16_shuffle_ps(a, b, i) SIMD16::shuffle_ps<i>(a, b)
#define _simd16_shuffle_pd(a, b, i) SIMD16::shuffle_pd<i>(a, b)
#define _simd16_shuffle_epi32(a, b, imm8) SIMD16::shuffle_epi32<imm8>(a, b)
#define _simd16_shuffle_epi64(a, b, imm8) SIMD16::shuffle_epi64<imm8>(a, b)
#define _simd16_cvtepu8_epi16 SIMD16::cvtepu8_epi16
#define _simd16_cvtepu8_epi32 SIMD16::cvtepu8_epi32
#define _simd16_cvtepu16_epi32 SIMD16::cvtepu16_epi32
#define _simd16_cvtepu16_epi64 SIMD16::cvtepu16_epi64
#define _simd16_cvtepu32_epi64 SIMD16::cvtepu32_epi64
#define _simd16_packus_epi16 SIMD16::packus_epi16
#define _simd16_packs_epi16 SIMD16::packs_epi16
#define _simd16_packus_epi32 SIMD16::packus_epi32
#define _simd16_packs_epi32 SIMD16::packs_epi32
#define _simd16_cmplt_ps_mask SIMD16::cmp_ps_mask<SIMD16::CompareType::LT_OQ>
#define _simd16_cmpeq_ps_mask SIMD16::cmp_ps_mask<SIMD16::CompareType::EQ_OQ>
#define _simd16_int2mask(mask) simd16mask(mask)
#define _simd16_mask2int(mask) int(mask)
#define _simd16_vmask_ps SIMD16::vmask_ps

#endif //__SWR_SIMD16INTRIN_H_
