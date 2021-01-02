/****************************************************************************
 * Copyright (C) 2014-2016 Intel Corporation.   All Rights Reserved.
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
 * @file conservativerast.h
 *
 ******************************************************************************/
#pragma once
#include <type_traits>
#include "common/simdintrin.h"

enum FixedPointFmt
{
    FP_UNINIT,
    _16_8,
    _16_9,
    _X_16,
};

//////////////////////////////////////////////////////////////////////////
/// @brief convenience typedefs for supported Fixed Point precisions
typedef std::integral_constant<uint32_t, FP_UNINIT> Fixed_Uninit;
typedef std::integral_constant<uint32_t, _16_8>     Fixed_16_8;
typedef std::integral_constant<uint32_t, _16_9>     Fixed_16_9;
typedef std::integral_constant<uint32_t, _X_16>     Fixed_X_16;

//////////////////////////////////////////////////////////////////////////
/// @struct FixedPointTraits
/// @brief holds constants relating to converting between FP and Fixed point
/// @tparam FT: fixed precision type
template <typename FT>
struct FixedPointTraits
{
};

//////////////////////////////////////////////////////////////////////////
/// @brief Fixed_16_8 specialization of FixedPointTraits
template <>
struct FixedPointTraits<Fixed_16_8>
{
    /// multiplier to go from FP32 to Fixed Point 16.8
    typedef std::integral_constant<uint32_t, 256> ScaleT;
    /// number of bits to shift to go from 16.8 fixed => int32
    typedef std::integral_constant<uint32_t, 8> BitsT;
    typedef Fixed_16_8                          TypeT;
};

//////////////////////////////////////////////////////////////////////////
/// @brief Fixed_16_9 specialization of FixedPointTraits
template <>
struct FixedPointTraits<Fixed_16_9>
{
    /// multiplier to go from FP32 to Fixed Point 16.9
    typedef std::integral_constant<uint32_t, 512> ScaleT;
    /// number of bits to shift to go from 16.9 fixed => int32
    typedef std::integral_constant<uint32_t, 9> BitsT;
    typedef Fixed_16_9                          TypeT;
};

//////////////////////////////////////////////////////////////////////////
/// @brief Fixed_16_9 specialization of FixedPointTraits
template <>
struct FixedPointTraits<Fixed_X_16>
{
    /// multiplier to go from FP32 to Fixed Point X.16
    typedef std::integral_constant<uint32_t, 65536> ScaleT;
    /// number of bits to shift to go from X.16 fixed => int32
    typedef std::integral_constant<uint32_t, 16> BitsT;
    typedef Fixed_X_16                           TypeT;
};

//////////////////////////////////////////////////////////////////////////
/// @brief convenience typedefs for conservative rasterization modes
typedef std::false_type StandardRastT;
typedef std::true_type  ConservativeRastT;

//////////////////////////////////////////////////////////////////////////
/// @brief convenience typedefs for Input Coverage rasterization modes
typedef std::integral_constant<uint32_t, SWR_INPUT_COVERAGE_NONE>   NoInputCoverageT;
typedef std::integral_constant<uint32_t, SWR_INPUT_COVERAGE_NORMAL> OuterConservativeCoverageT;
typedef std::integral_constant<uint32_t, SWR_INPUT_COVERAGE_INNER_CONSERVATIVE>
    InnerConservativeCoverageT;

//////////////////////////////////////////////////////////////////////////
/// @struct ConservativeRastTraits
/// @brief primary ConservativeRastTraits template. Shouldn't be instantiated
/// @tparam ConservativeT: type of conservative rasterization
template <typename ConservativeT>
struct ConservativeRastFETraits
{
};

//////////////////////////////////////////////////////////////////////////
/// @brief StandardRast specialization of ConservativeRastTraits
template <>
struct ConservativeRastFETraits<StandardRastT>
{
    typedef std::false_type                     IsConservativeT;
    typedef std::integral_constant<uint32_t, 0> BoundingBoxOffsetT;
};

//////////////////////////////////////////////////////////////////////////
/// @brief ConservativeRastT specialization of ConservativeRastTraits
template <>
struct ConservativeRastFETraits<ConservativeRastT>
{
    typedef std::true_type                      IsConservativeT;
    typedef std::integral_constant<uint32_t, 1> BoundingBoxOffsetT;
};

//////////////////////////////////////////////////////////////////////////
/// @brief convenience typedefs for ConservativeRastFETraits
typedef ConservativeRastFETraits<StandardRastT>     FEStandardRastT;
typedef ConservativeRastFETraits<ConservativeRastT> FEConservativeRastT;

