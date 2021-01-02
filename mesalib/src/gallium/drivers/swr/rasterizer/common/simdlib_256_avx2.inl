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
#if !defined(__SIMD_LIB_AVX2_HPP__)
#error Do not include this file directly, use "simdlib.hpp" instead.
#endif

//============================================================================
// SIMD256 AVX (2) implementation
//
// Since this implementation inherits from the AVX (1) implementation,
// the only operations below ones that replace AVX (1) operations.
// Mostly these are integer operations that are no longer emulated with SSE
//============================================================================

#define SIMD_IWRAPPER_1(op) \
    static SIMDINLINE Integer SIMDCALL op(Integer const& a) { return _mm256_##op(a); }

#define SIMD_IWRAPPER_1L(op)                                \
    static SIMDINLINE Integer SIMDCALL op(Integer const& a) \
    {                                                       \
        return _mm256_##op(_mm256_castsi256_si128(a));      \
    }

#define SIMD_IWRAPPER_1I(op)                                \
    template <int ImmT>                                     \
    static SIMDINLINE Integer SIMDCALL op(Integer const& a) \
    {                                                       \
        return _mm256_##op(a, ImmT);                        \
    }

#define SIMD_IWRAPPER_1I_(op, intrin)                       \
    template <int ImmT>                                     \
    static SIMDINLINE Integer SIMDCALL op(Integer const& a) \
    {                                                       \
        return _mm256_##intrin(a, ImmT);                    \
    }

#define SIMD_IWRAPPER_2_(op, intrin)                                          \
    static SIMDINLINE Integer SIMDCALL op(Integer const& a, Integer const& b) \
    {                                                                         \
        return _mm256_##intrin(a, b);                                         \
    }

#define SIMD_IWRAPPER_2(op)                                                   \
    static SIMDINLINE Integer SIMDCALL op(Integer const& a, Integer const& b) \
    {                                                                         \
        return _mm256_##op(a, b);                                             \
    }

#define SIMD_IWRAPPER_2I(op)                                                  \
    template <int ImmT>                                                       \
    static SIMDINLINE Integer SIMDCALL op(Integer const& a, Integer const& b) \
    {                                                                         \
        return _mm256_##op(a, b, ImmT);                                       \
    }

#define SIMD_IWRAPPER_2I(op)                                                  \
    template <int ImmT>                                                       \
    static SIMDINLINE Integer SIMDCALL op(Integer const& a, Integer const& b) \
    {                                                                         \
        return _mm256_##op(a, b, ImmT);                                       \
    }


//-----------------------------------------------------------------------
// Floating point arithmetic operations
//-----------------------------------------------------------------------
static SIMDINLINE Float SIMDCALL fmadd_ps(Float const& a,
                                          Float const& b,
                                          Float const& c) // return (a * b) + c
{
    return _mm256_fmadd_ps(a, b, c);
}

//-----------------------------------------------------------------------
// Integer (various width) arithmetic operations
//-----------------------------------------------------------------------
SIMD_IWRAPPER_1(abs_epi32); // return absolute_value(a) (int32)
SIMD_IWRAPPER_2(add_epi32); // return a + b (int32)
SIMD_IWRAPPER_2(add_epi8);  // return a + b (int8)
SIMD_IWRAPPER_2(adds_epu8); // return ((a + b) > 0xff) ? 0xff : (a + b) (uint8)
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
SIMD_IWRAPPER_2(subs_epu8); // return (b > a) ? 0 : (a - b) (uint8)

//-----------------------------------------------------------------------
// Logical operations
//-----------------------------------------------------------------------
#if _MSC_VER >= 1920 // && _MSC_FULL_VER < [some_fixed_version]
// Some versions of MSVC 2019 don't handle constant folding of and_si() correctly.
// Using and_ps instead inhibits the compiler's constant folding and actually issues
// the and intrinsic even though both inputs are constant values.
#else
// Use native integer and intrinsic
SIMD_IWRAPPER_2_(and_si, and_si256); // return a & b       (int)
#endif
SIMD_IWRAPPER_2_(andnot_si, andnot_si256); // return (~a) & b    (int)
SIMD_IWRAPPER_2_(or_si, or_si256);         // return a | b       (int)
SIMD_IWRAPPER_2_(xor_si, xor_si256);       // return a ^ b       (int)

//-----------------------------------------------------------------------
// Shift operations
//-----------------------------------------------------------------------
SIMD_IWRAPPER_1I(slli_epi32);           // return a << ImmT
SIMD_IWRAPPER_2(sllv_epi32);            // return a << b      (uint32)
SIMD_IWRAPPER_1I(srai_epi32);           // return a >> ImmT   (int32)
SIMD_IWRAPPER_1I(srli_epi32);           // return a >> ImmT   (uint32)
SIMD_IWRAPPER_2(srlv_epi32);            // return a >> b      (uint32)
SIMD_IWRAPPER_1I_(srli_si, srli_si256); // return a >> (ImmT*8) (uint)

template <int ImmT> // same as srli_si, but with Float cast to int
static SIMDINLINE Float SIMDCALL srlisi_ps(Float const& a)
{
    return castsi_ps(srli_si<ImmT>(castps_si(a)));
}

//-----------------------------------------------------------------------
// Conversion operations
//-----------------------------------------------------------------------
SIMD_IWRAPPER_1L(cvtepu8_epi16);  // return (int16)a    (uint8 --> int16)
SIMD_IWRAPPER_1L(cvtepu8_epi32);  // return (int32)a    (uint8 --> int32)
SIMD_IWRAPPER_1L(cvtepu16_epi32); // return (int32)a    (uint16 --> int32)
SIMD_IWRAPPER_1L(cvtepu16_epi64); // return (int64)a    (uint16 --> int64)
SIMD_IWRAPPER_1L(cvtepu32_epi64); // return (int64)a    (uint32 --> int64)

//-----------------------------------------------------------------------
// Comparison operations
//-----------------------------------------------------------------------
SIMD_IWRAPPER_2(cmpeq_epi8);  // return a == b (int8)
SIMD_IWRAPPER_2(cmpeq_epi16); // return a == b (int16)
SIMD_IWRAPPER_2(cmpeq_epi32); // return a == b (int32)
SIMD_IWRAPPER_2(cmpeq_epi64); // return a == b (int64)
SIMD_IWRAPPER_2(cmpgt_epi8);  // return a > b (int8)
SIMD_IWRAPPER_2(cmpgt_epi16); // return a > b (int16)
SIMD_IWRAPPER_2(cmpgt_epi32); // return a > b (int32)
SIMD_IWRAPPER_2(cmpgt_epi64); // return a > b (int64)

static SIMDINLINE Integer SIMDCALL cmplt_epi32(Integer const& a,
                                               Integer const& b) // return a < b (int32)
{
    return cmpgt_epi32(b, a);
}

//-----------------------------------------------------------------------
// Blend / shuffle / permute operations
//-----------------------------------------------------------------------
SIMD_IWRAPPER_2I(blend_epi32); // return ImmT ? b : a  (int32)
SIMD_IWRAPPER_2(packs_epi16);  // See documentation for _mm256_packs_epi16 and _mm512_packs_epi16
SIMD_IWRAPPER_2(packs_epi32);  // See documentation for _mm256_packs_epi32 and _mm512_packs_epi32
SIMD_IWRAPPER_2(packus_epi16); // See documentation for _mm256_packus_epi16 and _mm512_packus_epi16
SIMD_IWRAPPER_2(packus_epi32); // See documentation for _mm256_packus_epi32 and _mm512_packus_epi32

template <int ImmT>
static SIMDINLINE Float SIMDCALL permute_ps(Float const& a)
{
    return _mm256_permute_ps(a, ImmT);
}

SIMD_IWRAPPER_2_(permute_epi32, permutevar8x32_epi32);

static SIMDINLINE Float SIMDCALL
                        permute_ps(Float const& a, Integer const& swiz) // return a[swiz[i]] for each 32-bit lane i (float)
{
    return _mm256_permutevar8x32_ps(a, swiz);
}

SIMD_IWRAPPER_1I(shuffle_epi32);
template <int ImmT>
static SIMDINLINE Integer SIMDCALL shuffle_epi64(Integer const& a, Integer const& b)
{
    return castpd_si(shuffle_pd<ImmT>(castsi_pd(a), castsi_pd(b)));
}
SIMD_IWRAPPER_2(shuffle_epi8);
SIMD_IWRAPPER_2(unpackhi_epi16);
SIMD_IWRAPPER_2(unpackhi_epi32);
SIMD_IWRAPPER_2(unpackhi_epi64);
SIMD_IWRAPPER_2(unpackhi_epi8);
SIMD_IWRAPPER_2(unpacklo_epi16);
SIMD_IWRAPPER_2(unpacklo_epi32);
SIMD_IWRAPPER_2(unpacklo_epi64);
SIMD_IWRAPPER_2(unpacklo_epi8);

//-----------------------------------------------------------------------
// Load / store operations
//-----------------------------------------------------------------------
template <ScaleFactor ScaleT = ScaleFactor::SF_1>
static SIMDINLINE Float SIMDCALL
                        i32gather_ps(float const* p, Integer const& idx) // return *(float*)(((int8*)p) + (idx * ScaleT))
{
    return _mm256_i32gather_ps(p, idx, static_cast<int>(ScaleT));
}

#if _MSC_VER == 1920 // && _MSC_FULL_VER < [some_fixed_version]
// Don't use _mm256_mask_i32gather_ps(), the compiler doesn't preserve the mask register
// correctly in early versions of MSVC 2019
#else
// for each element: (mask & (1 << 31)) ? (i32gather_ps<ScaleT>(p, idx), mask = 0) : old
template <ScaleFactor ScaleT = ScaleFactor::SF_1>
static SIMDINLINE Float SIMDCALL
                        mask_i32gather_ps(Float const& old, float const* p, Integer const& idx, Float const& mask)
{
    // g++ in debug mode needs the explicit .v suffix instead of relying on operator __m256()
    // Only for this intrinsic - not sure why. :(
    return _mm256_mask_i32gather_ps(old.v, p, idx.v, mask.v, static_cast<int>(ScaleT));
}
#endif

static SIMDINLINE uint32_t SIMDCALL movemask_epi8(Integer const& a)
{
    return static_cast<uint32_t>(_mm256_movemask_epi8(a));
}

//=======================================================================
// Legacy interface (available only in SIMD256 width)
//=======================================================================

#undef SIMD_IWRAPPER_1
#undef SIMD_IWRAPPER_1L
#undef SIMD_IWRAPPER_1I
#undef SIMD_IWRAPPER_1I_
#undef SIMD_IWRAPPER_2_
#undef SIMD_IWRAPPER_2
#undef SIMD_IWRAPPER_2I
#undef SIMD_IWRAPPER_2I
