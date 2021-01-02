/****************************************************************************
* Copyright (C) 2014-2016 Intel Corporation.   All Rights Reserved.
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
* @file StoreTile.h
*
* @brief Functionality for Store.
*
******************************************************************************/
#pragma once

#include "common/os.h"
#include "common/formats.h"
#include "core/context.h"
#include "core/rdtsc_core.h"
#include "core/format_conversion.h"

#include "memory/TilingFunctions.h"
#include "memory/Convert.h"
#include "memory/SurfaceState.h"
#include "core/multisample.h"

#include <array>
#include <sstream>

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

// Function pointer to different storing functions for color, depth, and stencil based on incoming formats.
typedef void(*PFN_STORE_TILES)(uint8_t*, SWR_SURFACE_STATE*, uint32_t, uint32_t, uint32_t);

//////////////////////////////////////////////////////////////////////////
/// Store Raster Tile Function Tables.
//////////////////////////////////////////////////////////////////////////
extern PFN_STORE_TILES sStoreTilesTableColor[SWR_TILE_MODE_COUNT][NUM_SWR_FORMATS];
extern PFN_STORE_TILES sStoreTilesTableDepth[SWR_TILE_MODE_COUNT][NUM_SWR_FORMATS];
extern PFN_STORE_TILES sStoreTilesTableStencil[SWR_TILE_MODE_COUNT][NUM_SWR_FORMATS];

void InitStoreTilesTable_Linear_1();
void InitStoreTilesTable_Linear_2();
void InitStoreTilesTable_TileX_1();
void InitStoreTilesTable_TileX_2();
void InitStoreTilesTable_TileY_1();
void InitStoreTilesTable_TileY_2();
void InitStoreTilesTable_TileW();
void InitStoreTilesTable();

//////////////////////////////////////////////////////////////////////////
/// StorePixels
/// @brief Stores a 4x2 (AVX) raster-tile to two rows.
/// @param pSrc     - Pointer to source raster tile in SWRZ pixel order
/// @param ppDsts   - Array of destination pointers.  Each pointer is
///                   to a single row of at most 16B.
/// @tparam NumDests - Number of destination pointers.  Each pair of
///                    pointers is for a 16-byte column of two rows.
//////////////////////////////////////////////////////////////////////////
template <size_t PixelSize, size_t NumDests>
struct StorePixels
{
    static void Store(const uint8_t* pSrc, uint8_t* (&ppDsts)[NumDests]) = delete;
};

//////////////////////////////////////////////////////////////////////////
/// StorePixels (32-bit pixel specialization)
/// @brief Stores a 4x2 (AVX) raster-tile to two rows.
/// @param pSrc     - Pointer to source raster tile in SWRZ pixel order
/// @param ppDsts   - Array of destination pointers.  Each pointer is
///                   to a single row of at most 16B.
/// @tparam NumDests - Number of destination pointers.  Each pair of
///                    pointers is for a 16-byte column of two rows.
//////////////////////////////////////////////////////////////////////////
template <>
struct StorePixels<8, 2>
{
    static void Store(const uint8_t* pSrc, uint8_t* (&ppDsts)[2])
    {
        // Each 4-pixel row is 4 bytes.
        const uint16_t* pPixSrc = (const uint16_t*)pSrc;

        // Unswizzle from SWR-Z order
        uint16_t* pRow = (uint16_t*)ppDsts[0];
        pRow[0] = pPixSrc[0];
        pRow[1] = pPixSrc[2];

        pRow = (uint16_t*)ppDsts[1];
        pRow[0] = pPixSrc[1];
        pRow[1] = pPixSrc[3];
    }
};

template <>
struct StorePixels<8, 4>
{
    static void Store(const uint8_t* pSrc, uint8_t* (&ppDsts)[4])
    {
        // 8 x 2 bytes = 16 bytes, 16 pixels
        const uint16_t *pSrc16 = reinterpret_cast<const uint16_t *>(pSrc);

        uint16_t **ppDsts16 = reinterpret_cast<uint16_t **>(ppDsts);

        // Unswizzle from SWR-Z order
        ppDsts16[0][0] = pSrc16[0];     // 0 1
        ppDsts16[0][1] = pSrc16[2];     // 4 5

        ppDsts16[1][0] = pSrc16[1];     // 2 3
        ppDsts16[1][1] = pSrc16[3];     // 6 7

        ppDsts16[2][0] = pSrc16[4];     // 8 9
        ppDsts16[2][1] = pSrc16[6];     // C D

        ppDsts16[3][0] = pSrc16[5];     // A B
        ppDsts16[3][1] = pSrc16[7];     // E F
    }
};

//////////////////////////////////////////////////////////////////////////
/// StorePixels (32-bit pixel specialization)
/// @brief Stores a 4x2 (AVX) raster-tile to two rows.
/// @param pSrc     - Pointer to source raster tile in SWRZ pixel order
/// @param ppDsts   - Array of destination pointers.  Each pointer is
///                   to a single row of at most 16B.
/// @tparam NumDests - Number of destination pointers.  Each pair of
///                    pointers is for a 16-byte column of two rows.
//////////////////////////////////////////////////////////////////////////
template <>
struct StorePixels<16, 2>
{
    static void Store(const uint8_t* pSrc, uint8_t* (&ppDsts)[2])
    {
        // Each 4-pixel row is 8 bytes.
        const uint32_t* pPixSrc = (const uint32_t*)pSrc;

        // Unswizzle from SWR-Z order
        uint32_t* pRow = (uint32_t*)ppDsts[0];
        pRow[0] = pPixSrc[0];
        pRow[1] = pPixSrc[2];

        pRow = (uint32_t*)ppDsts[1];
        pRow[0] = pPixSrc[1];
        pRow[1] = pPixSrc[3];
    }
};

template <>
struct StorePixels<16, 4>
{
    static void Store(const uint8_t* pSrc, uint8_t* (&ppDsts)[4])
    {
        // 8 x 4 bytes = 32 bytes, 16 pixels
        const uint32_t *pSrc32 = reinterpret_cast<const uint32_t *>(pSrc);

        uint32_t **ppDsts32 = reinterpret_cast<uint32_t **>(ppDsts);

        // Unswizzle from SWR-Z order
        ppDsts32[0][0] = pSrc32[0];     // 0 1
        ppDsts32[0][1] = pSrc32[2];     // 4 5

        ppDsts32[1][0] = pSrc32[1];     // 2 3
        ppDsts32[1][1] = pSrc32[3];     // 6 7

        ppDsts32[2][0] = pSrc32[4];     // 8 9
        ppDsts32[2][1] = pSrc32[6];     // C D

        ppDsts32[3][0] = pSrc32[5];     // A B
        ppDsts32[3][1] = pSrc32[7];     // E F
    }
};

//////////////////////////////////////////////////////////////////////////
/// StorePixels (32-bit pixel specialization)
/// @brief Stores a 4x2 (AVX) raster-tile to two rows.
/// @param pSrc     - Pointer to source raster tile in SWRZ pixel order
/// @param ppDsts   - Array of destination pointers.  Each pointer is
///                   to a single row of at most 16B.
/// @tparam NumDests - Number of destination pointers.  Each pair of
///                    pointers is for a 16-byte column of two rows.
//////////////////////////////////////////////////////////////////////////
template <>
struct StorePixels<32, 2>
{
    static void Store(const uint8_t* pSrc, uint8_t* (&ppDsts)[2])
    {
        // Each 4-pixel row is 16-bytes
        simd4scalari *pZRow01 = (simd4scalari*)pSrc;
        simd4scalari vQuad00 = SIMD128::load_si(pZRow01);
        simd4scalari vQuad01 = SIMD128::load_si(pZRow01 + 1);

        simd4scalari vRow00 = SIMD128::unpacklo_epi64(vQuad00, vQuad01);
        simd4scalari vRow10 = SIMD128::unpackhi_epi64(vQuad00, vQuad01);

        SIMD128::storeu_si((simd4scalari*)ppDsts[0], vRow00);
        SIMD128::storeu_si((simd4scalari*)ppDsts[1], vRow10);
    }
};

template <>
struct StorePixels<32, 4>
{
    static void Store(const uint8_t* pSrc, uint8_t* (&ppDsts)[4])
    {
        // 4 x 16 bytes = 64 bytes, 16 pixels
        const simd4scalari *pSrc128 = reinterpret_cast<const simd4scalari *>(pSrc);

        simd4scalari **ppDsts128 = reinterpret_cast<simd4scalari **>(ppDsts);

        // Unswizzle from SWR-Z order
        simd4scalari quad0 = SIMD128::load_si(&pSrc128[0]);                        // 0 1 2 3
        simd4scalari quad1 = SIMD128::load_si(&pSrc128[1]);                        // 4 5 6 7
        simd4scalari quad2 = SIMD128::load_si(&pSrc128[2]);                        // 8 9 A B
        simd4scalari quad3 = SIMD128::load_si(&pSrc128[3]);                        // C D E F

        SIMD128::storeu_si(ppDsts128[0], SIMD128::unpacklo_epi64(quad0, quad1));   // 0 1 4 5
        SIMD128::storeu_si(ppDsts128[1], SIMD128::unpackhi_epi64(quad0, quad1));   // 2 3 6 7
        SIMD128::storeu_si(ppDsts128[2], SIMD128::unpacklo_epi64(quad2, quad3));   // 8 9 C D
        SIMD128::storeu_si(ppDsts128[3], SIMD128::unpackhi_epi64(quad2, quad3));   // A B E F
    }
};

//////////////////////////////////////////////////////////////////////////
/// StorePixels (32-bit pixel specialization)
/// @brief Stores a 4x2 (AVX) raster-tile to two rows.
/// @param pSrc     - Pointer to source raster tile in SWRZ pixel order
/// @param ppDsts   - Array of destination pointers.  Each pointer is
///                   to a single row of at most 16B.
/// @tparam NumDests - Number of destination pointers.  Each pair of
///                    pointers is for a 16-byte column of two rows.
//////////////////////////////////////////////////////////////////////////
template <>
struct StorePixels<64, 4>
{
    static void Store(const uint8_t* pSrc, uint8_t* (&ppDsts)[4])
    {
        // Each 4-pixel row is 32 bytes.
        const simd4scalari* pPixSrc = (const simd4scalari*)pSrc;

        // order of pointers match SWR-Z layout
        simd4scalari** pvDsts = (simd4scalari**)&ppDsts[0];
        *pvDsts[0] = pPixSrc[0];
        *pvDsts[1] = pPixSrc[1];
        *pvDsts[2] = pPixSrc[2];
        *pvDsts[3] = pPixSrc[3];
    }
};

template <>
struct StorePixels<64, 8>
{
    static void Store(const uint8_t* pSrc, uint8_t* (&ppDsts)[8])
    {
        // 8 x 16 bytes = 128 bytes, 16 pixels
        const simd4scalari *pSrc128 = reinterpret_cast<const simd4scalari *>(pSrc);

        simd4scalari **ppDsts128 = reinterpret_cast<simd4scalari **>(ppDsts);

        // order of pointers match SWR-Z layout
        *ppDsts128[0] = pSrc128[0];     // 0 1
        *ppDsts128[1] = pSrc128[1];     // 2 3
        *ppDsts128[2] = pSrc128[2];     // 4 5
        *ppDsts128[3] = pSrc128[3];     // 6 7
        *ppDsts128[4] = pSrc128[4];     // 8 9
        *ppDsts128[5] = pSrc128[5];     // A B
        *ppDsts128[6] = pSrc128[6];     // C D
        *ppDsts128[7] = pSrc128[7];     // E F
    }
};

//////////////////////////////////////////////////////////////////////////
/// StorePixels (32-bit pixel specialization)
/// @brief Stores a 4x2 (AVX) raster-tile to two rows.
/// @param pSrc     - Pointer to source raster tile in SWRZ pixel order
/// @param ppDsts   - Array of destination pointers.  Each pointer is
///                   to a single row of at most 16B.
/// @tparam NumDests - Number of destination pointers.  Each pair of
///                    pointers is for a 16-byte column of two rows.
//////////////////////////////////////////////////////////////////////////
template <>
struct StorePixels<128, 8>
{
    static void Store(const uint8_t* pSrc, uint8_t* (&ppDsts)[8])
    {
        // Each 4-pixel row is 64 bytes.
        const simd4scalari* pPixSrc = (const simd4scalari*)pSrc;

        // Unswizzle from SWR-Z order
        simd4scalari** pvDsts = (simd4scalari**)&ppDsts[0];
        *pvDsts[0] = pPixSrc[0];
        *pvDsts[1] = pPixSrc[2];
        *pvDsts[2] = pPixSrc[1];
        *pvDsts[3] = pPixSrc[3];
        *pvDsts[4] = pPixSrc[4];
        *pvDsts[5] = pPixSrc[6];
        *pvDsts[6] = pPixSrc[5];
        *pvDsts[7] = pPixSrc[7];
    }
};

template <>
struct StorePixels<128, 16>
{
    static void Store(const uint8_t* pSrc, uint8_t* (&ppDsts)[16])
    {
        // 16 x 16 bytes = 256 bytes, 16 pixels
        const simd4scalari *pSrc128 = reinterpret_cast<const simd4scalari *>(pSrc);

        simd4scalari **ppDsts128 = reinterpret_cast<simd4scalari **>(ppDsts);

        for (uint32_t i = 0; i < 16; i += 4)
        {
            *ppDsts128[i + 0] = pSrc128[i + 0];
            *ppDsts128[i + 1] = pSrc128[i + 2];
            *ppDsts128[i + 2] = pSrc128[i + 1];
            *ppDsts128[i + 3] = pSrc128[i + 3];
        }
    }
};

//////////////////////////////////////////////////////////////////////////
/// ConvertPixelsSOAtoAOS - Conversion for SIMD pixel (4x2 or 2x2)
//////////////////////////////////////////////////////////////////////////
template<SWR_FORMAT SrcFormat, SWR_FORMAT DstFormat>
struct ConvertPixelsSOAtoAOS
{
    //////////////////////////////////////////////////////////////////////////
    /// @brief Converts a SIMD from the Hot Tile to the destination format
    ///        and converts from SOA to AOS.
    /// @param pSrc - Pointer to raster tile.
    /// @param pDst - Pointer to destination surface or deswizzling buffer.
    template <size_t NumDests>
    INLINE static void Convert(const uint8_t* pSrc, uint8_t* (&ppDsts)[NumDests])
    {
        static const uint32_t MAX_RASTER_TILE_BYTES = 16 * 16; // 16 pixels * 16 bytes per pixel

        OSALIGNSIMD16(uint8_t) soaTile[MAX_RASTER_TILE_BYTES] = {0};
        OSALIGNSIMD16(uint8_t) aosTile[MAX_RASTER_TILE_BYTES] = {0};

        // Convert from SrcFormat --> DstFormat
        simd16vector src;
        LoadSOA<SrcFormat>(pSrc, src);
        StoreSOA<DstFormat>(src, soaTile);

        // Convert from SOA --> AOS
        FormatTraits<DstFormat>::TransposeT::Transpose_simd16(soaTile, aosTile);

        // Store data into destination
        StorePixels<FormatTraits<DstFormat>::bpp, NumDests>::Store(aosTile, ppDsts);
    }
};

//////////////////////////////////////////////////////////////////////////
/// ConvertPixelsSOAtoAOS - Conversion for SIMD pixel (4x2 or 2x2)
/// Specialization for no format conversion
//////////////////////////////////////////////////////////////////////////
template<SWR_FORMAT Format>
struct ConvertPixelsSOAtoAOS<Format, Format>
{
    //////////////////////////////////////////////////////////////////////////
    /// @brief Converts a SIMD from the Hot Tile to the destination format
    ///        and converts from SOA to AOS.
    /// @param pSrc - Pointer to raster tile.
    /// @param pDst - Pointer to destination surface or deswizzling buffer.
    template <size_t NumDests>
    INLINE static void Convert(const uint8_t* pSrc, uint8_t* (&ppDsts)[NumDests])
    {
        static const uint32_t MAX_RASTER_TILE_BYTES = 16 * 16; // 16 pixels * 16 bytes per pixel

        OSALIGNSIMD16(uint8_t) aosTile[MAX_RASTER_TILE_BYTES];

        // Convert from SOA --> AOS
        FormatTraits<Format>::TransposeT::Transpose_simd16(pSrc, aosTile);

        // Store data into destination
        StorePixels<FormatTraits<Format>::bpp, NumDests>::Store(aosTile, ppDsts);
    }
};

