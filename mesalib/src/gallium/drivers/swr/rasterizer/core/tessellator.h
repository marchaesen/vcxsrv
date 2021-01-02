/****************************************************************************
 * Copyright (C) 2014-2019 without restriction, including without limitation
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
 * @file tessellator.h
 *
 * @brief Tessellator fixed function unit interface definition
 *
 ******************************************************************************/
#pragma once

#include "tessellator.hpp"

struct SWR_TS_TESSELLATED_DATA
{
    uint32_t NumPrimitives;
    uint32_t NumDomainPoints;

    uint32_t* ppIndices[3];
    float*    pDomainPointsU;
    float*    pDomainPointsV;
    // For Tri: pDomainPointsW[i] = 1.0f - pDomainPointsU[i] - pDomainPointsV[i]
};

namespace Tessellator
{
    /// Wrapper class for the CHWTessellator reference tessellator from MSFT
    /// This class will store data not originally stored in CHWTessellator
    class SWR_TS : private CHWTessellator
    {
    private:
        typedef CHWTessellator SUPER;
        SWR_TS_DOMAIN          Domain;
        OSALIGNSIMD(float)     DomainPointsU[MAX_POINT_COUNT];
        OSALIGNSIMD(float)     DomainPointsV[MAX_POINT_COUNT];
        uint32_t               NumDomainPoints;
        OSALIGNSIMD(uint32_t)  Indices[3][MAX_INDEX_COUNT / 3];
        uint32_t               NumIndices;

    public:
        void Init(SWR_TS_DOMAIN          tsDomain,
                  SWR_TS_PARTITIONING    tsPartitioning,
                  SWR_TS_OUTPUT_TOPOLOGY tsOutputTopology)
        {
            static D3D11_TESSELLATOR_PARTITIONING CVT_TS_D3D_PARTITIONING[] = {
                D3D11_TESSELLATOR_PARTITIONING_INTEGER,         // SWR_TS_INTEGER
                D3D11_TESSELLATOR_PARTITIONING_FRACTIONAL_ODD,  // SWR_TS_ODD_FRACTIONAL
                D3D11_TESSELLATOR_PARTITIONING_FRACTIONAL_EVEN, // SWR_TS_EVEN_FRACTIONAL
                D3D11_TESSELLATOR_PARTITIONING_POW2            // SWR_TS_POW2
            };

            static D3D11_TESSELLATOR_OUTPUT_PRIMITIVE CVT_TS_D3D_OUTPUT_TOPOLOGY[] = {
                D3D11_TESSELLATOR_OUTPUT_POINT,        // SWR_TS_OUTPUT_POINT
                D3D11_TESSELLATOR_OUTPUT_LINE,         // SWR_TS_OUTPUT_LINE
                D3D11_TESSELLATOR_OUTPUT_TRIANGLE_CCW,  // SWR_TS_OUTPUT_TRI_CW - inverted logic, because DX
                D3D11_TESSELLATOR_OUTPUT_TRIANGLE_CW // SWR_TS_OUTPUT_TRI_CCW - inverted logic, because DX
            };

            SUPER::Init(CVT_TS_D3D_PARTITIONING[tsPartitioning],
                        CVT_TS_D3D_OUTPUT_TOPOLOGY[tsOutputTopology]);

            Domain          = tsDomain;
            NumDomainPoints = 0;
            NumIndices      = 0;
        }

