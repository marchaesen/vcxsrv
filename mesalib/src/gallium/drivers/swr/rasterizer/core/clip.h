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
 * @file clip.h
 *
 * @brief Definitions for clipping
 *
 ******************************************************************************/
#pragma once

#include "common/simdintrin.h"
#include "core/context.h"
#include "core/pa.h"
#include "rdtsc_core.h"

enum SWR_CLIPCODES
{
// Shift clip codes out of the mantissa to prevent denormalized values when used in float compare.
// Guardband is able to use a single high-bit with 4 separate LSBs, because it computes a union,
// rather than intersection, of clipcodes.
#define CLIPCODE_SHIFT 23
    FRUSTUM_LEFT   = (0x01 << CLIPCODE_SHIFT),
    FRUSTUM_TOP    = (0x02 << CLIPCODE_SHIFT),
    FRUSTUM_RIGHT  = (0x04 << CLIPCODE_SHIFT),
    FRUSTUM_BOTTOM = (0x08 << CLIPCODE_SHIFT),

    FRUSTUM_NEAR = (0x10 << CLIPCODE_SHIFT),
    FRUSTUM_FAR  = (0x20 << CLIPCODE_SHIFT),

    NEGW = (0x40 << CLIPCODE_SHIFT),

    GUARDBAND_LEFT   = (0x80 << CLIPCODE_SHIFT | 0x1),
    GUARDBAND_TOP    = (0x80 << CLIPCODE_SHIFT | 0x2),
    GUARDBAND_RIGHT  = (0x80 << CLIPCODE_SHIFT | 0x4),
    GUARDBAND_BOTTOM = (0x80 << CLIPCODE_SHIFT | 0x8)
};

#define GUARDBAND_CLIP_MASK                                                          \
    (FRUSTUM_NEAR | FRUSTUM_FAR | GUARDBAND_LEFT | GUARDBAND_TOP | GUARDBAND_RIGHT | \
     GUARDBAND_BOTTOM | NEGW)
#define FRUSTUM_CLIP_MASK \
    (FRUSTUM_NEAR | FRUSTUM_FAR | FRUSTUM_LEFT | FRUSTUM_RIGHT | FRUSTUM_TOP | FRUSTUM_BOTTOM)

template <typename SIMD_T>
void ComputeClipCodes(const API_STATE&       state,
                      const Vec4<SIMD_T>&    vertex,
                      Float<SIMD_T>&         clipCodes,
                      Integer<SIMD_T> const& viewportIndexes)
{
    clipCodes = SIMD_T::setzero_ps();

    // -w
    Float<SIMD_T> vNegW = SIMD_T::mul_ps(vertex.w, SIMD_T::set1_ps(-1.0f));

    // FRUSTUM_LEFT
    Float<SIMD_T> vRes = SIMD_T::cmplt_ps(vertex.x, vNegW);
    clipCodes          = SIMD_T::and_ps(vRes, SIMD_T::castsi_ps(SIMD_T::set1_epi32(FRUSTUM_LEFT)));

    // FRUSTUM_TOP
    vRes      = SIMD_T::cmplt_ps(vertex.y, vNegW);
    clipCodes = SIMD_T::or_ps(
        clipCodes, SIMD_T::and_ps(vRes, SIMD_T::castsi_ps(SIMD_T::set1_epi32(FRUSTUM_TOP))));

    // FRUSTUM_RIGHT
    vRes      = SIMD_T::cmpgt_ps(vertex.x, vertex.w);
    clipCodes = SIMD_T::or_ps(
        clipCodes, SIMD_T::and_ps(vRes, SIMD_T::castsi_ps(SIMD_T::set1_epi32(FRUSTUM_RIGHT))));

    // FRUSTUM_BOTTOM
    vRes      = SIMD_T::cmpgt_ps(vertex.y, vertex.w);
    clipCodes = SIMD_T::or_ps(
        clipCodes, SIMD_T::and_ps(vRes, SIMD_T::castsi_ps(SIMD_T::set1_epi32(FRUSTUM_BOTTOM))));

    if (state.rastState.depthClipEnable)
    {
        // FRUSTUM_NEAR
        // DX clips depth [0..w], GL clips [-w..w]
        if (state.rastState.clipHalfZ)
        {
            vRes = SIMD_T::cmplt_ps(vertex.z, SIMD_T::setzero_ps());
        }
        else
        {
            vRes = SIMD_T::cmplt_ps(vertex.z, vNegW);
        }
        clipCodes = SIMD_T::or_ps(
            clipCodes, SIMD_T::and_ps(vRes, SIMD_T::castsi_ps(SIMD_T::set1_epi32(FRUSTUM_NEAR))));

        // FRUSTUM_FAR
        vRes      = SIMD_T::cmpgt_ps(vertex.z, vertex.w);
        clipCodes = SIMD_T::or_ps(
            clipCodes, SIMD_T::and_ps(vRes, SIMD_T::castsi_ps(SIMD_T::set1_epi32(FRUSTUM_FAR))));
    }

    // NEGW
    vRes = SIMD_T::cmple_ps(vertex.w, SIMD_T::setzero_ps());
    clipCodes =
        SIMD_T::or_ps(clipCodes, SIMD_T::and_ps(vRes, SIMD_T::castsi_ps(SIMD_T::set1_epi32(NEGW))));

    // GUARDBAND_LEFT
    Float<SIMD_T> gbMult = SIMD_T::mul_ps(vNegW,
                                          SIMD_T::template i32gather_ps<ScaleFactor<SIMD_T>(4)>(
                                              &state.gbState.left[0], viewportIndexes));
    vRes                 = SIMD_T::cmplt_ps(vertex.x, gbMult);
    clipCodes            = SIMD_T::or_ps(
        clipCodes, SIMD_T::and_ps(vRes, SIMD_T::castsi_ps(SIMD_T::set1_epi32(GUARDBAND_LEFT))));

    // GUARDBAND_TOP
    gbMult    = SIMD_T::mul_ps(vNegW,
                            SIMD_T::template i32gather_ps<ScaleFactor<SIMD_T>(4)>(
                                &state.gbState.top[0], viewportIndexes));
    vRes      = SIMD_T::cmplt_ps(vertex.y, gbMult);
    clipCodes = SIMD_T::or_ps(
        clipCodes, SIMD_T::and_ps(vRes, SIMD_T::castsi_ps(SIMD_T::set1_epi32(GUARDBAND_TOP))));

    // GUARDBAND_RIGHT
    gbMult    = SIMD_T::mul_ps(vertex.w,
                            SIMD_T::template i32gather_ps<ScaleFactor<SIMD_T>(4)>(
                                &state.gbState.right[0], viewportIndexes));
    vRes      = SIMD_T::cmpgt_ps(vertex.x, gbMult);
    clipCodes = SIMD_T::or_ps(
        clipCodes, SIMD_T::and_ps(vRes, SIMD_T::castsi_ps(SIMD_T::set1_epi32(GUARDBAND_RIGHT))));

    // GUARDBAND_BOTTOM
    gbMult    = SIMD_T::mul_ps(vertex.w,
                            SIMD_T::template i32gather_ps<ScaleFactor<SIMD_T>(4)>(
                                &state.gbState.bottom[0], viewportIndexes));
    vRes      = SIMD_T::cmpgt_ps(vertex.y, gbMult);
    clipCodes = SIMD_T::or_ps(
        clipCodes, SIMD_T::and_ps(vRes, SIMD_T::castsi_ps(SIMD_T::set1_epi32(GUARDBAND_BOTTOM))));
}

