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
 * @file rasterizer.cpp
 *
 * @brief Implementation for the rasterizer.
 *
 ******************************************************************************/

#include <vector>
#include <algorithm>

#include "rasterizer.h"
#include "backends/gen_rasterizer.hpp"
#include "rdtsc_core.h"
#include "backend.h"
#include "utils.h"
#include "frontend.h"
#include "tilemgr.h"
#include "memory/tilingtraits.h"
#include "rasterizer_impl.h"

PFN_WORK_FUNC gRasterizerFuncs[SWR_MULTISAMPLE_TYPE_COUNT][2][2][SWR_INPUT_COVERAGE_COUNT]
                              [STATE_VALID_TRI_EDGE_COUNT][2];

void RasterizeLine(DRAW_CONTEXT* pDC, uint32_t workerId, uint32_t macroTile, void* pData)
{
    const TRIANGLE_WORK_DESC& workDesc = *((TRIANGLE_WORK_DESC*)pData);
#if KNOB_ENABLE_TOSS_POINTS
    if (KNOB_TOSS_BIN_TRIS)
    {
        return;
    }
#endif

    // bloat line to two tris and call the triangle rasterizer twice
    RDTSC_BEGIN(pDC->pContext->pBucketMgr, BERasterizeLine, pDC->drawId);

    const API_STATE&     state     = GetApiState(pDC);
    const SWR_RASTSTATE& rastState = state.rastState;

    // macrotile dimensioning
    uint32_t macroX, macroY;
    MacroTileMgr::getTileIndices(macroTile, macroX, macroY);
    int32_t macroBoxLeft   = macroX * KNOB_MACROTILE_X_DIM_FIXED;
    int32_t macroBoxRight  = macroBoxLeft + KNOB_MACROTILE_X_DIM_FIXED - 1;
    int32_t macroBoxTop    = macroY * KNOB_MACROTILE_Y_DIM_FIXED;
    int32_t macroBoxBottom = macroBoxTop + KNOB_MACROTILE_Y_DIM_FIXED - 1;

    const SWR_RECT& scissorInFixedPoint =
        state.scissorsInFixedPoint[workDesc.triFlags.viewportIndex];

    // create a copy of the triangle buffer to write our adjusted vertices to
    OSALIGNSIMD(float) newTriBuffer[4 * 4];
    TRIANGLE_WORK_DESC newWorkDesc = workDesc;
    newWorkDesc.pTriBuffer         = &newTriBuffer[0];

    // create a copy of the attrib buffer to write our adjusted attribs to
    OSALIGNSIMD(float) newAttribBuffer[4 * 3 * SWR_VTX_NUM_SLOTS];
    newWorkDesc.pAttribs = &newAttribBuffer[0];

    const __m128 vBloat0 = _mm_set_ps(0.5f, -0.5f, -0.5f, 0.5f);
    const __m128 vBloat1 = _mm_set_ps(0.5f, 0.5f, 0.5f, -0.5f);

    __m128 vX, vY, vZ, vRecipW;

    vX      = _mm_load_ps(workDesc.pTriBuffer);
    vY      = _mm_load_ps(workDesc.pTriBuffer + 4);
    vZ      = _mm_load_ps(workDesc.pTriBuffer + 8);
    vRecipW = _mm_load_ps(workDesc.pTriBuffer + 12);

    // triangle 0
    // v0,v1 -> v0,v0,v1
    __m128 vXa      = _mm_shuffle_ps(vX, vX, _MM_SHUFFLE(1, 1, 0, 0));
    __m128 vYa      = _mm_shuffle_ps(vY, vY, _MM_SHUFFLE(1, 1, 0, 0));
    __m128 vZa      = _mm_shuffle_ps(vZ, vZ, _MM_SHUFFLE(1, 1, 0, 0));
    __m128 vRecipWa = _mm_shuffle_ps(vRecipW, vRecipW, _MM_SHUFFLE(1, 1, 0, 0));

    __m128 vLineWidth = _mm_set1_ps(pDC->pState->state.rastState.lineWidth);
    __m128 vAdjust    = _mm_mul_ps(vLineWidth, vBloat0);
    if (workDesc.triFlags.yMajor)
    {
        vXa = _mm_add_ps(vAdjust, vXa);
    }
    else
    {
        vYa = _mm_add_ps(vAdjust, vYa);
    }

    // Store triangle description for rasterizer
    _mm_store_ps((float*)&newTriBuffer[0], vXa);
    _mm_store_ps((float*)&newTriBuffer[4], vYa);
    _mm_store_ps((float*)&newTriBuffer[8], vZa);
    _mm_store_ps((float*)&newTriBuffer[12], vRecipWa);

    // binner bins 3 edges for lines as v0, v1, v1
    // tri0 needs v0, v0, v1
    for (uint32_t a = 0; a < workDesc.numAttribs; ++a)
    {
        __m128 vAttrib0 = _mm_load_ps(&workDesc.pAttribs[a * 12 + 0]);
        __m128 vAttrib1 = _mm_load_ps(&workDesc.pAttribs[a * 12 + 4]);

        _mm_store_ps((float*)&newAttribBuffer[a * 12 + 0], vAttrib0);
        _mm_store_ps((float*)&newAttribBuffer[a * 12 + 4], vAttrib0);
        _mm_store_ps((float*)&newAttribBuffer[a * 12 + 8], vAttrib1);
    }

    // Store user clip distances for triangle 0
    float    newClipBuffer[3 * 8];
    uint32_t numClipDist = _mm_popcnt_u32(state.backendState.clipDistanceMask);
    if (numClipDist)
    {
        newWorkDesc.pUserClipBuffer = newClipBuffer;

        float* pOldBuffer = workDesc.pUserClipBuffer;
        float* pNewBuffer = newClipBuffer;
        for (uint32_t i = 0; i < numClipDist; ++i)
        {
            // read barycentric coeffs from binner
            float a = *(pOldBuffer++);
            float b = *(pOldBuffer++);

            // reconstruct original clip distance at vertices
            float c0 = a + b;
            float c1 = b;

            // construct triangle barycentrics
            *(pNewBuffer++) = c0 - c1;
            *(pNewBuffer++) = c0 - c1;
            *(pNewBuffer++) = c1;
        }
    }

    // setup triangle rasterizer function
    PFN_WORK_FUNC pfnTriRast;
    // conservative rast not supported for points/lines
    pfnTriRast = GetRasterizerFunc(rastState.sampleCount,
                                   rastState.bIsCenterPattern,
                                   false,
                                   SWR_INPUT_COVERAGE_NONE,
                                   EdgeValToEdgeState(ALL_EDGES_VALID),
                                   (pDC->pState->state.scissorsTileAligned == false));

    // make sure this macrotile intersects the triangle
    __m128i vXai = fpToFixedPoint(vXa);
    __m128i vYai = fpToFixedPoint(vYa);
    OSALIGNSIMD(SWR_RECT) bboxA;
    calcBoundingBoxInt(vXai, vYai, bboxA);

    if (!(bboxA.xmin > macroBoxRight || bboxA.xmin > scissorInFixedPoint.xmax ||
          bboxA.xmax - 1 < macroBoxLeft || bboxA.xmax - 1 < scissorInFixedPoint.xmin ||
          bboxA.ymin > macroBoxBottom || bboxA.ymin > scissorInFixedPoint.ymax ||
          bboxA.ymax - 1 < macroBoxTop || bboxA.ymax - 1 < scissorInFixedPoint.ymin))
    {
        // rasterize triangle
        pfnTriRast(pDC, workerId, macroTile, (void*)&newWorkDesc);
    }

    // triangle 1
    // v0,v1 -> v1,v1,v0
    vXa      = _mm_shuffle_ps(vX, vX, _MM_SHUFFLE(1, 0, 1, 1));
    vYa      = _mm_shuffle_ps(vY, vY, _MM_SHUFFLE(1, 0, 1, 1));
    vZa      = _mm_shuffle_ps(vZ, vZ, _MM_SHUFFLE(1, 0, 1, 1));
    vRecipWa = _mm_shuffle_ps(vRecipW, vRecipW, _MM_SHUFFLE(1, 0, 1, 1));

    vAdjust = _mm_mul_ps(vLineWidth, vBloat1);
    if (workDesc.triFlags.yMajor)
    {
        vXa = _mm_add_ps(vAdjust, vXa);
    }
    else
    {
        vYa = _mm_add_ps(vAdjust, vYa);
    }

    // Store triangle description for rasterizer
    _mm_store_ps((float*)&newTriBuffer[0], vXa);
    _mm_store_ps((float*)&newTriBuffer[4], vYa);
    _mm_store_ps((float*)&newTriBuffer[8], vZa);
    _mm_store_ps((float*)&newTriBuffer[12], vRecipWa);

    // binner bins 3 edges for lines as v0, v1, v1
    // tri1 needs v1, v1, v0
    for (uint32_t a = 0; a < workDesc.numAttribs; ++a)
    {
        __m128 vAttrib0 = _mm_load_ps(&workDesc.pAttribs[a * 12 + 0]);
        __m128 vAttrib1 = _mm_load_ps(&workDesc.pAttribs[a * 12 + 4]);

        _mm_store_ps((float*)&newAttribBuffer[a * 12 + 0], vAttrib1);
        _mm_store_ps((float*)&newAttribBuffer[a * 12 + 4], vAttrib1);
        _mm_store_ps((float*)&newAttribBuffer[a * 12 + 8], vAttrib0);
    }

    // store user clip distance for triangle 1
    if (numClipDist)
    {
        float* pOldBuffer = workDesc.pUserClipBuffer;
        float* pNewBuffer = newClipBuffer;
        for (uint32_t i = 0; i < numClipDist; ++i)
        {
            // read barycentric coeffs from binner
            float a = *(pOldBuffer++);
            float b = *(pOldBuffer++);

            // reconstruct original clip distance at vertices
            float c0 = a + b;
            float c1 = b;

            // construct triangle barycentrics
            *(pNewBuffer++) = c1 - c0;
            *(pNewBuffer++) = c1 - c0;
            *(pNewBuffer++) = c0;
        }
    }

    vXai = fpToFixedPoint(vXa);
    vYai = fpToFixedPoint(vYa);
    calcBoundingBoxInt(vXai, vYai, bboxA);

    if (!(bboxA.xmin > macroBoxRight || bboxA.xmin > scissorInFixedPoint.xmax ||
          bboxA.xmax - 1 < macroBoxLeft || bboxA.xmax - 1 < scissorInFixedPoint.xmin ||
          bboxA.ymin > macroBoxBottom || bboxA.ymin > scissorInFixedPoint.ymax ||
          bboxA.ymax - 1 < macroBoxTop || bboxA.ymax - 1 < scissorInFixedPoint.ymin))
    {
        // rasterize triangle
        pfnTriRast(pDC, workerId, macroTile, (void*)&newWorkDesc);
    }

    RDTSC_BEGIN(pDC->pContext->pBucketMgr, BERasterizeLine, 1);
}

