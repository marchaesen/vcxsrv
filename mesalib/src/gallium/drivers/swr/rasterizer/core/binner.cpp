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
 * @file binner.cpp
 *
 * @brief Implementation for the macrotile binner
 *
 ******************************************************************************/

#include "binner.h"
#include "context.h"
#include "frontend.h"
#include "conservativeRast.h"
#include "pa.h"
#include "rasterizer.h"
#include "rdtsc_core.h"
#include "tilemgr.h"

// Function Prototype
template <typename SIMD_T, uint32_t SIMD_WIDTH>
void BinPostSetupLinesImpl(DRAW_CONTEXT*          pDC,
                           PA_STATE&              pa,
                           uint32_t               workerId,
                           Vec4<SIMD_T>           prim[],
                           Float<SIMD_T>          recipW[],
                           uint32_t               primMask,
                           Integer<SIMD_T> const& primID,
                           Integer<SIMD_T> const& viewportIdx,
                           Integer<SIMD_T> const& rtIdx);

template <typename SIMD_T, uint32_t SIMD_WIDTH>
void BinPostSetupPointsImpl(DRAW_CONTEXT*          pDC,
                            PA_STATE&              pa,
                            uint32_t               workerId,
                            Vec4<SIMD_T>           prim[],
                            uint32_t               primMask,
                            Integer<SIMD_T> const& primID,
                            Integer<SIMD_T> const& viewportIdx,
                            Integer<SIMD_T> const& rtIdx);

//////////////////////////////////////////////////////////////////////////
/// @brief Processes attributes for the backend based on linkage mask and
///        linkage map.  Essentially just doing an SOA->AOS conversion and pack.
/// @param pDC - Draw context
/// @param pa - Primitive Assembly state
/// @param linkageMask - Specifies which VS outputs are routed to PS.
/// @param pLinkageMap - maps VS attribute slot to PS slot
/// @param triIndex - Triangle to process attributes for
/// @param pBuffer - Output result
template <typename NumVertsT,
          typename IsSwizzledT,
          typename HasConstantInterpT,
          typename IsDegenerate>
INLINE void ProcessAttributes(
    DRAW_CONTEXT* pDC, PA_STATE& pa, uint32_t triIndex, uint32_t primId, float* pBuffer)
{
    static_assert(NumVertsT::value > 0 && NumVertsT::value <= 3, "Invalid value for NumVertsT");
    const SWR_BACKEND_STATE& backendState = pDC->pState->state.backendState;
    // Conservative Rasterization requires degenerate tris to have constant attribute interpolation
    uint32_t constantInterpMask =
        IsDegenerate::value ? 0xFFFFFFFF : backendState.constantInterpolationMask;
    const uint32_t provokingVertex = pDC->pState->state.frontendState.topologyProvokingVertex;
    const PRIMITIVE_TOPOLOGY topo  = pa.binTopology;

    static const float constTable[3][4] = {
        {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}};

    for (uint32_t i = 0; i < backendState.numAttributes; ++i)
    {
        uint32_t inputSlot;
        if (IsSwizzledT::value)
        {
            SWR_ATTRIB_SWIZZLE attribSwizzle = backendState.swizzleMap[i];
            inputSlot = backendState.vertexAttribOffset + attribSwizzle.sourceAttrib;
        }
        else
        {
            inputSlot = backendState.vertexAttribOffset + i;
        }

        simd4scalar attrib[3]; // triangle attribs (always 4 wide)
        float*      pAttribStart = pBuffer;

        if (HasConstantInterpT::value || IsDegenerate::value)
        {
            if (CheckBit(constantInterpMask, i))
            {
                uint32_t              vid;
                uint32_t              adjustedTriIndex;
                static const uint32_t tristripProvokingVertex[]   = {0, 2, 1};
                static const int32_t  quadProvokingTri[2][4]      = {{0, 0, 0, 1}, {0, -1, 0, 0}};
                static const uint32_t quadProvokingVertex[2][4]   = {{0, 1, 2, 2}, {0, 1, 1, 2}};
                static const int32_t  qstripProvokingTri[2][4]    = {{0, 0, 0, 1}, {-1, 0, 0, 0}};
                static const uint32_t qstripProvokingVertex[2][4] = {{0, 1, 2, 1}, {0, 0, 2, 1}};

                switch (topo)
                {
                case TOP_QUAD_LIST:
                    adjustedTriIndex = triIndex + quadProvokingTri[triIndex & 1][provokingVertex];
                    vid              = quadProvokingVertex[triIndex & 1][provokingVertex];
                    break;
                case TOP_QUAD_STRIP:
                    adjustedTriIndex = triIndex + qstripProvokingTri[triIndex & 1][provokingVertex];
                    vid              = qstripProvokingVertex[triIndex & 1][provokingVertex];
                    break;
                case TOP_TRIANGLE_STRIP:
                    adjustedTriIndex = triIndex;
                    vid =
                        (triIndex & 1) ? tristripProvokingVertex[provokingVertex] : provokingVertex;
                    break;
                default:
                    adjustedTriIndex = triIndex;
                    vid              = provokingVertex;
                    break;
                }

                pa.AssembleSingle(inputSlot, adjustedTriIndex, attrib);

                for (uint32_t i = 0; i < NumVertsT::value; ++i)
                {
                    SIMD128::store_ps(pBuffer, attrib[vid]);
                    pBuffer += 4;
                }
            }
            else
            {
                pa.AssembleSingle(inputSlot, triIndex, attrib);

                for (uint32_t i = 0; i < NumVertsT::value; ++i)
                {
                    SIMD128::store_ps(pBuffer, attrib[i]);
                    pBuffer += 4;
                }
            }
        }
        else
        {
            pa.AssembleSingle(inputSlot, triIndex, attrib);

            for (uint32_t i = 0; i < NumVertsT::value; ++i)
            {
                SIMD128::store_ps(pBuffer, attrib[i]);
                pBuffer += 4;
            }
        }

        // pad out the attrib buffer to 3 verts to ensure the triangle
        // interpolation code in the pixel shader works correctly for the
        // 3 topologies - point, line, tri.  This effectively zeros out the
        // effect of the missing vertices in the triangle interpolation.
        for (uint32_t v = NumVertsT::value; v < 3; ++v)
        {
            SIMD128::store_ps(pBuffer, attrib[NumVertsT::value - 1]);
            pBuffer += 4;
        }

        // check for constant source overrides
        if (IsSwizzledT::value)
        {
            uint32_t mask = backendState.swizzleMap[i].componentOverrideMask;
            if (mask)
            {
                unsigned long comp;
                while (_BitScanForward(&comp, mask))
                {
                    mask &= ~(1 << comp);

                    float constantValue = 0.0f;
                    switch ((SWR_CONSTANT_SOURCE)backendState.swizzleMap[i].constantSource)
                    {
                    case SWR_CONSTANT_SOURCE_CONST_0000:
                    case SWR_CONSTANT_SOURCE_CONST_0001_FLOAT:
                    case SWR_CONSTANT_SOURCE_CONST_1111_FLOAT:
                        constantValue = constTable[backendState.swizzleMap[i].constantSource][comp];
                        break;
                    case SWR_CONSTANT_SOURCE_PRIM_ID:
                        constantValue = *(float*)&primId;
                        break;
                    }

                    // apply constant value to all 3 vertices
                    for (uint32_t v = 0; v < 3; ++v)
                    {
                        pAttribStart[comp + v * 4] = constantValue;
                    }
                }
            }
        }
    }
}

typedef void (*PFN_PROCESS_ATTRIBUTES)(DRAW_CONTEXT*, PA_STATE&, uint32_t, uint32_t, float*);

struct ProcessAttributesChooser
{
    typedef PFN_PROCESS_ATTRIBUTES FuncType;

    template <typename... ArgsB>
    static FuncType GetFunc()
    {
        return ProcessAttributes<ArgsB...>;
    }
};

PFN_PROCESS_ATTRIBUTES GetProcessAttributesFunc(uint32_t NumVerts,
                                                bool     IsSwizzled,
                                                bool     HasConstantInterp,
                                                bool     IsDegenerate = false)
{
    return TemplateArgUnroller<ProcessAttributesChooser>::GetFunc(
        IntArg<1, 3>{NumVerts}, IsSwizzled, HasConstantInterp, IsDegenerate);
}

//////////////////////////////////////////////////////////////////////////
/// @brief Processes enabled user clip distances. Loads the active clip
///        distances from the PA, sets up barycentric equations, and
///        stores the results to the output buffer
/// @param pa - Primitive Assembly state
/// @param primIndex - primitive index to process
/// @param clipDistMask - mask of enabled clip distances
/// @param pUserClipBuffer - buffer to store results
template <uint32_t NumVerts>
void ProcessUserClipDist(const SWR_BACKEND_STATE& state,
                         PA_STATE&                pa,
                         uint32_t                 primIndex,
                         float*                   pRecipW,
                         float*                   pUserClipBuffer)
{
    unsigned long clipDist;
    uint32_t clipDistMask = state.clipDistanceMask;
    while (_BitScanForward(&clipDist, clipDistMask))
    {
        clipDistMask &= ~(1 << clipDist);
        uint32_t clipSlot = clipDist >> 2;
        uint32_t clipComp = clipDist & 0x3;
        uint32_t clipAttribSlot =
            clipSlot == 0 ? state.vertexClipCullOffset : state.vertexClipCullOffset + 1;

        simd4scalar primClipDist[3];
        pa.AssembleSingle(clipAttribSlot, primIndex, primClipDist);

        float vertClipDist[NumVerts];
        for (uint32_t e = 0; e < NumVerts; ++e)
        {
            OSALIGNSIMD(float) aVertClipDist[4];
            SIMD128::store_ps(aVertClipDist, primClipDist[e]);
            vertClipDist[e] = aVertClipDist[clipComp];
        };

        // setup plane equations for barycentric interpolation in the backend
        float baryCoeff[NumVerts];
        float last = vertClipDist[NumVerts - 1] * pRecipW[NumVerts - 1];
        for (uint32_t e = 0; e < NumVerts - 1; ++e)
        {
            baryCoeff[e] = vertClipDist[e] * pRecipW[e] - last;
        }
        baryCoeff[NumVerts - 1] = last;

        for (uint32_t e = 0; e < NumVerts; ++e)
        {
            *(pUserClipBuffer++) = baryCoeff[e];
        }
    }
}

INLINE
void TransposeVertices(simd4scalar (&dst)[8],
                       const simdscalar& src0,
                       const simdscalar& src1,
                       const simdscalar& src2)
{
    vTranspose3x8(dst, src0, src1, src2);
}

INLINE
void TransposeVertices(simd4scalar (&dst)[16],
                       const simd16scalar& src0,
                       const simd16scalar& src1,
                       const simd16scalar& src2)
{
    vTranspose4x16(
        reinterpret_cast<simd16scalar(&)[4]>(dst), src0, src1, src2, _simd16_setzero_ps());
}

#if KNOB_ENABLE_EARLY_RAST

#define ER_SIMD_TILE_X_DIM (1 << ER_SIMD_TILE_X_SHIFT)
#define ER_SIMD_TILE_Y_DIM (1 << ER_SIMD_TILE_Y_SHIFT)

template <typename SIMD_T>
struct EarlyRastHelper
{
};

template <>
struct EarlyRastHelper<SIMD256>
{
    static SIMD256::Integer InitShiftCntrl()
    {
        return SIMD256::set_epi32(24, 25, 26, 27, 28, 29, 30, 31);
    }
};

#if USE_SIMD16_FRONTEND
template <>
struct EarlyRastHelper<SIMD512>
{
    static SIMD512::Integer InitShiftCntrl()
    {
        return SIMD512::set_epi32(16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31);
    }
};