template <typename SIMD_T>
struct BinnerChooser
{
};

template <>
struct BinnerChooser<SIMD256>
{
    PFN_PROCESS_PRIMS pfnBinFunc;

    BinnerChooser(uint32_t numVertsPerPrim, uint32_t conservativeRast)
        :
        pfnBinFunc(nullptr)
    {
        if (numVertsPerPrim == 3)
        {
            pfnBinFunc = GetBinTrianglesFunc(conservativeRast > 0);

        }
        else if (numVertsPerPrim == 2)
        {
            pfnBinFunc = BinLines;
        }
        else
        {
            SWR_ASSERT(0 && "Unexpected points in clipper.");
        }
    }

    BinnerChooser(PRIMITIVE_TOPOLOGY topology, uint32_t conservativeRast)
        :
        pfnBinFunc(nullptr)
    {
        switch (topology)
        {
        case TOP_POINT_LIST:
            pfnBinFunc = BinPoints;
            break;
        case TOP_LINE_LIST:
        case TOP_LINE_STRIP:
        case TOP_LINE_LOOP:
        case TOP_LINE_LIST_ADJ:
        case TOP_LISTSTRIP_ADJ:
            pfnBinFunc = BinLines;
            break;
        default:
            pfnBinFunc = GetBinTrianglesFunc(conservativeRast > 0);
            break;
        };
    }

    void BinFunc(DRAW_CONTEXT*           pDC,
                 PA_STATE&               pa,
                 uint32_t                workerId,
                 SIMD256::Vec4           prims[],
                 uint32_t                primMask,
                 SIMD256::Integer const& primID,
                 SIMD256::Integer&       viewportIdx,
                 SIMD256::Integer&       rtIdx)
    {
        SWR_ASSERT(pfnBinFunc != nullptr);

        pfnBinFunc(pDC, pa, workerId, prims, primMask, primID, viewportIdx, rtIdx);
    }
};

#if USE_SIMD16_FRONTEND
template <>
struct BinnerChooser<SIMD512>
{
    PFN_PROCESS_PRIMS_SIMD16 pfnBinFunc;

    BinnerChooser(uint32_t numVertsPerPrim, uint32_t conservativeRast)
        :
        pfnBinFunc(nullptr)
    {
        if (numVertsPerPrim == 3)
        {
            pfnBinFunc = GetBinTrianglesFunc_simd16(conservativeRast > 0);

        }
        else if (numVertsPerPrim == 2)
        {
            pfnBinFunc = BinLines_simd16;
        }
        else
        {
            SWR_ASSERT(0 && "Unexpected points in clipper.");
        }
    }

    BinnerChooser(PRIMITIVE_TOPOLOGY topology, uint32_t conservativeRast)
        :
        pfnBinFunc(nullptr)
    {
        switch (topology)
        {
        case TOP_POINT_LIST:
            pfnBinFunc = BinPoints_simd16;
            break;
        case TOP_LINE_LIST:
        case TOP_LINE_STRIP:
        case TOP_LINE_LOOP:
        case TOP_LINE_LIST_ADJ:
        case TOP_LISTSTRIP_ADJ:
            pfnBinFunc = BinLines_simd16;
            break;
        default:
            pfnBinFunc = GetBinTrianglesFunc_simd16(conservativeRast > 0);
            break;
        };
    }

    void BinFunc(DRAW_CONTEXT*           pDC,
                 PA_STATE&               pa,
                 uint32_t                workerId,
                 SIMD512::Vec4           prims[],
                 uint32_t                primMask,
                 SIMD512::Integer const& primID,
                 SIMD512::Integer&       viewportIdx,
                 SIMD512::Integer&       rtIdx)
    {
        SWR_ASSERT(pfnBinFunc != nullptr);

        pfnBinFunc(pDC, pa, workerId, prims, primMask, primID, viewportIdx, rtIdx);
    }
};

#endif
template <typename SIMD_T>
struct SimdHelper
{
};

template <>
struct SimdHelper<SIMD256>
{
    static SIMD256::Float insert_lo_ps(SIMD256::Float a) { return a; }

    static SIMD256::Mask cmpeq_ps_mask(SIMD256::Float a, SIMD256::Float b)
    {
        return SIMD256::movemask_ps(SIMD256::cmpeq_ps(a, b));
    }
};

#if USE_SIMD16_FRONTEND
template <>
struct SimdHelper<SIMD512>
{
    static SIMD512::Float insert_lo_ps(SIMD256::Float a)
    {
        return SIMD512::insert_ps<0>(SIMD512::setzero_ps(), a);
    }

    static SIMD512::Mask cmpeq_ps_mask(SIMD512::Float a, SIMD512::Float b)
    {
        return SIMD512::cmp_ps_mask<SIMD16::CompareType::EQ_OQ>(a, b);
    }
};
#endif

template <typename SIMD_T, uint32_t NumVertsPerPrimT>
class Clipper
{
public:
    INLINE Clipper(uint32_t in_workerId, DRAW_CONTEXT* in_pDC) :
        workerId(in_workerId), pDC(in_pDC), state(GetApiState(in_pDC))
    {
        static_assert(NumVertsPerPrimT >= 1 && NumVertsPerPrimT <= 3, "Invalid NumVertsPerPrim");
        THREAD_DATA &thread_data = in_pDC->pContext->threadPool.pThreadData[workerId];

        if (thread_data.clipperData == nullptr)
        {
            // 7 vertex temp data
            // 7 post-clipped vertices
            // 2 transposed verts for binning
            size_t alloc_size = sizeof(SIMDVERTEX_T<SIMD_T>) * (7 + 7 + 2);
            thread_data.clipperData = AlignedMalloc(alloc_size, KNOB_SIMD16_BYTES);
        }
        SWR_ASSERT(thread_data.clipperData);

        this->clippedVerts = (SIMDVERTEX_T<SIMD_T>*)thread_data.clipperData;
        this->tmpVerts = this->clippedVerts + 7;
        this->transposedVerts = this->tmpVerts + 7;
    }

    void ComputeClipCodes(Vec4<SIMD_T> vertex[], const Integer<SIMD_T>& viewportIndexes)
    {
        for (uint32_t i = 0; i < NumVertsPerPrimT; ++i)
        {
            ::ComputeClipCodes<SIMD_T>(state, vertex[i], clipCodes[i], viewportIndexes);
        }
    }

    Float<SIMD_T> ComputeClipCodeIntersection()
    {
        Float<SIMD_T> result = clipCodes[0];

        for (uint32_t i = 1; i < NumVertsPerPrimT; ++i)
        {
            result = SIMD_T::and_ps(result, clipCodes[i]);
        }

        return result;
    }

    Float<SIMD_T> ComputeClipCodeUnion()
    {
        Float<SIMD_T> result = clipCodes[0];

        for (uint32_t i = 1; i < NumVertsPerPrimT; ++i)
        {
            result = SIMD_T::or_ps(result, clipCodes[i]);
        }

        return result;
    }

    int ComputeClipMask()
    {
        Float<SIMD_T> clipUnion = ComputeClipCodeUnion();

        clipUnion =
            SIMD_T::and_ps(clipUnion, SIMD_T::castsi_ps(SIMD_T::set1_epi32(GUARDBAND_CLIP_MASK)));

        return SIMD_T::movemask_ps(SIMD_T::cmpneq_ps(clipUnion, SIMD_T::setzero_ps()));
    }

