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
 * @file utils.h
 *
 * @brief Utilities used by SWR core related to pixel formats.
 *
 ******************************************************************************/
#pragma once

#include "core/utils.h"
#include "common/simdintrin.h"

INLINE
void vTranspose(simd4scalar& row0, simd4scalar& row1, simd4scalar& row2, simd4scalar& row3)
{
    simd4scalari row0i = SIMD128::castps_si(row0);
    simd4scalari row1i = SIMD128::castps_si(row1);
    simd4scalari row2i = SIMD128::castps_si(row2);
    simd4scalari row3i = SIMD128::castps_si(row3);

    simd4scalari vTemp = row2i;
    row2i              = SIMD128::unpacklo_epi32(row2i, row3i);
    vTemp              = SIMD128::unpackhi_epi32(vTemp, row3i);

    row3i = row0i;
    row0i = SIMD128::unpacklo_epi32(row0i, row1i);
    row3i = SIMD128::unpackhi_epi32(row3i, row1i);

    row1i = row0i;
    row0i = SIMD128::unpacklo_epi64(row0i, row2i);
    row1i = SIMD128::unpackhi_epi64(row1i, row2i);

    row2i = row3i;
    row2i = SIMD128::unpacklo_epi64(row2i, vTemp);
    row3i = SIMD128::unpackhi_epi64(row3i, vTemp);

    row0 = SIMD128::castsi_ps(row0i);
    row1 = SIMD128::castsi_ps(row1i);
    row2 = SIMD128::castsi_ps(row2i);
    row3 = SIMD128::castsi_ps(row3i);
}

INLINE
void vTranspose(simd4scalari& row0, simd4scalari& row1, simd4scalari& row2, simd4scalari& row3)
{
    simd4scalari vTemp = row2;
    row2               = SIMD128::unpacklo_epi32(row2, row3);
    vTemp              = SIMD128::unpackhi_epi32(vTemp, row3);

    row3 = row0;
    row0 = SIMD128::unpacklo_epi32(row0, row1);
    row3 = SIMD128::unpackhi_epi32(row3, row1);

    row1 = row0;
    row0 = SIMD128::unpacklo_epi64(row0, row2);
    row1 = SIMD128::unpackhi_epi64(row1, row2);

    row2 = row3;
    row2 = SIMD128::unpacklo_epi64(row2, vTemp);
    row3 = SIMD128::unpackhi_epi64(row3, vTemp);
}

#if KNOB_SIMD_WIDTH == 8
INLINE
void vTranspose3x8(simd4scalar (&vDst)[8],
                   const simdscalar& vSrc0,
                   const simdscalar& vSrc1,
                   const simdscalar& vSrc2)
{
    simdscalar r0r2       = _simd_unpacklo_ps(vSrc0, vSrc2);              // x0z0x1z1 x4z4x5z5
    simdscalar r1rx       = _simd_unpacklo_ps(vSrc1, _simd_setzero_ps()); // y0w0y1w1 y4w4y5w5
    simdscalar r02r1xlolo = _simd_unpacklo_ps(r0r2, r1rx);                // x0y0z0w0 x4y4z4w4
    simdscalar r02r1xlohi = _simd_unpackhi_ps(r0r2, r1rx);                // x1y1z1w1 x5y5z5w5

    r0r2                  = _simd_unpackhi_ps(vSrc0, vSrc2);              // x2z2x3z3 x6z6x7z7
    r1rx                  = _simd_unpackhi_ps(vSrc1, _simd_setzero_ps()); // y2w2y3w3 y6w6yw77
    simdscalar r02r1xhilo = _simd_unpacklo_ps(r0r2, r1rx);                // x2y2z2w2 x6y6z6w6
    simdscalar r02r1xhihi = _simd_unpackhi_ps(r0r2, r1rx);                // x3y3z3w3 x7y7z7w7

    vDst[0] = _simd_extractf128_ps(r02r1xlolo, 0);
    vDst[1] = _simd_extractf128_ps(r02r1xlohi, 0);
    vDst[2] = _simd_extractf128_ps(r02r1xhilo, 0);
    vDst[3] = _simd_extractf128_ps(r02r1xhihi, 0);

    vDst[4] = _simd_extractf128_ps(r02r1xlolo, 1);
    vDst[5] = _simd_extractf128_ps(r02r1xlohi, 1);
    vDst[6] = _simd_extractf128_ps(r02r1xhilo, 1);
    vDst[7] = _simd_extractf128_ps(r02r1xhihi, 1);
}

INLINE
void vTranspose4x8(simd4scalar (&vDst)[8],
                   const simdscalar& vSrc0,
                   const simdscalar& vSrc1,
                   const simdscalar& vSrc2,
                   const simdscalar& vSrc3)
{
    simdscalar r0r2       = _simd_unpacklo_ps(vSrc0, vSrc2); // x0z0x1z1 x4z4x5z5
    simdscalar r1rx       = _simd_unpacklo_ps(vSrc1, vSrc3); // y0w0y1w1 y4w4y5w5
    simdscalar r02r1xlolo = _simd_unpacklo_ps(r0r2, r1rx);   // x0y0z0w0 x4y4z4w4
    simdscalar r02r1xlohi = _simd_unpackhi_ps(r0r2, r1rx);   // x1y1z1w1 x5y5z5w5

    r0r2                  = _simd_unpackhi_ps(vSrc0, vSrc2); // x2z2x3z3 x6z6x7z7
    r1rx                  = _simd_unpackhi_ps(vSrc1, vSrc3); // y2w2y3w3 y6w6yw77
    simdscalar r02r1xhilo = _simd_unpacklo_ps(r0r2, r1rx);   // x2y2z2w2 x6y6z6w6
    simdscalar r02r1xhihi = _simd_unpackhi_ps(r0r2, r1rx);   // x3y3z3w3 x7y7z7w7

    vDst[0] = _simd_extractf128_ps(r02r1xlolo, 0);
    vDst[1] = _simd_extractf128_ps(r02r1xlohi, 0);
    vDst[2] = _simd_extractf128_ps(r02r1xhilo, 0);
    vDst[3] = _simd_extractf128_ps(r02r1xhihi, 0);

    vDst[4] = _simd_extractf128_ps(r02r1xlolo, 1);
    vDst[5] = _simd_extractf128_ps(r02r1xlohi, 1);
    vDst[6] = _simd_extractf128_ps(r02r1xhilo, 1);
    vDst[7] = _simd_extractf128_ps(r02r1xhihi, 1);
}

