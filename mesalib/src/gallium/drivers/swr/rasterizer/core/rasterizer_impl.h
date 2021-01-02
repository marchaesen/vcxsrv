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
#include "rdtsc_core.h"
#include "backend.h"
#include "utils.h"
#include "frontend.h"
#include "tilemgr.h"
#include "memory/tilingtraits.h"

extern PFN_WORK_FUNC gRasterizerFuncs[SWR_MULTISAMPLE_TYPE_COUNT][2][2][SWR_INPUT_COVERAGE_COUNT]
                                     [STATE_VALID_TRI_EDGE_COUNT][2];

template <uint32_t numSamples = 1>
void GetRenderHotTiles(DRAW_CONTEXT*        pDC,
                       uint32_t             workerId,
                       uint32_t             macroID,
                       uint32_t             x,
                       uint32_t             y,
                       RenderOutputBuffers& renderBuffers,
                       uint32_t             renderTargetArrayIndex);
template <typename RT>
void StepRasterTileX(uint32_t colorHotTileMask, RenderOutputBuffers& buffers);
template <typename RT>
void StepRasterTileY(uint32_t             colorHotTileMask,
                     RenderOutputBuffers& buffers,
                     RenderOutputBuffers& startBufferRow);

#define MASKTOVEC(i3, i2, i1, i0) \
    {                             \
        -i0, -i1, -i2, -i3        \
    }
static const __m256d gMaskToVecpd[] = {
    MASKTOVEC(0, 0, 0, 0),
    MASKTOVEC(0, 0, 0, 1),
    MASKTOVEC(0, 0, 1, 0),
    MASKTOVEC(0, 0, 1, 1),
    MASKTOVEC(0, 1, 0, 0),
    MASKTOVEC(0, 1, 0, 1),
    MASKTOVEC(0, 1, 1, 0),
    MASKTOVEC(0, 1, 1, 1),
    MASKTOVEC(1, 0, 0, 0),
    MASKTOVEC(1, 0, 0, 1),
    MASKTOVEC(1, 0, 1, 0),
    MASKTOVEC(1, 0, 1, 1),
    MASKTOVEC(1, 1, 0, 0),
    MASKTOVEC(1, 1, 0, 1),
    MASKTOVEC(1, 1, 1, 0),
    MASKTOVEC(1, 1, 1, 1),
};

struct POS
{
    int32_t x, y;
};

struct EDGE
{
    double a, b;            // a, b edge coefficients in fix8
    double stepQuadX;       // step to adjacent horizontal quad in fix16
    double stepQuadY;       // step to adjacent vertical quad in fix16
    double stepRasterTileX; // step to adjacent horizontal raster tile in fix16
    double stepRasterTileY; // step to adjacent vertical raster tile in fix16

    __m256d vQuadOffsets;       // offsets for 4 samples of a quad
    __m256d vRasterTileOffsets; // offsets for the 4 corners of a raster tile
};

//////////////////////////////////////////////////////////////////////////
/// @brief rasterize a raster tile partially covered by the triangle
/// @param vEdge0-2 - edge equations evaluated at sample pos at each of the 4 corners of a raster
/// tile
/// @param vA, vB - A & B coefs for each edge of the triangle (Ax + Bx + C)
/// @param vStepQuad0-2 - edge equations evaluated at the UL corners of the 2x2 pixel quad.
///        Used to step between quads when sweeping over the raster tile.
template <uint32_t NumEdges, typename EdgeMaskT>
INLINE uint64_t rasterizePartialTile(DRAW_CONTEXT* pDC,
                                     double        startEdges[NumEdges],
                                     EDGE*         pRastEdges)
{
    uint64_t coverageMask = 0;

    __m256d vEdges[NumEdges];
    __m256d vStepX[NumEdges];
    __m256d vStepY[NumEdges];

    for (uint32_t e = 0; e < NumEdges; ++e)
    {
        // Step to the pixel sample locations of the 1st quad
        vEdges[e] = _mm256_add_pd(_mm256_set1_pd(startEdges[e]), pRastEdges[e].vQuadOffsets);

        // compute step to next quad (mul by 2 in x and y direction)
        vStepX[e] = _mm256_set1_pd(pRastEdges[e].stepQuadX);
        vStepY[e] = _mm256_set1_pd(pRastEdges[e].stepQuadY);
    }

    // fast unrolled version for 8x8 tile
#if KNOB_TILE_X_DIM == 8 && KNOB_TILE_Y_DIM == 8
    int      edgeMask[NumEdges];
    uint64_t mask;

    auto eval_lambda   = [&](int e) { edgeMask[e] = _mm256_movemask_pd(vEdges[e]); };
    auto update_lambda = [&](int e) { mask &= edgeMask[e]; };
    auto incx_lambda   = [&](int e) { vEdges[e] = _mm256_add_pd(vEdges[e], vStepX[e]); };
    auto incy_lambda   = [&](int e) { vEdges[e] = _mm256_add_pd(vEdges[e], vStepY[e]); };
    auto decx_lambda   = [&](int e) { vEdges[e] = _mm256_sub_pd(vEdges[e], vStepX[e]); };

// evaluate which pixels in the quad are covered
#define EVAL UnrollerLMask<0, NumEdges, 1, EdgeMaskT::value>::step(eval_lambda);

    // update coverage mask
    // if edge 0 is degenerate and will be skipped; init the mask
#define UPDATE_MASK(bit)                                                  \
    if (std::is_same<EdgeMaskT, E1E2ValidT>::value ||                     \
        std::is_same<EdgeMaskT, NoEdgesValidT>::value)                    \
    {                                                                     \
        mask = 0xf;                                                       \
    }                                                                     \
    else                                                                  \
    {                                                                     \
        mask = edgeMask[0];                                               \
    }                                                                     \
    UnrollerLMask<1, NumEdges, 1, EdgeMaskT::value>::step(update_lambda); \
    coverageMask |= (mask << bit);

    // step in the +x direction to the next quad
#define INCX UnrollerLMask<0, NumEdges, 1, EdgeMaskT::value>::step(incx_lambda);

    // step in the +y direction to the next quad
#define INCY UnrollerLMask<0, NumEdges, 1, EdgeMaskT::value>::step(incy_lambda);

    // step in the -x direction to the next quad
#define DECX UnrollerLMask<0, NumEdges, 1, EdgeMaskT::value>::step(decx_lambda);

    // sweep 2x2 quad back and forth through the raster tile,
    // computing coverage masks for the entire tile

    // raster tile
    // 0  1  2  3  4  5  6  7
    // x  x
    // x  x ------------------>
    //                   x  x  |
    // <-----------------x  x  V
    // ..

    // row 0
    EVAL;
    UPDATE_MASK(0);
    INCX;
    EVAL;
    UPDATE_MASK(4);
    INCX;
    EVAL;
    UPDATE_MASK(8);
    INCX;
    EVAL;
    UPDATE_MASK(12);
    INCY;

    // row 1
    EVAL;
    UPDATE_MASK(28);
    DECX;
    EVAL;
    UPDATE_MASK(24);
    DECX;
    EVAL;
    UPDATE_MASK(20);
    DECX;
    EVAL;
    UPDATE_MASK(16);
    INCY;

    // row 2
    EVAL;
    UPDATE_MASK(32);
    INCX;
    EVAL;
    UPDATE_MASK(36);
    INCX;
    EVAL;
    UPDATE_MASK(40);
    INCX;
    EVAL;
    UPDATE_MASK(44);
    INCY;

    // row 3
    EVAL;
    UPDATE_MASK(60);
    DECX;
    EVAL;
    UPDATE_MASK(56);
    DECX;
    EVAL;
    UPDATE_MASK(52);
    DECX;
    EVAL;
    UPDATE_MASK(48);
#else
    uint32_t bit = 0;
    for (uint32_t y = 0; y < KNOB_TILE_Y_DIM / 2; ++y)
    {
        __m256d vStartOfRowEdge[NumEdges];
        for (uint32_t e = 0; e < NumEdges; ++e)
        {
            vStartOfRowEdge[e] = vEdges[e];
        }

        for (uint32_t x = 0; x < KNOB_TILE_X_DIM / 2; ++x)
        {
            int edgeMask[NumEdges];
            for (uint32_t e = 0; e < NumEdges; ++e)
            {
                edgeMask[e] = _mm256_movemask_pd(vEdges[e]);
            }

            uint64_t mask = edgeMask[0];
            for (uint32_t e = 1; e < NumEdges; ++e)
            {
                mask &= edgeMask[e];
            }
            coverageMask |= (mask << bit);

            // step to the next pixel in the x
            for (uint32_t e = 0; e < NumEdges; ++e)
            {
                vEdges[e] = _mm256_add_pd(vEdges[e], vStepX[e]);
            }
            bit += 4;
        }

        // step to the next row
        for (uint32_t e = 0; e < NumEdges; ++e)
        {
            vEdges[e] = _mm256_add_pd(vStartOfRowEdge[e], vStepY[e]);
        }
    }
#endif
    return coverageMask;
}
// Top left rule:
// Top: if an edge is horizontal, and it is above other edges in tri pixel space, it is a 'top' edge
// Left: if an edge is not horizontal, and it is on the left side of the triangle in pixel space, it
// is a 'left' edge Top left: a sample is in if it is a top or left edge. Out: !(horizontal &&
// above) = !horizontal && below Out: !horizontal && left = !(!horizontal && left) = horizontal and
// right
INLINE void adjustTopLeftRuleIntFix16(const __m128i vA, const __m128i vB, __m256d& vEdge)
{
    // if vA < 0, vC--
    // if vA == 0 && vB < 0, vC--

    __m256d vEdgeOut    = vEdge;
    __m256d vEdgeAdjust = _mm256_sub_pd(vEdge, _mm256_set1_pd(1.0));

    // if vA < 0 (line is not horizontal and below)
    int msk = _mm_movemask_ps(_mm_castsi128_ps(vA));

    // if vA == 0 && vB < 0 (line is horizontal and we're on the left edge of a tri)
    __m128i vCmp = _mm_cmpeq_epi32(vA, _mm_setzero_si128());
    int     msk2 = _mm_movemask_ps(_mm_castsi128_ps(vCmp));
    msk2 &= _mm_movemask_ps(_mm_castsi128_ps(vB));

    // if either of these are true and we're on the line (edge == 0), bump it outside the line
    vEdge = _mm256_blendv_pd(vEdgeOut, vEdgeAdjust, gMaskToVecpd[msk | msk2]);
}