//////////////////////////////////////////////////////////////////////////
/// ConvertPixelsSOAtoAOS - Specialization conversion for B5G6R6_UNORM
//////////////////////////////////////////////////////////////////////////
template<>
struct ConvertPixelsSOAtoAOS < R32G32B32A32_FLOAT, B5G6R5_UNORM >
{
    //////////////////////////////////////////////////////////////////////////
    /// @brief Converts a SIMD from the Hot Tile to the destination format
    ///        and converts from SOA to AOS.
    /// @param pSrc - Pointer to raster tile.
    /// @param pDst - Pointer to destination surface or deswizzling buffer.
    template <size_t NumDests>
    INLINE static void Convert(const uint8_t* pSrc, uint8_t* (&ppDsts)[NumDests])
    {
        static const SWR_FORMAT SrcFormat = R32G32B32A32_FLOAT;
        static const SWR_FORMAT DstFormat = B5G6R5_UNORM;

        static const uint32_t MAX_RASTER_TILE_BYTES = 16 * 16; // 16 pixels * 16 bytes per pixel

        OSALIGNSIMD16(uint8_t) aosTile[MAX_RASTER_TILE_BYTES];

        // Load hot-tile
        simd16vector src, dst;
        LoadSOA<SrcFormat>(pSrc, src);

        // deswizzle
        dst.x = src[FormatTraits<DstFormat>::swizzle(0)];
        dst.y = src[FormatTraits<DstFormat>::swizzle(1)];
        dst.z = src[FormatTraits<DstFormat>::swizzle(2)];

        // clamp
        dst.x = Clamp<DstFormat>(dst.x, 0);
        dst.y = Clamp<DstFormat>(dst.y, 1);
        dst.z = Clamp<DstFormat>(dst.z, 2);

        // normalize
        dst.x = Normalize<DstFormat>(dst.x, 0);
        dst.y = Normalize<DstFormat>(dst.y, 1);
        dst.z = Normalize<DstFormat>(dst.z, 2);

        // pack
        simd16scalari packed = _simd16_castps_si(dst.x);

        SWR_ASSERT(FormatTraits<DstFormat>::GetBPC(0) == 5);
        SWR_ASSERT(FormatTraits<DstFormat>::GetBPC(1) == 6);

        packed = _simd16_or_si(packed, _simd16_slli_epi32(_simd16_castps_si(dst.y), 5));
        packed = _simd16_or_si(packed, _simd16_slli_epi32(_simd16_castps_si(dst.z), 5 + 6));

        // pack low 16 bits of each 32 bit lane to low 128 bits of dst
        uint32_t *pPacked = (uint32_t*)&packed;
        uint16_t *pAosTile = (uint16_t*)&aosTile[0];
        for (uint32_t t = 0; t < KNOB_SIMD16_WIDTH; ++t)
        {
            *pAosTile++ = *pPacked++;
        }

        // Store data into destination
        StorePixels<FormatTraits<DstFormat>::bpp, NumDests>::Store(aosTile, ppDsts);
    }
};

//////////////////////////////////////////////////////////////////////////
/// ConvertPixelsSOAtoAOS - Conversion for SIMD pixel (4x2 or 2x2)
//////////////////////////////////////////////////////////////////////////
template<>
struct ConvertPixelsSOAtoAOS<R32_FLOAT, R24_UNORM_X8_TYPELESS>
{
    static const SWR_FORMAT SrcFormat = R32_FLOAT;
    static const SWR_FORMAT DstFormat = R24_UNORM_X8_TYPELESS;

    //////////////////////////////////////////////////////////////////////////
    /// @brief Converts a SIMD from the Hot Tile to the destination format
    ///        and converts from SOA to AOS.
    /// @param pSrc - Pointer to raster tile.
    /// @param pDst - Pointer to destination surface or deswizzling buffer.
    template <size_t NumDests>
    INLINE static void Convert(const uint8_t* pSrc, uint8_t* (&ppDsts)[NumDests])
    {
        simd16scalar comp = _simd16_load_ps(reinterpret_cast<const float *>(pSrc));

        // clamp
        const simd16scalar zero = _simd16_setzero_ps();
        const simd16scalar ones = _simd16_set1_ps(1.0f);

        comp = _simd16_max_ps(comp, zero);
        comp = _simd16_min_ps(comp, ones);

        // normalize
        comp = _simd16_mul_ps(comp, _simd16_set1_ps(FormatTraits<DstFormat>::fromFloat(0)));

        simd16scalari temp = _simd16_cvtps_epi32(comp);

        // swizzle
        temp = _simd16_permute_epi32(temp, _simd16_set_epi32(15, 14, 11, 10, 13, 12, 9, 8, 7, 6, 3, 2, 5, 4, 1, 0));

        // merge/store data into destination but don't overwrite the X8 bits
        simdscalari destlo = _simd_loadu2_si(reinterpret_cast<simd4scalari *>(ppDsts[1]), reinterpret_cast<simd4scalari *>(ppDsts[0]));
        simdscalari desthi = _simd_loadu2_si(reinterpret_cast<simd4scalari *>(ppDsts[3]), reinterpret_cast<simd4scalari *>(ppDsts[2]));

        simd16scalari dest = _simd16_setzero_si();

        dest = _simd16_insert_si(dest, destlo, 0);
        dest = _simd16_insert_si(dest, desthi, 1);

        simd16scalari mask = _simd16_set1_epi32(0x00FFFFFF);

        dest = _simd16_or_si(_simd16_andnot_si(mask, dest), _simd16_and_si(mask, temp));

        _simd_storeu2_si(reinterpret_cast<simd4scalari *>(ppDsts[1]), reinterpret_cast<simd4scalari *>(ppDsts[0]), _simd16_extract_si(dest, 0));
        _simd_storeu2_si(reinterpret_cast<simd4scalari *>(ppDsts[3]), reinterpret_cast<simd4scalari *>(ppDsts[2]), _simd16_extract_si(dest, 1));
    }
};

template<SWR_FORMAT DstFormat>
INLINE static void FlatConvert(const uint8_t* pSrc, uint8_t* pDst0, uint8_t* pDst1, uint8_t* pDst2, uint8_t* pDst3)
{
    // swizzle rgba -> bgra while we load
    simd16scalar comp0 = _simd16_load_ps(reinterpret_cast<const float*>(pSrc + FormatTraits<DstFormat>::swizzle(0) * sizeof(simd16scalar))); // float32 rrrrrrrrrrrrrrrr
    simd16scalar comp1 = _simd16_load_ps(reinterpret_cast<const float*>(pSrc + FormatTraits<DstFormat>::swizzle(1) * sizeof(simd16scalar))); // float32 gggggggggggggggg
    simd16scalar comp2 = _simd16_load_ps(reinterpret_cast<const float*>(pSrc + FormatTraits<DstFormat>::swizzle(2) * sizeof(simd16scalar))); // float32 bbbbbbbbbbbbbbbb
    simd16scalar comp3 = _simd16_load_ps(reinterpret_cast<const float*>(pSrc + FormatTraits<DstFormat>::swizzle(3) * sizeof(simd16scalar))); // float32 aaaaaaaaaaaaaaaa

    // clamp
    const simd16scalar zero = _simd16_setzero_ps();
    const simd16scalar ones = _simd16_set1_ps(1.0f);

    comp0 = _simd16_max_ps(comp0, zero);
    comp0 = _simd16_min_ps(comp0, ones);

    comp1 = _simd16_max_ps(comp1, zero);
    comp1 = _simd16_min_ps(comp1, ones);

    comp2 = _simd16_max_ps(comp2, zero);
    comp2 = _simd16_min_ps(comp2, ones);

    comp3 = _simd16_max_ps(comp3, zero);
    comp3 = _simd16_min_ps(comp3, ones);

    // gamma-correct only rgb
    if (FormatTraits<DstFormat>::isSRGB)
    {
        comp0 = FormatTraits<R32G32B32A32_FLOAT>::convertSrgb(0, comp0);
        comp1 = FormatTraits<R32G32B32A32_FLOAT>::convertSrgb(1, comp1);
        comp2 = FormatTraits<R32G32B32A32_FLOAT>::convertSrgb(2, comp2);
    }

    // convert float components from 0.0f..1.0f to correct scale for 0..255 dest format
    comp0 = _simd16_mul_ps(comp0, _simd16_set1_ps(FormatTraits<DstFormat>::fromFloat(0)));
    comp1 = _simd16_mul_ps(comp1, _simd16_set1_ps(FormatTraits<DstFormat>::fromFloat(1)));
    comp2 = _simd16_mul_ps(comp2, _simd16_set1_ps(FormatTraits<DstFormat>::fromFloat(2)));
    comp3 = _simd16_mul_ps(comp3, _simd16_set1_ps(FormatTraits<DstFormat>::fromFloat(3)));

    // moving to 16 wide integer vector types
    simd16scalari src0 = _simd16_cvtps_epi32(comp0); // padded byte rrrrrrrrrrrrrrrr
    simd16scalari src1 = _simd16_cvtps_epi32(comp1); // padded byte gggggggggggggggg
    simd16scalari src2 = _simd16_cvtps_epi32(comp2); // padded byte bbbbbbbbbbbbbbbb
    simd16scalari src3 = _simd16_cvtps_epi32(comp3); // padded byte aaaaaaaaaaaaaaaa

    // SOA to AOS conversion
    src1 = _simd16_slli_epi32(src1,  8);
    src2 = _simd16_slli_epi32(src2, 16);
    src3 = _simd16_slli_epi32(src3, 24);

    simd16scalari final = _simd16_or_si(_simd16_or_si(src0, src1), _simd16_or_si(src2, src3));  // 0 1 2 3 4 5 6 7 8 9 A B C D E F

    // de-swizzle conversion
#if 1
    simd16scalari final0 = _simd16_permute2f128_si(final, final, 0xA0); // (2, 2, 0, 0)         // 0 1 2 3 0 1 2 3 8 9 A B 8 9 A B
    simd16scalari final1 = _simd16_permute2f128_si(final, final, 0xF5); // (3, 3, 1, 1)         // 4 5 6 7 4 5 6 7 C D E F C D E F

    final = _simd16_shuffle_epi64(final0, final1, 0xCC); // (1 1 0 0 1 1 0 0)                   // 0 1 4 5 2 3 6 7 8 9 C D A B E F

#else
    final = _simd16_permute_epi32(final, _simd16_set_epi32(15, 14, 11, 10, 13, 12, 9, 8, 7, 6, 3, 2, 5, 4, 1, 0));

#endif
    // store 8x2 memory order:
    //  row0: [ pDst0, pDst2 ] = { 0 1 4 5 }, { 8 9 C D }
    //  row1: [ pDst1, pDst3 ] = { 2 3 6 7 }, { A B E F }
    _simd_storeu2_si(reinterpret_cast<simd4scalari *>(pDst1), reinterpret_cast<simd4scalari *>(pDst0), _simd16_extract_si(final, 0));
    _simd_storeu2_si(reinterpret_cast<simd4scalari *>(pDst3), reinterpret_cast<simd4scalari *>(pDst2), _simd16_extract_si(final, 1));
}

template<SWR_FORMAT DstFormat>
INLINE static void FlatConvert(const uint8_t* pSrc, uint8_t* pDst, uint8_t* pDst1)
{
    static const uint32_t offset = sizeof(simdscalar);

    // swizzle rgba -> bgra while we load
    simdscalar vComp0 = _simd_load_ps((const float*)(pSrc + (FormatTraits<DstFormat>::swizzle(0))*offset)); // float32 rrrrrrrr
    simdscalar vComp1 = _simd_load_ps((const float*)(pSrc + (FormatTraits<DstFormat>::swizzle(1))*offset)); // float32 gggggggg
    simdscalar vComp2 = _simd_load_ps((const float*)(pSrc + (FormatTraits<DstFormat>::swizzle(2))*offset)); // float32 bbbbbbbb
    simdscalar vComp3 = _simd_load_ps((const float*)(pSrc + (FormatTraits<DstFormat>::swizzle(3))*offset)); // float32 aaaaaaaa

    // clamp
    vComp0 = _simd_max_ps(vComp0, _simd_setzero_ps());
    vComp0 = _simd_min_ps(vComp0, _simd_set1_ps(1.0f));

    vComp1 = _simd_max_ps(vComp1, _simd_setzero_ps());
    vComp1 = _simd_min_ps(vComp1, _simd_set1_ps(1.0f));

    vComp2 = _simd_max_ps(vComp2, _simd_setzero_ps());
    vComp2 = _simd_min_ps(vComp2, _simd_set1_ps(1.0f));

    vComp3 = _simd_max_ps(vComp3, _simd_setzero_ps());
    vComp3 = _simd_min_ps(vComp3, _simd_set1_ps(1.0f));

    if (FormatTraits<DstFormat>::isSRGB)
    {
        // Gamma-correct only rgb
        vComp0 = FormatTraits<R32G32B32A32_FLOAT>::convertSrgb(0, vComp0);
        vComp1 = FormatTraits<R32G32B32A32_FLOAT>::convertSrgb(1, vComp1);
        vComp2 = FormatTraits<R32G32B32A32_FLOAT>::convertSrgb(2, vComp2);
    }

    // convert float components from 0.0f .. 1.0f to correct scale for 0 .. 255 dest format
    vComp0 = _simd_mul_ps(vComp0, _simd_set1_ps(FormatTraits<DstFormat>::fromFloat(0)));
    vComp1 = _simd_mul_ps(vComp1, _simd_set1_ps(FormatTraits<DstFormat>::fromFloat(1)));
    vComp2 = _simd_mul_ps(vComp2, _simd_set1_ps(FormatTraits<DstFormat>::fromFloat(2)));
    vComp3 = _simd_mul_ps(vComp3, _simd_set1_ps(FormatTraits<DstFormat>::fromFloat(3)));

    // moving to 8 wide integer vector types
    simdscalari src0 = _simd_cvtps_epi32(vComp0); // padded byte rrrrrrrr
    simdscalari src1 = _simd_cvtps_epi32(vComp1); // padded byte gggggggg
    simdscalari src2 = _simd_cvtps_epi32(vComp2); // padded byte bbbbbbbb
    simdscalari src3 = _simd_cvtps_epi32(vComp3); // padded byte aaaaaaaa

#if KNOB_ARCH <= KNOB_ARCH_AVX

    // splitting into two sets of 4 wide integer vector types
    // because AVX doesn't have instructions to support this operation at 8 wide
    simd4scalari srcLo0 = _mm256_castsi256_si128(src0); // 000r000r000r000r
    simd4scalari srcLo1 = _mm256_castsi256_si128(src1); // 000g000g000g000g
    simd4scalari srcLo2 = _mm256_castsi256_si128(src2); // 000b000b000b000b
    simd4scalari srcLo3 = _mm256_castsi256_si128(src3); // 000a000a000a000a

    simd4scalari srcHi0 = _mm256_extractf128_si256(src0, 1); // 000r000r000r000r
    simd4scalari srcHi1 = _mm256_extractf128_si256(src1, 1); // 000g000g000g000g
    simd4scalari srcHi2 = _mm256_extractf128_si256(src2, 1); // 000b000b000b000b
    simd4scalari srcHi3 = _mm256_extractf128_si256(src3, 1); // 000a000a000a000a

    srcLo1 = _mm_slli_si128(srcLo1, 1); // 00g000g000g000g0
    srcHi1 = _mm_slli_si128(srcHi1, 1); // 00g000g000g000g0
    srcLo2 = _mm_slli_si128(srcLo2, 2); // 0b000b000b000b00
    srcHi2 = _mm_slli_si128(srcHi2, 2); // 0b000b000b000b00
    srcLo3 = _mm_slli_si128(srcLo3, 3); // a000a000a000a000
    srcHi3 = _mm_slli_si128(srcHi3, 3); // a000a000a000a000

    srcLo0 = SIMD128::or_si(srcLo0, srcLo1); // 00gr00gr00gr00gr
    srcLo2 = SIMD128::or_si(srcLo2, srcLo3); // ab00ab00ab00ab00

    srcHi0 = SIMD128::or_si(srcHi0, srcHi1); // 00gr00gr00gr00gr
    srcHi2 = SIMD128::or_si(srcHi2, srcHi3); // ab00ab00ab00ab00

    srcLo0 = SIMD128::or_si(srcLo0, srcLo2); // abgrabgrabgrabgr
    srcHi0 = SIMD128::or_si(srcHi0, srcHi2); // abgrabgrabgrabgr

    // unpack into rows that get the tiling order correct
    simd4scalari vRow00 = SIMD128::unpacklo_epi64(srcLo0, srcHi0);  // abgrabgrabgrabgrabgrabgrabgrabgr
    simd4scalari vRow10 = SIMD128::unpackhi_epi64(srcLo0, srcHi0);

    simdscalari final = _mm256_castsi128_si256(vRow00);
    final = _mm256_insertf128_si256(final, vRow10, 1);

#else

    // logic is as above, only wider
    src1 = _mm256_slli_si256(src1, 1);
    src2 = _mm256_slli_si256(src2, 2);
    src3 = _mm256_slli_si256(src3, 3);

    src0 = _mm256_or_si256(src0, src1);
    src2 = _mm256_or_si256(src2, src3);

    simdscalari final = _mm256_or_si256(src0, src2);

    // adjust the data to get the tiling order correct 0 1 2 3 -> 0 2 1 3
    final = _mm256_permute4x64_epi64(final, 0xD8);
#endif

    _simd_storeu2_si((simd4scalari*)pDst1, (simd4scalari*)pDst, final);
}