INLINE
void vTranspose4x16(simd16scalar (&dst)[4],
                    const simd16scalar& src0,
                    const simd16scalar& src1,
                    const simd16scalar& src2,
                    const simd16scalar& src3)
{
    const simd16scalari perm =
        _simd16_set_epi32(15, 11, 7, 3, 14, 10, 6, 2, 13, 9, 5, 1, 12, 8, 4, 0);

    // pre-permute input to setup the right order after all the unpacking

    simd16scalar pre0 = _simd16_permute_ps(src0, perm); // r
    simd16scalar pre1 = _simd16_permute_ps(src1, perm); // g
    simd16scalar pre2 = _simd16_permute_ps(src2, perm); // b
    simd16scalar pre3 = _simd16_permute_ps(src3, perm); // a

    simd16scalar rblo = _simd16_unpacklo_ps(pre0, pre2);
    simd16scalar galo = _simd16_unpacklo_ps(pre1, pre3);
    simd16scalar rbhi = _simd16_unpackhi_ps(pre0, pre2);
    simd16scalar gahi = _simd16_unpackhi_ps(pre1, pre3);

    dst[0] = _simd16_unpacklo_ps(rblo, galo);
    dst[1] = _simd16_unpackhi_ps(rblo, galo);
    dst[2] = _simd16_unpacklo_ps(rbhi, gahi);
    dst[3] = _simd16_unpackhi_ps(rbhi, gahi);
}

INLINE
void vTranspose8x8(simdscalar (&vDst)[8],
                   const simdscalar& vMask0,
                   const simdscalar& vMask1,
                   const simdscalar& vMask2,
                   const simdscalar& vMask3,
                   const simdscalar& vMask4,
                   const simdscalar& vMask5,
                   const simdscalar& vMask6,
                   const simdscalar& vMask7)
{
    simdscalar __t0  = _simd_unpacklo_ps(vMask0, vMask1);
    simdscalar __t1  = _simd_unpackhi_ps(vMask0, vMask1);
    simdscalar __t2  = _simd_unpacklo_ps(vMask2, vMask3);
    simdscalar __t3  = _simd_unpackhi_ps(vMask2, vMask3);
    simdscalar __t4  = _simd_unpacklo_ps(vMask4, vMask5);
    simdscalar __t5  = _simd_unpackhi_ps(vMask4, vMask5);
    simdscalar __t6  = _simd_unpacklo_ps(vMask6, vMask7);
    simdscalar __t7  = _simd_unpackhi_ps(vMask6, vMask7);
    simdscalar __tt0 = _simd_shuffle_ps(__t0, __t2, _MM_SHUFFLE(1, 0, 1, 0));
    simdscalar __tt1 = _simd_shuffle_ps(__t0, __t2, _MM_SHUFFLE(3, 2, 3, 2));
    simdscalar __tt2 = _simd_shuffle_ps(__t1, __t3, _MM_SHUFFLE(1, 0, 1, 0));
    simdscalar __tt3 = _simd_shuffle_ps(__t1, __t3, _MM_SHUFFLE(3, 2, 3, 2));
    simdscalar __tt4 = _simd_shuffle_ps(__t4, __t6, _MM_SHUFFLE(1, 0, 1, 0));
    simdscalar __tt5 = _simd_shuffle_ps(__t4, __t6, _MM_SHUFFLE(3, 2, 3, 2));
    simdscalar __tt6 = _simd_shuffle_ps(__t5, __t7, _MM_SHUFFLE(1, 0, 1, 0));
    simdscalar __tt7 = _simd_shuffle_ps(__t5, __t7, _MM_SHUFFLE(3, 2, 3, 2));
    vDst[0]          = _simd_permute2f128_ps(__tt0, __tt4, 0x20);
    vDst[1]          = _simd_permute2f128_ps(__tt1, __tt5, 0x20);
    vDst[2]          = _simd_permute2f128_ps(__tt2, __tt6, 0x20);
    vDst[3]          = _simd_permute2f128_ps(__tt3, __tt7, 0x20);
    vDst[4]          = _simd_permute2f128_ps(__tt0, __tt4, 0x31);
    vDst[5]          = _simd_permute2f128_ps(__tt1, __tt5, 0x31);
    vDst[6]          = _simd_permute2f128_ps(__tt2, __tt6, 0x31);
    vDst[7]          = _simd_permute2f128_ps(__tt3, __tt7, 0x31);
}

INLINE
void vTranspose8x8(simdscalar (&vDst)[8],
                   const simdscalari& vMask0,
                   const simdscalari& vMask1,
                   const simdscalari& vMask2,
                   const simdscalari& vMask3,
                   const simdscalari& vMask4,
                   const simdscalari& vMask5,
                   const simdscalari& vMask6,
                   const simdscalari& vMask7)
{
    vTranspose8x8(vDst,
                  _simd_castsi_ps(vMask0),
                  _simd_castsi_ps(vMask1),
                  _simd_castsi_ps(vMask2),
                  _simd_castsi_ps(vMask3),
                  _simd_castsi_ps(vMask4),
                  _simd_castsi_ps(vMask5),
                  _simd_castsi_ps(vMask6),
                  _simd_castsi_ps(vMask7));
}
#endif

//////////////////////////////////////////////////////////////////////////
/// TranposeSingleComponent
//////////////////////////////////////////////////////////////////////////
template <uint32_t bpp>
struct TransposeSingleComponent
{
    //////////////////////////////////////////////////////////////////////////
    /// @brief Pass-thru for single component.
    /// @param pSrc - source data in SOA form
    /// @param pDst - output data in AOS form
    INLINE static void Transpose(const uint8_t* pSrc, uint8_t* pDst)
    {
        memcpy(pDst, pSrc, (bpp * KNOB_SIMD_WIDTH) / 8);
    }

    INLINE static void Transpose_simd16(const uint8_t* pSrc, uint8_t* pDst)
    {
        memcpy(pDst, pSrc, (bpp * KNOB_SIMD16_WIDTH) / 8);
    }
};

