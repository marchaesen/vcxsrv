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
 * @file formats.h
 *
 * @brief Definitions for SWR_FORMAT functions.
 *
 ******************************************************************************/
#pragma once

#include "utils.h"
#include "common/simdintrin.h"

//////////////////////////////////////////////////////////////////////////
/// PackTraits - Helpers for packing / unpacking same pixel sizes
//////////////////////////////////////////////////////////////////////////
template <uint32_t NumBits, bool Signed = false>
struct PackTraits
{
    static const uint32_t MyNumBits = NumBits;

    static simdscalar     loadSOA(const uint8_t* pSrc)                   = delete;
    static void           storeSOA(uint8_t* pDst, simdscalar const& src) = delete;
    static simdscalar     unpack(simdscalar& in)                         = delete;
    static simdscalar     pack(simdscalar& in)                           = delete;

    static simd16scalar  loadSOA_16(const uint8_t* pSrc)                  = delete;
    static void SIMDCALL storeSOA(uint8_t* pDst, simd16scalar const& src) = delete;
    static simd16scalar  unpack(simd16scalar& in)                         = delete;
    static simd16scalar  pack(simd16scalar& in)                           = delete;
};

//////////////////////////////////////////////////////////////////////////
/// PackTraits - Helpers for packing / unpacking unused channels
//////////////////////////////////////////////////////////////////////////
template <>
struct PackTraits<0, false>
{
    static const uint32_t MyNumBits = 0;

    static simdscalar loadSOA(const uint8_t* pSrc) { return _simd_setzero_ps(); }
    static void       storeSOA(uint8_t* pDst, simdscalar const& src) { return; }
    static simdscalar unpack(simdscalar& in) { return _simd_setzero_ps(); }
    static simdscalar pack(simdscalar& in) { return _simd_setzero_ps(); }

    static simd16scalar  loadSOA_16(const uint8_t* pSrc) { return _simd16_setzero_ps(); }
    static void SIMDCALL storeSOA(uint8_t* pDst, simd16scalar const& src) { return; }
    static simd16scalar  unpack(simd16scalar& in) { return _simd16_setzero_ps(); }
    static simd16scalar  pack(simd16scalar& in) { return _simd16_setzero_ps(); }
};

//////////////////////////////////////////////////////////////////////////
/// PackTraits - Helpers for packing / unpacking 8 bit unsigned channels
//////////////////////////////////////////////////////////////////////////
template <>
struct PackTraits<8, false>
{
    static const uint32_t MyNumBits = 8;

    static simdscalar loadSOA(const uint8_t* pSrc)
    {
#if KNOB_SIMD_WIDTH == 8
        __m256 result = _mm256_setzero_ps();
        __m128 vLo    = _mm_castpd_ps(_mm_load_sd((double*)pSrc));
        return _mm256_insertf128_ps(result, vLo, 0);
#else
#error Unsupported vector width
#endif
    }

    static void storeSOA(uint8_t* pDst, simdscalar const& src)
    {
        // store simd bytes
#if KNOB_SIMD_WIDTH == 8
        _mm_storel_pd((double*)pDst, _mm_castps_pd(_mm256_castps256_ps128(src)));
#else
#error Unsupported vector width
#endif
    }

    static simdscalar unpack(simdscalar& in)
    {
#if KNOB_SIMD_WIDTH == 8
#if KNOB_ARCH <= KNOB_ARCH_AVX
        __m128i src   = _mm_castps_si128(_mm256_castps256_ps128(in));
        __m128i resLo = _mm_cvtepu8_epi32(src);
        __m128i resHi =
            _mm_shuffle_epi8(src, _mm_set_epi32(0x80808007, 0x80808006, 0x80808005, 0x80808004));

        __m256i result = _mm256_castsi128_si256(resLo);
        result         = _mm256_insertf128_si256(result, resHi, 1);
        return simdscalar{_mm256_castsi256_ps(result)};
#else
        return _mm256_castsi256_ps(
            _mm256_cvtepu8_epi32(_mm_castps_si128(_mm256_castps256_ps128(in))));
#endif
#else
#error Unsupported vector width
#endif
    }

    static simdscalar pack(simdscalar& in)
    {
#if KNOB_SIMD_WIDTH == 8
        simdscalari src = _simd_castps_si(in);
        __m128i     res16 =
            _mm_packus_epi32(_mm256_castsi256_si128(src), _mm256_extractf128_si256(src, 1));
        __m128i res8 = _mm_packus_epi16(res16, _mm_undefined_si128());
        return _mm256_castsi256_ps(_mm256_castsi128_si256(res8));
#else
#error Unsupported vector width
#endif
    }

    static simd16scalar loadSOA_16(const uint8_t* pSrc)
    {
        simd16scalar result   = _simd16_setzero_ps();
        simdscalar   resultlo = _simd_setzero_ps();

        const __m128 src = _mm_load_ps(reinterpret_cast<const float*>(pSrc));

        resultlo = _mm256_insertf128_ps(resultlo, src, 0);
        result   = _simd16_insert_ps(result, resultlo, 0);

        return result;
    }

    static void SIMDCALL storeSOA(uint8_t* pDst, simd16scalar const& src)
    {
        // store simd16 bytes
        _mm_store_ps(reinterpret_cast<float*>(pDst),
                     _mm256_castps256_ps128(_simd16_extract_ps(src, 0)));
    }

    static simd16scalar unpack(simd16scalar& in)
    {
        simd4scalari  tmp    = _mm_castps_si128(_mm256_castps256_ps128(_simd16_extract_ps(in, 0)));
        simd16scalari result = _simd16_cvtepu8_epi32(tmp);

        return _simd16_castsi_ps(result);
    }

    static simd16scalar pack(simd16scalar& in)
    {
        // clang-format off

        simd16scalari result = _simd16_setzero_si();

        simdscalari inlo = _simd_castps_si(_simd16_extract_ps(in, 0));  // r0 r1 r2 r3 r4 r5 r6 r7 (32b)
        simdscalari inhi = _simd_castps_si(_simd16_extract_ps(in, 1));  // r8 r9 rA rB rC rD rE rF

        simdscalari permlo = _simd_permute2f128_si(inlo, inhi, 0x20);   // r0 r1 r2 r3 r8 r9 rA rB (32b)
        simdscalari permhi = _simd_permute2f128_si(inlo, inhi, 0x31);   // r4 r5 r6 r7 rC rD rE rF (32b)

        simdscalari pack = _simd_packus_epi32(permlo, permhi);          // r0 r1 r2 r3 r4 r5 r6 r7 r8 r9 rA rB rC rD rE rF (16b)

        const simdscalari zero = _simd_setzero_si();

        permlo = _simd_permute2f128_si(pack, zero, 0x20); // (2, 0)     // r0 r1 r2 r3 r4 r5 r6 r7 00 00 00 00 00 00 00 00 (16b)
        permhi = _simd_permute2f128_si(pack, zero, 0x31); // (3, 1)     // r8 r9 rA rB rC rD rE rF 00 00 00 00 00 00 00 00 (16b)

        pack = _simd_packus_epi16(permlo, permhi);                      // r0 r1 r2 r3 r4 r5 r6 r7 r8 r9 rA rB rC rD rE rF 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 (8b)

        result = _simd16_insert_si(result, pack, 0);

        return _simd16_castsi_ps(result);

        // clang-format on
    }
};

//////////////////////////////////////////////////////////////////////////
/// PackTraits - Helpers for packing / unpacking 8 bit signed channels
//////////////////////////////////////////////////////////////////////////
template <>
struct PackTraits<8, true>
{
    static const uint32_t MyNumBits = 8;

    static simdscalar loadSOA(const uint8_t* pSrc)
    {
#if KNOB_SIMD_WIDTH == 8
        __m256 result = _mm256_setzero_ps();
        __m128 vLo    = _mm_castpd_ps(_mm_load_sd((double*)pSrc));
        return _mm256_insertf128_ps(result, vLo, 0);
#else
#error Unsupported vector width
#endif
    }