template<SWR_FORMAT DstFormat>
INLINE static void FlatConvertNoAlpha(const uint8_t* pSrc, uint8_t* pDst0, uint8_t* pDst1, uint8_t* pDst2, uint8_t* pDst3)
{
    // swizzle rgba -> bgra while we load
    simd16scalar comp0 = _simd16_load_ps(reinterpret_cast<const float*>(pSrc + FormatTraits<DstFormat>::swizzle(0) * sizeof(simd16scalar))); // float32 rrrrrrrrrrrrrrrr
    simd16scalar comp1 = _simd16_load_ps(reinterpret_cast<const float*>(pSrc + FormatTraits<DstFormat>::swizzle(1) * sizeof(simd16scalar))); // float32 gggggggggggggggg
    simd16scalar comp2 = _simd16_load_ps(reinterpret_cast<const float*>(pSrc + FormatTraits<DstFormat>::swizzle(2) * sizeof(simd16scalar))); // float32 bbbbbbbbbbbbbbbb

    // clamp
    const simd16scalar zero = _simd16_setzero_ps();
    const simd16scalar ones = _simd16_set1_ps(1.0f);

    comp0 = _simd16_max_ps(comp0, zero);
    comp0 = _simd16_min_ps(comp0, ones);

    comp1 = _simd16_max_ps(comp1, zero);
    comp1 = _simd16_min_ps(comp1, ones);

    comp2 = _simd16_max_ps(comp2, zero);
    comp2 = _simd16_min_ps(comp2, ones);

    // gamma-correct only rgb
    if (FormatTraits<DstFormat>::isSRGB)
    {
        comp0 = FormatTraits<R32G32B32A32_FLOAT>::convertSrgb(0, comp0);
        comp1 = FormatTraits<R32G32B32A32_FLOAT>::convertSrgb(1, comp1);
        comp2 = FormatTraits<R32G32B32A32_FLOAT>::convertSrgb(2, comp2);
    }

    // convert float components from 0.0f..1.0f to correct scale for 0..255 dest format
    comp0 = _simd16_mul_ps(comp0, _simd16_set1_ps(FormatTraits<DstFormat>::fromFloat(0)));
    comp1 = _simd16_mul_ps(comp1, _simd16_set1_ps(FormatTraits<DstFormat>::fromFloat(1)));
    comp2 = _simd16_mul_ps(comp2, _simd16_set1_ps(FormatTraits<DstFormat>::fromFloat(2)));

    // moving to 16 wide integer vector types
    simd16scalari src0 = _simd16_cvtps_epi32(comp0); // padded byte rrrrrrrrrrrrrrrr
    simd16scalari src1 = _simd16_cvtps_epi32(comp1); // padded byte gggggggggggggggg
    simd16scalari src2 = _simd16_cvtps_epi32(comp2); // padded byte bbbbbbbbbbbbbbbb

    // SOA to AOS conversion
    src1 = _simd16_slli_epi32(src1,  8);
    src2 = _simd16_slli_epi32(src2, 16);

    simd16scalari final = _simd16_or_si(_simd16_or_si(src0, src1), src2);                       // 0 1 2 3 4 5 6 7 8 9 A B C D E F

    // de-swizzle conversion
#if 1
    simd16scalari final0 = _simd16_permute2f128_si(final, final, 0xA0); // (2, 2, 0, 0)         // 0 1 2 3 0 1 2 3 8 9 A B 8 9 A B
    simd16scalari final1 = _simd16_permute2f128_si(final, final, 0xF5); // (3, 3, 1, 1)         // 4 5 6 7 4 5 6 7 C D E F C D E F

    final = _simd16_shuffle_epi64(final0, final1, 0xCC); // (1 1 0 0 1 1 0 0)                   // 0 1 4 5 2 3 6 7 8 9 C D A B E F

#else
    final = _simd16_permute_epi32(final, _simd16_set_epi32(15, 14, 11, 10, 13, 12, 9, 8, 7, 6, 3, 2, 5, 4, 1, 0));

#endif
    // store 8x2 memory order:
    //  row0: [ pDst0, pDst2 ] = { 0 1 4 5 }, { 8 9 C D }
    //  row1: [ pDst1, pDst3 ] = { 2 3 6 7 }, { A B E F }
    _simd_storeu2_si(reinterpret_cast<simd4scalari *>(pDst1), reinterpret_cast<simd4scalari *>(pDst0), _simd16_extract_si(final, 0));
    _simd_storeu2_si(reinterpret_cast<simd4scalari *>(pDst3), reinterpret_cast<simd4scalari *>(pDst2), _simd16_extract_si(final, 1));
}

template<SWR_FORMAT DstFormat>
INLINE static void FlatConvertNoAlpha(const uint8_t* pSrc, uint8_t* pDst, uint8_t* pDst1)
{
    static const uint32_t offset = sizeof(simdscalar);

    // swizzle rgba -> bgra while we load
    simdscalar vComp0 = _simd_load_ps((const float*)(pSrc + (FormatTraits<DstFormat>::swizzle(0))*offset)); // float32 rrrrrrrr
    simdscalar vComp1 = _simd_load_ps((const float*)(pSrc + (FormatTraits<DstFormat>::swizzle(1))*offset)); // float32 gggggggg
    simdscalar vComp2 = _simd_load_ps((const float*)(pSrc + (FormatTraits<DstFormat>::swizzle(2))*offset)); // float32 bbbbbbbb
                                                                                                            // clamp
    vComp0 = _simd_max_ps(vComp0, _simd_setzero_ps());
    vComp0 = _simd_min_ps(vComp0, _simd_set1_ps(1.0f));

    vComp1 = _simd_max_ps(vComp1, _simd_setzero_ps());
    vComp1 = _simd_min_ps(vComp1, _simd_set1_ps(1.0f));

    vComp2 = _simd_max_ps(vComp2, _simd_setzero_ps());
    vComp2 = _simd_min_ps(vComp2, _simd_set1_ps(1.0f));

    if (FormatTraits<DstFormat>::isSRGB)
    {
        // Gamma-correct only rgb
        vComp0 = FormatTraits<R32G32B32A32_FLOAT>::convertSrgb(0, vComp0);
        vComp1 = FormatTraits<R32G32B32A32_FLOAT>::convertSrgb(1, vComp1);
        vComp2 = FormatTraits<R32G32B32A32_FLOAT>::convertSrgb(2, vComp2);
    }

    // convert float components from 0.0f .. 1.0f to correct scale for 0 .. 255 dest format
    vComp0 = _simd_mul_ps(vComp0, _simd_set1_ps(FormatTraits<DstFormat>::fromFloat(0)));
    vComp1 = _simd_mul_ps(vComp1, _simd_set1_ps(FormatTraits<DstFormat>::fromFloat(1)));
    vComp2 = _simd_mul_ps(vComp2, _simd_set1_ps(FormatTraits<DstFormat>::fromFloat(2)));

    // moving to 8 wide integer vector types
    simdscalari src0 = _simd_cvtps_epi32(vComp0); // padded byte rrrrrrrr
    simdscalari src1 = _simd_cvtps_epi32(vComp1); // padded byte gggggggg
    simdscalari src2 = _simd_cvtps_epi32(vComp2); // padded byte bbbbbbbb

#if KNOB_ARCH <= KNOB_ARCH_AVX

    // splitting into two sets of 4 wide integer vector types
    // because AVX doesn't have instructions to support this operation at 8 wide
    simd4scalari srcLo0 = _mm256_castsi256_si128(src0); // 000r000r000r000r
    simd4scalari srcLo1 = _mm256_castsi256_si128(src1); // 000g000g000g000g
    simd4scalari srcLo2 = _mm256_castsi256_si128(src2); // 000b000b000b000b

    simd4scalari srcHi0 = _mm256_extractf128_si256(src0, 1); // 000r000r000r000r
    simd4scalari srcHi1 = _mm256_extractf128_si256(src1, 1); // 000g000g000g000g
    simd4scalari srcHi2 = _mm256_extractf128_si256(src2, 1); // 000b000b000b000b

    srcLo1 = _mm_slli_si128(srcLo1, 1); // 00g000g000g000g0
    srcHi1 = _mm_slli_si128(srcHi1, 1); // 00g000g000g000g0
    srcLo2 = _mm_slli_si128(srcLo2, 2); // 0b000b000b000b00
    srcHi2 = _mm_slli_si128(srcHi2, 2); // 0b000b000b000b00

    srcLo0 = SIMD128::or_si(srcLo0, srcLo1); // 00gr00gr00gr00gr

    srcHi0 = SIMD128::or_si(srcHi0, srcHi1); // 00gr00gr00gr00gr

    srcLo0 = SIMD128::or_si(srcLo0, srcLo2); // 0bgr0bgr0bgr0bgr
    srcHi0 = SIMD128::or_si(srcHi0, srcHi2); // 0bgr0bgr0bgr0bgr

    // unpack into rows that get the tiling order correct
    simd4scalari vRow00 = SIMD128::unpacklo_epi64(srcLo0, srcHi0);  // 0bgr0bgr0bgr0bgr0bgr0bgr0bgr0bgr
    simd4scalari vRow10 = SIMD128::unpackhi_epi64(srcLo0, srcHi0);

    simdscalari final = _mm256_castsi128_si256(vRow00);
    final = _mm256_insertf128_si256(final, vRow10, 1);

#else

                                              // logic is as above, only wider
    src1 = _mm256_slli_si256(src1, 1);
    src2 = _mm256_slli_si256(src2, 2);

    src0 = _mm256_or_si256(src0, src1);

    simdscalari final = _mm256_or_si256(src0, src2);

    // adjust the data to get the tiling order correct 0 1 2 3 -> 0 2 1 3
    final = _mm256_permute4x64_epi64(final, 0xD8);

#endif

    _simd_storeu2_si((simd4scalari*)pDst1, (simd4scalari*)pDst, final);
}

template<>
struct ConvertPixelsSOAtoAOS<R32G32B32A32_FLOAT, B8G8R8A8_UNORM>
{
    template <size_t NumDests>
    INLINE static void Convert(const uint8_t* pSrc, uint8_t* (&ppDsts)[NumDests])
    {
        FlatConvert<B8G8R8A8_UNORM>(pSrc, ppDsts[0], ppDsts[1], ppDsts[2], ppDsts[3]);
    }
};

template<>
struct ConvertPixelsSOAtoAOS<R32G32B32A32_FLOAT, B8G8R8X8_UNORM>
{
    template <size_t NumDests>
    INLINE static void Convert(const uint8_t* pSrc, uint8_t* (&ppDsts)[NumDests])
    {
        FlatConvertNoAlpha<B8G8R8X8_UNORM>(pSrc, ppDsts[0], ppDsts[1], ppDsts[2], ppDsts[3]);
    }
};

template<>
struct ConvertPixelsSOAtoAOS < R32G32B32A32_FLOAT, B8G8R8A8_UNORM_SRGB >
{
    template <size_t NumDests>
    INLINE static void Convert(const uint8_t* pSrc, uint8_t* (&ppDsts)[NumDests])
    {
        FlatConvert<B8G8R8A8_UNORM_SRGB>(pSrc, ppDsts[0], ppDsts[1], ppDsts[2], ppDsts[3]);
    }
};

template<>
struct ConvertPixelsSOAtoAOS < R32G32B32A32_FLOAT, B8G8R8X8_UNORM_SRGB >
{
    template <size_t NumDests>
    INLINE static void Convert(const uint8_t* pSrc, uint8_t* (&ppDsts)[NumDests])
    {
        FlatConvertNoAlpha<B8G8R8X8_UNORM_SRGB>(pSrc, ppDsts[0], ppDsts[1], ppDsts[2], ppDsts[3]);
    }
};

template<>
struct ConvertPixelsSOAtoAOS < R32G32B32A32_FLOAT, R8G8B8A8_UNORM >
{
    template <size_t NumDests>
    INLINE static void Convert(const uint8_t* pSrc, uint8_t* (&ppDsts)[NumDests])
    {
        FlatConvert<R8G8B8A8_UNORM>(pSrc, ppDsts[0], ppDsts[1], ppDsts[2], ppDsts[3]);
    }
};

template<>
struct ConvertPixelsSOAtoAOS < R32G32B32A32_FLOAT, R8G8B8X8_UNORM >
{
    template <size_t NumDests>
    INLINE static void Convert(const uint8_t* pSrc, uint8_t* (&ppDsts)[NumDests])
    {
        FlatConvertNoAlpha<R8G8B8X8_UNORM>(pSrc, ppDsts[0], ppDsts[1], ppDsts[2], ppDsts[3]);
    }
};

template<>
struct ConvertPixelsSOAtoAOS < R32G32B32A32_FLOAT, R8G8B8A8_UNORM_SRGB >
{
    template <size_t NumDests>
    INLINE static void Convert(const uint8_t* pSrc, uint8_t* (&ppDsts)[NumDests])
    {
        FlatConvert<R8G8B8A8_UNORM_SRGB>(pSrc, ppDsts[0], ppDsts[1], ppDsts[2], ppDsts[3]);
    }
};

template<>
struct ConvertPixelsSOAtoAOS < R32G32B32A32_FLOAT, R8G8B8X8_UNORM_SRGB >
{
    template <size_t NumDests>
    INLINE static void Convert(const uint8_t* pSrc, uint8_t* (&ppDsts)[NumDests])
    {
        FlatConvertNoAlpha<R8G8B8X8_UNORM_SRGB>(pSrc, ppDsts[0], ppDsts[1], ppDsts[2], ppDsts[3]);
    }
};