    // clipper is responsible for culling any prims with NAN coordinates
    int ComputeNaNMask(Vec4<SIMD_T> prim[])
    {
        Float<SIMD_T> vNanMask = SIMD_T::setzero_ps();

        for (uint32_t e = 0; e < NumVertsPerPrimT; ++e)
        {
            Float<SIMD_T> vNan01 =
                SIMD_T::template cmp_ps<SIMD_T::CompareType::UNORD_Q>(prim[e].v[0], prim[e].v[1]);
            vNanMask = SIMD_T::or_ps(vNanMask, vNan01);

            Float<SIMD_T> vNan23 =
                SIMD_T::template cmp_ps<SIMD_T::CompareType::UNORD_Q>(prim[e].v[2], prim[e].v[3]);
            vNanMask = SIMD_T::or_ps(vNanMask, vNan23);
        }

        return SIMD_T::movemask_ps(vNanMask);
    }

    int ComputeUserClipCullMask(PA_STATE& pa, Vec4<SIMD_T> prim[])
    {
        uint8_t  cullMask             = state.backendState.cullDistanceMask;
        uint32_t vertexClipCullOffset = state.backendState.vertexClipCullOffset;

        Float<SIMD_T> vClipCullMask = SIMD_T::setzero_ps();

        Vec4<SIMD_T> vClipCullDistLo[3];
        Vec4<SIMD_T> vClipCullDistHi[3];

        pa.Assemble(vertexClipCullOffset, vClipCullDistLo);
        pa.Assemble(vertexClipCullOffset + 1, vClipCullDistHi);

        unsigned long index;
        while (_BitScanForward(&index, cullMask))
        {
            cullMask &= ~(1 << index);
            uint32_t slot      = index >> 2;
            uint32_t component = index & 0x3;

            Float<SIMD_T> vCullMaskElem = SIMD_T::set1_ps(-1.0f);
            for (uint32_t e = 0; e < NumVertsPerPrimT; ++e)
            {
                Float<SIMD_T> vCullComp;
                if (slot == 0)
                {
                    vCullComp = vClipCullDistLo[e][component];
                }
                else
                {
                    vCullComp = vClipCullDistHi[e][component];
                }

                // cull if cull distance < 0 || NAN
                Float<SIMD_T> vCull = SIMD_T::template cmp_ps<SIMD_T::CompareType::NLE_UQ>(
                    SIMD_T::setzero_ps(), vCullComp);
                vCullMaskElem = SIMD_T::and_ps(vCullMaskElem, vCull);
            }
            vClipCullMask = SIMD_T::or_ps(vClipCullMask, vCullMaskElem);
        }

        // clipper should also discard any primitive with NAN clip distance
        uint8_t clipMask = state.backendState.clipDistanceMask;
        while (_BitScanForward(&index, clipMask))
        {
            clipMask &= ~(1 << index);
            uint32_t slot      = index >> 2;
            uint32_t component = index & 0x3;

            Float<SIMD_T> vCullMaskElem = SIMD_T::set1_ps(-1.0f);
            for (uint32_t e = 0; e < NumVertsPerPrimT; ++e)
            {
                Float<SIMD_T> vClipComp;
                if (slot == 0)
                {
                    vClipComp = vClipCullDistLo[e][component];
                }
                else
                {
                    vClipComp = vClipCullDistHi[e][component];
                }

                Float<SIMD_T> vClip =
                    SIMD_T::template cmp_ps<SIMD_T::CompareType::UNORD_Q>(vClipComp, vClipComp);
                Float<SIMD_T> vCull = SIMD_T::template cmp_ps<SIMD_T::CompareType::NLE_UQ>(
                    SIMD_T::setzero_ps(), vClipComp);
                vCullMaskElem = SIMD_T::and_ps(vCullMaskElem, vCull);
                vClipCullMask = SIMD_T::or_ps(vClipCullMask, vClip);
            }
            vClipCullMask = SIMD_T::or_ps(vClipCullMask, vCullMaskElem);
        }

        return SIMD_T::movemask_ps(vClipCullMask);
    }