//////////////////////////////////////////////////////////////////////////
/// Transpose8_8_8_8
//////////////////////////////////////////////////////////////////////////
struct Transpose8_8_8_8
{
    //////////////////////////////////////////////////////////////////////////
    /// @brief Performs an SOA to AOS conversion for packed 8_8_8_8 data.
    /// @param pSrc - source data in SOA form
    /// @param pDst - output data in AOS form
    INLINE static void Transpose(const uint8_t* pSrc, uint8_t* pDst)
    {
        simdscalari src = _simd_load_si((const simdscalari*)pSrc);

#if KNOB_SIMD_WIDTH == 8
#if KNOB_ARCH <= KNOB_ARCH_AVX
        simd4scalari c0c1 = src.v4[0]; // rrrrrrrrgggggggg
        simd4scalari c2c3 =
            SIMD128::castps_si(_simd_extractf128_ps(_simd_castsi_ps(src), 1)); // bbbbbbbbaaaaaaaa
        simd4scalari c0c2    = SIMD128::unpacklo_epi64(c0c1, c2c3);            // rrrrrrrrbbbbbbbb
        simd4scalari c1c3    = SIMD128::unpackhi_epi64(c0c1, c2c3);            // ggggggggaaaaaaaa
        simd4scalari c01     = SIMD128::unpacklo_epi8(c0c2, c1c3);             // rgrgrgrgrgrgrgrg
        simd4scalari c23     = SIMD128::unpackhi_epi8(c0c2, c1c3);             // babababababababa
        simd4scalari c0123lo = SIMD128::unpacklo_epi16(c01, c23);              // rgbargbargbargba
        simd4scalari c0123hi = SIMD128::unpackhi_epi16(c01, c23);              // rgbargbargbargba
        SIMD128::store_si((simd4scalari*)pDst, c0123lo);
        SIMD128::store_si((simd4scalari*)(pDst + 16), c0123hi);
#else
        simdscalari dst01 = _simd_shuffle_epi8(src,
                                               _simd_set_epi32(0x0f078080,
                                                               0x0e068080,
                                                               0x0d058080,
                                                               0x0c048080,
                                                               0x80800b03,
                                                               0x80800a02,
                                                               0x80800901,
                                                               0x80800800));
        simdscalari dst23 = _mm256_permute2x128_si256(src, src, 0x01);
        dst23             = _simd_shuffle_epi8(dst23,
                                   _simd_set_epi32(0x80800f07,
                                                   0x80800e06,
                                                   0x80800d05,
                                                   0x80800c04,
                                                   0x0b038080,
                                                   0x0a028080,
                                                   0x09018080,
                                                   0x08008080));
        simdscalari dst   = _simd_or_si(dst01, dst23);
        _simd_store_si((simdscalari*)pDst, dst);
#endif
#else
#error Unsupported vector width
#endif
    }

    INLINE static void Transpose_simd16(const uint8_t* pSrc, uint8_t* pDst)
    {
#if KNOB_SIMD16_WIDTH == 16
        // clang-format off

        simd4scalari src0 = SIMD128::load_si(reinterpret_cast<const simd4scalari*>(pSrc));      // rrrrrrrrrrrrrrrr
        simd4scalari src1 = SIMD128::load_si(reinterpret_cast<const simd4scalari*>(pSrc) + 1);  // gggggggggggggggg
        simd4scalari src2 = SIMD128::load_si(reinterpret_cast<const simd4scalari*>(pSrc) + 2);  // bbbbbbbbbbbbbbbb
        simd4scalari src3 = SIMD128::load_si(reinterpret_cast<const simd4scalari*>(pSrc) + 3);  // aaaaaaaaaaaaaaaa

        simd16scalari cvt0 = _simd16_cvtepu8_epi32(src0);
        simd16scalari cvt1 = _simd16_cvtepu8_epi32(src1);
        simd16scalari cvt2 = _simd16_cvtepu8_epi32(src2);
        simd16scalari cvt3 = _simd16_cvtepu8_epi32(src3);

        simd16scalari shl1 = _simd16_slli_epi32(cvt1,  8);
        simd16scalari shl2 = _simd16_slli_epi32(cvt2, 16);
        simd16scalari shl3 = _simd16_slli_epi32(cvt3, 24);

        simd16scalari dst = _simd16_or_si(_simd16_or_si(cvt0, shl1), _simd16_or_si(shl2, shl3));

        _simd16_store_si(reinterpret_cast<simd16scalari*>(pDst), dst);  // rgbargbargbargbargbargbargbargbargbargbargbargbargbargbargbargba

        // clang-format on
#else
#error Unsupported vector width
#endif
    }
};

//////////////////////////////////////////////////////////////////////////
/// Transpose8_8_8
//////////////////////////////////////////////////////////////////////////
struct Transpose8_8_8
{
    //////////////////////////////////////////////////////////////////////////
    /// @brief Performs an SOA to AOS conversion for packed 8_8_8 data.
    /// @param pSrc - source data in SOA form
    /// @param pDst - output data in AOS form
    INLINE static void Transpose(const uint8_t* pSrc, uint8_t* pDst) = delete;
    INLINE static void Transpose_simd16(const uint8_t* pSrc, uint8_t* pDst) = delete;
};

//////////////////////////////////////////////////////////////////////////
/// Transpose8_8
//////////////////////////////////////////////////////////////////////////
struct Transpose8_8
{
    //////////////////////////////////////////////////////////////////////////
    /// @brief Performs an SOA to AOS conversion for packed 8_8 data.
    /// @param pSrc - source data in SOA form
    /// @param pDst - output data in AOS form
    INLINE static void Transpose(const uint8_t* pSrc, uint8_t* pDst)
    {
#if KNOB_SIMD_WIDTH == 8
        simdscalari src = _simd_load_si((const simdscalari*)pSrc);

        simd4scalari rg = src.v4[0];                       // rrrrrrrr gggggggg
        simd4scalari g  = SIMD128::unpackhi_epi64(rg, rg); // gggggggg gggggggg
        rg              = SIMD128::unpacklo_epi8(rg, g);
        SIMD128::store_si((simd4scalari*)pDst, rg);
#else
#error Unsupported vector width
#endif
    }