    static void storeSOA(uint8_t* pDst, simdscalar const& src)
    {
        // store simd bytes
#if KNOB_SIMD_WIDTH == 8
        _mm_storel_pd((double*)pDst, _mm_castps_pd(_mm256_castps256_ps128(src)));
#else
#error Unsupported vector width
#endif
    }

    static simdscalar unpack(simdscalar& in)
    {
#if KNOB_SIMD_WIDTH == 8
#if KNOB_ARCH <= KNOB_ARCH_AVX
        SWR_INVALID("I think this may be incorrect.");
        __m128i src   = _mm_castps_si128(_mm256_castps256_ps128(in));
        __m128i resLo = _mm_cvtepi8_epi32(src);
        __m128i resHi =
            _mm_shuffle_epi8(src, _mm_set_epi32(0x80808007, 0x80808006, 0x80808005, 0x80808004));

        __m256i result = _mm256_castsi128_si256(resLo);
        result         = _mm256_insertf128_si256(result, resHi, 1);
        return _mm256_castsi256_ps(result);
#else
        return _mm256_castsi256_ps(
            _mm256_cvtepi8_epi32(_mm_castps_si128(_mm256_castps256_ps128(in))));
#endif
#else
#error Unsupported vector width
#endif
    }

    static simdscalar pack(simdscalar& in)
    {
#if KNOB_SIMD_WIDTH == 8
        simdscalari src = _simd_castps_si(in);
        __m128i     res16 =
            _mm_packs_epi32(_mm256_castsi256_si128(src), _mm256_extractf128_si256(src, 1));
        __m128i res8 = _mm_packs_epi16(res16, _mm_undefined_si128());
        return _mm256_castsi256_ps(_mm256_castsi128_si256(res8));
#else
#error Unsupported vector width
#endif
    }

    static simd16scalar loadSOA_16(const uint8_t* pSrc)
    {
        simd16scalar result   = _simd16_setzero_ps();
        simdscalar   resultlo = _simd_setzero_ps();

        const __m128 src = _mm_load_ps(reinterpret_cast<const float*>(pSrc));

        resultlo = _mm256_insertf128_ps(resultlo, src, 0);
        result   = _simd16_insert_ps(result, resultlo, 0);

        return result;
    }

    static void SIMDCALL storeSOA(uint8_t* pDst, simd16scalar const& src)
    {
        // store simd16 bytes
        _mm_store_ps(reinterpret_cast<float*>(pDst),
                     _mm256_castps256_ps128(_simd16_extract_ps(src, 0)));
    }

    static simd16scalar unpack(simd16scalar& in)
    {
        simd4scalari  tmp    = _mm_castps_si128(_mm256_castps256_ps128(_simd16_extract_ps(in, 0)));
        simd16scalari result = _simd16_cvtepu8_epi32(tmp);

        return _simd16_castsi_ps(result);
    }

    static simd16scalar pack(simd16scalar& in)
    {
        // clang-format off

        simd16scalari result = _simd16_setzero_si();

        simdscalari inlo = _simd_castps_si(_simd16_extract_ps(in, 0));  // r0 r1 r2 r3 r4 r5 r6 r7 (32b)
        simdscalari inhi = _simd_castps_si(_simd16_extract_ps(in, 1));  // r8 r9 rA rB rC rD rE rF

        simdscalari permlo = _simd_permute2f128_si(inlo, inhi, 0x20);   // r0 r1 r2 r3 r8 r9 rA rB (32b)
        simdscalari permhi = _simd_permute2f128_si(inlo, inhi, 0x31);   // r4 r5 r6 r7 rC rD rE rF (32b)

        simdscalari pack = _simd_packs_epi32(permlo, permhi);           // r0 r1 r2 r3 r4 r5 r6 r7 r8 r9 rA rB rC rD rE rF (16b)

        const simdscalari zero = _simd_setzero_si();

        permlo = _simd_permute2f128_si(pack, zero, 0x20); // (2, 0)     // r0 r1 r2 r3 r4 r5 r6 r7 00 00 00 00 00 00 00 00 (16b)
        permhi = _simd_permute2f128_si(pack, zero, 0x31); // (3, 1)     // r8 r9 rA rB rC rD rE rF 00 00 00 00 00 00 00 00 (16b)

        pack = _simd_packs_epi16(permlo, permhi);                       // r0 r1 r2 r3 r4 r5 r6 r7 r8 r9 rA rB rC rD rE rF 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 (8b)

        result = _simd16_insert_si(result, pack, 0);

        return _simd16_castsi_ps(result);

        // clang-format on
    }
};

//////////////////////////////////////////////////////////////////////////
/// PackTraits - Helpers for packing / unpacking 16 bit unsigned channels
//////////////////////////////////////////////////////////////////////////
template <>
struct PackTraits<16, false>
{
    static const uint32_t MyNumBits = 16;

    static simdscalar loadSOA(const uint8_t* pSrc)
    {
#if KNOB_SIMD_WIDTH == 8
        __m256 result = _mm256_setzero_ps();
        __m128 vLo    = _mm_load_ps((const float*)pSrc);
        return _mm256_insertf128_ps(result, vLo, 0);
#else
#error Unsupported vector width
#endif
    }

    static void storeSOA(uint8_t* pDst, simdscalar const& src)
    {
#if KNOB_SIMD_WIDTH == 8
        // store 16B (2B * 8)
        _mm_store_ps((float*)pDst, _mm256_castps256_ps128(src));
#else
#error Unsupported vector width
#endif
    }

    static simdscalar unpack(simdscalar& in)
    {
#if KNOB_SIMD_WIDTH == 8
#if KNOB_ARCH <= KNOB_ARCH_AVX
        __m128i src   = _mm_castps_si128(_mm256_castps256_ps128(in));
        __m128i resLo = _mm_cvtepu16_epi32(src);
        __m128i resHi =
            _mm_shuffle_epi8(src, _mm_set_epi32(0x80800F0E, 0x80800D0C, 0x80800B0A, 0x80800908));

        __m256i result = _mm256_castsi128_si256(resLo);
        result         = _mm256_insertf128_si256(result, resHi, 1);
        return _mm256_castsi256_ps(result);
#else
        return _mm256_castsi256_ps(
            _mm256_cvtepu16_epi32(_mm_castps_si128(_mm256_castps256_ps128(in))));
#endif
#else
#error Unsupported vector width
#endif
    }

    static simdscalar pack(simdscalar& in)
    {
#if KNOB_SIMD_WIDTH == 8
        simdscalari src = _simd_castps_si(in);
        __m256i     res = _mm256_castsi128_si256(
            _mm_packus_epi32(_mm256_castsi256_si128(src), _mm256_extractf128_si256(src, 1)));
        return _mm256_castsi256_ps(res);
#else
#error Unsupported vector width
#endif
    }

    static simd16scalar loadSOA_16(const uint8_t* pSrc)
    {
        simd16scalar result = _simd16_setzero_ps();

        simdscalar resultlo = _simd_load_ps(reinterpret_cast<const float*>(pSrc));

        result = _simd16_insert_ps(result, resultlo, 0);

        return result;
    }

    static void SIMDCALL storeSOA(uint8_t* pDst, simd16scalar const& src)
    {
        _simd_store_ps(reinterpret_cast<float*>(pDst), _simd16_extract_ps(src, 0));
    }

    static simd16scalar unpack(simd16scalar& in)
    {
        simd16scalari result = _simd16_cvtepu16_epi32(_simd_castps_si(_simd16_extract_ps(in, 0)));

        return _simd16_castsi_ps(result);
    }