    void ClipSimd(const Vec4<SIMD_T>     prim[],
                  const Float<SIMD_T>&   vPrimMask,
                  const Float<SIMD_T>&   vClipMask,
                  PA_STATE&              pa,
                  const Integer<SIMD_T>& vPrimId,
                  const Integer<SIMD_T>& vViewportIdx,
                  const Integer<SIMD_T>& vRtIdx)
    {
        // input/output vertex store for clipper
        SIMDVERTEX_T<SIMD_T>* vertices = this->clippedVerts;

        uint32_t constantInterpMask = state.backendState.constantInterpolationMask;
        uint32_t provokingVertex    = 0;
        if (pa.binTopology == TOP_TRIANGLE_FAN)
        {
            provokingVertex = state.frontendState.provokingVertex.triFan;
        }
        ///@todo: line topology for wireframe?

        // assemble pos
        Vec4<SIMD_T> tmpVector[NumVertsPerPrimT];
        for (uint32_t i = 0; i < NumVertsPerPrimT; ++i)
        {
            vertices[i].attrib[VERTEX_POSITION_SLOT] = prim[i];
        }

        // assemble attribs
        const SWR_BACKEND_STATE& backendState = state.backendState;

        int32_t maxSlot = -1;
        for (uint32_t slot = 0; slot < backendState.numAttributes; ++slot)
        {
            // Compute absolute attrib slot in vertex array
            uint32_t mapSlot =
                backendState.swizzleEnable ? backendState.swizzleMap[slot].sourceAttrib : slot;
            maxSlot            = std::max<int32_t>(maxSlot, mapSlot);
            uint32_t inputSlot = backendState.vertexAttribOffset + mapSlot;

            pa.Assemble(inputSlot, tmpVector);

            // if constant interpolation enabled for this attribute, assign the provoking
            // vertex values to all edges
            if (CheckBit(constantInterpMask, slot))
            {
                for (uint32_t i = 0; i < NumVertsPerPrimT; ++i)
                {
                    vertices[i].attrib[inputSlot] = tmpVector[provokingVertex];
                }
            }
            else
            {
                for (uint32_t i = 0; i < NumVertsPerPrimT; ++i)
                {
                    vertices[i].attrib[inputSlot] = tmpVector[i];
                }
            }
        }

        // assemble user clip distances if enabled
        uint32_t vertexClipCullSlot = state.backendState.vertexClipCullOffset;
        if (state.backendState.clipDistanceMask & 0xf)
        {
            pa.Assemble(vertexClipCullSlot, tmpVector);
            for (uint32_t i = 0; i < NumVertsPerPrimT; ++i)
            {
                vertices[i].attrib[vertexClipCullSlot] = tmpVector[i];
            }
        }

        if (state.backendState.clipDistanceMask & 0xf0)
        {
            pa.Assemble(vertexClipCullSlot + 1, tmpVector);
            for (uint32_t i = 0; i < NumVertsPerPrimT; ++i)
            {
                vertices[i].attrib[vertexClipCullSlot + 1] = tmpVector[i];
            }
        }

        uint32_t numAttribs = maxSlot + 1;

        Integer<SIMD_T> vNumClippedVerts =
            ClipPrims((float*)&vertices[0], vPrimMask, vClipMask, numAttribs);

        BinnerChooser<SIMD_T> binner(NumVertsPerPrimT,
                                     pa.pDC->pState->state.rastState.conservativeRast);

        // set up new PA for binning clipped primitives
        PRIMITIVE_TOPOLOGY clipTopology = TOP_UNKNOWN;
        if (NumVertsPerPrimT == 3)
        {
            clipTopology = TOP_TRIANGLE_FAN;

            // so that the binner knows to bloat wide points later
            if (pa.binTopology == TOP_POINT_LIST)
            {
                clipTopology = TOP_POINT_LIST;
            }
            else if (pa.binTopology == TOP_RECT_LIST)
            {
                clipTopology = TOP_RECT_LIST;
            }
        }
        else if (NumVertsPerPrimT == 2)
        {
            clipTopology = TOP_LINE_LIST;
        }
        else
        {
            SWR_ASSERT(0 && "Unexpected points in clipper.");
        }

        const uint32_t* pVertexCount = reinterpret_cast<const uint32_t*>(&vNumClippedVerts);
        const uint32_t* pPrimitiveId = reinterpret_cast<const uint32_t*>(&vPrimId);
        const uint32_t* pViewportIdx = reinterpret_cast<const uint32_t*>(&vViewportIdx);
        const uint32_t* pRtIdx       = reinterpret_cast<const uint32_t*>(&vRtIdx);

        const SIMD256::Integer vOffsets =
            SIMD256::set_epi32(0 * sizeof(SIMDVERTEX_T<SIMD_T>), // unused lane
                               6 * sizeof(SIMDVERTEX_T<SIMD_T>),
                               5 * sizeof(SIMDVERTEX_T<SIMD_T>),
                               4 * sizeof(SIMDVERTEX_T<SIMD_T>),
                               3 * sizeof(SIMDVERTEX_T<SIMD_T>),
                               2 * sizeof(SIMDVERTEX_T<SIMD_T>),
                               1 * sizeof(SIMDVERTEX_T<SIMD_T>),
                               0 * sizeof(SIMDVERTEX_T<SIMD_T>));

        // only need to gather 7 verts
        // @todo dynamic mask based on actual # of verts generated per lane
        const SIMD256::Float vMask = SIMD256::set_ps(0, -1, -1, -1, -1, -1, -1, -1);

        uint32_t numClippedPrims = 0;

        // transpose clipper output so that each lane's vertices are in SIMD order
        // set aside space for 2 vertices, as the PA will try to read up to 16 verts
        // for triangle fan
        SIMDVERTEX_T<SIMD_T>*  transposedPrims = this->transposedVerts;

        uint32_t              numInputPrims = pa.NumPrims();
        for (uint32_t inputPrim = 0; inputPrim < numInputPrims; ++inputPrim)
        {
            uint32_t numEmittedVerts = pVertexCount[inputPrim];
            if (numEmittedVerts < NumVertsPerPrimT)
            {
                continue;
            }
            SWR_ASSERT(numEmittedVerts <= 7, "Unexpected vertex count from clipper.");

            uint32_t numEmittedPrims = GetNumPrims(clipTopology, numEmittedVerts);
            SWR_ASSERT(numEmittedPrims <= 7, "Unexpected primitive count from clipper.");

            numClippedPrims += numEmittedPrims;

            // tranpose clipper output so that each lane's vertices are in SIMD order
            // set aside space for 2 vertices, as the PA will try to read up to 16 verts
            // for triangle fan

            // transpose pos
            float const* pBase =
                reinterpret_cast<float const*>(&vertices[0].attrib[VERTEX_POSITION_SLOT]) +
                inputPrim;

            for (uint32_t c = 0; c < 4; ++c)
            {
                SIMD256::Float temp =
                    SIMD256::mask_i32gather_ps(SIMD256::setzero_ps(), pBase, vOffsets, vMask);
                transposedPrims[0].attrib[VERTEX_POSITION_SLOT][c] =
                    SimdHelper<SIMD_T>::insert_lo_ps(temp);
                pBase = PtrAdd(pBase, sizeof(Float<SIMD_T>));
            }

            // transpose attribs
            pBase = reinterpret_cast<float const*>(
                        &vertices[0].attrib[backendState.vertexAttribOffset]) +
                    inputPrim;

            for (uint32_t attrib = 0; attrib < numAttribs; ++attrib)
            {
                uint32_t attribSlot = backendState.vertexAttribOffset + attrib;

                for (uint32_t c = 0; c < 4; ++c)
                {
                    SIMD256::Float temp =
                        SIMD256::mask_i32gather_ps(SIMD256::setzero_ps(), pBase, vOffsets, vMask);
                    transposedPrims[0].attrib[attribSlot][c] =
                        SimdHelper<SIMD_T>::insert_lo_ps(temp);
                    pBase = PtrAdd(pBase, sizeof(Float<SIMD_T>));
                }
            }

            // transpose user clip distances if enabled
            uint32_t vertexClipCullSlot = backendState.vertexClipCullOffset;
            if (state.backendState.clipDistanceMask & 0x0f)
            {
                pBase = reinterpret_cast<float const*>(&vertices[0].attrib[vertexClipCullSlot]) +
                        inputPrim;

                for (uint32_t c = 0; c < 4; ++c)
                {
                    SIMD256::Float temp =
                        SIMD256::mask_i32gather_ps(SIMD256::setzero_ps(), pBase, vOffsets, vMask);
                    transposedPrims[0].attrib[vertexClipCullSlot][c] =
                        SimdHelper<SIMD_T>::insert_lo_ps(temp);
                    pBase = PtrAdd(pBase, sizeof(Float<SIMD_T>));
                }
            }

            if (state.backendState.clipDistanceMask & 0xf0)
            {
                pBase =
                    reinterpret_cast<float const*>(&vertices[0].attrib[vertexClipCullSlot + 1]) +
                    inputPrim;

                for (uint32_t c = 0; c < 4; ++c)
                {
                    SIMD256::Float temp =
                        SIMD256::mask_i32gather_ps(SIMD256::setzero_ps(), pBase, vOffsets, vMask);
                    transposedPrims[0].attrib[vertexClipCullSlot + 1][c] =
                        SimdHelper<SIMD_T>::insert_lo_ps(temp);
                    pBase = PtrAdd(pBase, sizeof(Float<SIMD_T>));
                }
            }

            PA_STATE_OPT clipPA(pDC,
                                numEmittedPrims,
                                reinterpret_cast<uint8_t*>(&transposedPrims[0]),
                                numEmittedVerts,
                                SWR_VTX_NUM_SLOTS,
                                true,
                                NumVertsPerPrimT,
                                clipTopology);
            clipPA.viewportArrayActive = pa.viewportArrayActive;
            clipPA.rtArrayActive       = pa.rtArrayActive;

            static const uint32_t primMaskMap[] = {0x0, 0x1, 0x3, 0x7, 0xf, 0x1f, 0x3f, 0x7f};

            const uint32_t primMask = primMaskMap[numEmittedPrims];

            const Integer<SIMD_T> primID      = SIMD_T::set1_epi32(pPrimitiveId[inputPrim]);
            const Integer<SIMD_T> viewportIdx = SIMD_T::set1_epi32(pViewportIdx[inputPrim]);
            const Integer<SIMD_T> rtIdx       = SIMD_T::set1_epi32(pRtIdx[inputPrim]);

            while (clipPA.GetNextStreamOutput())
            {
                do
                {
                    Vec4<SIMD_T> attrib[NumVertsPerPrimT];

                    bool assemble = clipPA.Assemble(VERTEX_POSITION_SLOT, attrib);

                    if (assemble)
                    {
                        binner.pfnBinFunc(
                            pDC, clipPA, workerId, attrib, primMask, primID, viewportIdx, rtIdx);
                    }

                } while (clipPA.NextPrim());
            }
        }

        // update global pipeline stat
        UPDATE_STAT_FE(CPrimitives, numClippedPrims);
    }

