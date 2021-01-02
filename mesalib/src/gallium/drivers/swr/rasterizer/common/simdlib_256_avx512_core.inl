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
// SIMD256 AVX (512) implementation for Core processors
//
// Since this implementation inherits from the AVX (2) implementation,
// the only operations below ones that replace AVX (2) operations.
// These use native AVX512 instructions with masking to enable a larger
// register set.
//============================================================================

#define SIMD_DWRAPPER_1_(op, intrin, mask)                       \
    static SIMDINLINE Double SIMDCALL op(Double a)               \
    {                                                            \
        return __conv(_mm512_maskz_##intrin((mask), __conv(a))); \
    }
#define SIMD_DWRAPPER_1(op) SIMD_DWRAPPER_1_(op, op, __mmask8(0xf))

#define SIMD_DWRAPPER_1I_(op, intrin, mask)                            \
    template <int ImmT>                                                \
    static SIMDINLINE Double SIMDCALL op(Double a)                     \
    {                                                                  \
        return __conv(_mm512_maskz_##intrin((mask), __conv(a), ImmT)); \
    }
#define SIMD_DWRAPPER_1I(op) SIMD_DWRAPPER_1I_(op, op, __mmask8(0xf))

#define SIMD_DWRAPPER_2_(op, intrin, mask)                                  \
    static SIMDINLINE Double SIMDCALL op(Double a, Double b)                \
    {                                                                       \
        return __conv(_mm512_maskz_##intrin((mask), __conv(a), __conv(b))); \
    }
#define SIMD_DWRAPPER_2(op) SIMD_DWRAPPER_2_(op, op, __mmask8(0xf))

#define SIMD_IWRAPPER_1_(op, intrin, mask)                       \
    static SIMDINLINE Integer SIMDCALL op(Integer a)             \
    {                                                            \
        return __conv(_mm512_maskz_##intrin((mask), __conv(a))); \
    }
#define SIMD_IWRAPPER_1_8(op) SIMD_IWRAPPER_1_(op, op, __mmask64(0xffffffffull))
#define SIMD_IWRAPPER_1_16(op) SIMD_IWRAPPER_1_(op, op, __mmask32(0xffff))
#define SIMD_IWRAPPER_1_64(op) SIMD_IWRAPPER_1_(op, op, __mmask8(0xf))

#define SIMD_IWRAPPER_1I_(op, intrin, mask)                            \
    template <int ImmT>                                                \
    static SIMDINLINE Integer SIMDCALL op(Integer a)                   \
    {                                                                  \
        return __conv(_mm512_maskz_##intrin((mask), __conv(a), ImmT)); \
    }
#define SIMD_IWRAPPER_1I_8(op) SIMD_IWRAPPER_1I_(op, op, __mmask64(0xffffffffull))
#define SIMD_IWRAPPER_1I_16(op) SIMD_IWRAPPER_1I_(op, op, __mmask32(0xffff))
#define SIMD_IWRAPPER_1I_64(op) SIMD_IWRAPPER_1I_(op, op, __mmask8(0xf))

#define SIMD_IWRAPPER_2_(op, intrin, mask)                                  \
    static SIMDINLINE Integer SIMDCALL op(Integer a, Integer b)             \
    {                                                                       \
        return __conv(_mm512_maskz_##intrin((mask), __conv(a), __conv(b))); \
    }
#define SIMD_IWRAPPER_2_8(op) SIMD_IWRAPPER_2_(op, op, __mmask64(0xffffffffull))
#define SIMD_IWRAPPER_2_16(op) SIMD_IWRAPPER_2_(op, op, __mmask32(0xffff))
#define SIMD_IWRAPPER_2_64(op) SIMD_IWRAPPER_2_(op, op, __mmask8(0xf))

SIMD_IWRAPPER_2_8(add_epi8);      // return a + b (int8)
SIMD_IWRAPPER_2_8(adds_epu8);     // return ((a + b) > 0xff) ? 0xff : (a + b) (uint8)
SIMD_IWRAPPER_2_64(sub_epi64);    // return a - b (int64)
SIMD_IWRAPPER_2_8(subs_epu8);     // return (b > a) ? 0 : (a - b) (uint8)
SIMD_IWRAPPER_2_8(packs_epi16);   // int16 --> int8    See documentation for _mm256_packs_epi16 and
                                  // _mm512_packs_epi16
SIMD_IWRAPPER_2_16(packs_epi32);  // int32 --> int16   See documentation for _mm256_packs_epi32 and
                                  // _mm512_packs_epi32
SIMD_IWRAPPER_2_8(packus_epi16);  // uint16 --> uint8  See documentation for _mm256_packus_epi16 and
                                  // _mm512_packus_epi16
SIMD_IWRAPPER_2_16(packus_epi32); // uint32 --> uint16 See documentation for _mm256_packus_epi32 and
                                  // _mm512_packus_epi32
SIMD_IWRAPPER_2_16(unpackhi_epi16);
SIMD_IWRAPPER_2_64(unpackhi_epi64);
SIMD_IWRAPPER_2_8(unpackhi_epi8);
SIMD_IWRAPPER_2_16(unpacklo_epi16);
SIMD_IWRAPPER_2_64(unpacklo_epi64);
SIMD_IWRAPPER_2_8(unpacklo_epi8);

static SIMDINLINE uint32_t SIMDCALL movemask_epi8(Integer a)
{
    __mmask64 m = 0xffffffffull;
    return static_cast<uint32_t>(_mm512_mask_test_epi8_mask(m, __conv(a), _mm512_set1_epi8(0x80)));
}

#undef SIMD_DWRAPPER_1_
#undef SIMD_DWRAPPER_1
#undef SIMD_DWRAPPER_1I_
#undef SIMD_DWRAPPER_1I
#undef SIMD_DWRAPPER_2_
#undef SIMD_DWRAPPER_2
#undef SIMD_DWRAPPER_2I
#undef SIMD_IWRAPPER_1_
#undef SIMD_IWRAPPER_1_8
#undef SIMD_IWRAPPER_1_16
#undef SIMD_IWRAPPER_1_64
#undef SIMD_IWRAPPER_1I_
#undef SIMD_IWRAPPER_1I_8
#undef SIMD_IWRAPPER_1I_16
#undef SIMD_IWRAPPER_1I_64
#undef SIMD_IWRAPPER_2_
#undef SIMD_IWRAPPER_2_8
#undef SIMD_IWRAPPER_2_16
#undef SIMD_IWRAPPER_2_64