#endif
//////////////////////////////////////////////////////////////////////////
/// @brief Early Rasterizer (ER); triangles that fit small (e.g. 4x4) tile
///        (ER tile) can be rasterized as early as in binner to check if
///        they cover any  pixels. If not - the triangles can be
///        culled in binner.
///
/// @param er_bbox - coordinates of ER tile for each triangle
/// @param vAi - A coefficients of triangle edges
/// @param vBi - B coefficients of triangle edges
/// @param vXi - X coordinates of triangle vertices
/// @param vYi - Y coordinates of triangle vertices
/// @param frontWindingTris - mask indicating CCW/CW triangles
/// @param triMask - mask for valid SIMD lanes (triangles)
/// @param oneTileMask - defines triangles for ER to work on
///                      (tris that fit into ER tile)
template <typename SIMD_T, uint32_t SIMD_WIDTH, typename CT>
uint32_t SIMDCALL EarlyRasterizer(DRAW_CONTEXT*       pDC,
                                  SIMDBBOX_T<SIMD_T>& er_bbox,
                                  Integer<SIMD_T> (&vAi)[3],
                                  Integer<SIMD_T> (&vBi)[3],
                                  Integer<SIMD_T> (&vXi)[3],
                                  Integer<SIMD_T> (&vYi)[3],
                                  uint32_t cwTrisMask,
                                  uint32_t triMask,
                                  uint32_t oneTileMask)
{
    // step to pixel center of top-left pixel of the triangle bbox
    Integer<SIMD_T> vTopLeftX =
        SIMD_T::template slli_epi32<ER_SIMD_TILE_X_SHIFT + FIXED_POINT_SHIFT>(er_bbox.xmin);
    vTopLeftX = SIMD_T::add_epi32(vTopLeftX, SIMD_T::set1_epi32(FIXED_POINT_SCALE / 2));

    Integer<SIMD_T> vTopLeftY =
        SIMD_T::template slli_epi32<ER_SIMD_TILE_Y_SHIFT + FIXED_POINT_SHIFT>(er_bbox.ymin);
    vTopLeftY = SIMD_T::add_epi32(vTopLeftY, SIMD_T::set1_epi32(FIXED_POINT_SCALE / 2));

    // negate A and B for CW tris
    Integer<SIMD_T> vNegA0 = SIMD_T::mullo_epi32(vAi[0], SIMD_T::set1_epi32(-1));
    Integer<SIMD_T> vNegA1 = SIMD_T::mullo_epi32(vAi[1], SIMD_T::set1_epi32(-1));
    Integer<SIMD_T> vNegA2 = SIMD_T::mullo_epi32(vAi[2], SIMD_T::set1_epi32(-1));
    Integer<SIMD_T> vNegB0 = SIMD_T::mullo_epi32(vBi[0], SIMD_T::set1_epi32(-1));
    Integer<SIMD_T> vNegB1 = SIMD_T::mullo_epi32(vBi[1], SIMD_T::set1_epi32(-1));
    Integer<SIMD_T> vNegB2 = SIMD_T::mullo_epi32(vBi[2], SIMD_T::set1_epi32(-1));

    RDTSC_EVENT(pDC->pContext->pBucketMgr,
                FEEarlyRastEnter,
                _mm_popcnt_u32(oneTileMask & triMask),
                0);

    Integer<SIMD_T> vShiftCntrl = EarlyRastHelper<SIMD_T>::InitShiftCntrl();
    Integer<SIMD_T> vCwTris     = SIMD_T::set1_epi32(cwTrisMask);
    Integer<SIMD_T> vMask       = SIMD_T::sllv_epi32(vCwTris, vShiftCntrl);

    vAi[0] = SIMD_T::castps_si(SIMD_T::blendv_ps(
        SIMD_T::castsi_ps(vAi[0]), SIMD_T::castsi_ps(vNegA0), SIMD_T::castsi_ps(vMask)));
    vAi[1] = SIMD_T::castps_si(SIMD_T::blendv_ps(
        SIMD_T::castsi_ps(vAi[1]), SIMD_T::castsi_ps(vNegA1), SIMD_T::castsi_ps(vMask)));
    vAi[2] = SIMD_T::castps_si(SIMD_T::blendv_ps(
        SIMD_T::castsi_ps(vAi[2]), SIMD_T::castsi_ps(vNegA2), SIMD_T::castsi_ps(vMask)));
    vBi[0] = SIMD_T::castps_si(SIMD_T::blendv_ps(
        SIMD_T::castsi_ps(vBi[0]), SIMD_T::castsi_ps(vNegB0), SIMD_T::castsi_ps(vMask)));
    vBi[1] = SIMD_T::castps_si(SIMD_T::blendv_ps(
        SIMD_T::castsi_ps(vBi[1]), SIMD_T::castsi_ps(vNegB1), SIMD_T::castsi_ps(vMask)));
    vBi[2] = SIMD_T::castps_si(SIMD_T::blendv_ps(
        SIMD_T::castsi_ps(vBi[2]), SIMD_T::castsi_ps(vNegB2), SIMD_T::castsi_ps(vMask)));

    // evaluate edge equations at top-left pixel
    Integer<SIMD_T> vDeltaX0 = SIMD_T::sub_epi32(vTopLeftX, vXi[0]);
    Integer<SIMD_T> vDeltaX1 = SIMD_T::sub_epi32(vTopLeftX, vXi[1]);
    Integer<SIMD_T> vDeltaX2 = SIMD_T::sub_epi32(vTopLeftX, vXi[2]);

    Integer<SIMD_T> vDeltaY0 = SIMD_T::sub_epi32(vTopLeftY, vYi[0]);
    Integer<SIMD_T> vDeltaY1 = SIMD_T::sub_epi32(vTopLeftY, vYi[1]);
    Integer<SIMD_T> vDeltaY2 = SIMD_T::sub_epi32(vTopLeftY, vYi[2]);

    Integer<SIMD_T> vAX0 = SIMD_T::mullo_epi32(vAi[0], vDeltaX0);
    Integer<SIMD_T> vAX1 = SIMD_T::mullo_epi32(vAi[1], vDeltaX1);
    Integer<SIMD_T> vAX2 = SIMD_T::mullo_epi32(vAi[2], vDeltaX2);

    Integer<SIMD_T> vBY0 = SIMD_T::mullo_epi32(vBi[0], vDeltaY0);
    Integer<SIMD_T> vBY1 = SIMD_T::mullo_epi32(vBi[1], vDeltaY1);
    Integer<SIMD_T> vBY2 = SIMD_T::mullo_epi32(vBi[2], vDeltaY2);

    Integer<SIMD_T> vEdge0 = SIMD_T::add_epi32(vAX0, vBY0);
    Integer<SIMD_T> vEdge1 = SIMD_T::add_epi32(vAX1, vBY1);
    Integer<SIMD_T> vEdge2 = SIMD_T::add_epi32(vAX2, vBY2);

    vEdge0 = SIMD_T::template srai_epi32<FIXED_POINT_SHIFT>(vEdge0);
    vEdge1 = SIMD_T::template srai_epi32<FIXED_POINT_SHIFT>(vEdge1);
    vEdge2 = SIMD_T::template srai_epi32<FIXED_POINT_SHIFT>(vEdge2);

    // top left rule
    Integer<SIMD_T> vEdgeAdjust0 = SIMD_T::sub_epi32(vEdge0, SIMD_T::set1_epi32(1));
    Integer<SIMD_T> vEdgeAdjust1 = SIMD_T::sub_epi32(vEdge1, SIMD_T::set1_epi32(1));
    Integer<SIMD_T> vEdgeAdjust2 = SIMD_T::sub_epi32(vEdge2, SIMD_T::set1_epi32(1));

    // vA < 0
    vEdge0 = SIMD_T::castps_si(SIMD_T::blendv_ps(
        SIMD_T::castsi_ps(vEdge0), SIMD_T::castsi_ps(vEdgeAdjust0), SIMD_T::castsi_ps(vAi[0])));
    vEdge1 = SIMD_T::castps_si(SIMD_T::blendv_ps(
        SIMD_T::castsi_ps(vEdge1), SIMD_T::castsi_ps(vEdgeAdjust1), SIMD_T::castsi_ps(vAi[1])));
    vEdge2 = SIMD_T::castps_si(SIMD_T::blendv_ps(
        SIMD_T::castsi_ps(vEdge2), SIMD_T::castsi_ps(vEdgeAdjust2), SIMD_T::castsi_ps(vAi[2])));

    // vA == 0 && vB < 0
    Integer<SIMD_T> vCmp0 = SIMD_T::cmpeq_epi32(vAi[0], SIMD_T::setzero_si());
    Integer<SIMD_T> vCmp1 = SIMD_T::cmpeq_epi32(vAi[1], SIMD_T::setzero_si());
    Integer<SIMD_T> vCmp2 = SIMD_T::cmpeq_epi32(vAi[2], SIMD_T::setzero_si());

    vCmp0 = SIMD_T::and_si(vCmp0, vBi[0]);
    vCmp1 = SIMD_T::and_si(vCmp1, vBi[1]);
    vCmp2 = SIMD_T::and_si(vCmp2, vBi[2]);

    vEdge0 = SIMD_T::castps_si(SIMD_T::blendv_ps(
        SIMD_T::castsi_ps(vEdge0), SIMD_T::castsi_ps(vEdgeAdjust0), SIMD_T::castsi_ps(vCmp0)));
    vEdge1 = SIMD_T::castps_si(SIMD_T::blendv_ps(
        SIMD_T::castsi_ps(vEdge1), SIMD_T::castsi_ps(vEdgeAdjust1), SIMD_T::castsi_ps(vCmp1)));
    vEdge2 = SIMD_T::castps_si(SIMD_T::blendv_ps(
        SIMD_T::castsi_ps(vEdge2), SIMD_T::castsi_ps(vEdgeAdjust2), SIMD_T::castsi_ps(vCmp2)));

#if ER_SIMD_TILE_X_DIM == 4 && ER_SIMD_TILE_Y_DIM == 4
    // Go down
    // coverage pixel 0
    Integer<SIMD_T> vMask0 = SIMD_T::and_si(vEdge0, vEdge1);
    vMask0                 = SIMD_T::and_si(vMask0, vEdge2);

    // coverage pixel 1
    Integer<SIMD_T> vEdge0N = SIMD_T::add_epi32(vEdge0, vBi[0]);
    Integer<SIMD_T> vEdge1N = SIMD_T::add_epi32(vEdge1, vBi[1]);
    Integer<SIMD_T> vEdge2N = SIMD_T::add_epi32(vEdge2, vBi[2]);
    Integer<SIMD_T> vMask1  = SIMD_T::and_si(vEdge0N, vEdge1N);
    vMask1                  = SIMD_T::and_si(vMask1, vEdge2N);

    // coverage pixel 2
    vEdge0N                = SIMD_T::add_epi32(vEdge0N, vBi[0]);
    vEdge1N                = SIMD_T::add_epi32(vEdge1N, vBi[1]);
    vEdge2N                = SIMD_T::add_epi32(vEdge2N, vBi[2]);
    Integer<SIMD_T> vMask2 = SIMD_T::and_si(vEdge0N, vEdge1N);
    vMask2                 = SIMD_T::and_si(vMask2, vEdge2N);

    // coverage pixel 3
    vEdge0N                = SIMD_T::add_epi32(vEdge0N, vBi[0]);
    vEdge1N                = SIMD_T::add_epi32(vEdge1N, vBi[1]);
    vEdge2N                = SIMD_T::add_epi32(vEdge2N, vBi[2]);
    Integer<SIMD_T> vMask3 = SIMD_T::and_si(vEdge0N, vEdge1N);
    vMask3                 = SIMD_T::and_si(vMask3, vEdge2N);

    // One step to the right and then up

    // coverage pixel 4
    vEdge0N                = SIMD_T::add_epi32(vEdge0N, vAi[0]);
    vEdge1N                = SIMD_T::add_epi32(vEdge1N, vAi[1]);
    vEdge2N                = SIMD_T::add_epi32(vEdge2N, vAi[2]);
    Integer<SIMD_T> vMask4 = SIMD_T::and_si(vEdge0N, vEdge1N);
    vMask4                 = SIMD_T::and_si(vMask4, vEdge2N);

    // coverage pixel 5
    vEdge0N                = SIMD_T::sub_epi32(vEdge0N, vBi[0]);
    vEdge1N                = SIMD_T::sub_epi32(vEdge1N, vBi[1]);
    vEdge2N                = SIMD_T::sub_epi32(vEdge2N, vBi[2]);
    Integer<SIMD_T> vMask5 = SIMD_T::and_si(vEdge0N, vEdge1N);
    vMask5                 = SIMD_T::and_si(vMask5, vEdge2N);

    // coverage pixel 6
    vEdge0N                = SIMD_T::sub_epi32(vEdge0N, vBi[0]);
    vEdge1N                = SIMD_T::sub_epi32(vEdge1N, vBi[1]);
    vEdge2N                = SIMD_T::sub_epi32(vEdge2N, vBi[2]);
    Integer<SIMD_T> vMask6 = SIMD_T::and_si(vEdge0N, vEdge1N);
    vMask6                 = SIMD_T::and_si(vMask6, vEdge2N);

    // coverage pixel 7
    vEdge0N                = SIMD_T::sub_epi32(vEdge0N, vBi[0]);
    vEdge1N                = SIMD_T::sub_epi32(vEdge1N, vBi[1]);
    vEdge2N                = SIMD_T::sub_epi32(vEdge2N, vBi[2]);
    Integer<SIMD_T> vMask7 = SIMD_T::and_si(vEdge0N, vEdge1N);
    vMask7                 = SIMD_T::and_si(vMask7, vEdge2N);

    Integer<SIMD_T> vLit1 = SIMD_T::or_si(vMask0, vMask1);
    vLit1                 = SIMD_T::or_si(vLit1, vMask2);
    vLit1                 = SIMD_T::or_si(vLit1, vMask3);
    vLit1                 = SIMD_T::or_si(vLit1, vMask4);
    vLit1                 = SIMD_T::or_si(vLit1, vMask5);
    vLit1                 = SIMD_T::or_si(vLit1, vMask6);
    vLit1                 = SIMD_T::or_si(vLit1, vMask7);

    // Step to the right and go down again

    // coverage pixel 0
    vEdge0N = SIMD_T::add_epi32(vEdge0N, vAi[0]);
    vEdge1N = SIMD_T::add_epi32(vEdge1N, vAi[1]);
    vEdge2N = SIMD_T::add_epi32(vEdge2N, vAi[2]);
    vMask0  = SIMD_T::and_si(vEdge0N, vEdge1N);
    vMask0  = SIMD_T::and_si(vMask0, vEdge2N);

    // coverage pixel 1
    vEdge0N = SIMD_T::add_epi32(vEdge0N, vBi[0]);
    vEdge1N = SIMD_T::add_epi32(vEdge1N, vBi[1]);
    vEdge2N = SIMD_T::add_epi32(vEdge2N, vBi[2]);
    vMask1  = SIMD_T::and_si(vEdge0N, vEdge1N);
    vMask1  = SIMD_T::and_si(vMask1, vEdge2N);

    // coverage pixel 2
    vEdge0N = SIMD_T::add_epi32(vEdge0N, vBi[0]);
    vEdge1N = SIMD_T::add_epi32(vEdge1N, vBi[1]);
    vEdge2N = SIMD_T::add_epi32(vEdge2N, vBi[2]);
    vMask2  = SIMD_T::and_si(vEdge0N, vEdge1N);
    vMask2  = SIMD_T::and_si(vMask2, vEdge2N);

    // coverage pixel 3
    vEdge0N = SIMD_T::add_epi32(vEdge0N, vBi[0]);
    vEdge1N = SIMD_T::add_epi32(vEdge1N, vBi[1]);
    vEdge2N = SIMD_T::add_epi32(vEdge2N, vBi[2]);
    vMask3  = SIMD_T::and_si(vEdge0N, vEdge1N);
    vMask3  = SIMD_T::and_si(vMask3, vEdge2N);

    // And for the last time - to the right and up

    // coverage pixel 4
    vEdge0N = SIMD_T::add_epi32(vEdge0N, vAi[0]);
    vEdge1N = SIMD_T::add_epi32(vEdge1N, vAi[1]);
    vEdge2N = SIMD_T::add_epi32(vEdge2N, vAi[2]);
    vMask4  = SIMD_T::and_si(vEdge0N, vEdge1N);
    vMask4  = SIMD_T::and_si(vMask4, vEdge2N);

    // coverage pixel 5
    vEdge0N = SIMD_T::sub_epi32(vEdge0N, vBi[0]);
    vEdge1N = SIMD_T::sub_epi32(vEdge1N, vBi[1]);
    vEdge2N = SIMD_T::sub_epi32(vEdge2N, vBi[2]);
    vMask5  = SIMD_T::and_si(vEdge0N, vEdge1N);
    vMask5  = SIMD_T::and_si(vMask5, vEdge2N);

    // coverage pixel 6
    vEdge0N = SIMD_T::sub_epi32(vEdge0N, vBi[0]);
    vEdge1N = SIMD_T::sub_epi32(vEdge1N, vBi[1]);
    vEdge2N = SIMD_T::sub_epi32(vEdge2N, vBi[2]);
    vMask6  = SIMD_T::and_si(vEdge0N, vEdge1N);
    vMask6  = SIMD_T::and_si(vMask6, vEdge2N);

    // coverage pixel 7
    vEdge0N = SIMD_T::sub_epi32(vEdge0N, vBi[0]);
    vEdge1N = SIMD_T::sub_epi32(vEdge1N, vBi[1]);
    vEdge2N = SIMD_T::sub_epi32(vEdge2N, vBi[2]);
    vMask7  = SIMD_T::and_si(vEdge0N, vEdge1N);
    vMask7  = SIMD_T::and_si(vMask7, vEdge2N);

    Integer<SIMD_T> vLit2 = SIMD_T::or_si(vMask0, vMask1);
    vLit2                 = SIMD_T::or_si(vLit2, vMask2);
    vLit2                 = SIMD_T::or_si(vLit2, vMask3);
    vLit2                 = SIMD_T::or_si(vLit2, vMask4);
    vLit2                 = SIMD_T::or_si(vLit2, vMask5);
    vLit2                 = SIMD_T::or_si(vLit2, vMask6);
    vLit2                 = SIMD_T::or_si(vLit2, vMask7);

    Integer<SIMD_T> vLit = SIMD_T::or_si(vLit1, vLit2);

#else
    // Generic algorithm sweeping in row by row order
    Integer<SIMD_T> vRowMask[ER_SIMD_TILE_Y_DIM];

    Integer<SIMD_T> vEdge0N = vEdge0;
    Integer<SIMD_T> vEdge1N = vEdge1;
    Integer<SIMD_T> vEdge2N = vEdge2;

    for (uint32_t row = 0; row < ER_SIMD_TILE_Y_DIM; row++)
    {
        // Store edge values at the beginning of the row
        Integer<SIMD_T> vRowEdge0 = vEdge0N;
        Integer<SIMD_T> vRowEdge1 = vEdge1N;
        Integer<SIMD_T> vRowEdge2 = vEdge2N;

        Integer<SIMD_T> vColMask[ER_SIMD_TILE_X_DIM];

        for (uint32_t col = 0; col < ER_SIMD_TILE_X_DIM; col++)
        {
            vColMask[col] = SIMD_T::and_si(vEdge0N, vEdge1N);
            vColMask[col] = SIMD_T::and_si(vColMask[col], vEdge2N);

            vEdge0N = SIMD_T::add_epi32(vEdge0N, vAi[0]);
            vEdge1N = SIMD_T::add_epi32(vEdge1N, vAi[1]);
            vEdge2N = SIMD_T::add_epi32(vEdge2N, vAi[2]);
        }
        vRowMask[row] = vColMask[0];
        for (uint32_t col = 1; col < ER_SIMD_TILE_X_DIM; col++)
        {
            vRowMask[row] = SIMD_T::or_si(vRowMask[row], vColMask[col]);
        }
        // Restore values and go to the next row
        vEdge0N = vRowEdge0;
        vEdge1N = vRowEdge1;
        vEdge2N = vRowEdge2;

        vEdge0N = SIMD_T::add_epi32(vEdge0N, vBi[0]);
        vEdge1N = SIMD_T::add_epi32(vEdge1N, vBi[1]);
        vEdge2N = SIMD_T::add_epi32(vEdge2N, vBi[2]);
    }

    // compress all masks
    Integer<SIMD_T> vLit = vRowMask[0];
    for (uint32_t row = 1; row < ER_SIMD_TILE_Y_DIM; row++)
    {
        vLit = SIMD_T::or_si(vLit, vRowMask[row]);
    }

#endif
    // Check which triangles has any pixel lit
    uint32_t maskLit   = SIMD_T::movemask_ps(SIMD_T::castsi_ps(vLit));
    uint32_t maskUnlit = ~maskLit & oneTileMask;

    uint32_t oldTriMask = triMask;
    triMask &= ~maskUnlit;

    if (triMask ^ oldTriMask)
    {
        RDTSC_EVENT(pDC->pContext->pBucketMgr,
                    FEEarlyRastExit,
                    _mm_popcnt_u32(triMask & oneTileMask),
                    0);
    }
    return triMask;
}