//////////////////////////////////////////////////////////////////////////
/// @brief calculates difference in precision between the result of manh
/// calculation and the edge precision, based on compile time trait values
template <typename RT>
constexpr int64_t ManhToEdgePrecisionAdjust()
{
    static_assert(RT::PrecisionT::BitsT::value + RT::ConservativePrecisionT::BitsT::value >=
                      RT::EdgePrecisionT::BitsT::value,
                  "Inadequate precision of result of manh calculation ");
    return ((RT::PrecisionT::BitsT::value + RT::ConservativePrecisionT::BitsT::value) -
            RT::EdgePrecisionT::BitsT::value);
}

//////////////////////////////////////////////////////////////////////////
/// @struct adjustEdgeConservative
/// @brief Primary template definition used for partially specializing
/// the adjustEdgeConservative function. This struct should never
/// be instantiated.
/// @tparam RT: rasterizer traits
/// @tparam ConservativeEdgeOffsetT: does the edge need offsetting?
template <typename RT, typename ConservativeEdgeOffsetT>
struct adjustEdgeConservative
{
    //////////////////////////////////////////////////////////////////////////
    /// @brief Performs calculations to adjust each edge of a triangle away
    /// from the pixel center by 1/2 pixel + uncertainty region in both the x and y
    /// direction.
    ///
    /// Uncertainty regions arise from fixed point rounding, which
    /// can snap a vertex +/- by min fixed point value.
    /// Adding 1/2 pixel in x/y bumps the edge equation tests out towards the pixel corners.
    /// This allows the rasterizer to test for coverage only at the pixel center,
    /// instead of having to test individual pixel corners for conservative coverage
    INLINE adjustEdgeConservative(const __m128i& vAi, const __m128i& vBi, __m256d& vEdge)
    {
        // Assumes CCW winding order. Subtracting from the evaluated edge equation moves the edge
        // away from the pixel center (in the direction of the edge normal A/B)

        // edge = Ax + Bx + C - (manh/e)
        // manh = manhattan distance = abs(A) + abs(B)
        // e = absolute rounding error from snapping from float to fixed point precision

        // 'fixed point' multiply (in double to be avx1 friendly)
        // need doubles to hold result of a fixed multiply: 16.8 * 16.9 = 32.17, for example
        __m256d vAai = _mm256_cvtepi32_pd(_mm_abs_epi32(vAi)),
                vBai = _mm256_cvtepi32_pd(_mm_abs_epi32(vBi));
        __m256d manh =
            _mm256_add_pd(_mm256_mul_pd(vAai, _mm256_set1_pd(ConservativeEdgeOffsetT::value)),
                          _mm256_mul_pd(vBai, _mm256_set1_pd(ConservativeEdgeOffsetT::value)));

        static_assert(RT::PrecisionT::BitsT::value + RT::ConservativePrecisionT::BitsT::value >=
                          RT::EdgePrecisionT::BitsT::value,
                      "Inadequate precision of result of manh calculation ");

        // rasterizer incoming edge precision is x.16, so we need to get our edge offset into the
        // same precision since we're doing fixed math in double format, multiply by multiples of
        // 1/2 instead of a bit shift right
        manh = _mm256_mul_pd(manh, _mm256_set1_pd(ManhToEdgePrecisionAdjust<RT>() * 0.5));

        // move the edge away from the pixel center by the required conservative precision + 1/2
        // pixel this allows the rasterizer to do a single conservative coverage test to see if the
        // primitive intersects the pixel at all
        vEdge = _mm256_sub_pd(vEdge, manh);
    };
};

//////////////////////////////////////////////////////////////////////////
/// @brief adjustEdgeConservative specialization where no edge offset is needed
template <typename RT>
struct adjustEdgeConservative<RT, std::integral_constant<int32_t, 0>>
{
    INLINE adjustEdgeConservative(const __m128i& vAi, const __m128i& vBi, __m256d& vEdge){};
};

//////////////////////////////////////////////////////////////////////////
/// @brief calculates the distance a degenerate BBox needs to be adjusted
/// for conservative rast based on compile time trait values
template <typename RT>
constexpr int64_t ConservativeScissorOffset()
{
    static_assert(RT::ConservativePrecisionT::BitsT::value - RT::PrecisionT::BitsT::value >= 0,
                  "Rasterizer precision > conservative precision");
    // if we have a degenerate triangle, we need to compensate for adjusting the degenerate BBox
    // when calculating scissor edges
    typedef std::integral_constant<int32_t, (RT::ValidEdgeMaskT::value == ALL_EDGES_VALID) ? 0 : 1>
        DegenerateEdgeOffsetT;
    // 1/2 pixel edge offset + conservative offset - degenerateTriangle
    return RT::ConservativeEdgeOffsetT::value -
           (DegenerateEdgeOffsetT::value
            << (RT::ConservativePrecisionT::BitsT::value - RT::PrecisionT::BitsT::value));
}

//////////////////////////////////////////////////////////////////////////
/// @brief Performs calculations to adjust each a vector of evaluated edges out
/// from the pixel center by 1/2 pixel + uncertainty region in both the x and y
/// direction.
template <typename RT>
INLINE void adjustScissorEdge(const double a, const double b, __m256d& vEdge)
{
    int64_t aabs = std::abs(static_cast<int64_t>(a)), babs = std::abs(static_cast<int64_t>(b));
    int64_t manh =
        ((aabs * ConservativeScissorOffset<RT>()) + (babs * ConservativeScissorOffset<RT>())) >>
        ManhToEdgePrecisionAdjust<RT>();
    vEdge = _mm256_sub_pd(vEdge, _mm256_set1_pd(manh));
};

//////////////////////////////////////////////////////////////////////////
/// @brief Performs calculations to adjust each a scalar evaluated edge out
/// from the pixel center by 1/2 pixel + uncertainty region in both the x and y
/// direction.
template <typename RT, typename OffsetT>
INLINE double adjustScalarEdge(const double a, const double b, const double Edge)
{
    int64_t aabs = std::abs(static_cast<int64_t>(a)), babs = std::abs(static_cast<int64_t>(b));
    int64_t manh =
        ((aabs * OffsetT::value) + (babs * OffsetT::value)) >> ManhToEdgePrecisionAdjust<RT>();
    return (Edge - manh);
};

//////////////////////////////////////////////////////////////////////////
/// @brief Perform any needed adjustments to evaluated triangle edges
template <typename RT, typename EdgeOffsetT>
struct adjustEdgesFix16
{
    INLINE adjustEdgesFix16(const __m128i& vAi, const __m128i& vBi, __m256d& vEdge)
    {
        static_assert(
            std::is_same<typename RT::EdgePrecisionT, FixedPointTraits<Fixed_X_16>>::value,
            "Edge equation expected to be in x.16 fixed point");

        static_assert(RT::IsConservativeT::value,
                      "Edge offset assumes conservative rasterization is enabled");

        // need to apply any edge offsets before applying the top-left rule
        adjustEdgeConservative<RT, EdgeOffsetT>(vAi, vBi, vEdge);

        adjustTopLeftRuleIntFix16(vAi, vBi, vEdge);
    }
};

//////////////////////////////////////////////////////////////////////////
/// @brief Perform top left adjustments to evaluated triangle edges
template <typename RT>
struct adjustEdgesFix16<RT, std::integral_constant<int32_t, 0>>
{
    INLINE adjustEdgesFix16(const __m128i& vAi, const __m128i& vBi, __m256d& vEdge)
    {
        adjustTopLeftRuleIntFix16(vAi, vBi, vEdge);
    }
};