    INLINE static void Transpose_simd16(const uint8_t* pSrc, uint8_t* pDst)
    {
#if KNOB_SIMD16_WIDTH == 16
        // clang-format off

        simd4scalari src0 = SIMD128::load_si(reinterpret_cast<const simd4scalari*>(pSrc));      // rrrrrrrrrrrrrrrr
        simd4scalari src1 = SIMD128::load_si(reinterpret_cast<const simd4scalari*>(pSrc) + 1);  // gggggggggggggggg

        simdscalari cvt0 = _simd_cvtepu8_epi16(src0);
        simdscalari cvt1 = _simd_cvtepu8_epi16(src1);

        simdscalari shl1 = _simd_slli_epi32(cvt1, 8);

        simdscalari dst = _simd_or_si(cvt0, shl1);

        _simd_store_si(reinterpret_cast<simdscalari*>(pDst), dst);  // rgrgrgrgrgrgrgrgrgrgrgrgrgrgrgrg

        // clang-format on
#else
#error Unsupported vector width
#endif
    }
};

//////////////////////////////////////////////////////////////////////////
/// Transpose32_32_32_32
//////////////////////////////////////////////////////////////////////////
struct Transpose32_32_32_32
{
    //////////////////////////////////////////////////////////////////////////
    /// @brief Performs an SOA to AOS conversion for packed 32_32_32_32 data.
    /// @param pSrc - source data in SOA form
    /// @param pDst - output data in AOS form
    INLINE static void Transpose(const uint8_t* pSrc, uint8_t* pDst)
    {
#if KNOB_SIMD_WIDTH == 8
        simdscalar src0 = _simd_load_ps((const float*)pSrc);
        simdscalar src1 = _simd_load_ps((const float*)pSrc + 8);
        simdscalar src2 = _simd_load_ps((const float*)pSrc + 16);
        simdscalar src3 = _simd_load_ps((const float*)pSrc + 24);

        simd4scalar vDst[8];
        vTranspose4x8(vDst, src0, src1, src2, src3);
        SIMD128::store_ps((float*)pDst, vDst[0]);
        SIMD128::store_ps((float*)pDst + 4, vDst[1]);
        SIMD128::store_ps((float*)pDst + 8, vDst[2]);
        SIMD128::store_ps((float*)pDst + 12, vDst[3]);
        SIMD128::store_ps((float*)pDst + 16, vDst[4]);
        SIMD128::store_ps((float*)pDst + 20, vDst[5]);
        SIMD128::store_ps((float*)pDst + 24, vDst[6]);
        SIMD128::store_ps((float*)pDst + 28, vDst[7]);
#else
#error Unsupported vector width
#endif
    }

    INLINE static void Transpose_simd16(const uint8_t* pSrc, uint8_t* pDst)
    {
#if KNOB_SIMD16_WIDTH == 16
        // clang-format off

        simd16scalar src0 = _simd16_load_ps(reinterpret_cast<const float*>(pSrc));
        simd16scalar src1 = _simd16_load_ps(reinterpret_cast<const float*>(pSrc) + 16);
        simd16scalar src2 = _simd16_load_ps(reinterpret_cast<const float*>(pSrc) + 32);
        simd16scalar src3 = _simd16_load_ps(reinterpret_cast<const float*>(pSrc) + 48);

        simd16scalar dst[4];

        vTranspose4x16(dst, src0, src1, src2, src3);

        _simd16_store_ps(reinterpret_cast<float*>(pDst) +  0, dst[0]);
        _simd16_store_ps(reinterpret_cast<float*>(pDst) + 16, dst[1]);
        _simd16_store_ps(reinterpret_cast<float*>(pDst) + 32, dst[2]);
        _simd16_store_ps(reinterpret_cast<float*>(pDst) + 48, dst[3]);

        // clang-format on
#else
#error Unsupported vector width
#endif
    }
};

//////////////////////////////////////////////////////////////////////////
/// Transpose32_32_32
//////////////////////////////////////////////////////////////////////////
struct Transpose32_32_32
{
    //////////////////////////////////////////////////////////////////////////
    /// @brief Performs an SOA to AOS conversion for packed 32_32_32 data.
    /// @param pSrc - source data in SOA form
    /// @param pDst - output data in AOS form
    INLINE static void Transpose(const uint8_t* pSrc, uint8_t* pDst)
    {
#if KNOB_SIMD_WIDTH == 8
        simdscalar src0 = _simd_load_ps((const float*)pSrc);
        simdscalar src1 = _simd_load_ps((const float*)pSrc + 8);
        simdscalar src2 = _simd_load_ps((const float*)pSrc + 16);

        simd4scalar vDst[8];
        vTranspose3x8(vDst, src0, src1, src2);
        SIMD128::store_ps((float*)pDst, vDst[0]);
        SIMD128::store_ps((float*)pDst + 4, vDst[1]);
        SIMD128::store_ps((float*)pDst + 8, vDst[2]);
        SIMD128::store_ps((float*)pDst + 12, vDst[3]);
        SIMD128::store_ps((float*)pDst + 16, vDst[4]);
        SIMD128::store_ps((float*)pDst + 20, vDst[5]);
        SIMD128::store_ps((float*)pDst + 24, vDst[6]);
        SIMD128::store_ps((float*)pDst + 28, vDst[7]);
#else
#error Unsupported vector width
#endif
    }

    INLINE static void Transpose_simd16(const uint8_t* pSrc, uint8_t* pDst)
    {
#if KNOB_SIMD16_WIDTH == 16
        // clang-format off

        simd16scalar src0 = _simd16_load_ps(reinterpret_cast<const float*>(pSrc));
        simd16scalar src1 = _simd16_load_ps(reinterpret_cast<const float*>(pSrc) + 16);
        simd16scalar src2 = _simd16_load_ps(reinterpret_cast<const float*>(pSrc) + 32);
        simd16scalar src3 = _simd16_setzero_ps();

        simd16scalar dst[4];

        vTranspose4x16(dst, src0, src1, src2, src3);

        _simd16_store_ps(reinterpret_cast<float*>(pDst) +  0, dst[0]);
        _simd16_store_ps(reinterpret_cast<float*>(pDst) + 16, dst[1]);
        _simd16_store_ps(reinterpret_cast<float*>(pDst) + 32, dst[2]);
        _simd16_store_ps(reinterpret_cast<float*>(pDst) + 48, dst[3]);

        // clang-format on
#else
#error Unsupported vector width
#endif
    }
};