#endif // Early rasterizer

//////////////////////////////////////////////////////////////////////////
/// @brief Bin triangle primitives to macro tiles. Performs setup, clipping
///        culling, viewport transform, etc.
/// @param pDC - pointer to draw context.
/// @param pa - The primitive assembly object.
/// @param workerId - thread's worker id. Even thread has a unique id.
/// @param tri - Contains triangle position data for SIMDs worth of triangles.
/// @param primID - Primitive ID for each triangle.
/// @param viewportIdx - viewport array index for each triangle.
/// @tparam CT - ConservativeRastFETraits
template <typename SIMD_T, uint32_t SIMD_WIDTH, typename CT>
void SIMDCALL BinTrianglesImpl(DRAW_CONTEXT*          pDC,
                               PA_STATE&              pa,
                               uint32_t               workerId,
                               Vec4<SIMD_T>           tri[3],
                               uint32_t               triMask,
                               Integer<SIMD_T> const& primID,
                               Integer<SIMD_T> const& viewportIdx,
                               Integer<SIMD_T> const& rtIdx)
{
    const uint32_t* aRTAI = reinterpret_cast<const uint32_t*>(&rtIdx);

    RDTSC_BEGIN(pDC->pContext->pBucketMgr, FEBinTriangles, pDC->drawId);

    const API_STATE&          state     = GetApiState(pDC);
    const SWR_RASTSTATE&      rastState = state.rastState;
    const SWR_FRONTEND_STATE& feState   = state.frontendState;

    MacroTileMgr* pTileMgr = pDC->pTileMgr;

    Float<SIMD_T> vRecipW0 = SIMD_T::set1_ps(1.0f);
    Float<SIMD_T> vRecipW1 = SIMD_T::set1_ps(1.0f);
    Float<SIMD_T> vRecipW2 = SIMD_T::set1_ps(1.0f);

    if (feState.vpTransformDisable)
    {
        // RHW is passed in directly when VP transform is disabled
        vRecipW0 = tri[0].v[3];
        vRecipW1 = tri[1].v[3];
        vRecipW2 = tri[2].v[3];
    }
    else
    {
        // Perspective divide
        vRecipW0 = SIMD_T::div_ps(SIMD_T::set1_ps(1.0f), tri[0].w);
        vRecipW1 = SIMD_T::div_ps(SIMD_T::set1_ps(1.0f), tri[1].w);
        vRecipW2 = SIMD_T::div_ps(SIMD_T::set1_ps(1.0f), tri[2].w);

        tri[0].v[0] = SIMD_T::mul_ps(tri[0].v[0], vRecipW0);
        tri[1].v[0] = SIMD_T::mul_ps(tri[1].v[0], vRecipW1);
        tri[2].v[0] = SIMD_T::mul_ps(tri[2].v[0], vRecipW2);

        tri[0].v[1] = SIMD_T::mul_ps(tri[0].v[1], vRecipW0);
        tri[1].v[1] = SIMD_T::mul_ps(tri[1].v[1], vRecipW1);
        tri[2].v[1] = SIMD_T::mul_ps(tri[2].v[1], vRecipW2);

        tri[0].v[2] = SIMD_T::mul_ps(tri[0].v[2], vRecipW0);
        tri[1].v[2] = SIMD_T::mul_ps(tri[1].v[2], vRecipW1);
        tri[2].v[2] = SIMD_T::mul_ps(tri[2].v[2], vRecipW2);

        // Viewport transform to screen space coords
        if (pa.viewportArrayActive)
        {
            viewportTransform<3>(tri, state.vpMatrices, viewportIdx);
        }
        else
        {
            viewportTransform<3>(tri, state.vpMatrices);
        }
    }

    // Adjust for pixel center location
    Float<SIMD_T> offset = SwrPixelOffsets<SIMD_T>::GetOffset(rastState.pixelLocation);

    tri[0].x = SIMD_T::add_ps(tri[0].x, offset);
    tri[0].y = SIMD_T::add_ps(tri[0].y, offset);

    tri[1].x = SIMD_T::add_ps(tri[1].x, offset);
    tri[1].y = SIMD_T::add_ps(tri[1].y, offset);

    tri[2].x = SIMD_T::add_ps(tri[2].x, offset);
    tri[2].y = SIMD_T::add_ps(tri[2].y, offset);

    // Set vXi, vYi to required fixed point precision
    Integer<SIMD_T> vXi[3], vYi[3];
    FPToFixedPoint<SIMD_T>(tri, vXi, vYi);

    // triangle setup
    Integer<SIMD_T> vAi[3], vBi[3];
    triangleSetupABIntVertical(vXi, vYi, vAi, vBi);

    // determinant
    Integer<SIMD_T> vDet[2];
    calcDeterminantIntVertical(vAi, vBi, vDet);

    // cull zero area
    uint32_t maskLo =
        SIMD_T::movemask_pd(SIMD_T::castsi_pd(SIMD_T::cmpeq_epi64(vDet[0], SIMD_T::setzero_si())));
    uint32_t maskHi =
        SIMD_T::movemask_pd(SIMD_T::castsi_pd(SIMD_T::cmpeq_epi64(vDet[1], SIMD_T::setzero_si())));

    uint32_t cullZeroAreaMask = maskLo | (maskHi << (SIMD_WIDTH / 2));

    // don't cull degenerate triangles if we're conservatively rasterizing
    uint32_t origTriMask = triMask;
    if (rastState.fillMode == SWR_FILLMODE_SOLID && !CT::IsConservativeT::value)
    {
        triMask &= ~cullZeroAreaMask;
    }

    // determine front winding tris
    // CW  +det
    // CCW det < 0;
    // 0 area triangles are marked as backfacing regardless of winding order,
    // which is required behavior for conservative rast and wireframe rendering
    uint32_t frontWindingTris;
    if (rastState.frontWinding == SWR_FRONTWINDING_CW)
    {
        maskLo = SIMD_T::movemask_pd(
            SIMD_T::castsi_pd(SIMD_T::cmpgt_epi64(vDet[0], SIMD_T::setzero_si())));
        maskHi = SIMD_T::movemask_pd(
            SIMD_T::castsi_pd(SIMD_T::cmpgt_epi64(vDet[1], SIMD_T::setzero_si())));
    }
    else
    {
        maskLo = SIMD_T::movemask_pd(
            SIMD_T::castsi_pd(SIMD_T::cmpgt_epi64(SIMD_T::setzero_si(), vDet[0])));
        maskHi = SIMD_T::movemask_pd(
            SIMD_T::castsi_pd(SIMD_T::cmpgt_epi64(SIMD_T::setzero_si(), vDet[1])));
    }
    frontWindingTris = maskLo | (maskHi << (SIMD_WIDTH / 2));

    // cull
    uint32_t cullTris;
    switch ((SWR_CULLMODE)rastState.cullMode)
    {
    case SWR_CULLMODE_BOTH:
        cullTris = 0xffffffff;
        break;
    case SWR_CULLMODE_NONE:
        cullTris = 0x0;
        break;
    case SWR_CULLMODE_FRONT:
        cullTris = frontWindingTris;
        break;
        // 0 area triangles are marked as backfacing, which is required behavior for conservative
        // rast
    case SWR_CULLMODE_BACK:
        cullTris = ~frontWindingTris;
        break;
    default:
        SWR_INVALID("Invalid cull mode: %d", rastState.cullMode);
        cullTris = 0x0;
        break;
    }

    triMask &= ~cullTris;

    if (origTriMask ^ triMask)
    {
        RDTSC_EVENT(pDC->pContext->pBucketMgr,
                    FECullZeroAreaAndBackface,
                    _mm_popcnt_u32(origTriMask ^ triMask),
                    0);
    }

    AR_EVENT(CullInfoEvent(pDC->drawId, cullZeroAreaMask, cullTris, origTriMask));

    /// Note: these variable initializations must stay above any 'goto endBenTriangles'
    // compute per tri backface
    uint32_t        frontFaceMask  = frontWindingTris;
    uint32_t*       pPrimID        = (uint32_t*)&primID;
    const uint32_t* pViewportIndex = (uint32_t*)&viewportIdx;
    uint32_t        triIndex       = 0;

    uint32_t      edgeEnable;
    PFN_WORK_FUNC pfnWork;
    if (CT::IsConservativeT::value)
    {
        // determine which edges of the degenerate tri, if any, are valid to rasterize.
        // used to call the appropriate templated rasterizer function
        if (cullZeroAreaMask > 0)
        {
            // e0 = v1-v0
            const Integer<SIMD_T> x0x1Mask = SIMD_T::cmpeq_epi32(vXi[0], vXi[1]);
            const Integer<SIMD_T> y0y1Mask = SIMD_T::cmpeq_epi32(vYi[0], vYi[1]);

            uint32_t e0Mask =
                SIMD_T::movemask_ps(SIMD_T::castsi_ps(SIMD_T::and_si(x0x1Mask, y0y1Mask)));

            // e1 = v2-v1
            const Integer<SIMD_T> x1x2Mask = SIMD_T::cmpeq_epi32(vXi[1], vXi[2]);
            const Integer<SIMD_T> y1y2Mask = SIMD_T::cmpeq_epi32(vYi[1], vYi[2]);

            uint32_t e1Mask =
                SIMD_T::movemask_ps(SIMD_T::castsi_ps(SIMD_T::and_si(x1x2Mask, y1y2Mask)));

            // e2 = v0-v2
            // if v0 == v1 & v1 == v2, v0 == v2
            uint32_t e2Mask = e0Mask & e1Mask;
            SWR_ASSERT(KNOB_SIMD_WIDTH == 8, "Need to update degenerate mask code for avx512");

            // edge order: e0 = v0v1, e1 = v1v2, e2 = v0v2
            // 32 bit binary: 0000 0000 0010 0100 1001 0010 0100 1001
            e0Mask = pdep_u32(e0Mask, 0x00249249);

            // 32 bit binary: 0000 0000 0100 1001 0010 0100 1001 0010
            e1Mask = pdep_u32(e1Mask, 0x00492492);

            // 32 bit binary: 0000 0000 1001 0010 0100 1001 0010 0100
            e2Mask = pdep_u32(e2Mask, 0x00924924);

            edgeEnable = (0x00FFFFFF & (~(e0Mask | e1Mask | e2Mask)));
        }
        else
        {
            edgeEnable = 0x00FFFFFF;
        }
    }
    else
    {
        // degenerate triangles won't be sent to rasterizer; just enable all edges
        pfnWork = GetRasterizerFunc(rastState.sampleCount,
                                    rastState.bIsCenterPattern,
                                    (rastState.conservativeRast > 0),
                                    (SWR_INPUT_COVERAGE)pDC->pState->state.psState.inputCoverage,
                                    EdgeValToEdgeState(ALL_EDGES_VALID),
                                    (state.scissorsTileAligned == false));
    }

    SIMDBBOX_T<SIMD_T> bbox;

    if (!triMask)
    {
        goto endBinTriangles;
    }

    // Calc bounding box of triangles
    calcBoundingBoxIntVertical<SIMD_T, CT>(vXi, vYi, bbox);

    // determine if triangle falls between pixel centers and discard
    // only discard for non-MSAA case and when conservative rast is disabled
    // (xmin + 127) & ~255
    // (xmax + 128) & ~255
    if ((rastState.sampleCount == SWR_MULTISAMPLE_1X || rastState.bIsCenterPattern) &&
        (!CT::IsConservativeT::value))
    {
        origTriMask = triMask;

        int cullCenterMask;

        {
            Integer<SIMD_T> xmin = SIMD_T::add_epi32(bbox.xmin, SIMD_T::set1_epi32(127));
            xmin                 = SIMD_T::and_si(xmin, SIMD_T::set1_epi32(~255));
            Integer<SIMD_T> xmax = SIMD_T::add_epi32(bbox.xmax, SIMD_T::set1_epi32(128));
            xmax                 = SIMD_T::and_si(xmax, SIMD_T::set1_epi32(~255));

            Integer<SIMD_T> vMaskH = SIMD_T::cmpeq_epi32(xmin, xmax);

            Integer<SIMD_T> ymin = SIMD_T::add_epi32(bbox.ymin, SIMD_T::set1_epi32(127));
            ymin                 = SIMD_T::and_si(ymin, SIMD_T::set1_epi32(~255));
            Integer<SIMD_T> ymax = SIMD_T::add_epi32(bbox.ymax, SIMD_T::set1_epi32(128));
            ymax                 = SIMD_T::and_si(ymax, SIMD_T::set1_epi32(~255));

            Integer<SIMD_T> vMaskV = SIMD_T::cmpeq_epi32(ymin, ymax);

            vMaskV         = SIMD_T::or_si(vMaskH, vMaskV);
            cullCenterMask = SIMD_T::movemask_ps(SIMD_T::castsi_ps(vMaskV));
        }

        triMask &= ~cullCenterMask;

        if (origTriMask ^ triMask)
        {
            RDTSC_EVENT(pDC->pContext->pBucketMgr,
                        FECullBetweenCenters,
                        _mm_popcnt_u32(origTriMask ^ triMask),
                        0);
        }
    }

    // Intersect with scissor/viewport. Subtract 1 ULP in x.8 fixed point since xmax/ymax edge is
    // exclusive. Gather the AOS effective scissor rects based on the per-prim VP index.
    /// @todo:  Look at speeding this up -- weigh against corresponding costs in rasterizer.
    {
        Integer<SIMD_T> scisXmin, scisYmin, scisXmax, scisYmax;
        if (pa.viewportArrayActive)

        {
            GatherScissors(&state.scissorsInFixedPoint[0],
                           pViewportIndex,
                           scisXmin,
                           scisYmin,
                           scisXmax,
                           scisYmax);
        }
        else // broadcast fast path for non-VPAI case.
        {
            scisXmin = SIMD_T::set1_epi32(state.scissorsInFixedPoint[0].xmin);
            scisYmin = SIMD_T::set1_epi32(state.scissorsInFixedPoint[0].ymin);
            scisXmax = SIMD_T::set1_epi32(state.scissorsInFixedPoint[0].xmax);
            scisYmax = SIMD_T::set1_epi32(state.scissorsInFixedPoint[0].ymax);
        }

        // Make triangle bbox inclusive
        bbox.xmax = SIMD_T::sub_epi32(bbox.xmax, SIMD_T::set1_epi32(1));
        bbox.ymax = SIMD_T::sub_epi32(bbox.ymax, SIMD_T::set1_epi32(1));

        bbox.xmin = SIMD_T::max_epi32(bbox.xmin, scisXmin);
        bbox.ymin = SIMD_T::max_epi32(bbox.ymin, scisYmin);
        bbox.xmax = SIMD_T::min_epi32(bbox.xmax, scisXmax);
        bbox.ymax = SIMD_T::min_epi32(bbox.ymax, scisYmax);
    }

    if (CT::IsConservativeT::value)
    {
        // in the case where a degenerate triangle is on a scissor edge, we need to make sure the
        // primitive bbox has some area. Bump the xmax/ymax edges out

        Integer<SIMD_T> topEqualsBottom = SIMD_T::cmpeq_epi32(bbox.ymin, bbox.ymax);
        bbox.ymax                       = SIMD_T::blendv_epi32(
            bbox.ymax, SIMD_T::add_epi32(bbox.ymax, SIMD_T::set1_epi32(1)), topEqualsBottom);

        Integer<SIMD_T> leftEqualsRight = SIMD_T::cmpeq_epi32(bbox.xmin, bbox.xmax);
        bbox.xmax                       = SIMD_T::blendv_epi32(
            bbox.xmax, SIMD_T::add_epi32(bbox.xmax, SIMD_T::set1_epi32(1)), leftEqualsRight);
    }

    // Cull tris completely outside scissor
    {
        Integer<SIMD_T> maskOutsideScissorX = SIMD_T::cmpgt_epi32(bbox.xmin, bbox.xmax);
        Integer<SIMD_T> maskOutsideScissorY = SIMD_T::cmpgt_epi32(bbox.ymin, bbox.ymax);
        Integer<SIMD_T> maskOutsideScissorXY =
            SIMD_T::or_si(maskOutsideScissorX, maskOutsideScissorY);
        uint32_t maskOutsideScissor = SIMD_T::movemask_ps(SIMD_T::castsi_ps(maskOutsideScissorXY));
        triMask                     = triMask & ~maskOutsideScissor;
    }

#if KNOB_ENABLE_EARLY_RAST
    if (rastState.sampleCount == SWR_MULTISAMPLE_1X && !CT::IsConservativeT::value)
    {
        // Try early rasterization - culling small triangles which do not cover any pixels

        // convert to ER tiles
        SIMDBBOX_T<SIMD_T> er_bbox;

        er_bbox.xmin =
            SIMD_T::template srai_epi32<ER_SIMD_TILE_X_SHIFT + FIXED_POINT_SHIFT>(bbox.xmin);
        er_bbox.xmax =
            SIMD_T::template srai_epi32<ER_SIMD_TILE_X_SHIFT + FIXED_POINT_SHIFT>(bbox.xmax);
        er_bbox.ymin =
            SIMD_T::template srai_epi32<ER_SIMD_TILE_Y_SHIFT + FIXED_POINT_SHIFT>(bbox.ymin);
        er_bbox.ymax =
            SIMD_T::template srai_epi32<ER_SIMD_TILE_Y_SHIFT + FIXED_POINT_SHIFT>(bbox.ymax);

        Integer<SIMD_T> vTileX = SIMD_T::cmpeq_epi32(er_bbox.xmin, er_bbox.xmax);
        Integer<SIMD_T> vTileY = SIMD_T::cmpeq_epi32(er_bbox.ymin, er_bbox.ymax);

        // Take only triangles that fit into ER tile
        uint32_t oneTileMask =
            triMask & SIMD_T::movemask_ps(SIMD_T::castsi_ps(SIMD_T::and_si(vTileX, vTileY)));

        if (oneTileMask)
        {
            // determine CW tris (det > 0)
            uint32_t maskCwLo = SIMD_T::movemask_pd(
                SIMD_T::castsi_pd(SIMD_T::cmpgt_epi64(vDet[0], SIMD_T::setzero_si())));
            uint32_t maskCwHi = SIMD_T::movemask_pd(
                SIMD_T::castsi_pd(SIMD_T::cmpgt_epi64(vDet[1], SIMD_T::setzero_si())));
            uint32_t cwTrisMask = maskCwLo | (maskCwHi << (SIMD_WIDTH / 2));

            // Try early rasterization
            triMask = EarlyRasterizer<SIMD_T, SIMD_WIDTH, CT>(
                pDC, er_bbox, vAi, vBi, vXi, vYi, cwTrisMask, triMask, oneTileMask);

            if (!triMask)
            {
                RDTSC_END(pDC->pContext->pBucketMgr, FEBinTriangles, 1);
                return;
            }
        }
    }
#endif

endBinTriangles:


    if (!triMask)
    {
        RDTSC_END(pDC->pContext->pBucketMgr, FEBinTriangles, 1);
        return;
    }

    // Send surviving triangles to the line or point binner based on fill mode
    if (rastState.fillMode == SWR_FILLMODE_WIREFRAME)
    {
        // Simple non-conformant wireframe mode, useful for debugging
        // construct 3 SIMD lines out of the triangle and call the line binner for each SIMD
        Vec4<SIMD_T>  line[2];
        Float<SIMD_T> recipW[2];

        line[0]   = tri[0];
        line[1]   = tri[1];
        recipW[0] = vRecipW0;
        recipW[1] = vRecipW1;

        BinPostSetupLinesImpl<SIMD_T, SIMD_WIDTH>(
            pDC, pa, workerId, line, recipW, triMask, primID, viewportIdx, rtIdx);

        line[0]   = tri[1];
        line[1]   = tri[2];
        recipW[0] = vRecipW1;
        recipW[1] = vRecipW2;

        BinPostSetupLinesImpl<SIMD_T, SIMD_WIDTH>(
            pDC, pa, workerId, line, recipW, triMask, primID, viewportIdx, rtIdx);

        line[0]   = tri[2];
        line[1]   = tri[0];
        recipW[0] = vRecipW2;
        recipW[1] = vRecipW0;

        BinPostSetupLinesImpl<SIMD_T, SIMD_WIDTH>(
            pDC, pa, workerId, line, recipW, triMask, primID, viewportIdx, rtIdx);

        RDTSC_END(pDC->pContext->pBucketMgr, FEBinTriangles, 1);
        return;
    }
    else if (rastState.fillMode == SWR_FILLMODE_POINT)
    {
        // Bin 3 points
        BinPostSetupPointsImpl<SIMD_T, SIMD_WIDTH>(
            pDC, pa, workerId, &tri[0], triMask, primID, viewportIdx, rtIdx);
        BinPostSetupPointsImpl<SIMD_T, SIMD_WIDTH>(
            pDC, pa, workerId, &tri[1], triMask, primID, viewportIdx, rtIdx);
        BinPostSetupPointsImpl<SIMD_T, SIMD_WIDTH>(
            pDC, pa, workerId, &tri[2], triMask, primID, viewportIdx, rtIdx);

        RDTSC_END(pDC->pContext->pBucketMgr, FEBinTriangles, 1);
        return;
    }

    // Convert triangle bbox to macrotile units.
    bbox.xmin = SIMD_T::template srai_epi32<KNOB_MACROTILE_X_DIM_FIXED_SHIFT>(bbox.xmin);
    bbox.ymin = SIMD_T::template srai_epi32<KNOB_MACROTILE_Y_DIM_FIXED_SHIFT>(bbox.ymin);
    bbox.xmax = SIMD_T::template srai_epi32<KNOB_MACROTILE_X_DIM_FIXED_SHIFT>(bbox.xmax);
    bbox.ymax = SIMD_T::template srai_epi32<KNOB_MACROTILE_Y_DIM_FIXED_SHIFT>(bbox.ymax);

    OSALIGNSIMD16(uint32_t)
    aMTLeft[SIMD_WIDTH], aMTRight[SIMD_WIDTH], aMTTop[SIMD_WIDTH], aMTBottom[SIMD_WIDTH];

    SIMD_T::store_si(reinterpret_cast<Integer<SIMD_T>*>(aMTLeft), bbox.xmin);
    SIMD_T::store_si(reinterpret_cast<Integer<SIMD_T>*>(aMTRight), bbox.xmax);
    SIMD_T::store_si(reinterpret_cast<Integer<SIMD_T>*>(aMTTop), bbox.ymin);
    SIMD_T::store_si(reinterpret_cast<Integer<SIMD_T>*>(aMTBottom), bbox.ymax);

    // transpose verts needed for backend
    /// @todo modify BE to take non-transformed verts
    OSALIGNSIMD16(simd4scalar) vHorizX[SIMD_WIDTH];
    OSALIGNSIMD16(simd4scalar) vHorizY[SIMD_WIDTH];
    OSALIGNSIMD16(simd4scalar) vHorizZ[SIMD_WIDTH];
    OSALIGNSIMD16(simd4scalar) vHorizW[SIMD_WIDTH];

    TransposeVertices(vHorizX, tri[0].x, tri[1].x, tri[2].x);
    TransposeVertices(vHorizY, tri[0].y, tri[1].y, tri[2].y);
    TransposeVertices(vHorizZ, tri[0].z, tri[1].z, tri[2].z);
    TransposeVertices(vHorizW, vRecipW0, vRecipW1, vRecipW2);

    // scan remaining valid triangles and bin each separately
    while (_BitScanForward((unsigned long*)&triIndex, triMask))
    {
        uint32_t linkageCount     = state.backendState.numAttributes;
        uint32_t numScalarAttribs = linkageCount * 4;

        BE_WORK work;
        work.type = DRAW;

        bool isDegenerate;
        if (CT::IsConservativeT::value)
        {
            // only rasterize valid edges if we have a degenerate primitive
            int32_t triEdgeEnable = (edgeEnable >> (triIndex * 3)) & ALL_EDGES_VALID;
            work.pfnWork =
                GetRasterizerFunc(rastState.sampleCount,
                                  rastState.bIsCenterPattern,
                                  (rastState.conservativeRast > 0),
                                  (SWR_INPUT_COVERAGE)pDC->pState->state.psState.inputCoverage,
                                  EdgeValToEdgeState(triEdgeEnable),
                                  (state.scissorsTileAligned == false));

            // Degenerate triangles are required to be constant interpolated
            isDegenerate = (triEdgeEnable != ALL_EDGES_VALID) ? true : false;
        }
        else
        {
            isDegenerate = false;
            work.pfnWork = pfnWork;
        }

        // Select attribute processor
        PFN_PROCESS_ATTRIBUTES pfnProcessAttribs =
            GetProcessAttributesFunc(3,
                                     state.backendState.swizzleEnable,
                                     state.backendState.constantInterpolationMask,
                                     isDegenerate);

        TRIANGLE_WORK_DESC& desc = work.desc.tri;

        desc.triFlags.frontFacing = state.forceFront ? 1 : ((frontFaceMask >> triIndex) & 1);
        desc.triFlags.renderTargetArrayIndex = aRTAI[triIndex];
        desc.triFlags.viewportIndex          = pViewportIndex[triIndex];

        auto pArena = pDC->pArena;
        SWR_ASSERT(pArena != nullptr);

        // store active attribs
        float* pAttribs = (float*)pArena->AllocAligned(numScalarAttribs * 3 * sizeof(float), 16);
        desc.pAttribs   = pAttribs;
        desc.numAttribs = linkageCount;
        pfnProcessAttribs(pDC, pa, triIndex, pPrimID[triIndex], desc.pAttribs);

        // store triangle vertex data
        desc.pTriBuffer = (float*)pArena->AllocAligned(4 * 4 * sizeof(float), 16);

        SIMD128::store_ps(&desc.pTriBuffer[0], vHorizX[triIndex]);
        SIMD128::store_ps(&desc.pTriBuffer[4], vHorizY[triIndex]);
        SIMD128::store_ps(&desc.pTriBuffer[8], vHorizZ[triIndex]);
        SIMD128::store_ps(&desc.pTriBuffer[12], vHorizW[triIndex]);

        // store user clip distances
        if (state.backendState.clipDistanceMask)
        {
            uint32_t numClipDist = _mm_popcnt_u32(state.backendState.clipDistanceMask);
            desc.pUserClipBuffer = (float*)pArena->Alloc(numClipDist * 3 * sizeof(float));
            ProcessUserClipDist<3>(
                state.backendState, pa, triIndex, &desc.pTriBuffer[12], desc.pUserClipBuffer);
        }

        for (uint32_t y = aMTTop[triIndex]; y <= aMTBottom[triIndex]; ++y)
        {
            for (uint32_t x = aMTLeft[triIndex]; x <= aMTRight[triIndex]; ++x)
            {
#if KNOB_ENABLE_TOSS_POINTS
                if (!KNOB_TOSS_SETUP_TRIS)
#endif
                {
                    pTileMgr->enqueue(x, y, &work);
                }
            }
        }

        triMask &= ~(1 << triIndex);
    }

    RDTSC_END(pDC->pContext->pBucketMgr, FEBinTriangles, 1);
}

