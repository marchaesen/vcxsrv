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
 * @file rasterizer.h
 *
 * @brief Definitions for the rasterizer.
 *
 ******************************************************************************/
#pragma once

#include "context.h"
#include <type_traits>
#include "conservativeRast.h"
#include "multisample.h"

void RasterizeLine(DRAW_CONTEXT* pDC, uint32_t workerId, uint32_t macroTile, void* pData);
void RasterizeSimplePoint(DRAW_CONTEXT* pDC, uint32_t workerId, uint32_t macroTile, void* pData);
void RasterizeTriPoint(DRAW_CONTEXT* pDC, uint32_t workerId, uint32_t macroTile, void* pData);
void InitRasterizerFunctions();

INLINE
__m128i fpToFixedPoint(const __m128 vIn)
{
    __m128 vFixed = _mm_mul_ps(vIn, _mm_set1_ps(FIXED_POINT_SCALE));
    return _mm_cvtps_epi32(vFixed);
}

enum TriEdgesStates
{
    STATE_NO_VALID_EDGES = 0,
    STATE_E0_E1_VALID,
    STATE_E0_E2_VALID,
    STATE_E1_E2_VALID,
    STATE_ALL_EDGES_VALID,
    STATE_VALID_TRI_EDGE_COUNT,
};

enum TriEdgesValues
{
    NO_VALID_EDGES  = 0,
    E0_E1_VALID     = 0x3,
    E0_E2_VALID     = 0x5,
    E1_E2_VALID     = 0x6,
    ALL_EDGES_VALID = 0x7,
    VALID_TRI_EDGE_COUNT,
};

// Selector for correct templated RasterizeTriangle function
PFN_WORK_FUNC GetRasterizerFunc(SWR_MULTISAMPLE_COUNT numSamples,
                                bool                  IsCenter,
                                bool                  IsConservative,
                                SWR_INPUT_COVERAGE    InputCoverage,
                                uint32_t              EdgeEnable,
                                bool                  RasterizeScissorEdges);

//////////////////////////////////////////////////////////////////////////
/// @brief ValidTriEdges convenience typedefs used for templated function
/// specialization supported Fixed Point precisions
typedef std::integral_constant<uint32_t, ALL_EDGES_VALID> AllEdgesValidT;
typedef std::integral_constant<uint32_t, E0_E1_VALID>     E0E1ValidT;
typedef std::integral_constant<uint32_t, E0_E2_VALID>     E0E2ValidT;
typedef std::integral_constant<uint32_t, E1_E2_VALID>     E1E2ValidT;
typedef std::integral_constant<uint32_t, NO_VALID_EDGES>  NoEdgesValidT;

typedef std::integral_constant<uint32_t, STATE_ALL_EDGES_VALID> StateAllEdgesValidT;
typedef std::integral_constant<uint32_t, STATE_E0_E1_VALID>     StateE0E1ValidT;
typedef std::integral_constant<uint32_t, STATE_E0_E2_VALID>     StateE0E2ValidT;
typedef std::integral_constant<uint32_t, STATE_E1_E2_VALID>     StateE1E2ValidT;
typedef std::integral_constant<uint32_t, STATE_NO_VALID_EDGES>  StateNoEdgesValidT;

// some specializations to convert from edge state to edge bitmask values
template <typename EdgeMask>
struct EdgeMaskVal
{
    static_assert(EdgeMask::value > STATE_ALL_EDGES_VALID,
                  "Primary EdgeMaskVal shouldn't be instantiated");
};

template <>
struct EdgeMaskVal<StateAllEdgesValidT>
{
    typedef AllEdgesValidT T;
};

template <>
struct EdgeMaskVal<StateE0E1ValidT>
{
    typedef E0E1ValidT T;
};

template <>
struct EdgeMaskVal<StateE0E2ValidT>
{
    typedef E0E2ValidT T;
};

template <>
struct EdgeMaskVal<StateE1E2ValidT>
{
    typedef E1E2ValidT T;
};

template <>
struct EdgeMaskVal<StateNoEdgesValidT>
{
    typedef NoEdgesValidT T;
};

INLINE uint32_t EdgeValToEdgeState(uint32_t val)
{
    SWR_ASSERT(val < VALID_TRI_EDGE_COUNT, "Unexpected tri edge mask");
    static const uint32_t edgeValToEdgeState[VALID_TRI_EDGE_COUNT] = {0, 0, 0, 1, 0, 2, 3, 4};
    return edgeValToEdgeState[val];
}