    void ExecuteStage(PA_STATE&              pa,
                      Vec4<SIMD_T>           prim[],
                      uint32_t               primMask,
                      Integer<SIMD_T> const& primId,
                      Integer<SIMD_T> const& viewportIdx,
                      Integer<SIMD_T> const& rtIdx)
    {
        SWR_ASSERT(pa.pDC != nullptr);

        BinnerChooser<SIMD_T> binner(pa.binTopology,
                                     pa.pDC->pState->state.rastState.conservativeRast);

        // update clipper invocations pipeline stat
        uint32_t numInvoc = _mm_popcnt_u32(primMask);
        UPDATE_STAT_FE(CInvocations, numInvoc);

        ComputeClipCodes(prim, viewportIdx);

        // cull prims with NAN coords
        primMask &= ~ComputeNaNMask(prim);

        // user cull distance cull
        if (state.backendState.cullDistanceMask | state.backendState.clipDistanceMask)
        {
            primMask &= ~ComputeUserClipCullMask(pa, prim);
        }

        Float<SIMD_T> clipIntersection = ComputeClipCodeIntersection();
        // Mask out non-frustum codes
        clipIntersection = SIMD_T::and_ps(clipIntersection,
                                          SIMD_T::castsi_ps(SIMD_T::set1_epi32(FRUSTUM_CLIP_MASK)));

        // cull prims outside view frustum
        int validMask =
            primMask & SimdHelper<SIMD_T>::cmpeq_ps_mask(clipIntersection, SIMD_T::setzero_ps());

        // skip clipping for points
        uint32_t clipMask = 0;
        if (NumVertsPerPrimT != 1)
        {
            clipMask = validMask & ComputeClipMask();
        }

        AR_EVENT(ClipInfoEvent(numInvoc, validMask, clipMask));

        if (clipMask)
        {
            RDTSC_BEGIN(pa.pDC->pContext->pBucketMgr, FEGuardbandClip, pa.pDC->drawId);
            // we have to clip tris, execute the clipper, which will also
            // call the binner
            ClipSimd(prim,
                     SIMD_T::vmask_ps(validMask),
                     SIMD_T::vmask_ps(clipMask),
                     pa,
                     primId,
                     viewportIdx,
                     rtIdx);
            RDTSC_END(pa.pDC->pContext->pBucketMgr, FEGuardbandClip, 1);
        }
        else if (validMask)
        {
            // update CPrimitives pipeline state
            UPDATE_STAT_FE(CPrimitives, _mm_popcnt_u32(validMask));

            // forward valid prims directly to binner
            binner.pfnBinFunc(
                this->pDC, pa, this->workerId, prim, validMask, primId, viewportIdx, rtIdx);
        }
    }

private:
    Float<SIMD_T> ComputeInterpFactor(Float<SIMD_T> const& boundaryCoord0,
                                      Float<SIMD_T> const& boundaryCoord1)
    {
        return SIMD_T::div_ps(boundaryCoord0, SIMD_T::sub_ps(boundaryCoord0, boundaryCoord1));
    }

    Integer<SIMD_T>
    ComputeOffsets(uint32_t attrib, Integer<SIMD_T> const& vIndices, uint32_t component)
    {
        const uint32_t simdVertexStride = sizeof(SIMDVERTEX_T<SIMD_T>);
        const uint32_t componentStride  = sizeof(Float<SIMD_T>);
        const uint32_t attribStride     = sizeof(Vec4<SIMD_T>);

        static const OSALIGNSIMD16(uint32_t) elemOffset[16] = {
            0 * sizeof(float),
            1 * sizeof(float),
            2 * sizeof(float),
            3 * sizeof(float),
            4 * sizeof(float),
            5 * sizeof(float),
            6 * sizeof(float),
            7 * sizeof(float),
            8 * sizeof(float),
            9 * sizeof(float),
            10 * sizeof(float),
            11 * sizeof(float),
            12 * sizeof(float),
            13 * sizeof(float),
            14 * sizeof(float),
            15 * sizeof(float),
        };

        static_assert(sizeof(Integer<SIMD_T>) <= sizeof(elemOffset),
                      "Clipper::ComputeOffsets, Increase number of element offsets.");

        Integer<SIMD_T> vElemOffset =
            SIMD_T::loadu_si(reinterpret_cast<const Integer<SIMD_T>*>(elemOffset));

        // step to the simdvertex
        Integer<SIMD_T> vOffsets =
            SIMD_T::mullo_epi32(vIndices, SIMD_T::set1_epi32(simdVertexStride));

        // step to the attribute and component
        vOffsets = SIMD_T::add_epi32(
            vOffsets, SIMD_T::set1_epi32(attribStride * attrib + componentStride * component));

        // step to the lane
        vOffsets = SIMD_T::add_epi32(vOffsets, vElemOffset);

        return vOffsets;
    }

    Float<SIMD_T> GatherComponent(const float*           pBuffer,
                                  uint32_t               attrib,
                                  Float<SIMD_T> const&   vMask,
                                  Integer<SIMD_T> const& vIndices,
                                  uint32_t               component)
    {
        Integer<SIMD_T> vOffsets = ComputeOffsets(attrib, vIndices, component);
        Float<SIMD_T>   vSrc     = SIMD_T::setzero_ps();

        return SIMD_T::mask_i32gather_ps(vSrc, pBuffer, vOffsets, vMask);
    }

    void ScatterComponent(const float*           pBuffer,
                          uint32_t               attrib,
                          Float<SIMD_T> const&   vMask,
                          Integer<SIMD_T> const& vIndices,
                          uint32_t               component,
                          Float<SIMD_T> const&   vSrc)
    {
        Integer<SIMD_T> vOffsets = ComputeOffsets(attrib, vIndices, component);

        const uint32_t* pOffsets = reinterpret_cast<const uint32_t*>(&vOffsets);
        const float*    pSrc     = reinterpret_cast<const float*>(&vSrc);
        uint32_t        mask     = SIMD_T::movemask_ps(vMask);
        unsigned long  lane;
        while (_BitScanForward(&lane, mask))
        {
            mask &= ~(1 << lane);
            const uint8_t* pBuf = reinterpret_cast<const uint8_t*>(pBuffer) + pOffsets[lane];
            *(float*)pBuf       = pSrc[lane];
        }
    }