template <typename CT>
void BinTriangles(DRAW_CONTEXT*      pDC,
                  PA_STATE&          pa,
                  uint32_t           workerId,
                  simdvector         tri[3],
                  uint32_t           triMask,
                  simdscalari const& primID,
                  simdscalari const& viewportIdx,
                  simdscalari const& rtIdx)
{
    BinTrianglesImpl<SIMD256, KNOB_SIMD_WIDTH, CT>(
        pDC, pa, workerId, tri, triMask, primID, viewportIdx, rtIdx);
}

#if USE_SIMD16_FRONTEND
template <typename CT>
void SIMDCALL BinTriangles_simd16(DRAW_CONTEXT*        pDC,
                                  PA_STATE&            pa,
                                  uint32_t             workerId,
                                  simd16vector         tri[3],
                                  uint32_t             triMask,
                                  simd16scalari const& primID,
                                  simd16scalari const& viewportIdx,
                                  simd16scalari const& rtIdx)
{
    BinTrianglesImpl<SIMD512, KNOB_SIMD16_WIDTH, CT>(
        pDC, pa, workerId, tri, triMask, primID, viewportIdx, rtIdx);
}

#endif
struct FEBinTrianglesChooser
{
    typedef PFN_PROCESS_PRIMS FuncType;

    template <typename... ArgsB>
    static FuncType GetFunc()
    {
        return BinTriangles<ConservativeRastFETraits<ArgsB...>>;
    }
};

