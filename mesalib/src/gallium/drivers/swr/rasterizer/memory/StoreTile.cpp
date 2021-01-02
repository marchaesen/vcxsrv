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
* @file StoreTile.cpp
* 
* @brief Functionality for Store.
* 
******************************************************************************/
#include "StoreTile.h"
//////////////////////////////////////////////////////////////////////////
/// Store Raster Tile Function Tables.
//////////////////////////////////////////////////////////////////////////
PFN_STORE_TILES sStoreTilesTableColor[SWR_TILE_MODE_COUNT][NUM_SWR_FORMATS] = {};
PFN_STORE_TILES sStoreTilesTableDepth[SWR_TILE_MODE_COUNT][NUM_SWR_FORMATS] = {};
PFN_STORE_TILES sStoreTilesTableStencil[SWR_TILE_MODE_COUNT][NUM_SWR_FORMATS] = {};

// on demand buckets for store tiles
static std::mutex sBucketMutex;
static std::vector<int32_t> sBuckets(NUM_SWR_FORMATS, -1);

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
    uint8_t *pSrcHotTile)
{
    if (pDstSurface->type == SURFACE_NULL)
    {
        return;
    }

    // force 0 if requested renderTargetArrayIndex is OOB
    if (renderTargetArrayIndex >= pDstSurface->depth)
    {
        renderTargetArrayIndex = 0;
    }

    PFN_STORE_TILES pfnStoreTiles = nullptr;

    if (renderTargetIndex <= SWR_ATTACHMENT_COLOR7)
    {
        pfnStoreTiles = sStoreTilesTableColor[pDstSurface->tileMode][pDstSurface->format];
    }
    else if (renderTargetIndex == SWR_ATTACHMENT_DEPTH)
    {
        pfnStoreTiles = sStoreTilesTableDepth[pDstSurface->tileMode][pDstSurface->format];
    }
    else
    {
        pfnStoreTiles = sStoreTilesTableStencil[pDstSurface->tileMode][pDstSurface->format];
    }

    if(nullptr == pfnStoreTiles)
    {
        SWR_INVALID("Invalid pixel format / tile mode for store tiles");
        return;
    }

    // Store a macro tile
#ifdef KNOB_ENABLE_RDTSC
    if (sBuckets[pDstSurface->format] == -1)
    {
        // guard sBuckets update since storetiles is called by multiple threads
        sBucketMutex.lock();
        if (sBuckets[pDstSurface->format] == -1)
        {
            const SWR_FORMAT_INFO& info = GetFormatInfo(pDstSurface->format);
            BUCKET_DESC desc{info.name, "", false, 0xffffffff};
            sBuckets[pDstSurface->format] = pBucketMgr->RegisterBucket(desc);
        }
        sBucketMutex.unlock();
    }
#endif

#ifdef KNOB_ENABLE_RDTSC
    pBucketMgr->StartBucket(sBuckets[pDstSurface->format]);
#endif
    pfnStoreTiles(pSrcHotTile, pDstSurface, x, y, renderTargetArrayIndex);
#ifdef KNOB_ENABLE_RDTSC
    pBucketMgr->StopBucket(sBuckets[pDstSurface->format]);
#endif

}


//////////////////////////////////////////////////////////////////////////
/// @brief Sets up tables for StoreTile
void InitSimStoreTilesTable()
{
    memset(sStoreTilesTableColor, 0, sizeof(sStoreTilesTableColor));
    memset(sStoreTilesTableDepth, 0, sizeof(sStoreTilesTableDepth));

    InitStoreTilesTable_Linear_1();
    InitStoreTilesTable_Linear_2();
    InitStoreTilesTable_TileX_1();
    InitStoreTilesTable_TileX_2();
    InitStoreTilesTable_TileY_1();
    InitStoreTilesTable_TileY_2();
    InitStoreTilesTable_TileW();
}