// max(abs(dz/dx), abs(dz,dy)
INLINE float ComputeMaxDepthSlope(const SWR_TRIANGLE_DESC* pDesc)
{
    /*
    // evaluate i,j at (0,0)
    float i00 = pDesc->I[0] * 0.0f + pDesc->I[1] * 0.0f + pDesc->I[2];
    float j00 = pDesc->J[0] * 0.0f + pDesc->J[1] * 0.0f + pDesc->J[2];

    // evaluate i,j at (1,0)
    float i10 = pDesc->I[0] * 1.0f + pDesc->I[1] * 0.0f + pDesc->I[2];
    float j10 = pDesc->J[0] * 1.0f + pDesc->J[1] * 0.0f + pDesc->J[2];

    // compute dz/dx
    float d00 = pDesc->Z[0] * i00 + pDesc->Z[1] * j00 + pDesc->Z[2];
    float d10 = pDesc->Z[0] * i10 + pDesc->Z[1] * j10 + pDesc->Z[2];
    float dzdx = abs(d10 - d00);

    // evaluate i,j at (0,1)
    float i01 = pDesc->I[0] * 0.0f + pDesc->I[1] * 1.0f + pDesc->I[2];
    float j01 = pDesc->J[0] * 0.0f + pDesc->J[1] * 1.0f + pDesc->J[2];

    float d01 = pDesc->Z[0] * i01 + pDesc->Z[1] * j01 + pDesc->Z[2];
    float dzdy = abs(d01 - d00);
    */

    // optimized version of above
    float dzdx = fabsf(pDesc->recipDet * (pDesc->Z[0] * pDesc->I[0] + pDesc->Z[1] * pDesc->J[0]));
    float dzdy = fabsf(pDesc->recipDet * (pDesc->Z[0] * pDesc->I[1] + pDesc->Z[1] * pDesc->J[1]));

    return std::max(dzdx, dzdy);
}

INLINE float
ComputeBiasFactor(const SWR_RASTSTATE* pState, const SWR_TRIANGLE_DESC* pDesc, const float* z)
{
    if (pState->depthFormat == R24_UNORM_X8_TYPELESS)
    {
        return (1.0f / (1 << 24));
    }
    else if (pState->depthFormat == R16_UNORM)
    {
        return (1.0f / (1 << 16));
    }
    else
    {
        SWR_ASSERT(pState->depthFormat == R32_FLOAT);

        // for f32 depth, factor = 2^(exponent(max(abs(z) - 23)
        float    zMax    = std::max(fabsf(z[0]), std::max(fabsf(z[1]), fabsf(z[2])));
        uint32_t zMaxInt = *(uint32_t*)&zMax;
        zMaxInt &= 0x7f800000;
        zMax = *(float*)&zMaxInt;

        return zMax * (1.0f / (1 << 23));
    }
}

INLINE float
ComputeDepthBias(const SWR_RASTSTATE* pState, const SWR_TRIANGLE_DESC* pTri, const float* z)
{
    if (pState->depthBias == 0 && pState->slopeScaledDepthBias == 0)
    {
        return 0.0f;
    }

    float scale = pState->slopeScaledDepthBias;
    if (scale != 0.0f)
    {
        scale *= ComputeMaxDepthSlope(pTri);
    }

    float bias = pState->depthBias;
    if (!pState->depthBiasPreAdjusted)
    {
        bias *= ComputeBiasFactor(pState, pTri, z);
    }
    bias += scale;

    if (pState->depthBiasClamp > 0.0f)
    {
        bias = std::min(bias, pState->depthBiasClamp);
    }
    else if (pState->depthBiasClamp < 0.0f)
    {
        bias = std::max(bias, pState->depthBiasClamp);
    }

    return bias;
}

// Prevent DCE by writing coverage mask from rasterizer to volatile
#if KNOB_ENABLE_TOSS_POINTS
__declspec(thread) volatile uint64_t gToss;
#endif

static const uint32_t vertsPerTri = 3, componentsPerAttrib = 4;
// try to avoid _chkstk insertions; make this thread local
static THREAD
OSALIGNLINE(float) perspAttribsTLS[vertsPerTri * SWR_VTX_NUM_SLOTS * componentsPerAttrib];

INLINE
void ComputeEdgeData(int32_t a, int32_t b, EDGE& edge)
{
    edge.a = a;
    edge.b = b;

    // compute constant steps to adjacent quads
    edge.stepQuadX = (double)((int64_t)a * (int64_t)(2 * FIXED_POINT_SCALE));
    edge.stepQuadY = (double)((int64_t)b * (int64_t)(2 * FIXED_POINT_SCALE));

    // compute constant steps to adjacent raster tiles
    edge.stepRasterTileX = (double)((int64_t)a * (int64_t)(KNOB_TILE_X_DIM * FIXED_POINT_SCALE));
    edge.stepRasterTileY = (double)((int64_t)b * (int64_t)(KNOB_TILE_Y_DIM * FIXED_POINT_SCALE));

    // compute quad offsets
    const __m256d vQuadOffsetsXIntFix8 = _mm256_set_pd(FIXED_POINT_SCALE, 0, FIXED_POINT_SCALE, 0);
    const __m256d vQuadOffsetsYIntFix8 = _mm256_set_pd(FIXED_POINT_SCALE, FIXED_POINT_SCALE, 0, 0);

    __m256d vQuadStepXFix16 = _mm256_mul_pd(_mm256_set1_pd(edge.a), vQuadOffsetsXIntFix8);
    __m256d vQuadStepYFix16 = _mm256_mul_pd(_mm256_set1_pd(edge.b), vQuadOffsetsYIntFix8);
    edge.vQuadOffsets       = _mm256_add_pd(vQuadStepXFix16, vQuadStepYFix16);

    // compute raster tile offsets
    const __m256d vTileOffsetsXIntFix8 = _mm256_set_pd(
        (KNOB_TILE_X_DIM - 1) * FIXED_POINT_SCALE, 0, (KNOB_TILE_X_DIM - 1) * FIXED_POINT_SCALE, 0);
    const __m256d vTileOffsetsYIntFix8 = _mm256_set_pd(
        (KNOB_TILE_Y_DIM - 1) * FIXED_POINT_SCALE, (KNOB_TILE_Y_DIM - 1) * FIXED_POINT_SCALE, 0, 0);

    __m256d vTileStepXFix16 = _mm256_mul_pd(_mm256_set1_pd(edge.a), vTileOffsetsXIntFix8);
    __m256d vTileStepYFix16 = _mm256_mul_pd(_mm256_set1_pd(edge.b), vTileOffsetsYIntFix8);
    edge.vRasterTileOffsets = _mm256_add_pd(vTileStepXFix16, vTileStepYFix16);
}

INLINE
void ComputeEdgeData(const POS& p0, const POS& p1, EDGE& edge)
{
    ComputeEdgeData(p0.y - p1.y, p1.x - p0.x, edge);
}

//////////////////////////////////////////////////////////////////////////
/// @brief Primary template definition used for partially specializing
/// the UpdateEdgeMasks function. Offset evaluated edges from UL pixel
/// corner to sample position, and test for coverage
/// @tparam sampleCount: multisample count
template <typename NumSamplesT>
INLINE void UpdateEdgeMasks(const __m256d (&vEdgeTileBbox)[3],
                            const __m256d* vEdgeFix16,
                            int32_t&       mask0,
                            int32_t&       mask1,
                            int32_t&       mask2)
{
    __m256d vSampleBboxTest0, vSampleBboxTest1, vSampleBboxTest2;
    // evaluate edge equations at the tile multisample bounding box
    vSampleBboxTest0 = _mm256_add_pd(vEdgeTileBbox[0], vEdgeFix16[0]);
    vSampleBboxTest1 = _mm256_add_pd(vEdgeTileBbox[1], vEdgeFix16[1]);
    vSampleBboxTest2 = _mm256_add_pd(vEdgeTileBbox[2], vEdgeFix16[2]);
    mask0            = _mm256_movemask_pd(vSampleBboxTest0);
    mask1            = _mm256_movemask_pd(vSampleBboxTest1);
    mask2            = _mm256_movemask_pd(vSampleBboxTest2);
}

//////////////////////////////////////////////////////////////////////////
/// @brief UpdateEdgeMasks<SingleSampleT> specialization, instantiated
/// when only rasterizing a single coverage test point
template <>
INLINE void UpdateEdgeMasks<SingleSampleT>(
    const __m256d (&)[3], const __m256d* vEdgeFix16, int32_t& mask0, int32_t& mask1, int32_t& mask2)
{
    mask0 = _mm256_movemask_pd(vEdgeFix16[0]);
    mask1 = _mm256_movemask_pd(vEdgeFix16[1]);
    mask2 = _mm256_movemask_pd(vEdgeFix16[2]);
}