//////////////////////////////////////////////////////////////////////////
/// StoreRasterTile
//////////////////////////////////////////////////////////////////////////
template<typename TTraits, SWR_FORMAT SrcFormat, SWR_FORMAT DstFormat>
struct StoreRasterTile
{
    //////////////////////////////////////////////////////////////////////////
    /// @brief Retrieve color from hot tile source which is always float.
    /// @param pSrc - Pointer to raster tile.
    /// @param x, y - Coordinates to raster tile.
    /// @param output - output color
    INLINE static void GetSwizzledSrcColor(
        uint8_t* pSrc,
        uint32_t x, uint32_t y,
        float outputColor[4])
    {
        typedef SimdTile_16<SrcFormat, DstFormat> SimdT;

        SimdT *pSrcSimdTiles = reinterpret_cast<SimdT *>(pSrc);

        // Compute which simd tile we're accessing within 8x8 tile.
        //   i.e. Compute linear simd tile coordinate given (x, y) in pixel coordinates.
        uint32_t simdIndex = (y / SIMD16_TILE_Y_DIM) * (KNOB_TILE_X_DIM / SIMD16_TILE_X_DIM) + (x / SIMD16_TILE_X_DIM);

        SimdT *pSimdTile = &pSrcSimdTiles[simdIndex];

        uint32_t simdOffset = (y % SIMD16_TILE_Y_DIM) * SIMD16_TILE_X_DIM + (x % SIMD16_TILE_X_DIM);

        pSimdTile->GetSwizzledColor(simdOffset, outputColor);
    }

    //////////////////////////////////////////////////////////////////////////
    /// @brief Stores an 8x8 raster tile to the destination surface.
    /// @param pSrc - Pointer to raster tile.
    /// @param pDstSurface - Destination surface state
    /// @param x, y - Coordinates to raster tile.
    INLINE static void Store(
        uint8_t *pSrc,
        SWR_SURFACE_STATE* pDstSurface,
        uint32_t x, uint32_t y, uint32_t sampleNum, uint32_t renderTargetArrayIndex) // (x, y) pixel coordinate to start of raster tile.
    {
        uint32_t lodWidth = std::max(pDstSurface->width >> pDstSurface->lod, 1U);
        uint32_t lodHeight = std::max(pDstSurface->height >> pDstSurface->lod, 1U);

        // For each raster tile pixel (rx, ry)
        for (uint32_t ry = 0; ry < KNOB_TILE_Y_DIM; ++ry)
        {
            for (uint32_t rx = 0; rx < KNOB_TILE_X_DIM; ++rx)
            {
                // Perform bounds checking.
                if (((x + rx) < lodWidth) &&
                    ((y + ry) < lodHeight))
                {
                    float srcColor[4];
                    GetSwizzledSrcColor(pSrc, rx, ry, srcColor);

                    uint8_t *pDst = (uint8_t*)ComputeSurfaceAddress<false, false>((x + rx), (y + ry),
                        pDstSurface->arrayIndex + renderTargetArrayIndex, pDstSurface->arrayIndex + renderTargetArrayIndex,
                        sampleNum, pDstSurface->lod, pDstSurface);
                    {
                        ConvertPixelFromFloat<DstFormat>(pDst, srcColor);
                    }
                }
            }
        }
    }

    //////////////////////////////////////////////////////////////////////////
    /// @brief Resolves an 8x8 raster tile to the resolve destination surface.
    /// @param pSrc - Pointer to raster tile.
    /// @param pDstSurface - Destination surface state
    /// @param x, y - Coordinates to raster tile.
    /// @param sampleOffset - Offset between adjacent multisamples
    INLINE static void Resolve(
        uint8_t *pSrc,
        SWR_SURFACE_STATE* pDstSurface,
        uint32_t x, uint32_t y, uint32_t sampleOffset, uint32_t renderTargetArrayIndex) // (x, y) pixel coordinate to start of raster tile.
    {
        uint32_t lodWidth = std::max(pDstSurface->width >> pDstSurface->lod, 1U);
        uint32_t lodHeight = std::max(pDstSurface->height >> pDstSurface->lod, 1U);

        float oneOverNumSamples = 1.0f / pDstSurface->numSamples;

        // For each raster tile pixel (rx, ry)
        for (uint32_t ry = 0; ry < KNOB_TILE_Y_DIM; ++ry)
        {
            for (uint32_t rx = 0; rx < KNOB_TILE_X_DIM; ++rx)
            {
                // Perform bounds checking.
                if (((x + rx) < lodWidth) &&
                        ((y + ry) < lodHeight))
                {
                    // Sum across samples
                    float resolveColor[4] = {0};
                    for (uint32_t sampleNum = 0; sampleNum < pDstSurface->numSamples; sampleNum++)
                    {
                        float sampleColor[4] = {0};
                        uint8_t *pSampleSrc = pSrc + sampleOffset * sampleNum;
                        GetSwizzledSrcColor(pSampleSrc, rx, ry, sampleColor);
                        resolveColor[0] += sampleColor[0];
                        resolveColor[1] += sampleColor[1];
                        resolveColor[2] += sampleColor[2];
                        resolveColor[3] += sampleColor[3];
                    }

                    // Divide by numSamples to average
                    resolveColor[0] *= oneOverNumSamples;
                    resolveColor[1] *= oneOverNumSamples;
                    resolveColor[2] *= oneOverNumSamples;
                    resolveColor[3] *= oneOverNumSamples;

                    // Use the resolve surface state
                    SWR_SURFACE_STATE* pResolveSurface = (SWR_SURFACE_STATE*)pDstSurface->xpAuxBaseAddress;
                    uint8_t *pDst = (uint8_t*)ComputeSurfaceAddress<false, false>((x + rx), (y + ry),
                        pResolveSurface->arrayIndex + renderTargetArrayIndex, pResolveSurface->arrayIndex + renderTargetArrayIndex,
                        0, pResolveSurface->lod, pResolveSurface);
                    {
                        ConvertPixelFromFloat<DstFormat>(pDst, resolveColor);
                    }
                }
            }
        }
    }

};

template<typename TTraits, SWR_FORMAT SrcFormat, SWR_FORMAT DstFormat>
struct OptStoreRasterTile : StoreRasterTile<TTraits, SrcFormat, DstFormat>
{};

//////////////////////////////////////////////////////////////////////////
/// OptStoreRasterTile - SWR_TILE_MODE_NONE specialization for 8bpp
//////////////////////////////////////////////////////////////////////////
template<SWR_FORMAT SrcFormat, SWR_FORMAT DstFormat>
struct OptStoreRasterTile< TilingTraits<SWR_TILE_NONE, 8>, SrcFormat, DstFormat>
{
    typedef StoreRasterTile<TilingTraits<SWR_TILE_NONE, 8>, SrcFormat, DstFormat> GenericStoreTile;
    static const size_t SRC_BYTES_PER_PIXEL = FormatTraits<SrcFormat>::bpp / 8;
    static const size_t DST_BYTES_PER_PIXEL = FormatTraits<DstFormat>::bpp / 8;

    //////////////////////////////////////////////////////////////////////////
    /// @brief Stores an 8x8 raster tile to the destination surface.
    /// @param pSrc - Pointer to raster tile.
    /// @param pDstSurface - Destination surface state
    /// @param x, y - Coordinates to raster tile.
    INLINE static void Store(
        uint8_t *pSrc,
        SWR_SURFACE_STATE* pDstSurface,
        uint32_t x, uint32_t y, uint32_t sampleNum, uint32_t renderTargetArrayIndex)
    {
        // Punt non-full tiles to generic store
        uint32_t lodWidth = std::max(pDstSurface->width >> pDstSurface->lod, 1U);
        uint32_t lodHeight = std::max(pDstSurface->height >> pDstSurface->lod, 1U);

        if (x + KNOB_TILE_X_DIM > lodWidth || y + KNOB_TILE_Y_DIM > lodHeight)
        {
            return GenericStoreTile::Store(pSrc, pDstSurface, x, y, sampleNum, renderTargetArrayIndex);
        }

        uint8_t *pDst = (uint8_t*)ComputeSurfaceAddress<false, false>(x, y, pDstSurface->arrayIndex + renderTargetArrayIndex,
            pDstSurface->arrayIndex + renderTargetArrayIndex, sampleNum, pDstSurface->lod, pDstSurface);

        const uint32_t dx = SIMD16_TILE_X_DIM * DST_BYTES_PER_PIXEL;
        const uint32_t dy = SIMD16_TILE_Y_DIM * pDstSurface->pitch - KNOB_TILE_X_DIM * DST_BYTES_PER_PIXEL;

        uint8_t* ppDsts[] =
        {
            pDst,                                           // row 0, col 0
            pDst + pDstSurface->pitch,                      // row 1, col 0
            pDst + dx / 2,                                  // row 0, col 1
            pDst + pDstSurface->pitch + dx / 2              // row 1, col 1
        };

        for (uint32_t yy = 0; yy < KNOB_TILE_Y_DIM; yy += SIMD16_TILE_Y_DIM)
        {
            for (uint32_t xx = 0; xx < KNOB_TILE_X_DIM; xx += SIMD16_TILE_X_DIM)
            {
                ConvertPixelsSOAtoAOS<SrcFormat, DstFormat>::Convert(pSrc, ppDsts);

                pSrc += KNOB_SIMD16_WIDTH * SRC_BYTES_PER_PIXEL;

                ppDsts[0] += dx;
                ppDsts[1] += dx;
                ppDsts[2] += dx;
                ppDsts[3] += dx;
            }

            ppDsts[0] += dy;
            ppDsts[1] += dy;
            ppDsts[2] += dy;
            ppDsts[3] += dy;
        }
    }
};

//////////////////////////////////////////////////////////////////////////
/// OptStoreRasterTile - SWR_TILE_MODE_NONE specialization for 16bpp
//////////////////////////////////////////////////////////////////////////
template<SWR_FORMAT SrcFormat, SWR_FORMAT DstFormat>
struct OptStoreRasterTile< TilingTraits<SWR_TILE_NONE, 16>, SrcFormat, DstFormat>
{
    typedef StoreRasterTile<TilingTraits<SWR_TILE_NONE, 16>, SrcFormat, DstFormat> GenericStoreTile;
    static const size_t SRC_BYTES_PER_PIXEL = FormatTraits<SrcFormat>::bpp / 8;
    static const size_t DST_BYTES_PER_PIXEL = FormatTraits<DstFormat>::bpp / 8;

    //////////////////////////////////////////////////////////////////////////
    /// @brief Stores an 8x8 raster tile to the destination surface.
    /// @param pSrc - Pointer to raster tile.
    /// @param pDstSurface - Destination surface state
    /// @param x, y - Coordinates to raster tile.
    INLINE static void Store(
        uint8_t *pSrc,
        SWR_SURFACE_STATE* pDstSurface,
        uint32_t x, uint32_t y, uint32_t sampleNum, uint32_t renderTargetArrayIndex)
    {
        // Punt non-full tiles to generic store
        uint32_t lodWidth = std::max(pDstSurface->width >> pDstSurface->lod, 1U);
        uint32_t lodHeight = std::max(pDstSurface->height >> pDstSurface->lod, 1U);

        if (x + KNOB_TILE_X_DIM > lodWidth || y + KNOB_TILE_Y_DIM > lodHeight)
        {
            return GenericStoreTile::Store(pSrc, pDstSurface, x, y, sampleNum, renderTargetArrayIndex);
        }

        uint8_t *pDst = (uint8_t*)ComputeSurfaceAddress<false, false>(x, y, pDstSurface->arrayIndex + renderTargetArrayIndex,
            pDstSurface->arrayIndex + renderTargetArrayIndex, sampleNum, pDstSurface->lod, pDstSurface);

        const uint32_t dx = SIMD16_TILE_X_DIM * DST_BYTES_PER_PIXEL;
        const uint32_t dy = SIMD16_TILE_Y_DIM * pDstSurface->pitch - KNOB_TILE_X_DIM * DST_BYTES_PER_PIXEL;

        uint8_t* ppDsts[] =
        {
            pDst,                                           // row 0, col 0
            pDst + pDstSurface->pitch,                      // row 1, col 0
            pDst + dx / 2,                                  // row 0, col 1
            pDst + pDstSurface->pitch + dx / 2              // row 1, col 1
        };

        for (uint32_t yy = 0; yy < KNOB_TILE_Y_DIM; yy += SIMD16_TILE_Y_DIM)
        {
            for (uint32_t xx = 0; xx < KNOB_TILE_X_DIM; xx += SIMD16_TILE_X_DIM)
            {
                ConvertPixelsSOAtoAOS<SrcFormat, DstFormat>::Convert(pSrc, ppDsts);

                pSrc += KNOB_SIMD16_WIDTH * SRC_BYTES_PER_PIXEL;

                ppDsts[0] += dx;
                ppDsts[1] += dx;
                ppDsts[2] += dx;
                ppDsts[3] += dx;
            }

            ppDsts[0] += dy;
            ppDsts[1] += dy;
            ppDsts[2] += dy;
            ppDsts[3] += dy;
        }
    }
};

//////////////////////////////////////////////////////////////////////////
/// OptStoreRasterTile - SWR_TILE_MODE_NONE specialization for 32bpp
//////////////////////////////////////////////////////////////////////////
template<SWR_FORMAT SrcFormat, SWR_FORMAT DstFormat>
struct OptStoreRasterTile< TilingTraits<SWR_TILE_NONE, 32>, SrcFormat, DstFormat>
{
    typedef StoreRasterTile<TilingTraits<SWR_TILE_NONE, 32>, SrcFormat, DstFormat> GenericStoreTile;
    static const size_t SRC_BYTES_PER_PIXEL = FormatTraits<SrcFormat>::bpp / 8;
    static const size_t DST_BYTES_PER_PIXEL = FormatTraits<DstFormat>::bpp / 8;

    //////////////////////////////////////////////////////////////////////////
    /// @brief Stores an 8x8 raster tile to the destination surface.
    /// @param pSrc - Pointer to raster tile.
    /// @param pDstSurface - Destination surface state
    /// @param x, y - Coordinates to raster tile.
    INLINE static void Store(
        uint8_t *pSrc,
        SWR_SURFACE_STATE* pDstSurface,
        uint32_t x, uint32_t y, uint32_t sampleNum, uint32_t renderTargetArrayIndex)
    {
        // Punt non-full tiles to generic store
        uint32_t lodWidth = std::max(pDstSurface->width >> pDstSurface->lod, 1U);
        uint32_t lodHeight = std::max(pDstSurface->height >> pDstSurface->lod, 1U);

        if (x + KNOB_TILE_X_DIM > lodWidth || y + KNOB_TILE_Y_DIM > lodHeight)
        {
            return GenericStoreTile::Store(pSrc, pDstSurface, x, y, sampleNum, renderTargetArrayIndex);
        }

        uint8_t *pDst = (uint8_t*)ComputeSurfaceAddress<false, false>(x, y, pDstSurface->arrayIndex + renderTargetArrayIndex,
            pDstSurface->arrayIndex + renderTargetArrayIndex, sampleNum, pDstSurface->lod, pDstSurface);

        const uint32_t dx = SIMD16_TILE_X_DIM * DST_BYTES_PER_PIXEL;
        const uint32_t dy = SIMD16_TILE_Y_DIM * pDstSurface->pitch - KNOB_TILE_X_DIM * DST_BYTES_PER_PIXEL;

        uint8_t* ppDsts[] =
        {
            pDst,                                           // row 0, col 0
            pDst + pDstSurface->pitch,                      // row 1, col 0
            pDst + dx / 2,                                  // row 0, col 1
            pDst + pDstSurface->pitch + dx / 2              // row 1, col 1
        };

        for (uint32_t yy = 0; yy < KNOB_TILE_Y_DIM; yy += SIMD16_TILE_Y_DIM)
        {
            for (uint32_t xx = 0; xx < KNOB_TILE_X_DIM; xx += SIMD16_TILE_X_DIM)
            {
                ConvertPixelsSOAtoAOS<SrcFormat, DstFormat>::Convert(pSrc, ppDsts);

                pSrc += KNOB_SIMD16_WIDTH * SRC_BYTES_PER_PIXEL;

                ppDsts[0] += dx;
                ppDsts[1] += dx;
                ppDsts[2] += dx;
                ppDsts[3] += dx;
            }

            ppDsts[0] += dy;
            ppDsts[1] += dy;
            ppDsts[2] += dy;
            ppDsts[3] += dy;
        }
    }
};