    template <SWR_CLIPCODES ClippingPlane>
    void intersect(const Float<SIMD_T>&   vActiveMask,  // active lanes to operate on
                   const Integer<SIMD_T>& s,            // index to first edge vertex v0 in pInPts.
                   const Integer<SIMD_T>& p,            // index to second edge vertex v1 in pInPts.
                   const Vec4<SIMD_T>&    v1,           // vertex 0 position
                   const Vec4<SIMD_T>&    v2,           // vertex 1 position
                   Integer<SIMD_T>&       outIndex,     // output index.
                   const float*           pInVerts,     // array of all the input positions.
                   uint32_t               numInAttribs, // number of attributes per vertex.
                   float* pOutVerts) // array of output positions. We'll write our new intersection
                                     // point at i*4.
    {
        uint32_t vertexAttribOffset   = this->state.backendState.vertexAttribOffset;
        uint32_t vertexClipCullOffset = this->state.backendState.vertexClipCullOffset;

        // compute interpolation factor
        Float<SIMD_T> t;
        switch (ClippingPlane)
        {
        case FRUSTUM_LEFT:
            t = ComputeInterpFactor(SIMD_T::add_ps(v1[3], v1[0]), SIMD_T::add_ps(v2[3], v2[0]));
            break;
        case FRUSTUM_RIGHT:
            t = ComputeInterpFactor(SIMD_T::sub_ps(v1[3], v1[0]), SIMD_T::sub_ps(v2[3], v2[0]));
            break;
        case FRUSTUM_TOP:
            t = ComputeInterpFactor(SIMD_T::add_ps(v1[3], v1[1]), SIMD_T::add_ps(v2[3], v2[1]));
            break;
        case FRUSTUM_BOTTOM:
            t = ComputeInterpFactor(SIMD_T::sub_ps(v1[3], v1[1]), SIMD_T::sub_ps(v2[3], v2[1]));
            break;
        case FRUSTUM_NEAR:
            // DX Znear plane is 0, GL is -w
            if (this->state.rastState.clipHalfZ)
            {
                t = ComputeInterpFactor(v1[2], v2[2]);
            }
            else
            {
                t = ComputeInterpFactor(SIMD_T::add_ps(v1[3], v1[2]), SIMD_T::add_ps(v2[3], v2[2]));
            }
            break;
        case FRUSTUM_FAR:
            t = ComputeInterpFactor(SIMD_T::sub_ps(v1[3], v1[2]), SIMD_T::sub_ps(v2[3], v2[2]));
            break;
        default:
            SWR_INVALID("invalid clipping plane: %d", ClippingPlane);
        };

        // interpolate position and store
        for (uint32_t c = 0; c < 4; ++c)
        {
            Float<SIMD_T> vOutPos = SIMD_T::fmadd_ps(SIMD_T::sub_ps(v2[c], v1[c]), t, v1[c]);
            ScatterComponent(pOutVerts, VERTEX_POSITION_SLOT, vActiveMask, outIndex, c, vOutPos);
        }

        // interpolate attributes and store
        for (uint32_t a = 0; a < numInAttribs; ++a)
        {
            uint32_t attribSlot = vertexAttribOffset + a;
            for (uint32_t c = 0; c < 4; ++c)
            {
                Float<SIMD_T> vAttrib0 = GatherComponent(pInVerts, attribSlot, vActiveMask, s, c);
                Float<SIMD_T> vAttrib1 = GatherComponent(pInVerts, attribSlot, vActiveMask, p, c);
                Float<SIMD_T> vOutAttrib =
                    SIMD_T::fmadd_ps(SIMD_T::sub_ps(vAttrib1, vAttrib0), t, vAttrib0);
                ScatterComponent(pOutVerts, attribSlot, vActiveMask, outIndex, c, vOutAttrib);
            }
        }

        // interpolate clip distance if enabled
        if (this->state.backendState.clipDistanceMask & 0xf)
        {
            uint32_t attribSlot = vertexClipCullOffset;
            for (uint32_t c = 0; c < 4; ++c)
            {
                Float<SIMD_T> vAttrib0 = GatherComponent(pInVerts, attribSlot, vActiveMask, s, c);
                Float<SIMD_T> vAttrib1 = GatherComponent(pInVerts, attribSlot, vActiveMask, p, c);
                Float<SIMD_T> vOutAttrib =
                    SIMD_T::fmadd_ps(SIMD_T::sub_ps(vAttrib1, vAttrib0), t, vAttrib0);
                ScatterComponent(pOutVerts, attribSlot, vActiveMask, outIndex, c, vOutAttrib);
            }
        }

        if (this->state.backendState.clipDistanceMask & 0xf0)
        {
            uint32_t attribSlot = vertexClipCullOffset + 1;
            for (uint32_t c = 0; c < 4; ++c)
            {
                Float<SIMD_T> vAttrib0 = GatherComponent(pInVerts, attribSlot, vActiveMask, s, c);
                Float<SIMD_T> vAttrib1 = GatherComponent(pInVerts, attribSlot, vActiveMask, p, c);
                Float<SIMD_T> vOutAttrib =
                    SIMD_T::fmadd_ps(SIMD_T::sub_ps(vAttrib1, vAttrib0), t, vAttrib0);
                ScatterComponent(pOutVerts, attribSlot, vActiveMask, outIndex, c, vOutAttrib);
            }
        }
    }

    template <SWR_CLIPCODES ClippingPlane>
    Float<SIMD_T> inside(const Vec4<SIMD_T>& v)
    {
        switch (ClippingPlane)
        {
        case FRUSTUM_LEFT:
            return SIMD_T::cmpge_ps(v[0], SIMD_T::mul_ps(v[3], SIMD_T::set1_ps(-1.0f)));
        case FRUSTUM_RIGHT:
            return SIMD_T::cmple_ps(v[0], v[3]);
        case FRUSTUM_TOP:
            return SIMD_T::cmpge_ps(v[1], SIMD_T::mul_ps(v[3], SIMD_T::set1_ps(-1.0f)));
        case FRUSTUM_BOTTOM:
            return SIMD_T::cmple_ps(v[1], v[3]);
        case FRUSTUM_NEAR:
            return SIMD_T::cmpge_ps(v[2],
                                    this->state.rastState.clipHalfZ
                                        ? SIMD_T::setzero_ps()
                                        : SIMD_T::mul_ps(v[3], SIMD_T::set1_ps(-1.0f)));
        case FRUSTUM_FAR:
            return SIMD_T::cmple_ps(v[2], v[3]);
        default:
            SWR_INVALID("invalid clipping plane: %d", ClippingPlane);
            return SIMD_T::setzero_ps();
        }
    }

