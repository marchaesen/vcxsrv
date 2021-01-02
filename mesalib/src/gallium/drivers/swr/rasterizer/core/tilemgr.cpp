/****************************************************************************
 * Copyright (C) 2014-2018 Intel Corporation.   All Rights Reserved.
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
 * @file tilemgr.cpp
 *
 * @brief Implementation for Macro Tile Manager which provides the facilities
 *        for threads to work on an macro tile.
 *
 ******************************************************************************/
#include <unordered_map>

#include "fifo.hpp"
#include "core/tilemgr.h"
#include "core/multisample.h"
#include "rdtsc_core.h"

MacroTileMgr::MacroTileMgr(CachingArena& arena) : mArena(arena) {}

void MacroTileMgr::enqueue(uint32_t x, uint32_t y, BE_WORK* pWork)
{
    // Should not enqueue more then what we have backing for in the hot tile manager.
    SWR_ASSERT(x < KNOB_NUM_HOT_TILES_X);
    SWR_ASSERT(y < KNOB_NUM_HOT_TILES_Y);

    if ((x & ~(KNOB_NUM_HOT_TILES_X - 1)) | (y & ~(KNOB_NUM_HOT_TILES_Y - 1)))
    {
        return;
    }

    uint32_t id = getTileId(x, y);

    if (id >= mTiles.size())
    {
        mTiles.resize((16 + id) * 2);
    }

    MacroTileQueue* pTile = mTiles[id];
    if (!pTile)
    {
        pTile = mTiles[id] = new MacroTileQueue();
    }
    pTile->mWorkItemsFE++;
    pTile->mId = id;

    if (pTile->mWorkItemsFE == 1)
    {
        pTile->clear(mArena);
        mDirtyTiles.push_back(pTile);
    }

    mWorkItemsProduced++;
    pTile->enqueue_try_nosync(mArena, pWork);
}

void MacroTileMgr::markTileComplete(uint32_t id)
{
    SWR_ASSERT(mTiles.size() > id);
    MacroTileQueue& tile     = *mTiles[id];
    uint32_t        numTiles = tile.mWorkItemsFE;
    InterlockedExchangeAdd(&mWorkItemsConsumed, numTiles);

    _ReadWriteBarrier();
    tile.mWorkItemsBE += numTiles;
    SWR_ASSERT(tile.mWorkItemsFE == tile.mWorkItemsBE);

    // clear out tile, but defer fifo clear until the next DC first queues to it.
    // this prevents worker threads from constantly locking a completed macro tile
    tile.mWorkItemsFE = 0;
    tile.mWorkItemsBE = 0;
}