//////////////////////////////////////////////////////////////////////////
/// OptStoreRasterTile - SWR_TILE_MODE_NONE specialization for 64bpp
//////////////////////////////////////////////////////////////////////////
template<SWR_FORMAT SrcFormat, SWR_FORMAT DstFormat>
struct OptStoreRasterTile< TilingTraits<SWR_TILE_NONE, 64>, SrcFormat, DstFormat>
{
    typedef StoreRasterTile<TilingTraits<SWR_TILE_NONE, 64>, SrcFormat, DstFormat> GenericStoreTile;
    static const size_t SRC_BYTES_PER_PIXEL = FormatTraits<SrcFormat>::bpp / 8;
    static const size_t DST_BYTES_PER_PIXEL = FormatTraits<DstFormat>::bpp / 8;
    static const size_t MAX_DST_COLUMN_BYTES = 16;

    //////////////////////////////////////////////////////////////////////////
    /// @brief Stores an 8x8 raster tile to the destination surface.
    /// @param pSrc - Pointer to raster tile.
    /// @param pDstSurface - Destination surface state
    /// @param x, y - Coordinates to raster tile.
    INLINE static void Store(
        uint8_t *pSrc,
        SWR_SURFACE_STATE* pDstSurface,
        uint32_t x, uint32_t y, uint32_t sampleNum, uint32_t renderTargetArrayIndex)
    {
        // Punt non-full tiles to generic store
        uint32_t lodWidth = std::max(pDstSurface->width >> pDstSurface->lod, 1U);
        uint32_t lodHeight = std::max(pDstSurface->height >> pDstSurface->lod, 1U);

        if (x + KNOB_TILE_X_DIM > lodWidth || y + KNOB_TILE_Y_DIM > lodHeight)
        {
            return GenericStoreTile::Store(pSrc, pDstSurface, x, y, sampleNum, renderTargetArrayIndex);
        }

        uint8_t *pDst = (uint8_t*)ComputeSurfaceAddress<false, false>(x, y, pDstSurface->arrayIndex + renderTargetArrayIndex,
            pDstSurface->arrayIndex + renderTargetArrayIndex, sampleNum, pDstSurface->lod, pDstSurface);

        const uint32_t dx = SIMD16_TILE_X_DIM * DST_BYTES_PER_PIXEL;
        const uint32_t dy = SIMD16_TILE_Y_DIM * pDstSurface->pitch;

        // we have to break these large spans up, since ConvertPixelsSOAtoAOS() can only work on max 16B spans (a TileY limitation)
        static_assert(dx == MAX_DST_COLUMN_BYTES * 4, "Invalid column offsets");

        uint8_t *ppDsts[] =
        {
            pDst,                                                               // row 0, col 0
            pDst + pDstSurface->pitch,                                          // row 1, col 0
            pDst + MAX_DST_COLUMN_BYTES,                                        // row 0, col 1
            pDst + pDstSurface->pitch + MAX_DST_COLUMN_BYTES,                   // row 1, col 1
            pDst + MAX_DST_COLUMN_BYTES * 2,                                    // row 0, col 2
            pDst + pDstSurface->pitch + MAX_DST_COLUMN_BYTES * 2,               // row 1, col 2
            pDst + MAX_DST_COLUMN_BYTES * 3,                                    // row 0, col 3
            pDst + pDstSurface->pitch + MAX_DST_COLUMN_BYTES * 3                // row 1, col 3
        };

        for (uint32_t yy = 0; yy < KNOB_TILE_Y_DIM; yy += SIMD16_TILE_Y_DIM)
        {
            // Raster tile width is same as simd16 tile width
            static_assert(KNOB_TILE_X_DIM == SIMD16_TILE_X_DIM, "Invalid tile x dim");

            ConvertPixelsSOAtoAOS<SrcFormat, DstFormat>::Convert(pSrc, ppDsts);

            pSrc += KNOB_SIMD16_WIDTH * SRC_BYTES_PER_PIXEL;

            for (uint32_t i = 0; i < ARRAY_SIZE(ppDsts); i += 1)
            {
                ppDsts[i] += dy;
            }
        }
    }
};

//////////////////////////////////////////////////////////////////////////
/// OptStoreRasterTile - SWR_TILE_MODE_NONE specialization for 128bpp
//////////////////////////////////////////////////////////////////////////
template<SWR_FORMAT SrcFormat, SWR_FORMAT DstFormat>
struct OptStoreRasterTile< TilingTraits<SWR_TILE_NONE, 128>, SrcFormat, DstFormat>
{
    typedef StoreRasterTile<TilingTraits<SWR_TILE_NONE, 128>, SrcFormat, DstFormat> GenericStoreTile;
    static const size_t SRC_BYTES_PER_PIXEL = FormatTraits<SrcFormat>::bpp / 8;
    static const size_t DST_BYTES_PER_PIXEL = FormatTraits<DstFormat>::bpp / 8;
    static const size_t MAX_DST_COLUMN_BYTES = 16;

    //////////////////////////////////////////////////////////////////////////
    /// @brief Stores an 8x8 raster tile to the destination surface.
    /// @param pSrc - Pointer to raster tile.
    /// @param pDstSurface - Destination surface state
    /// @param x, y - Coordinates to raster tile.
    INLINE static void Store(
        uint8_t *pSrc,
        SWR_SURFACE_STATE* pDstSurface,
        uint32_t x, uint32_t y, uint32_t sampleNum, uint32_t renderTargetArrayIndex)
    {
        // Punt non-full tiles to generic store
        uint32_t lodWidth = std::max(pDstSurface->width >> pDstSurface->lod, 1U);
        uint32_t lodHeight = std::max(pDstSurface->height >> pDstSurface->lod, 1U);

        if (x + KNOB_TILE_X_DIM > lodWidth || y + KNOB_TILE_Y_DIM > lodHeight)
        {
            return GenericStoreTile::Store(pSrc, pDstSurface, x, y, sampleNum, renderTargetArrayIndex);
        }

        uint8_t *pDst = (uint8_t*)ComputeSurfaceAddress<false, false>(x, y, pDstSurface->arrayIndex + renderTargetArrayIndex,
            pDstSurface->arrayIndex + renderTargetArrayIndex, sampleNum, pDstSurface->lod, pDstSurface);

        const uint32_t dx = SIMD16_TILE_X_DIM * DST_BYTES_PER_PIXEL;
        const uint32_t dy = SIMD16_TILE_Y_DIM * pDstSurface->pitch;

        // we have to break these large spans up, since ConvertPixelsSOAtoAOS() can only work on max 16B spans (a TileY limitation)
        static_assert(dx == MAX_DST_COLUMN_BYTES * 8, "Invalid column offsets");

        uint8_t* ppDsts[] =
        {
            pDst,                                                               // row 0, col 0
            pDst + pDstSurface->pitch,                                          // row 1, col 0
            pDst + MAX_DST_COLUMN_BYTES,                                        // row 0, col 1
            pDst + pDstSurface->pitch + MAX_DST_COLUMN_BYTES,                   // row 1, col 1
            pDst + MAX_DST_COLUMN_BYTES * 2,                                    // row 0, col 2
            pDst + pDstSurface->pitch + MAX_DST_COLUMN_BYTES * 2,               // row 1, col 2
            pDst + MAX_DST_COLUMN_BYTES * 3,                                    // row 0, col 3
            pDst + pDstSurface->pitch + MAX_DST_COLUMN_BYTES * 3,               // row 1, col 3
            pDst + MAX_DST_COLUMN_BYTES * 4,                                    // row 0, col 4
            pDst + pDstSurface->pitch + MAX_DST_COLUMN_BYTES * 4,               // row 1, col 4
            pDst + MAX_DST_COLUMN_BYTES * 5,                                    // row 0, col 5
            pDst + pDstSurface->pitch + MAX_DST_COLUMN_BYTES * 5,               // row 1, col 5
            pDst + MAX_DST_COLUMN_BYTES * 6,                                    // row 0, col 6
            pDst + pDstSurface->pitch + MAX_DST_COLUMN_BYTES * 6,               // row 1, col 6
            pDst + MAX_DST_COLUMN_BYTES * 7,                                    // row 0, col 7
            pDst + pDstSurface->pitch + MAX_DST_COLUMN_BYTES * 7,               // row 1, col 7
        };

        for (uint32_t yy = 0; yy < KNOB_TILE_Y_DIM; yy += SIMD16_TILE_Y_DIM)
        {
            // Raster tile width is same as simd16 tile width
            static_assert(KNOB_TILE_X_DIM == SIMD16_TILE_X_DIM, "Invalid tile x dim");

            ConvertPixelsSOAtoAOS<SrcFormat, DstFormat>::Convert(pSrc, ppDsts);

            pSrc += KNOB_SIMD16_WIDTH * SRC_BYTES_PER_PIXEL;

            for (uint32_t i = 0; i < ARRAY_SIZE(ppDsts); i += 1)
            {
                ppDsts[i] += dy;
            }
        }
    }
};

//////////////////////////////////////////////////////////////////////////
/// OptStoreRasterTile - TILE_MODE_YMAJOR specialization for 8bpp
//////////////////////////////////////////////////////////////////////////
template<SWR_FORMAT SrcFormat, SWR_FORMAT DstFormat>
struct OptStoreRasterTile< TilingTraits<SWR_TILE_MODE_YMAJOR, 8>, SrcFormat, DstFormat>
{
    typedef StoreRasterTile<TilingTraits<SWR_TILE_MODE_YMAJOR, 8>, SrcFormat, DstFormat> GenericStoreTile;
    static const size_t SRC_BYTES_PER_PIXEL = FormatTraits<SrcFormat>::bpp / 8;

    //////////////////////////////////////////////////////////////////////////
    /// @brief Stores an 8x8 raster tile to the destination surface.
    /// @param pSrc - Pointer to raster tile.
    /// @param pDstSurface - Destination surface state
    /// @param x, y - Coordinates to raster tile.
    INLINE static void Store(
        uint8_t *pSrc,
        SWR_SURFACE_STATE* pDstSurface,
        uint32_t x, uint32_t y, uint32_t sampleNum, uint32_t renderTargetArrayIndex)
    {
        static const uint32_t DestRowWidthBytes = 16;                    // 16B rows

        // Punt non-full tiles to generic store
        uint32_t lodWidth = std::max(pDstSurface->width >> pDstSurface->lod, 1U);
        uint32_t lodHeight = std::max(pDstSurface->height >> pDstSurface->lod, 1U);

        if (x + KNOB_TILE_X_DIM > lodWidth || y + KNOB_TILE_Y_DIM > lodHeight)
        {
            return GenericStoreTile::Store(pSrc, pDstSurface, x, y, sampleNum, renderTargetArrayIndex);
        }

        // TileY is a column-major tiling mode where each 4KB tile consist of 8 columns of 32 x 16B rows.
        // We can compute the offsets to each column within the raster tile once and increment from these.
        // There will be 4 8x2 simd tiles in an 8x8 raster tile.
        uint8_t *pDst = (uint8_t*)ComputeSurfaceAddress<false, false>(x, y, pDstSurface->arrayIndex + renderTargetArrayIndex,
            pDstSurface->arrayIndex + renderTargetArrayIndex, sampleNum, pDstSurface->lod, pDstSurface);

        const uint32_t dy = SIMD16_TILE_Y_DIM * DestRowWidthBytes;

        // The Hot Tile uses a row-major tiling mode and has a larger memory footprint. So we iterate in a row-major pattern.
        uint8_t *ppDsts[] =
        {
            pDst,
            pDst + DestRowWidthBytes,
            pDst + DestRowWidthBytes / 4,
            pDst + DestRowWidthBytes + DestRowWidthBytes / 4
        };

        for (uint32_t yy = 0; yy < KNOB_TILE_Y_DIM; yy += SIMD16_TILE_Y_DIM)
        {
            // Raster tile width is same as simd16 tile width
            static_assert(KNOB_TILE_X_DIM == SIMD16_TILE_X_DIM, "Invalid tile x dim");

            ConvertPixelsSOAtoAOS<SrcFormat, DstFormat>::Convert(pSrc, ppDsts);

            pSrc += KNOB_SIMD16_WIDTH * SRC_BYTES_PER_PIXEL;

            ppDsts[0] += dy;
            ppDsts[1] += dy;
            ppDsts[2] += dy;
            ppDsts[3] += dy;
        }
    }
};

//////////////////////////////////////////////////////////////////////////
/// OptStoreRasterTile - TILE_MODE_YMAJOR specialization for 16bpp
//////////////////////////////////////////////////////////////////////////
template<SWR_FORMAT SrcFormat, SWR_FORMAT DstFormat>
struct OptStoreRasterTile< TilingTraits<SWR_TILE_MODE_YMAJOR, 16>, SrcFormat, DstFormat>
{
    typedef StoreRasterTile<TilingTraits<SWR_TILE_MODE_YMAJOR, 16>, SrcFormat, DstFormat> GenericStoreTile;
    static const size_t SRC_BYTES_PER_PIXEL = FormatTraits<SrcFormat>::bpp / 8;

    //////////////////////////////////////////////////////////////////////////
    /// @brief Stores an 8x8 raster tile to the destination surface.
    /// @param pSrc - Pointer to raster tile.
    /// @param pDstSurface - Destination surface state
    /// @param x, y - Coordinates to raster tile.
    INLINE static void Store(
        uint8_t *pSrc,
        SWR_SURFACE_STATE* pDstSurface,
        uint32_t x, uint32_t y, uint32_t sampleNum, uint32_t renderTargetArrayIndex)
    {
        static const uint32_t DestRowWidthBytes = 16;                    // 16B rows

        // Punt non-full tiles to generic store
        uint32_t lodWidth = std::max(pDstSurface->width >> pDstSurface->lod, 1U);
        uint32_t lodHeight = std::max(pDstSurface->height >> pDstSurface->lod, 1U);

        if (x + KNOB_TILE_X_DIM > lodWidth || y + KNOB_TILE_Y_DIM > lodHeight)
        {
            return GenericStoreTile::Store(pSrc, pDstSurface, x, y, sampleNum, renderTargetArrayIndex);
        }

        // TileY is a column-major tiling mode where each 4KB tile consist of 8 columns of 32 x 16B rows.
        // We can compute the offsets to each column within the raster tile once and increment from these.
        // There will be 4 8x2 simd tiles in an 8x8 raster tile.
        uint8_t *pDst = (uint8_t*)ComputeSurfaceAddress<false, false>(x, y, pDstSurface->arrayIndex + renderTargetArrayIndex,
            pDstSurface->arrayIndex + renderTargetArrayIndex, sampleNum, pDstSurface->lod, pDstSurface);

        const uint32_t dy = SIMD16_TILE_Y_DIM * DestRowWidthBytes;

        // The Hot Tile uses a row-major tiling mode and has a larger memory footprint. So we iterate in a row-major pattern.
        uint8_t *ppDsts[] =
        {
            pDst,
            pDst + DestRowWidthBytes,
            pDst + DestRowWidthBytes / 2,
            pDst + DestRowWidthBytes + DestRowWidthBytes / 2
        };

        for (uint32_t yy = 0; yy < KNOB_TILE_Y_DIM; yy += SIMD16_TILE_Y_DIM)
        {
            // Raster tile width is same as simd16 tile width
            static_assert(KNOB_TILE_X_DIM == SIMD16_TILE_X_DIM, "Invalid tile x dim");

            ConvertPixelsSOAtoAOS<SrcFormat, DstFormat>::Convert(pSrc, ppDsts);

            pSrc += KNOB_SIMD16_WIDTH * SRC_BYTES_PER_PIXEL;

            ppDsts[0] += dy;
            ppDsts[1] += dy;
            ppDsts[2] += dy;
            ppDsts[3] += dy;
        }
    }
};

//////////////////////////////////////////////////////////////////////////
/// OptStoreRasterTile - TILE_MODE_XMAJOR specialization for 32bpp
//////////////////////////////////////////////////////////////////////////
template<SWR_FORMAT SrcFormat, SWR_FORMAT DstFormat>
struct OptStoreRasterTile< TilingTraits<SWR_TILE_MODE_XMAJOR, 32>, SrcFormat, DstFormat>
{
    typedef StoreRasterTile<TilingTraits<SWR_TILE_MODE_XMAJOR, 32>, SrcFormat, DstFormat> GenericStoreTile;
    static const size_t SRC_BYTES_PER_PIXEL = FormatTraits<SrcFormat>::bpp / 8;
    static const size_t DST_BYTES_PER_PIXEL = FormatTraits<DstFormat>::bpp / 8;

