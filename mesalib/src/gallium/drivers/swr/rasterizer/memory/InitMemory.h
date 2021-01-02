/****************************************************************************
* Copyright (C) 2018 Intel Corporation.   All Rights Reserved.
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
* @file InitMemory.h
*
* @brief Provide access to tiles table initialization functions
*
******************************************************************************/

#pragma once

#include "common/os.h"
#include "memory/SurfaceState.h"

//////////////////////////////////////////////////////////////////////////
/// @brief Loads a full hottile from a render surface
/// @param hPrivateContext - Handle to private DC
/// @param dstFormat - Format for hot tile.
/// @param renderTargetIndex - Index to src render target
/// @param x, y - Coordinates to raster tile.
/// @param pDstHotTile - Pointer to Hot Tile
SWR_FUNC(void,
         SwrLoadHotTile,
         HANDLE                      hWorkerPrivateData,
         const SWR_SURFACE_STATE*    pSrcSurface,
         BucketManager*              pBucketManager,
         SWR_FORMAT                  dstFormat,
         SWR_RENDERTARGET_ATTACHMENT renderTargetIndex,
         uint32_t                    x,
         uint32_t                    y,
         uint32_t                    renderTargetArrayIndex,
         uint8_t*                    pDstHotTile);

//////////////////////////////////////////////////////////////////////////
/// @brief Deswizzles and stores a full hottile to a render surface
/// @param hPrivateContext - Handle to private DC
/// @param srcFormat - Format for hot tile.
/// @param renderTargetIndex - Index to destination render target
/// @param x, y - Coordinates to raster tile.
/// @param pSrcHotTile - Pointer to Hot Tile
SWR_FUNC(void,
         SwrStoreHotTileToSurface,
         HANDLE                      hWorkerPrivateData,
         SWR_SURFACE_STATE*          pDstSurface,
         BucketManager*              pBucketManager,
         SWR_FORMAT                  srcFormat,
         SWR_RENDERTARGET_ATTACHMENT renderTargetIndex,
         uint32_t                    x,
         uint32_t                    y,
         uint32_t                    renderTargetArrayIndex,
         uint8_t*                    pSrcHotTile);

struct SWR_TILE_INTERFACE {
    PFNSwrLoadHotTile           pfnSwrLoadHotTile;
    PFNSwrStoreHotTileToSurface pfnSwrStoreHotTileToSurface;
};

extern "C"
{
    SWR_VISIBLE void SWR_API InitTilesTable();

    typedef void(SWR_API* PFNSwrGetTileInterface)(SWR_TILE_INTERFACE& out_funcs);
    SWR_VISIBLE void SWR_API SwrGetTileIterface(SWR_TILE_INTERFACE &out_funcs);
}
