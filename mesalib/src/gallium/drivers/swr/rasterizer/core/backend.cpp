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
#include "backends/gen_BackendPixelRate.hpp"

#include <algorithm>


//////////////////////////////////////////////////////////////////////////
/// @brief Process compute work.
/// @param pDC - pointer to draw context (dispatch).
/// @param workerId - The unique worker ID that is assigned to this thread.
/// @param threadGroupId - the linear index for the thread group within the dispatch.
void ProcessComputeBE(DRAW_CONTEXT* pDC,
                      uint32_t      workerId,
                      uint32_t      threadGroupId,
                      void*&        pSpillFillBuffer,
                      void*&        pScratchSpace)
{
    SWR_CONTEXT* pContext = pDC->pContext;

    RDTSC_BEGIN(pDC->pContext->pBucketMgr, BEDispatch, pDC->drawId);

    const COMPUTE_DESC* pTaskData = (COMPUTE_DESC*)pDC->pDispatch->GetTasksData();
    SWR_ASSERT(pTaskData != nullptr);

    // Ensure spill fill memory has been allocated.
    size_t spillFillSize = pDC->pState->state.totalSpillFillSize;
    if (spillFillSize && pSpillFillBuffer == nullptr)
    {
        pSpillFillBuffer = pDC->pArena->AllocAlignedSync(spillFillSize, KNOB_SIMD16_BYTES);
    }

    size_t scratchSpaceSize =
        pDC->pState->state.scratchSpaceSizePerWarp * pDC->pState->state.scratchSpaceNumWarps;
    if (scratchSpaceSize && pScratchSpace == nullptr)
    {
        pScratchSpace = pDC->pArena->AllocAlignedSync(scratchSpaceSize, KNOB_SIMD16_BYTES);
    }

    const API_STATE& state = GetApiState(pDC);

    SWR_CS_CONTEXT csContext{0};
    csContext.tileCounter         = threadGroupId;
    csContext.dispatchDims[0]     = pTaskData->threadGroupCountX;
    csContext.dispatchDims[1]     = pTaskData->threadGroupCountY;
    csContext.dispatchDims[2]     = pTaskData->threadGroupCountZ;
    csContext.pTGSM               = pContext->ppScratch[workerId];
    csContext.pSpillFillBuffer    = (uint8_t*)pSpillFillBuffer;
    csContext.pScratchSpace       = (uint8_t*)pScratchSpace;
    csContext.scratchSpacePerWarp = pDC->pState->state.scratchSpaceSizePerWarp;

    state.pfnCsFunc(GetPrivateState(pDC),
                    pContext->threadPool.pThreadData[workerId].pWorkerPrivateData,
                    &csContext);

    UPDATE_STAT_BE(CsInvocations, state.totalThreadsInGroup);
    AR_EVENT(CSStats((HANDLE)&csContext.stats));

    RDTSC_END(pDC->pContext->pBucketMgr, BEDispatch, 1);
}

//////////////////////////////////////////////////////////////////////////
/// @brief Process shutdown.
/// @param pDC - pointer to draw context (dispatch).
/// @param workerId - The unique worker ID that is assigned to this thread.
/// @param threadGroupId - the linear index for the thread group within the dispatch.
void ProcessShutdownBE(DRAW_CONTEXT* pDC, uint32_t workerId, uint32_t macroTile, void* pUserData)
{
    // Dummy function
}

void ProcessSyncBE(DRAW_CONTEXT* pDC, uint32_t workerId, uint32_t macroTile, void* pUserData)
{
    uint32_t x, y;
    MacroTileMgr::getTileIndices(macroTile, x, y);
    SWR_ASSERT(x == 0 && y == 0);
}

