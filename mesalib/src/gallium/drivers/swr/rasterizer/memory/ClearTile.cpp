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
* @file ClearTile.cpp
*
* @brief Functionality for ClearTile. StoreHotTileClear clears a single macro
*        tile in the destination.
*
******************************************************************************/
#include "common/os.h"
#include "core/context.h"
#include "common/formats.h"
#include "memory/TilingFunctions.h"
#include "memory/tilingtraits.h"
#include "memory/Convert.h"

typedef void(*PFN_STORE_TILES_CLEAR)(const float*, SWR_SURFACE_STATE*, UINT, UINT, uint32_t);

//////////////////////////////////////////////////////////////////////////
/// Clear Raster Tile Function Tables.
//////////////////////////////////////////////////////////////////////////
static PFN_STORE_TILES_CLEAR sStoreTilesClearColorTable[NUM_SWR_FORMATS];

static PFN_STORE_TILES_CLEAR sStoreTilesClearDepthTable[NUM_SWR_FORMATS];

//////////////////////////////////////////////////////////////////////////
/// StoreRasterTileClear
//////////////////////////////////////////////////////////////////////////
template<SWR_FORMAT SrcFormat, SWR_FORMAT DstFormat>
struct StoreRasterTileClear
{
    //////////////////////////////////////////////////////////////////////////
    /// @brief Stores an 8x8 raster tile to the destination surface.
    /// @param pColor - Pointer to clear color.
    /// @param pDstSurface - Destination surface state
    /// @param x, y - Coordinates to raster tile.
    INLINE static void StoreClear(
        const uint8_t* dstFormattedColor,
        UINT dstBytesPerPixel,
        SWR_SURFACE_STATE* pDstSurface,
        UINT x, UINT y, // (x, y) pixel coordinate to start of raster tile.
        uint32_t renderTargetArrayIndex)
    {
        // If we're outside of the surface, stop.
        uint32_t lodWidth = std::max<uint32_t>(pDstSurface->width >> pDstSurface->lod, 1U);
        uint32_t lodHeight = std::max<uint32_t>(pDstSurface->height >> pDstSurface->lod, 1U);
        if (x >= lodWidth || y >= lodHeight)
            return;

        // Compute destination address for raster tile.
        uint8_t* pDstTile = (uint8_t*)ComputeSurfaceAddress<false, false>(
                x, y, pDstSurface->arrayIndex + renderTargetArrayIndex,
                pDstSurface->arrayIndex + renderTargetArrayIndex,
                0, // sampleNum
                pDstSurface->lod,
                pDstSurface);

        // start of first row
        uint8_t* pDst = pDstTile;
        UINT dstBytesPerRow = 0;

        // For each raster tile pixel in row 0 (rx, 0)
        for (UINT rx = 0; (rx < KNOB_TILE_X_DIM) && ((x + rx) < lodWidth); ++rx)
        {
            memcpy(pDst, dstFormattedColor, dstBytesPerPixel);

            // Increment pointer to next pixel in row.
            pDst += dstBytesPerPixel;
            dstBytesPerRow += dstBytesPerPixel;
        }

        // start of second row
        pDst = pDstTile + pDstSurface->pitch;

        // For each remaining row in the rest of the raster tile
        for (UINT ry = 1; (ry < KNOB_TILE_Y_DIM) && ((y + ry) < lodHeight); ++ry)
        {
            // copy row
            memcpy(pDst, pDstTile, dstBytesPerRow);

            // Increment pointer to first pixel in next row.
            pDst += pDstSurface->pitch;
        }
    }
};