//////////////////////////////////////////////////////////////////////////
/// @struct ComputeScissorEdges
/// @brief Primary template definition. Allows the function to be generically
/// called. When paired with below specializations, will result in an empty
/// inlined function if scissor is not enabled
/// @tparam RasterScissorEdgesT: is scissor enabled?
/// @tparam IsConservativeT: is conservative rast enabled?
/// @tparam RT: rasterizer traits
template <typename RasterScissorEdgesT, typename IsConservativeT, typename RT>
struct ComputeScissorEdges
{
    INLINE ComputeScissorEdges(const SWR_RECT& triBBox,
                               const SWR_RECT& scissorBBox,
                               const int32_t   x,
                               const int32_t   y,
                               EDGE (&rastEdges)[RT::NumEdgesT::value],
                               __m256d (&vEdgeFix16)[7]){};
};

//////////////////////////////////////////////////////////////////////////
/// @brief ComputeScissorEdges<std::true_type, std::true_type, RT> partial
/// specialization. Instantiated when conservative rast and scissor are enabled
template <typename RT>
struct ComputeScissorEdges<std::true_type, std::true_type, RT>
{
    //////////////////////////////////////////////////////////////////////////
    /// @brief Intersect tri bbox with scissor, compute scissor edge vectors,
    /// evaluate edge equations and offset them away from pixel center.
    INLINE ComputeScissorEdges(const SWR_RECT& triBBox,
                               const SWR_RECT& scissorBBox,
                               const int32_t   x,
                               const int32_t   y,
                               EDGE (&rastEdges)[RT::NumEdgesT::value],
                               __m256d (&vEdgeFix16)[7])
    {
        // if conservative rasterizing, triangle bbox intersected with scissor bbox is used
        SWR_RECT scissor;
        scissor.xmin = std::max(triBBox.xmin, scissorBBox.xmin);
        scissor.xmax = std::min(triBBox.xmax, scissorBBox.xmax);
        scissor.ymin = std::max(triBBox.ymin, scissorBBox.ymin);
        scissor.ymax = std::min(triBBox.ymax, scissorBBox.ymax);

        POS topLeft{scissor.xmin, scissor.ymin};
        POS bottomLeft{scissor.xmin, scissor.ymax};
        POS topRight{scissor.xmax, scissor.ymin};
        POS bottomRight{scissor.xmax, scissor.ymax};

        // construct 4 scissor edges in ccw direction
        ComputeEdgeData(topLeft, bottomLeft, rastEdges[3]);
        ComputeEdgeData(bottomLeft, bottomRight, rastEdges[4]);
        ComputeEdgeData(bottomRight, topRight, rastEdges[5]);
        ComputeEdgeData(topRight, topLeft, rastEdges[6]);

        vEdgeFix16[3] = _mm256_set1_pd((rastEdges[3].a * (x - scissor.xmin)) +
                                       (rastEdges[3].b * (y - scissor.ymin)));
        vEdgeFix16[4] = _mm256_set1_pd((rastEdges[4].a * (x - scissor.xmin)) +
                                       (rastEdges[4].b * (y - scissor.ymax)));
        vEdgeFix16[5] = _mm256_set1_pd((rastEdges[5].a * (x - scissor.xmax)) +
                                       (rastEdges[5].b * (y - scissor.ymax)));
        vEdgeFix16[6] = _mm256_set1_pd((rastEdges[6].a * (x - scissor.xmax)) +
                                       (rastEdges[6].b * (y - scissor.ymin)));

        // if conservative rasterizing, need to bump the scissor edges out by the conservative
        // uncertainty distance, else do nothing
        adjustScissorEdge<RT>(rastEdges[3].a, rastEdges[3].b, vEdgeFix16[3]);
        adjustScissorEdge<RT>(rastEdges[4].a, rastEdges[4].b, vEdgeFix16[4]);
        adjustScissorEdge<RT>(rastEdges[5].a, rastEdges[5].b, vEdgeFix16[5]);
        adjustScissorEdge<RT>(rastEdges[6].a, rastEdges[6].b, vEdgeFix16[6]);

        // Upper left rule for scissor
        vEdgeFix16[3] = _mm256_sub_pd(vEdgeFix16[3], _mm256_set1_pd(1.0));
        vEdgeFix16[6] = _mm256_sub_pd(vEdgeFix16[6], _mm256_set1_pd(1.0));
    }
};

//////////////////////////////////////////////////////////////////////////
/// @brief ComputeScissorEdges<std::true_type, std::false_type, RT> partial
/// specialization. Instantiated when scissor is enabled and conservative rast
/// is disabled.
template <typename RT>
struct ComputeScissorEdges<std::true_type, std::false_type, RT>
{
    //////////////////////////////////////////////////////////////////////////
    /// @brief Compute scissor edge vectors and evaluate edge equations
    INLINE ComputeScissorEdges(const SWR_RECT&,
                               const SWR_RECT& scissorBBox,
                               const int32_t   x,
                               const int32_t   y,
                               EDGE (&rastEdges)[RT::NumEdgesT::value],
                               __m256d (&vEdgeFix16)[7])
    {
        const SWR_RECT& scissor = scissorBBox;
        POS             topLeft{scissor.xmin, scissor.ymin};
        POS             bottomLeft{scissor.xmin, scissor.ymax};
        POS             topRight{scissor.xmax, scissor.ymin};
        POS             bottomRight{scissor.xmax, scissor.ymax};

        // construct 4 scissor edges in ccw direction
        ComputeEdgeData(topLeft, bottomLeft, rastEdges[3]);
        ComputeEdgeData(bottomLeft, bottomRight, rastEdges[4]);
        ComputeEdgeData(bottomRight, topRight, rastEdges[5]);
        ComputeEdgeData(topRight, topLeft, rastEdges[6]);

        vEdgeFix16[3] = _mm256_set1_pd((rastEdges[3].a * (x - scissor.xmin)) +
                                       (rastEdges[3].b * (y - scissor.ymin)));
        vEdgeFix16[4] = _mm256_set1_pd((rastEdges[4].a * (x - scissor.xmin)) +
                                       (rastEdges[4].b * (y - scissor.ymax)));
        vEdgeFix16[5] = _mm256_set1_pd((rastEdges[5].a * (x - scissor.xmax)) +
                                       (rastEdges[5].b * (y - scissor.ymax)));
        vEdgeFix16[6] = _mm256_set1_pd((rastEdges[6].a * (x - scissor.xmax)) +
                                       (rastEdges[6].b * (y - scissor.ymin)));

        // Upper left rule for scissor
        vEdgeFix16[3] = _mm256_sub_pd(vEdgeFix16[3], _mm256_set1_pd(1.0));
        vEdgeFix16[6] = _mm256_sub_pd(vEdgeFix16[6], _mm256_set1_pd(1.0));
    }
};

//////////////////////////////////////////////////////////////////////////
/// @brief Primary function template for TrivialRejectTest. Should
/// never be called, but TemplateUnroller instantiates a few unused values,
/// so it calls a runtime assert instead of a static_assert.
template <typename ValidEdgeMaskT>
INLINE bool TrivialRejectTest(const int, const int, const int)
{
    SWR_INVALID("Primary templated function should never be called");
    return false;
};

//////////////////////////////////////////////////////////////////////////
/// @brief E0E1ValidT specialization of TrivialRejectTest. Tests edge 0
/// and edge 1 for trivial coverage reject
template <>
INLINE bool TrivialRejectTest<E0E1ValidT>(const int mask0, const int mask1, const int)
{
    return (!(mask0 && mask1)) ? true : false;
};

//////////////////////////////////////////////////////////////////////////
/// @brief E0E2ValidT specialization of TrivialRejectTest. Tests edge 0
/// and edge 2 for trivial coverage reject
template <>
INLINE bool TrivialRejectTest<E0E2ValidT>(const int mask0, const int, const int mask2)
{
    return (!(mask0 && mask2)) ? true : false;
};

//////////////////////////////////////////////////////////////////////////
/// @brief E1E2ValidT specialization of TrivialRejectTest. Tests edge 1
/// and edge 2 for trivial coverage reject
template <>
INLINE bool TrivialRejectTest<E1E2ValidT>(const int, const int mask1, const int mask2)
{
    return (!(mask1 && mask2)) ? true : false;
};

//////////////////////////////////////////////////////////////////////////
/// @brief AllEdgesValidT specialization of TrivialRejectTest. Tests all
/// primitive edges for trivial coverage reject
template <>
INLINE bool TrivialRejectTest<AllEdgesValidT>(const int mask0, const int mask1, const int mask2)
{
    return (!(mask0 && mask1 && mask2)) ? true : false;
    ;
};

//////////////////////////////////////////////////////////////////////////
/// @brief NoEdgesValidT specialization of TrivialRejectTest. Degenerate
/// point, so return false and rasterize against conservative BBox
template <>
INLINE bool TrivialRejectTest<NoEdgesValidT>(const int, const int, const int)
{
    return false;
};

//////////////////////////////////////////////////////////////////////////
/// @brief Primary function template for TrivialAcceptTest. Always returns
/// false, since it will only be called for degenerate tris, and as such
/// will never cover the entire raster tile
template <typename ScissorEnableT>
INLINE bool TrivialAcceptTest(const int, const int, const int)
{
    return false;
};

