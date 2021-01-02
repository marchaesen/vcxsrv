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
#if !defined(__SIMD_LIB_AVX512_HPP__)
#error Do not include this file directly, use "simdlib.hpp" instead.
#endif

#if defined(__GNUC__) && !defined(__clang__) && !defined(__INTEL_COMPILER)
// gcc as of 7.1 was missing these intrinsics
#ifndef _mm512_cmpneq_ps_mask
#define _mm512_cmpneq_ps_mask(a, b) _mm512_cmp_ps_mask((a), (b), _CMP_NEQ_UQ)
#endif

#ifndef _mm512_cmplt_ps_mask
#define _mm512_cmplt_ps_mask(a, b) _mm512_cmp_ps_mask((a), (b), _CMP_LT_OS)
#endif

#ifndef _mm512_cmplt_pd_mask
#define _mm512_cmplt_pd_mask(a, b) _mm512_cmp_pd_mask((a), (b), _CMP_LT_OS)
#endif

#endif

//============================================================================
// SIMD16 AVX512 (F) implementation (compatible with Knights and Core
// processors)
//
//============================================================================

static const int TARGET_SIMD_WIDTH = 16;
using SIMD256T                     = SIMD256Impl::AVX2Impl;

#define SIMD_WRAPPER_1_(op, intrin) \
    static SIMDINLINE Float SIMDCALL op(Float a) { return intrin(a); }

#define SIMD_WRAPPER_1(op) SIMD_WRAPPER_1_(op, _mm512_##op)