//////////////////////////////////////////////////////////////////////////
/// StoreMacroTileClear - Stores a macro tile clear to its raster tiles.
//////////////////////////////////////////////////////////////////////////
template<SWR_FORMAT SrcFormat, SWR_FORMAT DstFormat>
struct StoreMacroTileClear
{
    //////////////////////////////////////////////////////////////////////////
    /// @brief Stores a macrotile to the destination surface.
    /// @param pColor - Pointer to color to write to pixels.
    /// @param pDstSurface - Destination surface state
    /// @param x, y - Coordinates to macro tile
    static void StoreClear(
        const float *pColor,
        SWR_SURFACE_STATE* pDstSurface,
        UINT x, UINT y, uint32_t renderTargetArrayIndex)
    {
        UINT dstBytesPerPixel = (FormatTraits<DstFormat>::bpp / 8);

        uint8_t dstFormattedColor[16]; // max bpp is 128, so 16 is all we need here for one pixel

        float srcColor[4];

        for (UINT comp = 0; comp < FormatTraits<DstFormat>::numComps; ++comp)
        {
            srcColor[comp] = pColor[FormatTraits<DstFormat>::swizzle(comp)];
        }

        // using this helper function, but the Tiling Traits is unused inside it so just using a dummy value
        ConvertPixelFromFloat<DstFormat>(dstFormattedColor, srcColor);

        // Store each raster tile from the hot tile to the destination surface.
        // TODO:  Put in check for partial coverage on x/y -- SWR_ASSERT if it happens.
        //        Intent is for this function to only handle full tiles.
        for (UINT row = 0; row < KNOB_MACROTILE_Y_DIM; row += KNOB_TILE_Y_DIM)
        {
            for (UINT col = 0; col < KNOB_MACROTILE_X_DIM; col += KNOB_TILE_X_DIM)
            {
                StoreRasterTileClear<SrcFormat, DstFormat>::StoreClear(dstFormattedColor, dstBytesPerPixel, pDstSurface, (x + col), (y + row), renderTargetArrayIndex);
            }
        }
    }
};

//////////////////////////////////////////////////////////////////////////
/// @brief Writes clear color to every pixel of a render surface
/// @param hPrivateContext - Handle to private DC
/// @param renderTargetIndex - Index to destination render target
/// @param x, y - Coordinates to raster tile.
/// @param pClearColor - Pointer to clear color
void SwrStoreHotTileClear(
    HANDLE hWorkerPrivateData,
    SWR_SURFACE_STATE *pDstSurface,
    SWR_RENDERTARGET_ATTACHMENT renderTargetIndex,
    UINT x,
    UINT y,
    uint32_t renderTargetArrayIndex,
    const float* pClearColor)
{
    PFN_STORE_TILES_CLEAR pfnStoreTilesClear = NULL;

    if (renderTargetIndex == SWR_ATTACHMENT_STENCIL)
    {
        SWR_ASSERT(pDstSurface->format == R8_UINT);
        pfnStoreTilesClear = StoreMacroTileClear<R8_UINT, R8_UINT>::StoreClear;
    }
    else if (renderTargetIndex == SWR_ATTACHMENT_DEPTH)
    {
        pfnStoreTilesClear = sStoreTilesClearDepthTable[pDstSurface->format];
    }
    else
    {
        pfnStoreTilesClear = sStoreTilesClearColorTable[pDstSurface->format];
    }

    SWR_ASSERT(pfnStoreTilesClear != NULL);

    // Store a macro tile.
    /// @todo Once all formats are supported then if check can go away. This is to help us near term to make progress.
    if (pfnStoreTilesClear != NULL)
    {
        pfnStoreTilesClear(pClearColor, pDstSurface, x, y, renderTargetArrayIndex);
    }
}

