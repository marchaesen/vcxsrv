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
 * @file backend.cpp
 *
 * @brief Backend handles rasterization, pixel shading and output merger
 *        operations.
 *
 ******************************************************************************/

#include <smmintrin.h>

#include "backend.h"
#include "backend_impl.h"
#include "tilemgr.h"
#include "memory/tilingtraits.h"
#include "core/multisample.h"

#include <algorithm>

template <SWR_FORMAT format>
void ClearRasterTile(uint8_t* pTileBuffer, simd16vector& value)
{
    auto lambda = [&](int32_t comp)
    {
        FormatTraits<format>::storeSOA(comp, pTileBuffer, value.v[comp]);

        pTileBuffer += (KNOB_SIMD16_WIDTH * FormatTraits<format>::GetBPC(comp) / 8);
    };

    const uint32_t numIter =
        (KNOB_TILE_Y_DIM / SIMD16_TILE_Y_DIM) * (KNOB_TILE_X_DIM / SIMD16_TILE_X_DIM);

    for (uint32_t i = 0; i < numIter; ++i)
    {
        UnrollerL<0, FormatTraits<format>::numComps, 1>::step(lambda);
    }
}

template <SWR_FORMAT format>
INLINE void ClearMacroTile(DRAW_CONTEXT*               pDC,
                           HANDLE                      hWorkerPrivateData,
                           SWR_RENDERTARGET_ATTACHMENT rt,
                           uint32_t                    macroTile,
                           uint32_t                    renderTargetArrayIndex,
                           uint32_t                    clear[4],
                           const SWR_RECT&             rect)
{
    // convert clear color to hottile format
    // clear color is in RGBA float/uint32

    simd16vector vClear;
    for (uint32_t comp = 0; comp < FormatTraits<format>::numComps; ++comp)
    {
        simd16scalar vComp = _simd16_load1_ps((const float*)&clear[comp]);

        if (FormatTraits<format>::isNormalized(comp))
        {
            vComp = _simd16_mul_ps(vComp, _simd16_set1_ps(FormatTraits<format>::fromFloat(comp)));
            vComp = _simd16_castsi_ps(_simd16_cvtps_epi32(vComp));
        }
        vComp = FormatTraits<format>::pack(comp, vComp);

        vClear.v[FormatTraits<format>::swizzle(comp)] = vComp;
    }

    uint32_t tileX, tileY;
    MacroTileMgr::getTileIndices(macroTile, tileX, tileY);

    // Init to full macrotile
    SWR_RECT clearTile = {
        KNOB_MACROTILE_X_DIM * int32_t(tileX),
        KNOB_MACROTILE_Y_DIM * int32_t(tileY),
        KNOB_MACROTILE_X_DIM * int32_t(tileX + 1),
        KNOB_MACROTILE_Y_DIM * int32_t(tileY + 1),
    };

    // intersect with clear rect
    clearTile &= rect;

    // translate to local hottile origin
    clearTile.Translate(-int32_t(tileX) * KNOB_MACROTILE_X_DIM,
                        -int32_t(tileY) * KNOB_MACROTILE_Y_DIM);

    // Make maximums inclusive (needed for convert to raster tiles)
    clearTile.xmax -= 1;
    clearTile.ymax -= 1;

    // convert to raster tiles
    clearTile.ymin >>= (KNOB_TILE_Y_DIM_SHIFT);
    clearTile.ymax >>= (KNOB_TILE_Y_DIM_SHIFT);
    clearTile.xmin >>= (KNOB_TILE_X_DIM_SHIFT);
    clearTile.xmax >>= (KNOB_TILE_X_DIM_SHIFT);

    const int32_t numSamples = GetNumSamples(pDC->pState->state.rastState.sampleCount);
    // compute steps between raster tile samples / raster tiles / macro tile rows
    const uint32_t rasterTileSampleStep =
        KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * FormatTraits<format>::bpp / 8;
    const uint32_t rasterTileStep =
        (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * (FormatTraits<format>::bpp / 8)) * numSamples;
    const uint32_t macroTileRowStep = (KNOB_MACROTILE_X_DIM / KNOB_TILE_X_DIM) * rasterTileStep;
    const uint32_t pitch            = (FormatTraits<format>::bpp * KNOB_MACROTILE_X_DIM / 8);

    HOTTILE* pHotTile = pDC->pContext->pHotTileMgr->GetHotTile(pDC->pContext,
                                                               pDC,
                                                               hWorkerPrivateData,
                                                               macroTile,
                                                               rt,
                                                               true,
                                                               numSamples,
                                                               renderTargetArrayIndex);
    uint32_t rasterTileStartOffset =
        (ComputeTileOffset2D<TilingTraits<SWR_TILE_SWRZ, FormatTraits<format>::bpp>>(
            pitch, clearTile.xmin, clearTile.ymin)) *
        numSamples;
    uint8_t* pRasterTileRow =
        pHotTile->pBuffer +
        rasterTileStartOffset; //(ComputeTileOffset2D< TilingTraits<SWR_TILE_SWRZ,
                               // FormatTraits<format>::bpp > >(pitch, x, y)) * numSamples;

    // loop over all raster tiles in the current hot tile
    for (int32_t y = clearTile.ymin; y <= clearTile.ymax; ++y)
    {
        uint8_t* pRasterTile = pRasterTileRow;
        for (int32_t x = clearTile.xmin; x <= clearTile.xmax; ++x)
        {
            for (int32_t sampleNum = 0; sampleNum < numSamples; sampleNum++)
            {
                ClearRasterTile<format>(pRasterTile, vClear);
                pRasterTile += rasterTileSampleStep;
            }
        }
        pRasterTileRow += macroTileRowStep;
    }

    pHotTile->state = HOTTILE_DIRTY;
}