HOTTILE* HotTileMgr::GetHotTile(SWR_CONTEXT*                pContext,
                                DRAW_CONTEXT*               pDC,
                                HANDLE                      hWorkerPrivateData,
                                uint32_t                    macroID,
                                SWR_RENDERTARGET_ATTACHMENT attachment,
                                bool                        create,
                                uint32_t                    numSamples,
                                uint32_t                    renderTargetArrayIndex)
{
    uint32_t x, y;
    MacroTileMgr::getTileIndices(macroID, x, y);

    SWR_ASSERT(x < KNOB_NUM_HOT_TILES_X);
    SWR_ASSERT(y < KNOB_NUM_HOT_TILES_Y);

    HotTileSet& tile    = mHotTiles[x][y];
    HOTTILE&    hotTile = tile.Attachment[attachment];
    if (hotTile.pBuffer == NULL)
    {
        if (create)
        {
            uint32_t size     = numSamples * mHotTileSize[attachment];
            uint32_t numaNode = ((x ^ y) & pContext->threadPool.numaMask);
            hotTile.pBuffer =
                (uint8_t*)AllocHotTileMem(size, 64, numaNode + pContext->threadInfo.BASE_NUMA_NODE);
            hotTile.state                  = HOTTILE_INVALID;
            hotTile.numSamples             = numSamples;
            hotTile.renderTargetArrayIndex = renderTargetArrayIndex;
        }
        else
        {
            return NULL;
        }
    }
    else
    {
        // free the old tile and create a new one with enough space to hold all samples
        if (numSamples > hotTile.numSamples)
        {
            // tile should be either uninitialized or resolved if we're deleting and switching to a
            // new sample count
            SWR_ASSERT((hotTile.state == HOTTILE_INVALID) || (hotTile.state == HOTTILE_RESOLVED) ||
                       (hotTile.state == HOTTILE_CLEAR));
            FreeHotTileMem(hotTile.pBuffer);

            uint32_t size     = numSamples * mHotTileSize[attachment];
            uint32_t numaNode = ((x ^ y) & pContext->threadPool.numaMask);
            hotTile.pBuffer =
                (uint8_t*)AllocHotTileMem(size, 64, numaNode + pContext->threadInfo.BASE_NUMA_NODE);
            hotTile.state      = HOTTILE_INVALID;
            hotTile.numSamples = numSamples;
        }

        // if requested render target array index isn't currently loaded, need to store out the
        // current hottile and load the requested array slice
        if (renderTargetArrayIndex != hotTile.renderTargetArrayIndex)
        {
            SWR_FORMAT format;
            switch (attachment)
            {
            case SWR_ATTACHMENT_COLOR0:
            case SWR_ATTACHMENT_COLOR1:
            case SWR_ATTACHMENT_COLOR2:
            case SWR_ATTACHMENT_COLOR3:
            case SWR_ATTACHMENT_COLOR4:
            case SWR_ATTACHMENT_COLOR5:
            case SWR_ATTACHMENT_COLOR6:
            case SWR_ATTACHMENT_COLOR7:
                format = KNOB_COLOR_HOT_TILE_FORMAT;
                break;
            case SWR_ATTACHMENT_DEPTH:
                format = KNOB_DEPTH_HOT_TILE_FORMAT;
                break;
            case SWR_ATTACHMENT_STENCIL:
                format = KNOB_STENCIL_HOT_TILE_FORMAT;
                break;
            default:
                SWR_INVALID("Unknown attachment: %d", attachment);
                format = KNOB_COLOR_HOT_TILE_FORMAT;
                break;
            }

            if (hotTile.state == HOTTILE_CLEAR)
            {
                if (attachment == SWR_ATTACHMENT_STENCIL)
                    ClearStencilHotTile(&hotTile);
                else if (attachment == SWR_ATTACHMENT_DEPTH)
                    ClearDepthHotTile(&hotTile);
                else
                    ClearColorHotTile(&hotTile);

                hotTile.state = HOTTILE_DIRTY;
            }

            if (hotTile.state == HOTTILE_DIRTY)
            {
                pContext->pfnStoreTile(pDC,
                                       hWorkerPrivateData,
                                       format,
                                       attachment,
                                       x * KNOB_MACROTILE_X_DIM,
                                       y * KNOB_MACROTILE_Y_DIM,
                                       hotTile.renderTargetArrayIndex,
                                       hotTile.pBuffer);
            }

            pContext->pfnLoadTile(pDC,
                                  hWorkerPrivateData,
                                  format,
                                  attachment,
                                  x * KNOB_MACROTILE_X_DIM,
                                  y * KNOB_MACROTILE_Y_DIM,
                                  renderTargetArrayIndex,
                                  hotTile.pBuffer);

            hotTile.renderTargetArrayIndex = renderTargetArrayIndex;
            hotTile.state = HOTTILE_RESOLVED;
        }
    }
    return &tile.Attachment[attachment];
}

HOTTILE* HotTileMgr::GetHotTileNoLoad(SWR_CONTEXT*                pContext,
                                      DRAW_CONTEXT*               pDC,
                                      uint32_t                    macroID,
                                      SWR_RENDERTARGET_ATTACHMENT attachment,
                                      bool                        create,
                                      uint32_t                    numSamples)
{
    uint32_t x, y;
    MacroTileMgr::getTileIndices(macroID, x, y);

    SWR_ASSERT(x < KNOB_NUM_HOT_TILES_X);
    SWR_ASSERT(y < KNOB_NUM_HOT_TILES_Y);

    HotTileSet& tile    = mHotTiles[x][y];
    HOTTILE&    hotTile = tile.Attachment[attachment];
    if (hotTile.pBuffer == NULL)
    {
        if (create)
        {
            uint32_t size                  = numSamples * mHotTileSize[attachment];
            hotTile.pBuffer                = (uint8_t*)AlignedMalloc(size, 64);
            hotTile.state                  = HOTTILE_INVALID;
            hotTile.numSamples             = numSamples;
            hotTile.renderTargetArrayIndex = 0;
        }
        else
        {
            return NULL;
        }
    }

    return &hotTile;
}

