/****************************************************************************
 * Copyright (C) 2016 Intel Corporation.   All Rights Reserved.
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
 * @file format_traits.h
 *
 * @brief Format Traits.  auto-generated file
 *
 * DO NOT EDIT
 *
 ******************************************************************************/
#pragma once

#include "format_types.h"
#include "format_utils.h"

//////////////////////////////////////////////////////////////////////////
/// FormatSwizzle - Component swizzle selects
//////////////////////////////////////////////////////////////////////////
template <uint32_t comp0 = 0, uint32_t comp1 = 0, uint32_t comp2 = 0, uint32_t comp3 = 0>
struct FormatSwizzle
{
    // Return swizzle select for component.
    INLINE static uint32_t swizzle(uint32_t c)
    {
        static const uint32_t s[4] = {comp0, comp1, comp2, comp3};
        return s[c];
    }
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits - Format traits
//////////////////////////////////////////////////////////////////////////
template <SWR_FORMAT format>
struct FormatTraits : ComponentTraits<SWR_TYPE_UNKNOWN, 0>, FormatSwizzle<0>, Defaults<0, 0, 0, 0>
{
    static const uint32_t bpp{0};
    static const uint32_t numComps{0};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};

    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R32G32B32A32_FLOAT> - Format traits specialization for R32G32B32A32_FLOAT
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R32G32B32A32_FLOAT> : ComponentTraits<SWR_TYPE_FLOAT,
                                                          32,
                                                          SWR_TYPE_FLOAT,
                                                          32,
                                                          SWR_TYPE_FLOAT,
                                                          32,
                                                          SWR_TYPE_FLOAT,
                                                          32>,
                                          FormatSwizzle<0, 1, 2, 3>,
                                          Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{128};
    static const uint32_t numComps{4};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose32_32_32_32    TransposeT;
    typedef Format4<32, 32, 32, 32> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R32G32B32A32_SINT> - Format traits specialization for R32G32B32A32_SINT
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R32G32B32A32_SINT>
    : ComponentTraits<SWR_TYPE_SINT, 32, SWR_TYPE_SINT, 32, SWR_TYPE_SINT, 32, SWR_TYPE_SINT, 32>,
      FormatSwizzle<0, 1, 2, 3>,
      Defaults<0, 0, 0, 0x1>
{
    static const uint32_t bpp{128};
    static const uint32_t numComps{4};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose32_32_32_32    TransposeT;
    typedef Format4<32, 32, 32, 32> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R32G32B32A32_UINT> - Format traits specialization for R32G32B32A32_UINT
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R32G32B32A32_UINT>
    : ComponentTraits<SWR_TYPE_UINT, 32, SWR_TYPE_UINT, 32, SWR_TYPE_UINT, 32, SWR_TYPE_UINT, 32>,
      FormatSwizzle<0, 1, 2, 3>,
      Defaults<0, 0, 0, 0x1>
{
    static const uint32_t bpp{128};
    static const uint32_t numComps{4};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose32_32_32_32    TransposeT;
    typedef Format4<32, 32, 32, 32> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R64G64_FLOAT> - Format traits specialization for R64G64_FLOAT
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R64G64_FLOAT> : ComponentTraits<SWR_TYPE_FLOAT, 64, SWR_TYPE_FLOAT, 64>,
                                    FormatSwizzle<0, 1>,
                                    Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{128};
    static const uint32_t numComps{2};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose64_64  TransposeT;
    typedef Format2<64, 64> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R32G32B32X32_FLOAT> - Format traits specialization for R32G32B32X32_FLOAT
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R32G32B32X32_FLOAT> : ComponentTraits<SWR_TYPE_FLOAT,
                                                          32,
                                                          SWR_TYPE_FLOAT,
                                                          32,
                                                          SWR_TYPE_FLOAT,
                                                          32,
                                                          SWR_TYPE_UNUSED,
                                                          32>,
                                          FormatSwizzle<0, 1, 2, 3>,
                                          Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{128};
    static const uint32_t numComps{3};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose32_32_32_32    TransposeT;
    typedef Format4<32, 32, 32, 32> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R32G32B32A32_SSCALED> - Format traits specialization for R32G32B32A32_SSCALED
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R32G32B32A32_SSCALED> : ComponentTraits<SWR_TYPE_SSCALED,
                                                            32,
                                                            SWR_TYPE_SSCALED,
                                                            32,
                                                            SWR_TYPE_SSCALED,
                                                            32,
                                                            SWR_TYPE_SSCALED,
                                                            32>,
                                            FormatSwizzle<0, 1, 2, 3>,
                                            Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{128};
    static const uint32_t numComps{4};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose32_32_32_32    TransposeT;
    typedef Format4<32, 32, 32, 32> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R32G32B32A32_USCALED> - Format traits specialization for R32G32B32A32_USCALED
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R32G32B32A32_USCALED> : ComponentTraits<SWR_TYPE_USCALED,
                                                            32,
                                                            SWR_TYPE_USCALED,
                                                            32,
                                                            SWR_TYPE_USCALED,
                                                            32,
                                                            SWR_TYPE_USCALED,
                                                            32>,
                                            FormatSwizzle<0, 1, 2, 3>,
                                            Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{128};
    static const uint32_t numComps{4};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose32_32_32_32    TransposeT;
    typedef Format4<32, 32, 32, 32> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R32G32B32A32_SFIXED> - Format traits specialization for R32G32B32A32_SFIXED
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R32G32B32A32_SFIXED> : ComponentTraits<SWR_TYPE_SFIXED,
                                                           32,
                                                           SWR_TYPE_SFIXED,
                                                           32,
                                                           SWR_TYPE_SFIXED,
                                                           32,
                                                           SWR_TYPE_SFIXED,
                                                           32>,
                                           FormatSwizzle<0, 1, 2, 3>,
                                           Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{128};
    static const uint32_t numComps{4};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose32_32_32_32    TransposeT;
    typedef Format4<32, 32, 32, 32> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R32G32B32_FLOAT> - Format traits specialization for R32G32B32_FLOAT
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R32G32B32_FLOAT>
    : ComponentTraits<SWR_TYPE_FLOAT, 32, SWR_TYPE_FLOAT, 32, SWR_TYPE_FLOAT, 32>,
      FormatSwizzle<0, 1, 2>,
      Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{96};
    static const uint32_t numComps{3};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose32_32_32   TransposeT;
    typedef Format3<32, 32, 32> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R32G32B32_SINT> - Format traits specialization for R32G32B32_SINT
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R32G32B32_SINT>
    : ComponentTraits<SWR_TYPE_SINT, 32, SWR_TYPE_SINT, 32, SWR_TYPE_SINT, 32>,
      FormatSwizzle<0, 1, 2>,
      Defaults<0, 0, 0, 0x1>
{
    static const uint32_t bpp{96};
    static const uint32_t numComps{3};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose32_32_32   TransposeT;
    typedef Format3<32, 32, 32> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R32G32B32_UINT> - Format traits specialization for R32G32B32_UINT
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R32G32B32_UINT>
    : ComponentTraits<SWR_TYPE_UINT, 32, SWR_TYPE_UINT, 32, SWR_TYPE_UINT, 32>,
      FormatSwizzle<0, 1, 2>,
      Defaults<0, 0, 0, 0x1>
{
    static const uint32_t bpp{96};
    static const uint32_t numComps{3};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose32_32_32   TransposeT;
    typedef Format3<32, 32, 32> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R32G32B32_SSCALED> - Format traits specialization for R32G32B32_SSCALED
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R32G32B32_SSCALED>
    : ComponentTraits<SWR_TYPE_SSCALED, 32, SWR_TYPE_SSCALED, 32, SWR_TYPE_SSCALED, 32>,
      FormatSwizzle<0, 1, 2>,
      Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{96};
    static const uint32_t numComps{3};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose32_32_32   TransposeT;
    typedef Format3<32, 32, 32> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R32G32B32_USCALED> - Format traits specialization for R32G32B32_USCALED
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R32G32B32_USCALED>
    : ComponentTraits<SWR_TYPE_USCALED, 32, SWR_TYPE_USCALED, 32, SWR_TYPE_USCALED, 32>,
      FormatSwizzle<0, 1, 2>,
      Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{96};
    static const uint32_t numComps{3};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose32_32_32   TransposeT;
    typedef Format3<32, 32, 32> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R32G32B32_SFIXED> - Format traits specialization for R32G32B32_SFIXED
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R32G32B32_SFIXED>
    : ComponentTraits<SWR_TYPE_SFIXED, 32, SWR_TYPE_SFIXED, 32, SWR_TYPE_SFIXED, 32>,
      FormatSwizzle<0, 1, 2>,
      Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{96};
    static const uint32_t numComps{3};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose32_32_32   TransposeT;
    typedef Format3<32, 32, 32> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R16G16B16A16_UNORM> - Format traits specialization for R16G16B16A16_UNORM
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R16G16B16A16_UNORM> : ComponentTraits<SWR_TYPE_UNORM,
                                                          16,
                                                          SWR_TYPE_UNORM,
                                                          16,
                                                          SWR_TYPE_UNORM,
                                                          16,
                                                          SWR_TYPE_UNORM,
                                                          16>,
                                          FormatSwizzle<0, 1, 2, 3>,
                                          Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{64};
    static const uint32_t numComps{4};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose16_16_16_16    TransposeT;
    typedef Format4<16, 16, 16, 16> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R16G16B16A16_SNORM> - Format traits specialization for R16G16B16A16_SNORM
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R16G16B16A16_SNORM> : ComponentTraits<SWR_TYPE_SNORM,
                                                          16,
                                                          SWR_TYPE_SNORM,
                                                          16,
                                                          SWR_TYPE_SNORM,
                                                          16,
                                                          SWR_TYPE_SNORM,
                                                          16>,
                                          FormatSwizzle<0, 1, 2, 3>,
                                          Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{64};
    static const uint32_t numComps{4};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose16_16_16_16    TransposeT;
    typedef Format4<16, 16, 16, 16> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R16G16B16A16_SINT> - Format traits specialization for R16G16B16A16_SINT
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R16G16B16A16_SINT>
    : ComponentTraits<SWR_TYPE_SINT, 16, SWR_TYPE_SINT, 16, SWR_TYPE_SINT, 16, SWR_TYPE_SINT, 16>,
      FormatSwizzle<0, 1, 2, 3>,
      Defaults<0, 0, 0, 0x1>
{
    static const uint32_t bpp{64};
    static const uint32_t numComps{4};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose16_16_16_16    TransposeT;
    typedef Format4<16, 16, 16, 16> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R16G16B16A16_UINT> - Format traits specialization for R16G16B16A16_UINT
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R16G16B16A16_UINT>
    : ComponentTraits<SWR_TYPE_UINT, 16, SWR_TYPE_UINT, 16, SWR_TYPE_UINT, 16, SWR_TYPE_UINT, 16>,
      FormatSwizzle<0, 1, 2, 3>,
      Defaults<0, 0, 0, 0x1>
{
    static const uint32_t bpp{64};
    static const uint32_t numComps{4};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose16_16_16_16    TransposeT;
    typedef Format4<16, 16, 16, 16> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R16G16B16A16_FLOAT> - Format traits specialization for R16G16B16A16_FLOAT
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R16G16B16A16_FLOAT> : ComponentTraits<SWR_TYPE_FLOAT,
                                                          16,
                                                          SWR_TYPE_FLOAT,
                                                          16,
                                                          SWR_TYPE_FLOAT,
                                                          16,
                                                          SWR_TYPE_FLOAT,
                                                          16>,
                                          FormatSwizzle<0, 1, 2, 3>,
                                          Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{64};
    static const uint32_t numComps{4};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose16_16_16_16    TransposeT;
    typedef Format4<16, 16, 16, 16> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R32G32_FLOAT> - Format traits specialization for R32G32_FLOAT
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R32G32_FLOAT> : ComponentTraits<SWR_TYPE_FLOAT, 32, SWR_TYPE_FLOAT, 32>,
                                    FormatSwizzle<0, 1>,
                                    Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{64};
    static const uint32_t numComps{2};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose32_32  TransposeT;
    typedef Format2<32, 32> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R32G32_SINT> - Format traits specialization for R32G32_SINT
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R32G32_SINT> : ComponentTraits<SWR_TYPE_SINT, 32, SWR_TYPE_SINT, 32>,
                                   FormatSwizzle<0, 1>,
                                   Defaults<0, 0, 0, 0x1>
{
    static const uint32_t bpp{64};
    static const uint32_t numComps{2};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose32_32  TransposeT;
    typedef Format2<32, 32> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R32G32_UINT> - Format traits specialization for R32G32_UINT
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R32G32_UINT> : ComponentTraits<SWR_TYPE_UINT, 32, SWR_TYPE_UINT, 32>,
                                   FormatSwizzle<0, 1>,
                                   Defaults<0, 0, 0, 0x1>
{
    static const uint32_t bpp{64};
    static const uint32_t numComps{2};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose32_32  TransposeT;
    typedef Format2<32, 32> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R32_FLOAT_X8X24_TYPELESS> - Format traits specialization for
/// R32_FLOAT_X8X24_TYPELESS
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R32_FLOAT_X8X24_TYPELESS>
    : ComponentTraits<SWR_TYPE_FLOAT, 32, SWR_TYPE_UNUSED, 32>,
      FormatSwizzle<0, 1>,
      Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{64};
    static const uint32_t numComps{1};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose32_32  TransposeT;
    typedef Format2<32, 32> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<X32_TYPELESS_G8X24_UINT> - Format traits specialization for X32_TYPELESS_G8X24_UINT
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<X32_TYPELESS_G8X24_UINT>
    : ComponentTraits<SWR_TYPE_UINT, 32, SWR_TYPE_UNUSED, 32>,
      FormatSwizzle<0, 1>,
      Defaults<0, 0, 0, 0x1>
{
    static const uint32_t bpp{64};
    static const uint32_t numComps{1};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose32_32  TransposeT;
    typedef Format2<32, 32> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<L32A32_FLOAT> - Format traits specialization for L32A32_FLOAT
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<L32A32_FLOAT> : ComponentTraits<SWR_TYPE_FLOAT, 32, SWR_TYPE_FLOAT, 32>,
                                    FormatSwizzle<0, 3>,
                                    Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{64};
    static const uint32_t numComps{2};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{1};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose32_32  TransposeT;
    typedef Format2<32, 32> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R64_FLOAT> - Format traits specialization for R64_FLOAT
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R64_FLOAT>
    : ComponentTraits<SWR_TYPE_FLOAT, 64>, FormatSwizzle<0>, Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{64};
    static const uint32_t numComps{1};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef TransposeSingleComponent<64> TransposeT;
    typedef Format1<64>                  FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R16G16B16X16_UNORM> - Format traits specialization for R16G16B16X16_UNORM
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R16G16B16X16_UNORM> : ComponentTraits<SWR_TYPE_UNORM,
                                                          16,
                                                          SWR_TYPE_UNORM,
                                                          16,
                                                          SWR_TYPE_UNORM,
                                                          16,
                                                          SWR_TYPE_UNUSED,
                                                          16>,
                                          FormatSwizzle<0, 1, 2, 3>,
                                          Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{64};
    static const uint32_t numComps{3};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose16_16_16_16    TransposeT;
    typedef Format4<16, 16, 16, 16> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R16G16B16X16_FLOAT> - Format traits specialization for R16G16B16X16_FLOAT
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R16G16B16X16_FLOAT> : ComponentTraits<SWR_TYPE_FLOAT,
                                                          16,
                                                          SWR_TYPE_FLOAT,
                                                          16,
                                                          SWR_TYPE_FLOAT,
                                                          16,
                                                          SWR_TYPE_UNUSED,
                                                          16>,
                                          FormatSwizzle<0, 1, 2, 3>,
                                          Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{64};
    static const uint32_t numComps{3};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose16_16_16_16    TransposeT;
    typedef Format4<16, 16, 16, 16> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<L32X32_FLOAT> - Format traits specialization for L32X32_FLOAT
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<L32X32_FLOAT> : ComponentTraits<SWR_TYPE_FLOAT, 32, SWR_TYPE_FLOAT, 32>,
                                    FormatSwizzle<0, 3>,
                                    Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{64};
    static const uint32_t numComps{2};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose32_32  TransposeT;
    typedef Format2<32, 32> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<I32X32_FLOAT> - Format traits specialization for I32X32_FLOAT
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<I32X32_FLOAT> : ComponentTraits<SWR_TYPE_FLOAT, 32, SWR_TYPE_FLOAT, 32>,
                                    FormatSwizzle<0, 3>,
                                    Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{64};
    static const uint32_t numComps{2};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose32_32  TransposeT;
    typedef Format2<32, 32> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R16G16B16A16_SSCALED> - Format traits specialization for R16G16B16A16_SSCALED
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R16G16B16A16_SSCALED> : ComponentTraits<SWR_TYPE_SSCALED,
                                                            16,
                                                            SWR_TYPE_SSCALED,
                                                            16,
                                                            SWR_TYPE_SSCALED,
                                                            16,
                                                            SWR_TYPE_SSCALED,
                                                            16>,
                                            FormatSwizzle<0, 1, 2, 3>,
                                            Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{64};
    static const uint32_t numComps{4};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose16_16_16_16    TransposeT;
    typedef Format4<16, 16, 16, 16> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R16G16B16A16_USCALED> - Format traits specialization for R16G16B16A16_USCALED
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R16G16B16A16_USCALED> : ComponentTraits<SWR_TYPE_USCALED,
                                                            16,
                                                            SWR_TYPE_USCALED,
                                                            16,
                                                            SWR_TYPE_USCALED,
                                                            16,
                                                            SWR_TYPE_USCALED,
                                                            16>,
                                            FormatSwizzle<0, 1, 2, 3>,
                                            Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{64};
    static const uint32_t numComps{4};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose16_16_16_16    TransposeT;
    typedef Format4<16, 16, 16, 16> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R32G32_SSCALED> - Format traits specialization for R32G32_SSCALED
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R32G32_SSCALED> : ComponentTraits<SWR_TYPE_SSCALED, 32, SWR_TYPE_SSCALED, 32>,
                                      FormatSwizzle<0, 1>,
                                      Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{64};
    static const uint32_t numComps{2};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose32_32  TransposeT;
    typedef Format2<32, 32> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R32G32_USCALED> - Format traits specialization for R32G32_USCALED
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R32G32_USCALED> : ComponentTraits<SWR_TYPE_USCALED, 32, SWR_TYPE_USCALED, 32>,
                                      FormatSwizzle<0, 1>,
                                      Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{64};
    static const uint32_t numComps{2};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose32_32  TransposeT;
    typedef Format2<32, 32> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R32G32_SFIXED> - Format traits specialization for R32G32_SFIXED
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R32G32_SFIXED> : ComponentTraits<SWR_TYPE_SFIXED, 32, SWR_TYPE_SFIXED, 32>,
                                     FormatSwizzle<0, 1>,
                                     Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{64};
    static const uint32_t numComps{2};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose32_32  TransposeT;
    typedef Format2<32, 32> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<B8G8R8A8_UNORM> - Format traits specialization for B8G8R8A8_UNORM
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<B8G8R8A8_UNORM>
    : ComponentTraits<SWR_TYPE_UNORM, 8, SWR_TYPE_UNORM, 8, SWR_TYPE_UNORM, 8, SWR_TYPE_UNORM, 8>,
      FormatSwizzle<2, 1, 0, 3>,
      Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{32};
    static const uint32_t numComps{4};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose8_8_8_8    TransposeT;
    typedef Format4<8, 8, 8, 8> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<B8G8R8A8_UNORM_SRGB> - Format traits specialization for B8G8R8A8_UNORM_SRGB
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<B8G8R8A8_UNORM_SRGB>
    : ComponentTraits<SWR_TYPE_UNORM, 8, SWR_TYPE_UNORM, 8, SWR_TYPE_UNORM, 8, SWR_TYPE_UNORM, 8>,
      FormatSwizzle<2, 1, 0, 3>,
      Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{32};
    static const uint32_t numComps{4};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{true};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose8_8_8_8    TransposeT;
    typedef Format4<8, 8, 8, 8> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R10G10B10A2_UNORM> - Format traits specialization for R10G10B10A2_UNORM
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R10G10B10A2_UNORM> : ComponentTraits<SWR_TYPE_UNORM,
                                                         10,
                                                         SWR_TYPE_UNORM,
                                                         10,
                                                         SWR_TYPE_UNORM,
                                                         10,
                                                         SWR_TYPE_UNORM,
                                                         2>,
                                         FormatSwizzle<0, 1, 2, 3>,
                                         Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{32};
    static const uint32_t numComps{4};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose10_10_10_2    TransposeT;
    typedef Format4<10, 10, 10, 2> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R10G10B10A2_UNORM_SRGB> - Format traits specialization for R10G10B10A2_UNORM_SRGB
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R10G10B10A2_UNORM_SRGB> : ComponentTraits<SWR_TYPE_UNORM,
                                                              10,
                                                              SWR_TYPE_UNORM,
                                                              10,
                                                              SWR_TYPE_UNORM,
                                                              10,
                                                              SWR_TYPE_UNORM,
                                                              2>,
                                              FormatSwizzle<0, 1, 2, 3>,
                                              Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{32};
    static const uint32_t numComps{4};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{true};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose10_10_10_2    TransposeT;
    typedef Format4<10, 10, 10, 2> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R10G10B10A2_UINT> - Format traits specialization for R10G10B10A2_UINT
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R10G10B10A2_UINT>
    : ComponentTraits<SWR_TYPE_UINT, 10, SWR_TYPE_UINT, 10, SWR_TYPE_UINT, 10, SWR_TYPE_UINT, 2>,
      FormatSwizzle<0, 1, 2, 3>,
      Defaults<0, 0, 0, 0x1>
{
    static const uint32_t bpp{32};
    static const uint32_t numComps{4};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose10_10_10_2    TransposeT;
    typedef Format4<10, 10, 10, 2> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R8G8B8A8_UNORM> - Format traits specialization for R8G8B8A8_UNORM
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R8G8B8A8_UNORM>
    : ComponentTraits<SWR_TYPE_UNORM, 8, SWR_TYPE_UNORM, 8, SWR_TYPE_UNORM, 8, SWR_TYPE_UNORM, 8>,
      FormatSwizzle<0, 1, 2, 3>,
      Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{32};
    static const uint32_t numComps{4};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose8_8_8_8    TransposeT;
    typedef Format4<8, 8, 8, 8> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R8G8B8A8_UNORM_SRGB> - Format traits specialization for R8G8B8A8_UNORM_SRGB
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R8G8B8A8_UNORM_SRGB>
    : ComponentTraits<SWR_TYPE_UNORM, 8, SWR_TYPE_UNORM, 8, SWR_TYPE_UNORM, 8, SWR_TYPE_UNORM, 8>,
      FormatSwizzle<0, 1, 2, 3>,
      Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{32};
    static const uint32_t numComps{4};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{true};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose8_8_8_8    TransposeT;
    typedef Format4<8, 8, 8, 8> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R8G8B8A8_SNORM> - Format traits specialization for R8G8B8A8_SNORM
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R8G8B8A8_SNORM>
    : ComponentTraits<SWR_TYPE_SNORM, 8, SWR_TYPE_SNORM, 8, SWR_TYPE_SNORM, 8, SWR_TYPE_SNORM, 8>,
      FormatSwizzle<0, 1, 2, 3>,
      Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{32};
    static const uint32_t numComps{4};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose8_8_8_8    TransposeT;
    typedef Format4<8, 8, 8, 8> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R8G8B8A8_SINT> - Format traits specialization for R8G8B8A8_SINT
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R8G8B8A8_SINT>
    : ComponentTraits<SWR_TYPE_SINT, 8, SWR_TYPE_SINT, 8, SWR_TYPE_SINT, 8, SWR_TYPE_SINT, 8>,
      FormatSwizzle<0, 1, 2, 3>,
      Defaults<0, 0, 0, 0x1>
{
    static const uint32_t bpp{32};
    static const uint32_t numComps{4};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose8_8_8_8    TransposeT;
    typedef Format4<8, 8, 8, 8> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R8G8B8A8_UINT> - Format traits specialization for R8G8B8A8_UINT
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R8G8B8A8_UINT>
    : ComponentTraits<SWR_TYPE_UINT, 8, SWR_TYPE_UINT, 8, SWR_TYPE_UINT, 8, SWR_TYPE_UINT, 8>,
      FormatSwizzle<0, 1, 2, 3>,
      Defaults<0, 0, 0, 0x1>
{
    static const uint32_t bpp{32};
    static const uint32_t numComps{4};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose8_8_8_8    TransposeT;
    typedef Format4<8, 8, 8, 8> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R16G16_UNORM> - Format traits specialization for R16G16_UNORM
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R16G16_UNORM> : ComponentTraits<SWR_TYPE_UNORM, 16, SWR_TYPE_UNORM, 16>,
                                    FormatSwizzle<0, 1>,
                                    Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{32};
    static const uint32_t numComps{2};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose16_16  TransposeT;
    typedef Format2<16, 16> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R16G16_SNORM> - Format traits specialization for R16G16_SNORM
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R16G16_SNORM> : ComponentTraits<SWR_TYPE_SNORM, 16, SWR_TYPE_SNORM, 16>,
                                    FormatSwizzle<0, 1>,
                                    Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{32};
    static const uint32_t numComps{2};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose16_16  TransposeT;
    typedef Format2<16, 16> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R16G16_SINT> - Format traits specialization for R16G16_SINT
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R16G16_SINT> : ComponentTraits<SWR_TYPE_SINT, 16, SWR_TYPE_SINT, 16>,
                                   FormatSwizzle<0, 1>,
                                   Defaults<0, 0, 0, 0x1>
{
    static const uint32_t bpp{32};
    static const uint32_t numComps{2};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose16_16  TransposeT;
    typedef Format2<16, 16> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R16G16_UINT> - Format traits specialization for R16G16_UINT
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R16G16_UINT> : ComponentTraits<SWR_TYPE_UINT, 16, SWR_TYPE_UINT, 16>,
                                   FormatSwizzle<0, 1>,
                                   Defaults<0, 0, 0, 0x1>
{
    static const uint32_t bpp{32};
    static const uint32_t numComps{2};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose16_16  TransposeT;
    typedef Format2<16, 16> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R16G16_FLOAT> - Format traits specialization for R16G16_FLOAT
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R16G16_FLOAT> : ComponentTraits<SWR_TYPE_FLOAT, 16, SWR_TYPE_FLOAT, 16>,
                                    FormatSwizzle<0, 1>,
                                    Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{32};
    static const uint32_t numComps{2};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose16_16  TransposeT;
    typedef Format2<16, 16> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<B10G10R10A2_UNORM> - Format traits specialization for B10G10R10A2_UNORM
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<B10G10R10A2_UNORM> : ComponentTraits<SWR_TYPE_UNORM,
                                                         10,
                                                         SWR_TYPE_UNORM,
                                                         10,
                                                         SWR_TYPE_UNORM,
                                                         10,
                                                         SWR_TYPE_UNORM,
                                                         2>,
                                         FormatSwizzle<2, 1, 0, 3>,
                                         Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{32};
    static const uint32_t numComps{4};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose10_10_10_2    TransposeT;
    typedef Format4<10, 10, 10, 2> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<B10G10R10A2_UNORM_SRGB> - Format traits specialization for B10G10R10A2_UNORM_SRGB
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<B10G10R10A2_UNORM_SRGB> : ComponentTraits<SWR_TYPE_UNORM,
                                                              10,
                                                              SWR_TYPE_UNORM,
                                                              10,
                                                              SWR_TYPE_UNORM,
                                                              10,
                                                              SWR_TYPE_UNORM,
                                                              2>,
                                              FormatSwizzle<2, 1, 0, 3>,
                                              Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{32};
    static const uint32_t numComps{4};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{true};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose10_10_10_2    TransposeT;
    typedef Format4<10, 10, 10, 2> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R11G11B10_FLOAT> - Format traits specialization for R11G11B10_FLOAT
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R11G11B10_FLOAT>
    : ComponentTraits<SWR_TYPE_FLOAT, 11, SWR_TYPE_FLOAT, 11, SWR_TYPE_FLOAT, 10>,
      FormatSwizzle<0, 1, 2>,
      Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{32};
    static const uint32_t numComps{3};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose11_11_10   TransposeT;
    typedef Format3<11, 11, 10> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R10G10B10_FLOAT_A2_UNORM> - Format traits specialization for
/// R10G10B10_FLOAT_A2_UNORM
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R10G10B10_FLOAT_A2_UNORM> : ComponentTraits<SWR_TYPE_FLOAT,
                                                                10,
                                                                SWR_TYPE_FLOAT,
                                                                10,
                                                                SWR_TYPE_FLOAT,
                                                                10,
                                                                SWR_TYPE_UNORM,
                                                                2>,
                                                FormatSwizzle<0, 1, 2, 3>,
                                                Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{32};
    static const uint32_t numComps{4};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose10_10_10_2    TransposeT;
    typedef Format4<10, 10, 10, 2> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R32_SINT> - Format traits specialization for R32_SINT
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R32_SINT>
    : ComponentTraits<SWR_TYPE_SINT, 32>, FormatSwizzle<0>, Defaults<0, 0, 0, 0x1>
{
    static const uint32_t bpp{32};
    static const uint32_t numComps{1};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef TransposeSingleComponent<32> TransposeT;
    typedef Format1<32>                  FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R32_UINT> - Format traits specialization for R32_UINT
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R32_UINT>
    : ComponentTraits<SWR_TYPE_UINT, 32>, FormatSwizzle<0>, Defaults<0, 0, 0, 0x1>
{
    static const uint32_t bpp{32};
    static const uint32_t numComps{1};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef TransposeSingleComponent<32> TransposeT;
    typedef Format1<32>                  FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R32_FLOAT> - Format traits specialization for R32_FLOAT
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R32_FLOAT>
    : ComponentTraits<SWR_TYPE_FLOAT, 32>, FormatSwizzle<0>, Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{32};
    static const uint32_t numComps{1};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef TransposeSingleComponent<32> TransposeT;
    typedef Format1<32>                  FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R24_UNORM_X8_TYPELESS> - Format traits specialization for R24_UNORM_X8_TYPELESS
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R24_UNORM_X8_TYPELESS>
    : ComponentTraits<SWR_TYPE_UNORM, 24>, FormatSwizzle<0>, Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{32};
    static const uint32_t numComps{1};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef TransposeSingleComponent<32> TransposeT;
    typedef Format1<24>                  FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<X24_TYPELESS_G8_UINT> - Format traits specialization for X24_TYPELESS_G8_UINT
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<X24_TYPELESS_G8_UINT>
    : ComponentTraits<SWR_TYPE_UINT, 32>, FormatSwizzle<1>, Defaults<0, 0, 0, 0x1>
{
    static const uint32_t bpp{32};
    static const uint32_t numComps{1};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef TransposeSingleComponent<32> TransposeT;
    typedef Format1<32>                  FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<L32_UNORM> - Format traits specialization for L32_UNORM
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<L32_UNORM>
    : ComponentTraits<SWR_TYPE_UNORM, 32>, FormatSwizzle<0>, Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{32};
    static const uint32_t numComps{1};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef TransposeSingleComponent<32> TransposeT;
    typedef Format1<32>                  FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<L16A16_UNORM> - Format traits specialization for L16A16_UNORM
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<L16A16_UNORM> : ComponentTraits<SWR_TYPE_UNORM, 16, SWR_TYPE_UNORM, 16>,
                                    FormatSwizzle<0, 3>,
                                    Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{32};
    static const uint32_t numComps{2};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{1};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose16_16  TransposeT;
    typedef Format2<16, 16> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<I24X8_UNORM> - Format traits specialization for I24X8_UNORM
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<I24X8_UNORM> : ComponentTraits<SWR_TYPE_UNORM, 24, SWR_TYPE_UNORM, 8>,
                                   FormatSwizzle<0, 3>,
                                   Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{32};
    static const uint32_t numComps{2};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose24_8  TransposeT;
    typedef Format2<24, 8> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<L24X8_UNORM> - Format traits specialization for L24X8_UNORM
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<L24X8_UNORM> : ComponentTraits<SWR_TYPE_UNORM, 24, SWR_TYPE_UNORM, 8>,
                                   FormatSwizzle<0, 3>,
                                   Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{32};
    static const uint32_t numComps{2};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose24_8  TransposeT;
    typedef Format2<24, 8> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<I32_FLOAT> - Format traits specialization for I32_FLOAT
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<I32_FLOAT>
    : ComponentTraits<SWR_TYPE_FLOAT, 32>, FormatSwizzle<0>, Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{32};
    static const uint32_t numComps{1};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef TransposeSingleComponent<32> TransposeT;
    typedef Format1<32>                  FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<L32_FLOAT> - Format traits specialization for L32_FLOAT
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<L32_FLOAT>
    : ComponentTraits<SWR_TYPE_FLOAT, 32>, FormatSwizzle<0>, Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{32};
    static const uint32_t numComps{1};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef TransposeSingleComponent<32> TransposeT;
    typedef Format1<32>                  FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<A32_FLOAT> - Format traits specialization for A32_FLOAT
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<A32_FLOAT>
    : ComponentTraits<SWR_TYPE_FLOAT, 32>, FormatSwizzle<3>, Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{32};
    static const uint32_t numComps{1};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef TransposeSingleComponent<32> TransposeT;
    typedef Format1<32>                  FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<B8G8R8X8_UNORM> - Format traits specialization for B8G8R8X8_UNORM
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<B8G8R8X8_UNORM>
    : ComponentTraits<SWR_TYPE_UNORM, 8, SWR_TYPE_UNORM, 8, SWR_TYPE_UNORM, 8, SWR_TYPE_UNUSED, 8>,
      FormatSwizzle<2, 1, 0, 3>,
      Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{32};
    static const uint32_t numComps{3};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose8_8_8_8    TransposeT;
    typedef Format4<8, 8, 8, 8> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<B8G8R8X8_UNORM_SRGB> - Format traits specialization for B8G8R8X8_UNORM_SRGB
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<B8G8R8X8_UNORM_SRGB>
    : ComponentTraits<SWR_TYPE_UNORM, 8, SWR_TYPE_UNORM, 8, SWR_TYPE_UNORM, 8, SWR_TYPE_UNUSED, 8>,
      FormatSwizzle<2, 1, 0, 3>,
      Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{32};
    static const uint32_t numComps{3};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{true};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose8_8_8_8    TransposeT;
    typedef Format4<8, 8, 8, 8> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R8G8B8X8_UNORM> - Format traits specialization for R8G8B8X8_UNORM
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R8G8B8X8_UNORM>
    : ComponentTraits<SWR_TYPE_UNORM, 8, SWR_TYPE_UNORM, 8, SWR_TYPE_UNORM, 8, SWR_TYPE_UNUSED, 8>,
      FormatSwizzle<0, 1, 2, 3>,
      Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{32};
    static const uint32_t numComps{3};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose8_8_8_8    TransposeT;
    typedef Format4<8, 8, 8, 8> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R8G8B8X8_UNORM_SRGB> - Format traits specialization for R8G8B8X8_UNORM_SRGB
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R8G8B8X8_UNORM_SRGB>
    : ComponentTraits<SWR_TYPE_UNORM, 8, SWR_TYPE_UNORM, 8, SWR_TYPE_UNORM, 8, SWR_TYPE_UNUSED, 8>,
      FormatSwizzle<0, 1, 2, 3>,
      Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{32};
    static const uint32_t numComps{3};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{true};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose8_8_8_8    TransposeT;
    typedef Format4<8, 8, 8, 8> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R9G9B9E5_SHAREDEXP> - Format traits specialization for R9G9B9E5_SHAREDEXP
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R9G9B9E5_SHAREDEXP>
    : ComponentTraits<SWR_TYPE_UINT, 9, SWR_TYPE_UINT, 9, SWR_TYPE_UINT, 9, SWR_TYPE_UINT, 5>,
      FormatSwizzle<0, 1, 2, 3>,
      Defaults<0, 0, 0, 0x1>
{
    static const uint32_t bpp{32};
    static const uint32_t numComps{4};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose9_9_9_5    TransposeT;
    typedef Format4<9, 9, 9, 5> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<B10G10R10X2_UNORM> - Format traits specialization for B10G10R10X2_UNORM
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<B10G10R10X2_UNORM> : ComponentTraits<SWR_TYPE_UNORM,
                                                         10,
                                                         SWR_TYPE_UNORM,
                                                         10,
                                                         SWR_TYPE_UNORM,
                                                         10,
                                                         SWR_TYPE_UNUSED,
                                                         2>,
                                         FormatSwizzle<2, 1, 0, 3>,
                                         Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{32};
    static const uint32_t numComps{3};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose10_10_10_2    TransposeT;
    typedef Format4<10, 10, 10, 2> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<L16A16_FLOAT> - Format traits specialization for L16A16_FLOAT
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<L16A16_FLOAT> : ComponentTraits<SWR_TYPE_FLOAT, 16, SWR_TYPE_FLOAT, 16>,
                                    FormatSwizzle<0, 3>,
                                    Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{32};
    static const uint32_t numComps{2};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{1};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose16_16  TransposeT;
    typedef Format2<16, 16> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R10G10B10X2_USCALED> - Format traits specialization for R10G10B10X2_USCALED
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R10G10B10X2_USCALED> : ComponentTraits<SWR_TYPE_USCALED,
                                                           10,
                                                           SWR_TYPE_USCALED,
                                                           10,
                                                           SWR_TYPE_USCALED,
                                                           10,
                                                           SWR_TYPE_UNUSED,
                                                           2>,
                                           FormatSwizzle<0, 1, 2, 3>,
                                           Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{32};
    static const uint32_t numComps{3};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose10_10_10_2    TransposeT;
    typedef Format4<10, 10, 10, 2> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R8G8B8A8_SSCALED> - Format traits specialization for R8G8B8A8_SSCALED
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R8G8B8A8_SSCALED> : ComponentTraits<SWR_TYPE_SSCALED,
                                                        8,
                                                        SWR_TYPE_SSCALED,
                                                        8,
                                                        SWR_TYPE_SSCALED,
                                                        8,
                                                        SWR_TYPE_SSCALED,
                                                        8>,
                                        FormatSwizzle<0, 1, 2, 3>,
                                        Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{32};
    static const uint32_t numComps{4};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose8_8_8_8    TransposeT;
    typedef Format4<8, 8, 8, 8> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R8G8B8A8_USCALED> - Format traits specialization for R8G8B8A8_USCALED
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R8G8B8A8_USCALED> : ComponentTraits<SWR_TYPE_USCALED,
                                                        8,
                                                        SWR_TYPE_USCALED,
                                                        8,
                                                        SWR_TYPE_USCALED,
                                                        8,
                                                        SWR_TYPE_USCALED,
                                                        8>,
                                        FormatSwizzle<0, 1, 2, 3>,
                                        Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{32};
    static const uint32_t numComps{4};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose8_8_8_8    TransposeT;
    typedef Format4<8, 8, 8, 8> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R16G16_SSCALED> - Format traits specialization for R16G16_SSCALED
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R16G16_SSCALED> : ComponentTraits<SWR_TYPE_SSCALED, 16, SWR_TYPE_SSCALED, 16>,
                                      FormatSwizzle<0, 1>,
                                      Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{32};
    static const uint32_t numComps{2};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose16_16  TransposeT;
    typedef Format2<16, 16> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R16G16_USCALED> - Format traits specialization for R16G16_USCALED
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R16G16_USCALED> : ComponentTraits<SWR_TYPE_USCALED, 16, SWR_TYPE_USCALED, 16>,
                                      FormatSwizzle<0, 1>,
                                      Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{32};
    static const uint32_t numComps{2};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose16_16  TransposeT;
    typedef Format2<16, 16> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R32_SSCALED> - Format traits specialization for R32_SSCALED
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R32_SSCALED>
    : ComponentTraits<SWR_TYPE_SSCALED, 32>, FormatSwizzle<0>, Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{32};
    static const uint32_t numComps{1};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef TransposeSingleComponent<32> TransposeT;
    typedef Format1<32>                  FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R32_USCALED> - Format traits specialization for R32_USCALED
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R32_USCALED>
    : ComponentTraits<SWR_TYPE_USCALED, 32>, FormatSwizzle<0>, Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{32};
    static const uint32_t numComps{1};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef TransposeSingleComponent<32> TransposeT;
    typedef Format1<32>                  FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<B5G6R5_UNORM> - Format traits specialization for B5G6R5_UNORM
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<B5G6R5_UNORM>
    : ComponentTraits<SWR_TYPE_UNORM, 5, SWR_TYPE_UNORM, 6, SWR_TYPE_UNORM, 5>,
      FormatSwizzle<2, 1, 0>,
      Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{16};
    static const uint32_t numComps{3};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose5_6_5   TransposeT;
    typedef Format3<5, 6, 5> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<B5G6R5_UNORM_SRGB> - Format traits specialization for B5G6R5_UNORM_SRGB
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<B5G6R5_UNORM_SRGB>
    : ComponentTraits<SWR_TYPE_UNORM, 5, SWR_TYPE_UNORM, 6, SWR_TYPE_UNORM, 5>,
      FormatSwizzle<2, 1, 0>,
      Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{16};
    static const uint32_t numComps{3};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{true};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose5_6_5   TransposeT;
    typedef Format3<5, 6, 5> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<B5G5R5A1_UNORM> - Format traits specialization for B5G5R5A1_UNORM
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<B5G5R5A1_UNORM>
    : ComponentTraits<SWR_TYPE_UNORM, 5, SWR_TYPE_UNORM, 5, SWR_TYPE_UNORM, 5, SWR_TYPE_UNORM, 1>,
      FormatSwizzle<2, 1, 0, 3>,
      Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{16};
    static const uint32_t numComps{4};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose5_5_5_1    TransposeT;
    typedef Format4<5, 5, 5, 1> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<B5G5R5A1_UNORM_SRGB> - Format traits specialization for B5G5R5A1_UNORM_SRGB
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<B5G5R5A1_UNORM_SRGB>
    : ComponentTraits<SWR_TYPE_UNORM, 5, SWR_TYPE_UNORM, 5, SWR_TYPE_UNORM, 5, SWR_TYPE_UNORM, 1>,
      FormatSwizzle<2, 1, 0, 3>,
      Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{16};
    static const uint32_t numComps{4};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{true};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose5_5_5_1    TransposeT;
    typedef Format4<5, 5, 5, 1> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<B4G4R4A4_UNORM> - Format traits specialization for B4G4R4A4_UNORM
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<B4G4R4A4_UNORM>
    : ComponentTraits<SWR_TYPE_UNORM, 4, SWR_TYPE_UNORM, 4, SWR_TYPE_UNORM, 4, SWR_TYPE_UNORM, 4>,
      FormatSwizzle<2, 1, 0, 3>,
      Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{16};
    static const uint32_t numComps{4};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose4_4_4_4    TransposeT;
    typedef Format4<4, 4, 4, 4> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<B4G4R4A4_UNORM_SRGB> - Format traits specialization for B4G4R4A4_UNORM_SRGB
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<B4G4R4A4_UNORM_SRGB>
    : ComponentTraits<SWR_TYPE_UNORM, 4, SWR_TYPE_UNORM, 4, SWR_TYPE_UNORM, 4, SWR_TYPE_UNORM, 4>,
      FormatSwizzle<2, 1, 0, 3>,
      Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{16};
    static const uint32_t numComps{4};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{true};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose4_4_4_4    TransposeT;
    typedef Format4<4, 4, 4, 4> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R8G8_UNORM> - Format traits specialization for R8G8_UNORM
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R8G8_UNORM> : ComponentTraits<SWR_TYPE_UNORM, 8, SWR_TYPE_UNORM, 8>,
                                  FormatSwizzle<0, 1>,
                                  Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{16};
    static const uint32_t numComps{2};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose8_8  TransposeT;
    typedef Format2<8, 8> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R8G8_SNORM> - Format traits specialization for R8G8_SNORM
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R8G8_SNORM> : ComponentTraits<SWR_TYPE_SNORM, 8, SWR_TYPE_SNORM, 8>,
                                  FormatSwizzle<0, 1>,
                                  Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{16};
    static const uint32_t numComps{2};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose8_8  TransposeT;
    typedef Format2<8, 8> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R8G8_SINT> - Format traits specialization for R8G8_SINT
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R8G8_SINT> : ComponentTraits<SWR_TYPE_SINT, 8, SWR_TYPE_SINT, 8>,
                                 FormatSwizzle<0, 1>,
                                 Defaults<0, 0, 0, 0x1>
{
    static const uint32_t bpp{16};
    static const uint32_t numComps{2};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose8_8  TransposeT;
    typedef Format2<8, 8> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R8G8_UINT> - Format traits specialization for R8G8_UINT
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R8G8_UINT> : ComponentTraits<SWR_TYPE_UINT, 8, SWR_TYPE_UINT, 8>,
                                 FormatSwizzle<0, 1>,
                                 Defaults<0, 0, 0, 0x1>
{
    static const uint32_t bpp{16};
    static const uint32_t numComps{2};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose8_8  TransposeT;
    typedef Format2<8, 8> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R16_UNORM> - Format traits specialization for R16_UNORM
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R16_UNORM>
    : ComponentTraits<SWR_TYPE_UNORM, 16>, FormatSwizzle<0>, Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{16};
    static const uint32_t numComps{1};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef TransposeSingleComponent<16> TransposeT;
    typedef Format1<16>                  FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R16_SNORM> - Format traits specialization for R16_SNORM
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R16_SNORM>
    : ComponentTraits<SWR_TYPE_SNORM, 16>, FormatSwizzle<0>, Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{16};
    static const uint32_t numComps{1};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef TransposeSingleComponent<16> TransposeT;
    typedef Format1<16>                  FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R16_SINT> - Format traits specialization for R16_SINT
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R16_SINT>
    : ComponentTraits<SWR_TYPE_SINT, 16>, FormatSwizzle<0>, Defaults<0, 0, 0, 0x1>
{
    static const uint32_t bpp{16};
    static const uint32_t numComps{1};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef TransposeSingleComponent<16> TransposeT;
    typedef Format1<16>                  FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R16_UINT> - Format traits specialization for R16_UINT
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R16_UINT>
    : ComponentTraits<SWR_TYPE_UINT, 16>, FormatSwizzle<0>, Defaults<0, 0, 0, 0x1>
{
    static const uint32_t bpp{16};
    static const uint32_t numComps{1};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef TransposeSingleComponent<16> TransposeT;
    typedef Format1<16>                  FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R16_FLOAT> - Format traits specialization for R16_FLOAT
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R16_FLOAT>
    : ComponentTraits<SWR_TYPE_FLOAT, 16>, FormatSwizzle<0>, Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{16};
    static const uint32_t numComps{1};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef TransposeSingleComponent<16> TransposeT;
    typedef Format1<16>                  FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<I16_UNORM> - Format traits specialization for I16_UNORM
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<I16_UNORM>
    : ComponentTraits<SWR_TYPE_UNORM, 16>, FormatSwizzle<0>, Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{16};
    static const uint32_t numComps{1};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef TransposeSingleComponent<16> TransposeT;
    typedef Format1<16>                  FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<L16_UNORM> - Format traits specialization for L16_UNORM
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<L16_UNORM>
    : ComponentTraits<SWR_TYPE_UNORM, 16>, FormatSwizzle<0>, Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{16};
    static const uint32_t numComps{1};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef TransposeSingleComponent<16> TransposeT;
    typedef Format1<16>                  FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<A16_UNORM> - Format traits specialization for A16_UNORM
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<A16_UNORM>
    : ComponentTraits<SWR_TYPE_UNORM, 16>, FormatSwizzle<3>, Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{16};
    static const uint32_t numComps{1};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef TransposeSingleComponent<16> TransposeT;
    typedef Format1<16>                  FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<L8A8_UNORM> - Format traits specialization for L8A8_UNORM
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<L8A8_UNORM> : ComponentTraits<SWR_TYPE_UNORM, 8, SWR_TYPE_UNORM, 8>,
                                  FormatSwizzle<0, 3>,
                                  Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{16};
    static const uint32_t numComps{2};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{1};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose8_8  TransposeT;
    typedef Format2<8, 8> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<I16_FLOAT> - Format traits specialization for I16_FLOAT
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<I16_FLOAT>
    : ComponentTraits<SWR_TYPE_FLOAT, 16>, FormatSwizzle<0>, Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{16};
    static const uint32_t numComps{1};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef TransposeSingleComponent<16> TransposeT;
    typedef Format1<16>                  FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<L16_FLOAT> - Format traits specialization for L16_FLOAT
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<L16_FLOAT>
    : ComponentTraits<SWR_TYPE_FLOAT, 16>, FormatSwizzle<0>, Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{16};
    static const uint32_t numComps{1};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef TransposeSingleComponent<16> TransposeT;
    typedef Format1<16>                  FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<A16_FLOAT> - Format traits specialization for A16_FLOAT
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<A16_FLOAT>
    : ComponentTraits<SWR_TYPE_FLOAT, 16>, FormatSwizzle<3>, Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{16};
    static const uint32_t numComps{1};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef TransposeSingleComponent<16> TransposeT;
    typedef Format1<16>                  FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<L8A8_UNORM_SRGB> - Format traits specialization for L8A8_UNORM_SRGB
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<L8A8_UNORM_SRGB> : ComponentTraits<SWR_TYPE_UNORM, 8, SWR_TYPE_UNORM, 8>,
                                       FormatSwizzle<0, 3>,
                                       Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{16};
    static const uint32_t numComps{2};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{1};
    static const bool     isSRGB{true};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose8_8  TransposeT;
    typedef Format2<8, 8> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<B5G5R5X1_UNORM> - Format traits specialization for B5G5R5X1_UNORM
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<B5G5R5X1_UNORM>
    : ComponentTraits<SWR_TYPE_UNORM, 5, SWR_TYPE_UNORM, 5, SWR_TYPE_UNORM, 5, SWR_TYPE_UNUSED, 1>,
      FormatSwizzle<2, 1, 0, 3>,
      Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{16};
    static const uint32_t numComps{3};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose5_5_5_1    TransposeT;
    typedef Format4<5, 5, 5, 1> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<B5G5R5X1_UNORM_SRGB> - Format traits specialization for B5G5R5X1_UNORM_SRGB
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<B5G5R5X1_UNORM_SRGB>
    : ComponentTraits<SWR_TYPE_UNORM, 5, SWR_TYPE_UNORM, 5, SWR_TYPE_UNORM, 5, SWR_TYPE_UNUSED, 1>,
      FormatSwizzle<2, 1, 0, 3>,
      Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{16};
    static const uint32_t numComps{3};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{true};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose5_5_5_1    TransposeT;
    typedef Format4<5, 5, 5, 1> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R8G8_SSCALED> - Format traits specialization for R8G8_SSCALED
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R8G8_SSCALED> : ComponentTraits<SWR_TYPE_SSCALED, 8, SWR_TYPE_SSCALED, 8>,
                                    FormatSwizzle<0, 1>,
                                    Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{16};
    static const uint32_t numComps{2};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose8_8  TransposeT;
    typedef Format2<8, 8> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R8G8_USCALED> - Format traits specialization for R8G8_USCALED
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R8G8_USCALED> : ComponentTraits<SWR_TYPE_USCALED, 8, SWR_TYPE_USCALED, 8>,
                                    FormatSwizzle<0, 1>,
                                    Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{16};
    static const uint32_t numComps{2};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose8_8  TransposeT;
    typedef Format2<8, 8> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R16_SSCALED> - Format traits specialization for R16_SSCALED
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R16_SSCALED>
    : ComponentTraits<SWR_TYPE_SSCALED, 16>, FormatSwizzle<0>, Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{16};
    static const uint32_t numComps{1};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef TransposeSingleComponent<16> TransposeT;
    typedef Format1<16>                  FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R16_USCALED> - Format traits specialization for R16_USCALED
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R16_USCALED>
    : ComponentTraits<SWR_TYPE_USCALED, 16>, FormatSwizzle<0>, Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{16};
    static const uint32_t numComps{1};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef TransposeSingleComponent<16> TransposeT;
    typedef Format1<16>                  FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<A1B5G5R5_UNORM> - Format traits specialization for A1B5G5R5_UNORM
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<A1B5G5R5_UNORM>
    : ComponentTraits<SWR_TYPE_UNORM, 1, SWR_TYPE_UNORM, 5, SWR_TYPE_UNORM, 5, SWR_TYPE_UNORM, 5>,
      FormatSwizzle<3, 2, 1, 0>,
      Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{16};
    static const uint32_t numComps{4};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose1_5_5_5    TransposeT;
    typedef Format4<1, 5, 5, 5> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<A4B4G4R4_UNORM> - Format traits specialization for A4B4G4R4_UNORM
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<A4B4G4R4_UNORM>
    : ComponentTraits<SWR_TYPE_UNORM, 4, SWR_TYPE_UNORM, 4, SWR_TYPE_UNORM, 4, SWR_TYPE_UNORM, 4>,
      FormatSwizzle<3, 2, 1, 0>,
      Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{16};
    static const uint32_t numComps{4};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose4_4_4_4    TransposeT;
    typedef Format4<4, 4, 4, 4> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<L8A8_UINT> - Format traits specialization for L8A8_UINT
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<L8A8_UINT> : ComponentTraits<SWR_TYPE_UINT, 8, SWR_TYPE_UINT, 8>,
                                 FormatSwizzle<0, 3>,
                                 Defaults<0, 0, 0, 0x1>
{
    static const uint32_t bpp{16};
    static const uint32_t numComps{2};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{1};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose8_8  TransposeT;
    typedef Format2<8, 8> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<L8A8_SINT> - Format traits specialization for L8A8_SINT
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<L8A8_SINT> : ComponentTraits<SWR_TYPE_SINT, 8, SWR_TYPE_SINT, 8>,
                                 FormatSwizzle<0, 3>,
                                 Defaults<0, 0, 0, 0x1>
{
    static const uint32_t bpp{16};
    static const uint32_t numComps{2};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{1};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose8_8  TransposeT;
    typedef Format2<8, 8> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R8_UNORM> - Format traits specialization for R8_UNORM
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R8_UNORM>
    : ComponentTraits<SWR_TYPE_UNORM, 8>, FormatSwizzle<0>, Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{8};
    static const uint32_t numComps{1};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef TransposeSingleComponent<8> TransposeT;
    typedef Format1<8>                  FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R8_SNORM> - Format traits specialization for R8_SNORM
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R8_SNORM>
    : ComponentTraits<SWR_TYPE_SNORM, 8>, FormatSwizzle<0>, Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{8};
    static const uint32_t numComps{1};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef TransposeSingleComponent<8> TransposeT;
    typedef Format1<8>                  FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R8_SINT> - Format traits specialization for R8_SINT
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R8_SINT>
    : ComponentTraits<SWR_TYPE_SINT, 8>, FormatSwizzle<0>, Defaults<0, 0, 0, 0x1>
{
    static const uint32_t bpp{8};
    static const uint32_t numComps{1};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef TransposeSingleComponent<8> TransposeT;
    typedef Format1<8>                  FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R8_UINT> - Format traits specialization for R8_UINT
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R8_UINT>
    : ComponentTraits<SWR_TYPE_UINT, 8>, FormatSwizzle<0>, Defaults<0, 0, 0, 0x1>
{
    static const uint32_t bpp{8};
    static const uint32_t numComps{1};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef TransposeSingleComponent<8> TransposeT;
    typedef Format1<8>                  FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<A8_UNORM> - Format traits specialization for A8_UNORM
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<A8_UNORM>
    : ComponentTraits<SWR_TYPE_UNORM, 8>, FormatSwizzle<3>, Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{8};
    static const uint32_t numComps{1};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef TransposeSingleComponent<8> TransposeT;
    typedef Format1<8>                  FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<I8_UNORM> - Format traits specialization for I8_UNORM
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<I8_UNORM>
    : ComponentTraits<SWR_TYPE_UNORM, 8>, FormatSwizzle<0>, Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{8};
    static const uint32_t numComps{1};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef TransposeSingleComponent<8> TransposeT;
    typedef Format1<8>                  FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<L8_UNORM> - Format traits specialization for L8_UNORM
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<L8_UNORM>
    : ComponentTraits<SWR_TYPE_UNORM, 8>, FormatSwizzle<0>, Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{8};
    static const uint32_t numComps{1};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef TransposeSingleComponent<8> TransposeT;
    typedef Format1<8>                  FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R8_SSCALED> - Format traits specialization for R8_SSCALED
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R8_SSCALED>
    : ComponentTraits<SWR_TYPE_SSCALED, 8>, FormatSwizzle<0>, Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{8};
    static const uint32_t numComps{1};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef TransposeSingleComponent<8> TransposeT;
    typedef Format1<8>                  FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R8_USCALED> - Format traits specialization for R8_USCALED
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R8_USCALED>
    : ComponentTraits<SWR_TYPE_USCALED, 8>, FormatSwizzle<0>, Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{8};
    static const uint32_t numComps{1};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef TransposeSingleComponent<8> TransposeT;
    typedef Format1<8>                  FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<L8_UNORM_SRGB> - Format traits specialization for L8_UNORM_SRGB
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<L8_UNORM_SRGB>
    : ComponentTraits<SWR_TYPE_UNORM, 8>, FormatSwizzle<0>, Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{8};
    static const uint32_t numComps{1};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{true};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef TransposeSingleComponent<8> TransposeT;
    typedef Format1<8>                  FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<L8_UINT> - Format traits specialization for L8_UINT
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<L8_UINT>
    : ComponentTraits<SWR_TYPE_UINT, 8>, FormatSwizzle<0>, Defaults<0, 0, 0, 0x1>
{
    static const uint32_t bpp{8};
    static const uint32_t numComps{1};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef TransposeSingleComponent<8> TransposeT;
    typedef Format1<8>                  FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<L8_SINT> - Format traits specialization for L8_SINT
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<L8_SINT>
    : ComponentTraits<SWR_TYPE_SINT, 8>, FormatSwizzle<0>, Defaults<0, 0, 0, 0x1>
{
    static const uint32_t bpp{8};
    static const uint32_t numComps{1};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef TransposeSingleComponent<8> TransposeT;
    typedef Format1<8>                  FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<I8_UINT> - Format traits specialization for I8_UINT
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<I8_UINT>
    : ComponentTraits<SWR_TYPE_UINT, 8>, FormatSwizzle<0>, Defaults<0, 0, 0, 0x1>
{
    static const uint32_t bpp{8};
    static const uint32_t numComps{1};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef TransposeSingleComponent<8> TransposeT;
    typedef Format1<8>                  FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<I8_SINT> - Format traits specialization for I8_SINT
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<I8_SINT>
    : ComponentTraits<SWR_TYPE_SINT, 8>, FormatSwizzle<0>, Defaults<0, 0, 0, 0x1>
{
    static const uint32_t bpp{8};
    static const uint32_t numComps{1};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef TransposeSingleComponent<8> TransposeT;
    typedef Format1<8>                  FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<DXT1_RGB_SRGB> - Format traits specialization for DXT1_RGB_SRGB
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<DXT1_RGB_SRGB>
    : ComponentTraits<SWR_TYPE_UNORM, 8>, FormatSwizzle<0>, Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{64};
    static const uint32_t numComps{1};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{false};
    static const bool     isBC{true};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{4};
    static const uint32_t bcHeight{4};

    typedef TransposeSingleComponent<8> TransposeT;
    typedef Format1<8>                  FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<YCRCB_SWAPUVY> - Format traits specialization for YCRCB_SWAPUVY
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<YCRCB_SWAPUVY>
    : ComponentTraits<SWR_TYPE_UINT, 8, SWR_TYPE_UINT, 8, SWR_TYPE_UINT, 8, SWR_TYPE_UINT, 8>,
      FormatSwizzle<0, 1, 2, 3>,
      Defaults<0, 0, 0, 0x1>
{
    static const uint32_t bpp{32};
    static const uint32_t numComps{4};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{true};
    static const uint32_t bcWidth{2};
    static const uint32_t bcHeight{1};

    typedef Transpose8_8_8_8    TransposeT;
    typedef Format4<8, 8, 8, 8> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<BC1_UNORM> - Format traits specialization for BC1_UNORM
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<BC1_UNORM>
    : ComponentTraits<SWR_TYPE_UNORM, 8>, FormatSwizzle<0>, Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{64};
    static const uint32_t numComps{1};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{false};
    static const bool     isBC{true};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{4};
    static const uint32_t bcHeight{4};

    typedef TransposeSingleComponent<8> TransposeT;
    typedef Format1<8>                  FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<BC2_UNORM> - Format traits specialization for BC2_UNORM
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<BC2_UNORM>
    : ComponentTraits<SWR_TYPE_UNORM, 8>, FormatSwizzle<0>, Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{128};
    static const uint32_t numComps{1};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{false};
    static const bool     isBC{true};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{4};
    static const uint32_t bcHeight{4};

    typedef TransposeSingleComponent<8> TransposeT;
    typedef Format1<8>                  FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<BC3_UNORM> - Format traits specialization for BC3_UNORM
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<BC3_UNORM>
    : ComponentTraits<SWR_TYPE_UNORM, 8>, FormatSwizzle<0>, Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{128};
    static const uint32_t numComps{1};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{false};
    static const bool     isBC{true};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{4};
    static const uint32_t bcHeight{4};

    typedef TransposeSingleComponent<8> TransposeT;
    typedef Format1<8>                  FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<BC4_UNORM> - Format traits specialization for BC4_UNORM
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<BC4_UNORM>
    : ComponentTraits<SWR_TYPE_UNORM, 8>, FormatSwizzle<0>, Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{64};
    static const uint32_t numComps{1};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{false};
    static const bool     isBC{true};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{4};
    static const uint32_t bcHeight{4};

    typedef TransposeSingleComponent<8> TransposeT;
    typedef Format1<8>                  FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<BC5_UNORM> - Format traits specialization for BC5_UNORM
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<BC5_UNORM>
    : ComponentTraits<SWR_TYPE_UNORM, 8>, FormatSwizzle<0>, Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{128};
    static const uint32_t numComps{1};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{false};
    static const bool     isBC{true};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{4};
    static const uint32_t bcHeight{4};

    typedef TransposeSingleComponent<8> TransposeT;
    typedef Format1<8>                  FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<BC1_UNORM_SRGB> - Format traits specialization for BC1_UNORM_SRGB
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<BC1_UNORM_SRGB>
    : ComponentTraits<SWR_TYPE_UNORM, 8>, FormatSwizzle<0>, Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{64};
    static const uint32_t numComps{1};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{true};
    static const bool     isBC{true};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{4};
    static const uint32_t bcHeight{4};

    typedef TransposeSingleComponent<8> TransposeT;
    typedef Format1<8>                  FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<BC2_UNORM_SRGB> - Format traits specialization for BC2_UNORM_SRGB
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<BC2_UNORM_SRGB>
    : ComponentTraits<SWR_TYPE_UNORM, 8>, FormatSwizzle<0>, Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{128};
    static const uint32_t numComps{1};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{true};
    static const bool     isBC{true};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{4};
    static const uint32_t bcHeight{4};

    typedef TransposeSingleComponent<8> TransposeT;
    typedef Format1<8>                  FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<BC3_UNORM_SRGB> - Format traits specialization for BC3_UNORM_SRGB
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<BC3_UNORM_SRGB>
    : ComponentTraits<SWR_TYPE_UNORM, 8>, FormatSwizzle<0>, Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{128};
    static const uint32_t numComps{1};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{true};
    static const bool     isBC{true};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{4};
    static const uint32_t bcHeight{4};

    typedef TransposeSingleComponent<8> TransposeT;
    typedef Format1<8>                  FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<YCRCB_SWAPUV> - Format traits specialization for YCRCB_SWAPUV
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<YCRCB_SWAPUV>
    : ComponentTraits<SWR_TYPE_UINT, 8, SWR_TYPE_UINT, 8, SWR_TYPE_UINT, 8, SWR_TYPE_UINT, 8>,
      FormatSwizzle<0, 1, 2, 3>,
      Defaults<0, 0, 0, 0x1>
{
    static const uint32_t bpp{32};
    static const uint32_t numComps{4};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{true};
    static const uint32_t bcWidth{2};
    static const uint32_t bcHeight{1};

    typedef Transpose8_8_8_8    TransposeT;
    typedef Format4<8, 8, 8, 8> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<DXT1_RGB> - Format traits specialization for DXT1_RGB
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<DXT1_RGB>
    : ComponentTraits<SWR_TYPE_UNORM, 8>, FormatSwizzle<0>, Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{64};
    static const uint32_t numComps{1};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{false};
    static const bool     isBC{true};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{4};
    static const uint32_t bcHeight{4};

    typedef TransposeSingleComponent<8> TransposeT;
    typedef Format1<8>                  FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R8G8B8_UNORM> - Format traits specialization for R8G8B8_UNORM
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R8G8B8_UNORM>
    : ComponentTraits<SWR_TYPE_UNORM, 8, SWR_TYPE_UNORM, 8, SWR_TYPE_UNORM, 8>,
      FormatSwizzle<0, 1, 2>,
      Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{24};
    static const uint32_t numComps{3};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose8_8_8   TransposeT;
    typedef Format3<8, 8, 8> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R8G8B8_SNORM> - Format traits specialization for R8G8B8_SNORM
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R8G8B8_SNORM>
    : ComponentTraits<SWR_TYPE_SNORM, 8, SWR_TYPE_SNORM, 8, SWR_TYPE_SNORM, 8>,
      FormatSwizzle<0, 1, 2>,
      Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{24};
    static const uint32_t numComps{3};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose8_8_8   TransposeT;
    typedef Format3<8, 8, 8> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R8G8B8_SSCALED> - Format traits specialization for R8G8B8_SSCALED
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R8G8B8_SSCALED>
    : ComponentTraits<SWR_TYPE_SSCALED, 8, SWR_TYPE_SSCALED, 8, SWR_TYPE_SSCALED, 8>,
      FormatSwizzle<0, 1, 2>,
      Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{24};
    static const uint32_t numComps{3};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose8_8_8   TransposeT;
    typedef Format3<8, 8, 8> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R8G8B8_USCALED> - Format traits specialization for R8G8B8_USCALED
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R8G8B8_USCALED>
    : ComponentTraits<SWR_TYPE_USCALED, 8, SWR_TYPE_USCALED, 8, SWR_TYPE_USCALED, 8>,
      FormatSwizzle<0, 1, 2>,
      Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{24};
    static const uint32_t numComps{3};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose8_8_8   TransposeT;
    typedef Format3<8, 8, 8> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R64G64B64A64_FLOAT> - Format traits specialization for R64G64B64A64_FLOAT
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R64G64B64A64_FLOAT> : ComponentTraits<SWR_TYPE_FLOAT,
                                                          64,
                                                          SWR_TYPE_FLOAT,
                                                          64,
                                                          SWR_TYPE_FLOAT,
                                                          64,
                                                          SWR_TYPE_FLOAT,
                                                          64>,
                                          FormatSwizzle<0, 1, 2, 3>,
                                          Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{256};
    static const uint32_t numComps{4};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose64_64_64_64    TransposeT;
    typedef Format4<64, 64, 64, 64> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R64G64B64_FLOAT> - Format traits specialization for R64G64B64_FLOAT
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R64G64B64_FLOAT>
    : ComponentTraits<SWR_TYPE_FLOAT, 64, SWR_TYPE_FLOAT, 64, SWR_TYPE_FLOAT, 64>,
      FormatSwizzle<0, 1, 2>,
      Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{192};
    static const uint32_t numComps{3};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose64_64_64   TransposeT;
    typedef Format3<64, 64, 64> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<BC4_SNORM> - Format traits specialization for BC4_SNORM
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<BC4_SNORM>
    : ComponentTraits<SWR_TYPE_SNORM, 8>, FormatSwizzle<0>, Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{64};
    static const uint32_t numComps{1};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{false};
    static const bool     isBC{true};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{4};
    static const uint32_t bcHeight{4};

    typedef TransposeSingleComponent<8> TransposeT;
    typedef Format1<8>                  FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<BC5_SNORM> - Format traits specialization for BC5_SNORM
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<BC5_SNORM>
    : ComponentTraits<SWR_TYPE_SNORM, 8>, FormatSwizzle<0>, Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{128};
    static const uint32_t numComps{1};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{false};
    static const bool     isBC{true};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{4};
    static const uint32_t bcHeight{4};

    typedef TransposeSingleComponent<8> TransposeT;
    typedef Format1<8>                  FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R16G16B16_FLOAT> - Format traits specialization for R16G16B16_FLOAT
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R16G16B16_FLOAT>
    : ComponentTraits<SWR_TYPE_FLOAT, 16, SWR_TYPE_FLOAT, 16, SWR_TYPE_FLOAT, 16>,
      FormatSwizzle<0, 1, 2>,
      Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{48};
    static const uint32_t numComps{3};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose16_16_16   TransposeT;
    typedef Format3<16, 16, 16> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R16G16B16_UNORM> - Format traits specialization for R16G16B16_UNORM
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R16G16B16_UNORM>
    : ComponentTraits<SWR_TYPE_UNORM, 16, SWR_TYPE_UNORM, 16, SWR_TYPE_UNORM, 16>,
      FormatSwizzle<0, 1, 2>,
      Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{48};
    static const uint32_t numComps{3};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose16_16_16   TransposeT;
    typedef Format3<16, 16, 16> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R16G16B16_SNORM> - Format traits specialization for R16G16B16_SNORM
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R16G16B16_SNORM>
    : ComponentTraits<SWR_TYPE_SNORM, 16, SWR_TYPE_SNORM, 16, SWR_TYPE_SNORM, 16>,
      FormatSwizzle<0, 1, 2>,
      Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{48};
    static const uint32_t numComps{3};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose16_16_16   TransposeT;
    typedef Format3<16, 16, 16> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R16G16B16_SSCALED> - Format traits specialization for R16G16B16_SSCALED
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R16G16B16_SSCALED>
    : ComponentTraits<SWR_TYPE_SSCALED, 16, SWR_TYPE_SSCALED, 16, SWR_TYPE_SSCALED, 16>,
      FormatSwizzle<0, 1, 2>,
      Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{48};
    static const uint32_t numComps{3};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose16_16_16   TransposeT;
    typedef Format3<16, 16, 16> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R16G16B16_USCALED> - Format traits specialization for R16G16B16_USCALED
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R16G16B16_USCALED>
    : ComponentTraits<SWR_TYPE_USCALED, 16, SWR_TYPE_USCALED, 16, SWR_TYPE_USCALED, 16>,
      FormatSwizzle<0, 1, 2>,
      Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{48};
    static const uint32_t numComps{3};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose16_16_16   TransposeT;
    typedef Format3<16, 16, 16> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<BC6H_SF16> - Format traits specialization for BC6H_SF16
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<BC6H_SF16>
    : ComponentTraits<SWR_TYPE_SNORM, 8>, FormatSwizzle<0>, Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{128};
    static const uint32_t numComps{1};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{false};
    static const bool     isBC{true};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{4};
    static const uint32_t bcHeight{4};

    typedef TransposeSingleComponent<8> TransposeT;
    typedef Format1<8>                  FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<BC7_UNORM> - Format traits specialization for BC7_UNORM
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<BC7_UNORM>
    : ComponentTraits<SWR_TYPE_UNORM, 8>, FormatSwizzle<0>, Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{128};
    static const uint32_t numComps{1};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{false};
    static const bool     isBC{true};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{4};
    static const uint32_t bcHeight{4};

    typedef TransposeSingleComponent<8> TransposeT;
    typedef Format1<8>                  FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<BC7_UNORM_SRGB> - Format traits specialization for BC7_UNORM_SRGB
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<BC7_UNORM_SRGB>
    : ComponentTraits<SWR_TYPE_UNORM, 8>, FormatSwizzle<0>, Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{128};
    static const uint32_t numComps{1};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{true};
    static const bool     isBC{true};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{4};
    static const uint32_t bcHeight{4};

    typedef TransposeSingleComponent<8> TransposeT;
    typedef Format1<8>                  FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<BC6H_UF16> - Format traits specialization for BC6H_UF16
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<BC6H_UF16>
    : ComponentTraits<SWR_TYPE_UNORM, 8>, FormatSwizzle<0>, Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{128};
    static const uint32_t numComps{1};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{false};
    static const bool     isBC{true};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{4};
    static const uint32_t bcHeight{4};

    typedef TransposeSingleComponent<8> TransposeT;
    typedef Format1<8>                  FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R8G8B8_UNORM_SRGB> - Format traits specialization for R8G8B8_UNORM_SRGB
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R8G8B8_UNORM_SRGB>
    : ComponentTraits<SWR_TYPE_UNORM, 8, SWR_TYPE_UNORM, 8, SWR_TYPE_UNORM, 8>,
      FormatSwizzle<0, 1, 2>,
      Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{24};
    static const uint32_t numComps{3};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{true};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose8_8_8   TransposeT;
    typedef Format3<8, 8, 8> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R16G16B16_UINT> - Format traits specialization for R16G16B16_UINT
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R16G16B16_UINT>
    : ComponentTraits<SWR_TYPE_UINT, 16, SWR_TYPE_UINT, 16, SWR_TYPE_UINT, 16>,
      FormatSwizzle<0, 1, 2>,
      Defaults<0, 0, 0, 0x1>
{
    static const uint32_t bpp{48};
    static const uint32_t numComps{3};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose16_16_16   TransposeT;
    typedef Format3<16, 16, 16> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R16G16B16_SINT> - Format traits specialization for R16G16B16_SINT
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R16G16B16_SINT>
    : ComponentTraits<SWR_TYPE_SINT, 16, SWR_TYPE_SINT, 16, SWR_TYPE_SINT, 16>,
      FormatSwizzle<0, 1, 2>,
      Defaults<0, 0, 0, 0x1>
{
    static const uint32_t bpp{48};
    static const uint32_t numComps{3};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose16_16_16   TransposeT;
    typedef Format3<16, 16, 16> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R32_SFIXED> - Format traits specialization for R32_SFIXED
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R32_SFIXED>
    : ComponentTraits<SWR_TYPE_SFIXED, 32>, FormatSwizzle<0>, Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{32};
    static const uint32_t numComps{1};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef TransposeSingleComponent<32> TransposeT;
    typedef Format1<32>                  FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R10G10B10A2_SNORM> - Format traits specialization for R10G10B10A2_SNORM
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R10G10B10A2_SNORM> : ComponentTraits<SWR_TYPE_SNORM,
                                                         10,
                                                         SWR_TYPE_SNORM,
                                                         10,
                                                         SWR_TYPE_SNORM,
                                                         10,
                                                         SWR_TYPE_SNORM,
                                                         2>,
                                         FormatSwizzle<0, 1, 2, 3>,
                                         Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{32};
    static const uint32_t numComps{4};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose10_10_10_2    TransposeT;
    typedef Format4<10, 10, 10, 2> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R10G10B10A2_USCALED> - Format traits specialization for R10G10B10A2_USCALED
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R10G10B10A2_USCALED> : ComponentTraits<SWR_TYPE_USCALED,
                                                           10,
                                                           SWR_TYPE_USCALED,
                                                           10,
                                                           SWR_TYPE_USCALED,
                                                           10,
                                                           SWR_TYPE_USCALED,
                                                           2>,
                                           FormatSwizzle<0, 1, 2, 3>,
                                           Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{32};
    static const uint32_t numComps{4};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose10_10_10_2    TransposeT;
    typedef Format4<10, 10, 10, 2> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R10G10B10A2_SSCALED> - Format traits specialization for R10G10B10A2_SSCALED
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R10G10B10A2_SSCALED> : ComponentTraits<SWR_TYPE_SSCALED,
                                                           10,
                                                           SWR_TYPE_SSCALED,
                                                           10,
                                                           SWR_TYPE_SSCALED,
                                                           10,
                                                           SWR_TYPE_SSCALED,
                                                           2>,
                                           FormatSwizzle<0, 1, 2, 3>,
                                           Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{32};
    static const uint32_t numComps{4};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose10_10_10_2    TransposeT;
    typedef Format4<10, 10, 10, 2> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R10G10B10A2_SINT> - Format traits specialization for R10G10B10A2_SINT
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R10G10B10A2_SINT>
    : ComponentTraits<SWR_TYPE_SINT, 10, SWR_TYPE_SINT, 10, SWR_TYPE_SINT, 10, SWR_TYPE_SINT, 2>,
      FormatSwizzle<0, 1, 2, 3>,
      Defaults<0, 0, 0, 0x1>
{
    static const uint32_t bpp{32};
    static const uint32_t numComps{4};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose10_10_10_2    TransposeT;
    typedef Format4<10, 10, 10, 2> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<B10G10R10A2_SNORM> - Format traits specialization for B10G10R10A2_SNORM
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<B10G10R10A2_SNORM> : ComponentTraits<SWR_TYPE_SNORM,
                                                         10,
                                                         SWR_TYPE_SNORM,
                                                         10,
                                                         SWR_TYPE_SNORM,
                                                         10,
                                                         SWR_TYPE_SNORM,
                                                         2>,
                                         FormatSwizzle<2, 1, 0, 3>,
                                         Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{32};
    static const uint32_t numComps{4};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose10_10_10_2    TransposeT;
    typedef Format4<10, 10, 10, 2> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<B10G10R10A2_USCALED> - Format traits specialization for B10G10R10A2_USCALED
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<B10G10R10A2_USCALED> : ComponentTraits<SWR_TYPE_USCALED,
                                                           10,
                                                           SWR_TYPE_USCALED,
                                                           10,
                                                           SWR_TYPE_USCALED,
                                                           10,
                                                           SWR_TYPE_USCALED,
                                                           2>,
                                           FormatSwizzle<2, 1, 0, 3>,
                                           Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{32};
    static const uint32_t numComps{4};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose10_10_10_2    TransposeT;
    typedef Format4<10, 10, 10, 2> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<B10G10R10A2_SSCALED> - Format traits specialization for B10G10R10A2_SSCALED
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<B10G10R10A2_SSCALED> : ComponentTraits<SWR_TYPE_SSCALED,
                                                           10,
                                                           SWR_TYPE_SSCALED,
                                                           10,
                                                           SWR_TYPE_SSCALED,
                                                           10,
                                                           SWR_TYPE_SSCALED,
                                                           2>,
                                           FormatSwizzle<2, 1, 0, 3>,
                                           Defaults<0, 0, 0, 0x3f800000>
{
    static const uint32_t bpp{32};
    static const uint32_t numComps{4};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose10_10_10_2    TransposeT;
    typedef Format4<10, 10, 10, 2> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<B10G10R10A2_UINT> - Format traits specialization for B10G10R10A2_UINT
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<B10G10R10A2_UINT>
    : ComponentTraits<SWR_TYPE_UINT, 10, SWR_TYPE_UINT, 10, SWR_TYPE_UINT, 10, SWR_TYPE_UINT, 2>,
      FormatSwizzle<2, 1, 0, 3>,
      Defaults<0, 0, 0, 0x1>
{
    static const uint32_t bpp{32};
    static const uint32_t numComps{4};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose10_10_10_2    TransposeT;
    typedef Format4<10, 10, 10, 2> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<B10G10R10A2_SINT> - Format traits specialization for B10G10R10A2_SINT
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<B10G10R10A2_SINT>
    : ComponentTraits<SWR_TYPE_SINT, 10, SWR_TYPE_SINT, 10, SWR_TYPE_SINT, 10, SWR_TYPE_SINT, 2>,
      FormatSwizzle<2, 1, 0, 3>,
      Defaults<0, 0, 0, 0x1>
{
    static const uint32_t bpp{32};
    static const uint32_t numComps{4};
    static const bool     hasAlpha{true};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose10_10_10_2    TransposeT;
    typedef Format4<10, 10, 10, 2> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R8G8B8_UINT> - Format traits specialization for R8G8B8_UINT
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R8G8B8_UINT>
    : ComponentTraits<SWR_TYPE_UINT, 8, SWR_TYPE_UINT, 8, SWR_TYPE_UINT, 8>,
      FormatSwizzle<0, 1, 2>,
      Defaults<0, 0, 0, 0x1>
{
    static const uint32_t bpp{24};
    static const uint32_t numComps{3};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose8_8_8   TransposeT;
    typedef Format3<8, 8, 8> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<R8G8B8_SINT> - Format traits specialization for R8G8B8_SINT
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<R8G8B8_SINT>
    : ComponentTraits<SWR_TYPE_SINT, 8, SWR_TYPE_SINT, 8, SWR_TYPE_SINT, 8>,
      FormatSwizzle<0, 1, 2>,
      Defaults<0, 0, 0, 0x1>
{
    static const uint32_t bpp{24};
    static const uint32_t numComps{3};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{0};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef Transpose8_8_8   TransposeT;
    typedef Format3<8, 8, 8> FormatT;
};

//////////////////////////////////////////////////////////////////////////
/// FormatTraits<RAW> - Format traits specialization for RAW
//////////////////////////////////////////////////////////////////////////
template <>
struct FormatTraits<RAW>
    : ComponentTraits<SWR_TYPE_UINT, 8>, FormatSwizzle<0>, Defaults<0, 0, 0, 0x1>
{
    static const uint32_t bpp{8};
    static const uint32_t numComps{1};
    static const bool     hasAlpha{false};
    static const uint32_t alphaComp{3};
    static const bool     isSRGB{false};
    static const bool     isBC{false};
    static const bool     isSubsampled{false};
    static const uint32_t bcWidth{1};
    static const uint32_t bcHeight{1};

    typedef TransposeSingleComponent<8> TransposeT;
    typedef Format1<8>                  FormatT;
};