//////////////////////////////////////////////////////////////////////////
/// INIT_STORE_TILES_TABLE - Helper macro for setting up the tables.
#define INIT_STORE_TILES_CLEAR_COLOR_TABLE() \
    memset(sStoreTilesClearColorTable, 0, sizeof(sStoreTilesClearColorTable)); \
    \
    sStoreTilesClearColorTable[R32G32B32A32_FLOAT]      = StoreMacroTileClear<R32G32B32A32_FLOAT, R32G32B32A32_FLOAT>::StoreClear; \
    sStoreTilesClearColorTable[R32G32B32A32_SINT]      = StoreMacroTileClear<R32G32B32A32_FLOAT, R32G32B32A32_SINT>::StoreClear; \
    sStoreTilesClearColorTable[R32G32B32A32_UINT]      = StoreMacroTileClear<R32G32B32A32_FLOAT, R32G32B32A32_UINT>::StoreClear; \
    sStoreTilesClearColorTable[R32G32B32X32_FLOAT]      = StoreMacroTileClear<R32G32B32A32_FLOAT, R32G32B32X32_FLOAT>::StoreClear; \
    sStoreTilesClearColorTable[R32G32B32_FLOAT]      = StoreMacroTileClear<R32G32B32A32_FLOAT, R32G32B32_FLOAT>::StoreClear; \
    sStoreTilesClearColorTable[R32G32B32_SINT]      = StoreMacroTileClear<R32G32B32A32_FLOAT, R32G32B32_SINT>::StoreClear; \
    sStoreTilesClearColorTable[R32G32B32_UINT]      = StoreMacroTileClear<R32G32B32A32_FLOAT, R32G32B32_UINT>::StoreClear; \
    sStoreTilesClearColorTable[R16G16B16A16_UNORM]      = StoreMacroTileClear<R32G32B32A32_FLOAT, R16G16B16A16_UNORM>::StoreClear; \
    sStoreTilesClearColorTable[R16G16B16A16_SNORM]      = StoreMacroTileClear<R32G32B32A32_FLOAT, R16G16B16A16_SNORM>::StoreClear; \
    sStoreTilesClearColorTable[R16G16B16A16_SINT]      = StoreMacroTileClear<R32G32B32A32_FLOAT, R16G16B16A16_SINT>::StoreClear; \
    sStoreTilesClearColorTable[R16G16B16A16_UINT]      = StoreMacroTileClear<R32G32B32A32_FLOAT, R16G16B16A16_UINT>::StoreClear; \
    sStoreTilesClearColorTable[R16G16B16A16_FLOAT]      = StoreMacroTileClear<R32G32B32A32_FLOAT, R16G16B16A16_FLOAT>::StoreClear; \
    sStoreTilesClearColorTable[R32G32_FLOAT]      = StoreMacroTileClear<R32G32B32A32_FLOAT, R32G32_FLOAT>::StoreClear; \
    sStoreTilesClearColorTable[R32G32_SINT]      = StoreMacroTileClear<R32G32B32A32_FLOAT, R32G32_SINT>::StoreClear; \
    sStoreTilesClearColorTable[R32G32_UINT]      = StoreMacroTileClear<R32G32B32A32_FLOAT, R32G32_UINT>::StoreClear; \
    sStoreTilesClearColorTable[R16G16B16X16_UNORM]      = StoreMacroTileClear<R32G32B32A32_FLOAT, R16G16B16X16_UNORM>::StoreClear; \
    sStoreTilesClearColorTable[R16G16B16X16_FLOAT]      = StoreMacroTileClear<R32G32B32A32_FLOAT, R16G16B16X16_FLOAT>::StoreClear; \
    sStoreTilesClearColorTable[B8G8R8A8_UNORM]      = StoreMacroTileClear<R32G32B32A32_FLOAT, B8G8R8A8_UNORM>::StoreClear; \
    sStoreTilesClearColorTable[B8G8R8A8_UNORM_SRGB]      = StoreMacroTileClear<R32G32B32A32_FLOAT, B8G8R8A8_UNORM_SRGB>::StoreClear; \
    sStoreTilesClearColorTable[R10G10B10A2_UNORM]      = StoreMacroTileClear<R32G32B32A32_FLOAT, R10G10B10A2_UNORM>::StoreClear; \
    sStoreTilesClearColorTable[R10G10B10A2_UNORM_SRGB]      = StoreMacroTileClear<R32G32B32A32_FLOAT, R10G10B10A2_UNORM_SRGB>::StoreClear; \
    sStoreTilesClearColorTable[R10G10B10A2_UINT]      = StoreMacroTileClear<R32G32B32A32_FLOAT, R10G10B10A2_UINT>::StoreClear; \
    sStoreTilesClearColorTable[R8G8B8A8_UNORM]      = StoreMacroTileClear<R32G32B32A32_FLOAT, R8G8B8A8_UNORM>::StoreClear; \
    sStoreTilesClearColorTable[R8G8B8A8_UNORM_SRGB]      = StoreMacroTileClear<R32G32B32A32_FLOAT, R8G8B8A8_UNORM_SRGB>::StoreClear; \
    sStoreTilesClearColorTable[R8G8B8A8_SNORM]      = StoreMacroTileClear<R32G32B32A32_FLOAT, R8G8B8A8_SNORM>::StoreClear; \
    sStoreTilesClearColorTable[R8G8B8A8_SINT]      = StoreMacroTileClear<R32G32B32A32_FLOAT, R8G8B8A8_SINT>::StoreClear; \
    sStoreTilesClearColorTable[R8G8B8A8_UINT]      = StoreMacroTileClear<R32G32B32A32_FLOAT, R8G8B8A8_UINT>::StoreClear; \
    sStoreTilesClearColorTable[R16G16_UNORM]      = StoreMacroTileClear<R32G32B32A32_FLOAT, R16G16_UNORM>::StoreClear; \
    sStoreTilesClearColorTable[R16G16_SNORM]      = StoreMacroTileClear<R32G32B32A32_FLOAT, R16G16_SNORM>::StoreClear; \
    sStoreTilesClearColorTable[R16G16_SINT]      = StoreMacroTileClear<R32G32B32A32_FLOAT, R16G16_SINT>::StoreClear; \
    sStoreTilesClearColorTable[R16G16_UINT]      = StoreMacroTileClear<R32G32B32A32_FLOAT, R16G16_UINT>::StoreClear; \
    sStoreTilesClearColorTable[R16G16_FLOAT]      = StoreMacroTileClear<R32G32B32A32_FLOAT, R16G16_FLOAT>::StoreClear; \
    sStoreTilesClearColorTable[B10G10R10A2_UNORM]      = StoreMacroTileClear<R32G32B32A32_FLOAT, B10G10R10A2_UNORM>::StoreClear; \
    sStoreTilesClearColorTable[B10G10R10A2_UNORM_SRGB]      = StoreMacroTileClear<R32G32B32A32_FLOAT, B10G10R10A2_UNORM_SRGB>::StoreClear; \
    sStoreTilesClearColorTable[R11G11B10_FLOAT]      = StoreMacroTileClear<R32G32B32A32_FLOAT, R11G11B10_FLOAT>::StoreClear; \
    sStoreTilesClearColorTable[R32_SINT]      = StoreMacroTileClear<R32G32B32A32_FLOAT, R32_SINT>::StoreClear; \
    sStoreTilesClearColorTable[R32_UINT]      = StoreMacroTileClear<R32G32B32A32_FLOAT, R32_UINT>::StoreClear; \
    sStoreTilesClearColorTable[R32_FLOAT]      = StoreMacroTileClear<R32G32B32A32_FLOAT, R32_FLOAT>::StoreClear; \
    sStoreTilesClearColorTable[A32_FLOAT]      = StoreMacroTileClear<R32G32B32A32_FLOAT, A32_FLOAT>::StoreClear; \
    sStoreTilesClearColorTable[B8G8R8X8_UNORM]      = StoreMacroTileClear<R32G32B32A32_FLOAT, B8G8R8X8_UNORM>::StoreClear; \
    sStoreTilesClearColorTable[B8G8R8X8_UNORM_SRGB]      = StoreMacroTileClear<R32G32B32A32_FLOAT, B8G8R8X8_UNORM_SRGB>::StoreClear; \
    sStoreTilesClearColorTable[R8G8B8X8_UNORM]      = StoreMacroTileClear<R32G32B32A32_FLOAT, R8G8B8X8_UNORM>::StoreClear; \
    sStoreTilesClearColorTable[R8G8B8X8_UNORM_SRGB]      = StoreMacroTileClear<R32G32B32A32_FLOAT, R8G8B8X8_UNORM_SRGB>::StoreClear; \
    sStoreTilesClearColorTable[B10G10R10X2_UNORM]      = StoreMacroTileClear<R32G32B32A32_FLOAT, B10G10R10X2_UNORM>::StoreClear; \
    sStoreTilesClearColorTable[B5G6R5_UNORM]      = StoreMacroTileClear<R32G32B32A32_FLOAT, B5G6R5_UNORM>::StoreClear; \
    sStoreTilesClearColorTable[B5G6R5_UNORM_SRGB]      = StoreMacroTileClear<R32G32B32A32_FLOAT, B5G6R5_UNORM_SRGB>::StoreClear; \
    sStoreTilesClearColorTable[B5G5R5A1_UNORM]      = StoreMacroTileClear<R32G32B32A32_FLOAT, B5G5R5A1_UNORM>::StoreClear; \
    sStoreTilesClearColorTable[B5G5R5A1_UNORM_SRGB]      = StoreMacroTileClear<R32G32B32A32_FLOAT, B5G5R5A1_UNORM_SRGB>::StoreClear; \
    sStoreTilesClearColorTable[B4G4R4A4_UNORM]      = StoreMacroTileClear<R32G32B32A32_FLOAT, B4G4R4A4_UNORM>::StoreClear; \
    sStoreTilesClearColorTable[B4G4R4A4_UNORM_SRGB]      = StoreMacroTileClear<R32G32B32A32_FLOAT, B4G4R4A4_UNORM_SRGB>::StoreClear; \
    sStoreTilesClearColorTable[R8G8_UNORM]      = StoreMacroTileClear<R32G32B32A32_FLOAT, R8G8_UNORM>::StoreClear; \
    sStoreTilesClearColorTable[R8G8_SNORM]      = StoreMacroTileClear<R32G32B32A32_FLOAT, R8G8_SNORM>::StoreClear; \
    sStoreTilesClearColorTable[R8G8_SINT]      = StoreMacroTileClear<R32G32B32A32_FLOAT, R8G8_SINT>::StoreClear; \
    sStoreTilesClearColorTable[R8G8_UINT]      = StoreMacroTileClear<R32G32B32A32_FLOAT, R8G8_UINT>::StoreClear; \
    sStoreTilesClearColorTable[R16_UNORM]      = StoreMacroTileClear<R32G32B32A32_FLOAT, R16_UNORM>::StoreClear; \
    sStoreTilesClearColorTable[R16_SNORM]      = StoreMacroTileClear<R32G32B32A32_FLOAT, R16_SNORM>::StoreClear; \
    sStoreTilesClearColorTable[R16_SINT]      = StoreMacroTileClear<R32G32B32A32_FLOAT, R16_SINT>::StoreClear; \
    sStoreTilesClearColorTable[R16_UINT]      = StoreMacroTileClear<R32G32B32A32_FLOAT, R16_UINT>::StoreClear; \
    sStoreTilesClearColorTable[R16_FLOAT]      = StoreMacroTileClear<R32G32B32A32_FLOAT, R16_FLOAT>::StoreClear; \
    sStoreTilesClearColorTable[A16_UNORM]      = StoreMacroTileClear<R32G32B32A32_FLOAT, A16_UNORM>::StoreClear; \
    sStoreTilesClearColorTable[A16_FLOAT]      = StoreMacroTileClear<R32G32B32A32_FLOAT, A16_FLOAT>::StoreClear; \
    sStoreTilesClearColorTable[B5G5R5X1_UNORM]      = StoreMacroTileClear<R32G32B32A32_FLOAT, B5G5R5X1_UNORM>::StoreClear; \
    sStoreTilesClearColorTable[B5G5R5X1_UNORM_SRGB]      = StoreMacroTileClear<R32G32B32A32_FLOAT, B5G5R5X1_UNORM_SRGB>::StoreClear; \
    sStoreTilesClearColorTable[R8_UNORM]      = StoreMacroTileClear<R32G32B32A32_FLOAT, R8_UNORM>::StoreClear; \
    sStoreTilesClearColorTable[R8_SNORM]      = StoreMacroTileClear<R32G32B32A32_FLOAT, R8_SNORM>::StoreClear; \
    sStoreTilesClearColorTable[R8_SINT]      = StoreMacroTileClear<R32G32B32A32_FLOAT, R8_SINT>::StoreClear; \
    sStoreTilesClearColorTable[R8_UINT]      = StoreMacroTileClear<R32G32B32A32_FLOAT, R8_UINT>::StoreClear; \
    sStoreTilesClearColorTable[A8_UNORM]      = StoreMacroTileClear<R32G32B32A32_FLOAT, A8_UNORM>::StoreClear; \
    sStoreTilesClearColorTable[BC1_UNORM]      = StoreMacroTileClear<R32G32B32A32_FLOAT, BC1_UNORM>::StoreClear; \
    sStoreTilesClearColorTable[BC2_UNORM]      = StoreMacroTileClear<R32G32B32A32_FLOAT, BC2_UNORM>::StoreClear; \
    sStoreTilesClearColorTable[BC3_UNORM]      = StoreMacroTileClear<R32G32B32A32_FLOAT, BC3_UNORM>::StoreClear; \
    sStoreTilesClearColorTable[BC4_UNORM]      = StoreMacroTileClear<R32G32B32A32_FLOAT, BC4_UNORM>::StoreClear; \
    sStoreTilesClearColorTable[BC5_UNORM]      = StoreMacroTileClear<R32G32B32A32_FLOAT, BC5_UNORM>::StoreClear; \
    sStoreTilesClearColorTable[BC1_UNORM_SRGB]      = StoreMacroTileClear<R32G32B32A32_FLOAT, BC1_UNORM_SRGB>::StoreClear; \
    sStoreTilesClearColorTable[BC2_UNORM_SRGB]      = StoreMacroTileClear<R32G32B32A32_FLOAT, BC2_UNORM_SRGB>::StoreClear; \
    sStoreTilesClearColorTable[BC3_UNORM_SRGB]      = StoreMacroTileClear<R32G32B32A32_FLOAT, BC3_UNORM_SRGB>::StoreClear; \
    sStoreTilesClearColorTable[R8G8B8_UNORM]      = StoreMacroTileClear<R32G32B32A32_FLOAT, R8G8B8_UNORM>::StoreClear; \
    sStoreTilesClearColorTable[R8G8B8_SNORM]      = StoreMacroTileClear<R32G32B32A32_FLOAT, R8G8B8_SNORM>::StoreClear; \
    sStoreTilesClearColorTable[BC4_SNORM]      = StoreMacroTileClear<R32G32B32A32_FLOAT, BC4_SNORM>::StoreClear; \
    sStoreTilesClearColorTable[BC5_SNORM]      = StoreMacroTileClear<R32G32B32A32_FLOAT, BC5_SNORM>::StoreClear; \
    sStoreTilesClearColorTable[R16G16B16_FLOAT]      = StoreMacroTileClear<R32G32B32A32_FLOAT, R16G16B16_FLOAT>::StoreClear; \
    sStoreTilesClearColorTable[R16G16B16_UNORM]      = StoreMacroTileClear<R32G32B32A32_FLOAT, R16G16B16_UNORM>::StoreClear; \
    sStoreTilesClearColorTable[R16G16B16_SNORM]      = StoreMacroTileClear<R32G32B32A32_FLOAT, R16G16B16_SNORM>::StoreClear; \
    sStoreTilesClearColorTable[R8G8B8_UNORM_SRGB]      = StoreMacroTileClear<R32G32B32A32_FLOAT, R8G8B8_UNORM_SRGB>::StoreClear; \
    sStoreTilesClearColorTable[R16G16B16_UINT]      = StoreMacroTileClear<R32G32B32A32_FLOAT, R16G16B16_UINT>::StoreClear; \
    sStoreTilesClearColorTable[R16G16B16_SINT]      = StoreMacroTileClear<R32G32B32A32_FLOAT, R16G16B16_SINT>::StoreClear; \
    sStoreTilesClearColorTable[R10G10B10A2_SNORM]      = StoreMacroTileClear<R32G32B32A32_FLOAT, R10G10B10A2_SNORM>::StoreClear; \
    sStoreTilesClearColorTable[R10G10B10A2_SINT]      = StoreMacroTileClear<R32G32B32A32_FLOAT, R10G10B10A2_SINT>::StoreClear; \
    sStoreTilesClearColorTable[B10G10R10A2_SNORM]      = StoreMacroTileClear<R32G32B32A32_FLOAT, B10G10R10A2_SNORM>::StoreClear; \
    sStoreTilesClearColorTable[B10G10R10A2_UINT]      = StoreMacroTileClear<R32G32B32A32_FLOAT, B10G10R10A2_UINT>::StoreClear; \
    sStoreTilesClearColorTable[B10G10R10A2_SINT]      = StoreMacroTileClear<R32G32B32A32_FLOAT, B10G10R10A2_SINT>::StoreClear; \
    sStoreTilesClearColorTable[R8G8B8_UINT]      = StoreMacroTileClear<R32G32B32A32_FLOAT, R8G8B8_UINT>::StoreClear; \
    sStoreTilesClearColorTable[R8G8B8_SINT]      = StoreMacroTileClear<R32G32B32A32_FLOAT, R8G8B8_SINT>::StoreClear;