void RasterizeSimplePoint(DRAW_CONTEXT* pDC, uint32_t workerId, uint32_t macroTile, void* pData)
{
#if KNOB_ENABLE_TOSS_POINTS
    if (KNOB_TOSS_BIN_TRIS)
    {
        return;
    }
#endif

    const TRIANGLE_WORK_DESC& workDesc     = *(const TRIANGLE_WORK_DESC*)pData;
    const BACKEND_FUNCS&      backendFuncs = pDC->pState->backendFuncs;

    // map x,y relative offsets from start of raster tile to bit position in
    // coverage mask for the point
    static const uint32_t coverageMap[8][8] = {{0, 1, 4, 5, 8, 9, 12, 13},
                                               {2, 3, 6, 7, 10, 11, 14, 15},
                                               {16, 17, 20, 21, 24, 25, 28, 29},
                                               {18, 19, 22, 23, 26, 27, 30, 31},
                                               {32, 33, 36, 37, 40, 41, 44, 45},
                                               {34, 35, 38, 39, 42, 43, 46, 47},
                                               {48, 49, 52, 53, 56, 57, 60, 61},
                                               {50, 51, 54, 55, 58, 59, 62, 63}};

    OSALIGNSIMD(SWR_TRIANGLE_DESC) triDesc = {};

    // pull point information from triangle buffer
    // @todo use structs for readability
    uint32_t tileAlignedX = *(uint32_t*)workDesc.pTriBuffer;
    uint32_t tileAlignedY = *(uint32_t*)(workDesc.pTriBuffer + 1);
    float    z            = *(workDesc.pTriBuffer + 2);

    // construct triangle descriptor for point
    // no interpolation, set up i,j for constant interpolation of z and attribs
    // @todo implement an optimized backend that doesn't require triangle information

    // compute coverage mask from x,y packed into the coverageMask flag
    // mask indices by the maximum valid index for x/y of coveragemap.
    uint32_t tX = workDesc.triFlags.coverageMask & 0x7;
    uint32_t tY = (workDesc.triFlags.coverageMask >> 4) & 0x7;
    for (uint32_t i = 0; i < _countof(triDesc.coverageMask); ++i)
    {
        triDesc.coverageMask[i] = 1ULL << coverageMap[tY][tX];
    }
    triDesc.anyCoveredSamples = triDesc.coverageMask[0];
    triDesc.innerCoverageMask = triDesc.coverageMask[0];

    // no persp divide needed for points
    triDesc.pAttribs = triDesc.pPerspAttribs = workDesc.pAttribs;
    triDesc.triFlags                         = workDesc.triFlags;
    triDesc.recipDet                         = 1.0f;
    triDesc.OneOverW[0] = triDesc.OneOverW[1] = triDesc.OneOverW[2] = 1.0f;
    triDesc.I[0] = triDesc.I[1] = triDesc.I[2] = 0.0f;
    triDesc.J[0] = triDesc.J[1] = triDesc.J[2] = 0.0f;
    triDesc.Z[0] = triDesc.Z[1] = triDesc.Z[2] = z;

    RenderOutputBuffers renderBuffers;
    GetRenderHotTiles(pDC,
                      workerId,
                      macroTile,
                      tileAlignedX >> KNOB_TILE_X_DIM_SHIFT,
                      tileAlignedY >> KNOB_TILE_Y_DIM_SHIFT,
                      renderBuffers,
                      triDesc.triFlags.renderTargetArrayIndex);

    RDTSC_BEGIN(pDC->pContext->pBucketMgr, BEPixelBackend, pDC->drawId);
    backendFuncs.pfnBackend(pDC, workerId, tileAlignedX, tileAlignedY, triDesc, renderBuffers);
    RDTSC_END(pDC->pContext->pBucketMgr, BEPixelBackend, 0);
}

