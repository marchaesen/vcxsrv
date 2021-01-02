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

//============================================================================
// SIMD16 AVX512 (F) implementation for Core processors
//
//============================================================================

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
static SIMDINLINE Integer vmask(__mmask32 m)
{
    return _mm512_maskz_set1_epi16(m, -1);
}
static SIMDINLINE Integer vmask(__mmask64 m)
{
    return _mm512_maskz_set1_epi8(m, -1);
}

public:
SIMD_IWRAPPER_2(add_epi8);  // return a + b (int8)
SIMD_IWRAPPER_2(adds_epu8); // return ((a + b) > 0xff) ? 0xff : (a + b) (uint8)
SIMD_IWRAPPER_2(subs_epu8); // return (b > a) ? 0 : (a - b) (uint8)

SIMD_WRAPPER_2(and_ps);    // return a & b       (float treated as int)
SIMD_WRAPPER_2(andnot_ps); // return (~a) & b    (float treated as int)
SIMD_WRAPPER_2(or_ps);     // return a | b       (float treated as int)
SIMD_WRAPPER_2(xor_ps);    // return a ^ b       (float treated as int)

SIMD_IWRAPPER_1_8(cvtepu8_epi16); // return (int16)a    (uint8 --> int16)

template <CompareTypeInt CmpTypeT>
static SIMDINLINE Integer SIMDCALL cmp_epi8(Integer a, Integer b)
{
    // Legacy vector mask generator
    __mmask64 result = _mm512_cmp_epi8_mask(a, b, static_cast<const int>(CmpTypeT));
    return vmask(result);
}
template <CompareTypeInt CmpTypeT>
static SIMDINLINE Integer SIMDCALL cmp_epi16(Integer a, Integer b)
{
    // Legacy vector mask generator
    __mmask32 result = _mm512_cmp_epi16_mask(a, b, static_cast<const int>(CmpTypeT));
    return vmask(result);
}

SIMD_IWRAPPER_2_CMP(cmpeq_epi8, cmp_epi8<CompareTypeInt::EQ>);   // return a == b (int8)
SIMD_IWRAPPER_2_CMP(cmpeq_epi16, cmp_epi16<CompareTypeInt::EQ>); // return a == b (int16)
SIMD_IWRAPPER_2_CMP(cmpgt_epi8, cmp_epi8<CompareTypeInt::GT>);   // return a > b (int8)
SIMD_IWRAPPER_2_CMP(cmpgt_epi16, cmp_epi16<CompareTypeInt::GT>); // return a > b (int16)

SIMD_IWRAPPER_2(packs_epi16);  // See documentation for _mm512_packs_epi16
SIMD_IWRAPPER_2(packs_epi32);  // See documentation for _mm512_packs_epi32
SIMD_IWRAPPER_2(packus_epi16); // See documentation for _mm512_packus_epi16
SIMD_IWRAPPER_2(packus_epi32); // See documentation for _mm512_packus_epi32

SIMD_IWRAPPER_2(unpackhi_epi8);  // See documentation for _mm512_unpackhi_epi8
SIMD_IWRAPPER_2(unpacklo_epi16); // See documentation for _mm512_unpacklo_epi16
SIMD_IWRAPPER_2(unpacklo_epi8);  // See documentation for _mm512_unpacklo_epi8

SIMD_IWRAPPER_2(shuffle_epi8);

static SIMDINLINE uint64_t SIMDCALL movemask_epi8(Integer a)
{
    __mmask64 m = _mm512_cmplt_epi8_mask(a, setzero_si());
    return static_cast<uint64_t>(m);
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