void HotTileMgr::ClearColorHotTile(
    const HOTTILE* pHotTile) // clear a macro tile from float4 clear data.
{
    // Load clear color into SIMD register...
    float*       pClearData = (float*)(pHotTile->clearData);
    simd16scalar valR       = _simd16_broadcast_ss(&pClearData[0]);
    simd16scalar valG       = _simd16_broadcast_ss(&pClearData[1]);
    simd16scalar valB       = _simd16_broadcast_ss(&pClearData[2]);
    simd16scalar valA       = _simd16_broadcast_ss(&pClearData[3]);

    float*   pfBuf      = (float*)pHotTile->pBuffer;
    uint32_t numSamples = pHotTile->numSamples;

    for (uint32_t row = 0; row < KNOB_MACROTILE_Y_DIM; row += KNOB_TILE_Y_DIM)
    {
        for (uint32_t col = 0; col < KNOB_MACROTILE_X_DIM; col += KNOB_TILE_X_DIM)
        {
            for (uint32_t si = 0; si < (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * numSamples);
                 si += SIMD16_TILE_X_DIM * SIMD16_TILE_Y_DIM)
            {
                _simd16_store_ps(pfBuf, valR);
                pfBuf += KNOB_SIMD16_WIDTH;

                _simd16_store_ps(pfBuf, valG);
                pfBuf += KNOB_SIMD16_WIDTH;

                _simd16_store_ps(pfBuf, valB);
                pfBuf += KNOB_SIMD16_WIDTH;

                _simd16_store_ps(pfBuf, valA);
                pfBuf += KNOB_SIMD16_WIDTH;
            }
        }
    }
}

void HotTileMgr::ClearDepthHotTile(
    const HOTTILE* pHotTile) // clear a macro tile from float4 clear data.
{
    // Load clear color into SIMD register...
    float*       pClearData = (float*)(pHotTile->clearData);
    simd16scalar valZ       = _simd16_broadcast_ss(&pClearData[0]);

    float*   pfBuf      = (float*)pHotTile->pBuffer;
    uint32_t numSamples = pHotTile->numSamples;

    for (uint32_t row = 0; row < KNOB_MACROTILE_Y_DIM; row += KNOB_TILE_Y_DIM)
    {
        for (uint32_t col = 0; col < KNOB_MACROTILE_X_DIM; col += KNOB_TILE_X_DIM)
        {
            for (uint32_t si = 0; si < (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * numSamples);
                 si += SIMD16_TILE_X_DIM * SIMD16_TILE_Y_DIM)
            {
                _simd16_store_ps(pfBuf, valZ);
                pfBuf += KNOB_SIMD16_WIDTH;
            }
        }
    }
}

void HotTileMgr::ClearStencilHotTile(const HOTTILE* pHotTile)
{
    // convert from F32 to U8.
    uint8_t clearVal = (uint8_t)(pHotTile->clearData[0]);
    // broadcast 32x into __m256i...
    simd16scalari valS = _simd16_set1_epi8(clearVal);

    simd16scalari* pBuf       = (simd16scalari*)pHotTile->pBuffer;
    uint32_t       numSamples = pHotTile->numSamples;

    for (uint32_t row = 0; row < KNOB_MACROTILE_Y_DIM; row += KNOB_TILE_Y_DIM)
    {
        for (uint32_t col = 0; col < KNOB_MACROTILE_X_DIM; col += KNOB_TILE_X_DIM)
        {
            // We're putting 4 pixels in each of the 32-bit slots, so increment 4 times as quickly.
            for (uint32_t si = 0; si < (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * numSamples);
                 si += SIMD16_TILE_X_DIM * SIMD16_TILE_Y_DIM * 4)
            {
                _simd16_store_si(pBuf, valS);
                pBuf += 1;
            }
        }
    }
}