//////////////////////////////////////////////////////////////////////////
/// @brief AllEdgesValidT specialization for TrivialAcceptTest. Test all
/// edge masks for a fully covered raster tile
template <>
INLINE bool TrivialAcceptTest<std::false_type>(const int mask0, const int mask1, const int mask2)
{
    return ((mask0 & mask1 & mask2) == 0xf);
};

//////////////////////////////////////////////////////////////////////////
/// @brief Primary function template for GenerateSVInnerCoverage. Results
/// in an empty function call if SVInnerCoverage isn't requested
template <typename RT, typename ValidEdgeMaskT, typename InputCoverageT>
struct GenerateSVInnerCoverage
{
    INLINE GenerateSVInnerCoverage(DRAW_CONTEXT*, uint32_t, EDGE*, double*, uint64_t&){};
};

//////////////////////////////////////////////////////////////////////////
/// @brief Specialization of GenerateSVInnerCoverage where all edges
/// are non-degenerate and SVInnerCoverage is requested. Offsets the evaluated
/// edge values from OuterConservative to InnerConservative and rasterizes.
template <typename RT>
struct GenerateSVInnerCoverage<RT, AllEdgesValidT, InnerConservativeCoverageT>
{
    INLINE GenerateSVInnerCoverage(DRAW_CONTEXT* pDC,
                                   uint32_t      workerId,
                                   EDGE*         pRastEdges,
                                   double*       pStartQuadEdges,
                                   uint64_t&     innerCoverageMask)
    {
        double startQuadEdgesAdj[RT::NumEdgesT::value];
        for (uint32_t e = 0; e < RT::NumEdgesT::value; ++e)
        {
            startQuadEdgesAdj[e] = adjustScalarEdge<RT, typename RT::InnerConservativeEdgeOffsetT>(
                pRastEdges[e].a, pRastEdges[e].b, pStartQuadEdges[e]);
        }

        // not trivial accept or reject, must rasterize full tile
        RDTSC_BEGIN(pDC->pContext->pBucketMgr, BERasterizePartial, pDC->drawId);
        innerCoverageMask = rasterizePartialTile<RT::NumEdgesT::value, typename RT::ValidEdgeMaskT>(
            pDC, startQuadEdgesAdj, pRastEdges);
        RDTSC_END(pDC->pContext->pBucketMgr, BERasterizePartial, 0);
    }
};

//////////////////////////////////////////////////////////////////////////
/// @brief Primary function template for UpdateEdgeMasksInnerConservative. Results
/// in an empty function call if SVInnerCoverage isn't requested
template <typename RT, typename ValidEdgeMaskT, typename InputCoverageT>
struct UpdateEdgeMasksInnerConservative
{
    INLINE UpdateEdgeMasksInnerConservative(const __m256d (&vEdgeTileBbox)[3],
                                            const __m256d*,
                                            const __m128i,
                                            const __m128i,
                                            int32_t&,
                                            int32_t&,
                                            int32_t&){};
};

//////////////////////////////////////////////////////////////////////////
/// @brief Specialization of UpdateEdgeMasksInnerConservative where all edges
/// are non-degenerate and SVInnerCoverage is requested. Offsets the edges
/// evaluated at raster tile corners to inner conservative position and
/// updates edge masks
template <typename RT>
struct UpdateEdgeMasksInnerConservative<RT, AllEdgesValidT, InnerConservativeCoverageT>
{
    INLINE UpdateEdgeMasksInnerConservative(const __m256d (&vEdgeTileBbox)[3],
                                            const __m256d* vEdgeFix16,
                                            const __m128i  vAi,
                                            const __m128i  vBi,
                                            int32_t&       mask0,
                                            int32_t&       mask1,
                                            int32_t&       mask2)
    {
        __m256d vTempEdge[3]{vEdgeFix16[0], vEdgeFix16[1], vEdgeFix16[2]};

        // instead of keeping 2 copies of evaluated edges around, just compensate for the outer
        // conservative evaluated edge when adjusting the edge in for inner conservative tests
        adjustEdgeConservative<RT, typename RT::InnerConservativeEdgeOffsetT>(
            vAi, vBi, vTempEdge[0]);
        adjustEdgeConservative<RT, typename RT::InnerConservativeEdgeOffsetT>(
            vAi, vBi, vTempEdge[1]);
        adjustEdgeConservative<RT, typename RT::InnerConservativeEdgeOffsetT>(
            vAi, vBi, vTempEdge[2]);

        UpdateEdgeMasks<typename RT::NumCoverageSamplesT>(
            vEdgeTileBbox, vTempEdge, mask0, mask1, mask2);
    }
};

//////////////////////////////////////////////////////////////////////////
/// @brief Specialization of UpdateEdgeMasksInnerConservative where SVInnerCoverage
/// is requested but at least one edge is degenerate. Since a degenerate triangle cannot
/// cover an entire raster tile, set mask0 to 0 to force it down the
/// rastierizePartialTile path
template <typename RT, typename ValidEdgeMaskT>
struct UpdateEdgeMasksInnerConservative<RT, ValidEdgeMaskT, InnerConservativeCoverageT>
{
    INLINE UpdateEdgeMasksInnerConservative(const __m256d (&)[3],
                                            const __m256d*,
                                            const __m128i,
                                            const __m128i,
                                            int32_t& mask0,
                                            int32_t&,
                                            int32_t&)
    {
        // set one mask to zero to force the triangle down the rastierizePartialTile path
        mask0 = 0;
    }
};

