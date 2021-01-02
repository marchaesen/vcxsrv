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
#if !defined(__SIMD_LIB_AVX_HPP__)
#error Do not include this file directly, use "simdlib.hpp" instead.
#endif

using SIMD128T = SIMD128Impl::AVXImpl;

//============================================================================
// SIMD256 AVX (1) implementation
//============================================================================

#define SIMD_WRAPPER_1(op) \
    static SIMDINLINE Float SIMDCALL op(Float const& a) { return _mm256_##op(a); }

#define SIMD_WRAPPER_2(op)                                              \
    static SIMDINLINE Float SIMDCALL op(Float const& a, Float const& b) \
    {                                                                   \
        return _mm256_##op(a, b);                                       \
    }

#define SIMD_DWRAPPER_2(op)                                                \
    static SIMDINLINE Double SIMDCALL op(Double const& a, Double const& b) \
    {                                                                      \
        return _mm256_##op(a, b);                                          \
    }

#define SIMD_WRAPPER_2I(op)                                             \
    template <int ImmT>                                                 \
    static SIMDINLINE Float SIMDCALL op(Float const& a, Float const& b) \
    {                                                                   \
        return _mm256_##op(a, b, ImmT);                                 \
    }

#define SIMD_DWRAPPER_2I(op)                                               \
    template <int ImmT>                                                    \
    static SIMDINLINE Double SIMDCALL op(Double const& a, Double const& b) \
    {                                                                      \
        return _mm256_##op(a, b, ImmT);                                    \
    }

#define SIMD_WRAPPER_3(op)                                                              \
    static SIMDINLINE Float SIMDCALL op(Float const& a, Float const& b, Float const& c) \
    {                                                                                   \
        return _mm256_##op(a, b, c);                                                    \
    }

#define SIMD_IWRAPPER_1(op) \
    static SIMDINLINE Integer SIMDCALL op(Integer const& a) { return _mm256_##op(a); }

#define SIMD_IWRAPPER_2(op)                                                   \
    static SIMDINLINE Integer SIMDCALL op(Integer const& a, Integer const& b) \
    {                                                                         \
        return _mm256_##op(a, b);                                             \
    }

#define SIMD_IFWRAPPER_2(op, intrin)                                          \
    static SIMDINLINE Integer SIMDCALL op(Integer const& a, Integer const& b) \
    {                                                                         \
        return castps_si(intrin(castsi_ps(a), castsi_ps(b)));                 \
    }

#define SIMD_IFWRAPPER_2I(op, intrin)                                         \
    template <int ImmT>                                                       \
    static SIMDINLINE Integer SIMDCALL op(Integer const& a, Integer const& b) \
    {                                                                         \
        return castps_si(intrin(castsi_ps(a), castsi_ps(b), ImmT));           \
    }

#define SIMD_IWRAPPER_2I_(op, intrin)                                         \
    template <int ImmT>                                                       \
    static SIMDINLINE Integer SIMDCALL op(Integer const& a, Integer const& b) \
    {                                                                         \
        return _mm256_##intrin(a, b, ImmT);                                   \
    }
#define SIMD_IWRAPPER_2I(op) SIMD_IWRAPPER_2I_(op, op)

#define SIMD_IWRAPPER_3(op)                                                                     \
    static SIMDINLINE Integer SIMDCALL op(Integer const& a, Integer const& b, Integer const& c) \
    {                                                                                           \
        return _mm256_##op(a, b, c);                                                            \
    }

// emulated integer simd
#define SIMD_EMU_IWRAPPER_1(op)                             \
    static SIMDINLINE Integer SIMDCALL op(Integer const& a) \
    {                                                       \
        return Integer{                                     \
            SIMD128T::op(a.v4[0]),                          \
            SIMD128T::op(a.v4[1]),                          \
        };                                                  \
    }
#define SIMD_EMU_IWRAPPER_1L(op, shift)                                  \
    static SIMDINLINE Integer SIMDCALL op(Integer const& a)              \
    {                                                                    \
        return Integer{                                                  \
            SIMD128T::op(a.v4[0]),                                       \
            SIMD128T::op(SIMD128T::template srli_si<shift>(a.v4[0])),    \
        };                                                               \
    }                                                                    \
    static SIMDINLINE Integer SIMDCALL op(SIMD128Impl::Integer const& a) \
    {                                                                    \
        return Integer{                                                  \
            SIMD128T::op(a),                                             \
            SIMD128T::op(SIMD128T::template srli_si<shift>(a)),          \
        };                                                               \
    }

#define SIMD_EMU_IWRAPPER_1I(op)                            \
    template <int ImmT>                                     \
    static SIMDINLINE Integer SIMDCALL op(Integer const& a) \
    {                                                       \
        return Integer{                                     \
            SIMD128T::template op<ImmT>(a.v4[0]),           \
            SIMD128T::template op<ImmT>(a.v4[1]),           \
        };                                                  \
    }

#define SIMD_EMU_IWRAPPER_2(op)                                               \
    static SIMDINLINE Integer SIMDCALL op(Integer const& a, Integer const& b) \
    {                                                                         \
        return Integer{                                                       \
            SIMD128T::op(a.v4[0], b.v4[0]),                                   \
            SIMD128T::op(a.v4[1], b.v4[1]),                                   \
        };                                                                    \
    }

#define SIMD_EMU_IWRAPPER_2I(op)                                              \
    template <int ImmT>                                                       \
    static SIMDINLINE Integer SIMDCALL op(Integer const& a, Integer const& b) \
    {                                                                         \
        return Integer{                                                       \
            SIMD128T::template op<ImmT>(a.v4[0], b.v[0]),                     \
            SIMD128T::template op<ImmT>(a.v4[1], b.v[1]),                     \
        };                                                                    \
    }

//-----------------------------------------------------------------------
// Single precision floating point arithmetic operations
//-----------------------------------------------------------------------
SIMD_WRAPPER_2(add_ps); // return a + b
SIMD_WRAPPER_2(div_ps); // return a / b

static SIMDINLINE Float SIMDCALL fmadd_ps(Float const& a,
                                          Float const& b,
                                          Float const& c) // return (a * b) + c
{
    return add_ps(mul_ps(a, b), c);
}

static SIMDINLINE Float SIMDCALL fmsub_ps(Float const& a,
                                          Float const& b,
                                          Float const& c) // return (a * b) - c
{
    return sub_ps(mul_ps(a, b), c);
}

SIMD_WRAPPER_2(max_ps);   // return (a > b) ? a : b
SIMD_WRAPPER_2(min_ps);   // return (a < b) ? a : b
SIMD_WRAPPER_2(mul_ps);   // return a * b
SIMD_WRAPPER_1(rcp_ps);   // return 1.0f / a
SIMD_WRAPPER_1(rsqrt_ps); // return 1.0f / sqrt(a)
SIMD_WRAPPER_2(sub_ps);   // return a - b

template <RoundMode RMT>
static SIMDINLINE Float SIMDCALL round_ps(Float const& a)
{
    return _mm256_round_ps(a, static_cast<int>(RMT));
}

static SIMDINLINE Float SIMDCALL ceil_ps(Float const& a)
{
    return round_ps<RoundMode::CEIL_NOEXC>(a);
}
static SIMDINLINE Float SIMDCALL floor_ps(Float const& a)
{
    return round_ps<RoundMode::FLOOR_NOEXC>(a);
}

//-----------------------------------------------------------------------
// Integer (various width) arithmetic operations
//-----------------------------------------------------------------------
SIMD_EMU_IWRAPPER_1(abs_epi32); // return absolute_value(a) (int32)
SIMD_EMU_IWRAPPER_2(add_epi32); // return a + b (int32)
SIMD_EMU_IWRAPPER_2(add_epi8);  // return a + b (int8)
SIMD_EMU_IWRAPPER_2(adds_epu8); // return ((a + b) > 0xff) ? 0xff : (a + b) (uint8)
SIMD_EMU_IWRAPPER_2(max_epi32); // return (a > b) ? a : b (int32)
SIMD_EMU_IWRAPPER_2(max_epu32); // return (a > b) ? a : b (uint32)
SIMD_EMU_IWRAPPER_2(min_epi32); // return (a < b) ? a : b (int32)
SIMD_EMU_IWRAPPER_2(min_epu32); // return (a < b) ? a : b (uint32)
SIMD_EMU_IWRAPPER_2(mul_epi32); // return a * b (int32)

// return (a * b) & 0xFFFFFFFF
//
// Multiply the packed 32-bit integers in a and b, producing intermediate 64-bit integers,
// and store the low 32 bits of the intermediate integers in dst.
SIMD_EMU_IWRAPPER_2(mullo_epi32);
SIMD_EMU_IWRAPPER_2(sub_epi32); // return a - b (int32)
SIMD_EMU_IWRAPPER_2(sub_epi64); // return a - b (int64)
SIMD_EMU_IWRAPPER_2(subs_epu8); // return (b > a) ? 0 : (a - b) (uint8)

//-----------------------------------------------------------------------
// Logical operations
//-----------------------------------------------------------------------
SIMD_WRAPPER_2(and_ps);                         // return a & b       (float treated as int)
SIMD_IFWRAPPER_2(and_si, _mm256_and_ps);        // return a & b       (int)
SIMD_WRAPPER_2(andnot_ps);                      // return (~a) & b    (float treated as int)
SIMD_IFWRAPPER_2(andnot_si, _mm256_andnot_ps);  // return (~a) & b    (int)
SIMD_WRAPPER_2(or_ps);                          // return a | b       (float treated as int)
SIMD_IFWRAPPER_2(or_si, _mm256_or_ps);          // return a | b       (int)
SIMD_WRAPPER_2(xor_ps);                         // return a ^ b       (float treated as int)
SIMD_IFWRAPPER_2(xor_si, _mm256_xor_ps);        // return a ^ b       (int)

//-----------------------------------------------------------------------
// Shift operations
//-----------------------------------------------------------------------
SIMD_EMU_IWRAPPER_1I(slli_epi32); // return a << ImmT

static SIMDINLINE Integer SIMDCALL sllv_epi32(Integer const& vA,
                                              Integer const& vCount) // return a << b      (uint32)
{
    int32_t aHi, aLow, countHi, countLow;
    __m128i vAHi      = _mm_castps_si128(_mm256_extractf128_ps(_mm256_castsi256_ps(vA), 1));
    __m128i vALow     = _mm_castps_si128(_mm256_extractf128_ps(_mm256_castsi256_ps(vA), 0));
    __m128i vCountHi  = _mm_castps_si128(_mm256_extractf128_ps(_mm256_castsi256_ps(vCount), 1));
    __m128i vCountLow = _mm_castps_si128(_mm256_extractf128_ps(_mm256_castsi256_ps(vCount), 0));

    aHi     = _mm_extract_epi32(vAHi, 0);
    countHi = _mm_extract_epi32(vCountHi, 0);
    aHi <<= countHi;
    vAHi = _mm_insert_epi32(vAHi, aHi, 0);

    aLow     = _mm_extract_epi32(vALow, 0);
    countLow = _mm_extract_epi32(vCountLow, 0);
    aLow <<= countLow;
    vALow = _mm_insert_epi32(vALow, aLow, 0);

    aHi     = _mm_extract_epi32(vAHi, 1);
    countHi = _mm_extract_epi32(vCountHi, 1);
    aHi <<= countHi;
    vAHi = _mm_insert_epi32(vAHi, aHi, 1);

    aLow     = _mm_extract_epi32(vALow, 1);
    countLow = _mm_extract_epi32(vCountLow, 1);
    aLow <<= countLow;
    vALow = _mm_insert_epi32(vALow, aLow, 1);

    aHi     = _mm_extract_epi32(vAHi, 2);
    countHi = _mm_extract_epi32(vCountHi, 2);
    aHi <<= countHi;
    vAHi = _mm_insert_epi32(vAHi, aHi, 2);

    aLow     = _mm_extract_epi32(vALow, 2);
    countLow = _mm_extract_epi32(vCountLow, 2);
    aLow <<= countLow;
    vALow = _mm_insert_epi32(vALow, aLow, 2);

    aHi     = _mm_extract_epi32(vAHi, 3);
    countHi = _mm_extract_epi32(vCountHi, 3);
    aHi <<= countHi;
    vAHi = _mm_insert_epi32(vAHi, aHi, 3);

    aLow     = _mm_extract_epi32(vALow, 3);
    countLow = _mm_extract_epi32(vCountLow, 3);
    aLow <<= countLow;
    vALow = _mm_insert_epi32(vALow, aLow, 3);

    __m256i ret = _mm256_set1_epi32(0);
    ret         = _mm256_insertf128_si256(ret, vAHi, 1);
    ret         = _mm256_insertf128_si256(ret, vALow, 0);
    return ret;
}

SIMD_EMU_IWRAPPER_1I(srai_epi32); // return a >> ImmT   (int32)
SIMD_EMU_IWRAPPER_1I(srli_epi32); // return a >> ImmT   (uint32)
SIMD_EMU_IWRAPPER_1I(srli_si);    // return a >> (ImmT*8) (uint)

template <int ImmT> // same as srli_si, but with Float cast to int
static SIMDINLINE Float SIMDCALL srlisi_ps(Float const& a)
{
    return castsi_ps(srli_si<ImmT>(castps_si(a)));
}

static SIMDINLINE Integer SIMDCALL srlv_epi32(Integer const& vA,
                                              Integer const& vCount) // return a >> b      (uint32)
{
    int32_t aHi, aLow, countHi, countLow;
    __m128i vAHi      = _mm_castps_si128(_mm256_extractf128_ps(_mm256_castsi256_ps(vA), 1));
    __m128i vALow     = _mm_castps_si128(_mm256_extractf128_ps(_mm256_castsi256_ps(vA), 0));
    __m128i vCountHi  = _mm_castps_si128(_mm256_extractf128_ps(_mm256_castsi256_ps(vCount), 1));
    __m128i vCountLow = _mm_castps_si128(_mm256_extractf128_ps(_mm256_castsi256_ps(vCount), 0));

    aHi     = _mm_extract_epi32(vAHi, 0);
    countHi = _mm_extract_epi32(vCountHi, 0);
    aHi >>= countHi;
    vAHi = _mm_insert_epi32(vAHi, aHi, 0);

    aLow     = _mm_extract_epi32(vALow, 0);
    countLow = _mm_extract_epi32(vCountLow, 0);
    aLow >>= countLow;
    vALow = _mm_insert_epi32(vALow, aLow, 0);

    aHi     = _mm_extract_epi32(vAHi, 1);
    countHi = _mm_extract_epi32(vCountHi, 1);
    aHi >>= countHi;
    vAHi = _mm_insert_epi32(vAHi, aHi, 1);

    aLow     = _mm_extract_epi32(vALow, 1);
    countLow = _mm_extract_epi32(vCountLow, 1);
    aLow >>= countLow;
    vALow = _mm_insert_epi32(vALow, aLow, 1);

    aHi     = _mm_extract_epi32(vAHi, 2);
    countHi = _mm_extract_epi32(vCountHi, 2);
    aHi >>= countHi;
    vAHi = _mm_insert_epi32(vAHi, aHi, 2);

    aLow     = _mm_extract_epi32(vALow, 2);
    countLow = _mm_extract_epi32(vCountLow, 2);
    aLow >>= countLow;
    vALow = _mm_insert_epi32(vALow, aLow, 2);

    aHi     = _mm_extract_epi32(vAHi, 3);
    countHi = _mm_extract_epi32(vCountHi, 3);
    aHi >>= countHi;
    vAHi = _mm_insert_epi32(vAHi, aHi, 3);

    aLow     = _mm_extract_epi32(vALow, 3);
    countLow = _mm_extract_epi32(vCountLow, 3);
    aLow >>= countLow;
    vALow = _mm_insert_epi32(vALow, aLow, 3);

    __m256i ret = _mm256_set1_epi32(0);
    ret         = _mm256_insertf128_si256(ret, vAHi, 1);
    ret         = _mm256_insertf128_si256(ret, vALow, 0);
    return ret;
}

//-----------------------------------------------------------------------
// Conversion operations
//-----------------------------------------------------------------------
static SIMDINLINE Float SIMDCALL castpd_ps(Double const& a) // return *(Float*)(&a)
{
    return _mm256_castpd_ps(a);
}

static SIMDINLINE Integer SIMDCALL castps_si(Float const& a) // return *(Integer*)(&a)
{
    return _mm256_castps_si256(a);
}

static SIMDINLINE Double SIMDCALL castsi_pd(Integer const& a) // return *(Double*)(&a)
{
    return _mm256_castsi256_pd(a);
}

static SIMDINLINE Double SIMDCALL castps_pd(Float const& a) // return *(Double*)(&a)
{
    return _mm256_castps_pd(a);
}

static SIMDINLINE Integer SIMDCALL castpd_si(Double const& a) // return *(Integer*)(&a)
{
    return _mm256_castpd_si256(a);
}

static SIMDINLINE Float SIMDCALL castsi_ps(Integer const& a) // return *(Float*)(&a)
{
    return _mm256_castsi256_ps(a);
}

static SIMDINLINE Float SIMDCALL
                        cvtepi32_ps(Integer const& a) // return (float)a    (int32 --> float)
{
    return _mm256_cvtepi32_ps(a);
}

SIMD_EMU_IWRAPPER_1L(cvtepu8_epi16, 8);  // return (int16)a    (uint8 --> int16)
SIMD_EMU_IWRAPPER_1L(cvtepu8_epi32, 4);  // return (int32)a    (uint8 --> int32)
SIMD_EMU_IWRAPPER_1L(cvtepu16_epi32, 8); // return (int32)a    (uint16 --> int32)
SIMD_EMU_IWRAPPER_1L(cvtepu16_epi64, 4); // return (int64)a    (uint16 --> int64)
SIMD_EMU_IWRAPPER_1L(cvtepu32_epi64, 8); // return (int64)a    (uint32 --> int64)

static SIMDINLINE Integer SIMDCALL
                          cvtps_epi32(Float const& a) // return (int32)a    (float --> int32)
{
    return _mm256_cvtps_epi32(a);
}

static SIMDINLINE Integer SIMDCALL
                          cvttps_epi32(Float const& a) // return (int32)a    (rnd_to_zero(float) --> int32)
{
    return _mm256_cvttps_epi32(a);
}

//-----------------------------------------------------------------------
// Comparison operations
//-----------------------------------------------------------------------
template <CompareType CmpTypeT>
static SIMDINLINE Float SIMDCALL cmp_ps(Float const& a, Float const& b) // return a (CmpTypeT) b
{
    return _mm256_cmp_ps(a, b, static_cast<const int>(CmpTypeT));
}
static SIMDINLINE Float SIMDCALL cmplt_ps(Float const& a, Float const& b)
{
    return cmp_ps<CompareType::LT_OQ>(a, b);
}
static SIMDINLINE Float SIMDCALL cmpgt_ps(Float const& a, Float const& b)
{
    return cmp_ps<CompareType::GT_OQ>(a, b);
}
static SIMDINLINE Float SIMDCALL cmpneq_ps(Float const& a, Float const& b)
{
    return cmp_ps<CompareType::NEQ_OQ>(a, b);
}
static SIMDINLINE Float SIMDCALL cmpeq_ps(Float const& a, Float const& b)
{
    return cmp_ps<CompareType::EQ_OQ>(a, b);
}
static SIMDINLINE Float SIMDCALL cmpge_ps(Float const& a, Float const& b)
{
    return cmp_ps<CompareType::GE_OQ>(a, b);
}
static SIMDINLINE Float SIMDCALL cmple_ps(Float const& a, Float const& b)
{
    return cmp_ps<CompareType::LE_OQ>(a, b);
}

SIMD_EMU_IWRAPPER_2(cmpeq_epi8);  // return a == b (int8)
SIMD_EMU_IWRAPPER_2(cmpeq_epi16); // return a == b (int16)
SIMD_EMU_IWRAPPER_2(cmpeq_epi32); // return a == b (int32)
SIMD_EMU_IWRAPPER_2(cmpeq_epi64); // return a == b (int64)
SIMD_EMU_IWRAPPER_2(cmpgt_epi8);  // return a > b (int8)
SIMD_EMU_IWRAPPER_2(cmpgt_epi16); // return a > b (int16)
SIMD_EMU_IWRAPPER_2(cmpgt_epi32); // return a > b (int32)
SIMD_EMU_IWRAPPER_2(cmpgt_epi64); // return a > b (int64)
SIMD_EMU_IWRAPPER_2(cmplt_epi32); // return a < b (int32)

static SIMDINLINE bool SIMDCALL
                       testz_ps(Float const& a, Float const& b) // return all_lanes_zero(a & b) ? 1 : 0 (float)
{
    return 0 != _mm256_testz_ps(a, b);
}

static SIMDINLINE bool SIMDCALL
                       testz_si(Integer const& a, Integer const& b) // return all_lanes_zero(a & b) ? 1 : 0 (int)
{
    return 0 != _mm256_testz_si256(a, b);
}

//-----------------------------------------------------------------------
// Blend / shuffle / permute operations
//-----------------------------------------------------------------------
SIMD_WRAPPER_2I(blend_ps);                       // return ImmT ? b : a  (float)
SIMD_IFWRAPPER_2I(blend_epi32, _mm256_blend_ps); // return ImmT ? b : a  (int32)
SIMD_WRAPPER_3(blendv_ps);                       // return mask ? b : a  (float)

static SIMDINLINE Integer SIMDCALL blendv_epi32(Integer const& a,
                                                Integer const& b,
                                                Float const&   mask) // return mask ? b : a (int)
{
    return castps_si(blendv_ps(castsi_ps(a), castsi_ps(b), mask));
}

static SIMDINLINE Integer SIMDCALL blendv_epi32(Integer const& a,
                                                Integer const& b,
                                                Integer const& mask) // return mask ? b : a (int)
{
    return castps_si(blendv_ps(castsi_ps(a), castsi_ps(b), castsi_ps(mask)));
}

static SIMDINLINE Float SIMDCALL
                        broadcast_ss(float const* p) // return *p (all elements in vector get same value)
{
    return _mm256_broadcast_ss(p);
}

SIMD_EMU_IWRAPPER_2(packs_epi16); // See documentation for _mm256_packs_epi16 and _mm512_packs_epi16
SIMD_EMU_IWRAPPER_2(packs_epi32); // See documentation for _mm256_packs_epi32 and _mm512_packs_epi32
SIMD_EMU_IWRAPPER_2(
    packus_epi16); // See documentation for _mm256_packus_epi16 and _mm512_packus_epi16
SIMD_EMU_IWRAPPER_2(
    packus_epi32); // See documentation for _mm256_packus_epi32 and _mm512_packus_epi32

template <int ImmT>
static SIMDINLINE Float SIMDCALL permute_ps(Float const& a)
{
    return _mm256_permute_ps(a, ImmT);
}

static SIMDINLINE Integer SIMDCALL permute_epi32(
    Integer const& a, Integer const& swiz) // return a[swiz[i]] for each 32-bit lane i (int32)
{
    Integer result;

    // Ugly slow implementation
    uint32_t const* pA      = reinterpret_cast<uint32_t const*>(&a);
    uint32_t const* pSwiz   = reinterpret_cast<uint32_t const*>(&swiz);
    uint32_t*       pResult = reinterpret_cast<uint32_t*>(&result);

    for (uint32_t i = 0; i < SIMD_WIDTH; ++i)
    {
        pResult[i] = pA[0xF & pSwiz[i]];
    }

    return result;
}

static SIMDINLINE Float SIMDCALL
                        permute_ps(Float const& a, Integer const& swiz) // return a[swiz[i]] for each 32-bit lane i (float)
{
    Float result;

    // Ugly slow implementation
    float const*    pA      = reinterpret_cast<float const*>(&a);
    uint32_t const* pSwiz   = reinterpret_cast<uint32_t const*>(&swiz);
    float*          pResult = reinterpret_cast<float*>(&result);

    for (uint32_t i = 0; i < SIMD_WIDTH; ++i)
    {
        pResult[i] = pA[0xF & pSwiz[i]];
    }

    return result;
}

SIMD_WRAPPER_2I(permute2f128_ps);
SIMD_DWRAPPER_2I(permute2f128_pd);
SIMD_IWRAPPER_2I_(permute2f128_si, permute2f128_si256);

SIMD_EMU_IWRAPPER_1I(shuffle_epi32);

template <int ImmT>
static SIMDINLINE Integer SIMDCALL shuffle_epi64(Integer const& a, Integer const& b)
{
    return castpd_si(shuffle_pd<ImmT>(castsi_pd(a), castsi_pd(b)));
}
SIMD_EMU_IWRAPPER_2(shuffle_epi8);
SIMD_DWRAPPER_2I(shuffle_pd);
SIMD_WRAPPER_2I(shuffle_ps);
SIMD_EMU_IWRAPPER_2(unpackhi_epi16);
SIMD_IFWRAPPER_2(unpackhi_epi32, _mm256_unpackhi_ps);
SIMD_EMU_IWRAPPER_2(unpackhi_epi64);
SIMD_EMU_IWRAPPER_2(unpackhi_epi8);
SIMD_DWRAPPER_2(unpackhi_pd);
SIMD_WRAPPER_2(unpackhi_ps);
SIMD_EMU_IWRAPPER_2(unpacklo_epi16);
SIMD_IFWRAPPER_2(unpacklo_epi32, _mm256_unpacklo_ps);
SIMD_EMU_IWRAPPER_2(unpacklo_epi64);
SIMD_EMU_IWRAPPER_2(unpacklo_epi8);
SIMD_DWRAPPER_2(unpacklo_pd);
SIMD_WRAPPER_2(unpacklo_ps);

//-----------------------------------------------------------------------
// Load / store operations
//-----------------------------------------------------------------------
template <ScaleFactor ScaleT = ScaleFactor::SF_1>
static SIMDINLINE Float SIMDCALL
                        i32gather_ps(float const* p, Integer const& idx) // return *(float*)(((int8*)p) + (idx * ScaleT))
{
    uint32_t* pOffsets = (uint32_t*)&idx;
    Float     vResult;
    float*    pResult = (float*)&vResult;
    for (uint32_t i = 0; i < SIMD_WIDTH; ++i)
    {
        uint32_t offset = pOffsets[i];
        offset          = offset * static_cast<uint32_t>(ScaleT);
        pResult[i]      = *(float const*)(((uint8_t const*)p + offset));
    }

    return vResult;
}

template <ScaleFactor ScaleT = ScaleFactor::SF_1>
static SIMDINLINE Float SIMDCALL
sw_i32gather_ps(float const* p, Integer const& idx) // return *(float*)(((int8*)p) + (idx * ScaleT))
{
    return i32gather_ps<ScaleT>(p, idx);
}

static SIMDINLINE Float SIMDCALL
                        load1_ps(float const* p) // return *p    (broadcast 1 value to all elements)
{
    return broadcast_ss(p);
}

static SIMDINLINE Float SIMDCALL
                        load_ps(float const* p) // return *p    (loads SIMD width elements from memory)
{
    return _mm256_load_ps(p);
}

static SIMDINLINE Integer SIMDCALL load_si(Integer const* p) // return *p
{
    return _mm256_load_si256(&p->v);
}

static SIMDINLINE Float SIMDCALL
                        loadu_ps(float const* p) // return *p    (same as load_ps but allows for unaligned mem)
{
    return _mm256_loadu_ps(p);
}

static SIMDINLINE Integer SIMDCALL
                          loadu_si(Integer const* p) // return *p    (same as load_si but allows for unaligned mem)
{
    return _mm256_lddqu_si256(&p->v);
}

// for each element: (mask & (1 << 31)) ? (i32gather_ps<ScaleT>(p, idx), mask = 0) : old
template <ScaleFactor ScaleT = ScaleFactor::SF_1>
static SIMDINLINE Float SIMDCALL
                        mask_i32gather_ps(Float const& old, float const* p, Integer const& idx, Float const& mask)
{
    uint32_t* pOffsets = (uint32_t*)&idx;
    Float     vResult  = old;
    float*    pResult  = (float*)&vResult;
    unsigned long index = 0;
    uint32_t  umask = movemask_ps(mask);
    while (_BitScanForward(&index, umask))
    {
        umask &= ~(1 << index);
        uint32_t offset = pOffsets[index];
        offset          = offset * static_cast<uint32_t>(ScaleT);
        pResult[index]  = *(float const*)(((uint8_t const*)p + offset));
    }

    return vResult;
}

template <ScaleFactor ScaleT = ScaleFactor::SF_1>
static SIMDINLINE Float SIMDCALL
sw_mask_i32gather_ps(Float const& old, float const* p, Integer const& idx, Float const& mask)
{
    return mask_i32gather_ps<ScaleT>(old, p, idx, mask);
}

static SIMDINLINE void SIMDCALL maskstore_ps(float* p, Integer const& mask, Float const& src)
{
    _mm256_maskstore_ps(p, mask, src);
}

static SIMDINLINE uint32_t SIMDCALL movemask_epi8(Integer const& a)
{
    return SIMD128T::movemask_epi8(a.v4[0]) | (SIMD128T::movemask_epi8(a.v4[1]) << 16);
}

static SIMDINLINE uint32_t SIMDCALL movemask_pd(Double const& a)
{
    return static_cast<uint32_t>(_mm256_movemask_pd(a));
}
static SIMDINLINE uint32_t SIMDCALL movemask_ps(Float const& a)
{
    return static_cast<uint32_t>(_mm256_movemask_ps(a));
}

static SIMDINLINE Integer SIMDCALL set1_epi32(int i) // return i (all elements are same value)
{
    return _mm256_set1_epi32(i);
}

static SIMDINLINE Integer SIMDCALL set1_epi8(char i) // return i (all elements are same value)
{
    return _mm256_set1_epi8(i);
}

static SIMDINLINE Float SIMDCALL set1_ps(float f) // return f (all elements are same value)
{
    return _mm256_set1_ps(f);
}

static SIMDINLINE Float SIMDCALL setzero_ps() // return 0 (float)
{
    return _mm256_setzero_ps();
}

static SIMDINLINE Integer SIMDCALL setzero_si() // return 0 (integer)
{
    return _mm256_setzero_si256();
}

static SIMDINLINE void SIMDCALL
                       store_ps(float* p, Float const& a) // *p = a   (stores all elements contiguously in memory)
{
    _mm256_store_ps(p, a);
}

static SIMDINLINE void SIMDCALL store_si(Integer* p, Integer const& a) // *p = a
{
    _mm256_store_si256(&p->v, a);
}

static SIMDINLINE void SIMDCALL
                       stream_ps(float* p, Float const& a) // *p = a   (same as store_ps, but doesn't keep memory in cache)
{
    _mm256_stream_ps(p, a);
}

//=======================================================================
// Legacy interface (available only in SIMD256 width)
//=======================================================================

static SIMDINLINE Float SIMDCALL broadcast_ps(SIMD128Impl::Float const* p)
{
    return _mm256_broadcast_ps(&p->v);
}

template <int ImmT>
static SIMDINLINE SIMD128Impl::Double SIMDCALL extractf128_pd(Double const& a)
{
    return _mm256_extractf128_pd(a, ImmT);
}

template <int ImmT>
static SIMDINLINE SIMD128Impl::Float SIMDCALL extractf128_ps(Float const& a)
{
    return _mm256_extractf128_ps(a, ImmT);
}

template <int ImmT>
static SIMDINLINE SIMD128Impl::Integer SIMDCALL extractf128_si(Integer const& a)
{
    return _mm256_extractf128_si256(a, ImmT);
}

template <int ImmT>
static SIMDINLINE Double SIMDCALL insertf128_pd(Double const& a, SIMD128Impl::Double const& b)
{
    return _mm256_insertf128_pd(a, b, ImmT);
}

template <int ImmT>
static SIMDINLINE Float SIMDCALL insertf128_ps(Float const& a, SIMD128Impl::Float const& b)
{
    return _mm256_insertf128_ps(a, b, ImmT);
}

template <int ImmT>
static SIMDINLINE Integer SIMDCALL insertf128_si(Integer const& a, SIMD128Impl::Integer const& b)
{
    return _mm256_insertf128_si256(a, b, ImmT);
}

#ifndef _mm256_set_m128i
#define _mm256_set_m128i(/* SIMD128Impl::Integer */ hi, /* SIMD128Impl::Integer */ lo) \
    _mm256_insertf128_si256(_mm256_castsi128_si256(lo), (hi), 0x1)
#endif

#ifndef _mm256_loadu2_m128i
#define _mm256_loadu2_m128i(/* SIMD128Impl::Integer const* */ hiaddr, \
                            /* SIMD128Impl::Integer const* */ loaddr) \
    _mm256_set_m128i(_mm_loadu_si128(hiaddr), _mm_loadu_si128(loaddr))
#endif

static SIMDINLINE Integer SIMDCALL loadu2_si(SIMD128Impl::Integer const* phi,
                                             SIMD128Impl::Integer const* plo)
{
    return _mm256_loadu2_m128i(&phi->v, &plo->v);
}

static SIMDINLINE Integer SIMDCALL
                          set_epi32(int i7, int i6, int i5, int i4, int i3, int i2, int i1, int i0)
{
    return _mm256_set_epi32(i7, i6, i5, i4, i3, i2, i1, i0);
}

static SIMDINLINE Float SIMDCALL
                        set_ps(float i7, float i6, float i5, float i4, float i3, float i2, float i1, float i0)
{
    return _mm256_set_ps(i7, i6, i5, i4, i3, i2, i1, i0);
}

static SIMDINLINE void SIMDCALL storeu2_si(SIMD128Impl::Integer* phi,
                                           SIMD128Impl::Integer* plo,
                                           Integer const&        src)
{
    _mm256_storeu2_m128i(&phi->v, &plo->v, src);
}

static SIMDINLINE Float SIMDCALL vmask_ps(int32_t mask)
{
    Integer       vec = set1_epi32(mask);
    const Integer bit = set_epi32(0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01);
    vec               = and_si(vec, bit);
    vec               = cmplt_epi32(setzero_si(), vec);
    return castsi_ps(vec);
}

#undef SIMD_WRAPPER_1
#undef SIMD_WRAPPER_2
#undef SIMD_DWRAPPER_2
#undef SIMD_DWRAPPER_2I
#undef SIMD_WRAPPER_2I
#undef SIMD_WRAPPER_3
#undef SIMD_IWRAPPER_1
#undef SIMD_IWRAPPER_2
#undef SIMD_IFWRAPPER_2
#undef SIMD_IFWRAPPER_2I
#undef SIMD_IWRAPPER_2I
#undef SIMD_IWRAPPER_2I_
#undef SIMD_IWRAPPER_2_
#undef SIMD_IWRAPPER_3
#undef SIMD_EMU_IWRAPPER_1
#undef SIMD_EMU_IWRAPPER_1I
#undef SIMD_EMU_IWRAPPER_2
#undef SIMD_EMU_IWRAPPER_2I