//////////////////////////////////////////////////////////////////////////
/// @struct ConservativeRastBETraits
/// @brief primary ConservativeRastBETraits template. Shouldn't be instantiated;
/// default to standard rasterization behavior
/// @tparam ConservativeT: type of conservative rasterization
/// @tparam InputCoverageT: type of input coverage requested, if any
template <typename ConservativeT, typename _InputCoverageT>
struct ConservativeRastBETraits
{
    typedef std::false_type                    IsConservativeT;
    typedef _InputCoverageT                    InputCoverageT;
    typedef FixedPointTraits<Fixed_16_8>       ConservativePrecisionT;
    typedef std::integral_constant<int32_t, 0> ConservativeEdgeOffsetT;
    typedef std::integral_constant<int32_t, 0> InnerConservativeEdgeOffsetT;
};

//////////////////////////////////////////////////////////////////////////
/// @brief StandardRastT specialization of ConservativeRastBETraits
template <typename _InputCoverageT>
struct ConservativeRastBETraits<StandardRastT, _InputCoverageT>
{
    typedef std::false_type                    IsConservativeT;
    typedef _InputCoverageT                    InputCoverageT;
    typedef FixedPointTraits<Fixed_16_8>       ConservativePrecisionT;
    typedef std::integral_constant<int32_t, 0> ConservativeEdgeOffsetT;
    typedef std::integral_constant<int32_t, 0> InnerConservativeEdgeOffsetT;
};

//////////////////////////////////////////////////////////////////////////
/// @brief ConservativeRastT specialization of ConservativeRastBETraits
/// with no input coverage
template <>
struct ConservativeRastBETraits<ConservativeRastT, NoInputCoverageT>
{
    typedef std::true_type   IsConservativeT;
    typedef NoInputCoverageT InputCoverageT;

    typedef FixedPointTraits<Fixed_16_9> ConservativePrecisionT;

    /// offset edge away from pixel center by 1/2 pixel + 1/512, in Fixed 16.9 precision
    /// this allows the rasterizer to do the 3 edge coverage tests against a single point, instead
    /// of of having to compare individual edges to pixel corners to check if any part of the
    /// triangle intersects a pixel
    typedef std::integral_constant<int32_t, (ConservativePrecisionT::ScaleT::value / 2) + 1>
                                               ConservativeEdgeOffsetT;
    typedef std::integral_constant<int32_t, 0> InnerConservativeEdgeOffsetT;
};

//////////////////////////////////////////////////////////////////////////
/// @brief ConservativeRastT specialization of ConservativeRastBETraits
/// with OuterConservativeCoverage
template <>
struct ConservativeRastBETraits<ConservativeRastT, OuterConservativeCoverageT>
{
    typedef std::true_type             IsConservativeT;
    typedef OuterConservativeCoverageT InputCoverageT;

    typedef FixedPointTraits<Fixed_16_9> ConservativePrecisionT;

    /// offset edge away from pixel center by 1/2 pixel + 1/512, in Fixed 16.9 precision
    /// this allows the rasterizer to do the 3 edge coverage tests against a single point, instead
    /// of of having to compare individual edges to pixel corners to check if any part of the
    /// triangle intersects a pixel
    typedef std::integral_constant<int32_t, (ConservativePrecisionT::ScaleT::value / 2) + 1>
                                               ConservativeEdgeOffsetT;
    typedef std::integral_constant<int32_t, 0> InnerConservativeEdgeOffsetT;
};

//////////////////////////////////////////////////////////////////////////
/// @brief ConservativeRastT specialization of ConservativeRastBETraits
/// with InnerConservativeCoverage
template <>
struct ConservativeRastBETraits<ConservativeRastT, InnerConservativeCoverageT>
{
    typedef std::true_type             IsConservativeT;
    typedef InnerConservativeCoverageT InputCoverageT;

    typedef FixedPointTraits<Fixed_16_9> ConservativePrecisionT;

    /// offset edge away from pixel center by 1/2 pixel + 1/512, in Fixed 16.9 precision
    /// this allows the rasterizer to do the 3 edge coverage tests against a single point, instead
    /// of of having to compare individual edges to pixel corners to check if any part of the
    /// triangle intersects a pixel
    typedef std::integral_constant<int32_t, (ConservativePrecisionT::ScaleT::value / 2) + 1>
        ConservativeEdgeOffsetT;

    /// undo the outer conservative offset and offset edge towards from pixel center by 1/2 pixel +
    /// 1/512, in Fixed 16.9 precision this allows the rasterizer to do the 3 edge coverage tests
    /// against a single point, instead of of having to compare individual edges to pixel corners to
    /// check if a pixel is fully covered by a triangle
    typedef std::integral_constant<int32_t,
                                   static_cast<int32_t>(
                                       -((ConservativePrecisionT::ScaleT::value / 2) + 1) -
                                       ConservativeEdgeOffsetT::value)>
        InnerConservativeEdgeOffsetT;
};