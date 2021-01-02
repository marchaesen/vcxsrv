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

template <typename T>
void BackendSampleRate(DRAW_CONTEXT*        pDC,
                       uint32_t             workerId,
                       uint32_t             x,
                       uint32_t             y,
                       SWR_TRIANGLE_DESC&   work,
                       RenderOutputBuffers& renderBuffers)
{
    RDTSC_BEGIN(pDC->pContext->pBucketMgr, BESampleRateBackend, pDC->drawId);
    RDTSC_BEGIN(pDC->pContext->pBucketMgr, BESetup, pDC->drawId);

    void* pWorkerData      = pDC->pContext->threadPool.pThreadData[workerId].pWorkerPrivateData;
    const API_STATE& state = GetApiState(pDC);

    BarycentricCoeffs coeffs;
    SetupBarycentricCoeffs(&coeffs, work);

    SWR_PS_CONTEXT             psContext;
    const SWR_MULTISAMPLE_POS& samplePos = state.rastState.samplePositions;
    SetupPixelShaderContext<T>(&psContext, samplePos, work);

    uint8_t *pDepthBuffer, *pStencilBuffer;
    SetupRenderBuffers(psContext.pColorBuffer,
                       &pDepthBuffer,
                       &pStencilBuffer,
                       state.colorHottileEnable,
                       renderBuffers);

    bool isTileDirty = false;

    RDTSC_END(pDC->pContext->pBucketMgr, BESetup, 0);

    psContext.vY.UL     = _simd_add_ps(vULOffsetsY, _simd_set1_ps(static_cast<float>(y)));
    psContext.vY.center = _simd_add_ps(vCenterOffsetsY, _simd_set1_ps(static_cast<float>(y)));

    const simdscalar dy = _simd_set1_ps(static_cast<float>(SIMD_TILE_Y_DIM));

    for (uint32_t yy = y; yy < y + KNOB_TILE_Y_DIM; yy += SIMD_TILE_Y_DIM)
    {
        psContext.vX.UL     = _simd_add_ps(vULOffsetsX, _simd_set1_ps(static_cast<float>(x)));
        psContext.vX.center = _simd_add_ps(vCenterOffsetsX, _simd_set1_ps(static_cast<float>(x)));

        const simdscalar dx = _simd_set1_ps(static_cast<float>(SIMD_TILE_X_DIM));

        for (uint32_t xx = x; xx < x + KNOB_TILE_X_DIM; xx += SIMD_TILE_X_DIM)
        {
            const bool useAlternateOffset = ((xx & SIMD_TILE_X_DIM) != 0);


            if (T::InputCoverage != SWR_INPUT_COVERAGE_NONE)
            {
                const uint64_t* pCoverageMask =
                    (T::InputCoverage == SWR_INPUT_COVERAGE_INNER_CONSERVATIVE)
                        ? &work.innerCoverageMask
                        : &work.coverageMask[0];

                generateInputCoverage<T, T::InputCoverage>(
                    pCoverageMask, psContext.inputMask, state.blendState.sampleMask);
            }

            RDTSC_BEGIN(pDC->pContext->pBucketMgr, BEBarycentric, pDC->drawId);

            CalcPixelBarycentrics(coeffs, psContext);

            CalcCentroid<T, false>(
                &psContext, samplePos, coeffs, work.coverageMask, state.blendState.sampleMask);

            RDTSC_END(pDC->pContext->pBucketMgr, BEBarycentric, 0);

            for (uint32_t sample = 0; sample < T::MultisampleT::numSamples; sample++)
            {
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
                    psContext.vX.sample = _simd_add_ps(psContext.vX.UL, samplePos.vX(sample));
                    psContext.vY.sample = _simd_add_ps(psContext.vY.UL, samplePos.vY(sample));

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
                    simdscalar depthPassMask   = vCoverageMask;
                    simdscalar stencilPassMask = vCoverageMask;

                    // Early-Z?
                    if (T::bCanEarlyZ)
                    {
                        RDTSC_BEGIN(pDC->pContext->pBucketMgr, BEEarlyDepthTest, pDC->drawId);
                        depthPassMask = DepthStencilTest(&state,
                                                         work.triFlags.frontFacing,
                                                         work.triFlags.viewportIndex,
                                                         psContext.vZ,
                                                         pDepthSample,
                                                         vCoverageMask,
                                                         pStencilSample,
                                                         &stencilPassMask);
                        AR_EVENT(EarlyDepthStencilInfoSampleRate(_simd_movemask_ps(depthPassMask),
                                                                 _simd_movemask_ps(stencilPassMask),
                                                                 _simd_movemask_ps(vCoverageMask)));
                        RDTSC_END(pDC->pContext->pBucketMgr, BEEarlyDepthTest, 0);

                        // early-exit if no samples passed depth or earlyZ is forced on.
                        if (state.psState.forceEarlyZ || !_simd_movemask_ps(depthPassMask))
                        {
                            DepthStencilWrite(&state.vp[work.triFlags.viewportIndex],
                                              &state.depthStencilState,
                                              work.triFlags.frontFacing,
                                              psContext.vZ,
                                              pDepthSample,
                                              depthPassMask,
                                              vCoverageMask,
                                              pStencilSample,
                                              stencilPassMask);

                            if (!_simd_movemask_ps(depthPassMask))
                            {
                                work.coverageMask[sample] >>= (SIMD_TILE_Y_DIM * SIMD_TILE_X_DIM);
                                continue;
                            }
                        }
                    }

                    psContext.sampleIndex = sample;
                    psContext.activeMask  = _simd_castps_si(vCoverageMask);

                    // execute pixel shader
                    RDTSC_BEGIN(pDC->pContext->pBucketMgr, BEPixelShader, pDC->drawId);
                    state.psState.pfnPixelShader(GetPrivateState(pDC), pWorkerData, &psContext);
                    RDTSC_END(pDC->pContext->pBucketMgr, BEPixelShader, 0);

                    // update stats
                    UPDATE_STAT_BE(PsInvocations, _mm_popcnt_u32(_simd_movemask_ps(vCoverageMask)));
                    AR_EVENT(PSStats((HANDLE)&psContext.stats));

                    vCoverageMask = _simd_castsi_ps(psContext.activeMask);

                    if (_simd_movemask_ps(vCoverageMask))
                    {
                        isTileDirty = true;
                    }

                    // late-Z
                    if (!T::bCanEarlyZ)
                    {
                        RDTSC_BEGIN(pDC->pContext->pBucketMgr, BELateDepthTest, pDC->drawId);
                        depthPassMask = DepthStencilTest(&state,
                                                         work.triFlags.frontFacing,
                                                         work.triFlags.viewportIndex,
                                                         psContext.vZ,
                                                         pDepthSample,
                                                         vCoverageMask,
                                                         pStencilSample,
                                                         &stencilPassMask);
                        AR_EVENT(LateDepthStencilInfoSampleRate(_simd_movemask_ps(depthPassMask),
                                                                _simd_movemask_ps(stencilPassMask),
                                                                _simd_movemask_ps(vCoverageMask)));
                        RDTSC_END(pDC->pContext->pBucketMgr, BELateDepthTest, 0);

                        if (!_simd_movemask_ps(depthPassMask))
                        {
                            // need to call depth/stencil write for stencil write
                            DepthStencilWrite(&state.vp[work.triFlags.viewportIndex],
                                              &state.depthStencilState,
                                              work.triFlags.frontFacing,
                                              psContext.vZ,
                                              pDepthSample,
                                              depthPassMask,
                                              vCoverageMask,
                                              pStencilSample,
                                              stencilPassMask);

                            work.coverageMask[sample] >>= (SIMD_TILE_Y_DIM * SIMD_TILE_X_DIM);
                            continue;
                        }
                    }

                    uint32_t statMask  = _simd_movemask_ps(depthPassMask);
                    uint32_t statCount = _mm_popcnt_u32(statMask);
                    UPDATE_STAT_BE(DepthPassCount, statCount);

                    // output merger
                    RDTSC_BEGIN(pDC->pContext->pBucketMgr, BEOutputMerger, pDC->drawId);

                    OutputMerger8x2(pDC,
                                    psContext,
                                    psContext.pColorBuffer,
                                    sample,
                                    &state.blendState,
                                    state.pfnBlendFunc,
                                    vCoverageMask,
                                    depthPassMask,
                                    state.psState.renderTargetMask,
                                    useAlternateOffset,
                                    workerId);

                    // do final depth write after all pixel kills
                    if (!state.psState.forceEarlyZ)
                    {
                        DepthStencilWrite(&state.vp[work.triFlags.viewportIndex],
                                          &state.depthStencilState,
                                          work.triFlags.frontFacing,
                                          psContext.vZ,
                                          pDepthSample,
                                          depthPassMask,
                                          vCoverageMask,
                                          pStencilSample,
                                          stencilPassMask);
                    }
                    RDTSC_END(pDC->pContext->pBucketMgr, BEOutputMerger, 0);
                }
                work.coverageMask[sample] >>= (SIMD_TILE_Y_DIM * SIMD_TILE_X_DIM);
            }

        Endtile:
            ATTR_UNUSED;

            RDTSC_BEGIN(pDC->pContext->pBucketMgr, BEEndTile, pDC->drawId);

            if (T::InputCoverage == SWR_INPUT_COVERAGE_INNER_CONSERVATIVE)
            {
                work.innerCoverageMask >>= (SIMD_TILE_Y_DIM * SIMD_TILE_X_DIM);
            }

            if (useAlternateOffset)
            {
                unsigned long rt;
                uint32_t rtMask = state.colorHottileEnable;
                while (_BitScanForward(&rt, rtMask))
                {
                    rtMask &= ~(1 << rt);
                    psContext.pColorBuffer[rt] +=
                        (2 * KNOB_SIMD_WIDTH * FormatTraits<KNOB_COLOR_HOT_TILE_FORMAT>::bpp) / 8;
                }
            }

            pDepthBuffer += (KNOB_SIMD_WIDTH * FormatTraits<KNOB_DEPTH_HOT_TILE_FORMAT>::bpp) / 8;
            pStencilBuffer +=
                (KNOB_SIMD_WIDTH * FormatTraits<KNOB_STENCIL_HOT_TILE_FORMAT>::bpp) / 8;

            RDTSC_END(pDC->pContext->pBucketMgr, BEEndTile, 0);

            psContext.vX.UL     = _simd_add_ps(psContext.vX.UL, dx);
            psContext.vX.center = _simd_add_ps(psContext.vX.center, dx);
        }

        psContext.vY.UL     = _simd_add_ps(psContext.vY.UL, dy);
        psContext.vY.center = _simd_add_ps(psContext.vY.center, dy);
    }

    if (isTileDirty)
    {
        SetRenderHotTilesDirty(pDC, renderBuffers);
    }

    RDTSC_END(pDC->pContext->pBucketMgr, BESampleRateBackend, 0);
}