//////////////////////////////////////////////////////////////////////////
/// @struct RasterScissorEdgesT
/// @brief Primary RasterScissorEdgesT templated struct that holds compile
/// time information about the number of edges needed to be rasterized,
/// If either the scissor rect or conservative rast is enabled,
/// the scissor test is enabled and the rasterizer will test
/// 3 triangle edges + 4 scissor edges for coverage.
/// @tparam RasterScissorEdgesT: number of multisamples
/// @tparam ConservativeT: is this a conservative rasterization
/// @tparam EdgeMaskT: Which edges are valid(not degenerate)
template <typename RasterScissorEdgesT, typename ConservativeT, typename EdgeMaskT>
struct RasterEdgeTraits
{
    typedef std::true_type                      RasterizeScissorEdgesT;
    typedef std::integral_constant<uint32_t, 7> NumEdgesT;
    // typedef std::integral_constant<uint32_t, EdgeMaskT::value> ValidEdgeMaskT;
    typedef typename EdgeMaskVal<EdgeMaskT>::T ValidEdgeMaskT;
};

//////////////////////////////////////////////////////////////////////////
/// @brief specialization of RasterEdgeTraits. If neither scissor rect
/// nor conservative rast is enabled, only test 3 triangle edges
/// for coverage
template <typename EdgeMaskT>
struct RasterEdgeTraits<std::false_type, std::false_type, EdgeMaskT>
{
    typedef std::false_type                     RasterizeScissorEdgesT;
    typedef std::integral_constant<uint32_t, 3> NumEdgesT;
    // no need for degenerate edge masking in non-conservative case; rasterize all triangle edges
    typedef std::integral_constant<uint32_t, ALL_EDGES_VALID> ValidEdgeMaskT;
};

//////////////////////////////////////////////////////////////////////////
/// @struct RasterizerTraits
/// @brief templated struct that holds compile time information used
/// during rasterization. Inherits EdgeTraits and ConservativeRastBETraits.
/// @tparam NumSamplesT: number of multisamples
/// @tparam ConservativeT: is this a conservative rasterization
/// @tparam InputCoverageT: what type of input coverage is the PS expecting?
/// (only used with conservative rasterization)
/// @tparam RasterScissorEdgesT: do we need to rasterize with a scissor?
template <typename NumSamplesT,
          typename CenterPatternT,
          typename ConservativeT,
          typename InputCoverageT,
          typename EdgeEnableT,
          typename RasterScissorEdgesT>
struct _RasterizerTraits : public ConservativeRastBETraits<ConservativeT, InputCoverageT>,
                           public RasterEdgeTraits<RasterScissorEdgesT, ConservativeT, EdgeEnableT>
{
    typedef MultisampleTraits<static_cast<SWR_MULTISAMPLE_COUNT>(NumSamplesT::value),
                              CenterPatternT::value>
        MT;

    /// Fixed point precision the rasterizer is using
    typedef FixedPointTraits<Fixed_16_8> PrecisionT;
    /// Fixed point precision of the edge tests used during rasterization
    typedef FixedPointTraits<Fixed_X_16> EdgePrecisionT;

    // If conservative rast or MSAA center pattern is enabled, only need a single sample coverage
    // test, with the result copied to all samples
    typedef std::integral_constant<int, ConservativeT::value ? 1 : MT::numCoverageSamples>
        NumCoverageSamplesT;

    static_assert(
        EdgePrecisionT::BitsT::value >=
            ConservativeRastBETraits<ConservativeT,
                                     InputCoverageT>::ConservativePrecisionT::BitsT::value,
        "Rasterizer edge fixed point precision < required conservative rast precision");

    /// constants used to offset between different types of raster tiles
    static const int colorRasterTileStep{
        (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * (FormatTraits<KNOB_COLOR_HOT_TILE_FORMAT>::bpp / 8)) *
        MT::numSamples};
    static const int depthRasterTileStep{
        (KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM * (FormatTraits<KNOB_DEPTH_HOT_TILE_FORMAT>::bpp / 8)) *
        MT::numSamples};
    static const int stencilRasterTileStep{(KNOB_TILE_X_DIM * KNOB_TILE_Y_DIM *
                                            (FormatTraits<KNOB_STENCIL_HOT_TILE_FORMAT>::bpp / 8)) *
                                           MT::numSamples};
    static const int colorRasterTileRowStep{(KNOB_MACROTILE_X_DIM / KNOB_TILE_X_DIM) *
                                            colorRasterTileStep};
    static const int depthRasterTileRowStep{(KNOB_MACROTILE_X_DIM / KNOB_TILE_X_DIM) *
                                            depthRasterTileStep};
    static const int stencilRasterTileRowStep{(KNOB_MACROTILE_X_DIM / KNOB_TILE_X_DIM) *
                                              stencilRasterTileStep};
};

template <uint32_t NumSamplesT,
          uint32_t CenterPatternT,
          uint32_t ConservativeT,
          uint32_t InputCoverageT,
          uint32_t EdgeEnableT,
          uint32_t RasterScissorEdgesT>
struct RasterizerTraits final
    : public _RasterizerTraits<std::integral_constant<uint32_t, NumSamplesT>,
                               std::integral_constant<bool, CenterPatternT != 0>,
                               std::integral_constant<bool, ConservativeT != 0>,
                               std::integral_constant<uint32_t, InputCoverageT>,
                               std::integral_constant<uint32_t, EdgeEnableT>,
                               std::integral_constant<bool, RasterScissorEdgesT != 0>>
{
};