//////////////////////////////////////////////////////////////////////////
/// Transpose32_32
//////////////////////////////////////////////////////////////////////////
struct Transpose32_32
{
    //////////////////////////////////////////////////////////////////////////
    /// @brief Performs an SOA to AOS conversion for packed 32_32 data.
    /// @param pSrc - source data in SOA form
    /// @param pDst - output data in AOS form
    INLINE static void Transpose(const uint8_t* pSrc, uint8_t* pDst)
    {
#if KNOB_SIMD_WIDTH == 8
        const float* pfSrc  = (const float*)pSrc;
        simd4scalar  src_r0 = SIMD128::load_ps(pfSrc + 0);
        simd4scalar  src_r1 = SIMD128::load_ps(pfSrc + 4);
        simd4scalar  src_g0 = SIMD128::load_ps(pfSrc + 8);
        simd4scalar  src_g1 = SIMD128::load_ps(pfSrc + 12);

        simd4scalar dst0 = SIMD128::unpacklo_ps(src_r0, src_g0);
        simd4scalar dst1 = SIMD128::unpackhi_ps(src_r0, src_g0);
        simd4scalar dst2 = SIMD128::unpacklo_ps(src_r1, src_g1);
        simd4scalar dst3 = SIMD128::unpackhi_ps(src_r1, src_g1);

        float* pfDst = (float*)pDst;
        SIMD128::store_ps(pfDst + 0, dst0);
        SIMD128::store_ps(pfDst + 4, dst1);
        SIMD128::store_ps(pfDst + 8, dst2);
        SIMD128::store_ps(pfDst + 12, dst3);
#else
#error Unsupported vector width
#endif
    }

    INLINE static void Transpose_simd16(const uint8_t* pSrc, uint8_t* pDst)
    {
#if KNOB_SIMD16_WIDTH == 16
        // clang-format off

        simd16scalar src0 = _simd16_load_ps(reinterpret_cast<const float*>(pSrc));      // rrrrrrrrrrrrrrrr
        simd16scalar src1 = _simd16_load_ps(reinterpret_cast<const float*>(pSrc) + 16); // gggggggggggggggg

        simd16scalar tmp0 = _simd16_unpacklo_ps(src0, src1);                            // r0 g0 r1 g1 r4 g4 r5 g5 r8 g8 r9 g9 rC gC rD gD
        simd16scalar tmp1 = _simd16_unpackhi_ps(src0, src1);                            // r2 g2 r3 g3 r6 g6 r7 g7 rA gA rB gB rE gE rF gF

        simd16scalar per0 = _simd16_permute2f128_ps(tmp0, tmp1, 0x44); // (1, 0, 1, 0)  // r0 g0 r1 g1 r4 g4 r5 g5 r2 g2 r3 g3 r6 g6 r7 g7
        simd16scalar per1 = _simd16_permute2f128_ps(tmp0, tmp1, 0xEE); // (3, 2, 3, 2)  // r8 g8 r9 g9 rC gC rD gD rA gA rB gB rE gE rF gF

        simd16scalar dst0 = _simd16_permute2f128_ps(per0, per0, 0xD8); // (3, 1, 2, 0)  // r0 g0 r1 g1 r2 g2 r3 g3 r4 g4 r5 g5 r6 g6 r7 g7
        simd16scalar dst1 = _simd16_permute2f128_ps(per1, per1, 0xD8); // (3, 1, 2, 0)  // r8 g8 r9 g9 rA gA rB gB rC gC rD gD rE gE rF gF

        _simd16_store_ps(reinterpret_cast<float*>(pDst) +  0, dst0);                    // rgrgrgrgrgrgrgrg
        _simd16_store_ps(reinterpret_cast<float*>(pDst) + 16, dst1);                    // rgrgrgrgrgrgrgrg

        // clang-format on
#else
#error Unsupported vector width
#endif
    }
};

//////////////////////////////////////////////////////////////////////////
/// Transpose16_16_16_16
//////////////////////////////////////////////////////////////////////////
struct Transpose16_16_16_16
{
    //////////////////////////////////////////////////////////////////////////
    /// @brief Performs an SOA to AOS conversion for packed 16_16_16_16 data.
    /// @param pSrc - source data in SOA form
    /// @param pDst - output data in AOS form
    INLINE static void Transpose(const uint8_t* pSrc, uint8_t* pDst)
    {
#if KNOB_SIMD_WIDTH == 8
        simdscalari src_rg = _simd_load_si((const simdscalari*)pSrc);
        simdscalari src_ba = _simd_load_si((const simdscalari*)(pSrc + sizeof(simdscalari)));

        simd4scalari src_r = _simd_extractf128_si(src_rg, 0);
        simd4scalari src_g = _simd_extractf128_si(src_rg, 1);
        simd4scalari src_b = _simd_extractf128_si(src_ba, 0);
        simd4scalari src_a = _simd_extractf128_si(src_ba, 1);

        simd4scalari rg0 = SIMD128::unpacklo_epi16(src_r, src_g);
        simd4scalari rg1 = SIMD128::unpackhi_epi16(src_r, src_g);
        simd4scalari ba0 = SIMD128::unpacklo_epi16(src_b, src_a);
        simd4scalari ba1 = SIMD128::unpackhi_epi16(src_b, src_a);

        simd4scalari dst0 = SIMD128::unpacklo_epi32(rg0, ba0);
        simd4scalari dst1 = SIMD128::unpackhi_epi32(rg0, ba0);
        simd4scalari dst2 = SIMD128::unpacklo_epi32(rg1, ba1);
        simd4scalari dst3 = SIMD128::unpackhi_epi32(rg1, ba1);

        SIMD128::store_si(((simd4scalari*)pDst) + 0, dst0);
        SIMD128::store_si(((simd4scalari*)pDst) + 1, dst1);
        SIMD128::store_si(((simd4scalari*)pDst) + 2, dst2);
        SIMD128::store_si(((simd4scalari*)pDst) + 3, dst3);
#else
#error Unsupported vector width
#endif
    }

