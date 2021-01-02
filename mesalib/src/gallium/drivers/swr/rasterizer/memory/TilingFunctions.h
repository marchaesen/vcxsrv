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
* @file TilingFunctions.h
* 
* @brief Tiling functions.
* 
******************************************************************************/
#pragma once

#include "core/state.h"
#include "core/format_traits.h"
#include "memory/tilingtraits.h"
#include "memory/SurfaceState.h"

#include <algorithm>

#define MAX_NUM_LOD 15

#define GFX_ALIGN(x, a) (((x) + ((a) - 1)) - (((x) + ((a) - 1)) & ((a) - 1))) // Alt implementation with bitwise not (~) has issue with uint32 align used with 64-bit value, since ~'ed value will remain 32-bit.

//////////////////////////////////////////////////////////////////////////
/// SimdTile SSE(2x2), AVX(4x2), or AVX-512(4x4?)
//////////////////////////////////////////////////////////////////////////
template<SWR_FORMAT HotTileFormat, SWR_FORMAT SrcOrDstFormat>
struct SimdTile
{
    // SimdTile is SOA (e.g. rrrrrrrr gggggggg bbbbbbbb aaaaaaaa )
    float color[FormatTraits<HotTileFormat>::numComps][KNOB_SIMD_WIDTH];

    //////////////////////////////////////////////////////////////////////////
    /// @brief Retrieve color from simd.
    /// @param index - linear index to color within simd.
    /// @param outputColor - output color
    INLINE void GetSwizzledColor(
        uint32_t index,
        float outputColor[4])
    {
        // SOA pattern for 2x2 is a subset of 4x2.
        //   0 1 4 5
        //   2 3 6 7
        // The offset converts pattern to linear
#if (SIMD_TILE_X_DIM == 4)
        static const uint32_t offset[] = { 0, 1, 4, 5, 2, 3, 6, 7 };
#elif (SIMD_TILE_X_DIM == 2)
        static const uint32_t offset[] = { 0, 1, 2, 3 };
#endif

        for (uint32_t i = 0; i < FormatTraits<SrcOrDstFormat>::numComps; ++i)
        {
            outputColor[i] = this->color[FormatTraits<SrcOrDstFormat>::swizzle(i)][offset[index]];
        }
    }

    //////////////////////////////////////////////////////////////////////////
    /// @brief Retrieve color from simd.
    /// @param index - linear index to color within simd.
    /// @param outputColor - output color
    INLINE void SetSwizzledColor(
        uint32_t index,
        const float src[4])
    {
        // SOA pattern for 2x2 is a subset of 4x2.
        //   0 1 4 5
        //   2 3 6 7
        // The offset converts pattern to linear
#if (SIMD_TILE_X_DIM == 4)
        static const uint32_t offset[] = { 0, 1, 4, 5, 2, 3, 6, 7 };
#elif (SIMD_TILE_X_DIM == 2)
        static const uint32_t offset[] = { 0, 1, 2, 3 };
#endif

        // Only loop over the components needed for destination.
        for (uint32_t i = 0; i < FormatTraits<SrcOrDstFormat>::numComps; ++i)
        {
            this->color[i][offset[index]] = src[i];
        }
    }
};

template<>
struct SimdTile <R8_UINT,R8_UINT>
{
    // SimdTile is SOA (e.g. rrrrrrrr gggggggg bbbbbbbb aaaaaaaa )
    uint8_t color[FormatTraits<R8_UINT>::numComps][KNOB_SIMD_WIDTH];

    //////////////////////////////////////////////////////////////////////////
    /// @brief Retrieve color from simd.
    /// @param index - linear index to color within simd.
    /// @param outputColor - output color
    INLINE void GetSwizzledColor(
        uint32_t index,
        float outputColor[4])
    {
        // SOA pattern for 2x2 is a subset of 4x2.
        //   0 1 4 5
        //   2 3 6 7
        // The offset converts pattern to linear
#if (SIMD_TILE_X_DIM == 4)
        static const uint32_t offset[] = { 0, 1, 4, 5, 2, 3, 6, 7 };
#elif (SIMD_TILE_X_DIM == 2)
        static const uint32_t offset[] = { 0, 1, 2, 3 };
#endif

        for (uint32_t i = 0; i < FormatTraits<R8_UINT>::numComps; ++i)
        {
            uint32_t src = this->color[FormatTraits<R8_UINT>::swizzle(i)][offset[index]];
            outputColor[i] = *(float*)&src;
        }
    }