    static simd16scalar pack(simd16scalar& in)
    {
        // clang-format off

        const simd16scalari zero = _simd16_setzero_si();

        simd16scalari permlo = _simd16_permute2f128_si(_simd16_castps_si(in), zero, 0x08);  // (0, 0, 2, 0) // r0 r1 r2 r3 r8 r9 rA rB 00 00 00 00 00 00 00 00 (32b)
        simd16scalari permhi = _simd16_permute2f128_si(_simd16_castps_si(in), zero, 0x0D);  // (0, 0, 3, 1) // r4 r5 r6 r7 rC rD rE rF 00 00 00 00 00 00 00 00

        simd16scalari result = _simd16_packus_epi32(permlo, permhi);                        // r0 r1 r2 r3 r4 r5 r6 r7 r8 r9 rA rB rC rD rE rF 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 (16b)

        return _simd16_castsi_ps(result);

        // clang-format on
    }
};

//////////////////////////////////////////////////////////////////////////
/// PackTraits - Helpers for packing / unpacking 16 bit signed channels
//////////////////////////////////////////////////////////////////////////
template <>
struct PackTraits<16, true>
{
    static const uint32_t MyNumBits = 16;

    static simdscalar loadSOA(const uint8_t* pSrc)
    {
#if KNOB_SIMD_WIDTH == 8
        __m256 result = _mm256_setzero_ps();
        __m128 vLo    = _mm_load_ps((const float*)pSrc);
        return _mm256_insertf128_ps(result, vLo, 0);
#else
#error Unsupported vector width
#endif
    }

    static void storeSOA(uint8_t* pDst, simdscalar const& src)
    {
#if KNOB_SIMD_WIDTH == 8
        // store 16B (2B * 8)
        _mm_store_ps((float*)pDst, _mm256_castps256_ps128(src));
#else
#error Unsupported vector width
#endif
    }

    static simdscalar unpack(simdscalar& in)
    {
#if KNOB_SIMD_WIDTH == 8
#if KNOB_ARCH <= KNOB_ARCH_AVX
        SWR_INVALID("I think this may be incorrect.");
        __m128i src   = _mm_castps_si128(_mm256_castps256_ps128(in));
        __m128i resLo = _mm_cvtepi16_epi32(src);
        __m128i resHi =
            _mm_shuffle_epi8(src, _mm_set_epi32(0x80800F0E, 0x80800D0C, 0x80800B0A, 0x80800908));

        __m256i result = _mm256_castsi128_si256(resLo);
        result         = _mm256_insertf128_si256(result, resHi, 1);
        return _mm256_castsi256_ps(result);
#else
        return _mm256_castsi256_ps(
            _mm256_cvtepi16_epi32(_mm_castps_si128(_mm256_castps256_ps128(in))));
#endif
#else
#error Unsupported vector width
#endif
    }

    static simdscalar pack(simdscalar& in)
    {
#if KNOB_SIMD_WIDTH == 8
        simdscalari src = _simd_castps_si(in);
        __m256i     res = _mm256_castsi128_si256(
            _mm_packs_epi32(_mm256_castsi256_si128(src), _mm256_extractf128_si256(src, 1)));
        return _mm256_castsi256_ps(res);
#else
#error Unsupported vector width
#endif
    }

    static simd16scalar loadSOA_16(const uint8_t* pSrc)
    {
        simd16scalar result = _simd16_setzero_ps();

        simdscalar resultlo = _simd_load_ps(reinterpret_cast<const float*>(pSrc));

        result = _simd16_insert_ps(result, resultlo, 0);

        return result;
    }

    static void SIMDCALL storeSOA(uint8_t* pDst, simd16scalar const& src)
    {
        _simd_store_ps(reinterpret_cast<float*>(pDst), _simd16_extract_ps(src, 0));
    }

    static simd16scalar unpack(simd16scalar& in)
    {
        simd16scalari result = _simd16_cvtepu16_epi32(_simd_castps_si(_simd16_extract_ps(in, 0)));

        return _simd16_castsi_ps(result);
    }

    static simd16scalar pack(simd16scalar& in)
    {
        // clang-format off

        const simd16scalari zero = _simd16_setzero_si();

        simd16scalari permlo = _simd16_permute2f128_si(_simd16_castps_si(in), zero, 0x08);  // (0, 0, 2, 0) // r0 r1 r2 r3 r8 r9 rA rB 00 00 00 00 00 00 00 00 (32b)
        simd16scalari permhi = _simd16_permute2f128_si(_simd16_castps_si(in), zero, 0x0D);  // (0, 0, 3, 1) // r4 r5 r6 r7 rC rD rE rF 00 00 00 00 00 00 00 00

        simd16scalari result = _simd16_packs_epi32(permlo, permhi);                         // r0 r1 r2 r3 r4 r5 r6 r7 r8 r9 rA rB rC rD rE rF 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 (16b)

        return _simd16_castsi_ps(result);

        // clang-format on
    }
};

//////////////////////////////////////////////////////////////////////////
/// PackTraits - Helpers for packing / unpacking 32 bit channels
//////////////////////////////////////////////////////////////////////////
template <>
struct PackTraits<32, false>
{
    static const uint32_t MyNumBits = 32;

    static simdscalar loadSOA(const uint8_t* pSrc) { return _simd_load_ps((const float*)pSrc); }
    static void       storeSOA(uint8_t* pDst, simdscalar const& src)
    {
        _simd_store_ps((float*)pDst, src);
    }
    static simdscalar unpack(simdscalar& in) { return in; }
    static simdscalar pack(simdscalar& in) { return in; }

    static simd16scalar loadSOA_16(const uint8_t* pSrc)
    {
        return _simd16_load_ps(reinterpret_cast<const float*>(pSrc));
    }

    static void SIMDCALL storeSOA(uint8_t* pDst, simd16scalar const& src)
    {
        _simd16_store_ps(reinterpret_cast<float*>(pDst), src);
    }

    static simd16scalar unpack(simd16scalar& in) { return in; }

    static simd16scalar pack(simd16scalar& in) { return in; }
};

//////////////////////////////////////////////////////////////////////////
/// TypeTraits - Format type traits.
//////////////////////////////////////////////////////////////////////////
template <SWR_TYPE type, uint32_t NumBits>
struct TypeTraits : PackTraits<NumBits>
{
    static const SWR_TYPE MyType = type;
    static float          toFloat() { return 0.0; }
    static float          fromFloat()
    {
        SWR_NOT_IMPL;
        return 0.0;
    }
    static simdscalar convertSrgb(simdscalar& in)
    {
        SWR_NOT_IMPL;
        return _simd_setzero_ps();
    }
};

//////////////////////////////////////////////////////////////////////////
/// TypeTraits - Format type traits specialization for UINT8
//////////////////////////////////////////////////////////////////////////
template <>
struct TypeTraits<SWR_TYPE_UINT, 8> : PackTraits<8>
{
    static const SWR_TYPE MyType = SWR_TYPE_UINT;
    static float          toFloat() { return 0.0; }
    static float          fromFloat()
    {
        SWR_NOT_IMPL;
        return 0.0;
    }
    static simdscalar convertSrgb(simdscalar& in)
    {
        SWR_NOT_IMPL;
        return _simd_setzero_ps();
    }
};

//////////////////////////////////////////////////////////////////////////
/// TypeTraits - Format type traits specialization for UINT8
//////////////////////////////////////////////////////////////////////////
template <>
struct TypeTraits<SWR_TYPE_SINT, 8> : PackTraits<8, true>
{
    static const SWR_TYPE MyType = SWR_TYPE_SINT;
    static float          toFloat() { return 0.0; }
    static float          fromFloat()
    {
        SWR_NOT_IMPL;
        return 0.0;
    }
    static simdscalar convertSrgb(simdscalar& in)
    {
        SWR_NOT_IMPL;
        return _simd_setzero_ps();
    }
};