    //////////////////////////////////////////////////////////////////////////
    /// @brief Stores an 8x8 raster tile to the destination surface.
    /// @param pSrc - Pointer to raster tile.
    /// @param pDstSurface - Destination surface state
    /// @param x, y - Coordinates to raster tile.
    INLINE static void Store(
        uint8_t *pSrc,
        SWR_SURFACE_STATE* pDstSurface,
        uint32_t x, uint32_t y, uint32_t sampleNum, uint32_t renderTargetArrayIndex)
    {
        static const uint32_t DestRowWidthBytes = 512;                   // 512B rows

        // Punt non-full tiles to generic store
        uint32_t lodWidth = std::max(pDstSurface->width >> pDstSurface->lod, 1U);
        uint32_t lodHeight = std::max(pDstSurface->height >> pDstSurface->lod, 1U);

        if (x + KNOB_TILE_X_DIM > lodWidth || y + KNOB_TILE_Y_DIM > lodHeight)
        {
            return GenericStoreTile::Store(pSrc, pDstSurface, x, y, sampleNum, renderTargetArrayIndex);
        }

        // TileX is a row-major tiling mode where each 4KB tile consist of 8 x 512B rows.
        // We can compute the offsets to each column within the raster tile once and increment from these.
        uint8_t *pDst = (uint8_t*)ComputeSurfaceAddress<false, false>(x, y, pDstSurface->arrayIndex + renderTargetArrayIndex,
            pDstSurface->arrayIndex + renderTargetArrayIndex, sampleNum, pDstSurface->lod, pDstSurface);

        const uint32_t dx = SIMD16_TILE_X_DIM * DST_BYTES_PER_PIXEL;
        const uint32_t dy = SIMD16_TILE_Y_DIM * DestRowWidthBytes - KNOB_TILE_X_DIM * DST_BYTES_PER_PIXEL;

        uint8_t* ppDsts[] =
        {
            pDst,                                           // row 0, col 0
            pDst + DestRowWidthBytes,                       // row 1, col 0
            pDst + dx / 2,                                  // row 0, col 1
            pDst + DestRowWidthBytes + dx / 2               // row 1, col 1
        };

        for (uint32_t yy = 0; yy < KNOB_TILE_Y_DIM; yy += SIMD16_TILE_Y_DIM)
        {
            for (uint32_t xx = 0; xx < KNOB_TILE_X_DIM; xx += SIMD16_TILE_X_DIM)
            {
                ConvertPixelsSOAtoAOS<SrcFormat, DstFormat>::Convert(pSrc, ppDsts);

                pSrc += KNOB_SIMD16_WIDTH * SRC_BYTES_PER_PIXEL;

                ppDsts[0] += dx;
                ppDsts[1] += dx;
                ppDsts[2] += dx;
                ppDsts[3] += dx;
            }

            ppDsts[0] += dy;
            ppDsts[1] += dy;
            ppDsts[2] += dy;
            ppDsts[3] += dy;
        }
    }
};

//////////////////////////////////////////////////////////////////////////
/// OptStoreRasterTile - TILE_MODE_YMAJOR specialization for 32bpp
//////////////////////////////////////////////////////////////////////////
template<SWR_FORMAT SrcFormat, SWR_FORMAT DstFormat>
struct OptStoreRasterTile< TilingTraits<SWR_TILE_MODE_YMAJOR, 32>, SrcFormat, DstFormat>
{
    typedef StoreRasterTile<TilingTraits<SWR_TILE_MODE_YMAJOR, 32>, SrcFormat, DstFormat> GenericStoreTile;
    static const size_t SRC_BYTES_PER_PIXEL = FormatTraits<SrcFormat>::bpp / 8;

    //////////////////////////////////////////////////////////////////////////
    /// @brief Stores an 8x8 raster tile to the destination surface.
    /// @param pSrc - Pointer to raster tile.
    /// @param pDstSurface - Destination surface state
    /// @param x, y - Coordinates to raster tile.
    INLINE static void Store(
        uint8_t *pSrc,
        SWR_SURFACE_STATE* pDstSurface,
        uint32_t x, uint32_t y, uint32_t sampleNum, uint32_t renderTargetArrayIndex)
    {
        static const uint32_t DestRowWidthBytes = 16;                    // 16B rows
        static const uint32_t DestColumnBytes = DestRowWidthBytes * 32;  // 16B x 32 rows.

        // Punt non-full tiles to generic store
        uint32_t lodWidth = std::max(pDstSurface->width >> pDstSurface->lod, 1U);
        uint32_t lodHeight = std::max(pDstSurface->height >> pDstSurface->lod, 1U);

        if (x + KNOB_TILE_X_DIM > lodWidth || y + KNOB_TILE_Y_DIM > lodHeight)
        {
            return GenericStoreTile::Store(pSrc, pDstSurface, x, y, sampleNum, renderTargetArrayIndex);
        }

        // TileY is a column-major tiling mode where each 4KB tile consist of 8 columns of 32 x 16B rows.
        // We can compute the offsets to each column within the raster tile once and increment from these.
        // There will be 4 8x2 simd tiles in an 8x8 raster tile.
        uint8_t *pDst = (uint8_t*)ComputeSurfaceAddress<false, false>(x, y, pDstSurface->arrayIndex + renderTargetArrayIndex,
            pDstSurface->arrayIndex + renderTargetArrayIndex, sampleNum, pDstSurface->lod, pDstSurface);

        // we have to break these large spans up, since ConvertPixelsSOAtoAOS() can only work on max 16B spans (a TileY limitation)
        const uint32_t dy = SIMD16_TILE_Y_DIM * DestRowWidthBytes;

        // The Hot Tile uses a row-major tiling mode and has a larger memory footprint. So we iterate in a row-major pattern.
        uint8_t *ppDsts[] =
        {
            pDst,                                           // row 0, col 0
            pDst + DestRowWidthBytes,                       // row 1, col 0
            pDst + DestColumnBytes,                         // row 0, col 1
            pDst + DestRowWidthBytes + DestColumnBytes      // row 1, col 1
        };

        for (uint32_t yy = 0; yy < KNOB_TILE_Y_DIM; yy += SIMD16_TILE_Y_DIM)
        {
            // Raster tile width is same as simd16 tile width
            static_assert(KNOB_TILE_X_DIM == SIMD16_TILE_X_DIM, "Invalid tile x dim");

            ConvertPixelsSOAtoAOS<SrcFormat, DstFormat>::Convert(pSrc, ppDsts);

            pSrc += KNOB_SIMD16_WIDTH * SRC_BYTES_PER_PIXEL;

            ppDsts[0] += dy;
            ppDsts[1] += dy;
            ppDsts[2] += dy;
            ppDsts[3] += dy;
        }
    }
};

//////////////////////////////////////////////////////////////////////////
/// OptStoreRasterTile - TILE_MODE_YMAJOR specialization for 64bpp
//////////////////////////////////////////////////////////////////////////
template<SWR_FORMAT SrcFormat, SWR_FORMAT DstFormat>
struct OptStoreRasterTile< TilingTraits<SWR_TILE_MODE_YMAJOR, 64>, SrcFormat, DstFormat>
{
    typedef StoreRasterTile<TilingTraits<SWR_TILE_MODE_YMAJOR, 64>, SrcFormat, DstFormat> GenericStoreTile;
    static const size_t SRC_BYTES_PER_PIXEL = FormatTraits<SrcFormat>::bpp / 8;

    //////////////////////////////////////////////////////////////////////////
    /// @brief Stores an 8x8 raster tile to the destination surface.
    /// @param pSrc - Pointer to raster tile.
    /// @param pDstSurface - Destination surface state
    /// @param x, y - Coordinates to raster tile.
    INLINE static void Store(
        uint8_t *pSrc,
        SWR_SURFACE_STATE* pDstSurface,
        uint32_t x, uint32_t y, uint32_t sampleNum, uint32_t renderTargetArrayIndex)
    {
        static const uint32_t DestRowWidthBytes = 16;                    // 16B rows
        static const uint32_t DestColumnBytes = DestRowWidthBytes * 32;  // 16B x 32 rows.

        // Punt non-full tiles to generic store
        uint32_t lodWidth = std::max(pDstSurface->width >> pDstSurface->lod, 1U);
        uint32_t lodHeight = std::max(pDstSurface->height >> pDstSurface->lod, 1U);

        if (x + KNOB_TILE_X_DIM > lodWidth || y + KNOB_TILE_Y_DIM > lodHeight)
        {
            return GenericStoreTile::Store(pSrc, pDstSurface, x, y, sampleNum, renderTargetArrayIndex);
        }

        // TileY is a column-major tiling mode where each 4KB tile consist of 8 columns of 32 x 16B rows.
        // We can compute the offsets to each column within the raster tile once and increment from these.
        // There will be 4 8x2 simd tiles in an 8x8 raster tile.
        uint8_t *pDst = (uint8_t*)ComputeSurfaceAddress<false, false>(x, y, pDstSurface->arrayIndex + renderTargetArrayIndex,
            pDstSurface->arrayIndex + renderTargetArrayIndex, sampleNum, pDstSurface->lod, pDstSurface);

        // we have to break these large spans up, since ConvertPixelsSOAtoAOS() can only work on max 16B spans (a TileY limitation)
        const uint32_t dy = SIMD16_TILE_Y_DIM * DestRowWidthBytes;

        // The Hot Tile uses a row-major tiling mode and has a larger memory footprint. So we iterate in a row-major pattern.
        uint8_t *ppDsts[] =
        {
            pDst,                                           // row 0, col 0
            pDst + DestRowWidthBytes,                       // row 1, col 0
            pDst + DestColumnBytes,                         // row 0, col 1
            pDst + DestRowWidthBytes + DestColumnBytes,     // row 1, col 1
            pDst + DestColumnBytes * 2,                     // row 0, col 2
            pDst + DestRowWidthBytes + DestColumnBytes * 2, // row 1, col 2
            pDst + DestColumnBytes * 3,                     // row 0, col 3
            pDst + DestRowWidthBytes + DestColumnBytes * 3  // row 1, col 3
        };

        for (uint32_t yy = 0; yy < KNOB_TILE_Y_DIM; yy += SIMD16_TILE_Y_DIM)
        {
            // Raster tile width is same as simd16 tile width
            static_assert(KNOB_TILE_X_DIM == SIMD16_TILE_X_DIM, "Invalid tile x dim");

            ConvertPixelsSOAtoAOS<SrcFormat, DstFormat>::Convert(pSrc, ppDsts);

            pSrc += KNOB_SIMD16_WIDTH * SRC_BYTES_PER_PIXEL;

            for (uint32_t i = 0; i < ARRAY_SIZE(ppDsts); i += 1)
            {
                ppDsts[i] += dy;
            }
        }
    }
};

//////////////////////////////////////////////////////////////////////////
/// OptStoreRasterTile - SWR_TILE_MODE_YMAJOR specialization for 128bpp
//////////////////////////////////////////////////////////////////////////
template<SWR_FORMAT SrcFormat, SWR_FORMAT DstFormat>
struct OptStoreRasterTile< TilingTraits<SWR_TILE_MODE_YMAJOR, 128>, SrcFormat, DstFormat>
{
    typedef StoreRasterTile<TilingTraits<SWR_TILE_MODE_YMAJOR, 128>, SrcFormat, DstFormat> GenericStoreTile;
    static const size_t SRC_BYTES_PER_PIXEL = FormatTraits<SrcFormat>::bpp / 8;

    //////////////////////////////////////////////////////////////////////////
    /// @brief Stores an 8x8 raster tile to the destination surface.
    /// @param pSrc - Pointer to raster tile.
    /// @param pDstSurface - Destination surface state
    /// @param x, y - Coordinates to raster tile.
    INLINE static void Store(
        uint8_t *pSrc,
        SWR_SURFACE_STATE* pDstSurface,
        uint32_t x, uint32_t y, uint32_t sampleNum, uint32_t renderTargetArrayIndex)
    {
        static const uint32_t DestRowWidthBytes = 16;                    // 16B rows
        static const uint32_t DestColumnBytes = DestRowWidthBytes * 32;  // 16B x 32 rows.

        // Punt non-full tiles to generic store
        uint32_t lodWidth = std::max(pDstSurface->width >> pDstSurface->lod, 1U);
        uint32_t lodHeight = std::max(pDstSurface->height >> pDstSurface->lod, 1U);

        if (x + KNOB_TILE_X_DIM > lodWidth || y + KNOB_TILE_Y_DIM > lodHeight)
        {
            return GenericStoreTile::Store(pSrc, pDstSurface, x, y, sampleNum, renderTargetArrayIndex);
        }

        // TileY is a column-major tiling mode where each 4KB tile consist of 8 columns of 32 x 16B rows.
        // We can compute the offsets to each column within the raster tile once and increment from these.
        // There will be 4 8x2 simd tiles in an 8x8 raster tile.
        uint8_t *pDst = (uint8_t*)ComputeSurfaceAddress<false, false>(x, y, pDstSurface->arrayIndex + renderTargetArrayIndex,
            pDstSurface->arrayIndex + renderTargetArrayIndex, sampleNum, pDstSurface->lod, pDstSurface);

        // we have to break these large spans up, since ConvertPixelsSOAtoAOS() can only work on max 16B spans (a TileY limitation)
        const uint32_t dy = SIMD16_TILE_Y_DIM * DestRowWidthBytes;

        // The Hot Tile uses a row-major tiling mode and has a larger memory footprint. So we iterate in a row-major pattern.
        uint8_t *ppDsts[] =
        {
            pDst,                                           // row 0, col 0
            pDst + DestRowWidthBytes,                       // row 1, col 0
            pDst + DestColumnBytes,                         // row 0, col 1
            pDst + DestRowWidthBytes + DestColumnBytes,     // row 1, col 1
            pDst + DestColumnBytes * 2,                     // row 0, col 2
            pDst + DestRowWidthBytes + DestColumnBytes * 2, // row 1, col 2
            pDst + DestColumnBytes * 3,                     // row 0, col 3
            pDst + DestRowWidthBytes + DestColumnBytes * 3, // row 1, col 3
            pDst + DestColumnBytes * 4,                     // row 0, col 4
            pDst + DestRowWidthBytes + DestColumnBytes * 4, // row 1, col 4
            pDst + DestColumnBytes * 5,                     // row 0, col 5
            pDst + DestRowWidthBytes + DestColumnBytes * 5, // row 1, col 5
            pDst + DestColumnBytes * 6,                     // row 0, col 6
            pDst + DestRowWidthBytes + DestColumnBytes * 6, // row 1, col 6
            pDst + DestColumnBytes * 7,                     // row 0, col 7
            pDst + DestRowWidthBytes + DestColumnBytes * 7  // row 1, col 7
        };

        for (uint32_t yy = 0; yy < KNOB_TILE_Y_DIM; yy += SIMD16_TILE_Y_DIM)
        {
            // Raster tile width is same as simd16 tile width
            static_assert(KNOB_TILE_X_DIM == SIMD16_TILE_X_DIM, "Invalid tile x dim");

            ConvertPixelsSOAtoAOS<SrcFormat, DstFormat>::Convert(pSrc, ppDsts);

            pSrc += KNOB_SIMD16_WIDTH * SRC_BYTES_PER_PIXEL;

            for (uint32_t i = 0; i < ARRAY_SIZE(ppDsts); i += 1)
            {
                ppDsts[i] += dy;
            }
        }
    }
};

