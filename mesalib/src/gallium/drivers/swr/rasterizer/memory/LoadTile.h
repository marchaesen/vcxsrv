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
* @file LoadTile.h
* 
* @brief Functionality for Load
* 
******************************************************************************/
#include "common/os.h"
#include "common/formats.h"
#include "core/context.h"
#include "core/rdtsc_core.h"
#include "memory/TilingFunctions.h"
#include "memory/tilingtraits.h"
#include "memory/Convert.h"

typedef void(*PFN_LOAD_TILES)(const SWR_SURFACE_STATE*, uint8_t*, uint32_t, uint32_t, uint32_t);
typedef void(*PFN_LOAD_RASTER_TILES)(const SWR_SURFACE_STATE*, uint8_t*, uint32_t, uint32_t, uint32_t, uint32_t);

//////////////////////////////////////////////////////////////////////////
/// Load Raster Tile Function Tables.
//////////////////////////////////////////////////////////////////////////
extern PFN_LOAD_TILES sLoadTilesColorTable_SWR_TILE_NONE[NUM_SWR_FORMATS];
extern PFN_LOAD_TILES sLoadTilesDepthTable_SWR_TILE_NONE[NUM_SWR_FORMATS];

extern PFN_LOAD_TILES sLoadTilesColorTable_SWR_TILE_MODE_YMAJOR[NUM_SWR_FORMATS];
extern PFN_LOAD_TILES sLoadTilesColorTable_SWR_TILE_MODE_XMAJOR[NUM_SWR_FORMATS];

extern PFN_LOAD_TILES sLoadTilesDepthTable_SWR_TILE_MODE_YMAJOR[NUM_SWR_FORMATS];

void InitLoadTilesTable_Linear();
void InitLoadTilesTable_XMajor();
void InitLoadTilesTable_YMajor();

//////////////////////////////////////////////////////////////////////////
/// LoadRasterTile
//////////////////////////////////////////////////////////////////////////
template<typename TTraits, SWR_FORMAT SrcFormat, SWR_FORMAT DstFormat>
struct LoadRasterTile
{
    //////////////////////////////////////////////////////////////////////////
    /// @brief Retrieve color from hot tile source which is always float.
    /// @param pSrc - Pointer to raster tile.
    /// @param x, y - Coordinates to raster tile.
    /// @param output - output color
    INLINE static void SetSwizzledDstColor(
        const float srcColor[4],
        uint32_t x, uint32_t y,
        uint8_t* pDst)
    {
        typedef SimdTile_16<DstFormat, SrcFormat> SimdT;

        SimdT* pDstSimdTiles = (SimdT*)pDst;

        // Compute which simd tile we're accessing within 8x8 tile.
        //   i.e. Compute linear simd tile coordinate given (x, y) in pixel coordinates.
        uint32_t simdIndex = (y / SIMD16_TILE_Y_DIM) * (KNOB_TILE_X_DIM / SIMD16_TILE_X_DIM) + (x / SIMD16_TILE_X_DIM);

        SimdT* pSimdTile = &pDstSimdTiles[simdIndex];

        uint32_t simdOffset = (y % SIMD16_TILE_Y_DIM) * SIMD16_TILE_X_DIM + (x % SIMD16_TILE_X_DIM);

        pSimdTile->SetSwizzledColor(simdOffset, srcColor);
    }

    //////////////////////////////////////////////////////////////////////////
    /// @brief Loads an 8x8 raster tile from the src surface.
    /// @param pSrcSurface - Src surface state
    /// @param pDst - Destination hot tile pointer
    /// @param x, y - Coordinates to raster tile.
    INLINE static void Load(
        const SWR_SURFACE_STATE* pSrcSurface,
        uint8_t* pDst,
        uint32_t x, uint32_t y, uint32_t sampleNum, uint32_t renderTargetArrayIndex) // (x, y) pixel coordinate to start of raster tile.
    {
        uint32_t lodWidth = (pSrcSurface->width == 1) ? 1 : pSrcSurface->width >> pSrcSurface->lod;
        uint32_t lodHeight = (pSrcSurface->height == 1) ? 1 : pSrcSurface->height >> pSrcSurface->lod;

        // For each raster tile pixel (rx, ry)
        for (uint32_t ry = 0; ry < KNOB_TILE_Y_DIM; ++ry)
        {
            for (uint32_t rx = 0; rx < KNOB_TILE_X_DIM; ++rx)
            {
                if (((x + rx) < lodWidth) &&
                    ((y + ry) < lodHeight))
                {
                    uint8_t* pSrc = (uint8_t*)ComputeSurfaceAddress<false, true>(x + rx, y + ry, pSrcSurface->arrayIndex + renderTargetArrayIndex,
                            pSrcSurface->arrayIndex + renderTargetArrayIndex, sampleNum,
                            pSrcSurface->lod, pSrcSurface);

                    float srcColor[4];
                    ConvertPixelToFloat<SrcFormat>(srcColor, pSrc);

                    // store pixel to hottile
                    SetSwizzledDstColor(srcColor, rx, ry, pDst);
                }
            }
        }
    }
};