template <typename RT>
void RasterizeTriangle(DRAW_CONTEXT* pDC, uint32_t workerId, uint32_t macroTile, void* pDesc)
{
    const TRIANGLE_WORK_DESC& workDesc = *((TRIANGLE_WORK_DESC*)pDesc);
#if KNOB_ENABLE_TOSS_POINTS
    if (KNOB_TOSS_BIN_TRIS)
    {
        return;
    }
#endif
    RDTSC_BEGIN(pDC->pContext->pBucketMgr, BERasterizeTriangle, pDC->drawId);
    RDTSC_BEGIN(pDC->pContext->pBucketMgr, BETriangleSetup, pDC->drawId);

    const API_STATE&     state        = GetApiState(pDC);
    const SWR_RASTSTATE& rastState    = state.rastState;
    const BACKEND_FUNCS& backendFuncs = pDC->pState->backendFuncs;

    OSALIGNSIMD(SWR_TRIANGLE_DESC) triDesc;
    triDesc.pUserClipBuffer = workDesc.pUserClipBuffer;

    __m128 vX, vY, vZ, vRecipW;

    // pTriBuffer data layout: grouped components of the 3 triangle points and 1 don't care
    // eg: vX = [x0 x1 x2 dc]
    vX      = _mm_load_ps(workDesc.pTriBuffer);
    vY      = _mm_load_ps(workDesc.pTriBuffer + 4);
    vZ      = _mm_load_ps(workDesc.pTriBuffer + 8);
    vRecipW = _mm_load_ps(workDesc.pTriBuffer + 12);

    // convert to fixed point
    static_assert(std::is_same<typename RT::PrecisionT, FixedPointTraits<Fixed_16_8>>::value,
                  "Rasterizer expects 16.8 fixed point precision");
    __m128i vXi = fpToFixedPoint(vX);
    __m128i vYi = fpToFixedPoint(vY);

    // quantize floating point position to fixed point precision
    // to prevent attribute creep around the triangle vertices
    vX = _mm_mul_ps(_mm_cvtepi32_ps(vXi), _mm_set1_ps(1.0f / FIXED_POINT_SCALE));
    vY = _mm_mul_ps(_mm_cvtepi32_ps(vYi), _mm_set1_ps(1.0f / FIXED_POINT_SCALE));

    // triangle setup - A and B edge equation coefs
    __m128 vA, vB;
    triangleSetupAB(vX, vY, vA, vB);

    __m128i vAi, vBi;
    triangleSetupABInt(vXi, vYi, vAi, vBi);

    // determinant
    float det = calcDeterminantInt(vAi, vBi);

    // Verts in Pixel Coordinate Space at this point
    // Det > 0 = CW winding order
    // Convert CW triangles to CCW
    if (det > 0.0)
    {
        vA  = _mm_mul_ps(vA, _mm_set1_ps(-1));
        vB  = _mm_mul_ps(vB, _mm_set1_ps(-1));
        vAi = _mm_mullo_epi32(vAi, _mm_set1_epi32(-1));
        vBi = _mm_mullo_epi32(vBi, _mm_set1_epi32(-1));
        det = -det;
    }

    __m128 vC;
    // Finish triangle setup - C edge coef
    triangleSetupC(vX, vY, vA, vB, vC);

    if (RT::ValidEdgeMaskT::value != ALL_EDGES_VALID)
    {
        // If we have degenerate edge(s) to rasterize, set I and J coefs
        // to 0 for constant interpolation of attributes
        triDesc.I[0] = 0.0f;
        triDesc.I[1] = 0.0f;
        triDesc.I[2] = 0.0f;
        triDesc.J[0] = 0.0f;
        triDesc.J[1] = 0.0f;
        triDesc.J[2] = 0.0f;

        // Degenerate triangles have no area
        triDesc.recipDet = 0.0f;
    }
    else
    {
        // only extract coefs for 2 of the barycentrics; the 3rd can be
        // determined from the barycentric equation:
        // i + j + k = 1 <=> k = 1 - j - i
        _MM_EXTRACT_FLOAT(triDesc.I[0], vA, 1);
        _MM_EXTRACT_FLOAT(triDesc.I[1], vB, 1);
        _MM_EXTRACT_FLOAT(triDesc.I[2], vC, 1);
        _MM_EXTRACT_FLOAT(triDesc.J[0], vA, 2);
        _MM_EXTRACT_FLOAT(triDesc.J[1], vB, 2);
        _MM_EXTRACT_FLOAT(triDesc.J[2], vC, 2);

        // compute recipDet, used to calculate barycentric i and j in the backend
        triDesc.recipDet = 1.0f / det;
    }

    OSALIGNSIMD(float) oneOverW[4];
    _mm_store_ps(oneOverW, vRecipW);
    triDesc.OneOverW[0] = oneOverW[0] - oneOverW[2];
    triDesc.OneOverW[1] = oneOverW[1] - oneOverW[2];
    triDesc.OneOverW[2] = oneOverW[2];

    // calculate perspective correct coefs per vertex attrib
    float* pPerspAttribs  = perspAttribsTLS;
    float* pAttribs       = workDesc.pAttribs;
    triDesc.pPerspAttribs = pPerspAttribs;
    triDesc.pAttribs      = pAttribs;
    float* pRecipW        = workDesc.pTriBuffer + 12;
    triDesc.pRecipW       = pRecipW;
    __m128 vOneOverWV0    = _mm_broadcast_ss(pRecipW);
    __m128 vOneOverWV1    = _mm_broadcast_ss(pRecipW += 1);
    __m128 vOneOverWV2    = _mm_broadcast_ss(pRecipW += 1);
    for (uint32_t i = 0; i < workDesc.numAttribs; i++)
    {
        __m128 attribA = _mm_load_ps(pAttribs);
        __m128 attribB = _mm_load_ps(pAttribs += 4);
        __m128 attribC = _mm_load_ps(pAttribs += 4);
        pAttribs += 4;

        attribA = _mm_mul_ps(attribA, vOneOverWV0);
        attribB = _mm_mul_ps(attribB, vOneOverWV1);
        attribC = _mm_mul_ps(attribC, vOneOverWV2);

        _mm_store_ps(pPerspAttribs, attribA);
        _mm_store_ps(pPerspAttribs += 4, attribB);
        _mm_store_ps(pPerspAttribs += 4, attribC);
        pPerspAttribs += 4;
    }

    // compute bary Z
    // zInterp = zVert0 + i(zVert1-zVert0) + j (zVert2 - zVert0)
    OSALIGNSIMD(float) a[4];
    _mm_store_ps(a, vZ);
    triDesc.Z[0] = a[0] - a[2];
    triDesc.Z[1] = a[1] - a[2];
    triDesc.Z[2] = a[2];

    // add depth bias
    triDesc.Z[2] += ComputeDepthBias(&rastState, &triDesc, workDesc.pTriBuffer + 8);

    // Calc bounding box of triangle
    OSALIGNSIMD(SWR_RECT) bbox;
    calcBoundingBoxInt(vXi, vYi, bbox);

    const SWR_RECT& scissorInFixedPoint =
        state.scissorsInFixedPoint[workDesc.triFlags.viewportIndex];

    if (RT::ValidEdgeMaskT::value != ALL_EDGES_VALID)
    {
        // If we're rasterizing a degenerate triangle, expand bounding box to guarantee the BBox is
        // valid
        bbox.xmin--;
        bbox.xmax++;
        bbox.ymin--;
        bbox.ymax++;
        SWR_ASSERT(scissorInFixedPoint.xmin >= 0 && scissorInFixedPoint.ymin >= 0,
                   "Conservative rast degenerate handling requires a valid scissor rect");
    }

    // Intersect with scissor/viewport
    OSALIGNSIMD(SWR_RECT) intersect;
    intersect.xmin = std::max(bbox.xmin, scissorInFixedPoint.xmin);
    intersect.xmax = std::min(bbox.xmax - 1, scissorInFixedPoint.xmax);
    intersect.ymin = std::max(bbox.ymin, scissorInFixedPoint.ymin);
    intersect.ymax = std::min(bbox.ymax - 1, scissorInFixedPoint.ymax);

    triDesc.triFlags = workDesc.triFlags;

    // further constrain backend to intersecting bounding box of macro tile and scissored triangle
    // bbox
    uint32_t macroX, macroY;
    MacroTileMgr::getTileIndices(macroTile, macroX, macroY);
    int32_t macroBoxLeft   = macroX * KNOB_MACROTILE_X_DIM_FIXED;
    int32_t macroBoxRight  = macroBoxLeft + KNOB_MACROTILE_X_DIM_FIXED - 1;
    int32_t macroBoxTop    = macroY * KNOB_MACROTILE_Y_DIM_FIXED;
    int32_t macroBoxBottom = macroBoxTop + KNOB_MACROTILE_Y_DIM_FIXED - 1;

    intersect.xmin = std::max(intersect.xmin, macroBoxLeft);
    intersect.ymin = std::max(intersect.ymin, macroBoxTop);
    intersect.xmax = std::min(intersect.xmax, macroBoxRight);
    intersect.ymax = std::min(intersect.ymax, macroBoxBottom);

    SWR_ASSERT(intersect.xmin <= intersect.xmax && intersect.ymin <= intersect.ymax &&
               intersect.xmin >= 0 && intersect.xmax >= 0 && intersect.ymin >= 0 &&
               intersect.ymax >= 0);

    RDTSC_END(pDC->pContext->pBucketMgr, BETriangleSetup, 0);

    // update triangle desc
    uint32_t minTileX  = intersect.xmin >> (KNOB_TILE_X_DIM_SHIFT + FIXED_POINT_SHIFT);
    uint32_t minTileY  = intersect.ymin >> (KNOB_TILE_Y_DIM_SHIFT + FIXED_POINT_SHIFT);
    uint32_t maxTileX  = intersect.xmax >> (KNOB_TILE_X_DIM_SHIFT + FIXED_POINT_SHIFT);
    uint32_t maxTileY  = intersect.ymax >> (KNOB_TILE_Y_DIM_SHIFT + FIXED_POINT_SHIFT);
    uint32_t numTilesX = maxTileX - minTileX + 1;
    uint32_t numTilesY = maxTileY - minTileY + 1;

    if (numTilesX == 0 || numTilesY == 0)
    {
        RDTSC_EVENT(pDC->pContext->pBucketMgr, BEEmptyTriangle, 1, 0);
        RDTSC_END(pDC->pContext->pBucketMgr, BERasterizeTriangle, 1);
        return;
    }

    RDTSC_BEGIN(pDC->pContext->pBucketMgr, BEStepSetup, pDC->drawId);

    // Step to pixel center of top-left pixel of the triangle bbox
    // Align intersect bbox (top/left) to raster tile's (top/left).
    int32_t x = AlignDown(intersect.xmin, (FIXED_POINT_SCALE * KNOB_TILE_X_DIM));
    int32_t y = AlignDown(intersect.ymin, (FIXED_POINT_SCALE * KNOB_TILE_Y_DIM));

    // convenience typedef
    typedef typename RT::NumCoverageSamplesT NumCoverageSamplesT;

    // single sample rasterization evaluates edges at pixel center,
    // multisample evaluates edges UL pixel corner and steps to each sample position
    if (std::is_same<NumCoverageSamplesT, SingleSampleT>::value)
    {
        // Add 0.5, in fixed point, to offset to pixel center
        x += (FIXED_POINT_SCALE / 2);
        y += (FIXED_POINT_SCALE / 2);
    }

    __m128i vTopLeftX = _mm_set1_epi32(x);
    __m128i vTopLeftY = _mm_set1_epi32(y);

    // evaluate edge equations at top-left pixel using 64bit math
    //
    // line = Ax + By + C
    // solving for C:
    // C = -Ax - By
    // we know x0 and y0 are on the line; plug them in:
    // C = -Ax0 - By0
    // plug C back into line equation:
    // line = Ax - By - Ax0 - By0
    // line = A(x - x0) + B(y - y0)
    // dX = (x-x0), dY = (y-y0)
    // so all this simplifies to
    // edge = A(dX) + B(dY), our first test at the top left of the bbox we're rasterizing within

    __m128i vDeltaX = _mm_sub_epi32(vTopLeftX, vXi);
    __m128i vDeltaY = _mm_sub_epi32(vTopLeftY, vYi);

    // evaluate A(dx) and B(dY) for all points
    __m256d vAipd     = _mm256_cvtepi32_pd(vAi);
    __m256d vBipd     = _mm256_cvtepi32_pd(vBi);
    __m256d vDeltaXpd = _mm256_cvtepi32_pd(vDeltaX);
    __m256d vDeltaYpd = _mm256_cvtepi32_pd(vDeltaY);

    __m256d vAiDeltaXFix16 = _mm256_mul_pd(vAipd, vDeltaXpd);
    __m256d vBiDeltaYFix16 = _mm256_mul_pd(vBipd, vDeltaYpd);
    __m256d vEdge          = _mm256_add_pd(vAiDeltaXFix16, vBiDeltaYFix16);

    // apply any edge adjustments(top-left, crast, etc)
    adjustEdgesFix16<RT, typename RT::ConservativeEdgeOffsetT>(vAi, vBi, vEdge);

    // broadcast respective edge results to all lanes
    double* pEdge = (double*)&vEdge;
    __m256d vEdgeFix16[7];
    vEdgeFix16[0] = _mm256_set1_pd(pEdge[0]);
    vEdgeFix16[1] = _mm256_set1_pd(pEdge[1]);
    vEdgeFix16[2] = _mm256_set1_pd(pEdge[2]);

    OSALIGNSIMD(int32_t) aAi[4], aBi[4];
    _mm_store_si128((__m128i*)aAi, vAi);
    _mm_store_si128((__m128i*)aBi, vBi);
    EDGE rastEdges[RT::NumEdgesT::value];

    // Compute and store triangle edge data
    ComputeEdgeData(aAi[0], aBi[0], rastEdges[0]);
    ComputeEdgeData(aAi[1], aBi[1], rastEdges[1]);
    ComputeEdgeData(aAi[2], aBi[2], rastEdges[2]);

    // Compute and store triangle edge data if scissor needs to rasterized
    ComputeScissorEdges<typename RT::RasterizeScissorEdgesT, typename RT::IsConservativeT, RT>(
        bbox, scissorInFixedPoint, x, y, rastEdges, vEdgeFix16);

    // Evaluate edge equations at sample positions of each of the 4 corners of a raster tile
    // used to for testing if entire raster tile is inside a triangle
    for (uint32_t e = 0; e < RT::NumEdgesT::value; ++e)
    {
        vEdgeFix16[e] = _mm256_add_pd(vEdgeFix16[e], rastEdges[e].vRasterTileOffsets);
    }

    // at this point vEdge has been evaluated at the UL pixel corners of raster tile bbox
    // step sample positions to the raster tile bbox of multisample points
    // min(xSamples),min(ySamples)  ------  max(xSamples),min(ySamples)
    //                             |      |
    //                             |      |
    // min(xSamples),max(ySamples)  ------  max(xSamples),max(ySamples)
    __m256d vEdgeTileBbox[3];
    if (NumCoverageSamplesT::value > 1)
    {
        const SWR_MULTISAMPLE_POS& samplePos         = rastState.samplePositions;
        const __m128i              vTileSampleBBoxXh = samplePos.TileSampleOffsetsX();
        const __m128i              vTileSampleBBoxYh = samplePos.TileSampleOffsetsY();

        __m256d vTileSampleBBoxXFix8 = _mm256_cvtepi32_pd(vTileSampleBBoxXh);
        __m256d vTileSampleBBoxYFix8 = _mm256_cvtepi32_pd(vTileSampleBBoxYh);

        // step edge equation tests from Tile
        // used to for testing if entire raster tile is inside a triangle
        for (uint32_t e = 0; e < 3; ++e)
        {
            __m256d vResultAxFix16 =
                _mm256_mul_pd(_mm256_set1_pd(rastEdges[e].a), vTileSampleBBoxXFix8);
            __m256d vResultByFix16 =
                _mm256_mul_pd(_mm256_set1_pd(rastEdges[e].b), vTileSampleBBoxYFix8);
            vEdgeTileBbox[e] = _mm256_add_pd(vResultAxFix16, vResultByFix16);

            // adjust for msaa tile bbox edges outward for conservative rast, if enabled
            adjustEdgeConservative<RT, typename RT::ConservativeEdgeOffsetT>(
                vAi, vBi, vEdgeTileBbox[e]);
        }
    }

    RDTSC_END(pDC->pContext->pBucketMgr, BEStepSetup, 0);

    uint32_t tY   = minTileY;
    uint32_t tX   = minTileX;
    uint32_t maxY = maxTileY;
    uint32_t maxX = maxTileX;

    RenderOutputBuffers renderBuffers, currentRenderBufferRow;
    GetRenderHotTiles<RT::MT::numSamples>(pDC,
                                          workerId,
                                          macroTile,
                                          minTileX,
                                          minTileY,
                                          renderBuffers,
                                          triDesc.triFlags.renderTargetArrayIndex);
    currentRenderBufferRow = renderBuffers;

    // rasterize and generate coverage masks per sample
    for (uint32_t tileY = tY; tileY <= maxY; ++tileY)
    {
        __m256d vStartOfRowEdge[RT::NumEdgesT::value];
        for (uint32_t e = 0; e < RT::NumEdgesT::value; ++e)
        {
            vStartOfRowEdge[e] = vEdgeFix16[e];
        }

        for (uint32_t tileX = tX; tileX <= maxX; ++tileX)
        {
            triDesc.anyCoveredSamples = 0;

            // is the corner of the edge outside of the raster tile? (vEdge < 0)
            int mask0, mask1, mask2;
            UpdateEdgeMasks<NumCoverageSamplesT>(vEdgeTileBbox, vEdgeFix16, mask0, mask1, mask2);

            for (uint32_t sampleNum = 0; sampleNum < NumCoverageSamplesT::value; sampleNum++)
            {
                // trivial reject, at least one edge has all 4 corners of raster tile outside
                bool trivialReject =
                    TrivialRejectTest<typename RT::ValidEdgeMaskT>(mask0, mask1, mask2);

                if (!trivialReject)
                {
                    // trivial accept mask
                    triDesc.coverageMask[sampleNum] = 0xffffffffffffffffULL;

                    // Update the raster tile edge masks based on inner conservative edge offsets,
                    // if enabled
                    UpdateEdgeMasksInnerConservative<RT,
                                                     typename RT::ValidEdgeMaskT,
                                                     typename RT::InputCoverageT>(
                        vEdgeTileBbox, vEdgeFix16, vAi, vBi, mask0, mask1, mask2);

                    // @todo Make this a bit smarter to allow use of trivial accept when:
                    //   1) scissor/vp intersection rect is raster tile aligned
                    //   2) raster tile is entirely within scissor/vp intersection rect
                    if (TrivialAcceptTest<typename RT::RasterizeScissorEdgesT>(mask0, mask1, mask2))
                    {
                        // trivial accept, all 4 corners of all 3 edges are negative
                        // i.e. raster tile completely inside triangle
                        triDesc.anyCoveredSamples = triDesc.coverageMask[sampleNum];
                        if (std::is_same<typename RT::InputCoverageT,
                                         InnerConservativeCoverageT>::value)
                        {
                            triDesc.innerCoverageMask = 0xffffffffffffffffULL;
                        }
                        RDTSC_EVENT(pDC->pContext->pBucketMgr, BETrivialAccept, 1, 0);
                    }
                    else
                    {
                        __m256d vEdgeAtSample[RT::NumEdgesT::value];
                        if (std::is_same<NumCoverageSamplesT, SingleSampleT>::value)
                        {
                            // should get optimized out for single sample case (global value
                            // numbering or copy propagation)
                            for (uint32_t e = 0; e < RT::NumEdgesT::value; ++e)
                            {
                                vEdgeAtSample[e] = vEdgeFix16[e];
                            }
                        }
                        else
                        {
                            const SWR_MULTISAMPLE_POS& samplePos       = rastState.samplePositions;
                            __m128i                    vSampleOffsetXh = samplePos.vXi(sampleNum);
                            __m128i                    vSampleOffsetYh = samplePos.vYi(sampleNum);
                            __m256d vSampleOffsetX = _mm256_cvtepi32_pd(vSampleOffsetXh);
                            __m256d vSampleOffsetY = _mm256_cvtepi32_pd(vSampleOffsetYh);

                            // step edge equation tests from UL tile corner to pixel sample position
                            for (uint32_t e = 0; e < RT::NumEdgesT::value; ++e)
                            {
                                __m256d vResultAxFix16 =
                                    _mm256_mul_pd(_mm256_set1_pd(rastEdges[e].a), vSampleOffsetX);
                                __m256d vResultByFix16 =
                                    _mm256_mul_pd(_mm256_set1_pd(rastEdges[e].b), vSampleOffsetY);
                                vEdgeAtSample[e] = _mm256_add_pd(vResultAxFix16, vResultByFix16);
                                vEdgeAtSample[e] = _mm256_add_pd(vEdgeFix16[e], vEdgeAtSample[e]);
                            }
                        }

                        double        startQuadEdges[RT::NumEdgesT::value];
                        const __m256i vLane0Mask = _mm256_set_epi32(0, 0, 0, 0, 0, 0, -1, -1);
                        for (uint32_t e = 0; e < RT::NumEdgesT::value; ++e)
                        {
                            _mm256_maskstore_pd(&startQuadEdges[e], vLane0Mask, vEdgeAtSample[e]);
                        }

                        // not trivial accept or reject, must rasterize full tile
                        RDTSC_BEGIN(pDC->pContext->pBucketMgr, BERasterizePartial, pDC->drawId);
                        triDesc.coverageMask[sampleNum] =
                            rasterizePartialTile<RT::NumEdgesT::value, typename RT::ValidEdgeMaskT>(
                                pDC, startQuadEdges, rastEdges);
                        RDTSC_END(pDC->pContext->pBucketMgr, BERasterizePartial, 0);

                        triDesc.anyCoveredSamples |= triDesc.coverageMask[sampleNum];

                        // Output SV InnerCoverage, if needed
                        GenerateSVInnerCoverage<RT,
                                                typename RT::ValidEdgeMaskT,
                                                typename RT::InputCoverageT>(
                            pDC, workerId, rastEdges, startQuadEdges, triDesc.innerCoverageMask);
                    }
                }
                else
                {
                    // if we're calculating coverage per sample, need to store it off. otherwise no
                    // covered samples, don't need to do anything
                    if (NumCoverageSamplesT::value > 1)
                    {
                        triDesc.coverageMask[sampleNum] = 0;
                    }
                    RDTSC_EVENT(pDC->pContext->pBucketMgr, BETrivialReject, 1, 0);
                }
            }

#if KNOB_ENABLE_TOSS_POINTS
            if (KNOB_TOSS_RS)
            {
                gToss = triDesc.coverageMask[0];
            }
            else
#endif
                if (triDesc.anyCoveredSamples)
            {
                // if conservative rast and MSAA are enabled, conservative coverage for a pixel
                // means all samples in that pixel are covered copy conservative coverage result to
                // all samples
                if (RT::IsConservativeT::value)
                {
                    auto copyCoverage = [&](int sample) {
                        triDesc.coverageMask[sample] = triDesc.coverageMask[0];
                    };
                    UnrollerL<1, RT::MT::numSamples, 1>::step(copyCoverage);
                }

                // Track rasterized subspans
                AR_EVENT(RasterTileCount(pDC->drawId, 1));

                RDTSC_BEGIN(pDC->pContext->pBucketMgr, BEPixelBackend, pDC->drawId);
                backendFuncs.pfnBackend(pDC,
                                        workerId,
                                        tileX << KNOB_TILE_X_DIM_SHIFT,
                                        tileY << KNOB_TILE_Y_DIM_SHIFT,
                                        triDesc,
                                        renderBuffers);
                RDTSC_END(pDC->pContext->pBucketMgr, BEPixelBackend, 0);
            }

            // step to the next tile in X
            for (uint32_t e = 0; e < RT::NumEdgesT::value; ++e)
            {
                vEdgeFix16[e] =
                    _mm256_add_pd(vEdgeFix16[e], _mm256_set1_pd(rastEdges[e].stepRasterTileX));
            }
            StepRasterTileX<RT>(state.colorHottileEnable, renderBuffers);
        }

        // step to the next tile in Y
        for (uint32_t e = 0; e < RT::NumEdgesT::value; ++e)
        {
            vEdgeFix16[e] =
                _mm256_add_pd(vStartOfRowEdge[e], _mm256_set1_pd(rastEdges[e].stepRasterTileY));
        }
        StepRasterTileY<RT>(state.colorHottileEnable, renderBuffers, currentRenderBufferRow);
    }

    RDTSC_END(pDC->pContext->pBucketMgr, BERasterizeTriangle, 1);
}