//////////////////////////////////////////////////////////////////////////
/// TypeTraits - Format type traits specialization for UINT16
//////////////////////////////////////////////////////////////////////////
template <>
struct TypeTraits<SWR_TYPE_UINT, 16> : PackTraits<16>
{
    static const SWR_TYPE MyType = SWR_TYPE_UINT;
    static float          toFloat() { return 0.0; }
    static float          fromFloat()
    {
        SWR_NOT_IMPL;
        return 0.0;
    }
    static simdscalar convertSrgb(simdscalar& in)
    {
        SWR_NOT_IMPL;
        return _simd_setzero_ps();
    }
};

//////////////////////////////////////////////////////////////////////////
/// TypeTraits - Format type traits specialization for SINT16
//////////////////////////////////////////////////////////////////////////
template <>
struct TypeTraits<SWR_TYPE_SINT, 16> : PackTraits<16, true>
{
    static const SWR_TYPE MyType = SWR_TYPE_SINT;
    static float          toFloat() { return 0.0; }
    static float          fromFloat()
    {
        SWR_NOT_IMPL;
        return 0.0;
    }
    static simdscalar convertSrgb(simdscalar& in)
    {
        SWR_NOT_IMPL;
        return _simd_setzero_ps();
    }
};

//////////////////////////////////////////////////////////////////////////
/// TypeTraits - Format type traits specialization for UINT32
//////////////////////////////////////////////////////////////////////////
template <>
struct TypeTraits<SWR_TYPE_UINT, 32> : PackTraits<32>
{
    static const SWR_TYPE MyType = SWR_TYPE_UINT;
    static float          toFloat() { return 0.0; }
    static float          fromFloat()
    {
        SWR_NOT_IMPL;
        return 0.0;
    }
    static simdscalar convertSrgb(simdscalar& in)
    {
        SWR_NOT_IMPL;
        return _simd_setzero_ps();
    }
};

//////////////////////////////////////////////////////////////////////////
/// TypeTraits - Format type traits specialization for UINT32
//////////////////////////////////////////////////////////////////////////
template <>
struct TypeTraits<SWR_TYPE_SINT, 32> : PackTraits<32>
{
    static const SWR_TYPE MyType = SWR_TYPE_SINT;
    static float          toFloat() { return 0.0; }
    static float          fromFloat()
    {
        SWR_NOT_IMPL;
        return 0.0;
    }
    static simdscalar convertSrgb(simdscalar& in)
    {
        SWR_NOT_IMPL;
        return _simd_setzero_ps();
    }
};

//////////////////////////////////////////////////////////////////////////
/// TypeTraits - Format type traits specialization for UNORM5
//////////////////////////////////////////////////////////////////////////
template <>
struct TypeTraits<SWR_TYPE_UNORM, 5> : PackTraits<5>
{
    static const SWR_TYPE MyType = SWR_TYPE_UNORM;
    static float          toFloat() { return 1.0f / 31.0f; }
    static float          fromFloat() { return 31.0f; }
    static simdscalar     convertSrgb(simdscalar& in)
    {
        SWR_NOT_IMPL;
        return _simd_setzero_ps();
    }
};

//////////////////////////////////////////////////////////////////////////
/// TypeTraits - Format type traits specialization for UNORM6
//////////////////////////////////////////////////////////////////////////
template <>
struct TypeTraits<SWR_TYPE_UNORM, 6> : PackTraits<6>
{
    static const SWR_TYPE MyType = SWR_TYPE_UNORM;
    static float          toFloat() { return 1.0f / 63.0f; }
    static float          fromFloat() { return 63.0f; }
    static simdscalar     convertSrgb(simdscalar& in)
    {
        SWR_NOT_IMPL;
        return _simd_setzero_ps();
    }
};

//////////////////////////////////////////////////////////////////////////
/// TypeTraits - Format type traits specialization for UNORM8
//////////////////////////////////////////////////////////////////////////
template <>
struct TypeTraits<SWR_TYPE_UNORM, 8> : PackTraits<8>
{
    static const SWR_TYPE MyType = SWR_TYPE_UNORM;
    static float          toFloat() { return 1.0f / 255.0f; }
    static float          fromFloat() { return 255.0f; }
    static simdscalar     convertSrgb(simdscalar& in)
    {
        SWR_NOT_IMPL;
        return _simd_setzero_ps();
    }
};

//////////////////////////////////////////////////////////////////////////
/// TypeTraits - Format type traits specialization for UNORM8
//////////////////////////////////////////////////////////////////////////
template <>
struct TypeTraits<SWR_TYPE_SNORM, 8> : PackTraits<8, true>
{
    static const SWR_TYPE MyType = SWR_TYPE_SNORM;
    static float          toFloat() { return 1.0f / 127.0f; }
    static float          fromFloat() { return 127.0f; }
    static simdscalar     convertSrgb(simdscalar& in)
    {
        SWR_NOT_IMPL;
        return _simd_setzero_ps();
    }
};

//////////////////////////////////////////////////////////////////////////
/// TypeTraits - Format type traits specialization for UNORM16
//////////////////////////////////////////////////////////////////////////
template <>
struct TypeTraits<SWR_TYPE_UNORM, 16> : PackTraits<16>
{
    static const SWR_TYPE MyType = SWR_TYPE_UNORM;
    static float          toFloat() { return 1.0f / 65535.0f; }
    static float          fromFloat() { return 65535.0f; }
    static simdscalar     convertSrgb(simdscalar& in)
    {
        SWR_NOT_IMPL;
        return _simd_setzero_ps();
    }
};

//////////////////////////////////////////////////////////////////////////
/// TypeTraits - Format type traits specialization for SNORM16
//////////////////////////////////////////////////////////////////////////
template <>
struct TypeTraits<SWR_TYPE_SNORM, 16> : PackTraits<16, true>
{
    static const SWR_TYPE MyType = SWR_TYPE_UNORM;
    static float          toFloat() { return 1.0f / 32767.0f; }
    static float          fromFloat() { return 32767.0f; }
    static simdscalar     convertSrgb(simdscalar& in)
    {
        SWR_NOT_IMPL;
        return _simd_setzero_ps();
    }
};

//////////////////////////////////////////////////////////////////////////
/// TypeTraits - Format type traits specialization for UNORM24
//////////////////////////////////////////////////////////////////////////
template <>
struct TypeTraits<SWR_TYPE_UNORM, 24> : PackTraits<32>
{
    static const SWR_TYPE MyType = SWR_TYPE_UNORM;
    static float          toFloat() { return 1.0f / 16777215.0f; }
    static float          fromFloat() { return 16777215.0f; }
    static simdscalar     convertSrgb(simdscalar& in)
    {
        SWR_NOT_IMPL;
        return _simd_setzero_ps();
    }
};

//////////////////////////////////////////////////////////////////////////
// FLOAT Specializations from here on...
//////////////////////////////////////////////////////////////////////////
#define TO_M128i(a) _mm_castps_si128(a)
#define TO_M128(a) _mm_castsi128_ps(a)

#include "math.h"

template <unsigned expnum, unsigned expden, unsigned coeffnum, unsigned coeffden>
inline static __m128 fastpow(__m128 arg)
{
    __m128 ret = arg;

    static const __m128 factor =
        _mm_set1_ps(exp2(127.0f * expden / expnum - 127.0f) *
                    powf(1.0f * coeffnum / coeffden, 1.0f * expden / expnum));

    // Apply a constant pre-correction factor.
    ret = _mm_mul_ps(ret, factor);

    // Reinterpret arg as integer to obtain logarithm.
    // asm("cvtdq2ps %1, %0" : "=x" (ret) : "x" (ret));
    ret = _mm_cvtepi32_ps(_mm_castps_si128(ret));

    // Multiply logarithm by power.
    ret = _mm_mul_ps(ret, _mm_set1_ps(1.0f * expnum / expden));

    // Convert back to "integer" to exponentiate.
    // asm("cvtps2dq %1, %0" : "=x" (ret) : "x" (ret));
    ret = _mm_castsi128_ps(_mm_cvtps_epi32(ret));

    return ret;
}