    //////////////////////////////////////////////////////////////////////////
    /// @brief Retrieve color from simd.
    /// @param index - linear index to color within simd.
    /// @param outputColor - output color
    INLINE void SetSwizzledColor(
        uint32_t index,
        const float src[4])
    {
        // SOA pattern for 2x2 is a subset of 4x2.
        //   0 1 4 5
        //   2 3 6 7
        // The offset converts pattern to linear
#if (SIMD_TILE_X_DIM == 4)
        static const uint32_t offset[] = { 0, 1, 4, 5, 2, 3, 6, 7 };
#elif (SIMD_TILE_X_DIM == 2)
        static const uint32_t offset[] = { 0, 1, 2, 3 };
#endif

        // Only loop over the components needed for destination.
        for (uint32_t i = 0; i < FormatTraits<R8_UINT>::numComps; ++i)
        {
            this->color[i][offset[index]] = *(uint8_t*)&src[i];
        }
    }
};

//////////////////////////////////////////////////////////////////////////
/// SimdTile 8x2 for AVX-512
//////////////////////////////////////////////////////////////////////////

template<SWR_FORMAT HotTileFormat, SWR_FORMAT SrcOrDstFormat>
struct SimdTile_16
{
    // SimdTile is SOA (e.g. rrrrrrrrrrrrrrrr gggggggggggggggg bbbbbbbbbbbbbbbb aaaaaaaaaaaaaaaa )
    float color[FormatTraits<HotTileFormat>::numComps][KNOB_SIMD16_WIDTH];

    //////////////////////////////////////////////////////////////////////////
    /// @brief Retrieve color from simd.
    /// @param index - linear index to color within simd.
    /// @param outputColor - output color
    INLINE void GetSwizzledColor(
        uint32_t index,
        float outputColor[4])
    {
        // SOA pattern for 8x2..
        //   0 1 4 5 8 9 C D
        //   2 3 6 7 A B E F
        // The offset converts pattern to linear
        static const uint32_t offset[KNOB_SIMD16_WIDTH] = { 0, 1, 4, 5, 8, 9, 12, 13, 2, 3, 6, 7, 10, 11, 14, 15 };

        for (uint32_t i = 0; i < FormatTraits<SrcOrDstFormat>::numComps; ++i)
        {
            outputColor[i] = this->color[FormatTraits<SrcOrDstFormat>::swizzle(i)][offset[index]];
        }
    }

    //////////////////////////////////////////////////////////////////////////
    /// @brief Retrieve color from simd.
    /// @param index - linear index to color within simd.
    /// @param outputColor - output color
    INLINE void SetSwizzledColor(
        uint32_t index,
        const float src[4])
    {
        // SOA pattern for 8x2..
        //   0 1 4 5 8 9 C D
        //   2 3 6 7 A B E F
        // The offset converts pattern to linear
        static const uint32_t offset[KNOB_SIMD16_WIDTH] = { 0, 1, 4, 5, 8, 9, 12, 13, 2, 3, 6, 7, 10, 11, 14, 15 };

        for (uint32_t i = 0; i < FormatTraits<SrcOrDstFormat>::numComps; ++i)
        {
            this->color[i][offset[index]] = src[i];
        }
    }
};

template<>
struct SimdTile_16 <R8_UINT, R8_UINT>
{
    // SimdTile is SOA (e.g. rrrrrrrrrrrrrrrr gggggggggggggggg bbbbbbbbbbbbbbbb aaaaaaaaaaaaaaaa )
    uint8_t color[FormatTraits<R8_UINT>::numComps][KNOB_SIMD16_WIDTH];