void RasterizeTriPoint(DRAW_CONTEXT* pDC, uint32_t workerId, uint32_t macroTile, void* pData)
{
    const TRIANGLE_WORK_DESC& workDesc     = *(const TRIANGLE_WORK_DESC*)pData;
    const SWR_RASTSTATE&      rastState    = pDC->pState->state.rastState;
    const SWR_BACKEND_STATE&  backendState = pDC->pState->state.backendState;

    bool isPointSpriteTexCoordEnabled = backendState.pointSpriteTexCoordMask != 0;

    // load point vertex
    float x = *workDesc.pTriBuffer;
    float y = *(workDesc.pTriBuffer + 1);
    float z = *(workDesc.pTriBuffer + 2);

    // create a copy of the triangle buffer to write our adjusted vertices to
    OSALIGNSIMD(float) newTriBuffer[4 * 4];
    TRIANGLE_WORK_DESC newWorkDesc = workDesc;
    newWorkDesc.pTriBuffer         = &newTriBuffer[0];

    // create a copy of the attrib buffer to write our adjusted attribs to
    OSALIGNSIMD(float) newAttribBuffer[4 * 3 * SWR_VTX_NUM_SLOTS];
    newWorkDesc.pAttribs = &newAttribBuffer[0];

    newWorkDesc.pUserClipBuffer = workDesc.pUserClipBuffer;
    newWorkDesc.numAttribs      = workDesc.numAttribs;
    newWorkDesc.triFlags        = workDesc.triFlags;

    // construct two tris by bloating point by point size
    float halfPointSize = workDesc.triFlags.pointSize * 0.5f;
    float lowerX        = x - halfPointSize;
    float upperX        = x + halfPointSize;
    float lowerY        = y - halfPointSize;
    float upperY        = y + halfPointSize;

    // tri 0
    float* pBuf = &newTriBuffer[0];
    *pBuf++     = lowerX;
    *pBuf++     = lowerX;
    *pBuf++     = upperX;
    pBuf++;
    *pBuf++ = lowerY;
    *pBuf++ = upperY;
    *pBuf++ = upperY;
    pBuf++;
    _mm_store_ps(pBuf, _mm_set1_ps(z));
    _mm_store_ps(pBuf += 4, _mm_set1_ps(1.0f));

    // setup triangle rasterizer function
    PFN_WORK_FUNC pfnTriRast;
    // conservative rast not supported for points/lines
    pfnTriRast = GetRasterizerFunc(rastState.sampleCount,
                                   rastState.bIsCenterPattern,
                                   false,
                                   SWR_INPUT_COVERAGE_NONE,
                                   EdgeValToEdgeState(ALL_EDGES_VALID),
                                   (pDC->pState->state.scissorsTileAligned == false));

    // overwrite texcoords for point sprites
    if (isPointSpriteTexCoordEnabled)
    {
        // copy original attribs
        memcpy(&newAttribBuffer[0], workDesc.pAttribs, 4 * 3 * workDesc.numAttribs * sizeof(float));
        newWorkDesc.pAttribs = &newAttribBuffer[0];

        // overwrite texcoord for point sprites
        uint32_t texCoordMask   = backendState.pointSpriteTexCoordMask;
        unsigned long texCoordAttrib = 0;

        while (_BitScanForward(&texCoordAttrib, texCoordMask))
        {
            texCoordMask &= ~(1 << texCoordAttrib);
            __m128* pTexAttrib = (__m128*)&newAttribBuffer[0] + 3 * texCoordAttrib;
            if (rastState.pointSpriteTopOrigin)
            {
                pTexAttrib[0] = _mm_set_ps(1, 0, 0, 0);
                pTexAttrib[1] = _mm_set_ps(1, 0, 1, 0);
                pTexAttrib[2] = _mm_set_ps(1, 0, 1, 1);
            }
            else
            {
                pTexAttrib[0] = _mm_set_ps(1, 0, 1, 0);
                pTexAttrib[1] = _mm_set_ps(1, 0, 0, 0);
                pTexAttrib[2] = _mm_set_ps(1, 0, 0, 1);
            }
        }
    }
    else
    {
        // no texcoord overwrite, can reuse the attrib buffer from frontend
        newWorkDesc.pAttribs = workDesc.pAttribs;
    }

    pfnTriRast(pDC, workerId, macroTile, (void*)&newWorkDesc);

    // tri 1
    pBuf    = &newTriBuffer[0];
    *pBuf++ = lowerX;
    *pBuf++ = upperX;
    *pBuf++ = upperX;
    pBuf++;
    *pBuf++ = lowerY;
    *pBuf++ = upperY;
    *pBuf++ = lowerY;
    // z, w unchanged

    if (isPointSpriteTexCoordEnabled)
    {
        uint32_t texCoordMask   = backendState.pointSpriteTexCoordMask;
        unsigned long texCoordAttrib = 0;

        while (_BitScanForward(&texCoordAttrib, texCoordMask))
        {
            texCoordMask &= ~(1 << texCoordAttrib);
            __m128* pTexAttrib = (__m128*)&newAttribBuffer[0] + 3 * texCoordAttrib;
            if (rastState.pointSpriteTopOrigin)
            {
                pTexAttrib[0] = _mm_set_ps(1, 0, 0, 0);
                pTexAttrib[1] = _mm_set_ps(1, 0, 1, 1);
                pTexAttrib[2] = _mm_set_ps(1, 0, 0, 1);
            }
            else
            {
                pTexAttrib[0] = _mm_set_ps(1, 0, 1, 0);
                pTexAttrib[1] = _mm_set_ps(1, 0, 0, 1);
                pTexAttrib[2] = _mm_set_ps(1, 0, 1, 1);
            }
        }
    }

    pfnTriRast(pDC, workerId, macroTile, (void*)&newWorkDesc);
}

void InitRasterizerFunctions()
{
    InitRasterizerFuncs();
}

// Selector for correct templated RasterizeTriangle function
PFN_WORK_FUNC GetRasterizerFunc(SWR_MULTISAMPLE_COUNT numSamples,
                                bool                  IsCenter,
                                bool                  IsConservative,
                                SWR_INPUT_COVERAGE    InputCoverage,
                                uint32_t              EdgeEnable,
                                bool                  RasterizeScissorEdges)
{
    SWR_ASSERT(numSamples >= 0 && numSamples < SWR_MULTISAMPLE_TYPE_COUNT);
    SWR_ASSERT(InputCoverage >= 0 && InputCoverage < SWR_INPUT_COVERAGE_COUNT);
    SWR_ASSERT(EdgeEnable < STATE_VALID_TRI_EDGE_COUNT);

    PFN_WORK_FUNC func = gRasterizerFuncs[numSamples][IsCenter][IsConservative][InputCoverage]
                                         [EdgeEnable][RasterizeScissorEdges];
    SWR_ASSERT(func);

    return func;
}