inline static __m128 pow512_4(__m128 arg)
{
    // 5/12 is too small, so compute the 4th root of 20/12 instead.
    // 20/12 = 5/3 = 1 + 2/3 = 2 - 1/3. 2/3 is a suitable argument for fastpow.
    // weighting coefficient: a^-1/2 = 2 a; a = 2^-2/3
    __m128 xf    = fastpow<2, 3, int(0.629960524947437 * 1e9), int(1e9)>(arg);
    __m128 xover = _mm_mul_ps(arg, xf);

    __m128 xfm1   = _mm_rsqrt_ps(xf);
    __m128 x2     = _mm_mul_ps(arg, arg);
    __m128 xunder = _mm_mul_ps(x2, xfm1);

    // sqrt2 * over + 2 * sqrt2 * under
    __m128 xavg = _mm_mul_ps(_mm_set1_ps(1.0f / (3.0f * 0.629960524947437f) * 0.999852f),
                             _mm_add_ps(xover, xunder));

    xavg = _mm_mul_ps(xavg, _mm_rsqrt_ps(xavg));
    xavg = _mm_mul_ps(xavg, _mm_rsqrt_ps(xavg));
    return xavg;
}

inline static __m128 powf_wrapper(__m128 Base, float Exp)
{
    float* f = (float*)(&Base);

    return _mm_set_ps(powf(f[3], Exp), powf(f[2], Exp), powf(f[1], Exp), powf(f[0], Exp));
}

static inline __m128 ConvertFloatToSRGB2(__m128& Src)
{
    // create a mask with 0xFFFFFFFF in the DWORDs where the source is <= the minimal SRGB float
    // value
    __m128i CmpToSRGBThresholdMask = TO_M128i(_mm_cmpnlt_ps(_mm_set1_ps(0.0031308f), Src));

    // squeeze the mask down to 16 bits (4 bits per DWORD)
    int CompareResult = _mm_movemask_epi8(CmpToSRGBThresholdMask);

    __m128 Result;

    //
    if (CompareResult == 0xFFFF)
    {
        // all DWORDs are <= the threshold
        Result = _mm_mul_ps(Src, _mm_set1_ps(12.92f));
    }
    else if (CompareResult == 0x0)
    {
        // all DWORDs are > the threshold
        __m128 fSrc_0RGB = Src;

        // --> 1.055f * c(1.0f/2.4f) - 0.055f
#if KNOB_USE_FAST_SRGB == TRUE
        // 1.0f / 2.4f is 5.0f / 12.0f which is used for approximation.
        __m128 f = pow512_4(fSrc_0RGB);
#else
        __m128 f = powf_wrapper(fSrc_0RGB, 1.0f / 2.4f);
#endif
        f      = _mm_mul_ps(f, _mm_set1_ps(1.055f));
        Result = _mm_sub_ps(f, _mm_set1_ps(0.055f));
    }
    else
    {
        // some DWORDs are <= the threshold and some are > threshold
        __m128 Src_0RGB_mul_denorm = _mm_mul_ps(Src, _mm_set1_ps(12.92f));

        __m128 fSrc_0RGB = Src;

        // --> 1.055f * c(1.0f/2.4f) - 0.055f
#if KNOB_USE_FAST_SRGB == TRUE
        // 1.0f / 2.4f is 5.0f / 12.0f which is used for approximation.
        __m128 f = pow512_4(fSrc_0RGB);
#else
        __m128 f = powf_wrapper(fSrc_0RGB, 1.0f / 2.4f);
#endif
        f = _mm_mul_ps(f, _mm_set1_ps(1.055f));
        f = _mm_sub_ps(f, _mm_set1_ps(0.055f));

        // Clear the alpha (is garbage after the sub)
        __m128i i = _mm_and_si128(TO_M128i(f),
                                  _mm_set_epi32(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF));

        __m128i LessThanPart = _mm_and_si128(CmpToSRGBThresholdMask, TO_M128i(Src_0RGB_mul_denorm));
        __m128i GreaterEqualPart = _mm_andnot_si128(CmpToSRGBThresholdMask, i);
        __m128i CombinedParts    = _mm_or_si128(LessThanPart, GreaterEqualPart);

        Result = TO_M128(CombinedParts);
    }

    return Result;
}

template <unsigned expnum, unsigned expden, unsigned coeffnum, unsigned coeffden>
inline static simd16scalar SIMDCALL fastpow(simd16scalar const& value)
{
    static const float factor1 = exp2(127.0f * expden / expnum - 127.0f) *
                                 powf(1.0f * coeffnum / coeffden, 1.0f * expden / expnum);

    // Apply a constant pre-correction factor.
    simd16scalar result = _simd16_mul_ps(value, _simd16_set1_ps(factor1));

    // Reinterpret arg as integer to obtain logarithm.
    // asm("cvtdq2ps %1, %0" : "=x" (result) : "x" (result));
    result = _simd16_cvtepi32_ps(_simd16_castps_si(result));

    // Multiply logarithm by power.
    result = _simd16_mul_ps(result, _simd16_set1_ps(1.0f * expnum / expden));

    // Convert back to "integer" to exponentiate.
    // asm("cvtps2dq %1, %0" : "=x" (result) : "x" (result));
    result = _simd16_castsi_ps(_simd16_cvtps_epi32(result));

    return result;
}

inline static simd16scalar SIMDCALL pow512_4(simd16scalar const& arg)
{
    // 5/12 is too small, so compute the 4th root of 20/12 instead.
    // 20/12 = 5/3 = 1 + 2/3 = 2 - 1/3. 2/3 is a suitable argument for fastpow.
    // weighting coefficient: a^-1/2 = 2 a; a = 2^-2/3
    simd16scalar xf    = fastpow<2, 3, int(0.629960524947437 * 1e9), int(1e9)>(arg);
    simd16scalar xover = _simd16_mul_ps(arg, xf);

    simd16scalar xfm1   = _simd16_rsqrt_ps(xf);
    simd16scalar x2     = _simd16_mul_ps(arg, arg);
    simd16scalar xunder = _simd16_mul_ps(x2, xfm1);

    // sqrt2 * over + 2 * sqrt2 * under
    simd16scalar xavg =
        _simd16_mul_ps(_simd16_set1_ps(1.0f / (3.0f * 0.629960524947437f) * 0.999852f),
                       _simd16_add_ps(xover, xunder));

    xavg = _simd16_mul_ps(xavg, _simd16_rsqrt_ps(xavg));
    xavg = _simd16_mul_ps(xavg, _simd16_rsqrt_ps(xavg));

    return xavg;
}

inline static simd16scalar SIMDCALL powf_wrapper(const simd16scalar& base, float exp)
{
    const float* f = reinterpret_cast<const float*>(&base);

    return _simd16_set_ps(powf(f[15], exp),
                          powf(f[14], exp),
                          powf(f[13], exp),
                          powf(f[12], exp),
                          powf(f[11], exp),
                          powf(f[10], exp),
                          powf(f[9], exp),
                          powf(f[8], exp),
                          powf(f[7], exp),
                          powf(f[6], exp),
                          powf(f[5], exp),
                          powf(f[4], exp),
                          powf(f[3], exp),
                          powf(f[2], exp),
                          powf(f[1], exp),
                          powf(f[0], exp));
}