void ProcessClearBE(DRAW_CONTEXT* pDC, uint32_t workerId, uint32_t macroTile, void* pUserData)
{
    SWR_CONTEXT* pContext           = pDC->pContext;
    HANDLE       hWorkerPrivateData = pContext->threadPool.pThreadData[workerId].pWorkerPrivateData;

    if (KNOB_FAST_CLEAR)
    {
        CLEAR_DESC*           pClear      = (CLEAR_DESC*)pUserData;
        SWR_MULTISAMPLE_COUNT sampleCount = pDC->pState->state.rastState.sampleCount;
        uint32_t              numSamples  = GetNumSamples(sampleCount);

        SWR_ASSERT(pClear->attachmentMask != 0); // shouldn't be here without a reason.

        RDTSC_BEGIN(pDC->pContext->pBucketMgr, BEClear, pDC->drawId);

        if (pClear->attachmentMask & SWR_ATTACHMENT_MASK_COLOR)
        {
            unsigned long rt   = 0;
            uint32_t      mask = pClear->attachmentMask & SWR_ATTACHMENT_MASK_COLOR;
            while (_BitScanForward(&rt, mask))
            {
                mask &= ~(1 << rt);

                HOTTILE* pHotTile =
                    pContext->pHotTileMgr->GetHotTile(pContext,
                                                      pDC,
                                                      hWorkerPrivateData,
                                                      macroTile,
                                                      (SWR_RENDERTARGET_ATTACHMENT)rt,
                                                      true,
                                                      numSamples,
                                                      pClear->renderTargetArrayIndex);

                // All we want to do here is to mark the hot tile as being in a "needs clear" state.
                pHotTile->clearData[0] = *(uint32_t*)&(pClear->clearRTColor[0]);
                pHotTile->clearData[1] = *(uint32_t*)&(pClear->clearRTColor[1]);
                pHotTile->clearData[2] = *(uint32_t*)&(pClear->clearRTColor[2]);
                pHotTile->clearData[3] = *(uint32_t*)&(pClear->clearRTColor[3]);
                pHotTile->state        = HOTTILE_CLEAR;
            }
        }

        if (pClear->attachmentMask & SWR_ATTACHMENT_DEPTH_BIT)
        {
            HOTTILE* pHotTile      = pContext->pHotTileMgr->GetHotTile(pContext,
                                                                  pDC,
                                                                  hWorkerPrivateData,
                                                                  macroTile,
                                                                  SWR_ATTACHMENT_DEPTH,
                                                                  true,
                                                                  numSamples,
                                                                  pClear->renderTargetArrayIndex);
            pHotTile->clearData[0] = *(uint32_t*)&pClear->clearDepth;
            pHotTile->state        = HOTTILE_CLEAR;
        }

        if (pClear->attachmentMask & SWR_ATTACHMENT_STENCIL_BIT)
        {
            HOTTILE* pHotTile = pContext->pHotTileMgr->GetHotTile(pContext,
                                                                  pDC,
                                                                  hWorkerPrivateData,
                                                                  macroTile,
                                                                  SWR_ATTACHMENT_STENCIL,
                                                                  true,
                                                                  numSamples,
                                                                  pClear->renderTargetArrayIndex);

            pHotTile->clearData[0] = pClear->clearStencil;
            pHotTile->state        = HOTTILE_CLEAR;
        }

        RDTSC_END(pDC->pContext->pBucketMgr, BEClear, 1);
    }
    else
    {
        // Legacy clear
        CLEAR_DESC* pClear = (CLEAR_DESC*)pUserData;
        RDTSC_BEGIN(pDC->pContext->pBucketMgr, BEClear, pDC->drawId);

        if (pClear->attachmentMask & SWR_ATTACHMENT_MASK_COLOR)
        {
            uint32_t clearData[4];
            clearData[0] = *(uint32_t*)&(pClear->clearRTColor[0]);
            clearData[1] = *(uint32_t*)&(pClear->clearRTColor[1]);
            clearData[2] = *(uint32_t*)&(pClear->clearRTColor[2]);
            clearData[3] = *(uint32_t*)&(pClear->clearRTColor[3]);

            PFN_CLEAR_TILES pfnClearTiles = gClearTilesTable[KNOB_COLOR_HOT_TILE_FORMAT];
            SWR_ASSERT(pfnClearTiles != nullptr);

            unsigned long rt   = 0;
            uint32_t      mask = pClear->attachmentMask & SWR_ATTACHMENT_MASK_COLOR;
            while (_BitScanForward(&rt, mask))
            {
                mask &= ~(1 << rt);

                pfnClearTiles(pDC,
                              hWorkerPrivateData,
                              (SWR_RENDERTARGET_ATTACHMENT)rt,
                              macroTile,
                              pClear->renderTargetArrayIndex,
                              clearData,
                              pClear->rect);
            }
        }

        if (pClear->attachmentMask & SWR_ATTACHMENT_DEPTH_BIT)
        {
            uint32_t clearData[4];
            clearData[0]                  = *(uint32_t*)&pClear->clearDepth;
            PFN_CLEAR_TILES pfnClearTiles = gClearTilesTable[KNOB_DEPTH_HOT_TILE_FORMAT];
            SWR_ASSERT(pfnClearTiles != nullptr);

            pfnClearTiles(pDC,
                          hWorkerPrivateData,
                          SWR_ATTACHMENT_DEPTH,
                          macroTile,
                          pClear->renderTargetArrayIndex,
                          clearData,
                          pClear->rect);
        }

        if (pClear->attachmentMask & SWR_ATTACHMENT_STENCIL_BIT)
        {
            uint32_t clearData[4];
            clearData[0]                  = pClear->clearStencil;
            PFN_CLEAR_TILES pfnClearTiles = gClearTilesTable[KNOB_STENCIL_HOT_TILE_FORMAT];

            pfnClearTiles(pDC,
                          hWorkerPrivateData,
                          SWR_ATTACHMENT_STENCIL,
                          macroTile,
                          pClear->renderTargetArrayIndex,
                          clearData,
                          pClear->rect);
        }

        RDTSC_END(pDC->pContext->pBucketMgr, BEClear, 1);
    }
}

void InitClearTilesTable()
{
    memset(gClearTilesTable, 0, sizeof(gClearTilesTable));

    gClearTilesTable[R8G8B8A8_UNORM]     = ClearMacroTile<R8G8B8A8_UNORM>;
    gClearTilesTable[B8G8R8A8_UNORM]     = ClearMacroTile<B8G8R8A8_UNORM>;
    gClearTilesTable[R32_FLOAT]          = ClearMacroTile<R32_FLOAT>;
    gClearTilesTable[R32G32B32A32_FLOAT] = ClearMacroTile<R32G32B32A32_FLOAT>;
    gClearTilesTable[R8_UINT]            = ClearMacroTile<R8_UINT>;
}