    //////////////////////////////////////////////////////////////////////////
    /// @brief Retrieve color from simd.
    /// @param index - linear index to color within simd.
    /// @param outputColor - output color
    INLINE void GetSwizzledColor(
        uint32_t index,
        float outputColor[4])
    {
        // SOA pattern for 8x2..
        //   0 1 4 5 8 9 C D
        //   2 3 6 7 A B E F
        // The offset converts pattern to linear
        static const uint32_t offset[KNOB_SIMD16_WIDTH] = { 0, 1, 4, 5, 8, 9, 12, 13, 2, 3, 6, 7, 10, 11, 14, 15 };

        for (uint32_t i = 0; i < FormatTraits<R8_UINT>::numComps; ++i)
        {
            uint32_t src = this->color[FormatTraits<R8_UINT>::swizzle(i)][offset[index]];
            outputColor[i] = *(float*)&src;
        }
    }

    //////////////////////////////////////////////////////////////////////////
    /// @brief Retrieve color from simd.
    /// @param index - linear index to color within simd.
    /// @param outputColor - output color
    INLINE void SetSwizzledColor(
        uint32_t index,
        const float src[4])
    {
        // SOA pattern for 8x2..
        //   0 1 4 5 8 9 C D
        //   2 3 6 7 A B E F
        // The offset converts pattern to linear
        static const uint32_t offset[KNOB_SIMD16_WIDTH] = { 0, 1, 4, 5, 8, 9, 12, 13, 2, 3, 6, 7, 10, 11, 14, 15 };

        for (uint32_t i = 0; i < FormatTraits<R8_UINT>::numComps; ++i)
        {
            this->color[i][offset[index]] = *(uint8_t*)&src[i];
        }
    }
};

//////////////////////////////////////////////////////////////////////////
/// @brief Computes lod offset for 1D surface at specified lod.
/// @param baseWidth - width of basemip (mip 0).
/// @param hAlign - horizontal alignment per miip, in texels
/// @param lod - lod index
/// @param offset - output offset.
INLINE void ComputeLODOffset1D(
    const SWR_FORMAT_INFO& info,
    uint32_t baseWidth,
    uint32_t hAlign,
    uint32_t lod,
    uint32_t &offset)
{
    if (lod == 0)
    {
        offset = 0;
    }
    else
    {
        uint32_t curWidth = baseWidth;
        // @note hAlign is already in blocks for compressed formats so upconvert
        //       so that we have the desired alignment post-divide.
        if (info.isBC)
        {
            hAlign *= info.bcWidth;
        }

        offset = GFX_ALIGN(curWidth, hAlign);
        for (uint32_t l = 1; l < lod; ++l)
        {
            curWidth = std::max<uint32_t>(curWidth >> 1, 1U);
            offset += GFX_ALIGN(curWidth, hAlign);
        }

        if (info.isSubsampled || info.isBC)
        {
            offset /= info.bcWidth;
        }
    }
}

//////////////////////////////////////////////////////////////////////////
/// @brief Computes x lod offset for 2D surface at specified lod.
/// @param baseWidth - width of basemip (mip 0).
/// @param hAlign - horizontal alignment per mip, in texels
/// @param lod - lod index
/// @param offset - output offset.
INLINE void ComputeLODOffsetX(
    const SWR_FORMAT_INFO& info,
    uint32_t baseWidth,
    uint32_t hAlign,
    uint32_t lod,
    uint32_t &offset)
{
    if (lod < 2)
    {
        offset = 0;
    }
    else
    {
        uint32_t curWidth = baseWidth;
        // @note hAlign is already in blocks for compressed formats so upconvert
        //       so that we have the desired alignment post-divide.
        if (info.isBC)
        {
            hAlign *= info.bcWidth;
        }

        curWidth = std::max<uint32_t>(curWidth >> 1, 1U);
        curWidth = GFX_ALIGN(curWidth, hAlign);

        if (info.isSubsampled || info.isBC)
        {
            curWidth /= info.bcWidth;
        }

        offset = curWidth;
    }
}