//////////////////////////////////////////////////////////////////////////
/// LoadMacroTile - Loads a macro tile which consists of raster tiles.
//////////////////////////////////////////////////////////////////////////
template<typename TTraits, SWR_FORMAT SrcFormat, SWR_FORMAT DstFormat>
struct LoadMacroTile
{
    //////////////////////////////////////////////////////////////////////////
    /// @brief Load a macrotile to the destination surface.
    /// @param pSrc - Pointer to macro tile.
    /// @param pDstSurface - Destination surface state
    /// @param x, y - Coordinates to macro tile
    static void Load(
        const SWR_SURFACE_STATE* pSrcSurface,
        uint8_t *pDstHotTile,
        uint32_t x, uint32_t y, uint32_t renderTargetArrayIndex)
    {
        PFN_LOAD_RASTER_TILES loadRasterTileFn;
        loadRasterTileFn = LoadRasterTile<TTraits, SrcFormat, DstFormat>::Load;

        // Load each raster tile from the hot tile to the destination surface.
        for (uint32_t row = 0; row < KNOB_MACROTILE_Y_DIM; row += KNOB_TILE_Y_DIM)
        {
            for (uint32_t col = 0; col < KNOB_MACROTILE_X_DIM; col += KNOB_TILE_X_DIM)
            {
                for (uint32_t sampleNum = 0; sampleNum < pSrcSurface->numSamples; sampleNum++)
                {
                    loadRasterTileFn(pSrcSurface, pDstHotTile, (x + col), (y + row), sampleNum, renderTargetArrayIndex);
                    pDstHotTile += KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * (FormatTraits<DstFormat>::bpp / 8);
                }
            }
        }
    }
};