    INLINE static void Transpose_simd16(const uint8_t* pSrc, uint8_t* pDst)
    {
#if KNOB_SIMD16_WIDTH == 16
        // clang-format off

        simdscalari src0 = _simd_load_si(reinterpret_cast<const simdscalari*>(pSrc));       // rrrrrrrrrrrrrrrr
        simdscalari src1 = _simd_load_si(reinterpret_cast<const simdscalari*>(pSrc) + 1);   // gggggggggggggggg
        simdscalari src2 = _simd_load_si(reinterpret_cast<const simdscalari*>(pSrc) + 2);   // bbbbbbbbbbbbbbbb
        simdscalari src3 = _simd_load_si(reinterpret_cast<const simdscalari*>(pSrc) + 3);   // aaaaaaaaaaaaaaaa

        simdscalari pre0 = _simd_unpacklo_epi16(src0, src1);                    // rg0 rg1 rg2 rg3 rg8 rg9 rgA rgB
        simdscalari pre1 = _simd_unpackhi_epi16(src0, src1);                    // rg4 rg5 rg6 rg7 rgC rgD rgE rgF
        simdscalari pre2 = _simd_unpacklo_epi16(src2, src3);                    // ba0 ba1 ba3 ba3 ba8 ba9 baA baB
        simdscalari pre3 = _simd_unpackhi_epi16(src2, src3);                    // ba4 ba5 ba6 ba7 baC baD baE baF

        simdscalari tmp0 = _simd_unpacklo_epi32(pre0, pre2);                    // rbga0 rbga1 rbga8 rbga9
        simdscalari tmp1 = _simd_unpackhi_epi32(pre0, pre2);                    // rbga2 rbga3 rbgaA rbgaB
        simdscalari tmp2 = _simd_unpacklo_epi32(pre1, pre3);                    // rbga4 rbga5 rgbaC rbgaD
        simdscalari tmp3 = _simd_unpackhi_epi32(pre1, pre3);                    // rbga6 rbga7 rbgaE rbgaF

        simdscalari dst0 = _simd_permute2f128_si(tmp0, tmp1, 0x20); // (2, 0)   // rbga0 rbga1 rbga2 rbga3
        simdscalari dst1 = _simd_permute2f128_si(tmp2, tmp3, 0x20); // (2, 0)   // rbga4 rbga5 rbga6 rbga7
        simdscalari dst2 = _simd_permute2f128_si(tmp0, tmp1, 0x31); // (3, 1)   // rbga8 rbga9 rbgaA rbgaB
        simdscalari dst3 = _simd_permute2f128_si(tmp2, tmp3, 0x31); // (3, 1)   // rbgaC rbgaD rbgaE rbgaF

        _simd_store_si(reinterpret_cast<simdscalari*>(pDst) + 0, dst0);         // rgbargbargbargba
        _simd_store_si(reinterpret_cast<simdscalari*>(pDst) + 1, dst1);         // rgbargbargbargba
        _simd_store_si(reinterpret_cast<simdscalari*>(pDst) + 2, dst2);         // rgbargbargbargba
        _simd_store_si(reinterpret_cast<simdscalari*>(pDst) + 3, dst3);         // rgbargbargbargba

        // clang-format on
#else
#error Unsupported vector width
#endif
    }
};

//////////////////////////////////////////////////////////////////////////
/// Transpose16_16_16
//////////////////////////////////////////////////////////////////////////
struct Transpose16_16_16
{
    //////////////////////////////////////////////////////////////////////////
    /// @brief Performs an SOA to AOS conversion for packed 16_16_16 data.
    /// @param pSrc - source data in SOA form
    /// @param pDst - output data in AOS form
    INLINE static void Transpose(const uint8_t* pSrc, uint8_t* pDst)
    {
#if KNOB_SIMD_WIDTH == 8
        simdscalari src_rg = _simd_load_si((const simdscalari*)pSrc);

        simd4scalari src_r = _simd_extractf128_si(src_rg, 0);
        simd4scalari src_g = _simd_extractf128_si(src_rg, 1);
        simd4scalari src_b = SIMD128::load_si((const simd4scalari*)(pSrc + sizeof(simdscalari)));
        simd4scalari src_a = SIMD128::setzero_si();

        simd4scalari rg0 = SIMD128::unpacklo_epi16(src_r, src_g);
        simd4scalari rg1 = SIMD128::unpackhi_epi16(src_r, src_g);
        simd4scalari ba0 = SIMD128::unpacklo_epi16(src_b, src_a);
        simd4scalari ba1 = SIMD128::unpackhi_epi16(src_b, src_a);

        simd4scalari dst0 = SIMD128::unpacklo_epi32(rg0, ba0);
        simd4scalari dst1 = SIMD128::unpackhi_epi32(rg0, ba0);
        simd4scalari dst2 = SIMD128::unpacklo_epi32(rg1, ba1);
        simd4scalari dst3 = SIMD128::unpackhi_epi32(rg1, ba1);

        SIMD128::store_si(((simd4scalari*)pDst) + 0, dst0);
        SIMD128::store_si(((simd4scalari*)pDst) + 1, dst1);
        SIMD128::store_si(((simd4scalari*)pDst) + 2, dst2);
        SIMD128::store_si(((simd4scalari*)pDst) + 3, dst3);
#else
#error Unsupported vector width
#endif
    }