//////////////////////////////////////////////////////////////////////////
/// StoreMacroTile - Stores a macro tile which consists of raster tiles.
//////////////////////////////////////////////////////////////////////////
template<typename TTraits, SWR_FORMAT SrcFormat, SWR_FORMAT DstFormat>
struct StoreMacroTile
{
    //////////////////////////////////////////////////////////////////////////
    /// @brief Stores a macrotile to the destination surface using safe implementation.
    /// @param pSrc - Pointer to macro tile.
    /// @param pDstSurface - Destination surface state
    /// @param x, y - Coordinates to macro tile
    static void StoreGeneric(
        uint8_t *pSrcHotTile,
        SWR_SURFACE_STATE* pDstSurface,
        uint32_t x, uint32_t y, uint32_t renderTargetArrayIndex)
    {
        PFN_STORE_TILES_INTERNAL pfnStore;
        pfnStore = StoreRasterTile<TTraits, SrcFormat, DstFormat>::Store;

        // Store each raster tile from the hot tile to the destination surface.
        for (uint32_t row = 0; row < KNOB_MACROTILE_Y_DIM; row += KNOB_TILE_Y_DIM)
        {
            for (uint32_t col = 0; col < KNOB_MACROTILE_X_DIM; col += KNOB_TILE_X_DIM)
            {
                for (uint32_t sampleNum = 0; sampleNum < pDstSurface->numSamples; sampleNum++)
                {
                    pfnStore(pSrcHotTile, pDstSurface, (x + col), (y + row), sampleNum, renderTargetArrayIndex);
                    pSrcHotTile += KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * (FormatTraits<SrcFormat>::bpp / 8);
                }
            }
        }

    }

    typedef void(*PFN_STORE_TILES_INTERNAL)(uint8_t*, SWR_SURFACE_STATE*, uint32_t, uint32_t, uint32_t, uint32_t);
    //////////////////////////////////////////////////////////////////////////
    /// @brief Stores a macrotile to the destination surface.
    /// @param pSrc - Pointer to macro tile.
    /// @param pDstSurface - Destination surface state
    /// @param x, y - Coordinates to macro tile
    static void Store(
        uint8_t *pSrcHotTile,
        SWR_SURFACE_STATE* pDstSurface,
        uint32_t x, uint32_t y, uint32_t renderTargetArrayIndex)
    {
        PFN_STORE_TILES_INTERNAL pfnStore[SWR_MAX_NUM_MULTISAMPLES];

        for (uint32_t sampleNum = 0; sampleNum < pDstSurface->numSamples; sampleNum++)
        {
            size_t dstSurfAddress = (size_t)ComputeSurfaceAddress<false, false>(
                0,
                0,
                pDstSurface->arrayIndex + renderTargetArrayIndex, // z for 3D surfaces
                pDstSurface->arrayIndex + renderTargetArrayIndex, // array index for 2D arrays
                sampleNum,
                pDstSurface->lod,
                pDstSurface);

            // Only support generic store-tile if lod surface doesn't start on a page boundary and is non-linear
            bool bForceGeneric = ((pDstSurface->tileMode != SWR_TILE_NONE) && (0 != (dstSurfAddress & 0xfff))) ||
                (pDstSurface->bInterleavedSamples);

            pfnStore[sampleNum] = (bForceGeneric || KNOB_USE_GENERIC_STORETILE) ? StoreRasterTile<TTraits, SrcFormat, DstFormat>::Store : OptStoreRasterTile<TTraits, SrcFormat, DstFormat>::Store;
        }

        // Save original for pSrcHotTile resolve.
        uint8_t *pResolveSrcHotTile = pSrcHotTile;

        // Store each raster tile from the hot tile to the destination surface.
        for(uint32_t row = 0; row < KNOB_MACROTILE_Y_DIM; row += KNOB_TILE_Y_DIM)
        {
            for(uint32_t col = 0; col < KNOB_MACROTILE_X_DIM; col += KNOB_TILE_X_DIM)
            {
                for(uint32_t sampleNum = 0; sampleNum < pDstSurface->numSamples; sampleNum++)
                {
                    pfnStore[sampleNum](pSrcHotTile, pDstSurface, (x + col), (y + row), sampleNum, renderTargetArrayIndex);
                    pSrcHotTile += KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * (FormatTraits<SrcFormat>::bpp / 8);
                }
            }
        }

        if (pDstSurface->xpAuxBaseAddress)
        {
            uint32_t sampleOffset = KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * (FormatTraits<SrcFormat>::bpp / 8);
            // Store each raster tile from the hot tile to the destination surface.
            for(uint32_t row = 0; row < KNOB_MACROTILE_Y_DIM; row += KNOB_TILE_Y_DIM)
            {
                for(uint32_t col = 0; col < KNOB_MACROTILE_X_DIM; col += KNOB_TILE_X_DIM)
                {
                    StoreRasterTile<TTraits, SrcFormat, DstFormat>::Resolve(pResolveSrcHotTile, pDstSurface, (x + col), (y + row), sampleOffset, renderTargetArrayIndex);
                    pResolveSrcHotTile += sampleOffset * pDstSurface->numSamples;
                }
            }
        }
    }
};

//////////////////////////////////////////////////////////////////////////
/// InitStoreTilesTable - Helper for setting up the tables.
template <SWR_TILE_MODE TTileMode, size_t NumTileModesT, size_t ArraySizeT>
void InitStoreTilesTableColor_Half1(
    PFN_STORE_TILES (&table)[NumTileModesT][ArraySizeT])
{
    table[TTileMode][R32G32B32A32_FLOAT]            = StoreMacroTile<TilingTraits<TTileMode, 128>, R32G32B32A32_FLOAT, R32G32B32A32_FLOAT>::Store;
    table[TTileMode][R32G32B32A32_SINT]             = StoreMacroTile<TilingTraits<TTileMode, 128>, R32G32B32A32_FLOAT, R32G32B32A32_SINT>::Store;
    table[TTileMode][R32G32B32A32_UINT]             = StoreMacroTile<TilingTraits<TTileMode, 128>, R32G32B32A32_FLOAT, R32G32B32A32_UINT>::Store;
    table[TTileMode][R32G32B32X32_FLOAT]            = StoreMacroTile<TilingTraits<TTileMode, 128>, R32G32B32A32_FLOAT, R32G32B32X32_FLOAT>::Store;
    table[TTileMode][R32G32B32A32_SSCALED]          = StoreMacroTile<TilingTraits<TTileMode, 128>, R32G32B32A32_FLOAT, R32G32B32A32_SSCALED>::Store;
    table[TTileMode][R32G32B32A32_USCALED]          = StoreMacroTile<TilingTraits<TTileMode, 128>, R32G32B32A32_FLOAT, R32G32B32A32_USCALED>::Store;
    table[TTileMode][R32G32B32_FLOAT]               = StoreMacroTile<TilingTraits<TTileMode, 96>, R32G32B32A32_FLOAT, R32G32B32_FLOAT>::Store;
    table[TTileMode][R32G32B32_SINT]                = StoreMacroTile<TilingTraits<TTileMode, 96>, R32G32B32A32_FLOAT, R32G32B32_SINT>::Store;
    table[TTileMode][R32G32B32_UINT]                = StoreMacroTile<TilingTraits<TTileMode, 96>, R32G32B32A32_FLOAT, R32G32B32_UINT>::Store;
    table[TTileMode][R32G32B32_SSCALED]             = StoreMacroTile<TilingTraits<TTileMode, 96>, R32G32B32A32_FLOAT, R32G32B32_SSCALED>::Store;
    table[TTileMode][R32G32B32_USCALED]             = StoreMacroTile<TilingTraits<TTileMode, 96>, R32G32B32A32_FLOAT, R32G32B32_USCALED>::Store;
    table[TTileMode][R16G16B16A16_UNORM]            = StoreMacroTile<TilingTraits<TTileMode, 64>, R32G32B32A32_FLOAT, R16G16B16A16_UNORM>::Store;
    table[TTileMode][R16G16B16A16_SNORM]            = StoreMacroTile<TilingTraits<TTileMode, 64>, R32G32B32A32_FLOAT, R16G16B16A16_SNORM>::Store;
    table[TTileMode][R16G16B16A16_SINT]             = StoreMacroTile<TilingTraits<TTileMode, 64>, R32G32B32A32_FLOAT, R16G16B16A16_SINT>::Store;
    table[TTileMode][R16G16B16A16_UINT]             = StoreMacroTile<TilingTraits<TTileMode, 64>, R32G32B32A32_FLOAT, R16G16B16A16_UINT>::Store;
    table[TTileMode][R16G16B16A16_FLOAT]            = StoreMacroTile<TilingTraits<TTileMode, 64>, R32G32B32A32_FLOAT, R16G16B16A16_FLOAT>::Store;
    table[TTileMode][R32G32_FLOAT]                  = StoreMacroTile<TilingTraits<TTileMode, 64>, R32G32B32A32_FLOAT, R32G32_FLOAT>::Store;
    table[TTileMode][R32G32_SINT]                   = StoreMacroTile<TilingTraits<TTileMode, 64>, R32G32B32A32_FLOAT, R32G32_SINT>::Store;
    table[TTileMode][R32G32_UINT]                   = StoreMacroTile<TilingTraits<TTileMode, 64>, R32G32B32A32_FLOAT, R32G32_UINT>::Store;
    table[TTileMode][R32_FLOAT_X8X24_TYPELESS]      = StoreMacroTile<TilingTraits<TTileMode, 64>, R32G32B32A32_FLOAT, R32_FLOAT_X8X24_TYPELESS>::Store;
    table[TTileMode][X32_TYPELESS_G8X24_UINT]       = StoreMacroTile<TilingTraits<TTileMode, 64>, R32G32B32A32_FLOAT, X32_TYPELESS_G8X24_UINT>::Store;
    table[TTileMode][R16G16B16X16_UNORM]            = StoreMacroTile<TilingTraits<TTileMode, 64>, R32G32B32A32_FLOAT, R16G16B16X16_UNORM>::Store;
    table[TTileMode][R16G16B16X16_FLOAT]            = StoreMacroTile<TilingTraits<TTileMode, 64>, R32G32B32A32_FLOAT, R16G16B16X16_FLOAT>::Store;
    table[TTileMode][R16G16B16A16_SSCALED]          = StoreMacroTile<TilingTraits<TTileMode, 64>, R32G32B32A32_FLOAT, R16G16B16A16_SSCALED>::Store;
    table[TTileMode][R16G16B16A16_USCALED]          = StoreMacroTile<TilingTraits<TTileMode, 64>, R32G32B32A32_FLOAT, R16G16B16A16_USCALED>::Store;
    table[TTileMode][R32G32_SSCALED]                = StoreMacroTile<TilingTraits<TTileMode, 64>, R32G32B32A32_FLOAT, R32G32_SSCALED>::Store;
    table[TTileMode][R32G32_USCALED]                = StoreMacroTile<TilingTraits<TTileMode, 64>, R32G32B32A32_FLOAT, R32G32_USCALED>::Store;
    table[TTileMode][B8G8R8A8_UNORM]                = StoreMacroTile<TilingTraits<TTileMode, 32>, R32G32B32A32_FLOAT, B8G8R8A8_UNORM>::Store;
    table[TTileMode][B8G8R8A8_UNORM_SRGB]           = StoreMacroTile<TilingTraits<TTileMode, 32>, R32G32B32A32_FLOAT, B8G8R8A8_UNORM_SRGB>::Store;
    table[TTileMode][R10G10B10A2_UNORM]             = StoreMacroTile<TilingTraits<TTileMode, 32>, R32G32B32A32_FLOAT, R10G10B10A2_UNORM>::StoreGeneric;
    table[TTileMode][R10G10B10A2_UNORM_SRGB]        = StoreMacroTile<TilingTraits<TTileMode, 32>, R32G32B32A32_FLOAT, R10G10B10A2_UNORM_SRGB>::StoreGeneric;
    table[TTileMode][R10G10B10A2_UINT]              = StoreMacroTile<TilingTraits<TTileMode, 32>, R32G32B32A32_FLOAT, R10G10B10A2_UINT>::StoreGeneric;
    table[TTileMode][R8G8B8A8_UNORM]                = StoreMacroTile<TilingTraits<TTileMode, 32>, R32G32B32A32_FLOAT, R8G8B8A8_UNORM>::Store;
    table[TTileMode][R8G8B8A8_UNORM_SRGB]           = StoreMacroTile<TilingTraits<TTileMode, 32>, R32G32B32A32_FLOAT, R8G8B8A8_UNORM_SRGB>::Store;
    table[TTileMode][R8G8B8A8_SNORM]                = StoreMacroTile<TilingTraits<TTileMode, 32>, R32G32B32A32_FLOAT, R8G8B8A8_SNORM>::Store;
    table[TTileMode][R8G8B8A8_SINT]                 = StoreMacroTile<TilingTraits<TTileMode, 32>, R32G32B32A32_FLOAT, R8G8B8A8_SINT>::Store;
    table[TTileMode][R8G8B8A8_UINT]                 = StoreMacroTile<TilingTraits<TTileMode, 32>, R32G32B32A32_FLOAT, R8G8B8A8_UINT>::Store;
    table[TTileMode][R16G16_UNORM]                  = StoreMacroTile<TilingTraits<TTileMode, 32>, R32G32B32A32_FLOAT, R16G16_UNORM>::Store;
    table[TTileMode][R16G16_SNORM]                  = StoreMacroTile<TilingTraits<TTileMode, 32>, R32G32B32A32_FLOAT, R16G16_SNORM>::Store;
    table[TTileMode][R16G16_SINT]                   = StoreMacroTile<TilingTraits<TTileMode, 32>, R32G32B32A32_FLOAT, R16G16_SINT>::Store;
    table[TTileMode][R16G16_UINT]                   = StoreMacroTile<TilingTraits<TTileMode, 32>, R32G32B32A32_FLOAT, R16G16_UINT>::Store;
    table[TTileMode][R16G16_FLOAT]                  = StoreMacroTile<TilingTraits<TTileMode, 32>, R32G32B32A32_FLOAT, R16G16_FLOAT>::Store;
    table[TTileMode][B10G10R10A2_UNORM]             = StoreMacroTile<TilingTraits<TTileMode, 32>, R32G32B32A32_FLOAT, B10G10R10A2_UNORM>::StoreGeneric;
    table[TTileMode][B10G10R10A2_UNORM_SRGB]        = StoreMacroTile<TilingTraits<TTileMode, 32>, R32G32B32A32_FLOAT, B10G10R10A2_UNORM_SRGB>::StoreGeneric;
    table[TTileMode][R11G11B10_FLOAT]               = StoreMacroTile<TilingTraits<TTileMode, 32>, R32G32B32A32_FLOAT, R11G11B10_FLOAT>::StoreGeneric;
    table[TTileMode][R10G10B10_FLOAT_A2_UNORM]      = StoreMacroTile<TilingTraits<TTileMode, 32>, R32G32B32A32_FLOAT, R10G10B10_FLOAT_A2_UNORM>::StoreGeneric;
    table[TTileMode][R32_SINT]                      = StoreMacroTile<TilingTraits<TTileMode, 32>, R32G32B32A32_FLOAT, R32_SINT>::Store;
    table[TTileMode][R32_UINT]                      = StoreMacroTile<TilingTraits<TTileMode, 32>, R32G32B32A32_FLOAT, R32_UINT>::Store;
    table[TTileMode][R32_FLOAT]                     = StoreMacroTile<TilingTraits<TTileMode, 32>, R32G32B32A32_FLOAT, R32_FLOAT>::Store;
    table[TTileMode][R24_UNORM_X8_TYPELESS]         = StoreMacroTile<TilingTraits<TTileMode, 32>, R32G32B32A32_FLOAT, R24_UNORM_X8_TYPELESS>::StoreGeneric;
    table[TTileMode][X24_TYPELESS_G8_UINT]          = StoreMacroTile<TilingTraits<TTileMode, 32>, R32G32B32A32_FLOAT, X24_TYPELESS_G8_UINT>::StoreGeneric;
    table[TTileMode][A32_FLOAT]                     = StoreMacroTile<TilingTraits<TTileMode, 32>, R32G32B32A32_FLOAT, A32_FLOAT>::Store;
    table[TTileMode][B8G8R8X8_UNORM]                = StoreMacroTile<TilingTraits<TTileMode, 32>, R32G32B32A32_FLOAT, B8G8R8X8_UNORM>::Store;
    table[TTileMode][B8G8R8X8_UNORM_SRGB]           = StoreMacroTile<TilingTraits<TTileMode, 32>, R32G32B32A32_FLOAT, B8G8R8X8_UNORM_SRGB>::Store;
    table[TTileMode][R8G8B8X8_UNORM]                = StoreMacroTile<TilingTraits<TTileMode, 32>, R32G32B32A32_FLOAT, R8G8B8X8_UNORM>::Store;
    table[TTileMode][R8G8B8X8_UNORM_SRGB]           = StoreMacroTile<TilingTraits<TTileMode, 32>, R32G32B32A32_FLOAT, R8G8B8X8_UNORM_SRGB>::Store;
}