    template <SWR_CLIPCODES ClippingPlane>
    Integer<SIMD_T> ClipTriToPlane(const float*           pInVerts,
                                   const Integer<SIMD_T>& vNumInPts,
                                   uint32_t               numInAttribs,
                                   float*                 pOutVerts)
    {
        uint32_t vertexAttribOffset = this->state.backendState.vertexAttribOffset;

        Integer<SIMD_T> vCurIndex   = SIMD_T::setzero_si();
        Integer<SIMD_T> vOutIndex   = SIMD_T::setzero_si();
        Float<SIMD_T>   vActiveMask = SIMD_T::castsi_ps(SIMD_T::cmplt_epi32(vCurIndex, vNumInPts));

        while (!SIMD_T::testz_ps(vActiveMask, vActiveMask)) // loop until activeMask is empty
        {
            Integer<SIMD_T> s             = vCurIndex;
            Integer<SIMD_T> p             = SIMD_T::add_epi32(s, SIMD_T::set1_epi32(1));
            Integer<SIMD_T> underFlowMask = SIMD_T::cmpgt_epi32(vNumInPts, p);
            p                             = SIMD_T::castps_si(SIMD_T::blendv_ps(
                SIMD_T::setzero_ps(), SIMD_T::castsi_ps(p), SIMD_T::castsi_ps(underFlowMask)));

            // gather position
            Vec4<SIMD_T> vInPos0, vInPos1;
            for (uint32_t c = 0; c < 4; ++c)
            {
                vInPos0[c] = GatherComponent(pInVerts, VERTEX_POSITION_SLOT, vActiveMask, s, c);
                vInPos1[c] = GatherComponent(pInVerts, VERTEX_POSITION_SLOT, vActiveMask, p, c);
            }

            // compute inside mask
            Float<SIMD_T> s_in = inside<ClippingPlane>(vInPos0);
            Float<SIMD_T> p_in = inside<ClippingPlane>(vInPos1);

            // compute intersection mask (s_in != p_in)
            Float<SIMD_T> intersectMask = SIMD_T::xor_ps(s_in, p_in);
            intersectMask               = SIMD_T::and_ps(intersectMask, vActiveMask);

            // store s if inside
            s_in = SIMD_T::and_ps(s_in, vActiveMask);
            if (!SIMD_T::testz_ps(s_in, s_in))
            {
                // store position
                for (uint32_t c = 0; c < 4; ++c)
                {
                    ScatterComponent(
                        pOutVerts, VERTEX_POSITION_SLOT, s_in, vOutIndex, c, vInPos0[c]);
                }

                // store attribs
                for (uint32_t a = 0; a < numInAttribs; ++a)
                {
                    uint32_t attribSlot = vertexAttribOffset + a;
                    for (uint32_t c = 0; c < 4; ++c)
                    {
                        Float<SIMD_T> vAttrib = GatherComponent(pInVerts, attribSlot, s_in, s, c);
                        ScatterComponent(pOutVerts, attribSlot, s_in, vOutIndex, c, vAttrib);
                    }
                }

                // store clip distance if enabled
                uint32_t vertexClipCullSlot = this->state.backendState.vertexClipCullOffset;
                if (this->state.backendState.clipDistanceMask & 0xf)
                {
                    uint32_t attribSlot = vertexClipCullSlot;
                    for (uint32_t c = 0; c < 4; ++c)
                    {
                        Float<SIMD_T> vAttrib = GatherComponent(pInVerts, attribSlot, s_in, s, c);
                        ScatterComponent(pOutVerts, attribSlot, s_in, vOutIndex, c, vAttrib);
                    }
                }

                if (this->state.backendState.clipDistanceMask & 0xf0)
                {
                    uint32_t attribSlot = vertexClipCullSlot + 1;
                    for (uint32_t c = 0; c < 4; ++c)
                    {
                        Float<SIMD_T> vAttrib = GatherComponent(pInVerts, attribSlot, s_in, s, c);
                        ScatterComponent(pOutVerts, attribSlot, s_in, vOutIndex, c, vAttrib);
                    }
                }

                // increment outIndex
                vOutIndex = SIMD_T::blendv_epi32(
                    vOutIndex, SIMD_T::add_epi32(vOutIndex, SIMD_T::set1_epi32(1)), s_in);
            }

            // compute and store intersection
            if (!SIMD_T::testz_ps(intersectMask, intersectMask))
            {
                intersect<ClippingPlane>(intersectMask,
                                         s,
                                         p,
                                         vInPos0,
                                         vInPos1,
                                         vOutIndex,
                                         pInVerts,
                                         numInAttribs,
                                         pOutVerts);

                // increment outIndex for active lanes
                vOutIndex = SIMD_T::blendv_epi32(
                    vOutIndex, SIMD_T::add_epi32(vOutIndex, SIMD_T::set1_epi32(1)), intersectMask);
            }

            // increment loop index and update active mask
            vCurIndex   = SIMD_T::add_epi32(vCurIndex, SIMD_T::set1_epi32(1));
            vActiveMask = SIMD_T::castsi_ps(SIMD_T::cmplt_epi32(vCurIndex, vNumInPts));
        }

        return vOutIndex;
    }

    template <SWR_CLIPCODES ClippingPlane>
    Integer<SIMD_T> ClipLineToPlane(const float*           pInVerts,
                                    const Integer<SIMD_T>& vNumInPts,
                                    uint32_t               numInAttribs,
                                    float*                 pOutVerts)
    {
        uint32_t vertexAttribOffset = this->state.backendState.vertexAttribOffset;

        Integer<SIMD_T> vCurIndex   = SIMD_T::setzero_si();
        Integer<SIMD_T> vOutIndex   = SIMD_T::setzero_si();
        Float<SIMD_T>   vActiveMask = SIMD_T::castsi_ps(SIMD_T::cmplt_epi32(vCurIndex, vNumInPts));

        if (!SIMD_T::testz_ps(vActiveMask, vActiveMask))
        {
            Integer<SIMD_T> s = vCurIndex;
            Integer<SIMD_T> p = SIMD_T::add_epi32(s, SIMD_T::set1_epi32(1));

            // gather position
            Vec4<SIMD_T> vInPos0, vInPos1;
            for (uint32_t c = 0; c < 4; ++c)
            {
                vInPos0[c] = GatherComponent(pInVerts, VERTEX_POSITION_SLOT, vActiveMask, s, c);
                vInPos1[c] = GatherComponent(pInVerts, VERTEX_POSITION_SLOT, vActiveMask, p, c);
            }

            // compute inside mask
            Float<SIMD_T> s_in = inside<ClippingPlane>(vInPos0);
            Float<SIMD_T> p_in = inside<ClippingPlane>(vInPos1);

            // compute intersection mask (s_in != p_in)
            Float<SIMD_T> intersectMask = SIMD_T::xor_ps(s_in, p_in);
            intersectMask               = SIMD_T::and_ps(intersectMask, vActiveMask);

            // store s if inside
            s_in = SIMD_T::and_ps(s_in, vActiveMask);
            if (!SIMD_T::testz_ps(s_in, s_in))
            {
                for (uint32_t c = 0; c < 4; ++c)
                {
                    ScatterComponent(
                        pOutVerts, VERTEX_POSITION_SLOT, s_in, vOutIndex, c, vInPos0[c]);
                }

                // interpolate attributes and store
                for (uint32_t a = 0; a < numInAttribs; ++a)
                {
                    uint32_t attribSlot = vertexAttribOffset + a;
                    for (uint32_t c = 0; c < 4; ++c)
                    {
                        Float<SIMD_T> vAttrib = GatherComponent(pInVerts, attribSlot, s_in, s, c);
                        ScatterComponent(pOutVerts, attribSlot, s_in, vOutIndex, c, vAttrib);
                    }
                }

                // increment outIndex
                vOutIndex = SIMD_T::blendv_epi32(
                    vOutIndex, SIMD_T::add_epi32(vOutIndex, SIMD_T::set1_epi32(1)), s_in);
            }

            // compute and store intersection
            if (!SIMD_T::testz_ps(intersectMask, intersectMask))
            {
                intersect<ClippingPlane>(intersectMask,
                                         s,
                                         p,
                                         vInPos0,
                                         vInPos1,
                                         vOutIndex,
                                         pInVerts,
                                         numInAttribs,
                                         pOutVerts);

                // increment outIndex for active lanes
                vOutIndex = SIMD_T::blendv_epi32(
                    vOutIndex, SIMD_T::add_epi32(vOutIndex, SIMD_T::set1_epi32(1)), intersectMask);
            }

            // store p if inside
            p_in = SIMD_T::and_ps(p_in, vActiveMask);
            if (!SIMD_T::testz_ps(p_in, p_in))
            {
                for (uint32_t c = 0; c < 4; ++c)
                {
                    ScatterComponent(
                        pOutVerts, VERTEX_POSITION_SLOT, p_in, vOutIndex, c, vInPos1[c]);
                }

                // interpolate attributes and store
                for (uint32_t a = 0; a < numInAttribs; ++a)
                {
                    uint32_t attribSlot = vertexAttribOffset + a;
                    for (uint32_t c = 0; c < 4; ++c)
                    {
                        Float<SIMD_T> vAttrib = GatherComponent(pInVerts, attribSlot, p_in, p, c);
                        ScatterComponent(pOutVerts, attribSlot, p_in, vOutIndex, c, vAttrib);
                    }
                }

                // increment outIndex
                vOutIndex = SIMD_T::blendv_epi32(
                    vOutIndex, SIMD_T::add_epi32(vOutIndex, SIMD_T::set1_epi32(1)), p_in);
            }
        }

        return vOutIndex;
    }