//////////////////////////////////////////////////////////////////////////
/// @brief InitializeHotTiles
/// for draw calls, we initialize the active hot tiles and perform deferred
/// load on them if tile is in invalid state. we do this in the outer thread
/// loop instead of inside the draw routine itself mainly for performance,
/// to avoid unnecessary setup every triangle
/// @todo support deferred clear
/// @param pCreateInfo - pointer to creation info.
void HotTileMgr::InitializeHotTiles(SWR_CONTEXT*  pContext,
                                    DRAW_CONTEXT* pDC,
                                    uint32_t      workerId,
                                    uint32_t      macroID)
{
    const API_STATE& state    = GetApiState(pDC);
    HANDLE hWorkerPrivateData = pDC->pContext->threadPool.pThreadData[workerId].pWorkerPrivateData;

    uint32_t x, y;
    MacroTileMgr::getTileIndices(macroID, x, y);
    x *= KNOB_MACROTILE_X_DIM;
    y *= KNOB_MACROTILE_Y_DIM;

    uint32_t numSamples = GetNumSamples(state.rastState.sampleCount);

    // check RT if enabled
    unsigned long rtSlot                 = 0;
    uint32_t      colorHottileEnableMask = state.colorHottileEnable;
    while (_BitScanForward(&rtSlot, colorHottileEnableMask))
    {
        HOTTILE* pHotTile =
            GetHotTile(pContext,
                       pDC,
                       hWorkerPrivateData,
                       macroID,
                       (SWR_RENDERTARGET_ATTACHMENT)(SWR_ATTACHMENT_COLOR0 + rtSlot),
                       true,
                       numSamples);

        if (pHotTile->state == HOTTILE_INVALID)
        {
            RDTSC_BEGIN(pContext->pBucketMgr, BELoadTiles, pDC->drawId);
            // invalid hottile before draw requires a load from surface before we can draw to it
            pContext->pfnLoadTile(pDC,
                                  hWorkerPrivateData,
                                  KNOB_COLOR_HOT_TILE_FORMAT,
                                  (SWR_RENDERTARGET_ATTACHMENT)(SWR_ATTACHMENT_COLOR0 + rtSlot),
                                  x,
                                  y,
                                  pHotTile->renderTargetArrayIndex,
                                  pHotTile->pBuffer);
            pHotTile->state = HOTTILE_RESOLVED;
            RDTSC_END(pContext->pBucketMgr, BELoadTiles, 0);
        }
        else if (pHotTile->state == HOTTILE_CLEAR)
        {
            RDTSC_BEGIN(pContext->pBucketMgr, BELoadTiles, pDC->drawId);
            // Clear the tile.
            ClearColorHotTile(pHotTile);
            pHotTile->state = HOTTILE_DIRTY;
            RDTSC_END(pContext->pBucketMgr, BELoadTiles, 0);
        }
        colorHottileEnableMask &= ~(1 << rtSlot);
    }

    // check depth if enabled
    if (state.depthHottileEnable)
    {
        HOTTILE* pHotTile = GetHotTile(
            pContext, pDC, hWorkerPrivateData, macroID, SWR_ATTACHMENT_DEPTH, true, numSamples);
        if (pHotTile->state == HOTTILE_INVALID)
        {
            RDTSC_BEGIN(pContext->pBucketMgr, BELoadTiles, pDC->drawId);
            // invalid hottile before draw requires a load from surface before we can draw to it
            pContext->pfnLoadTile(pDC,
                                  hWorkerPrivateData,
                                  KNOB_DEPTH_HOT_TILE_FORMAT,
                                  SWR_ATTACHMENT_DEPTH,
                                  x,
                                  y,
                                  pHotTile->renderTargetArrayIndex,
                                  pHotTile->pBuffer);
            pHotTile->state = HOTTILE_DIRTY;
            RDTSC_END(pContext->pBucketMgr, BELoadTiles, 0);
        }
        else if (pHotTile->state == HOTTILE_CLEAR)
        {
            RDTSC_BEGIN(pContext->pBucketMgr, BELoadTiles, pDC->drawId);
            // Clear the tile.
            ClearDepthHotTile(pHotTile);
            pHotTile->state = HOTTILE_DIRTY;
            RDTSC_END(pContext->pBucketMgr, BELoadTiles, 0);
        }
    }

    // check stencil if enabled
    if (state.stencilHottileEnable)
    {
        HOTTILE* pHotTile = GetHotTile(
            pContext, pDC, hWorkerPrivateData, macroID, SWR_ATTACHMENT_STENCIL, true, numSamples);
        if (pHotTile->state == HOTTILE_INVALID)
        {
            RDTSC_BEGIN(pContext->pBucketMgr, BELoadTiles, pDC->drawId);
            // invalid hottile before draw requires a load from surface before we can draw to it
            pContext->pfnLoadTile(pDC,
                                  hWorkerPrivateData,
                                  KNOB_STENCIL_HOT_TILE_FORMAT,
                                  SWR_ATTACHMENT_STENCIL,
                                  x,
                                  y,
                                  pHotTile->renderTargetArrayIndex,
                                  pHotTile->pBuffer);
            pHotTile->state = HOTTILE_DIRTY;
            RDTSC_END(pContext->pBucketMgr, BELoadTiles, 0);
        }
        else if (pHotTile->state == HOTTILE_CLEAR)
        {
            RDTSC_BEGIN(pContext->pBucketMgr, BELoadTiles, pDC->drawId);
            // Clear the tile.
            ClearStencilHotTile(pHotTile);
            pHotTile->state = HOTTILE_DIRTY;
            RDTSC_END(pContext->pBucketMgr, BELoadTiles, 0);
        }
    }
}