// float to SRGB conversion formula
//
// if (value < 0.0031308f)
//     value *= 12.92f;
// else
//     value = 1.055f * pow(value, 1.0f / 2.4f) - 0.055f;
//
static inline simd16scalar ConvertFloatToSRGB2(const simd16scalar& value)
{
    // create a mask where the source is < the minimal SRGB float value
    const simd16mask mask = _simd16_cmplt_ps_mask(value, _simd16_set1_ps(0.0031308f));

    // if all elements are < the threshold, result = value * 12.92
    simd16scalar result = _simd16_mul_ps(value, _simd16_set1_ps(12.92f));

    if (_simd16_mask2int(mask) != 0xFFFF)
    {
        // some elements are >= threshold, result = 1.055 * power(value, 1.0 / 2.4) - 0.055
#if KNOB_USE_FAST_SRGB == TRUE
        // 1.0f / 2.4f is 5.0f / 12.0f which is used for approximation.
        simd16scalar result2 = pow512_4(value);
#else
        simd16scalar result2 = powf_wrapper(value, 1.0f / 2.4f);
#endif

        result2 = _simd16_mul_ps(result2, _simd16_set1_ps(1.055f));
        result2 = _simd16_sub_ps(result2, _simd16_set1_ps(0.055f));

#if (KNOB_ARCH == KNOB_ARCH_AVX512)
        // only native AVX512 can directly use the computed mask for the blend operation
        result = _mm512_mask_blend_ps(mask, result2, result);
#else
        result = _simd16_blendv_ps(
            result2, result, _simd16_cmplt_ps(value, _simd16_set1_ps(0.0031308f)));
#endif
    }

    return result;
}

//////////////////////////////////////////////////////////////////////////
/// TypeTraits - Format type traits specialization for FLOAT16
//////////////////////////////////////////////////////////////////////////
template <>
struct TypeTraits<SWR_TYPE_FLOAT, 16> : PackTraits<16>
{
    static const SWR_TYPE MyType = SWR_TYPE_FLOAT;
    static float          toFloat() { return 1.0f; }
    static float          fromFloat() { return 1.0f; }
    static simdscalar     convertSrgb(simdscalar& in)
    {
        SWR_NOT_IMPL;
        return _simd_setzero_ps();
    }

    static simdscalar pack(const simdscalar& in)
    {
#if KNOB_SIMD_WIDTH == 8
#if (KNOB_ARCH == KNOB_ARCH_AVX)
        // input is 8 packed float32, output is 8 packed float16
        simdscalari src = _simd_castps_si(in);

        static const uint32_t FLOAT_EXP_BITS      = 8;
        static const uint32_t FLOAT_MANTISSA_BITS = 23;
        static const uint32_t FLOAT_MANTISSA_MASK = (1U << FLOAT_MANTISSA_BITS) - 1;
        static const uint32_t FLOAT_EXP_MASK = ((1U << FLOAT_EXP_BITS) - 1) << FLOAT_MANTISSA_BITS;

        static const uint32_t HALF_EXP_BITS      = 5;
        static const uint32_t HALF_MANTISSA_BITS = 10;
        static const uint32_t HALF_EXP_MASK = ((1U << HALF_EXP_BITS) - 1) << HALF_MANTISSA_BITS;

        // minimum exponent required, exponents below this are flushed to 0.
        static const int32_t HALF_EXP_MIN   = -14;
        static const int32_t FLOAT_EXP_BIAS = 127;
        static const int32_t FLOAT_EXP_MIN  = HALF_EXP_MIN + FLOAT_EXP_BIAS;
        static const int32_t FLOAT_EXP_MIN_FTZ =
            FLOAT_EXP_MIN - (HALF_MANTISSA_BITS + 1); // +1 for the lack of implicit significand

        // maximum exponent required, exponents above this are set to infinity
        static const int32_t HALF_EXP_MAX  = 15;
        static const int32_t FLOAT_EXP_MAX = HALF_EXP_MAX + FLOAT_EXP_BIAS;

        const simdscalari vSignMask = _simd_set1_epi32(0x80000000);
        const simdscalari vExpMask  = _simd_set1_epi32(FLOAT_EXP_MASK);
        const simdscalari vManMask  = _simd_set1_epi32(FLOAT_MANTISSA_MASK);
        const simdscalari vExpMin =
            _simd_set1_epi32(FLOAT_EXP_MASK & uint32_t(FLOAT_EXP_MIN << FLOAT_MANTISSA_BITS));
        const simdscalari vExpMinFtz =
            _simd_set1_epi32(FLOAT_EXP_MASK & uint32_t(FLOAT_EXP_MIN_FTZ << FLOAT_MANTISSA_BITS));
        const simdscalari vExpMax =
            _simd_set1_epi32(FLOAT_EXP_MASK & uint32_t(FLOAT_EXP_MAX << FLOAT_MANTISSA_BITS));

        simdscalari vSign = _simd_and_si(src, vSignMask);
        simdscalari vExp  = _simd_and_si(src, vExpMask);
        simdscalari vMan  = _simd_and_si(src, vManMask);

        simdscalari vFTZMask    = _simd_cmplt_epi32(vExp, vExpMinFtz);
        simdscalari vDenormMask = _simd_andnot_si(vFTZMask, _simd_cmplt_epi32(vExp, vExpMin));
        simdscalari vInfMask    = _simd_cmpeq_epi32(vExpMask, vExp);
        simdscalari vClampMask  = _simd_andnot_si(vInfMask, _simd_cmplt_epi32(vExpMax, vExp));

        simdscalari vHalfExp = _simd_add_epi32(_simd_sub_epi32(vExp, vExpMin),
                                               _simd_set1_epi32(1U << FLOAT_MANTISSA_BITS));

        // pack output 16-bits into the lower 16-bits of each 32-bit channel
        simdscalari vDst =
            _simd_and_si(_simd_srli_epi32(vHalfExp, 13), _simd_set1_epi32(HALF_EXP_MASK));
        vDst = _simd_or_si(vDst, _simd_srli_epi32(vMan, FLOAT_MANTISSA_BITS - HALF_MANTISSA_BITS));

        // Flush To Zero
        vDst = _simd_andnot_si(vFTZMask, vDst);
        // Apply Infinites / NaN
        vDst = _simd_or_si(vDst, _simd_and_si(vInfMask, _simd_set1_epi32(HALF_EXP_MASK)));

        // Apply clamps
        vDst = _simd_andnot_si(vClampMask, vDst);
        vDst = _simd_or_si(vDst, _simd_and_si(vClampMask, _simd_set1_epi32(0x7BFF)));

        // Compute Denormals (subnormals)
        if (!_mm256_testz_si256(vDenormMask, vDenormMask))
        {
            uint32_t* pDenormMask = (uint32_t*)&vDenormMask;
            uint32_t* pExp        = (uint32_t*)&vExp;
            uint32_t* pMan        = (uint32_t*)&vMan;
            uint32_t* pDst        = (uint32_t*)&vDst;
            for (uint32_t i = 0; i < KNOB_SIMD_WIDTH; ++i)
            {
                if (pDenormMask[i])
                {
                    // Need to compute subnormal value
                    uint32_t exponent = pExp[i] >> FLOAT_MANTISSA_BITS;
                    uint32_t mantissa =
                        pMan[i] | (1U << FLOAT_MANTISSA_BITS); // Denorms include no "implicit" 1s.
                                                               // Make it explicit

                    pDst[i] = mantissa >> ((FLOAT_EXP_MIN - exponent) +
                                           (FLOAT_MANTISSA_BITS - HALF_MANTISSA_BITS));
                }
            }
        }

        // Add in sign bits
        vDst = _simd_or_si(vDst, _simd_srli_epi32(vSign, 16));

        // Pack to lower 128-bits
        vDst = _mm256_castsi128_si256(
            _mm_packus_epi32(_mm256_castsi256_si128(vDst), _mm256_extractf128_si256(vDst, 1)));

#if 0
#if !defined(NDEBUG)
        simdscalari vCheck = _mm256_castsi128_si256(_mm256_cvtps_ph(in, _MM_FROUND_TRUNC));

        for (uint32_t i = 0; i < 4; ++i)
        {
            SWR_ASSERT(vCheck.m256i_i32[i] == vDst.m256i_i32[i]);
        }
#endif
#endif

        return _simd_castsi_ps(vDst);

#else
        return _mm256_castsi256_ps(_mm256_castsi128_si256(_mm256_cvtps_ph(in, _MM_FROUND_TRUNC)));
#endif
#else
#error Unsupported vector width
#endif
    }