    INLINE static void Transpose_simd16(const uint8_t* pSrc, uint8_t* pDst)
    {
#if KNOB_SIMD16_WIDTH == 16
        // clang-format off

        simdscalari src0 = _simd_load_si(reinterpret_cast<const simdscalari*>(pSrc));       // rrrrrrrrrrrrrrrr
        simdscalari src1 = _simd_load_si(reinterpret_cast<const simdscalari*>(pSrc) + 1);   // gggggggggggggggg
        simdscalari src2 = _simd_load_si(reinterpret_cast<const simdscalari*>(pSrc) + 2);   // bbbbbbbbbbbbbbbb
        simdscalari src3 = _simd_setzero_si();                                              // aaaaaaaaaaaaaaaa

        simdscalari pre0 = _simd_unpacklo_epi16(src0, src1);                    // rg0 rg1 rg2 rg3 rg8 rg9 rgA rgB
        simdscalari pre1 = _simd_unpackhi_epi16(src0, src1);                    // rg4 rg5 rg6 rg7 rgC rgD rgE rgF
        simdscalari pre2 = _simd_unpacklo_epi16(src2, src3);                    // ba0 ba1 ba3 ba3 ba8 ba9 baA baB
        simdscalari pre3 = _simd_unpackhi_epi16(src2, src3);                    // ba4 ba5 ba6 ba7 baC baD baE baF

        simdscalari tmp0 = _simd_unpacklo_epi32(pre0, pre2);                    // rbga0 rbga1 rbga8 rbga9
        simdscalari tmp1 = _simd_unpackhi_epi32(pre0, pre2);                    // rbga2 rbga3 rbgaA rbgaB
        simdscalari tmp2 = _simd_unpacklo_epi32(pre1, pre3);                    // rbga4 rbga5 rgbaC rbgaD
        simdscalari tmp3 = _simd_unpackhi_epi32(pre1, pre3);                    // rbga6 rbga7 rbgaE rbgaF

        simdscalari dst0 = _simd_permute2f128_si(tmp0, tmp1, 0x20); // (2, 0)  // rbga0 rbga1 rbga2 rbga3
        simdscalari dst1 = _simd_permute2f128_si(tmp2, tmp3, 0x20); // (2, 0)  // rbga4 rbga5 rbga6 rbga7
        simdscalari dst2 = _simd_permute2f128_si(tmp0, tmp1, 0x31); // (3, 1)  // rbga8 rbga9 rbgaA rbgaB
        simdscalari dst3 = _simd_permute2f128_si(tmp2, tmp3, 0x31); // (3, 1)  // rbgaC rbgaD rbgaE rbgaF

        _simd_store_si(reinterpret_cast<simdscalari*>(pDst) + 0, dst0);         // rgbargbargbargba
        _simd_store_si(reinterpret_cast<simdscalari*>(pDst) + 1, dst1);         // rgbargbargbargba
        _simd_store_si(reinterpret_cast<simdscalari*>(pDst) + 2, dst2);         // rgbargbargbargba
        _simd_store_si(reinterpret_cast<simdscalari*>(pDst) + 3, dst3);         // rgbargbargbargba

        // clang-format on
#else
#error Unsupported vector width
#endif
    }
};

//////////////////////////////////////////////////////////////////////////
/// Transpose16_16
//////////////////////////////////////////////////////////////////////////
struct Transpose16_16
{
    //////////////////////////////////////////////////////////////////////////
    /// @brief Performs an SOA to AOS conversion for packed 16_16 data.
    /// @param pSrc - source data in SOA form
    /// @param pDst - output data in AOS form
    INLINE static void Transpose(const uint8_t* pSrc, uint8_t* pDst)
    {
#if KNOB_SIMD_WIDTH == 8
        simdscalar src = _simd_load_ps((const float*)pSrc);

        simd4scalar comp0 = _simd_extractf128_ps(src, 0);
        simd4scalar comp1 = _simd_extractf128_ps(src, 1);

        simd4scalari comp0i = SIMD128::castps_si(comp0);
        simd4scalari comp1i = SIMD128::castps_si(comp1);

        simd4scalari resLo = SIMD128::unpacklo_epi16(comp0i, comp1i);
        simd4scalari resHi = SIMD128::unpackhi_epi16(comp0i, comp1i);

        SIMD128::store_si((simd4scalari*)pDst, resLo);
        SIMD128::store_si((simd4scalari*)pDst + 1, resHi);
#else
#error Unsupported vector width
#endif
    }

    INLINE static void Transpose_simd16(const uint8_t* pSrc, uint8_t* pDst)
    {
#if KNOB_SIMD16_WIDTH == 16
        // clang-format off

        simdscalari src0 = _simd_load_si(reinterpret_cast<const simdscalari*>(pSrc));       // rrrrrrrrrrrrrrrr
        simdscalari src1 = _simd_load_si(reinterpret_cast<const simdscalari*>(pSrc) + 1);   // gggggggggggggggg

        simdscalari tmp0 = _simd_unpacklo_epi16(src0, src1);                    // rg0 rg1 rg2 rg3 rg8 rg9 rgA rgB
        simdscalari tmp1 = _simd_unpackhi_epi16(src0, src1);                    // rg4 rg5 rg6 rg7 rgC rgD rgE rgF

        simdscalari dst0 = _simd_permute2f128_si(tmp0, tmp1, 0x20); // (2, 0)   // rg0 rg1 rg2 rg3 rg4 rg5 rg6 rg7
        simdscalari dst1 = _simd_permute2f128_si(tmp0, tmp1, 0x31); // (3, 1)   // rg8 rg9 rgA rgB rgC rgD rgE rgF

        _simd_store_si(reinterpret_cast<simdscalari*>(pDst) + 0, dst0);         // rgrgrgrgrgrgrgrg
        _simd_store_si(reinterpret_cast<simdscalari*>(pDst) + 1, dst1);         // rgrgrgrgrgrgrgrg

        // clang-format on
#else
#error Unsupported vector width
#endif
    }
};

//////////////////////////////////////////////////////////////////////////
/// Transpose24_8
//////////////////////////////////////////////////////////////////////////
struct Transpose24_8
{
    //////////////////////////////////////////////////////////////////////////
    /// @brief Performs an SOA to AOS conversion for packed 24_8 data.
    /// @param pSrc - source data in SOA form
    /// @param pDst - output data in AOS form
    static void Transpose(const uint8_t* pSrc, uint8_t* pDst) = delete;
    static void Transpose_simd16(const uint8_t* pSrc, uint8_t* pDst) = delete;
};

//////////////////////////////////////////////////////////////////////////
/// Transpose32_8_24
//////////////////////////////////////////////////////////////////////////
struct Transpose32_8_24
{
    //////////////////////////////////////////////////////////////////////////
    /// @brief Performs an SOA to AOS conversion for packed 32_8_24 data.
    /// @param pSrc - source data in SOA form
    /// @param pDst - output data in AOS form
    static void Transpose(const uint8_t* pSrc, uint8_t* pDst) = delete;
    static void Transpose_simd16(const uint8_t* pSrc, uint8_t* pDst) = delete;
};