//////////////////////////////////////////////////////////////////////////
/// InitLoadTileColorTable - Helper function for setting up the tables.
template<SWR_TILE_MODE TTileMode>
static INLINE void InitLoadTileColorTable(PFN_LOAD_TILES (&table)[NUM_SWR_FORMATS])
{
    memset(table, 0, sizeof(table));
   
    table[R32G32B32A32_FLOAT]              = LoadMacroTile<TilingTraits<TTileMode, 128>, R32G32B32A32_FLOAT, R32G32B32A32_FLOAT>::Load;
    table[R32G32B32A32_SINT]               = LoadMacroTile<TilingTraits<TTileMode, 128>, R32G32B32A32_SINT, R32G32B32A32_FLOAT>::Load;
    table[R32G32B32A32_UINT]               = LoadMacroTile<TilingTraits<TTileMode, 128>, R32G32B32A32_UINT, R32G32B32A32_FLOAT>::Load;
    table[R32G32B32X32_FLOAT]              = LoadMacroTile<TilingTraits<TTileMode, 128>, R32G32B32X32_FLOAT, R32G32B32A32_FLOAT>::Load;
    table[R32G32B32A32_SSCALED]            = LoadMacroTile<TilingTraits<TTileMode, 128>, R32G32B32A32_SSCALED, R32G32B32A32_FLOAT>::Load;
    table[R32G32B32A32_USCALED]            = LoadMacroTile<TilingTraits<TTileMode, 128>, R32G32B32A32_USCALED, R32G32B32A32_FLOAT>::Load;
    table[R32G32B32_FLOAT]                 = LoadMacroTile<TilingTraits<TTileMode, 96>, R32G32B32_FLOAT, R32G32B32A32_FLOAT>::Load;
    table[R32G32B32_SINT]                  = LoadMacroTile<TilingTraits<TTileMode, 96>, R32G32B32_SINT, R32G32B32A32_FLOAT>::Load;
    table[R32G32B32_UINT]                  = LoadMacroTile<TilingTraits<TTileMode, 96>, R32G32B32_UINT, R32G32B32A32_FLOAT>::Load;
    table[R32G32B32_SSCALED]               = LoadMacroTile<TilingTraits<TTileMode, 96>, R32G32B32_SSCALED, R32G32B32A32_FLOAT>::Load;
    table[R32G32B32_USCALED]               = LoadMacroTile<TilingTraits<TTileMode, 96>, R32G32B32_USCALED, R32G32B32A32_FLOAT>::Load;
    table[R16G16B16A16_UNORM]              = LoadMacroTile<TilingTraits<TTileMode, 64>, R16G16B16A16_UNORM, R32G32B32A32_FLOAT>::Load;
    table[R16G16B16A16_SNORM]              = LoadMacroTile<TilingTraits<TTileMode, 64>, R16G16B16A16_SNORM, R32G32B32A32_FLOAT>::Load;
    table[R16G16B16A16_SINT]               = LoadMacroTile<TilingTraits<TTileMode, 64>, R16G16B16A16_SINT, R32G32B32A32_FLOAT>::Load;
    table[R16G16B16A16_UINT]               = LoadMacroTile<TilingTraits<TTileMode, 64>, R16G16B16A16_UINT, R32G32B32A32_FLOAT>::Load;
    table[R16G16B16A16_FLOAT]              = LoadMacroTile<TilingTraits<TTileMode, 64>, R16G16B16A16_FLOAT, R32G32B32A32_FLOAT>::Load;
    table[R32G32_FLOAT]                    = LoadMacroTile<TilingTraits<TTileMode, 64>, R32G32_FLOAT, R32G32B32A32_FLOAT>::Load;
    table[R32G32_SINT]                     = LoadMacroTile<TilingTraits<TTileMode, 64>, R32G32_SINT, R32G32B32A32_FLOAT>::Load;
    table[R32G32_UINT]                     = LoadMacroTile<TilingTraits<TTileMode, 64>, R32G32_UINT, R32G32B32A32_FLOAT>::Load;
    table[R32_FLOAT_X8X24_TYPELESS]        = LoadMacroTile<TilingTraits<TTileMode, 64>, R32_FLOAT_X8X24_TYPELESS, R32G32B32A32_FLOAT>::Load;
    table[X32_TYPELESS_G8X24_UINT]         = LoadMacroTile<TilingTraits<TTileMode, 64>, X32_TYPELESS_G8X24_UINT, R32G32B32A32_FLOAT>::Load;
    table[L32A32_FLOAT]                    = LoadMacroTile<TilingTraits<TTileMode, 64>, L32A32_FLOAT, R32G32B32A32_FLOAT>::Load;
    table[R16G16B16X16_UNORM]              = LoadMacroTile<TilingTraits<TTileMode, 64>, R16G16B16X16_UNORM, R32G32B32A32_FLOAT>::Load;
    table[R16G16B16X16_FLOAT]              = LoadMacroTile<TilingTraits<TTileMode, 64>, R16G16B16X16_FLOAT, R32G32B32A32_FLOAT>::Load;
    table[L32X32_FLOAT]                    = LoadMacroTile<TilingTraits<TTileMode, 64>, L32X32_FLOAT, R32G32B32A32_FLOAT>::Load;
    table[I32X32_FLOAT]                    = LoadMacroTile<TilingTraits<TTileMode, 64>, I32X32_FLOAT, R32G32B32A32_FLOAT>::Load;
    table[R16G16B16A16_SSCALED]            = LoadMacroTile<TilingTraits<TTileMode, 64>, R16G16B16A16_SSCALED, R32G32B32A32_FLOAT>::Load;
    table[R16G16B16A16_USCALED]            = LoadMacroTile<TilingTraits<TTileMode, 64>, R16G16B16A16_USCALED, R32G32B32A32_FLOAT>::Load;
    table[R32G32_SSCALED]                  = LoadMacroTile<TilingTraits<TTileMode, 64>, R32G32_SSCALED, R32G32B32A32_FLOAT>::Load;
    table[R32G32_USCALED]                  = LoadMacroTile<TilingTraits<TTileMode, 64>, R32G32_USCALED, R32G32B32A32_FLOAT>::Load;
    table[B8G8R8A8_UNORM]                  = LoadMacroTile<TilingTraits<TTileMode, 32>, B8G8R8A8_UNORM, R32G32B32A32_FLOAT>::Load;
    table[B8G8R8A8_UNORM_SRGB]             = LoadMacroTile<TilingTraits<TTileMode, 32>, B8G8R8A8_UNORM_SRGB, R32G32B32A32_FLOAT>::Load;
    table[R10G10B10A2_UNORM]               = LoadMacroTile<TilingTraits<TTileMode, 32>, R10G10B10A2_UNORM, R32G32B32A32_FLOAT>::Load;
    table[R10G10B10A2_UNORM_SRGB]          = LoadMacroTile<TilingTraits<TTileMode, 32>, R10G10B10A2_UNORM_SRGB, R32G32B32A32_FLOAT>::Load;
    table[R10G10B10A2_UINT]                = LoadMacroTile<TilingTraits<TTileMode, 32>, R10G10B10A2_UINT, R32G32B32A32_FLOAT>::Load;
    table[R8G8B8A8_UNORM]                  = LoadMacroTile<TilingTraits<TTileMode, 32>, R8G8B8A8_UNORM, R32G32B32A32_FLOAT>::Load;
    table[R8G8B8A8_UNORM_SRGB]             = LoadMacroTile<TilingTraits<TTileMode, 32>, R8G8B8A8_UNORM_SRGB, R32G32B32A32_FLOAT>::Load;
    table[R8G8B8A8_SNORM]                  = LoadMacroTile<TilingTraits<TTileMode, 32>, R8G8B8A8_SNORM, R32G32B32A32_FLOAT>::Load;
    table[R8G8B8A8_SINT]                   = LoadMacroTile<TilingTraits<TTileMode, 32>, R8G8B8A8_SINT, R32G32B32A32_FLOAT>::Load;
    table[R8G8B8A8_UINT]                   = LoadMacroTile<TilingTraits<TTileMode, 32>, R8G8B8A8_UINT, R32G32B32A32_FLOAT>::Load;
    table[R16G16_UNORM]                    = LoadMacroTile<TilingTraits<TTileMode, 32>, R16G16_UNORM, R32G32B32A32_FLOAT>::Load;
    table[R16G16_SNORM]                    = LoadMacroTile<TilingTraits<TTileMode, 32>, R16G16_SNORM, R32G32B32A32_FLOAT>::Load;
    table[R16G16_SINT]                     = LoadMacroTile<TilingTraits<TTileMode, 32>, R16G16_SINT, R32G32B32A32_FLOAT>::Load;
    table[R16G16_UINT]                     = LoadMacroTile<TilingTraits<TTileMode, 32>, R16G16_UINT, R32G32B32A32_FLOAT>::Load;
    table[R16G16_FLOAT]                    = LoadMacroTile<TilingTraits<TTileMode, 32>, R16G16_FLOAT, R32G32B32A32_FLOAT>::Load;
    table[B10G10R10A2_UNORM]               = LoadMacroTile<TilingTraits<TTileMode, 32>, B10G10R10A2_UNORM, R32G32B32A32_FLOAT>::Load;
    table[B10G10R10A2_UNORM_SRGB]          = LoadMacroTile<TilingTraits<TTileMode, 32>, B10G10R10A2_UNORM_SRGB, R32G32B32A32_FLOAT>::Load;
    table[R11G11B10_FLOAT]                 = LoadMacroTile<TilingTraits<TTileMode, 32>, R11G11B10_FLOAT, R32G32B32A32_FLOAT>::Load;
    table[R10G10B10_FLOAT_A2_UNORM]        = LoadMacroTile<TilingTraits<TTileMode, 32>, R10G10B10_FLOAT_A2_UNORM, R32G32B32A32_FLOAT>::Load;
    table[R32_SINT]                        = LoadMacroTile<TilingTraits<TTileMode, 32>, R32_SINT, R32G32B32A32_FLOAT>::Load;
    table[R32_UINT]                        = LoadMacroTile<TilingTraits<TTileMode, 32>, R32_UINT, R32G32B32A32_FLOAT>::Load;
    table[R32_FLOAT]                       = LoadMacroTile<TilingTraits<TTileMode, 32>, R32_FLOAT, R32G32B32A32_FLOAT>::Load;
    table[R24_UNORM_X8_TYPELESS]           = LoadMacroTile<TilingTraits<TTileMode, 32>, R24_UNORM_X8_TYPELESS, R32G32B32A32_FLOAT>::Load;
    table[X24_TYPELESS_G8_UINT]            = LoadMacroTile<TilingTraits<TTileMode, 32>, X24_TYPELESS_G8_UINT, R32G32B32A32_FLOAT>::Load;
    table[L32_UNORM]                       = LoadMacroTile<TilingTraits<TTileMode, 32>, L32_UNORM, R32G32B32A32_FLOAT>::Load;
    table[L16A16_UNORM]                    = LoadMacroTile<TilingTraits<TTileMode, 32>, L16A16_UNORM, R32G32B32A32_FLOAT>::Load;
    table[I24X8_UNORM]                     = LoadMacroTile<TilingTraits<TTileMode, 32>, I24X8_UNORM, R32G32B32A32_FLOAT>::Load;
    table[L24X8_UNORM]                     = LoadMacroTile<TilingTraits<TTileMode, 32>, L24X8_UNORM, R32G32B32A32_FLOAT>::Load;
    table[I32_FLOAT]                       = LoadMacroTile<TilingTraits<TTileMode, 32>, I32_FLOAT, R32G32B32A32_FLOAT>::Load;
    table[L32_FLOAT]                       = LoadMacroTile<TilingTraits<TTileMode, 32>, L32_FLOAT, R32G32B32A32_FLOAT>::Load;
    table[A32_FLOAT]                       = LoadMacroTile<TilingTraits<TTileMode, 32>, A32_FLOAT, R32G32B32A32_FLOAT>::Load;
    table[B8G8R8X8_UNORM]                  = LoadMacroTile<TilingTraits<TTileMode, 32>, B8G8R8X8_UNORM, R32G32B32A32_FLOAT>::Load;
    table[B8G8R8X8_UNORM_SRGB]             = LoadMacroTile<TilingTraits<TTileMode, 32>, B8G8R8X8_UNORM_SRGB, R32G32B32A32_FLOAT>::Load;
    table[R8G8B8X8_UNORM]                  = LoadMacroTile<TilingTraits<TTileMode, 32>, R8G8B8X8_UNORM, R32G32B32A32_FLOAT>::Load;
    table[R8G8B8X8_UNORM_SRGB]             = LoadMacroTile<TilingTraits<TTileMode, 32>, R8G8B8X8_UNORM_SRGB, R32G32B32A32_FLOAT>::Load;
    table[R9G9B9E5_SHAREDEXP]              = LoadMacroTile<TilingTraits<TTileMode, 32>, R9G9B9E5_SHAREDEXP, R32G32B32A32_FLOAT>::Load;
    table[B10G10R10X2_UNORM]               = LoadMacroTile<TilingTraits<TTileMode, 32>, B10G10R10X2_UNORM, R32G32B32A32_FLOAT>::Load;
    table[L16A16_FLOAT]                    = LoadMacroTile<TilingTraits<TTileMode, 32>, L16A16_FLOAT, R32G32B32A32_FLOAT>::Load;
    table[R10G10B10X2_USCALED]             = LoadMacroTile<TilingTraits<TTileMode, 32>, R10G10B10X2_USCALED, R32G32B32A32_FLOAT>::Load;
    table[R8G8B8A8_SSCALED]                = LoadMacroTile<TilingTraits<TTileMode, 32>, R8G8B8A8_SSCALED, R32G32B32A32_FLOAT>::Load;
    table[R8G8B8A8_USCALED]                = LoadMacroTile<TilingTraits<TTileMode, 32>, R8G8B8A8_USCALED, R32G32B32A32_FLOAT>::Load;
    table[R16G16_SSCALED]                  = LoadMacroTile<TilingTraits<TTileMode, 32>, R16G16_SSCALED, R32G32B32A32_FLOAT>::Load;
    table[R16G16_USCALED]                  = LoadMacroTile<TilingTraits<TTileMode, 32>, R16G16_USCALED, R32G32B32A32_FLOAT>::Load;
    table[R32_SSCALED]                     = LoadMacroTile<TilingTraits<TTileMode, 32>, R32_SSCALED, R32G32B32A32_FLOAT>::Load;
    table[R32_USCALED]                     = LoadMacroTile<TilingTraits<TTileMode, 32>, R32_USCALED, R32G32B32A32_FLOAT>::Load;
    table[B5G6R5_UNORM]                    = LoadMacroTile<TilingTraits<TTileMode, 16>, B5G6R5_UNORM, R32G32B32A32_FLOAT>::Load;
    table[B5G6R5_UNORM_SRGB]               = LoadMacroTile<TilingTraits<TTileMode, 16>, B5G6R5_UNORM_SRGB, R32G32B32A32_FLOAT>::Load;
    table[B5G5R5A1_UNORM]                  = LoadMacroTile<TilingTraits<TTileMode, 16>, B5G5R5A1_UNORM, R32G32B32A32_FLOAT>::Load;
    table[B5G5R5A1_UNORM_SRGB]             = LoadMacroTile<TilingTraits<TTileMode, 16>, B5G5R5A1_UNORM_SRGB, R32G32B32A32_FLOAT>::Load;
    table[B4G4R4A4_UNORM]                  = LoadMacroTile<TilingTraits<TTileMode, 16>, B4G4R4A4_UNORM, R32G32B32A32_FLOAT>::Load;
    table[B4G4R4A4_UNORM_SRGB]             = LoadMacroTile<TilingTraits<TTileMode, 16>, B4G4R4A4_UNORM_SRGB, R32G32B32A32_FLOAT>::Load;
    table[R8G8_UNORM]                      = LoadMacroTile<TilingTraits<TTileMode, 16>, R8G8_UNORM, R32G32B32A32_FLOAT>::Load;
    table[R8G8_SNORM]                      = LoadMacroTile<TilingTraits<TTileMode, 16>, R8G8_SNORM, R32G32B32A32_FLOAT>::Load;
    table[R8G8_SINT]                       = LoadMacroTile<TilingTraits<TTileMode, 16>, R8G8_SINT, R32G32B32A32_FLOAT>::Load;
    table[R8G8_UINT]                       = LoadMacroTile<TilingTraits<TTileMode, 16>, R8G8_UINT, R32G32B32A32_FLOAT>::Load;
    table[R16_UNORM]                       = LoadMacroTile<TilingTraits<TTileMode, 16>, R16_UNORM, R32G32B32A32_FLOAT>::Load;
    table[R16_SNORM]                       = LoadMacroTile<TilingTraits<TTileMode, 16>, R16_SNORM, R32G32B32A32_FLOAT>::Load;
    table[R16_SINT]                        = LoadMacroTile<TilingTraits<TTileMode, 16>, R16_SINT, R32G32B32A32_FLOAT>::Load;
    table[R16_UINT]                        = LoadMacroTile<TilingTraits<TTileMode, 16>, R16_UINT, R32G32B32A32_FLOAT>::Load;
    table[R16_FLOAT]                       = LoadMacroTile<TilingTraits<TTileMode, 16>, R16_FLOAT, R32G32B32A32_FLOAT>::Load;
    table[I16_UNORM]                       = LoadMacroTile<TilingTraits<TTileMode, 16>, I16_UNORM, R32G32B32A32_FLOAT>::Load;
    table[L16_UNORM]                       = LoadMacroTile<TilingTraits<TTileMode, 16>, L16_UNORM, R32G32B32A32_FLOAT>::Load;
    table[A16_UNORM]                       = LoadMacroTile<TilingTraits<TTileMode, 16>, A16_UNORM, R32G32B32A32_FLOAT>::Load;
    table[L8A8_UNORM]                      = LoadMacroTile<TilingTraits<TTileMode, 16>, L8A8_UNORM, R32G32B32A32_FLOAT>::Load;
    table[I16_FLOAT]                       = LoadMacroTile<TilingTraits<TTileMode, 16>, I16_FLOAT, R32G32B32A32_FLOAT>::Load;
    table[L16_FLOAT]                       = LoadMacroTile<TilingTraits<TTileMode, 16>, L16_FLOAT, R32G32B32A32_FLOAT>::Load;
    table[A16_FLOAT]                       = LoadMacroTile<TilingTraits<TTileMode, 16>, A16_FLOAT, R32G32B32A32_FLOAT>::Load;
    table[L8A8_UNORM_SRGB]                 = LoadMacroTile<TilingTraits<TTileMode, 16>, L8A8_UNORM_SRGB, R32G32B32A32_FLOAT>::Load;
    table[B5G5R5X1_UNORM]                  = LoadMacroTile<TilingTraits<TTileMode, 16>, B5G5R5X1_UNORM, R32G32B32A32_FLOAT>::Load;
    table[B5G5R5X1_UNORM_SRGB]             = LoadMacroTile<TilingTraits<TTileMode, 16>, B5G5R5X1_UNORM_SRGB, R32G32B32A32_FLOAT>::Load;
    table[R8G8_SSCALED]                    = LoadMacroTile<TilingTraits<TTileMode, 16>, R8G8_SSCALED, R32G32B32A32_FLOAT>::Load;
    table[R8G8_USCALED]                    = LoadMacroTile<TilingTraits<TTileMode, 16>, R8G8_USCALED, R32G32B32A32_FLOAT>::Load;
    table[R16_SSCALED]                     = LoadMacroTile<TilingTraits<TTileMode, 16>, R16_SSCALED, R32G32B32A32_FLOAT>::Load;
    table[R16_USCALED]                     = LoadMacroTile<TilingTraits<TTileMode, 16>, R16_USCALED, R32G32B32A32_FLOAT>::Load;
    table[A1B5G5R5_UNORM]                  = LoadMacroTile<TilingTraits<TTileMode, 16>, A1B5G5R5_UNORM, R32G32B32A32_FLOAT>::Load;
    table[A4B4G4R4_UNORM]                  = LoadMacroTile<TilingTraits<TTileMode, 16>, A4B4G4R4_UNORM, R32G32B32A32_FLOAT>::Load;
    table[L8A8_UINT]                       = LoadMacroTile<TilingTraits<TTileMode, 16>, L8A8_UINT, R32G32B32A32_FLOAT>::Load;
    table[L8A8_SINT]                       = LoadMacroTile<TilingTraits<TTileMode, 16>, L8A8_SINT, R32G32B32A32_FLOAT>::Load;
    table[R8_UNORM]                        = LoadMacroTile<TilingTraits<TTileMode, 8>, R8_UNORM, R32G32B32A32_FLOAT>::Load;
    table[R8_SNORM]                        = LoadMacroTile<TilingTraits<TTileMode, 8>, R8_SNORM, R32G32B32A32_FLOAT>::Load;
    table[R8_SINT]                         = LoadMacroTile<TilingTraits<TTileMode, 8>, R8_SINT, R32G32B32A32_FLOAT>::Load;
    table[R8_UINT]                         = LoadMacroTile<TilingTraits<TTileMode, 8>, R8_UINT, R32G32B32A32_FLOAT>::Load;
    table[A8_UNORM]                        = LoadMacroTile<TilingTraits<TTileMode, 8>, A8_UNORM, R32G32B32A32_FLOAT>::Load;
    table[I8_UNORM]                        = LoadMacroTile<TilingTraits<TTileMode, 8>, I8_UNORM, R32G32B32A32_FLOAT>::Load;
    table[L8_UNORM]                        = LoadMacroTile<TilingTraits<TTileMode, 8>, L8_UNORM, R32G32B32A32_FLOAT>::Load;
    table[R8_SSCALED]                      = LoadMacroTile<TilingTraits<TTileMode, 8>, R8_SSCALED, R32G32B32A32_FLOAT>::Load;
    table[R8_USCALED]                      = LoadMacroTile<TilingTraits<TTileMode, 8>, R8_USCALED, R32G32B32A32_FLOAT>::Load;
    table[L8_UNORM_SRGB]                   = LoadMacroTile<TilingTraits<TTileMode, 8>, L8_UNORM_SRGB, R32G32B32A32_FLOAT>::Load;
    table[L8_UINT]                         = LoadMacroTile<TilingTraits<TTileMode, 8>, L8_UINT, R32G32B32A32_FLOAT>::Load;
    table[L8_SINT]                         = LoadMacroTile<TilingTraits<TTileMode, 8>, L8_SINT, R32G32B32A32_FLOAT>::Load;
    table[I8_UINT]                         = LoadMacroTile<TilingTraits<TTileMode, 8>, I8_UINT, R32G32B32A32_FLOAT>::Load;
    table[I8_SINT]                         = LoadMacroTile<TilingTraits<TTileMode, 8>, I8_SINT, R32G32B32A32_FLOAT>::Load;
    table[YCRCB_SWAPUVY]                   = LoadMacroTile<TilingTraits<TTileMode, 32>, YCRCB_SWAPUVY, R32G32B32A32_FLOAT>::Load;
    table[BC1_UNORM]                       = LoadMacroTile<TilingTraits<TTileMode, 64>, BC1_UNORM, R32G32B32A32_FLOAT>::Load;
    table[BC2_UNORM]                       = LoadMacroTile<TilingTraits<TTileMode, 128>, BC2_UNORM, R32G32B32A32_FLOAT>::Load;
    table[BC3_UNORM]                       = LoadMacroTile<TilingTraits<TTileMode, 128>, BC3_UNORM, R32G32B32A32_FLOAT>::Load;
    table[BC4_UNORM]                       = LoadMacroTile<TilingTraits<TTileMode, 64>, BC4_UNORM, R32G32B32A32_FLOAT>::Load;
    table[BC5_UNORM]                       = LoadMacroTile<TilingTraits<TTileMode, 128>, BC5_UNORM, R32G32B32A32_FLOAT>::Load;
    table[BC1_UNORM_SRGB]                  = LoadMacroTile<TilingTraits<TTileMode, 64>, BC1_UNORM_SRGB, R32G32B32A32_FLOAT>::Load;
    table[BC2_UNORM_SRGB]                  = LoadMacroTile<TilingTraits<TTileMode, 128>, BC2_UNORM_SRGB, R32G32B32A32_FLOAT>::Load;
    table[BC3_UNORM_SRGB]                  = LoadMacroTile<TilingTraits<TTileMode, 128>, BC3_UNORM_SRGB, R32G32B32A32_FLOAT>::Load;
    table[YCRCB_SWAPUV]                    = LoadMacroTile<TilingTraits<TTileMode, 32>, YCRCB_SWAPUV, R32G32B32A32_FLOAT>::Load;
    table[R8G8B8_UNORM]                    = LoadMacroTile<TilingTraits<TTileMode, 24>, R8G8B8_UNORM, R32G32B32A32_FLOAT>::Load;
    table[R8G8B8_SNORM]                    = LoadMacroTile<TilingTraits<TTileMode, 24>, R8G8B8_SNORM, R32G32B32A32_FLOAT>::Load;
    table[R8G8B8_SSCALED]                  = LoadMacroTile<TilingTraits<TTileMode, 24>, R8G8B8_SSCALED, R32G32B32A32_FLOAT>::Load;
    table[R8G8B8_USCALED]                  = LoadMacroTile<TilingTraits<TTileMode, 24>, R8G8B8_USCALED, R32G32B32A32_FLOAT>::Load;
    table[BC4_SNORM]                       = LoadMacroTile<TilingTraits<TTileMode, 64>, BC4_SNORM, R32G32B32A32_FLOAT>::Load;
    table[BC5_SNORM]                       = LoadMacroTile<TilingTraits<TTileMode, 128>, BC5_SNORM, R32G32B32A32_FLOAT>::Load;
    table[R16G16B16_FLOAT]                 = LoadMacroTile<TilingTraits<TTileMode, 48>, R16G16B16_FLOAT, R32G32B32A32_FLOAT>::Load;
    table[R16G16B16_UNORM]                 = LoadMacroTile<TilingTraits<TTileMode, 48>, R16G16B16_UNORM, R32G32B32A32_FLOAT>::Load;
    table[R16G16B16_SNORM]                 = LoadMacroTile<TilingTraits<TTileMode, 48>, R16G16B16_SNORM, R32G32B32A32_FLOAT>::Load;
    table[R16G16B16_SSCALED]               = LoadMacroTile<TilingTraits<TTileMode, 48>, R16G16B16_SSCALED, R32G32B32A32_FLOAT>::Load;
    table[R16G16B16_USCALED]               = LoadMacroTile<TilingTraits<TTileMode, 48>, R16G16B16_USCALED, R32G32B32A32_FLOAT>::Load;
    table[BC6H_SF16]                       = LoadMacroTile<TilingTraits<TTileMode, 128>, BC6H_SF16, R32G32B32A32_FLOAT>::Load;
    table[BC7_UNORM]                       = LoadMacroTile<TilingTraits<TTileMode, 128>, BC7_UNORM, R32G32B32A32_FLOAT>::Load;
    table[BC7_UNORM_SRGB]                  = LoadMacroTile<TilingTraits<TTileMode, 128>, BC7_UNORM_SRGB, R32G32B32A32_FLOAT>::Load;
    table[BC6H_UF16]                       = LoadMacroTile<TilingTraits<TTileMode, 128>, BC6H_UF16, R32G32B32A32_FLOAT>::Load;
    table[R8G8B8_UNORM_SRGB]               = LoadMacroTile<TilingTraits<TTileMode, 24>, R8G8B8_UNORM_SRGB, R32G32B32A32_FLOAT>::Load;
    table[R16G16B16_UINT]                  = LoadMacroTile<TilingTraits<TTileMode, 48>, R16G16B16_UINT, R32G32B32A32_FLOAT>::Load;
    table[R16G16B16_SINT]                  = LoadMacroTile<TilingTraits<TTileMode, 48>, R16G16B16_SINT, R32G32B32A32_FLOAT>::Load;
    table[R10G10B10A2_SNORM]               = LoadMacroTile<TilingTraits<TTileMode, 32>, R10G10B10A2_SNORM, R32G32B32A32_FLOAT>::Load;
    table[R10G10B10A2_USCALED]             = LoadMacroTile<TilingTraits<TTileMode, 32>, R10G10B10A2_USCALED, R32G32B32A32_FLOAT>::Load;
    table[R10G10B10A2_SSCALED]             = LoadMacroTile<TilingTraits<TTileMode, 32>, R10G10B10A2_SSCALED, R32G32B32A32_FLOAT>::Load;
    table[R10G10B10A2_SINT]                = LoadMacroTile<TilingTraits<TTileMode, 32>, R10G10B10A2_SINT, R32G32B32A32_FLOAT>::Load;
    table[B10G10R10A2_SNORM]               = LoadMacroTile<TilingTraits<TTileMode, 32>, B10G10R10A2_SNORM, R32G32B32A32_FLOAT>::Load;
    table[B10G10R10A2_USCALED]             = LoadMacroTile<TilingTraits<TTileMode, 32>, B10G10R10A2_USCALED, R32G32B32A32_FLOAT>::Load;
    table[B10G10R10A2_SSCALED]             = LoadMacroTile<TilingTraits<TTileMode, 32>, B10G10R10A2_SSCALED, R32G32B32A32_FLOAT>::Load;
    table[B10G10R10A2_UINT]                = LoadMacroTile<TilingTraits<TTileMode, 32>, B10G10R10A2_UINT, R32G32B32A32_FLOAT>::Load;
    table[B10G10R10A2_SINT]                = LoadMacroTile<TilingTraits<TTileMode, 32>, B10G10R10A2_SINT, R32G32B32A32_FLOAT>::Load;
    table[R8G8B8_UINT]                     = LoadMacroTile<TilingTraits<TTileMode, 24>, R8G8B8_UINT, R32G32B32A32_FLOAT>::Load;
    table[R8G8B8_SINT]                     = LoadMacroTile<TilingTraits<TTileMode, 24>, R8G8B8_SINT, R32G32B32A32_FLOAT>::Load;
    table[RAW]                             = LoadMacroTile<TilingTraits<TTileMode, 8>, RAW, R32G32B32A32_FLOAT>::Load;
}