    static simdscalar unpack(const simdscalar& in)
    {
        // input is 8 packed float16, output is 8 packed float32
        SWR_NOT_IMPL; // @todo
        return _simd_setzero_ps();
    }

    static simd16scalar pack(const simd16scalar& in)
    {
        simd16scalari result   = _simd16_setzero_si();
        simdscalari   resultlo = _simd_setzero_si();

#if (KNOB_ARCH == KNOB_ARCH_AVX)
        simdscalar simdlo = pack(_simd16_extract_ps(in, 0));
        simdscalar simdhi = pack(_simd16_extract_ps(in, 1));

        __m128i templo = _simd_extractf128_si(_simd_castps_si(simdlo), 0);
        __m128i temphi = _simd_extractf128_si(_simd_castps_si(simdhi), 0);

#else
        __m128i templo = _mm256_cvtps_ph(_simd16_extract_ps(in, 0), _MM_FROUND_TRUNC);
        __m128i temphi = _mm256_cvtps_ph(_simd16_extract_ps(in, 1), _MM_FROUND_TRUNC);

#endif
        resultlo = _simd_insertf128_si(resultlo, templo, 0);
        resultlo = _simd_insertf128_si(resultlo, temphi, 1);

        result = _simd16_insert_si(result, resultlo, 0);

        return _simd16_castsi_ps(result);
    }

    static simd16scalar unpack(const simd16scalar& in)
    {
        // input is 16 packed float16, output is 16 packed float32
        SWR_NOT_IMPL; //  @todo
        return _simd16_setzero_ps();
    }
};

//////////////////////////////////////////////////////////////////////////
/// TypeTraits - Format type traits specialization for FLOAT32
//////////////////////////////////////////////////////////////////////////
template <>
struct TypeTraits<SWR_TYPE_FLOAT, 32> : PackTraits<32>
{
    static const SWR_TYPE    MyType = SWR_TYPE_FLOAT;
    static float             toFloat() { return 1.0f; }
    static float             fromFloat() { return 1.0f; }
    static inline simdscalar convertSrgb(simdscalar& in)
    {
#if KNOB_SIMD_WIDTH == 8
        __m128 srcLo = _mm256_extractf128_ps(in, 0);
        __m128 srcHi = _mm256_extractf128_ps(in, 1);

        srcLo = ConvertFloatToSRGB2(srcLo);
        srcHi = ConvertFloatToSRGB2(srcHi);

        in = _mm256_insertf128_ps(in, srcLo, 0);
        in = _mm256_insertf128_ps(in, srcHi, 1);
#else
#error Unsupported vector width
#endif
        return in;
    }

    static inline simd16scalar convertSrgb(simd16scalar& in) { return ConvertFloatToSRGB2(in); }
};

//////////////////////////////////////////////////////////////////////////
/// FormatIntType - Calculate base integer type for pixel components based
///                 on total number of bits.  Components can be smaller
///                 that this type, but the entire pixel must not be
///                 any smaller than this type.
//////////////////////////////////////////////////////////////////////////
template <uint32_t bits, bool bits8 = bits <= 8, bool bits16 = bits <= 16>
struct FormatIntType
{
    typedef uint32_t TYPE;
};

template <uint32_t bits>
struct FormatIntType<bits, true, true>
{
    typedef uint8_t TYPE;
};

template <uint32_t bits>
struct FormatIntType<bits, false, true>
{
    typedef uint16_t TYPE;
};

//////////////////////////////////////////////////////////////////////////
/// Format1 - Bitfield for single component formats.
//////////////////////////////////////////////////////////////////////////
template <uint32_t x>
union Format1
{
    typedef typename FormatIntType<x>::TYPE TYPE;
    struct
    {
        TYPE r : x;
    };

    ///@ The following are here to provide full template needed in Formats.
    struct
    {
        TYPE g : x;
    };
    struct
    {
        TYPE b : x;
    };
    struct
    {
        TYPE a : x;
    };
};

//////////////////////////////////////////////////////////////////////////
/// Format2 - Bitfield for 2 component formats.
//////////////////////////////////////////////////////////////////////////
template <uint32_t x, uint32_t y>
union Format2
{
    typedef typename FormatIntType<x + y>::TYPE TYPE;

    struct
    {
        TYPE r : x;
        TYPE g : y;
    };
    struct
    {
        ///@ The following are here to provide full template needed in Formats.
        TYPE b : x;
        TYPE a : y;
    };
};

//////////////////////////////////////////////////////////////////////////
/// Format3 - Bitfield for 3 component formats.
//////////////////////////////////////////////////////////////////////////
template <uint32_t x, uint32_t y, uint32_t z>
union Format3
{
    typedef typename FormatIntType<x + y + z>::TYPE TYPE;

    struct
    {
        TYPE r : x;
        TYPE g : y;
        TYPE b : z;
    };
    TYPE a; ///@note This is here to provide full template needed in Formats.
};

//////////////////////////////////////////////////////////////////////////
/// Format4 - Bitfield for 4 component formats.
//////////////////////////////////////////////////////////////////////////
template <uint32_t x, uint32_t y, uint32_t z, uint32_t w>
struct Format4
{
    typedef typename FormatIntType<x + y + z + w>::TYPE TYPE;

    TYPE r : x;
    TYPE g : y;
    TYPE b : z;
    TYPE a : w;
};

//////////////////////////////////////////////////////////////////////////
/// ComponentTraits - Default components
//////////////////////////////////////////////////////////////////////////
template <uint32_t x, uint32_t y, uint32_t z, uint32_t w>
struct Defaults
{
    INLINE static uint32_t GetDefault(uint32_t comp)
    {
        static const uint32_t defaults[4]{x, y, z, w};
        return defaults[comp];
    }
};

//////////////////////////////////////////////////////////////////////////
/// ComponentTraits - Component type traits.
//////////////////////////////////////////////////////////////////////////
template <SWR_TYPE X,
          uint32_t NumBitsX,
          SWR_TYPE Y        = SWR_TYPE_UNKNOWN,
          uint32_t NumBitsY = 0,
          SWR_TYPE Z        = SWR_TYPE_UNKNOWN,
          uint32_t NumBitsZ = 0,
          SWR_TYPE W        = SWR_TYPE_UNKNOWN,
          uint32_t NumBitsW = 0>
struct ComponentTraits
{
    INLINE static SWR_TYPE GetType(uint32_t comp)
    {
        static const SWR_TYPE CompType[4]{X, Y, Z, W};
        return CompType[comp];
    }

    INLINE static constexpr uint32_t GetConstBPC(uint32_t comp)
    {
        return (comp == 3) ? NumBitsW
                           : ((comp == 2) ? NumBitsZ : ((comp == 1) ? NumBitsY : NumBitsX));
    }

    INLINE static uint32_t GetBPC(uint32_t comp)
    {
        static const uint32_t MyBpc[4]{NumBitsX, NumBitsY, NumBitsZ, NumBitsW};
        return MyBpc[comp];
    }

    INLINE static bool isNormalized(uint32_t comp)
    {
        switch (comp)
        {
        case 0:
            return (X == SWR_TYPE_UNORM || X == SWR_TYPE_SNORM) ? true : false;
        case 1:
            return (Y == SWR_TYPE_UNORM || Y == SWR_TYPE_SNORM) ? true : false;
        case 2:
            return (Z == SWR_TYPE_UNORM || Z == SWR_TYPE_SNORM) ? true : false;
        case 3:
            return (W == SWR_TYPE_UNORM || W == SWR_TYPE_SNORM) ? true : false;
        }
        SWR_INVALID("Invalid component: %d", comp);
        return false;
    }