void ProcessStoreTileBE(DRAW_CONTEXT*               pDC,
                        uint32_t                    workerId,
                        uint32_t                    macroTile,
                        STORE_TILES_DESC*           pDesc,
                        SWR_RENDERTARGET_ATTACHMENT attachment)
{
    SWR_CONTEXT* pContext           = pDC->pContext;
    HANDLE       hWorkerPrivateData = pContext->threadPool.pThreadData[workerId].pWorkerPrivateData;

    RDTSC_BEGIN(pDC->pContext->pBucketMgr, BEStoreTiles, pDC->drawId);

    SWR_FORMAT srcFormat;
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
        srcFormat = KNOB_COLOR_HOT_TILE_FORMAT;
        break;
    case SWR_ATTACHMENT_DEPTH:
        srcFormat = KNOB_DEPTH_HOT_TILE_FORMAT;
        break;
    case SWR_ATTACHMENT_STENCIL:
        srcFormat = KNOB_STENCIL_HOT_TILE_FORMAT;
        break;
    default:
        SWR_INVALID("Unknown attachment: %d", attachment);
        srcFormat = KNOB_COLOR_HOT_TILE_FORMAT;
        break;
    }

    uint32_t x, y;
    MacroTileMgr::getTileIndices(macroTile, x, y);

    // Only need to store the hottile if it's been rendered to...
    HOTTILE* pHotTile =
        pContext->pHotTileMgr->GetHotTileNoLoad(pContext, pDC, macroTile, attachment, false);
    if (pHotTile)
    {
        // clear if clear is pending (i.e., not rendered to), then mark as dirty for store.
        if (pHotTile->state == HOTTILE_CLEAR)
        {
            PFN_CLEAR_TILES pfnClearTiles = gClearTilesTable[srcFormat];
            SWR_ASSERT(pfnClearTiles != nullptr);

            pfnClearTiles(pDC,
                          hWorkerPrivateData,
                          attachment,
                          macroTile,
                          pHotTile->renderTargetArrayIndex,
                          pHotTile->clearData,
                          pDesc->rect);
        }

        if (pHotTile->state == HOTTILE_DIRTY ||
            pDesc->postStoreTileState == (SWR_TILE_STATE)HOTTILE_DIRTY)
        {
            int32_t destX = KNOB_MACROTILE_X_DIM * x;
            int32_t destY = KNOB_MACROTILE_Y_DIM * y;

            pContext->pfnStoreTile(pDC,
                                   hWorkerPrivateData,
                                   srcFormat,
                                   attachment,
                                   destX,
                                   destY,
                                   pHotTile->renderTargetArrayIndex,
                                   pHotTile->pBuffer);
        }

        if (pHotTile->state == HOTTILE_DIRTY || pHotTile->state == HOTTILE_RESOLVED) 
        {
            if (!(pDesc->postStoreTileState == (SWR_TILE_STATE)HOTTILE_DIRTY &&
                  pHotTile->state == HOTTILE_RESOLVED))
            {
                pHotTile->state = (HOTTILE_STATE)pDesc->postStoreTileState;
            }
        }
    }
    RDTSC_END(pDC->pContext->pBucketMgr, BEStoreTiles, 1);
}

void ProcessStoreTilesBE(DRAW_CONTEXT* pDC, uint32_t workerId, uint32_t macroTile, void* pData)
{
    STORE_TILES_DESC* pDesc = (STORE_TILES_DESC*)pData;

    unsigned long rt   = 0;
    uint32_t      mask = pDesc->attachmentMask;
    while (_BitScanForward(&rt, mask))
    {
        mask &= ~(1 << rt);
        ProcessStoreTileBE(pDC, workerId, macroTile, pDesc, (SWR_RENDERTARGET_ATTACHMENT)rt);
    }
}

void ProcessDiscardInvalidateTilesBE(DRAW_CONTEXT* pDC,
                                     uint32_t      workerId,
                                     uint32_t      macroTile,
                                     void*         pData)
{
    DISCARD_INVALIDATE_TILES_DESC* pDesc    = (DISCARD_INVALIDATE_TILES_DESC*)pData;
    SWR_CONTEXT*                   pContext = pDC->pContext;

    const int32_t numSamples = GetNumSamples(pDC->pState->state.rastState.sampleCount);

    for (uint32_t i = 0; i < SWR_NUM_ATTACHMENTS; ++i)
    {
        if (pDesc->attachmentMask & (1 << i))
        {
            HOTTILE* pHotTile =
                pContext->pHotTileMgr->GetHotTileNoLoad(pContext,
                                                        pDC,
                                                        macroTile,
                                                        (SWR_RENDERTARGET_ATTACHMENT)i,
                                                        pDesc->createNewTiles,
                                                        numSamples);
            if (pHotTile)
            {
                HOTTILE_STATE newState = (HOTTILE_STATE)pDesc->newTileState;;
                if (pHotTile->state == HOTTILE_DIRTY || pHotTile->state == HOTTILE_CLEAR)
                {
                    if (newState == HOTTILE_INVALID)
                    {
                        // This is OK for APIs that explicitly allow discards
                        // (for e.g. depth / stencil data)
                        //SWR_INVALID("Discarding valid data!");
                    }
                }
                pHotTile->state = newState;
            }
        }
    }
}