#define SIMD_WRAPPER_2_(op, intrin) \
    static SIMDINLINE Float SIMDCALL op(Float a, Float b) { return _mm512_##intrin(a, b); }
#define SIMD_WRAPPER_2(op) SIMD_WRAPPER_2_(op, op)

#define SIMD_WRAPPERI_2_(op, intrin)                                          \
    static SIMDINLINE Float SIMDCALL op(Float a, Float b)                     \
    {                                                                         \
        return _mm512_castsi512_ps(                                           \
            _mm512_##intrin(_mm512_castps_si512(a), _mm512_castps_si512(b))); \
    }

#define SIMD_DWRAPPER_2(op) \
    static SIMDINLINE Double SIMDCALL op(Double a, Double b) { return _mm512_##op(a, b); }

#define SIMD_WRAPPER_2I_(op, intrin)                      \
    template <int ImmT>                                   \
    static SIMDINLINE Float SIMDCALL op(Float a, Float b) \
    {                                                     \
        return _mm512_##intrin(a, b, ImmT);               \
    }
#define SIMD_WRAPPER_2I(op) SIMD_WRAPPER_2I_(op, op)

#define SIMD_DWRAPPER_2I_(op, intrin)                        \
    template <int ImmT>                                      \
    static SIMDINLINE Double SIMDCALL op(Double a, Double b) \
    {                                                        \
        return _mm512_##intrin(a, b, ImmT);                  \
    }
#define SIMD_DWRAPPER_2I(op) SIMD_DWRAPPER_2I_(op, op)

#define SIMD_WRAPPER_3(op) \
    static SIMDINLINE Float SIMDCALL op(Float a, Float b, Float c) { return _mm512_##op(a, b, c); }

#define SIMD_IWRAPPER_1(op) \
    static SIMDINLINE Integer SIMDCALL op(Integer a) { return _mm512_##op(a); }
#define SIMD_IWRAPPER_1_8(op) \
    static SIMDINLINE Integer SIMDCALL op(SIMD256Impl::Integer a) { return _mm512_##op(a); }

#define SIMD_IWRAPPER_1_4(op) \
    static SIMDINLINE Integer SIMDCALL op(SIMD128Impl::Integer a) { return _mm512_##op(a); }

#define SIMD_IWRAPPER_1I_(op, intrin)                \
    template <int ImmT>                              \
    static SIMDINLINE Integer SIMDCALL op(Integer a) \
    {                                                \
        return intrin(a, ImmT);                      \
    }
#define SIMD_IWRAPPER_1I(op) SIMD_IWRAPPER_1I_(op, _mm512_##op)

#define SIMD_IWRAPPER_2_(op, intrin) \
    static SIMDINLINE Integer SIMDCALL op(Integer a, Integer b) { return _mm512_##intrin(a, b); }
#define SIMD_IWRAPPER_2(op) SIMD_IWRAPPER_2_(op, op)

#define SIMD_IWRAPPER_2_CMP(op, cmp) \
    static SIMDINLINE Integer SIMDCALL op(Integer a, Integer b) { return cmp(a, b); }

#define SIMD_IFWRAPPER_2(op, intrin)                                   \
    static SIMDINLINE Integer SIMDCALL op(Integer a, Integer b)        \
    {                                                                  \
        return castps_si(_mm512_##intrin(castsi_ps(a), castsi_ps(b))); \
    }

#define SIMD_IWRAPPER_2I_(op, intrin)                           \
    template <int ImmT>                                         \
    static SIMDINLINE Integer SIMDCALL op(Integer a, Integer b) \
    {                                                           \
        return _mm512_##intrin(a, b, ImmT);                     \
    }
#define SIMD_IWRAPPER_2I(op) SIMD_IWRAPPER_2I_(op, op)

private:
static SIMDINLINE Integer vmask(__mmask16 m)
{
    return _mm512_maskz_set1_epi32(m, -1);
}

static SIMDINLINE Integer vmask(__mmask8 m)
{
    return _mm512_maskz_set1_epi64(m, -1LL);
}

public:
//-----------------------------------------------------------------------
// Single precision floating point arithmetic operations
//-----------------------------------------------------------------------
SIMD_WRAPPER_2(add_ps);                       // return a + b
SIMD_WRAPPER_2(div_ps);                       // return a / b
SIMD_WRAPPER_3(fmadd_ps);                     // return (a * b) + c
SIMD_WRAPPER_3(fmsub_ps);                     // return (a * b) - c
SIMD_WRAPPER_2(max_ps);                       // return (a > b) ? a : b
SIMD_WRAPPER_2(min_ps);                       // return (a < b) ? a : b
SIMD_WRAPPER_2(mul_ps);                       // return a * b
SIMD_WRAPPER_1_(rcp_ps, _mm512_rcp14_ps);     // return 1.0f / a
SIMD_WRAPPER_1_(rsqrt_ps, _mm512_rsqrt14_ps); // return 1.0f / sqrt(a)
SIMD_WRAPPER_2(sub_ps);                       // return a - b

template <RoundMode RMT>
static SIMDINLINE Float SIMDCALL round_ps(Float a)
{
    return _mm512_roundscale_ps(a, static_cast<int>(RMT));
}

static SIMDINLINE Float SIMDCALL ceil_ps(Float a)
{
    return round_ps<RoundMode::CEIL_NOEXC>(a);
}
static SIMDINLINE Float SIMDCALL floor_ps(Float a)
{
    return round_ps<RoundMode::FLOOR_NOEXC>(a);
}

//-----------------------------------------------------------------------
// Integer (various width) arithmetic operations
//-----------------------------------------------------------------------
SIMD_IWRAPPER_1(abs_epi32); // return absolute_value(a) (int32)
SIMD_IWRAPPER_2(add_epi32); // return a + b (int32)
// SIMD_IWRAPPER_2(add_epi8);  // return a + b (int8)
// SIMD_IWRAPPER_2(adds_epu8); // return ((a + b) > 0xff) ? 0xff : (a + b) (uint8)
SIMD_IWRAPPER_2(max_epi32); // return (a > b) ? a : b (int32)
SIMD_IWRAPPER_2(max_epu32); // return (a > b) ? a : b (uint32)
SIMD_IWRAPPER_2(min_epi32); // return (a < b) ? a : b (int32)
SIMD_IWRAPPER_2(min_epu32); // return (a < b) ? a : b (uint32)
SIMD_IWRAPPER_2(mul_epi32); // return a * b (int32)

// return (a * b) & 0xFFFFFFFF
//
// Multiply the packed 32-bit integers in a and b, producing intermediate 64-bit integers,
// and store the low 32 bits of the intermediate integers in dst.
SIMD_IWRAPPER_2(mullo_epi32);
SIMD_IWRAPPER_2(sub_epi32); // return a - b (int32)
SIMD_IWRAPPER_2(sub_epi64); // return a - b (int64)
// SIMD_IWRAPPER_2(subs_epu8); // return (b > a) ? 0 : (a - b) (uint8)

//-----------------------------------------------------------------------
// Logical operations
//-----------------------------------------------------------------------
SIMD_IWRAPPER_2_(and_si, and_si512);       // return a & b       (int)
SIMD_IWRAPPER_2_(andnot_si, andnot_si512); // return (~a) & b    (int)
SIMD_IWRAPPER_2_(or_si, or_si512);         // return a | b       (int)
SIMD_IWRAPPER_2_(xor_si, xor_si512);       // return a ^ b       (int)

// SIMD_WRAPPER_2(and_ps);                     // return a & b       (float treated as int)
// SIMD_WRAPPER_2(andnot_ps);                  // return (~a) & b    (float treated as int)
// SIMD_WRAPPER_2(or_ps);                      // return a | b       (float treated as int)
// SIMD_WRAPPER_2(xor_ps);                     // return a ^ b       (float treated as int)

//-----------------------------------------------------------------------
// Shift operations
//-----------------------------------------------------------------------
SIMD_IWRAPPER_1I(slli_epi32); // return a << ImmT
SIMD_IWRAPPER_2(sllv_epi32);
SIMD_IWRAPPER_1I(srai_epi32); // return a >> ImmT   (int32)
SIMD_IWRAPPER_1I(srli_epi32); // return a >> ImmT   (uint32)

#if 0
SIMD_IWRAPPER_1I_(srli_si, srli_si512);     // return a >> (ImmT*8) (uint)

template<int ImmT>                              // same as srli_si, but with Float cast to int
static SIMDINLINE Float SIMDCALL srlisi_ps(Float a)
{
    return castsi_ps(srli_si<ImmT>(castps_si(a)));
}
#endif

SIMD_IWRAPPER_2(srlv_epi32);

//-----------------------------------------------------------------------
// Conversion operations
//-----------------------------------------------------------------------
static SIMDINLINE Float SIMDCALL castpd_ps(Double a) // return *(Float*)(&a)
{
    return _mm512_castpd_ps(a);
}

static SIMDINLINE Integer SIMDCALL castps_si(Float a) // return *(Integer*)(&a)
{
    return _mm512_castps_si512(a);
}

static SIMDINLINE Double SIMDCALL castsi_pd(Integer a) // return *(Double*)(&a)
{
    return _mm512_castsi512_pd(a);
}

static SIMDINLINE Double SIMDCALL castps_pd(Float a) // return *(Double*)(&a)
{
    return _mm512_castps_pd(a);
}

static SIMDINLINE Integer SIMDCALL castpd_si(Double a) // return *(Integer*)(&a)
{
    return _mm512_castpd_si512(a);
}

static SIMDINLINE Float SIMDCALL castsi_ps(Integer a) // return *(Float*)(&a)
{
    return _mm512_castsi512_ps(a);
}

static SIMDINLINE Float SIMDCALL cvtepi32_ps(Integer a) // return (float)a    (int32 --> float)
{
    return _mm512_cvtepi32_ps(a);
}

// SIMD_IWRAPPER_1_8(cvtepu8_epi16);     // return (int16)a    (uint8 --> int16)
SIMD_IWRAPPER_1_4(cvtepu8_epi32);  // return (int32)a    (uint8 --> int32)
SIMD_IWRAPPER_1_8(cvtepu16_epi32); // return (int32)a    (uint16 --> int32)
SIMD_IWRAPPER_1_4(cvtepu16_epi64); // return (int64)a    (uint16 --> int64)
SIMD_IWRAPPER_1_8(cvtepu32_epi64); // return (int64)a    (uint32 --> int64)

static SIMDINLINE Integer SIMDCALL cvtps_epi32(Float a) // return (int32)a    (float --> int32)
{
    return _mm512_cvtps_epi32(a);
}

static SIMDINLINE Integer SIMDCALL
                          cvttps_epi32(Float a) // return (int32)a    (rnd_to_zero(float) --> int32)
{
    return _mm512_cvttps_epi32(a);
}

//-----------------------------------------------------------------------
// Comparison operations
//-----------------------------------------------------------------------
template <CompareType CmpTypeT>
static SIMDINLINE Mask SIMDCALL cmp_ps_mask(Float a, Float b)
{
    return _mm512_cmp_ps_mask(a, b, static_cast<const int>(CmpTypeT));
}

template <CompareType CmpTypeT>
static SIMDINLINE Float SIMDCALL cmp_ps(Float a, Float b) // return a (CmpTypeT) b
{
    // Legacy vector mask generator
    __mmask16 result = cmp_ps_mask<CmpTypeT>(a, b);
    return castsi_ps(vmask(result));
}

static SIMDINLINE Float SIMDCALL cmplt_ps(Float a, Float b)
{
    return cmp_ps<CompareType::LT_OQ>(a, b);
}
static SIMDINLINE Float SIMDCALL cmpgt_ps(Float a, Float b)
{
    return cmp_ps<CompareType::GT_OQ>(a, b);
}
static SIMDINLINE Float SIMDCALL cmpneq_ps(Float a, Float b)
{
    return cmp_ps<CompareType::NEQ_OQ>(a, b);
}
static SIMDINLINE Float SIMDCALL cmpeq_ps(Float a, Float b)
{
    return cmp_ps<CompareType::EQ_OQ>(a, b);
}
static SIMDINLINE Float SIMDCALL cmpge_ps(Float a, Float b)
{
    return cmp_ps<CompareType::GE_OQ>(a, b);
}
static SIMDINLINE Float SIMDCALL cmple_ps(Float a, Float b)
{
    return cmp_ps<CompareType::LE_OQ>(a, b);
}

template <CompareTypeInt CmpTypeT>
static SIMDINLINE Integer SIMDCALL cmp_epi32(Integer a, Integer b)
{
    // Legacy vector mask generator
    __mmask16 result = _mm512_cmp_epi32_mask(a, b, static_cast<const int>(CmpTypeT));
    return vmask(result);
}
template <CompareTypeInt CmpTypeT>
static SIMDINLINE Integer SIMDCALL cmp_epi64(Integer a, Integer b)
{
    // Legacy vector mask generator
    __mmask8 result = _mm512_cmp_epi64_mask(a, b, static_cast<const int>(CmpTypeT));
    return vmask(result);
}

// SIMD_IWRAPPER_2_CMP(cmpeq_epi8,  cmp_epi8<CompareTypeInt::EQ>);    // return a == b (int8)
// SIMD_IWRAPPER_2_CMP(cmpeq_epi16, cmp_epi16<CompareTypeInt::EQ>);   // return a == b (int16)
SIMD_IWRAPPER_2_CMP(cmpeq_epi32, cmp_epi32<CompareTypeInt::EQ>); // return a == b (int32)
SIMD_IWRAPPER_2_CMP(cmpeq_epi64, cmp_epi64<CompareTypeInt::EQ>); // return a == b (int64)
// SIMD_IWRAPPER_2_CMP(cmpgt_epi8,  cmp_epi8<CompareTypeInt::GT>);    // return a > b (int8)
// SIMD_IWRAPPER_2_CMP(cmpgt_epi16, cmp_epi16<CompareTypeInt::GT>);   // return a > b (int16)
SIMD_IWRAPPER_2_CMP(cmpgt_epi32, cmp_epi32<CompareTypeInt::GT>); // return a > b (int32)
SIMD_IWRAPPER_2_CMP(cmpgt_epi64, cmp_epi64<CompareTypeInt::GT>); // return a > b (int64)
SIMD_IWRAPPER_2_CMP(cmplt_epi32, cmp_epi32<CompareTypeInt::LT>); // return a < b (int32)

static SIMDINLINE bool SIMDCALL testz_ps(Float a,
                                         Float b) // return all_lanes_zero(a & b) ? 1 : 0 (float)
{
    return (0 == static_cast<int>(_mm512_test_epi32_mask(castps_si(a), castps_si(b))));
}

static SIMDINLINE bool SIMDCALL testz_si(Integer a,
                                         Integer b) // return all_lanes_zero(a & b) ? 1 : 0 (int)
{
    return (0 == static_cast<int>(_mm512_test_epi32_mask(a, b)));
}

//-----------------------------------------------------------------------
// Blend / shuffle / permute operations
//-----------------------------------------------------------------------
template <int ImmT>
static SIMDINLINE Float blend_ps(Float a, Float b) // return ImmT ? b : a  (float)
{
    return _mm512_mask_blend_ps(__mmask16(ImmT), a, b);
}

template <int ImmT>
static SIMDINLINE Integer blend_epi32(Integer a, Integer b) // return ImmT ? b : a  (int32)
{
    return _mm512_mask_blend_epi32(__mmask16(ImmT), a, b);
}

static SIMDINLINE Float blendv_ps(Float a, Float b, Float mask) // return mask ? b : a  (float)
{
    return _mm512_mask_blend_ps(__mmask16(movemask_ps(mask)), a, b);
}

static SIMDINLINE Integer SIMDCALL blendv_epi32(Integer a,
                                                Integer b,
                                                Float   mask) // return mask ? b : a (int)
{
    return castps_si(blendv_ps(castsi_ps(a), castsi_ps(b), mask));
}

static SIMDINLINE Integer SIMDCALL blendv_epi32(Integer a,
                                                Integer b,
                                                Integer mask) // return mask ? b : a (int)
{
    return castps_si(blendv_ps(castsi_ps(a), castsi_ps(b), castsi_ps(mask)));
}

static SIMDINLINE Float SIMDCALL
                        broadcast_ss(float const* p) // return *p (all elements in vector get same value)
{
    return _mm512_set1_ps(*p);
}

template <int imm>
static SIMDINLINE SIMD256Impl::Float SIMDCALL extract_ps(Float a)
{
    return _mm256_castpd_ps(_mm512_extractf64x4_pd(_mm512_castps_pd(a), imm));
}

template <int imm>
static SIMDINLINE SIMD256Impl::Double SIMDCALL extract_pd(Double a)
{
    return _mm512_extractf64x4_pd(a, imm);
}

template <int imm>
static SIMDINLINE SIMD256Impl::Integer SIMDCALL extract_si(Integer a)
{
    return _mm512_extracti64x4_epi64(a, imm);
}

template <int imm>
static SIMDINLINE Float SIMDCALL insert_ps(Float a, SIMD256Impl::Float b)
{
    return _mm512_castpd_ps(_mm512_insertf64x4(_mm512_castps_pd(a), _mm256_castps_pd(b), imm));
}

template <int imm>
static SIMDINLINE Double SIMDCALL insert_pd(Double a, SIMD256Impl::Double b)
{
    return _mm512_insertf64x4(a, b, imm);
}

template <int imm>
static SIMDINLINE Integer SIMDCALL insert_si(Integer a, SIMD256Impl::Integer b)
{
    return _mm512_inserti64x4(a, b, imm);
}

// SIMD_IWRAPPER_2(packs_epi16);   // See documentation for _mm512_packs_epi16 and
// _mm512_packs_epi16 SIMD_IWRAPPER_2(packs_epi32);   // See documentation for _mm512_packs_epi32
// and _mm512_packs_epi32 SIMD_IWRAPPER_2(packus_epi16);  // See documentation for
// _mm512_packus_epi16 and _mm512_packus_epi16 SIMD_IWRAPPER_2(packus_epi32);  // See documentation
// for _mm512_packus_epi32 and _mm512_packus_epi32

template <int ImmT>
static SIMDINLINE Float SIMDCALL permute_ps(Float const& a)
{
    return _mm512_permute_ps(a, ImmT);
}

static SIMDINLINE Integer SIMDCALL
                          permute_epi32(Integer a, Integer swiz) // return a[swiz[i]] for each 32-bit lane i (float)
{
    return _mm512_permutexvar_epi32(swiz, a);
}

static SIMDINLINE Float SIMDCALL
                        permute_ps(Float a, Integer swiz) // return a[swiz[i]] for each 32-bit lane i (float)
{
    return _mm512_permutexvar_ps(swiz, a);
}

SIMD_WRAPPER_2I_(permute2f128_ps, shuffle_f32x4);
SIMD_DWRAPPER_2I_(permute2f128_pd, shuffle_f64x2);
SIMD_IWRAPPER_2I_(permute2f128_si, shuffle_i32x4);

SIMD_IWRAPPER_1I(shuffle_epi32);

// SIMD_IWRAPPER_2(shuffle_epi8);
SIMD_DWRAPPER_2I(shuffle_pd);
SIMD_WRAPPER_2I(shuffle_ps);

template <int ImmT>
static SIMDINLINE Integer SIMDCALL shuffle_epi64(Integer a, Integer b)
{
    return castpd_si(shuffle_pd<ImmT>(castsi_pd(a), castsi_pd(b)));
}

SIMD_IWRAPPER_2(unpackhi_epi16);

// SIMD_IFWRAPPER_2(unpackhi_epi32, _mm512_unpackhi_ps);
static SIMDINLINE Integer SIMDCALL unpackhi_epi32(Integer a, Integer b)
{
    return castps_si(_mm512_unpackhi_ps(castsi_ps(a), castsi_ps(b)));
}

SIMD_IWRAPPER_2(unpackhi_epi64);
// SIMD_IWRAPPER_2(unpackhi_epi8);
SIMD_DWRAPPER_2(unpackhi_pd);
SIMD_WRAPPER_2(unpackhi_ps);
// SIMD_IWRAPPER_2(unpacklo_epi16);
SIMD_IFWRAPPER_2(unpacklo_epi32, unpacklo_ps);
SIMD_IWRAPPER_2(unpacklo_epi64);
// SIMD_IWRAPPER_2(unpacklo_epi8);
SIMD_DWRAPPER_2(unpacklo_pd);
SIMD_WRAPPER_2(unpacklo_ps);

//-----------------------------------------------------------------------
// Load / store operations
//-----------------------------------------------------------------------
template <ScaleFactor ScaleT = ScaleFactor::SF_1>
static SIMDINLINE Float SIMDCALL
                        i32gather_ps(float const* p, Integer idx) // return *(float*)(((int8*)p) + (idx * ScaleT))
{
    return _mm512_i32gather_ps(idx, p, static_cast<int>(ScaleT));
}

static SIMDINLINE Float SIMDCALL
                        load1_ps(float const* p) // return *p    (broadcast 1 value to all elements)
{
    return broadcast_ss(p);
}

static SIMDINLINE Float SIMDCALL
                        load_ps(float const* p) // return *p    (loads SIMD width elements from memory)
{
    return _mm512_load_ps(p);
}

static SIMDINLINE Integer SIMDCALL load_si(Integer const* p) // return *p
{
    return _mm512_load_si512(&p->v);
}

static SIMDINLINE Float SIMDCALL
                        loadu_ps(float const* p) // return *p    (same as load_ps but allows for unaligned mem)
{
    return _mm512_loadu_ps(p);
}

static SIMDINLINE Integer SIMDCALL
                          loadu_si(Integer const* p) // return *p    (same as load_si but allows for unaligned mem)
{
    return _mm512_loadu_si512(p);
}

// for each element: (mask & (1 << 31)) ? (i32gather_ps<ScaleT>(p, idx), mask = 0) : old
template <ScaleFactor ScaleT = ScaleFactor::SF_1>
static SIMDINLINE Float SIMDCALL
                        mask_i32gather_ps(Float old, float const* p, Integer idx, Float mask)
{
    __mmask16 k = _mm512_test_epi32_mask(castps_si(mask), set1_epi32(0x80000000));

    return _mm512_mask_i32gather_ps(old, k, idx, p, static_cast<int>(ScaleT));
}

static SIMDINLINE void SIMDCALL maskstore_ps(float* p, Integer mask, Float src)
{
    Mask m = _mm512_cmplt_epi32_mask(mask, setzero_si());
    _mm512_mask_store_ps(p, m, src);
}

// static SIMDINLINE uint64_t SIMDCALL movemask_epi8(Integer a)
//{
//    __mmask64 m = _mm512_cmplt_epi8_mask(a, setzero_si());
//    return static_cast<uint64_t>(m);
//}

static SIMDINLINE uint32_t SIMDCALL movemask_pd(Double a)
{
    __mmask8 m = _mm512_test_epi64_mask(castpd_si(a), set1_epi64(0x8000000000000000LL));
    return static_cast<uint32_t>(m);
}
static SIMDINLINE uint32_t SIMDCALL movemask_ps(Float a)
{
    __mmask16 m = _mm512_test_epi32_mask(castps_si(a), set1_epi32(0x80000000));
    return static_cast<uint32_t>(m);
}

static SIMDINLINE Integer SIMDCALL set1_epi64(long long i) // return i (all elements are same value)
{
    return _mm512_set1_epi64(i);
}

static SIMDINLINE Integer SIMDCALL set1_epi32(int i) // return i (all elements are same value)
{
    return _mm512_set1_epi32(i);
}

static SIMDINLINE Integer SIMDCALL set1_epi8(char i) // return i (all elements are same value)
{
    return _mm512_set1_epi8(i);
}

static SIMDINLINE Float SIMDCALL set1_ps(float f) // return f (all elements are same value)
{
    return _mm512_set1_ps(f);
}

static SIMDINLINE Double SIMDCALL setzero_pd() // return 0 (double)
{
    return _mm512_setzero_pd();
}

static SIMDINLINE Float SIMDCALL setzero_ps() // return 0 (float)
{
    return _mm512_setzero_ps();
}

static SIMDINLINE Integer SIMDCALL setzero_si() // return 0 (integer)
{
    return _mm512_setzero_si512();
}

static SIMDINLINE void SIMDCALL
                       store_ps(float* p, Float a) // *p = a   (stores all elements contiguously in memory)
{
    _mm512_store_ps(p, a);
}

static SIMDINLINE void SIMDCALL store_si(Integer* p, Integer a) // *p = a
{
    _mm512_store_si512(&p->v, a);
}

static SIMDINLINE void SIMDCALL
                       storeu_si(Integer* p, Integer a) // *p = a    (same as store_si but allows for unaligned mem)
{
    _mm512_storeu_si512(&p->v, a);
}

static SIMDINLINE void SIMDCALL
                       stream_ps(float* p, Float a) // *p = a   (same as store_ps, but doesn't keep memory in cache)
{
    _mm512_stream_ps(p, a);
}

static SIMDINLINE Integer SIMDCALL set_epi32(int i15,
                                             int i14,
                                             int i13,
                                             int i12,
                                             int i11,
                                             int i10,
                                             int i9,
                                             int i8,
                                             int i7,
                                             int i6,
                                             int i5,
                                             int i4,
                                             int i3,
                                             int i2,
                                             int i1,
                                             int i0)
{
    return _mm512_set_epi32(i15, i14, i13, i12, i11, i10, i9, i8, i7, i6, i5, i4, i3, i2, i1, i0);
}

static SIMDINLINE Integer SIMDCALL
                          set_epi32(int i7, int i6, int i5, int i4, int i3, int i2, int i1, int i0)
{
    return set_epi32(0, 0, 0, 0, 0, 0, 0, 0, i7, i6, i5, i4, i3, i2, i1, i0);
}

static SIMDINLINE Float SIMDCALL set_ps(float i15,
                                        float i14,
                                        float i13,
                                        float i12,
                                        float i11,
                                        float i10,
                                        float i9,
                                        float i8,
                                        float i7,
                                        float i6,
                                        float i5,
                                        float i4,
                                        float i3,
                                        float i2,
                                        float i1,
                                        float i0)
{
    return _mm512_set_ps(i15, i14, i13, i12, i11, i10, i9, i8, i7, i6, i5, i4, i3, i2, i1, i0);
}

static SIMDINLINE Float SIMDCALL
                        set_ps(float i7, float i6, float i5, float i4, float i3, float i2, float i1, float i0)
{
    return set_ps(0, 0, 0, 0, 0, 0, 0, 0, i7, i6, i5, i4, i3, i2, i1, i0);
}

static SIMDINLINE Float SIMDCALL vmask_ps(int32_t mask)
{
    return castsi_ps(_mm512_maskz_mov_epi32(__mmask16(mask), set1_epi32(-1)));
}

#undef SIMD_WRAPPER_1_
#undef SIMD_WRAPPER_1
#undef SIMD_WRAPPER_2
#undef SIMD_WRAPPER_2_
#undef SIMD_WRAPPERI_2_
#undef SIMD_DWRAPPER_2
#undef SIMD_DWRAPPER_2I
#undef SIMD_WRAPPER_2I_
#undef SIMD_WRAPPER_3_
#undef SIMD_WRAPPER_2I
#undef SIMD_WRAPPER_3
#undef SIMD_IWRAPPER_1
#undef SIMD_IWRAPPER_2
#undef SIMD_IFWRAPPER_2
#undef SIMD_IWRAPPER_2I
#undef SIMD_IWRAPPER_1
#undef SIMD_IWRAPPER_1I
#undef SIMD_IWRAPPER_1I_
#undef SIMD_IWRAPPER_2
#undef SIMD_IWRAPPER_2_
#undef SIMD_IWRAPPER_2I