//////////////////////////////////////////////////////////////////////////
/// INIT_STORE_TILES_TABLE - Helper macro for setting up the tables.
#define INIT_STORE_TILES_CLEAR_DEPTH_TABLE() \
    memset(sStoreTilesClearDepthTable, 0, sizeof(sStoreTilesClearDepthTable)); \
    \
    sStoreTilesClearDepthTable[R32_FLOAT] = StoreMacroTileClear<R32_FLOAT, R32_FLOAT>::StoreClear; \
    sStoreTilesClearDepthTable[R32_FLOAT_X8X24_TYPELESS] = StoreMacroTileClear<R32_FLOAT, R32_FLOAT_X8X24_TYPELESS>::StoreClear; \
    sStoreTilesClearDepthTable[R24_UNORM_X8_TYPELESS] = StoreMacroTileClear<R32_FLOAT, R24_UNORM_X8_TYPELESS>::StoreClear; \
    sStoreTilesClearDepthTable[R16_UNORM] = StoreMacroTileClear<R32_FLOAT, R16_UNORM>::StoreClear;

//////////////////////////////////////////////////////////////////////////
/// @brief Sets up tables for ClearTile
void InitSimClearTilesTable()
{
    INIT_STORE_TILES_CLEAR_COLOR_TABLE();
    INIT_STORE_TILES_CLEAR_DEPTH_TABLE();
}