template <uint32_t sampleCountT>
void BackendNullPS(DRAW_CONTEXT*        pDC,
                   uint32_t             workerId,
                   uint32_t             x,
                   uint32_t             y,
                   SWR_TRIANGLE_DESC&   work,
                   RenderOutputBuffers& renderBuffers)
{
    RDTSC_BEGIN(pDC->pContext->pBucketMgr, BENullBackend, pDC->drawId);
    ///@todo: handle center multisample pattern
    RDTSC_BEGIN(pDC->pContext->pBucketMgr, BESetup, pDC->drawId);

    const API_STATE& state = GetApiState(pDC);

    BarycentricCoeffs coeffs;
    SetupBarycentricCoeffs(&coeffs, work);

    uint8_t *pDepthBuffer, *pStencilBuffer;
    SetupRenderBuffers(NULL, &pDepthBuffer, &pStencilBuffer, 0, renderBuffers);

    SWR_PS_CONTEXT psContext;
    // skip SetupPixelShaderContext(&psContext, ...); // not needed here

    RDTSC_END(pDC->pContext->pBucketMgr, BESetup, 0);

    simdscalar vYSamplePosUL = _simd_add_ps(vULOffsetsY, _simd_set1_ps(static_cast<float>(y)));

    const simdscalar           dy        = _simd_set1_ps(static_cast<float>(SIMD_TILE_Y_DIM));
    const SWR_MULTISAMPLE_POS& samplePos = state.rastState.samplePositions;
    for (uint32_t yy = y; yy < y + KNOB_TILE_Y_DIM; yy += SIMD_TILE_Y_DIM)
    {
        simdscalar vXSamplePosUL = _simd_add_ps(vULOffsetsX, _simd_set1_ps(static_cast<float>(x)));

        const simdscalar dx = _simd_set1_ps(static_cast<float>(SIMD_TILE_X_DIM));

        for (uint32_t xx = x; xx < x + KNOB_TILE_X_DIM; xx += SIMD_TILE_X_DIM)
        {
            // iterate over active samples
            unsigned long sample     = 0;
            uint32_t      sampleMask = state.blendState.sampleMask;
            while (_BitScanForward(&sample, sampleMask))
            {
                sampleMask &= ~(1 << sample);

                simdmask coverageMask = work.coverageMask[sample] & MASK;

                if (coverageMask)
                {
                    // offset depth/stencil buffers current sample
                    uint8_t* pDepthSample   = pDepthBuffer + RasterTileDepthOffset(sample);
                    uint8_t* pStencilSample = pStencilBuffer + RasterTileStencilOffset(sample);

                    if (state.depthHottileEnable && state.depthBoundsState.depthBoundsTestEnable)
                    {
                        static_assert(KNOB_DEPTH_HOT_TILE_FORMAT == R32_FLOAT,
                                      "Unsupported depth hot tile format");

                        const simdscalar z =
                            _simd_load_ps(reinterpret_cast<const float*>(pDepthSample));

                        const float minz = state.depthBoundsState.depthBoundsTestMinValue;
                        const float maxz = state.depthBoundsState.depthBoundsTestMaxValue;

                        coverageMask &= CalcDepthBoundsAcceptMask(z, minz, maxz);
                    }

                    RDTSC_BEGIN(pDC->pContext->pBucketMgr, BEBarycentric, pDC->drawId);

                    // calculate per sample positions
                    psContext.vX.sample = _simd_add_ps(vXSamplePosUL, samplePos.vX(sample));
                    psContext.vY.sample = _simd_add_ps(vYSamplePosUL, samplePos.vY(sample));

                    CalcSampleBarycentrics(coeffs, psContext);

                    // interpolate and quantize z
                    psContext.vZ = vplaneps(coeffs.vZa,
                                            coeffs.vZb,
                                            coeffs.vZc,
                                            psContext.vI.sample,
                                            psContext.vJ.sample);
                    psContext.vZ = state.pfnQuantizeDepth(psContext.vZ);

                    RDTSC_END(pDC->pContext->pBucketMgr, BEBarycentric, 0);

                    // interpolate user clip distance if available
                    if (state.backendState.clipDistanceMask)
                    {
                        coverageMask &= ~ComputeUserClipMask(state.backendState.clipDistanceMask,
                                                             work.pUserClipBuffer,
                                                             psContext.vI.sample,
                                                             psContext.vJ.sample);
                    }

                    simdscalar vCoverageMask   = _simd_vmask_ps(coverageMask);
                    simdscalar stencilPassMask = vCoverageMask;

                    RDTSC_BEGIN(pDC->pContext->pBucketMgr, BEEarlyDepthTest, pDC->drawId);
                    simdscalar depthPassMask = DepthStencilTest(&state,
                                                                work.triFlags.frontFacing,
                                                                work.triFlags.viewportIndex,
                                                                psContext.vZ,
                                                                pDepthSample,
                                                                vCoverageMask,
                                                                pStencilSample,
                                                                &stencilPassMask);
                    AR_EVENT(EarlyDepthStencilInfoNullPS(_simd_movemask_ps(depthPassMask),
                                                         _simd_movemask_ps(stencilPassMask),
                                                         _simd_movemask_ps(vCoverageMask)));
                    DepthStencilWrite(&state.vp[work.triFlags.viewportIndex],
                                      &state.depthStencilState,
                                      work.triFlags.frontFacing,
                                      psContext.vZ,
                                      pDepthSample,
                                      depthPassMask,
                                      vCoverageMask,
                                      pStencilSample,
                                      stencilPassMask);
                    RDTSC_END(pDC->pContext->pBucketMgr, BEEarlyDepthTest, 0);

                    uint32_t statMask  = _simd_movemask_ps(depthPassMask);
                    uint32_t statCount = _mm_popcnt_u32(statMask);
                    UPDATE_STAT_BE(DepthPassCount, statCount);
                }

            Endtile:
                ATTR_UNUSED;
                work.coverageMask[sample] >>= (SIMD_TILE_Y_DIM * SIMD_TILE_X_DIM);
            }

            pDepthBuffer += (KNOB_SIMD_WIDTH * FormatTraits<KNOB_DEPTH_HOT_TILE_FORMAT>::bpp) / 8;
            pStencilBuffer +=
                (KNOB_SIMD_WIDTH * FormatTraits<KNOB_STENCIL_HOT_TILE_FORMAT>::bpp) / 8;

            vXSamplePosUL = _simd_add_ps(vXSamplePosUL, dx);
        }

        vYSamplePosUL = _simd_add_ps(vYSamplePosUL, dy);
    }

    RDTSC_END(pDC->pContext->pBucketMgr, BENullBackend, 0);
}

