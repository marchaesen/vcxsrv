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
// SIMD256 AVX (512) implementation
//
// Since this implementation inherits from the AVX (2) implementation,
// the only operations below ones that replace AVX (2) operations.
// These use native AVX512 instructions with masking to enable a larger
// register set.
//============================================================================

private:
static SIMDINLINE __m512 __conv(Float r)
{
    return _mm512_castps256_ps512(r.v);
}
static SIMDINLINE __m512d __conv(Double r)
{
    return _mm512_castpd256_pd512(r.v);
}
static SIMDINLINE __m512i __conv(Integer r)
{
    return _mm512_castsi256_si512(r.v);
}
static SIMDINLINE Float __conv(__m512 r)
{
    return _mm512_castps512_ps256(r);
}
static SIMDINLINE Double __conv(__m512d r)
{
    return _mm512_castpd512_pd256(r);
}
static SIMDINLINE Integer __conv(__m512i r)
{
    return _mm512_castsi512_si256(r);
}

public:
#define SIMD_WRAPPER_1_(op, intrin, mask)                        \
    static SIMDINLINE Float SIMDCALL op(Float a)                 \
    {                                                            \
        return __conv(_mm512_maskz_##intrin((mask), __conv(a))); \
    }
#define SIMD_WRAPPER_1(op) SIMD_WRAPPER_1_(op, op, __mmask16(0xff))

#define SIMD_WRAPPER_1I_(op, intrin, mask)                             \
    template <int ImmT>                                                \
    static SIMDINLINE Float SIMDCALL op(Float a)                       \
    {                                                                  \
        return __conv(_mm512_maskz_##intrin((mask), __conv(a), ImmT)); \
    }
#define SIMD_WRAPPER_1I(op) SIMD_WRAPPER_1I_(op, op, __mmask16(0xff))

#define SIMD_WRAPPER_2_(op, intrin, mask)                                   \
    static SIMDINLINE Float SIMDCALL op(Float a, Float b)                   \
    {                                                                       \
        return __conv(_mm512_maskz_##intrin((mask), __conv(a), __conv(b))); \
    }
#define SIMD_WRAPPER_2(op) SIMD_WRAPPER_2_(op, op, __mmask16(0xff))

#define SIMD_WRAPPER_2I(op)                                                 \
    template <int ImmT>                                                     \
    static SIMDINLINE Float SIMDCALL op(Float a, Float b)                   \
    {                                                                       \
        return __conv(_mm512_maskz_##op(0xff, __conv(a), __conv(b), ImmT)); \
    }

#define SIMD_WRAPPER_3_(op, intrin, mask)                                              \
    static SIMDINLINE Float SIMDCALL op(Float a, Float b, Float c)                     \
    {                                                                                  \
        return __conv(_mm512_maskz_##intrin((mask), __conv(a), __conv(b), __conv(c))); \
    }
#define SIMD_WRAPPER_3(op) SIMD_WRAPPER_3_(op, op, __mmask16(0xff))

#define SIMD_DWRAPPER_2I(op)                                               \
    template <int ImmT>                                                    \
    static SIMDINLINE Double SIMDCALL op(Double a, Double b)               \
    {                                                                      \
        return __conv(_mm512_maskz_##op(0xf, __conv(a), __conv(b), ImmT)); \
    }

#define SIMD_IWRAPPER_1_(op, intrin, mask)                       \
    static SIMDINLINE Integer SIMDCALL op(Integer a)             \
    {                                                            \
        return __conv(_mm512_maskz_##intrin((mask), __conv(a))); \
    }
#define SIMD_IWRAPPER_1_32(op) SIMD_IWRAPPER_1_(op, op, __mmask16(0xff))

#define SIMD_IWRAPPER_1I_(op, intrin, mask)                            \
    template <int ImmT>                                                \
    static SIMDINLINE Integer SIMDCALL op(Integer a)                   \
    {                                                                  \
        return __conv(_mm512_maskz_##intrin((mask), __conv(a), ImmT)); \
    }
#define SIMD_IWRAPPER_1I_32(op) SIMD_IWRAPPER_1I_(op, op, __mmask16(0xff))

#define SIMD_IWRAPPER_2_(op, intrin, mask)                                  \
    static SIMDINLINE Integer SIMDCALL op(Integer a, Integer b)             \
    {                                                                       \
        return __conv(_mm512_maskz_##intrin((mask), __conv(a), __conv(b))); \
    }
#define SIMD_IWRAPPER_2_32(op) SIMD_IWRAPPER_2_(op, op, __mmask16(0xff))

#define SIMD_IWRAPPER_2I(op)                                                \
    template <int ImmT>                                                     \
    static SIMDINLINE Integer SIMDCALL op(Integer a, Integer b)             \
    {                                                                       \
        return __conv(_mm512_maskz_##op(0xff, __conv(a), __conv(b), ImmT)); \
    }

//-----------------------------------------------------------------------
// Single precision floating point arithmetic operations
//-----------------------------------------------------------------------
SIMD_WRAPPER_2(add_ps);                                 // return a + b
SIMD_WRAPPER_2(div_ps);                                 // return a / b
SIMD_WRAPPER_3(fmadd_ps);                               // return (a * b) + c
SIMD_WRAPPER_3(fmsub_ps);                               // return (a * b) - c
SIMD_WRAPPER_2(max_ps);                                 // return (a > b) ? a : b
SIMD_WRAPPER_2(min_ps);                                 // return (a < b) ? a : b
SIMD_WRAPPER_2(mul_ps);                                 // return a * b
SIMD_WRAPPER_1_(rcp_ps, rcp14_ps, __mmask16(0xff));     // return 1.0f / a
SIMD_WRAPPER_1_(rsqrt_ps, rsqrt14_ps, __mmask16(0xff)); // return 1.0f / sqrt(a)
SIMD_WRAPPER_2(sub_ps);                                 // return a - b

//-----------------------------------------------------------------------
// Integer (various width) arithmetic operations
//-----------------------------------------------------------------------
SIMD_IWRAPPER_1_32(abs_epi32); // return absolute_value(a) (int32)
SIMD_IWRAPPER_2_32(add_epi32); // return a + b (int32)
SIMD_IWRAPPER_2_32(max_epi32); // return (a > b) ? a : b (int32)
SIMD_IWRAPPER_2_32(max_epu32); // return (a > b) ? a : b (uint32)
SIMD_IWRAPPER_2_32(min_epi32); // return (a < b) ? a : b (int32)
SIMD_IWRAPPER_2_32(min_epu32); // return (a < b) ? a : b (uint32)
SIMD_IWRAPPER_2_32(mul_epi32); // return a * b (int32)

// SIMD_IWRAPPER_2_8(add_epi8);    // return a + b (int8)
// SIMD_IWRAPPER_2_8(adds_epu8);   // return ((a + b) > 0xff) ? 0xff : (a + b) (uint8)

// return (a * b) & 0xFFFFFFFF
//
// Multiply the packed 32-bit integers in a and b, producing intermediate 64-bit integers,
// and store the low 32 bits of the intermediate integers in dst.
SIMD_IWRAPPER_2_32(mullo_epi32);
SIMD_IWRAPPER_2_32(sub_epi32); // return a - b (int32)

// SIMD_IWRAPPER_2_64(sub_epi64);  // return a - b (int64)
// SIMD_IWRAPPER_2_8(subs_epu8);   // return (b > a) ? 0 : (a - b) (uint8)

//-----------------------------------------------------------------------
// Logical operations
//-----------------------------------------------------------------------
SIMD_IWRAPPER_2_(and_si, and_epi32, __mmask16(0xff));       // return a & b       (int)
SIMD_IWRAPPER_2_(andnot_si, andnot_epi32, __mmask16(0xff)); // return (~a) & b    (int)
SIMD_IWRAPPER_2_(or_si, or_epi32, __mmask16(0xff));         // return a | b       (int)
SIMD_IWRAPPER_2_(xor_si, xor_epi32, __mmask16(0xff));       // return a ^ b       (int)

//-----------------------------------------------------------------------
// Shift operations
//-----------------------------------------------------------------------
SIMD_IWRAPPER_1I_32(slli_epi32); // return a << ImmT
SIMD_IWRAPPER_2_32(sllv_epi32);  // return a << b      (uint32)
SIMD_IWRAPPER_1I_32(srai_epi32); // return a >> ImmT   (int32)
SIMD_IWRAPPER_1I_32(srli_epi32); // return a >> ImmT   (uint32)
SIMD_IWRAPPER_2_32(srlv_epi32);  // return a >> b      (uint32)

// use AVX2 version
// SIMD_IWRAPPER_1I_(srli_si, srli_si256);     // return a >> (ImmT*8) (uint)

//-----------------------------------------------------------------------
// Conversion operations (Use AVX2 versions)
//-----------------------------------------------------------------------
// SIMD_IWRAPPER_1L(cvtepu8_epi16, 0xffff);    // return (int16)a    (uint8 --> int16)
// SIMD_IWRAPPER_1L(cvtepu8_epi32, 0xff);      // return (int32)a    (uint8 --> int32)
// SIMD_IWRAPPER_1L(cvtepu16_epi32, 0xff);     // return (int32)a    (uint16 --> int32)
// SIMD_IWRAPPER_1L(cvtepu16_epi64, 0xf);      // return (int64)a    (uint16 --> int64)
// SIMD_IWRAPPER_1L(cvtepu32_epi64, 0xf);      // return (int64)a    (uint32 --> int64)

//-----------------------------------------------------------------------
// Comparison operations (Use AVX2 versions
//-----------------------------------------------------------------------
// SIMD_IWRAPPER_2_CMP(cmpeq_epi8);    // return a == b (int8)
// SIMD_IWRAPPER_2_CMP(cmpeq_epi16);   // return a == b (int16)
// SIMD_IWRAPPER_2_CMP(cmpeq_epi32);   // return a == b (int32)
// SIMD_IWRAPPER_2_CMP(cmpeq_epi64);   // return a == b (int64)
// SIMD_IWRAPPER_2_CMP(cmpgt_epi8,);   // return a > b (int8)
// SIMD_IWRAPPER_2_CMP(cmpgt_epi16);   // return a > b (int16)
// SIMD_IWRAPPER_2_CMP(cmpgt_epi32);   // return a > b (int32)
// SIMD_IWRAPPER_2_CMP(cmpgt_epi64);   // return a > b (int64)
//
// static SIMDINLINE Integer SIMDCALL cmplt_epi32(Integer a, Integer b)   // return a < b (int32)
//{
//    return cmpgt_epi32(b, a);
//}

//-----------------------------------------------------------------------
// Blend / shuffle / permute operations
//-----------------------------------------------------------------------
// SIMD_IWRAPPER_2_8(packs_epi16);     // int16 --> int8    See documentation for _mm256_packs_epi16
// and _mm512_packs_epi16 SIMD_IWRAPPER_2_16(packs_epi32);    // int32 --> int16   See documentation
// for _mm256_packs_epi32 and _mm512_packs_epi32 SIMD_IWRAPPER_2_8(packus_epi16);    // uint16 -->
// uint8  See documentation for _mm256_packus_epi16 and _mm512_packus_epi16
// SIMD_IWRAPPER_2_16(packus_epi32);   // uint32 --> uint16 See documentation for
// _mm256_packus_epi32 and _mm512_packus_epi32

// SIMD_IWRAPPER_2_(permute_epi32, permutevar8x32_epi32);

// static SIMDINLINE Float SIMDCALL permute_ps(Float a, Integer swiz)    // return a[swiz[i]] for
// each 32-bit lane i (float)
//{
//    return _mm256_permutevar8x32_ps(a, swiz);
//}

SIMD_IWRAPPER_1I_32(shuffle_epi32);
// template<int ImmT>
// static SIMDINLINE Integer SIMDCALL shuffle_epi64(Integer a, Integer b)
//{
//    return castpd_si(shuffle_pd<ImmT>(castsi_pd(a), castsi_pd(b)));
//}
// SIMD_IWRAPPER_2(shuffle_epi8);
SIMD_IWRAPPER_2_32(unpackhi_epi32);
SIMD_IWRAPPER_2_32(unpacklo_epi32);

// SIMD_IWRAPPER_2_16(unpackhi_epi16);
// SIMD_IWRAPPER_2_64(unpackhi_epi64);
// SIMD_IWRAPPER_2_8(unpackhi_epi8);
// SIMD_IWRAPPER_2_16(unpacklo_epi16);
// SIMD_IWRAPPER_2_64(unpacklo_epi64);
// SIMD_IWRAPPER_2_8(unpacklo_epi8);

//-----------------------------------------------------------------------
// Load / store operations
//-----------------------------------------------------------------------
static SIMDINLINE Float SIMDCALL
                        load_ps(float const* p) // return *p    (loads SIMD width elements from memory)
{
    return __conv(_mm512_maskz_loadu_ps(__mmask16(0xff), p));
}

static SIMDINLINE Integer SIMDCALL load_si(Integer const* p) // return *p
{
    return __conv(_mm512_maskz_loadu_epi32(__mmask16(0xff), p));
}

static SIMDINLINE Float SIMDCALL
                        loadu_ps(float const* p) // return *p    (same as load_ps but allows for unaligned mem)
{
    return __conv(_mm512_maskz_loadu_ps(__mmask16(0xff), p));
}

static SIMDINLINE Integer SIMDCALL
                          loadu_si(Integer const* p) // return *p    (same as load_si but allows for unaligned mem)
{
    return __conv(_mm512_maskz_loadu_epi32(__mmask16(0xff), p));
}

template <ScaleFactor ScaleT = ScaleFactor::SF_1>
static SIMDINLINE Float SIMDCALL
                        i32gather_ps(float const* p, Integer idx) // return *(float*)(((int8*)p) + (idx * ScaleT))
{
    return __conv(_mm512_mask_i32gather_ps(
        _mm512_setzero_ps(), __mmask16(0xff), __conv(idx), p, static_cast<int>(ScaleT)));
}

// for each element: (mask & (1 << 31)) ? (i32gather_ps<ScaleT>(p, idx), mask = 0) : old
template <ScaleFactor ScaleT = ScaleFactor::SF_1>
static SIMDINLINE Float SIMDCALL
                        mask_i32gather_ps(Float old, float const* p, Integer idx, Float mask)
{
    __mmask16 m = 0xff;
    m           = _mm512_mask_test_epi32_mask(
        m, _mm512_castps_si512(__conv(mask)), _mm512_set1_epi32(0x80000000));
    return __conv(
        _mm512_mask_i32gather_ps(__conv(old), m, __conv(idx), p, static_cast<int>(ScaleT)));
}

// static SIMDINLINE uint32_t SIMDCALL movemask_epi8(Integer a)
// {
//     __mmask64 m = 0xffffffffull;
//     return static_cast<uint32_t>(
//         _mm512_mask_test_epi8_mask(m, __conv(a), _mm512_set1_epi8(0x80)));
// }

static SIMDINLINE void SIMDCALL maskstore_ps(float* p, Integer mask, Float src)
{
    __mmask16 m = 0xff;
    m           = _mm512_mask_test_epi32_mask(m, __conv(mask), _mm512_set1_epi32(0x80000000));
    _mm512_mask_storeu_ps(p, m, __conv(src));
}

static SIMDINLINE void SIMDCALL
                       store_ps(float* p, Float a) // *p = a   (stores all elements contiguously in memory)
{
    _mm512_mask_storeu_ps(p, __mmask16(0xff), __conv(a));
}

static SIMDINLINE void SIMDCALL store_si(Integer* p, Integer a) // *p = a
{
    _mm512_mask_storeu_epi32(p, __mmask16(0xff), __conv(a));
}

static SIMDINLINE Float SIMDCALL vmask_ps(int32_t mask)
{
    return castsi_ps(__conv(_mm512_maskz_set1_epi32(__mmask16(mask & 0xff), -1)));
}

//=======================================================================
// Legacy interface (available only in SIMD256 width)
//=======================================================================

#undef SIMD_WRAPPER_1_
#undef SIMD_WRAPPER_1
#undef SIMD_WRAPPER_1I_
#undef SIMD_WRAPPER_1I
#undef SIMD_WRAPPER_2_
#undef SIMD_WRAPPER_2
#undef SIMD_WRAPPER_2I
#undef SIMD_WRAPPER_3_
#undef SIMD_WRAPPER_3
#undef SIMD_IWRAPPER_1_
#undef SIMD_IWRAPPER_1_32
#undef SIMD_IWRAPPER_1I_
#undef SIMD_IWRAPPER_1I_32
#undef SIMD_IWRAPPER_2_
#undef SIMD_IWRAPPER_2_32
#undef SIMD_IWRAPPER_2I