//////////////////////////////////////////////////////////////////////////
/// @brief Computes y lod offset for 2D surface at specified lod.
/// @param baseWidth - width of basemip (mip 0).
/// @param vAlign - vertical alignment per mip, in rows
/// @param lod - lod index
/// @param offset - output offset.
INLINE void ComputeLODOffsetY(
    const SWR_FORMAT_INFO& info,
    uint32_t baseHeight,
    uint32_t vAlign,
    uint32_t lod,
    uint32_t &offset)
{
    if (lod == 0)
    {
        offset = 0;
    }
    else
    {
        offset = 0;
        uint32_t mipHeight = baseHeight;

        // @note vAlign is already in blocks for compressed formats so upconvert
        //       so that we have the desired alignment post-divide.
        if (info.isBC)
        {
            vAlign *= info.bcHeight;
        }

        for (uint32_t l = 1; l <= lod; ++l)
        {
            uint32_t alignedMipHeight = GFX_ALIGN(mipHeight, vAlign);
            offset += ((l != 2) ? alignedMipHeight : 0);
            mipHeight = std::max<uint32_t>(mipHeight >> 1, 1U);
        }

        if (info.isBC)
        {
            offset /= info.bcHeight;
        }
    }
}

//////////////////////////////////////////////////////////////////////////
/// @brief Computes 1D surface offset
/// @param x - offset from start of array slice at given lod.
/// @param array - array slice index
/// @param lod - lod index
/// @param pState - surface state
/// @param xOffsetBytes - output offset in bytes.
template<bool UseCachedOffsets>
INLINE void ComputeSurfaceOffset1D(
    uint32_t x,
    uint32_t array,
    uint32_t lod,
    const SWR_SURFACE_STATE *pState,
    uint32_t &xOffsetBytes)
{
    const SWR_FORMAT_INFO &info = GetFormatInfo(pState->format);
    uint32_t lodOffset;

    if (UseCachedOffsets)
    {
        lodOffset = pState->lodOffsets[0][lod];
    }
    else
    {
        ComputeLODOffset1D(info, pState->width, pState->halign, lod, lodOffset);
    }

    xOffsetBytes = (array * pState->qpitch + lodOffset + x) * info.Bpp;
}