// Recursive template used to auto-nest conditionals.  Converts dynamic enum function
// arguments to static template arguments.
template <uint32_t... ArgsT>
struct BEChooserSampleRate
{
    // Last Arg Terminator
    static PFN_BACKEND_FUNC GetFunc(SWR_BACKEND_FUNCS tArg)
    {
        switch (tArg)
        {
        case SWR_BACKEND_MSAA_SAMPLE_RATE:
            return BackendSampleRate<SwrBackendTraits<ArgsT...>>;
            break;
        case SWR_BACKEND_SINGLE_SAMPLE:
        case SWR_BACKEND_MSAA_PIXEL_RATE:
            SWR_ASSERT(0 && "Invalid backend func\n");
            return nullptr;
            break;
        default:
            SWR_ASSERT(0 && "Invalid backend func\n");
            return nullptr;
            break;
        }
    }

    // Recursively parse args
    template <typename... TArgsT>
    static PFN_BACKEND_FUNC GetFunc(SWR_INPUT_COVERAGE tArg, TArgsT... remainingArgs)
    {
        switch (tArg)
        {
        case SWR_INPUT_COVERAGE_NONE:
            return BEChooserSampleRate<ArgsT..., SWR_INPUT_COVERAGE_NONE>::GetFunc(
                remainingArgs...);
            break;
        case SWR_INPUT_COVERAGE_NORMAL:
            return BEChooserSampleRate<ArgsT..., SWR_INPUT_COVERAGE_NORMAL>::GetFunc(
                remainingArgs...);
            break;
        case SWR_INPUT_COVERAGE_INNER_CONSERVATIVE:
            return BEChooserSampleRate<ArgsT..., SWR_INPUT_COVERAGE_INNER_CONSERVATIVE>::GetFunc(
                remainingArgs...);
            break;
        default:
            SWR_ASSERT(0 && "Invalid sample pattern\n");
            return BEChooserSampleRate<ArgsT..., SWR_INPUT_COVERAGE_NONE>::GetFunc(
                remainingArgs...);
            break;
        }
    }