// Selector for correct templated BinTrinagles function
PFN_PROCESS_PRIMS GetBinTrianglesFunc(bool IsConservative)
{
    return TemplateArgUnroller<FEBinTrianglesChooser>::GetFunc(IsConservative);
}

#if USE_SIMD16_FRONTEND
struct FEBinTrianglesChooser_simd16
{
    typedef PFN_PROCESS_PRIMS_SIMD16 FuncType;

    template <typename... ArgsB>
    static FuncType GetFunc()
    {
        return BinTriangles_simd16<ConservativeRastFETraits<ArgsB...>>;
    }
};

// Selector for correct templated BinTrinagles function
PFN_PROCESS_PRIMS_SIMD16 GetBinTrianglesFunc_simd16(bool IsConservative)
{
    return TemplateArgUnroller<FEBinTrianglesChooser_simd16>::GetFunc(IsConservative);
}

#endif

template <typename SIMD_T, uint32_t SIMD_WIDTH>
void BinPostSetupPointsImpl(DRAW_CONTEXT*          pDC,
                            PA_STATE&              pa,
                            uint32_t               workerId,
                            Vec4<SIMD_T>           prim[],
                            uint32_t               primMask,
                            Integer<SIMD_T> const& primID,
                            Integer<SIMD_T> const& viewportIdx,
                            Integer<SIMD_T> const& rtIdx)
{
    RDTSC_BEGIN(pDC->pContext->pBucketMgr, FEBinPoints, pDC->drawId);

    Vec4<SIMD_T>& primVerts = prim[0];

    const API_STATE&     state          = GetApiState(pDC);
    const SWR_RASTSTATE& rastState      = state.rastState;
    const uint32_t*      pViewportIndex = (uint32_t*)&viewportIdx;

    // Select attribute processor
    PFN_PROCESS_ATTRIBUTES pfnProcessAttribs = GetProcessAttributesFunc(
        1, state.backendState.swizzleEnable, state.backendState.constantInterpolationMask);

    // convert to fixed point
    Integer<SIMD_T> vXi, vYi;

    vXi = fpToFixedPointVertical<SIMD_T>(primVerts.x);
    vYi = fpToFixedPointVertical<SIMD_T>(primVerts.y);

    if (CanUseSimplePoints(pDC))
    {
        // adjust for ymin-xmin rule
        vXi = SIMD_T::sub_epi32(vXi, SIMD_T::set1_epi32(1));
        vYi = SIMD_T::sub_epi32(vYi, SIMD_T::set1_epi32(1));

        // cull points off the ymin-xmin edge of the viewport
        primMask &= ~SIMD_T::movemask_ps(SIMD_T::castsi_ps(vXi));
        primMask &= ~SIMD_T::movemask_ps(SIMD_T::castsi_ps(vYi));

        // compute macro tile coordinates
        Integer<SIMD_T> macroX = SIMD_T::template srai_epi32<KNOB_MACROTILE_X_DIM_FIXED_SHIFT>(vXi);
        Integer<SIMD_T> macroY = SIMD_T::template srai_epi32<KNOB_MACROTILE_Y_DIM_FIXED_SHIFT>(vYi);

        OSALIGNSIMD16(uint32_t) aMacroX[SIMD_WIDTH], aMacroY[SIMD_WIDTH];

        SIMD_T::store_si(reinterpret_cast<Integer<SIMD_T>*>(aMacroX), macroX);
        SIMD_T::store_si(reinterpret_cast<Integer<SIMD_T>*>(aMacroY), macroY);

        // compute raster tile coordinates
        Integer<SIMD_T> rasterX =
            SIMD_T::template srai_epi32<KNOB_TILE_X_DIM_SHIFT + FIXED_POINT_SHIFT>(vXi);
        Integer<SIMD_T> rasterY =
            SIMD_T::template srai_epi32<KNOB_TILE_Y_DIM_SHIFT + FIXED_POINT_SHIFT>(vYi);

        // compute raster tile relative x,y for coverage mask
        Integer<SIMD_T> tileAlignedX = SIMD_T::template slli_epi32<KNOB_TILE_X_DIM_SHIFT>(rasterX);
        Integer<SIMD_T> tileAlignedY = SIMD_T::template slli_epi32<KNOB_TILE_Y_DIM_SHIFT>(rasterY);

        Integer<SIMD_T> tileRelativeX =
            SIMD_T::sub_epi32(SIMD_T::template srai_epi32<FIXED_POINT_SHIFT>(vXi), tileAlignedX);
        Integer<SIMD_T> tileRelativeY =
            SIMD_T::sub_epi32(SIMD_T::template srai_epi32<FIXED_POINT_SHIFT>(vYi), tileAlignedY);

        OSALIGNSIMD16(uint32_t) aTileRelativeX[SIMD_WIDTH];
        OSALIGNSIMD16(uint32_t) aTileRelativeY[SIMD_WIDTH];

        SIMD_T::store_si(reinterpret_cast<Integer<SIMD_T>*>(aTileRelativeX), tileRelativeX);
        SIMD_T::store_si(reinterpret_cast<Integer<SIMD_T>*>(aTileRelativeY), tileRelativeY);

        OSALIGNSIMD16(uint32_t) aTileAlignedX[SIMD_WIDTH];
        OSALIGNSIMD16(uint32_t) aTileAlignedY[SIMD_WIDTH];

        SIMD_T::store_si(reinterpret_cast<Integer<SIMD_T>*>(aTileAlignedX), tileAlignedX);
        SIMD_T::store_si(reinterpret_cast<Integer<SIMD_T>*>(aTileAlignedY), tileAlignedY);

        OSALIGNSIMD16(float) aZ[SIMD_WIDTH];
        SIMD_T::store_ps(reinterpret_cast<float*>(aZ), primVerts.z);

        // store render target array index
        const uint32_t* aRTAI = reinterpret_cast<const uint32_t*>(&rtIdx);

        uint32_t* pPrimID   = (uint32_t*)&primID;
        uint32_t  primIndex = 0;

        const SWR_BACKEND_STATE& backendState = pDC->pState->state.backendState;

        // scan remaining valid triangles and bin each separately
        while (_BitScanForward((unsigned long*)&primIndex, primMask))
        {
            uint32_t linkageCount     = backendState.numAttributes;
            uint32_t numScalarAttribs = linkageCount * 4;

            BE_WORK work;
            work.type = DRAW;

            TRIANGLE_WORK_DESC& desc = work.desc.tri;

            // points are always front facing
            desc.triFlags.frontFacing            = 1;
            desc.triFlags.renderTargetArrayIndex = aRTAI[primIndex];
            desc.triFlags.viewportIndex          = pViewportIndex[primIndex];

            work.pfnWork = RasterizeSimplePoint;

            auto pArena = pDC->pArena;
            SWR_ASSERT(pArena != nullptr);

            // store attributes
            float* pAttribs =
                (float*)pArena->AllocAligned(3 * numScalarAttribs * sizeof(float), 16);
            desc.pAttribs   = pAttribs;
            desc.numAttribs = linkageCount;

            pfnProcessAttribs(pDC, pa, primIndex, pPrimID[primIndex], pAttribs);

            // store raster tile aligned x, y, perspective correct z
            float* pTriBuffer        = (float*)pArena->AllocAligned(4 * sizeof(float), 16);
            desc.pTriBuffer          = pTriBuffer;
            *(uint32_t*)pTriBuffer++ = aTileAlignedX[primIndex];
            *(uint32_t*)pTriBuffer++ = aTileAlignedY[primIndex];
            *pTriBuffer              = aZ[primIndex];

            uint32_t tX = aTileRelativeX[primIndex];
            uint32_t tY = aTileRelativeY[primIndex];

            // pack the relative x,y into the coverageMask, the rasterizer will
            // generate the true coverage mask from it
            work.desc.tri.triFlags.coverageMask = tX | (tY << 4);

            // bin it
            MacroTileMgr* pTileMgr = pDC->pTileMgr;
#if KNOB_ENABLE_TOSS_POINTS
            if (!KNOB_TOSS_SETUP_TRIS)
#endif
            {
                pTileMgr->enqueue(aMacroX[primIndex], aMacroY[primIndex], &work);
            }

            primMask &= ~(1 << primIndex);
        }
    }
    else
    {
        // non simple points need to be potentially binned to multiple macro tiles
        Float<SIMD_T> vPointSize;

        if (rastState.pointParam)
        {
            Vec4<SIMD_T> size[3];
            pa.Assemble(VERTEX_SGV_SLOT, size);
            vPointSize = size[0][VERTEX_SGV_POINT_SIZE_COMP];
        }
        else
        {
            vPointSize = SIMD_T::set1_ps(rastState.pointSize);
        }

        // bloat point to bbox
        SIMDBBOX_T<SIMD_T> bbox;

        bbox.xmin = bbox.xmax = vXi;
        bbox.ymin = bbox.ymax = vYi;

        Float<SIMD_T>   vHalfWidth  = SIMD_T::mul_ps(vPointSize, SIMD_T::set1_ps(0.5f));
        Integer<SIMD_T> vHalfWidthi = fpToFixedPointVertical<SIMD_T>(vHalfWidth);

        bbox.xmin = SIMD_T::sub_epi32(bbox.xmin, vHalfWidthi);
        bbox.xmax = SIMD_T::add_epi32(bbox.xmax, vHalfWidthi);
        bbox.ymin = SIMD_T::sub_epi32(bbox.ymin, vHalfWidthi);
        bbox.ymax = SIMD_T::add_epi32(bbox.ymax, vHalfWidthi);

        // Intersect with scissor/viewport. Subtract 1 ULP in x.8 fixed point since xmax/ymax edge
        // is exclusive. Gather the AOS effective scissor rects based on the per-prim VP index.
        /// @todo:  Look at speeding this up -- weigh against corresponding costs in rasterizer.
        {
            Integer<SIMD_T> scisXmin, scisYmin, scisXmax, scisYmax;

            if (pa.viewportArrayActive)
            {
                GatherScissors(&state.scissorsInFixedPoint[0],
                               pViewportIndex,
                               scisXmin,
                               scisYmin,
                               scisXmax,
                               scisYmax);
            }
            else // broadcast fast path for non-VPAI case.
            {
                scisXmin = SIMD_T::set1_epi32(state.scissorsInFixedPoint[0].xmin);
                scisYmin = SIMD_T::set1_epi32(state.scissorsInFixedPoint[0].ymin);
                scisXmax = SIMD_T::set1_epi32(state.scissorsInFixedPoint[0].xmax);
                scisYmax = SIMD_T::set1_epi32(state.scissorsInFixedPoint[0].ymax);
            }

            bbox.xmin = SIMD_T::max_epi32(bbox.xmin, scisXmin);
            bbox.ymin = SIMD_T::max_epi32(bbox.ymin, scisYmin);
            bbox.xmax =
                SIMD_T::min_epi32(SIMD_T::sub_epi32(bbox.xmax, SIMD_T::set1_epi32(1)), scisXmax);
            bbox.ymax =
                SIMD_T::min_epi32(SIMD_T::sub_epi32(bbox.ymax, SIMD_T::set1_epi32(1)), scisYmax);
        }

        // Cull bloated points completely outside scissor
        Integer<SIMD_T> maskOutsideScissorX = SIMD_T::cmpgt_epi32(bbox.xmin, bbox.xmax);
        Integer<SIMD_T> maskOutsideScissorY = SIMD_T::cmpgt_epi32(bbox.ymin, bbox.ymax);
        Integer<SIMD_T> maskOutsideScissorXY =
            SIMD_T::or_si(maskOutsideScissorX, maskOutsideScissorY);
        uint32_t maskOutsideScissor = SIMD_T::movemask_ps(SIMD_T::castsi_ps(maskOutsideScissorXY));
        primMask                    = primMask & ~maskOutsideScissor;

        // Convert bbox to macrotile units.
        bbox.xmin = SIMD_T::template srai_epi32<KNOB_MACROTILE_X_DIM_FIXED_SHIFT>(bbox.xmin);
        bbox.ymin = SIMD_T::template srai_epi32<KNOB_MACROTILE_Y_DIM_FIXED_SHIFT>(bbox.ymin);
        bbox.xmax = SIMD_T::template srai_epi32<KNOB_MACROTILE_X_DIM_FIXED_SHIFT>(bbox.xmax);
        bbox.ymax = SIMD_T::template srai_epi32<KNOB_MACROTILE_Y_DIM_FIXED_SHIFT>(bbox.ymax);

        OSALIGNSIMD16(uint32_t)
        aMTLeft[SIMD_WIDTH], aMTRight[SIMD_WIDTH], aMTTop[SIMD_WIDTH], aMTBottom[SIMD_WIDTH];

        SIMD_T::store_si(reinterpret_cast<Integer<SIMD_T>*>(aMTLeft), bbox.xmin);
        SIMD_T::store_si(reinterpret_cast<Integer<SIMD_T>*>(aMTRight), bbox.xmax);
        SIMD_T::store_si(reinterpret_cast<Integer<SIMD_T>*>(aMTTop), bbox.ymin);
        SIMD_T::store_si(reinterpret_cast<Integer<SIMD_T>*>(aMTBottom), bbox.ymax);

        // store render target array index
        const uint32_t* aRTAI = reinterpret_cast<const uint32_t*>(&rtIdx);

        OSALIGNSIMD16(float) aPointSize[SIMD_WIDTH];
        SIMD_T::store_ps(reinterpret_cast<float*>(aPointSize), vPointSize);

        uint32_t* pPrimID = (uint32_t*)&primID;

        OSALIGNSIMD16(float) aPrimVertsX[SIMD_WIDTH];
        OSALIGNSIMD16(float) aPrimVertsY[SIMD_WIDTH];
        OSALIGNSIMD16(float) aPrimVertsZ[SIMD_WIDTH];

        SIMD_T::store_ps(reinterpret_cast<float*>(aPrimVertsX), primVerts.x);
        SIMD_T::store_ps(reinterpret_cast<float*>(aPrimVertsY), primVerts.y);
        SIMD_T::store_ps(reinterpret_cast<float*>(aPrimVertsZ), primVerts.z);

        // scan remaining valid prims and bin each separately
        const SWR_BACKEND_STATE& backendState = state.backendState;
        uint32_t                 primIndex;
        while (_BitScanForward((unsigned long*)&primIndex, primMask))
        {
            uint32_t linkageCount     = backendState.numAttributes;
            uint32_t numScalarAttribs = linkageCount * 4;

            BE_WORK work;
            work.type = DRAW;

            TRIANGLE_WORK_DESC& desc = work.desc.tri;

            desc.triFlags.frontFacing            = 1;
            desc.triFlags.pointSize              = aPointSize[primIndex];
            desc.triFlags.renderTargetArrayIndex = aRTAI[primIndex];
            desc.triFlags.viewportIndex          = pViewportIndex[primIndex];

            work.pfnWork = RasterizeTriPoint;

            auto pArena = pDC->pArena;
            SWR_ASSERT(pArena != nullptr);

            // store active attribs
            desc.pAttribs = (float*)pArena->AllocAligned(numScalarAttribs * 3 * sizeof(float), 16);
            desc.numAttribs = linkageCount;
            pfnProcessAttribs(pDC, pa, primIndex, pPrimID[primIndex], desc.pAttribs);

            // store point vertex data
            float* pTriBuffer = (float*)pArena->AllocAligned(4 * sizeof(float), 16);
            desc.pTriBuffer   = pTriBuffer;
            *pTriBuffer++     = aPrimVertsX[primIndex];
            *pTriBuffer++     = aPrimVertsY[primIndex];
            *pTriBuffer       = aPrimVertsZ[primIndex];

            // store user clip distances
            if (backendState.clipDistanceMask)
            {
                uint32_t numClipDist = _mm_popcnt_u32(backendState.clipDistanceMask);
                desc.pUserClipBuffer = (float*)pArena->Alloc(numClipDist * 3 * sizeof(float));
                float dists[8];
                float one = 1.0f;
                ProcessUserClipDist<1>(backendState, pa, primIndex, &one, dists);
                for (uint32_t i = 0; i < numClipDist; i++)
                {
                    desc.pUserClipBuffer[3 * i + 0] = 0.0f;
                    desc.pUserClipBuffer[3 * i + 1] = 0.0f;
                    desc.pUserClipBuffer[3 * i + 2] = dists[i];
                }
            }

            MacroTileMgr* pTileMgr = pDC->pTileMgr;
            for (uint32_t y = aMTTop[primIndex]; y <= aMTBottom[primIndex]; ++y)
            {
                for (uint32_t x = aMTLeft[primIndex]; x <= aMTRight[primIndex]; ++x)
                {
#if KNOB_ENABLE_TOSS_POINTS
                    if (!KNOB_TOSS_SETUP_TRIS)
#endif
                    {
                        pTileMgr->enqueue(x, y, &work);
                    }
                }
            }

            primMask &= ~(1 << primIndex);
        }
    }

    RDTSC_END(pDC->pContext->pBucketMgr, FEBinPoints, 1);
}