//////////////////////////////////////////////////////////////////////////
/// @brief Adjusts the array slice for legacy TileY MSAA
/// @param pState - surface state
/// @param array - array slice index
/// @param sampleNum - requested sample
INLINE void AdjustCoordsForMSAA(const SWR_SURFACE_STATE *pState, uint32_t& x, uint32_t& y, uint32_t& arrayIndex, uint32_t sampleNum)
{
    /// @todo: might want to templatize adjusting for sample slices when we support tileYS/tileYF.
    if((pState->tileMode == SWR_TILE_MODE_YMAJOR ||
        pState->tileMode == SWR_TILE_MODE_WMAJOR) && 
       pState->bInterleavedSamples)
    {
        uint32_t newX, newY, newSampleX, newSampleY;
        switch(pState->numSamples)
        {
        case 1:
            newX = x;
            newY = y;
            newSampleX = newSampleY = 0;
            break;
        case 2:
        {
            assert(pState->type == SURFACE_2D);
            static const uint32_t xMask = 0xFFFFFFFD;
            static const uint32_t sampleMaskX = 0x1;
            newX = pdep_u32(x, xMask);
            newY = y;
            newSampleX = pext_u32(sampleNum, sampleMaskX);
            newSampleY = 0;
        }
            break;
        case 4:
        {
            assert(pState->type == SURFACE_2D);
            static const uint32_t mask = 0xFFFFFFFD;
            static const uint32_t sampleMaskX = 0x1;
            static const uint32_t sampleMaskY = 0x2;
            newX = pdep_u32(x, mask);
            newY = pdep_u32(y, mask);
            newSampleX = pext_u32(sampleNum, sampleMaskX);
            newSampleY = pext_u32(sampleNum, sampleMaskY);
        }
            break;
        case 8:
        {
            assert(pState->type == SURFACE_2D);
            static const uint32_t xMask = 0xFFFFFFF9;
            static const uint32_t yMask = 0xFFFFFFFD;
            static const uint32_t sampleMaskX = 0x5;
            static const uint32_t sampleMaskY = 0x2;
            newX = pdep_u32(x, xMask);
            newY = pdep_u32(y, yMask);
            newSampleX = pext_u32(sampleNum, sampleMaskX);
            newSampleY = pext_u32(sampleNum, sampleMaskY);
        }
            break;
        case 16:
        {
            assert(pState->type == SURFACE_2D);
            static const uint32_t mask = 0xFFFFFFF9;
            static const uint32_t sampleMaskX = 0x5;
            static const uint32_t sampleMaskY = 0xA;
            newX = pdep_u32(x, mask);
            newY = pdep_u32(y, mask);
            newSampleX = pext_u32(sampleNum, sampleMaskX);
            newSampleY = pext_u32(sampleNum, sampleMaskY);
        }
            break;
        default:
            assert(0 && "Unsupported sample count");
            newX = newY = 0;
            newSampleX = newSampleY = 0;
            break;
        }
        x = newX | (newSampleX << 1);
        y = newY | (newSampleY << 1);
    }
    else if(pState->tileMode == SWR_TILE_MODE_YMAJOR ||
            pState->tileMode == SWR_TILE_NONE)
    {
        uint32_t sampleShift;
        switch(pState->numSamples)
        {
        case 1:
            assert(sampleNum == 0);
            sampleShift = 0;
            break;
        case 2:
            assert(pState->type == SURFACE_2D);
            sampleShift = 1;
            break;
        case 4:
            assert(pState->type == SURFACE_2D);
            sampleShift = 2;
            break;
        case 8:
            assert(pState->type == SURFACE_2D);
            sampleShift = 3;
            break;
        case 16:
            assert(pState->type == SURFACE_2D);
            sampleShift = 4;
            break;
        default:
            assert(0 && "Unsupported sample count");
            sampleShift = 0;
            break;
        }
        arrayIndex = (arrayIndex << sampleShift) | sampleNum;
    }
}

//////////////////////////////////////////////////////////////////////////
/// @brief Computes 2D surface offset
/// @param x - horizontal offset from start of array slice and lod.
/// @param y - vertical offset from start of array slice and lod.
/// @param array - array slice index
/// @param lod - lod index
/// @param pState - surface state
/// @param xOffsetBytes - output x offset in bytes.
/// @param yOffsetRows - output y offset in bytes.
template<bool UseCachedOffsets>
INLINE void ComputeSurfaceOffset2D(uint32_t x, uint32_t y, uint32_t array, uint32_t sampleNum, uint32_t lod, const SWR_SURFACE_STATE *pState, uint32_t &xOffsetBytes, uint32_t &yOffsetRows)
{
    const SWR_FORMAT_INFO &info = GetFormatInfo(pState->format);
    uint32_t lodOffsetX, lodOffsetY;

    if (UseCachedOffsets)
    {
        lodOffsetX = pState->lodOffsets[0][lod];
        lodOffsetY = pState->lodOffsets[1][lod];
    }
    else
    {
        ComputeLODOffsetX(info, pState->width, pState->halign, lod, lodOffsetX);
        ComputeLODOffsetY(info, pState->height, pState->valign, lod, lodOffsetY);
    }

    AdjustCoordsForMSAA(pState, x, y, array, sampleNum);
    xOffsetBytes = (x + lodOffsetX + pState->xOffset) * info.Bpp;
    yOffsetRows = (array * pState->qpitch) + lodOffsetY + y + pState->yOffset;
}