        void Tessellate(const SWR_TESSELLATION_FACTORS& tsTessFactors,
                        SWR_TS_TESSELLATED_DATA&        tsTessellatedData)
        {
            uint32_t IndexDiv = 0;
            switch (Domain)
            {
            case SWR_TS_QUAD:
                IndexDiv = 3;
                SUPER::TessellateQuadDomain(
                    tsTessFactors.OuterTessFactors[SWR_QUAD_U_EQ0_TRI_U_LINE_DETAIL],
                    tsTessFactors.OuterTessFactors[SWR_QUAD_V_EQ0_TRI_W],
                    tsTessFactors.OuterTessFactors[SWR_QUAD_U_EQ1_TRI_V_LINE_DENSITY],
                    tsTessFactors.OuterTessFactors[SWR_QUAD_V_EQ1],
                    tsTessFactors.InnerTessFactors[SWR_QUAD_U_TRI_INSIDE],
                    tsTessFactors.InnerTessFactors[SWR_QUAD_V_INSIDE]);
                break;

            case SWR_TS_TRI:
                IndexDiv = 3;
                SUPER::TessellateTriDomain(
                    tsTessFactors.OuterTessFactors[SWR_QUAD_U_EQ0_TRI_U_LINE_DETAIL],
                    tsTessFactors.OuterTessFactors[SWR_QUAD_U_EQ1_TRI_V_LINE_DENSITY],
                    tsTessFactors.OuterTessFactors[SWR_QUAD_V_EQ0_TRI_W],
                    tsTessFactors.InnerTessFactors[SWR_QUAD_U_TRI_INSIDE]);
                break;

            case SWR_TS_ISOLINE:
                IndexDiv = 2;
                SUPER::TessellateIsoLineDomain(
                    tsTessFactors.OuterTessFactors[SWR_QUAD_U_EQ1_TRI_V_LINE_DENSITY],
                    tsTessFactors.OuterTessFactors[SWR_QUAD_U_EQ0_TRI_U_LINE_DETAIL]);
                break;

            default:
                SWR_INVALID("Invalid Tessellation Domain: %d", Domain);
                assert(false);
            }

            NumDomainPoints = (uint32_t)SUPER::GetPointCount();

            DOMAIN_POINT* pPoints = SUPER::GetPoints();
            for (uint32_t i = 0; i < NumDomainPoints; i++) {
                DomainPointsU[i] = pPoints[i].u;
                DomainPointsV[i] = pPoints[i].v;
            }
            tsTessellatedData.NumDomainPoints = NumDomainPoints;
            tsTessellatedData.pDomainPointsU  = &DomainPointsU[0];
            tsTessellatedData.pDomainPointsV  = &DomainPointsV[0];

            NumIndices = (uint32_t)SUPER::GetIndexCount();

            assert(NumIndices % IndexDiv == 0);
            tsTessellatedData.NumPrimitives = NumIndices / IndexDiv;

            uint32_t* pIndices = (uint32_t*)SUPER::GetIndices();
            for (uint32_t i = 0; i < NumIndices; i++) {
                Indices[i % IndexDiv][i / IndexDiv] = pIndices[i];
            }

            tsTessellatedData.ppIndices[0] = &Indices[0][0];
            tsTessellatedData.ppIndices[1] = &Indices[1][0];
            tsTessellatedData.ppIndices[2] = &Indices[2][0];
        }
    };
} // namespace Tessellator

/// Allocate and initialize a new tessellation context
INLINE HANDLE SWR_API
              TSInitCtx(SWR_TS_DOMAIN          tsDomain, ///< [IN] Tessellation domain (isoline, quad, triangle)
                        SWR_TS_PARTITIONING    tsPartitioning, ///< [IN] Tessellation partitioning algorithm
                        SWR_TS_OUTPUT_TOPOLOGY tsOutputTopology, ///< [IN] Tessellation output topology
                        void*                  pContextMem, ///< [IN] Memory to use for the context
                        size_t& memSize) ///< [INOUT] In: Amount of memory in pContextMem. Out: Mem required
{
    using Tessellator::SWR_TS;
    SWR_ASSERT(tsDomain < SWR_TS_DOMAIN_COUNT);
    SWR_ASSERT(tsPartitioning < SWR_TS_PARTITIONING_COUNT);
    SWR_ASSERT(tsOutputTopology < SWR_TS_OUTPUT_TOPOLOGY_COUNT);

    size_t origMemSize = memSize;
    memSize            = AlignUp(sizeof(SWR_TS), 64);

    if (nullptr == pContextMem || memSize > origMemSize)
    {
        return nullptr;
    }

    HANDLE tsCtx = pContextMem;

    SWR_TS* pTessellator = new (tsCtx) SWR_TS();
    SWR_ASSERT(pTessellator == tsCtx);

    pTessellator->Init(tsDomain, tsPartitioning, tsOutputTopology);

    return tsCtx;
}

/// Destroy & de-allocate tessellation context
INLINE void SWR_API TSDestroyCtx(HANDLE tsCtx) ///< [IN] Tessellation context to be destroyed
{
    using Tessellator::SWR_TS;
    SWR_TS* pTessellator = (SWR_TS*)tsCtx;

    if (pTessellator)
    {
        pTessellator->~SWR_TS();
    }
}

/// Perform Tessellation
INLINE void SWR_API
            TSTessellate(HANDLE                          tsCtx, ///< [IN] Tessellation Context
                         const SWR_TESSELLATION_FACTORS& tsTessFactors, ///< [IN] Tessellation Factors
                         SWR_TS_TESSELLATED_DATA&        tsTessellatedData)    ///< [OUT] Tessellated Data
{
    using Tessellator::SWR_TS;
    SWR_TS* pTessellator = (SWR_TS*)tsCtx;
    SWR_ASSERT(pTessellator);

    pTessellator->Tessellate(tsTessFactors, tsTessellatedData);
}