//////////////////////////////////////////////////////////////////////////
/// @brief Bin SIMD points to the backend.  Only supports point size of 1
/// @param pDC - pointer to draw context.
/// @param pa - The primitive assembly object.
/// @param workerId - thread's worker id. Even thread has a unique id.
/// @param tri - Contains point position data for SIMDs worth of points.
/// @param primID - Primitive ID for each point.
template <typename SIMD_T, uint32_t SIMD_WIDTH>
void BinPointsImpl(DRAW_CONTEXT*          pDC,
                   PA_STATE&              pa,
                   uint32_t               workerId,
                   Vec4<SIMD_T>           prim[3],
                   uint32_t               primMask,
                   Integer<SIMD_T> const& primID,
                   Integer<SIMD_T> const& viewportIdx,
                   Integer<SIMD_T> const& rtIdx)
{
    const API_STATE&          state     = GetApiState(pDC);
    const SWR_FRONTEND_STATE& feState   = state.frontendState;
    const SWR_RASTSTATE&      rastState = state.rastState;

    if (!feState.vpTransformDisable)
    {
        // perspective divide
        Float<SIMD_T> vRecipW0 = SIMD_T::div_ps(SIMD_T::set1_ps(1.0f), prim[0].w);

        prim[0].x = SIMD_T::mul_ps(prim[0].x, vRecipW0);
        prim[0].y = SIMD_T::mul_ps(prim[0].y, vRecipW0);
        prim[0].z = SIMD_T::mul_ps(prim[0].z, vRecipW0);

        // viewport transform to screen coords
        if (pa.viewportArrayActive)
        {
            viewportTransform<1>(prim, state.vpMatrices, viewportIdx);
        }
        else
        {
            viewportTransform<1>(prim, state.vpMatrices);
        }
    }

    Float<SIMD_T> offset = SwrPixelOffsets<SIMD_T>::GetOffset(rastState.pixelLocation);

    prim[0].x = SIMD_T::add_ps(prim[0].x, offset);
    prim[0].y = SIMD_T::add_ps(prim[0].y, offset);

    BinPostSetupPointsImpl<SIMD_T, SIMD_WIDTH>(
        pDC, pa, workerId, prim, primMask, primID, viewportIdx, rtIdx);
}