//////////////////////////////////////////////////////////////////////////
/// @brief Computes 3D surface offset
/// @param x - horizontal offset from start of array slice and lod.
/// @param y - vertical offset from start of array slice and lod.
/// @param z - depth offset from start of array slice and lod.
/// @param lod - lod index
/// @param pState - surface state
/// @param xOffsetBytes - output x offset in bytes.
/// @param yOffsetRows - output y offset in rows.
/// @param zOffsetSlices - output y offset in slices.
template<bool UseCachedOffsets>
INLINE void ComputeSurfaceOffset3D(uint32_t x, uint32_t y, uint32_t z, uint32_t lod, const SWR_SURFACE_STATE *pState, uint32_t &xOffsetBytes, uint32_t &yOffsetRows, uint32_t &zOffsetSlices)
{
    const SWR_FORMAT_INFO &info = GetFormatInfo(pState->format);
    uint32_t lodOffsetX, lodOffsetY;

    if (UseCachedOffsets)
    {
        lodOffsetX = pState->lodOffsets[0][lod];
        lodOffsetY = pState->lodOffsets[1][lod];
    }
    else
    {
        ComputeLODOffsetX(info, pState->width, pState->halign, lod, lodOffsetX);
        ComputeLODOffsetY(info, pState->height, pState->valign, lod, lodOffsetY);
    }

    xOffsetBytes = (x + lodOffsetX) * info.Bpp;
    yOffsetRows = lodOffsetY + y;
    zOffsetSlices = z;
}

//////////////////////////////////////////////////////////////////////////
/// @brief Swizzles the linear x,y offsets depending on surface tiling mode
///        and returns final surface address
/// @param xOffsetBytes - x offset from base of surface in bytes
/// @param yOffsetRows - y offset from base of surface in rows
/// @param pState - pointer to the surface state
template<typename TTraits>
INLINE uint32_t ComputeTileSwizzle2D(uint32_t xOffsetBytes, uint32_t yOffsetRows, const SWR_SURFACE_STATE *pState)
{
    return ComputeOffset2D<TTraits>(pState->pitch, xOffsetBytes, yOffsetRows);
}

//////////////////////////////////////////////////////////////////////////
/// @brief Swizzles the linear x,y offsets depending on surface tiling mode
///        and returns final surface address
/// @param xOffsetBytes - x offset from base of surface in bytes
/// @param yOffsetRows - y offset from base of surface in rows
/// @param pState - pointer to the surface state
template<typename TTraits>
INLINE uint32_t ComputeTileSwizzle3D(uint32_t xOffsetBytes, uint32_t yOffsetRows, uint32_t zOffsetSlices, const SWR_SURFACE_STATE *pState)
{
    return ComputeOffset3D<TTraits>(pState->qpitch, pState->pitch, xOffsetBytes, yOffsetRows, zOffsetSlices);
}

//////////////////////////////////////////////////////////////////////////
/// @brief Swizzles the linear x,y offsets depending on surface tiling mode
///        and returns final surface address
/// @param xOffsetBytes - x offset from base of surface in bytes
/// @param yOffsetRows - y offset from base of surface in rows
/// @param pState - pointer to the surface state
INLINE
uint32_t TileSwizzle2D(uint32_t xOffsetBytes, uint32_t yOffsetRows, const SWR_SURFACE_STATE *pState)
{
    switch (pState->tileMode)
    {
    case SWR_TILE_NONE: return ComputeTileSwizzle2D<TilingTraits<SWR_TILE_NONE, 32> >(xOffsetBytes, yOffsetRows, pState);
    case SWR_TILE_SWRZ: return ComputeTileSwizzle2D<TilingTraits<SWR_TILE_SWRZ, 32> >(xOffsetBytes, yOffsetRows, pState);
    case SWR_TILE_MODE_XMAJOR: return ComputeTileSwizzle2D<TilingTraits<SWR_TILE_MODE_XMAJOR, 8> >(xOffsetBytes, yOffsetRows, pState);
    case SWR_TILE_MODE_YMAJOR: return ComputeTileSwizzle2D<TilingTraits<SWR_TILE_MODE_YMAJOR, 32> >(xOffsetBytes, yOffsetRows, pState);
    case SWR_TILE_MODE_WMAJOR: return ComputeTileSwizzle2D<TilingTraits<SWR_TILE_MODE_WMAJOR, 8> >(xOffsetBytes, yOffsetRows, pState);
    default: SWR_INVALID("Unsupported tiling mode");
    }
    return 0;
}