    INLINE static float toFloat(uint32_t comp)
    {
        switch (comp)
        {
        case 0:
            return TypeTraits<X, NumBitsX>::toFloat();
        case 1:
            return TypeTraits<Y, NumBitsY>::toFloat();
        case 2:
            return TypeTraits<Z, NumBitsZ>::toFloat();
        case 3:
            return TypeTraits<W, NumBitsW>::toFloat();
        }
        SWR_INVALID("Invalid component: %d", comp);
        return TypeTraits<X, NumBitsX>::toFloat();
    }

    INLINE static float fromFloat(uint32_t comp)
    {
        switch (comp)
        {
        case 0:
            return TypeTraits<X, NumBitsX>::fromFloat();
        case 1:
            return TypeTraits<Y, NumBitsY>::fromFloat();
        case 2:
            return TypeTraits<Z, NumBitsZ>::fromFloat();
        case 3:
            return TypeTraits<W, NumBitsW>::fromFloat();
        }
        SWR_INVALID("Invalid component: %d", comp);
        return TypeTraits<X, NumBitsX>::fromFloat();
    }

    INLINE static void loadSOA(uint32_t comp, const uint8_t* pSrc, simdscalar& dst)
    {
        switch (comp)
        {
        case 0:
            dst = TypeTraits<X, NumBitsX>::loadSOA(pSrc);
            return;
        case 1:
            dst = TypeTraits<Y, NumBitsY>::loadSOA(pSrc);
            return;
        case 2:
            dst = TypeTraits<Z, NumBitsZ>::loadSOA(pSrc);
            return;
        case 3:
            dst = TypeTraits<W, NumBitsW>::loadSOA(pSrc);
            return;
        }
        SWR_INVALID("Invalid component: %d", comp);
        dst = TypeTraits<X, NumBitsX>::loadSOA(pSrc);
    }

    INLINE static void storeSOA(uint32_t comp, uint8_t* pDst, simdscalar const& src)
    {
        switch (comp)
        {
        case 0:
            TypeTraits<X, NumBitsX>::storeSOA(pDst, src);
            return;
        case 1:
            TypeTraits<Y, NumBitsY>::storeSOA(pDst, src);
            return;
        case 2:
            TypeTraits<Z, NumBitsZ>::storeSOA(pDst, src);
            return;
        case 3:
            TypeTraits<W, NumBitsW>::storeSOA(pDst, src);
            return;
        }
        SWR_INVALID("Invalid component: %d", comp);
    }

    INLINE static simdscalar unpack(uint32_t comp, simdscalar& in)
    {
        simdscalar out;
        switch (comp)
        {
        case 0:
            out = TypeTraits<X, NumBitsX>::unpack(in);
            break;
        case 1:
            out = TypeTraits<Y, NumBitsY>::unpack(in);
            break;
        case 2:
            out = TypeTraits<Z, NumBitsZ>::unpack(in);
            break;
        case 3:
            out = TypeTraits<W, NumBitsW>::unpack(in);
            break;
        default:
            SWR_INVALID("Invalid component: %d", comp);
            out = in;
            break;
        }
        return out;
    }

    INLINE static simdscalar pack(uint32_t comp, simdscalar& in)
    {
        simdscalar out;
        switch (comp)
        {
        case 0:
            out = TypeTraits<X, NumBitsX>::pack(in);
            break;
        case 1:
            out = TypeTraits<Y, NumBitsY>::pack(in);
            break;
        case 2:
            out = TypeTraits<Z, NumBitsZ>::pack(in);
            break;
        case 3:
            out = TypeTraits<W, NumBitsW>::pack(in);
            break;
        default:
            SWR_INVALID("Invalid component: %d", comp);
            out = in;
            break;
        }
        return out;
    }

    INLINE static simdscalar convertSrgb(uint32_t comp, simdscalar& in)
    {
        switch (comp)
        {
        case 0:
            return TypeTraits<X, NumBitsX>::convertSrgb(in);
        case 1:
            return TypeTraits<Y, NumBitsY>::convertSrgb(in);
        case 2:
            return TypeTraits<Z, NumBitsZ>::convertSrgb(in);
        case 3:
            return TypeTraits<W, NumBitsW>::convertSrgb(in);
        }
        SWR_INVALID("Invalid component: %d", comp);
        return TypeTraits<X, NumBitsX>::convertSrgb(in);
    }

    INLINE static void SIMDCALL loadSOA(uint32_t comp, const uint8_t* pSrc, simd16scalar& dst)
    {
        switch (comp)
        {
        case 0:
            dst = TypeTraits<X, NumBitsX>::loadSOA_16(pSrc);
            return;
        case 1:
            dst = TypeTraits<Y, NumBitsY>::loadSOA_16(pSrc);
            return;
        case 2:
            dst = TypeTraits<Z, NumBitsZ>::loadSOA_16(pSrc);
            return;
        case 3:
            dst = TypeTraits<W, NumBitsW>::loadSOA_16(pSrc);
            return;
        }
        SWR_INVALID("Invalid component: %d", comp);
        dst = TypeTraits<X, NumBitsX>::loadSOA_16(pSrc);
    }

    INLINE static void SIMDCALL storeSOA(uint32_t comp, uint8_t* pDst, simd16scalar const& src)
    {
        switch (comp)
        {
        case 0:
            TypeTraits<X, NumBitsX>::storeSOA(pDst, src);
            return;
        case 1:
            TypeTraits<Y, NumBitsY>::storeSOA(pDst, src);
            return;
        case 2:
            TypeTraits<Z, NumBitsZ>::storeSOA(pDst, src);
            return;
        case 3:
            TypeTraits<W, NumBitsW>::storeSOA(pDst, src);
            return;
        }
        SWR_INVALID("Invalid component: %d", comp);
        TypeTraits<X, NumBitsX>::storeSOA(pDst, src);
    }

    INLINE static simd16scalar unpack(uint32_t comp, simd16scalar& in)
    {
        switch (comp)
        {
        case 0:
            return TypeTraits<X, NumBitsX>::unpack(in);
        case 1:
            return TypeTraits<Y, NumBitsY>::unpack(in);
        case 2:
            return TypeTraits<Z, NumBitsZ>::unpack(in);
        case 3:
            return TypeTraits<W, NumBitsW>::unpack(in);
        }
        SWR_INVALID("Invalid component: %d", comp);
        return TypeTraits<X, NumBitsX>::unpack(in);
    }

    INLINE static simd16scalar pack(uint32_t comp, simd16scalar& in)
    {
        switch (comp)
        {
        case 0:
            return TypeTraits<X, NumBitsX>::pack(in);
        case 1:
            return TypeTraits<Y, NumBitsY>::pack(in);
        case 2:
            return TypeTraits<Z, NumBitsZ>::pack(in);
        case 3:
            return TypeTraits<W, NumBitsW>::pack(in);
        }
        SWR_INVALID("Invalid component: %d", comp);
        return TypeTraits<X, NumBitsX>::pack(in);
    }

    INLINE static simd16scalar convertSrgb(uint32_t comp, simd16scalar& in)
    {
        switch (comp)
        {
        case 0:
            return TypeTraits<X, NumBitsX>::convertSrgb(in);
        case 1:
            return TypeTraits<Y, NumBitsY>::convertSrgb(in);
        case 2:
            return TypeTraits<Z, NumBitsZ>::convertSrgb(in);
        case 3:
            return TypeTraits<W, NumBitsW>::convertSrgb(in);
        }
        SWR_INVALID("Invalid component: %d", comp);
        return TypeTraits<X, NumBitsX>::convertSrgb(in);
    }
};