void BinPoints(DRAW_CONTEXT*      pDC,
               PA_STATE&          pa,
               uint32_t           workerId,
               simdvector         prim[3],
               uint32_t           primMask,
               simdscalari const& primID,
               simdscalari const& viewportIdx,
               simdscalari const& rtIdx)
{
    BinPointsImpl<SIMD256, KNOB_SIMD_WIDTH>(
        pDC, pa, workerId, prim, primMask, primID, viewportIdx, rtIdx);
}

#if USE_SIMD16_FRONTEND
void SIMDCALL BinPoints_simd16(DRAW_CONTEXT*        pDC,
                               PA_STATE&            pa,
                               uint32_t             workerId,
                               simd16vector         prim[3],
                               uint32_t             primMask,
                               simd16scalari const& primID,
                               simd16scalari const& viewportIdx,
                               simd16scalari const& rtIdx)
{
    BinPointsImpl<SIMD512, KNOB_SIMD16_WIDTH>(
        pDC, pa, workerId, prim, primMask, primID, viewportIdx, rtIdx);
}

#endif
//////////////////////////////////////////////////////////////////////////
/// @brief Bin SIMD lines to the backend.
/// @param pDC - pointer to draw context.
/// @param pa - The primitive assembly object.
/// @param workerId - thread's worker id. Even thread has a unique id.
/// @param tri - Contains line position data for SIMDs worth of points.
/// @param primID - Primitive ID for each line.
/// @param viewportIdx - Viewport Array Index for each line.
template <typename SIMD_T, uint32_t SIMD_WIDTH>
void BinPostSetupLinesImpl(DRAW_CONTEXT*          pDC,
                           PA_STATE&              pa,
                           uint32_t               workerId,
                           Vec4<SIMD_T>           prim[],
                           Float<SIMD_T>          recipW[],
                           uint32_t               primMask,
                           Integer<SIMD_T> const& primID,
                           Integer<SIMD_T> const& viewportIdx,
                           Integer<SIMD_T> const& rtIdx)
{
    const uint32_t* aRTAI = reinterpret_cast<const uint32_t*>(&rtIdx);

    RDTSC_BEGIN(pDC->pContext->pBucketMgr, FEBinLines, pDC->drawId);

    const API_STATE&     state     = GetApiState(pDC);
    const SWR_RASTSTATE& rastState = state.rastState;

    // Select attribute processor
    PFN_PROCESS_ATTRIBUTES pfnProcessAttribs = GetProcessAttributesFunc(
        2, state.backendState.swizzleEnable, state.backendState.constantInterpolationMask);

    Float<SIMD_T>& vRecipW0 = recipW[0];
    Float<SIMD_T>& vRecipW1 = recipW[1];

    // convert to fixed point
    Integer<SIMD_T> vXi[2], vYi[2];

    vXi[0] = fpToFixedPointVertical<SIMD_T>(prim[0].x);
    vYi[0] = fpToFixedPointVertical<SIMD_T>(prim[0].y);
    vXi[1] = fpToFixedPointVertical<SIMD_T>(prim[1].x);
    vYi[1] = fpToFixedPointVertical<SIMD_T>(prim[1].y);

    // compute x-major vs y-major mask
    Integer<SIMD_T> xLength     = SIMD_T::abs_epi32(SIMD_T::sub_epi32(vXi[0], vXi[1]));
    Integer<SIMD_T> yLength     = SIMD_T::abs_epi32(SIMD_T::sub_epi32(vYi[0], vYi[1]));
    Float<SIMD_T>   vYmajorMask = SIMD_T::castsi_ps(SIMD_T::cmpgt_epi32(yLength, xLength));
    uint32_t        yMajorMask  = SIMD_T::movemask_ps(vYmajorMask);

    // cull zero-length lines
    Integer<SIMD_T> vZeroLengthMask = SIMD_T::cmpeq_epi32(xLength, SIMD_T::setzero_si());
    vZeroLengthMask =
        SIMD_T::and_si(vZeroLengthMask, SIMD_T::cmpeq_epi32(yLength, SIMD_T::setzero_si()));

    primMask &= ~SIMD_T::movemask_ps(SIMD_T::castsi_ps(vZeroLengthMask));

    uint32_t*       pPrimID        = (uint32_t*)&primID;
    const uint32_t* pViewportIndex = (uint32_t*)&viewportIdx;

    // Calc bounding box of lines
    SIMDBBOX_T<SIMD_T> bbox;
    bbox.xmin = SIMD_T::min_epi32(vXi[0], vXi[1]);
    bbox.xmax = SIMD_T::max_epi32(vXi[0], vXi[1]);
    bbox.ymin = SIMD_T::min_epi32(vYi[0], vYi[1]);
    bbox.ymax = SIMD_T::max_epi32(vYi[0], vYi[1]);

    // bloat bbox by line width along minor axis
    Float<SIMD_T>   vHalfWidth  = SIMD_T::set1_ps(rastState.lineWidth / 2.0f);
    Integer<SIMD_T> vHalfWidthi = fpToFixedPointVertical<SIMD_T>(vHalfWidth);

    SIMDBBOX_T<SIMD_T> bloatBox;

    bloatBox.xmin = SIMD_T::sub_epi32(bbox.xmin, vHalfWidthi);
    bloatBox.xmax = SIMD_T::add_epi32(bbox.xmax, vHalfWidthi);
    bloatBox.ymin = SIMD_T::sub_epi32(bbox.ymin, vHalfWidthi);
    bloatBox.ymax = SIMD_T::add_epi32(bbox.ymax, vHalfWidthi);

    bbox.xmin = SIMD_T::blendv_epi32(bbox.xmin, bloatBox.xmin, vYmajorMask);
    bbox.xmax = SIMD_T::blendv_epi32(bbox.xmax, bloatBox.xmax, vYmajorMask);
    bbox.ymin = SIMD_T::blendv_epi32(bloatBox.ymin, bbox.ymin, vYmajorMask);
    bbox.ymax = SIMD_T::blendv_epi32(bloatBox.ymax, bbox.ymax, vYmajorMask);

    // Intersect with scissor/viewport. Subtract 1 ULP in x.8 fixed point since xmax/ymax edge is
    // exclusive.
    {
        Integer<SIMD_T> scisXmin, scisYmin, scisXmax, scisYmax;

        if (pa.viewportArrayActive)
        {
            GatherScissors(&state.scissorsInFixedPoint[0],
                           pViewportIndex,
                           scisXmin,
                           scisYmin,
                           scisXmax,
                           scisYmax);
        }
        else // broadcast fast path for non-VPAI case.
        {
            scisXmin = SIMD_T::set1_epi32(state.scissorsInFixedPoint[0].xmin);
            scisYmin = SIMD_T::set1_epi32(state.scissorsInFixedPoint[0].ymin);
            scisXmax = SIMD_T::set1_epi32(state.scissorsInFixedPoint[0].xmax);
            scisYmax = SIMD_T::set1_epi32(state.scissorsInFixedPoint[0].ymax);
        }

        bbox.xmin = SIMD_T::max_epi32(bbox.xmin, scisXmin);
        bbox.ymin = SIMD_T::max_epi32(bbox.ymin, scisYmin);
        bbox.xmax =
            SIMD_T::min_epi32(SIMD_T::sub_epi32(bbox.xmax, SIMD_T::set1_epi32(1)), scisXmax);
        bbox.ymax =
            SIMD_T::min_epi32(SIMD_T::sub_epi32(bbox.ymax, SIMD_T::set1_epi32(1)), scisYmax);
    }

    // Cull prims completely outside scissor
    {
        Integer<SIMD_T> maskOutsideScissorX = SIMD_T::cmpgt_epi32(bbox.xmin, bbox.xmax);
        Integer<SIMD_T> maskOutsideScissorY = SIMD_T::cmpgt_epi32(bbox.ymin, bbox.ymax);
        Integer<SIMD_T> maskOutsideScissorXY =
            SIMD_T::or_si(maskOutsideScissorX, maskOutsideScissorY);
        uint32_t maskOutsideScissor = SIMD_T::movemask_ps(SIMD_T::castsi_ps(maskOutsideScissorXY));
        primMask                    = primMask & ~maskOutsideScissor;
    }

    // transpose verts needed for backend
    /// @todo modify BE to take non-transformed verts
    OSALIGNSIMD16(simd4scalar) vHorizX[SIMD_WIDTH];
    OSALIGNSIMD16(simd4scalar) vHorizY[SIMD_WIDTH];
    OSALIGNSIMD16(simd4scalar) vHorizZ[SIMD_WIDTH];
    OSALIGNSIMD16(simd4scalar) vHorizW[SIMD_WIDTH];

    if (!primMask)
    {
        goto endBinLines;
    }

    // Convert triangle bbox to macrotile units.
    bbox.xmin = SIMD_T::template srai_epi32<KNOB_MACROTILE_X_DIM_FIXED_SHIFT>(bbox.xmin);
    bbox.ymin = SIMD_T::template srai_epi32<KNOB_MACROTILE_Y_DIM_FIXED_SHIFT>(bbox.ymin);
    bbox.xmax = SIMD_T::template srai_epi32<KNOB_MACROTILE_X_DIM_FIXED_SHIFT>(bbox.xmax);
    bbox.ymax = SIMD_T::template srai_epi32<KNOB_MACROTILE_Y_DIM_FIXED_SHIFT>(bbox.ymax);

    OSALIGNSIMD16(uint32_t)
    aMTLeft[SIMD_WIDTH], aMTRight[SIMD_WIDTH], aMTTop[SIMD_WIDTH], aMTBottom[SIMD_WIDTH];

    SIMD_T::store_si(reinterpret_cast<Integer<SIMD_T>*>(aMTLeft), bbox.xmin);
    SIMD_T::store_si(reinterpret_cast<Integer<SIMD_T>*>(aMTRight), bbox.xmax);
    SIMD_T::store_si(reinterpret_cast<Integer<SIMD_T>*>(aMTTop), bbox.ymin);
    SIMD_T::store_si(reinterpret_cast<Integer<SIMD_T>*>(aMTBottom), bbox.ymax);

    TransposeVertices(vHorizX, prim[0].x, prim[1].x, SIMD_T::setzero_ps());
    TransposeVertices(vHorizY, prim[0].y, prim[1].y, SIMD_T::setzero_ps());
    TransposeVertices(vHorizZ, prim[0].z, prim[1].z, SIMD_T::setzero_ps());
    TransposeVertices(vHorizW, vRecipW0, vRecipW1, SIMD_T::setzero_ps());

    // scan remaining valid prims and bin each separately
    unsigned long primIndex;
    while (_BitScanForward(&primIndex, primMask))
    {
        uint32_t linkageCount     = state.backendState.numAttributes;
        uint32_t numScalarAttribs = linkageCount * 4;

        BE_WORK work;
        work.type = DRAW;

        TRIANGLE_WORK_DESC& desc = work.desc.tri;

        desc.triFlags.frontFacing            = 1;
        desc.triFlags.yMajor                 = (yMajorMask >> primIndex) & 1;
        desc.triFlags.renderTargetArrayIndex = aRTAI[primIndex];
        desc.triFlags.viewportIndex          = pViewportIndex[primIndex];

        work.pfnWork = RasterizeLine;

        auto pArena = pDC->pArena;
        SWR_ASSERT(pArena != nullptr);

        // store active attribs
        desc.pAttribs   = (float*)pArena->AllocAligned(numScalarAttribs * 3 * sizeof(float), 16);
        desc.numAttribs = linkageCount;
        pfnProcessAttribs(pDC, pa, primIndex, pPrimID[primIndex], desc.pAttribs);

        // store line vertex data
        desc.pTriBuffer = (float*)pArena->AllocAligned(4 * 4 * sizeof(float), 16);

        _mm_store_ps(&desc.pTriBuffer[0], vHorizX[primIndex]);
        _mm_store_ps(&desc.pTriBuffer[4], vHorizY[primIndex]);
        _mm_store_ps(&desc.pTriBuffer[8], vHorizZ[primIndex]);
        _mm_store_ps(&desc.pTriBuffer[12], vHorizW[primIndex]);

        // store user clip distances
        if (state.backendState.clipDistanceMask)
        {
            uint32_t numClipDist = _mm_popcnt_u32(state.backendState.clipDistanceMask);
            desc.pUserClipBuffer = (float*)pArena->Alloc(numClipDist * 2 * sizeof(float));
            ProcessUserClipDist<2>(
                state.backendState, pa, primIndex, &desc.pTriBuffer[12], desc.pUserClipBuffer);
        }

        MacroTileMgr* pTileMgr = pDC->pTileMgr;
        for (uint32_t y = aMTTop[primIndex]; y <= aMTBottom[primIndex]; ++y)
        {
            for (uint32_t x = aMTLeft[primIndex]; x <= aMTRight[primIndex]; ++x)
            {
#if KNOB_ENABLE_TOSS_POINTS
                if (!KNOB_TOSS_SETUP_TRIS)
#endif
                {
                    pTileMgr->enqueue(x, y, &work);
                }
            }
        }

        primMask &= ~(1 << primIndex);
    }

endBinLines:

    RDTSC_END(pDC->pContext->pBucketMgr, FEBinLines, 1);
}