PFN_CLEAR_TILES  gClearTilesTable[NUM_SWR_FORMATS] = {};
PFN_BACKEND_FUNC gBackendNullPs[SWR_MULTISAMPLE_TYPE_COUNT];
PFN_BACKEND_FUNC gBackendSingleSample[SWR_INPUT_COVERAGE_COUNT][2] // centroid
                                     [2]                           // canEarlyZ
    = {};
PFN_BACKEND_FUNC gBackendPixelRateTable[SWR_MULTISAMPLE_TYPE_COUNT][2] // isCenterPattern
                                       [SWR_INPUT_COVERAGE_COUNT][2]   // centroid
                                       [2]                             // forcedSampleCount
                                       [2]                             // canEarlyZ
    = {};
PFN_BACKEND_FUNC gBackendSampleRateTable[SWR_MULTISAMPLE_TYPE_COUNT][SWR_INPUT_COVERAGE_COUNT]
                                        [2] // centroid
                                        [2] // canEarlyZ
    = {};

void InitBackendFuncTables()
{
    InitBackendPixelRate();
    InitBackendSingleFuncTable(gBackendSingleSample);
    InitBackendSampleFuncTable(gBackendSampleRateTable);

    gBackendNullPs[SWR_MULTISAMPLE_1X]  = &BackendNullPS<SWR_MULTISAMPLE_1X>;
    gBackendNullPs[SWR_MULTISAMPLE_2X]  = &BackendNullPS<SWR_MULTISAMPLE_2X>;
    gBackendNullPs[SWR_MULTISAMPLE_4X]  = &BackendNullPS<SWR_MULTISAMPLE_4X>;
    gBackendNullPs[SWR_MULTISAMPLE_8X]  = &BackendNullPS<SWR_MULTISAMPLE_8X>;
    gBackendNullPs[SWR_MULTISAMPLE_16X] = &BackendNullPS<SWR_MULTISAMPLE_16X>;
}