    Integer<SIMD_T> ClipPrims(float*               pVertices,
                              const Float<SIMD_T>& vPrimMask,
                              const Float<SIMD_T>& vClipMask,
                              int                  numAttribs)
    {
        // temp storage
        float* pTempVerts = reinterpret_cast<float*>(this->tmpVerts);

        // zero out num input verts for non-active lanes
        Integer<SIMD_T> vNumInPts = SIMD_T::set1_epi32(NumVertsPerPrimT);
        vNumInPts = SIMD_T::blendv_epi32(SIMD_T::setzero_si(), vNumInPts, vClipMask);

        // clip prims to frustum
        Integer<SIMD_T> vNumOutPts;
        if (NumVertsPerPrimT == 3)
        {
            vNumOutPts = ClipTriToPlane<FRUSTUM_NEAR>(pVertices, vNumInPts, numAttribs, pTempVerts);
            vNumOutPts = ClipTriToPlane<FRUSTUM_FAR>(pTempVerts, vNumOutPts, numAttribs, pVertices);
            vNumOutPts =
                ClipTriToPlane<FRUSTUM_LEFT>(pVertices, vNumOutPts, numAttribs, pTempVerts);
            vNumOutPts =
                ClipTriToPlane<FRUSTUM_RIGHT>(pTempVerts, vNumOutPts, numAttribs, pVertices);
            vNumOutPts =
                ClipTriToPlane<FRUSTUM_BOTTOM>(pVertices, vNumOutPts, numAttribs, pTempVerts);
            vNumOutPts = ClipTriToPlane<FRUSTUM_TOP>(pTempVerts, vNumOutPts, numAttribs, pVertices);
        }
        else
        {
            SWR_ASSERT(NumVertsPerPrimT == 2);
            vNumOutPts =
                ClipLineToPlane<FRUSTUM_NEAR>(pVertices, vNumInPts, numAttribs, pTempVerts);
            vNumOutPts =
                ClipLineToPlane<FRUSTUM_FAR>(pTempVerts, vNumOutPts, numAttribs, pVertices);
            vNumOutPts =
                ClipLineToPlane<FRUSTUM_LEFT>(pVertices, vNumOutPts, numAttribs, pTempVerts);
            vNumOutPts =
                ClipLineToPlane<FRUSTUM_RIGHT>(pTempVerts, vNumOutPts, numAttribs, pVertices);
            vNumOutPts =
                ClipLineToPlane<FRUSTUM_BOTTOM>(pVertices, vNumOutPts, numAttribs, pTempVerts);
            vNumOutPts =
                ClipLineToPlane<FRUSTUM_TOP>(pTempVerts, vNumOutPts, numAttribs, pVertices);
        }

        // restore num verts for non-clipped, active lanes
        Float<SIMD_T> vNonClippedMask = SIMD_T::andnot_ps(vClipMask, vPrimMask);
        vNumOutPts =
            SIMD_T::blendv_epi32(vNumOutPts, SIMD_T::set1_epi32(NumVertsPerPrimT), vNonClippedMask);

        return vNumOutPts;
    }

    const uint32_t   workerId{0};
    DRAW_CONTEXT*    pDC{nullptr};
    const API_STATE& state;
    Float<SIMD_T>    clipCodes[NumVertsPerPrimT];
    SIMDVERTEX_T<SIMD_T>* clippedVerts;
    SIMDVERTEX_T<SIMD_T>* tmpVerts;
    SIMDVERTEX_T<SIMD_T>* transposedVerts;
};

// pipeline stage functions
void ClipRectangles(DRAW_CONTEXT*      pDC,
                    PA_STATE&          pa,
                    uint32_t           workerId,
                    simdvector         prims[],
                    uint32_t           primMask,
                    simdscalari const& primId,
                    simdscalari const& viewportIdx,
                    simdscalari const& rtIdx);
void ClipTriangles(DRAW_CONTEXT*      pDC,
                   PA_STATE&          pa,
                   uint32_t           workerId,
                   simdvector         prims[],
                   uint32_t           primMask,
                   simdscalari const& primId,
                   simdscalari const& viewportIdx,
                   simdscalari const& rtIdx);
void ClipLines(DRAW_CONTEXT*      pDC,
               PA_STATE&          pa,
               uint32_t           workerId,
               simdvector         prims[],
               uint32_t           primMask,
               simdscalari const& primId,
               simdscalari const& viewportIdx,
               simdscalari const& rtIdx);
void ClipPoints(DRAW_CONTEXT*      pDC,
                PA_STATE&          pa,
                uint32_t           workerId,
                simdvector         prims[],
                uint32_t           primMask,
                simdscalari const& primId,
                simdscalari const& viewportIdx,
                simdscalari const& rtIdx);
#if USE_SIMD16_FRONTEND
void SIMDCALL ClipRectangles_simd16(DRAW_CONTEXT*        pDC,
                                    PA_STATE&            pa,
                                    uint32_t             workerId,
                                    simd16vector         prims[],
                                    uint32_t             primMask,
                                    simd16scalari const& primId,
                                    simd16scalari const& viewportIdx,
                                    simd16scalari const& rtIdx);
void SIMDCALL ClipTriangles_simd16(DRAW_CONTEXT*        pDC,
                                   PA_STATE&            pa,
                                   uint32_t             workerId,
                                   simd16vector         prims[],
                                   uint32_t             primMask,
                                   simd16scalari const& primId,
                                   simd16scalari const& viewportIdx,
                                   simd16scalari const& rtIdx);
void SIMDCALL ClipLines_simd16(DRAW_CONTEXT*        pDC,
                               PA_STATE&            pa,
                               uint32_t             workerId,
                               simd16vector         prims[],
                               uint32_t             primMask,
                               simd16scalari const& primId,
                               simd16scalari const& viewportIdx,
                               simd16scalari const& rtIdx);
void SIMDCALL ClipPoints_simd16(DRAW_CONTEXT*        pDC,
                                PA_STATE&            pa,
                                uint32_t             workerId,
                                simd16vector         prims[],
                                uint32_t             primMask,
                                simd16scalari const& primId,
                                simd16scalari const& viewportIdx,
                                simd16scalari const& rtIdx);
#endif