// Get pointers to hot tile memory for color RT, depth, stencil
template <uint32_t numSamples>
void GetRenderHotTiles(DRAW_CONTEXT*        pDC,
                       uint32_t             workerId,
                       uint32_t             macroID,
                       uint32_t             tileX,
                       uint32_t             tileY,
                       RenderOutputBuffers& renderBuffers,
                       uint32_t             renderTargetArrayIndex)
{
    const API_STATE& state    = GetApiState(pDC);
    SWR_CONTEXT*     pContext = pDC->pContext;
    HANDLE hWorkerPrivateData = pContext->threadPool.pThreadData[workerId].pWorkerPrivateData;

    uint32_t mx, my;
    MacroTileMgr::getTileIndices(macroID, mx, my);
    tileX -= KNOB_MACROTILE_X_DIM_IN_TILES * mx;
    tileY -= KNOB_MACROTILE_Y_DIM_IN_TILES * my;

    // compute tile offset for active hottile buffers
    const uint32_t pitch = KNOB_MACROTILE_X_DIM * FormatTraits<KNOB_COLOR_HOT_TILE_FORMAT>::bpp / 8;
    uint32_t       offset = ComputeTileOffset2D<
        TilingTraits<SWR_TILE_SWRZ, FormatTraits<KNOB_COLOR_HOT_TILE_FORMAT>::bpp>>(
        pitch, tileX, tileY);
    offset *= numSamples;

    unsigned long rtSlot                 = 0;
    uint32_t      colorHottileEnableMask = state.colorHottileEnable;
    while (_BitScanForward(&rtSlot, colorHottileEnableMask))
    {
        HOTTILE* pColor = pContext->pHotTileMgr->GetHotTile(
            pContext,
            pDC,
            hWorkerPrivateData,
            macroID,
            (SWR_RENDERTARGET_ATTACHMENT)(SWR_ATTACHMENT_COLOR0 + rtSlot),
            true,
            numSamples,
            renderTargetArrayIndex);
        renderBuffers.pColor[rtSlot] = pColor->pBuffer + offset;
        renderBuffers.pColorHotTile[rtSlot] = pColor;

        colorHottileEnableMask &= ~(1 << rtSlot);
    }
    if (state.depthHottileEnable)
    {
        const uint32_t pitch =
            KNOB_MACROTILE_X_DIM * FormatTraits<KNOB_DEPTH_HOT_TILE_FORMAT>::bpp / 8;
        uint32_t offset = ComputeTileOffset2D<
            TilingTraits<SWR_TILE_SWRZ, FormatTraits<KNOB_DEPTH_HOT_TILE_FORMAT>::bpp>>(
            pitch, tileX, tileY);
        offset *= numSamples;
        HOTTILE* pDepth = pContext->pHotTileMgr->GetHotTile(pContext,
                                                            pDC,
                                                            hWorkerPrivateData,
                                                            macroID,
                                                            SWR_ATTACHMENT_DEPTH,
                                                            true,
                                                            numSamples,
                                                            renderTargetArrayIndex);
        pDepth->state   = HOTTILE_DIRTY;
        SWR_ASSERT(pDepth->pBuffer != nullptr);
        renderBuffers.pDepth = pDepth->pBuffer + offset;
        renderBuffers.pDepthHotTile = pDepth;
    }
    if (state.stencilHottileEnable)
    {
        const uint32_t pitch =
            KNOB_MACROTILE_X_DIM * FormatTraits<KNOB_STENCIL_HOT_TILE_FORMAT>::bpp / 8;
        uint32_t offset = ComputeTileOffset2D<
            TilingTraits<SWR_TILE_SWRZ, FormatTraits<KNOB_STENCIL_HOT_TILE_FORMAT>::bpp>>(
            pitch, tileX, tileY);
        offset *= numSamples;
        HOTTILE* pStencil = pContext->pHotTileMgr->GetHotTile(pContext,
                                                              pDC,
                                                              hWorkerPrivateData,
                                                              macroID,
                                                              SWR_ATTACHMENT_STENCIL,
                                                              true,
                                                              numSamples,
                                                              renderTargetArrayIndex);
        pStencil->state   = HOTTILE_DIRTY;
        SWR_ASSERT(pStencil->pBuffer != nullptr);
        renderBuffers.pStencil = pStencil->pBuffer + offset;
        renderBuffers.pStencilHotTile = pStencil;
    }
}

template <typename RT>
INLINE void StepRasterTileX(uint32_t colorHotTileMask, RenderOutputBuffers& buffers)
{
    unsigned long rt = 0;
    while (_BitScanForward(&rt, colorHotTileMask))
    {
        colorHotTileMask &= ~(1 << rt);
        buffers.pColor[rt] += RT::colorRasterTileStep;
    }

    buffers.pDepth += RT::depthRasterTileStep;
    buffers.pStencil += RT::stencilRasterTileStep;
}

template <typename RT>
INLINE void StepRasterTileY(uint32_t             colorHotTileMask,
                            RenderOutputBuffers& buffers,
                            RenderOutputBuffers& startBufferRow)
{
    unsigned long rt = 0;
    while (_BitScanForward(&rt, colorHotTileMask))
    {
        colorHotTileMask &= ~(1 << rt);
        startBufferRow.pColor[rt] += RT::colorRasterTileRowStep;
        buffers.pColor[rt] = startBufferRow.pColor[rt];
    }
    startBufferRow.pDepth += RT::depthRasterTileRowStep;
    buffers.pDepth = startBufferRow.pDepth;

    startBufferRow.pStencil += RT::stencilRasterTileRowStep;
    buffers.pStencil = startBufferRow.pStencil;
}