//////////////////////////////////////////////////////////////////////////
/// InitLoadTileColorTable - Helper function for setting up the tables.
template<SWR_TILE_MODE TTileMode>
static INLINE void InitLoadTileDepthTable(PFN_LOAD_TILES(&table)[NUM_SWR_FORMATS])
{
    memset(table, 0, sizeof(table));

   table[R32_FLOAT]                       = LoadMacroTile<TilingTraits<TTileMode, 32>, R32_FLOAT, R32_FLOAT>::Load;
   table[R32_FLOAT_X8X24_TYPELESS]        = LoadMacroTile<TilingTraits<TTileMode, 64>, R32_FLOAT_X8X24_TYPELESS, R32_FLOAT>::Load;
   table[R24_UNORM_X8_TYPELESS]           = LoadMacroTile<TilingTraits<TTileMode, 32>, R24_UNORM_X8_TYPELESS, R32_FLOAT>::Load;
   table[R16_UNORM]                       = LoadMacroTile<TilingTraits<TTileMode, 16>, R16_UNORM, R32_FLOAT>::Load;
}


//////////////////////////////////////////////////////////////////////////
/// @brief Loads a full hottile from a render surface
/// @param hPrivateContext - Handle to private DC
/// @param dstFormat - Format for hot tile.
/// @param renderTargetIndex - Index to src render target
/// @param x, y - Coordinates to raster tile.
/// @param pDstHotTile - Pointer to Hot Tile
void SwrLoadHotTile(
        HANDLE hWorkerPrivateData,
        const SWR_SURFACE_STATE *pSrcSurface,
        BucketManager* pBucketMgr,
        SWR_FORMAT dstFormat,
        SWR_RENDERTARGET_ATTACHMENT renderTargetIndex,
        uint32_t x, uint32_t y, uint32_t renderTargetArrayIndex,
        uint8_t *pDstHotTile);