//////////////////////////////////////////////////////////////////////////
/// @brief Bin SIMD lines to the backend.
/// @param pDC - pointer to draw context.
/// @param pa - The primitive assembly object.
/// @param workerId - thread's worker id. Even thread has a unique id.
/// @param tri - Contains line position data for SIMDs worth of points.
/// @param primID - Primitive ID for each line.
/// @param viewportIdx - Viewport Array Index for each line.
template <typename SIMD_T, uint32_t SIMD_WIDTH>
void SIMDCALL BinLinesImpl(DRAW_CONTEXT*          pDC,
                           PA_STATE&              pa,
                           uint32_t               workerId,
                           Vec4<SIMD_T>           prim[3],
                           uint32_t               primMask,
                           Integer<SIMD_T> const& primID,
                           Integer<SIMD_T> const& viewportIdx,
                           Integer<SIMD_T> const& rtIdx)
{
    const API_STATE&          state     = GetApiState(pDC);
    const SWR_RASTSTATE&      rastState = state.rastState;
    const SWR_FRONTEND_STATE& feState   = state.frontendState;

    Float<SIMD_T> vRecipW[2] = {SIMD_T::set1_ps(1.0f), SIMD_T::set1_ps(1.0f)};

    if (!feState.vpTransformDisable)
    {
        // perspective divide
        vRecipW[0] = SIMD_T::div_ps(SIMD_T::set1_ps(1.0f), prim[0].w);
        vRecipW[1] = SIMD_T::div_ps(SIMD_T::set1_ps(1.0f), prim[1].w);

        prim[0].v[0] = SIMD_T::mul_ps(prim[0].v[0], vRecipW[0]);
        prim[1].v[0] = SIMD_T::mul_ps(prim[1].v[0], vRecipW[1]);

        prim[0].v[1] = SIMD_T::mul_ps(prim[0].v[1], vRecipW[0]);
        prim[1].v[1] = SIMD_T::mul_ps(prim[1].v[1], vRecipW[1]);

        prim[0].v[2] = SIMD_T::mul_ps(prim[0].v[2], vRecipW[0]);
        prim[1].v[2] = SIMD_T::mul_ps(prim[1].v[2], vRecipW[1]);

        // viewport transform to screen coords
        if (pa.viewportArrayActive)
        {
            viewportTransform<2>(prim, state.vpMatrices, viewportIdx);
        }
        else
        {
            viewportTransform<2>(prim, state.vpMatrices);
        }
    }

    // adjust for pixel center location
    Float<SIMD_T> offset = SwrPixelOffsets<SIMD_T>::GetOffset(rastState.pixelLocation);

    prim[0].x = SIMD_T::add_ps(prim[0].x, offset);
    prim[0].y = SIMD_T::add_ps(prim[0].y, offset);

    prim[1].x = SIMD_T::add_ps(prim[1].x, offset);
    prim[1].y = SIMD_T::add_ps(prim[1].y, offset);

    BinPostSetupLinesImpl<SIMD_T, SIMD_WIDTH>(
        pDC, pa, workerId, prim, vRecipW, primMask, primID, viewportIdx, rtIdx);
}

void BinLines(DRAW_CONTEXT*      pDC,
              PA_STATE&          pa,
              uint32_t           workerId,
              simdvector         prim[],
              uint32_t           primMask,
              simdscalari const& primID,
              simdscalari const& viewportIdx,
              simdscalari const& rtIdx)
{
    BinLinesImpl<SIMD256, KNOB_SIMD_WIDTH>(
        pDC, pa, workerId, prim, primMask, primID, viewportIdx, rtIdx);
}

#if USE_SIMD16_FRONTEND
void SIMDCALL BinLines_simd16(DRAW_CONTEXT*        pDC,
                              PA_STATE&            pa,
                              uint32_t             workerId,
                              simd16vector         prim[3],
                              uint32_t             primMask,
                              simd16scalari const& primID,
                              simd16scalari const& viewportIdx,
                              simd16scalari const& rtIdx)
{
    BinLinesImpl<SIMD512, KNOB_SIMD16_WIDTH>(
        pDC, pa, workerId, prim, primMask, primID, viewportIdx, rtIdx);
}

#endif