//////////////////////////////////////////////////////////////////////////
/// @brief Swizzles the linear x,y,z offsets depending on surface tiling mode
///        and returns final surface address
/// @param xOffsetBytes - x offset from base of surface in bytes
/// @param yOffsetRows - y offset from base of surface in rows
/// @param zOffsetSlices - z offset from base of surface in slices
/// @param pState - pointer to the surface state
INLINE
uint32_t TileSwizzle3D(uint32_t xOffsetBytes, uint32_t yOffsetRows, uint32_t zOffsetSlices, const SWR_SURFACE_STATE *pState)
{
    switch (pState->tileMode)
    {
    case SWR_TILE_NONE: return ComputeTileSwizzle3D<TilingTraits<SWR_TILE_NONE, 32> >(xOffsetBytes, yOffsetRows, zOffsetSlices, pState);
    case SWR_TILE_SWRZ: return ComputeTileSwizzle3D<TilingTraits<SWR_TILE_SWRZ, 32> >(xOffsetBytes, yOffsetRows, zOffsetSlices, pState);
    case SWR_TILE_MODE_YMAJOR: return ComputeTileSwizzle3D<TilingTraits<SWR_TILE_MODE_YMAJOR, 32> >(xOffsetBytes, yOffsetRows, zOffsetSlices, pState);
    default: SWR_INVALID("Unsupported tiling mode");
    }
    return 0;
}

template<bool UseCachedOffsets>
INLINE
uint32_t ComputeSurfaceOffset(uint32_t x, uint32_t y, uint32_t z, uint32_t array, uint32_t sampleNum, uint32_t lod, const SWR_SURFACE_STATE *pState)
{
    uint32_t offsetX = 0, offsetY = 0, offsetZ = 0;
    switch (pState->type)
    {
    case SURFACE_BUFFER:
    case SURFACE_STRUCTURED_BUFFER:
        offsetX = x * pState->pitch;
        return offsetX;
        break;
    case SURFACE_1D:
        ComputeSurfaceOffset1D<UseCachedOffsets>(x, array, lod, pState, offsetX);
        return TileSwizzle2D(offsetX, 0, pState);
        break;
    case SURFACE_2D:
        ComputeSurfaceOffset2D<UseCachedOffsets>(x, y, array, sampleNum, lod, pState, offsetX, offsetY);
        return TileSwizzle2D(offsetX, offsetY, pState);
    case SURFACE_3D:
        ComputeSurfaceOffset3D<UseCachedOffsets>(x, y, z, lod, pState, offsetX, offsetY, offsetZ);
        return TileSwizzle3D(offsetX, offsetY, offsetZ, pState);
        break;
    case SURFACE_CUBE:
        ComputeSurfaceOffset2D<UseCachedOffsets>(x, y, array, sampleNum, lod, pState, offsetX, offsetY);
        return TileSwizzle2D(offsetX, offsetY, pState);
        break;
    default: SWR_INVALID("Unsupported format");
    }

    return 0;
}

typedef void*(*PFN_COMPUTESURFADDR)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const SWR_SURFACE_STATE*);

//////////////////////////////////////////////////////////////////////////
/// @brief Computes surface address at the given location and lod
/// @param x - x location in pixels
/// @param y - y location in rows
/// @param z - z location for 3D surfaces
/// @param array - array slice for 1D and 2D surfaces
/// @param lod - level of detail
/// @param pState - pointer to the surface state
template<bool UseCachedOffsets, bool IsRead>
INLINE
void* ComputeSurfaceAddress(uint32_t x, uint32_t y, uint32_t z, uint32_t array, uint32_t sampleNum, uint32_t lod, const SWR_SURFACE_STATE *pState)
{
    return (void*)(pState->xpBaseAddress + ComputeSurfaceOffset<UseCachedOffsets>(x, y, z, array, sampleNum, lod, pState));
}