    // Recursively parse args
    template <typename... TArgsT>
    static PFN_BACKEND_FUNC GetFunc(SWR_MULTISAMPLE_COUNT tArg, TArgsT... remainingArgs)
    {
        switch (tArg)
        {
        case SWR_MULTISAMPLE_1X:
            return BEChooserSampleRate<ArgsT..., SWR_MULTISAMPLE_1X>::GetFunc(remainingArgs...);
            break;
        case SWR_MULTISAMPLE_2X:
            return BEChooserSampleRate<ArgsT..., SWR_MULTISAMPLE_2X>::GetFunc(remainingArgs...);
            break;
        case SWR_MULTISAMPLE_4X:
            return BEChooserSampleRate<ArgsT..., SWR_MULTISAMPLE_4X>::GetFunc(remainingArgs...);
            break;
        case SWR_MULTISAMPLE_8X:
            return BEChooserSampleRate<ArgsT..., SWR_MULTISAMPLE_8X>::GetFunc(remainingArgs...);
            break;
        case SWR_MULTISAMPLE_16X:
            return BEChooserSampleRate<ArgsT..., SWR_MULTISAMPLE_16X>::GetFunc(remainingArgs...);
            break;
        default:
            SWR_ASSERT(0 && "Invalid sample count\n");
            return BEChooserSampleRate<ArgsT..., SWR_MULTISAMPLE_1X>::GetFunc(remainingArgs...);
            break;
        }
    }