//////////////////////////////////////////////////////////////////////////
/// Transpose4_4_4_4
//////////////////////////////////////////////////////////////////////////
struct Transpose4_4_4_4
{
    //////////////////////////////////////////////////////////////////////////
    /// @brief Performs an SOA to AOS conversion for packed 4_4_4_4 data.
    /// @param pSrc - source data in SOA form
    /// @param pDst - output data in AOS form
    static void Transpose(const uint8_t* pSrc, uint8_t* pDst) = delete;
    static void Transpose_simd16(const uint8_t* pSrc, uint8_t* pDst) = delete;
};

//////////////////////////////////////////////////////////////////////////
/// Transpose5_6_5
//////////////////////////////////////////////////////////////////////////
struct Transpose5_6_5
{
    //////////////////////////////////////////////////////////////////////////
    /// @brief Performs an SOA to AOS conversion for packed 5_6_5 data.
    /// @param pSrc - source data in SOA form
    /// @param pDst - output data in AOS form
    static void Transpose(const uint8_t* pSrc, uint8_t* pDst) = delete;
    static void Transpose_simd16(const uint8_t* pSrc, uint8_t* pDst) = delete;
};

//////////////////////////////////////////////////////////////////////////
/// Transpose9_9_9_5
//////////////////////////////////////////////////////////////////////////
struct Transpose9_9_9_5
{
    //////////////////////////////////////////////////////////////////////////
    /// @brief Performs an SOA to AOS conversion for packed 9_9_9_5 data.
    /// @param pSrc - source data in SOA form
    /// @param pDst - output data in AOS form
    static void Transpose(const uint8_t* pSrc, uint8_t* pDst) = delete;
    static void Transpose_simd16(const uint8_t* pSrc, uint8_t* pDst) = delete;
};

//////////////////////////////////////////////////////////////////////////
/// Transpose5_5_5_1
//////////////////////////////////////////////////////////////////////////
struct Transpose5_5_5_1
{
    //////////////////////////////////////////////////////////////////////////
    /// @brief Performs an SOA to AOS conversion for packed 5_5_5_1 data.
    /// @param pSrc - source data in SOA form
    /// @param pDst - output data in AOS form
    static void Transpose(const uint8_t* pSrc, uint8_t* pDst) = delete;
    static void Transpose_simd16(const uint8_t* pSrc, uint8_t* pDst) = delete;
};

//////////////////////////////////////////////////////////////////////////
/// Transpose1_5_5_5
//////////////////////////////////////////////////////////////////////////
struct Transpose1_5_5_5
{
    //////////////////////////////////////////////////////////////////////////
    /// @brief Performs an SOA to AOS conversion for packed 5_5_5_1 data.
    /// @param pSrc - source data in SOA form
    /// @param pDst - output data in AOS form
    static void Transpose(const uint8_t* pSrc, uint8_t* pDst) = delete;
    static void Transpose_simd16(const uint8_t* pSrc, uint8_t* pDst) = delete;
};

//////////////////////////////////////////////////////////////////////////
/// Transpose10_10_10_2
//////////////////////////////////////////////////////////////////////////
struct Transpose10_10_10_2
{
    //////////////////////////////////////////////////////////////////////////
    /// @brief Performs an SOA to AOS conversion for packed 10_10_10_2 data.
    /// @param pSrc - source data in SOA form
    /// @param pDst - output data in AOS form
    static void Transpose(const uint8_t* pSrc, uint8_t* pDst) = delete;
    static void Transpose_simd16(const uint8_t* pSrc, uint8_t* pDst) = delete;
};

//////////////////////////////////////////////////////////////////////////
/// Transpose11_11_10
//////////////////////////////////////////////////////////////////////////
struct Transpose11_11_10
{
    //////////////////////////////////////////////////////////////////////////
    /// @brief Performs an SOA to AOS conversion for packed 11_11_10 data.
    /// @param pSrc - source data in SOA form
    /// @param pDst - output data in AOS form
    static void Transpose(const uint8_t* pSrc, uint8_t* pDst) = delete;
    static void Transpose_simd16(const uint8_t* pSrc, uint8_t* pDst) = delete;
};

//////////////////////////////////////////////////////////////////////////
/// Transpose64
//////////////////////////////////////////////////////////////////////////
struct Transpose64
{
    //////////////////////////////////////////////////////////////////////////
    /// @brief Performs an SOA to AOS conversion
    /// @param pSrc - source data in SOA form
    /// @param pDst - output data in AOS form
    static void Transpose(const uint8_t* pSrc, uint8_t* pDst) = delete;
    static void Transpose_simd16(const uint8_t* pSrc, uint8_t* pDst) = delete;
};

//////////////////////////////////////////////////////////////////////////
/// Transpose64_64
//////////////////////////////////////////////////////////////////////////
struct Transpose64_64
{
    //////////////////////////////////////////////////////////////////////////
    /// @brief Performs an SOA to AOS conversion
    /// @param pSrc - source data in SOA form
    /// @param pDst - output data in AOS form
    static void Transpose(const uint8_t* pSrc, uint8_t* pDst) = delete;
    static void Transpose_simd16(const uint8_t* pSrc, uint8_t* pDst) = delete;
};

//////////////////////////////////////////////////////////////////////////
/// Transpose64_64_64
//////////////////////////////////////////////////////////////////////////
struct Transpose64_64_64
{
    //////////////////////////////////////////////////////////////////////////
    /// @brief Performs an SOA to AOS conversion
    /// @param pSrc - source data in SOA form
    /// @param pDst - output data in AOS form
    static void Transpose(const uint8_t* pSrc, uint8_t* pDst) = delete;
    static void Transpose_simd16(const uint8_t* pSrc, uint8_t* pDst) = delete;
};

//////////////////////////////////////////////////////////////////////////
/// Transpose64_64_64_64
//////////////////////////////////////////////////////////////////////////
struct Transpose64_64_64_64
{
    //////////////////////////////////////////////////////////////////////////
    /// @brief Performs an SOA to AOS conversion
    /// @param pSrc - source data in SOA form
    /// @param pDst - output data in AOS form
    static void Transpose(const uint8_t* pSrc, uint8_t* pDst) = delete;
    static void Transpose_simd16(const uint8_t* pSrc, uint8_t* pDst) = delete;
};
