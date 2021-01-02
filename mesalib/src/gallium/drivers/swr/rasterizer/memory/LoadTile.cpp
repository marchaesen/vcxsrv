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
* @file LoadTile.cpp
* 
* @brief Functionality for Load
* 
******************************************************************************/
#include "LoadTile.h"

// on demand buckets for load tiles
static std::vector<int> sBuckets(NUM_SWR_FORMATS, -1);
static std::mutex sBucketMutex;

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
    uint8_t *pDstHotTile)
{
    PFN_LOAD_TILES pfnLoadTiles = NULL;

    // don't need to load null surfaces
    if (pSrcSurface->type == SURFACE_NULL)
    {
        return;
    }

    // force 0 if requested renderTargetArrayIndex is OOB
    if (renderTargetArrayIndex >= pSrcSurface->depth)
    {
        renderTargetArrayIndex = 0;
    }

    if (renderTargetIndex < SWR_ATTACHMENT_DEPTH)
    {
        switch (pSrcSurface->tileMode)
        {
        case SWR_TILE_NONE:
            pfnLoadTiles = sLoadTilesColorTable_SWR_TILE_NONE[pSrcSurface->format];
            break;
        case SWR_TILE_MODE_YMAJOR:
            pfnLoadTiles = sLoadTilesColorTable_SWR_TILE_MODE_YMAJOR[pSrcSurface->format];
            break;
        case SWR_TILE_MODE_XMAJOR:
            pfnLoadTiles = sLoadTilesColorTable_SWR_TILE_MODE_XMAJOR[pSrcSurface->format];
            break;
        case SWR_TILE_MODE_WMAJOR:
            SWR_ASSERT(pSrcSurface->format == R8_UINT);
            pfnLoadTiles = LoadMacroTile<TilingTraits<SWR_TILE_MODE_WMAJOR, 8>, R8_UINT, R8_UINT>::Load;
            break;
        default:
            SWR_INVALID("Unsupported tiling mode");
            break;
        }
    }
    else if (renderTargetIndex == SWR_ATTACHMENT_DEPTH)
    {
        // Currently depth can map to linear and tile-y.
        switch (pSrcSurface->tileMode)
        {
        case SWR_TILE_NONE:
            pfnLoadTiles = sLoadTilesDepthTable_SWR_TILE_NONE[pSrcSurface->format];
            break;
        case SWR_TILE_MODE_YMAJOR:
            pfnLoadTiles = sLoadTilesDepthTable_SWR_TILE_MODE_YMAJOR[pSrcSurface->format];
            break;
        default:
            SWR_INVALID("Unsupported tiling mode");
            break;
        }
    }
    else
    {
        SWR_ASSERT(renderTargetIndex == SWR_ATTACHMENT_STENCIL);
        SWR_ASSERT(pSrcSurface->format == R8_UINT);
        switch (pSrcSurface->tileMode)
        {
        case SWR_TILE_NONE:
            pfnLoadTiles = LoadMacroTile<TilingTraits<SWR_TILE_NONE, 8>, R8_UINT, R8_UINT>::Load;
            break;
        case SWR_TILE_MODE_WMAJOR:
            pfnLoadTiles = LoadMacroTile<TilingTraits<SWR_TILE_MODE_WMAJOR, 8>, R8_UINT, R8_UINT>::Load;
            break;
        default:
            SWR_INVALID("Unsupported tiling mode");
            break;
        }
    }

    if (pfnLoadTiles == nullptr)
    {
        SWR_INVALID("Unsupported format for load tile");
        return;
    }

    // Load a macro tile.
#ifdef KNOB_ENABLE_RDTSC
    if (sBuckets[pSrcSurface->format] == -1)
    {
        // guard sBuckets update since storetiles is called by multiple threads
        sBucketMutex.lock();
        if (sBuckets[pSrcSurface->format] == -1)
        {
            const SWR_FORMAT_INFO& info = GetFormatInfo(pSrcSurface->format);
            BUCKET_DESC desc{ info.name, "", false, 0xffffffff };
            sBuckets[pSrcSurface->format] = pBucketMgr->RegisterBucket(desc);
        }
        sBucketMutex.unlock();
    }
#endif

#ifdef KNOB_ENABLE_RDTSC
    pBucketMgr->StartBucket(sBuckets[pSrcSurface->format]);
#endif
    pfnLoadTiles(pSrcSurface, pDstHotTile, x, y, renderTargetArrayIndex);
#ifdef KNOB_ENABLE_RDTSC
    pBucketMgr->StopBucket(sBuckets[pSrcSurface->format]);
#endif
}


void InitSimLoadTilesTable()
{
    InitLoadTilesTable_Linear();
    InitLoadTilesTable_XMajor();
    InitLoadTilesTable_YMajor();
}