    // Recursively parse args
    template <typename... TArgsT>
    static PFN_BACKEND_FUNC GetFunc(bool tArg, TArgsT... remainingArgs)
    {
        if (tArg == true)
        {
            return BEChooserSampleRate<ArgsT..., 1>::GetFunc(remainingArgs...);
        }

        return BEChooserSampleRate<ArgsT..., 0>::GetFunc(remainingArgs...);
    }
};

void InitBackendSampleFuncTable(
    PFN_BACKEND_FUNC (&table)[SWR_MULTISAMPLE_TYPE_COUNT][SWR_INPUT_COVERAGE_COUNT][2][2])
{
    for (uint32_t sampleCount = SWR_MULTISAMPLE_1X; sampleCount < SWR_MULTISAMPLE_TYPE_COUNT;
         sampleCount++)
    {
        for (uint32_t inputCoverage = 0; inputCoverage < SWR_INPUT_COVERAGE_COUNT; inputCoverage++)
        {
            for (uint32_t centroid = 0; centroid < 2; centroid++)
            {
                for (uint32_t canEarlyZ = 0; canEarlyZ < 2; canEarlyZ++)
                {
                    table[sampleCount][inputCoverage][centroid][canEarlyZ] =
                        BEChooserSampleRate<>::GetFunc(
                            (SWR_MULTISAMPLE_COUNT)sampleCount,
                            false,
                            (SWR_INPUT_COVERAGE)inputCoverage,
                            (centroid > 0),
                            false,
                            (canEarlyZ > 0),
                            (SWR_BACKEND_FUNCS)SWR_BACKEND_MSAA_SAMPLE_RATE);
                }
            }
        }
    }
}