template <SWR_TILE_MODE TTileMode, size_t NumTileModesT, size_t ArraySizeT>
void InitStoreTilesTableColor_Half2(
    PFN_STORE_TILES(&table)[NumTileModesT][ArraySizeT])
{
    table[TTileMode][R9G9B9E5_SHAREDEXP]            = StoreMacroTile<TilingTraits<TTileMode, 32>, R32G32B32A32_FLOAT, R9G9B9E5_SHAREDEXP>::StoreGeneric;
    table[TTileMode][B10G10R10X2_UNORM]             = StoreMacroTile<TilingTraits<TTileMode, 32>, R32G32B32A32_FLOAT, B10G10R10X2_UNORM>::StoreGeneric;
    table[TTileMode][R10G10B10X2_USCALED]           = StoreMacroTile<TilingTraits<TTileMode, 32>, R32G32B32A32_FLOAT, R10G10B10X2_USCALED>::StoreGeneric;
    table[TTileMode][R8G8B8A8_SSCALED]              = StoreMacroTile<TilingTraits<TTileMode, 32>, R32G32B32A32_FLOAT, R8G8B8A8_SSCALED>::Store;
    table[TTileMode][R8G8B8A8_USCALED]              = StoreMacroTile<TilingTraits<TTileMode, 32>, R32G32B32A32_FLOAT, R8G8B8A8_USCALED>::Store;
    table[TTileMode][R16G16_SSCALED]                = StoreMacroTile<TilingTraits<TTileMode, 32>, R32G32B32A32_FLOAT, R16G16_SSCALED>::Store;
    table[TTileMode][R16G16_USCALED]                = StoreMacroTile<TilingTraits<TTileMode, 32>, R32G32B32A32_FLOAT, R16G16_USCALED>::Store;
    table[TTileMode][R32_SSCALED]                   = StoreMacroTile<TilingTraits<TTileMode, 32>, R32G32B32A32_FLOAT, R32_SSCALED>::Store;
    table[TTileMode][R32_USCALED]                   = StoreMacroTile<TilingTraits<TTileMode, 32>, R32G32B32A32_FLOAT, R32_USCALED>::Store;
    table[TTileMode][B5G6R5_UNORM]                  = StoreMacroTile<TilingTraits<TTileMode, 16>, R32G32B32A32_FLOAT, B5G6R5_UNORM>::Store;
    table[TTileMode][B5G6R5_UNORM_SRGB]             = StoreMacroTile<TilingTraits<TTileMode, 16>, R32G32B32A32_FLOAT, B5G6R5_UNORM_SRGB>::StoreGeneric;
    table[TTileMode][B5G5R5A1_UNORM]                = StoreMacroTile<TilingTraits<TTileMode, 16>, R32G32B32A32_FLOAT, B5G5R5A1_UNORM>::StoreGeneric;
    table[TTileMode][B5G5R5A1_UNORM_SRGB]           = StoreMacroTile<TilingTraits<TTileMode, 16>, R32G32B32A32_FLOAT, B5G5R5A1_UNORM_SRGB>::StoreGeneric;
    table[TTileMode][B4G4R4A4_UNORM]                = StoreMacroTile<TilingTraits<TTileMode, 16>, R32G32B32A32_FLOAT, B4G4R4A4_UNORM>::StoreGeneric;
    table[TTileMode][B4G4R4A4_UNORM_SRGB]           = StoreMacroTile<TilingTraits<TTileMode, 16>, R32G32B32A32_FLOAT, B4G4R4A4_UNORM_SRGB>::StoreGeneric;
    table[TTileMode][R8G8_UNORM]                    = StoreMacroTile<TilingTraits<TTileMode, 16>, R32G32B32A32_FLOAT, R8G8_UNORM>::Store;
    table[TTileMode][R8G8_SNORM]                    = StoreMacroTile<TilingTraits<TTileMode, 16>, R32G32B32A32_FLOAT, R8G8_SNORM>::Store;
    table[TTileMode][R8G8_SINT]                     = StoreMacroTile<TilingTraits<TTileMode, 16>, R32G32B32A32_FLOAT, R8G8_SINT>::Store;
    table[TTileMode][R8G8_UINT]                     = StoreMacroTile<TilingTraits<TTileMode, 16>, R32G32B32A32_FLOAT, R8G8_UINT>::Store;
    table[TTileMode][R16_UNORM]                     = StoreMacroTile<TilingTraits<TTileMode, 16>, R32G32B32A32_FLOAT, R16_UNORM>::Store;
    table[TTileMode][R16_SNORM]                     = StoreMacroTile<TilingTraits<TTileMode, 16>, R32G32B32A32_FLOAT, R16_SNORM>::Store;
    table[TTileMode][R16_SINT]                      = StoreMacroTile<TilingTraits<TTileMode, 16>, R32G32B32A32_FLOAT, R16_SINT>::Store;
    table[TTileMode][R16_UINT]                      = StoreMacroTile<TilingTraits<TTileMode, 16>, R32G32B32A32_FLOAT, R16_UINT>::Store;
    table[TTileMode][R16_FLOAT]                     = StoreMacroTile<TilingTraits<TTileMode, 16>, R32G32B32A32_FLOAT, R16_FLOAT>::Store;
    table[TTileMode][A16_UNORM]                     = StoreMacroTile<TilingTraits<TTileMode, 16>, R32G32B32A32_FLOAT, A16_UNORM>::Store;
    table[TTileMode][A16_FLOAT]                     = StoreMacroTile<TilingTraits<TTileMode, 16>, R32G32B32A32_FLOAT, A16_FLOAT>::Store;
    table[TTileMode][B5G5R5X1_UNORM]                = StoreMacroTile<TilingTraits<TTileMode, 16>, R32G32B32A32_FLOAT, B5G5R5X1_UNORM>::StoreGeneric;
    table[TTileMode][B5G5R5X1_UNORM_SRGB]           = StoreMacroTile<TilingTraits<TTileMode, 16>, R32G32B32A32_FLOAT, B5G5R5X1_UNORM_SRGB>::StoreGeneric;
    table[TTileMode][R8G8_SSCALED]                  = StoreMacroTile<TilingTraits<TTileMode, 16>, R32G32B32A32_FLOAT, R8G8_SSCALED>::Store;
    table[TTileMode][R8G8_USCALED]                  = StoreMacroTile<TilingTraits<TTileMode, 16>, R32G32B32A32_FLOAT, R8G8_USCALED>::Store;
    table[TTileMode][R16_SSCALED]                   = StoreMacroTile<TilingTraits<TTileMode, 16>, R32G32B32A32_FLOAT, R16_SSCALED>::Store;
    table[TTileMode][R16_USCALED]                   = StoreMacroTile<TilingTraits<TTileMode, 16>, R32G32B32A32_FLOAT, R16_USCALED>::Store;
    table[TTileMode][A1B5G5R5_UNORM]                = StoreMacroTile<TilingTraits<TTileMode, 16>, R32G32B32A32_FLOAT, A1B5G5R5_UNORM>::StoreGeneric;
    table[TTileMode][A4B4G4R4_UNORM]                = StoreMacroTile<TilingTraits<TTileMode, 16>, R32G32B32A32_FLOAT, A4B4G4R4_UNORM>::StoreGeneric;
    table[TTileMode][R8_UNORM]                      = StoreMacroTile<TilingTraits<TTileMode, 8>, R32G32B32A32_FLOAT, R8_UNORM>::Store;
    table[TTileMode][R8_SNORM]                      = StoreMacroTile<TilingTraits<TTileMode, 8>, R32G32B32A32_FLOAT, R8_SNORM>::Store;
    table[TTileMode][R8_SINT]                       = StoreMacroTile<TilingTraits<TTileMode, 8>, R32G32B32A32_FLOAT, R8_SINT>::Store;
    table[TTileMode][R8_UINT]                       = StoreMacroTile<TilingTraits<TTileMode, 8>, R32G32B32A32_FLOAT, R8_UINT>::Store;
    table[TTileMode][A8_UNORM]                      = StoreMacroTile<TilingTraits<TTileMode, 8>, R32G32B32A32_FLOAT, A8_UNORM>::Store;
    table[TTileMode][R8_SSCALED]                    = StoreMacroTile<TilingTraits<TTileMode, 8>, R32G32B32A32_FLOAT, R8_SSCALED>::Store;
    table[TTileMode][R8_USCALED]                    = StoreMacroTile<TilingTraits<TTileMode, 8>, R32G32B32A32_FLOAT, R8_USCALED>::Store;
    table[TTileMode][R8G8B8_UNORM]                  = StoreMacroTile<TilingTraits<TTileMode, 24>, R32G32B32A32_FLOAT, R8G8B8_UNORM>::Store;
    table[TTileMode][R8G8B8_SNORM]                  = StoreMacroTile<TilingTraits<TTileMode, 24>, R32G32B32A32_FLOAT, R8G8B8_SNORM>::Store;
    table[TTileMode][R8G8B8_SSCALED]                = StoreMacroTile<TilingTraits<TTileMode, 24>, R32G32B32A32_FLOAT, R8G8B8_SSCALED>::Store;
    table[TTileMode][R8G8B8_USCALED]                = StoreMacroTile<TilingTraits<TTileMode, 24>, R32G32B32A32_FLOAT, R8G8B8_USCALED>::Store;
    table[TTileMode][R16G16B16_FLOAT]               = StoreMacroTile<TilingTraits<TTileMode, 48>, R32G32B32A32_FLOAT, R16G16B16_FLOAT>::Store;
    table[TTileMode][R16G16B16_UNORM]               = StoreMacroTile<TilingTraits<TTileMode, 48>, R32G32B32A32_FLOAT, R16G16B16_UNORM>::Store;
    table[TTileMode][R16G16B16_SNORM]               = StoreMacroTile<TilingTraits<TTileMode, 48>, R32G32B32A32_FLOAT, R16G16B16_SNORM>::Store;
    table[TTileMode][R16G16B16_SSCALED]             = StoreMacroTile<TilingTraits<TTileMode, 48>, R32G32B32A32_FLOAT, R16G16B16_SSCALED>::Store;
    table[TTileMode][R16G16B16_USCALED]             = StoreMacroTile<TilingTraits<TTileMode, 48>, R32G32B32A32_FLOAT, R16G16B16_USCALED>::Store;
    table[TTileMode][R8G8B8_UNORM_SRGB]             = StoreMacroTile<TilingTraits<TTileMode, 24>, R32G32B32A32_FLOAT, R8G8B8_UNORM_SRGB>::Store;
    table[TTileMode][R16G16B16_UINT]                = StoreMacroTile<TilingTraits<TTileMode, 48>, R32G32B32A32_FLOAT, R16G16B16_UINT>::Store;
    table[TTileMode][R16G16B16_SINT]                = StoreMacroTile<TilingTraits<TTileMode, 48>, R32G32B32A32_FLOAT, R16G16B16_SINT>::Store;
    table[TTileMode][R10G10B10A2_SNORM]             = StoreMacroTile<TilingTraits<TTileMode, 32>, R32G32B32A32_FLOAT, R10G10B10A2_SNORM>::StoreGeneric;
    table[TTileMode][R10G10B10A2_USCALED]           = StoreMacroTile<TilingTraits<TTileMode, 32>, R32G32B32A32_FLOAT, R10G10B10A2_USCALED>::StoreGeneric;
    table[TTileMode][R10G10B10A2_SSCALED]           = StoreMacroTile<TilingTraits<TTileMode, 32>, R32G32B32A32_FLOAT, R10G10B10A2_SSCALED>::StoreGeneric;
    table[TTileMode][R10G10B10A2_SINT]              = StoreMacroTile<TilingTraits<TTileMode, 32>, R32G32B32A32_FLOAT, R10G10B10A2_SINT>::StoreGeneric;
    table[TTileMode][B10G10R10A2_SNORM]             = StoreMacroTile<TilingTraits<TTileMode, 32>, R32G32B32A32_FLOAT, B10G10R10A2_SNORM>::StoreGeneric;
    table[TTileMode][B10G10R10A2_USCALED]           = StoreMacroTile<TilingTraits<TTileMode, 32>, R32G32B32A32_FLOAT, B10G10R10A2_USCALED>::StoreGeneric;
    table[TTileMode][B10G10R10A2_SSCALED]           = StoreMacroTile<TilingTraits<TTileMode, 32>, R32G32B32A32_FLOAT, B10G10R10A2_SSCALED>::StoreGeneric;
    table[TTileMode][B10G10R10A2_UINT]              = StoreMacroTile<TilingTraits<TTileMode, 32>, R32G32B32A32_FLOAT, B10G10R10A2_UINT>::StoreGeneric;
    table[TTileMode][B10G10R10A2_SINT]              = StoreMacroTile<TilingTraits<TTileMode, 32>, R32G32B32A32_FLOAT, B10G10R10A2_SINT>::StoreGeneric;
    table[TTileMode][R8G8B8_UINT]                   = StoreMacroTile<TilingTraits<TTileMode, 24>, R32G32B32A32_FLOAT, R8G8B8_UINT>::Store;
    table[TTileMode][R8G8B8_SINT]                   = StoreMacroTile<TilingTraits<TTileMode, 24>, R32G32B32A32_FLOAT, R8G8B8_SINT>::Store;
}

//////////////////////////////////////////////////////////////////////////
/// INIT_STORE_TILES_TABLE - Helper macro for setting up the tables.
template <SWR_TILE_MODE TTileMode, size_t NumTileModes, size_t ArraySizeT>
void InitStoreTilesTableDepth(
    PFN_STORE_TILES(&table)[NumTileModes][ArraySizeT])
{
   table[TTileMode][R32_FLOAT]                      = StoreMacroTile<TilingTraits<TTileMode, 32>, R32_FLOAT, R32_FLOAT>::Store;
   table[TTileMode][R32_FLOAT_X8X24_TYPELESS]       = StoreMacroTile<TilingTraits<TTileMode, 64>, R32_FLOAT, R32_FLOAT_X8X24_TYPELESS>::Store;
   table[TTileMode][R24_UNORM_X8_TYPELESS]          = StoreMacroTile<TilingTraits<TTileMode, 32>, R32_FLOAT, R24_UNORM_X8_TYPELESS>::Store;
   table[TTileMode][R16_UNORM]                      = StoreMacroTile<TilingTraits<TTileMode, 16>, R32_FLOAT, R16_UNORM>::Store;
}

template <SWR_TILE_MODE TTileMode, size_t NumTileModes, size_t ArraySizeT>
void InitStoreTilesTableStencil(
    PFN_STORE_TILES(&table)[NumTileModes][ArraySizeT])
{
    table[TTileMode][R8_UINT]                       = StoreMacroTile<TilingTraits<TTileMode, 8>, R8_UINT, R8_UINT>::Store;
}


//////////////////////////////////////////////////////////////////////////
/// @brief Deswizzles and stores a full hottile to a render surface
/// @param hPrivateContext - Handle to private DC
/// @param srcFormat - Format for hot tile.
/// @param renderTargetIndex - Index to destination render target
/// @param x, y - Coordinates to raster tile.
/// @param pSrcHotTile - Pointer to Hot Tile
void SwrStoreHotTileToSurface(
        HANDLE hWorkerPrivateData,
        SWR_SURFACE_STATE *pDstSurface,
	 BucketManager* pBucketMgr,
        SWR_FORMAT srcFormat,
        SWR_RENDERTARGET_ATTACHMENT renderTargetIndex,
        uint32_t x, uint32_t y, uint32_t renderTargetArrayIndex,
        uint8_t *pSrcHotTile);
