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
 * @file binner.h
 *
 * @brief Declaration for the macrotile binner
 *
 ******************************************************************************/
#include "state.h"
#include "conservativeRast.h"
#include "utils.h"
//////////////////////////////////////////////////////////////////////////
/// @brief Offsets added to post-viewport vertex positions based on
/// raster state.
///
/// Can't use templated variable because we must stick with C++11 features.
/// Template variables were introduced with C++14
template <typename SIMD_T>
struct SwrPixelOffsets
{
public:
    INLINE static Float<SIMD_T> GetOffset(uint32_t loc)
    {
        SWR_ASSERT(loc <= 1);

        return SIMD_T::set1_ps(loc ? 0.5f : 0.0f);
    }
};

//////////////////////////////////////////////////////////////////////////
/// @brief Convert the X,Y coords of a triangle to the requested Fixed
/// Point precision from FP32.
template <typename SIMD_T, typename PT = FixedPointTraits<Fixed_16_8>>
INLINE Integer<SIMD_T> fpToFixedPointVertical(const Float<SIMD_T>& vIn)
{
    return SIMD_T::cvtps_epi32(SIMD_T::mul_ps(vIn, SIMD_T::set1_ps(PT::ScaleT::value)));
}

//////////////////////////////////////////////////////////////////////////
/// @brief Helper function to set the X,Y coords of a triangle to the
/// requested Fixed Point precision from FP32.
/// @param tri: simdvector[3] of FP triangle verts
/// @param vXi: fixed point X coords of tri verts
/// @param vYi: fixed point Y coords of tri verts
template <typename SIMD_T>
INLINE static void
FPToFixedPoint(const Vec4<SIMD_T>* const tri, Integer<SIMD_T> (&vXi)[3], Integer<SIMD_T> (&vYi)[3])
{
    vXi[0] = fpToFixedPointVertical<SIMD_T>(tri[0].x);
    vYi[0] = fpToFixedPointVertical<SIMD_T>(tri[0].y);
    vXi[1] = fpToFixedPointVertical<SIMD_T>(tri[1].x);
    vYi[1] = fpToFixedPointVertical<SIMD_T>(tri[1].y);
    vXi[2] = fpToFixedPointVertical<SIMD_T>(tri[2].x);
    vYi[2] = fpToFixedPointVertical<SIMD_T>(tri[2].y);
}

//////////////////////////////////////////////////////////////////////////
/// @brief Calculate bounding box for current triangle
/// @tparam CT: ConservativeRastFETraits type
/// @param vX: fixed point X position for triangle verts
/// @param vY: fixed point Y position for triangle verts
/// @param bbox: fixed point bbox
/// *Note*: expects vX, vY to be in the correct precision for the type
/// of rasterization. This avoids unnecessary FP->fixed conversions.
template <typename SIMD_T, typename CT>
INLINE void calcBoundingBoxIntVertical(const Integer<SIMD_T> (&vX)[3],
                                       const Integer<SIMD_T> (&vY)[3],
                                       SIMDBBOX_T<SIMD_T>& bbox)
{
    Integer<SIMD_T> vMinX = vX[0];

    vMinX = SIMD_T::min_epi32(vMinX, vX[1]);
    vMinX = SIMD_T::min_epi32(vMinX, vX[2]);

    Integer<SIMD_T> vMaxX = vX[0];

    vMaxX = SIMD_T::max_epi32(vMaxX, vX[1]);
    vMaxX = SIMD_T::max_epi32(vMaxX, vX[2]);

    Integer<SIMD_T> vMinY = vY[0];

    vMinY = SIMD_T::min_epi32(vMinY, vY[1]);
    vMinY = SIMD_T::min_epi32(vMinY, vY[2]);

    Integer<SIMD_T> vMaxY = vY[0];

    vMaxY = SIMD_T::max_epi32(vMaxY, vY[1]);
    vMaxY = SIMD_T::max_epi32(vMaxY, vY[2]);

    if (CT::BoundingBoxOffsetT::value != 0)
    {
        /// Bounding box needs to be expanded by 1/512 before snapping to 16.8 for conservative
        /// rasterization expand bbox by 1/256; coverage will be correctly handled in the
        /// rasterizer.

        const Integer<SIMD_T> value = SIMD_T::set1_epi32(CT::BoundingBoxOffsetT::value);

        vMinX = SIMD_T::sub_epi32(vMinX, value);
        vMaxX = SIMD_T::add_epi32(vMaxX, value);
        vMinY = SIMD_T::sub_epi32(vMinY, value);
        vMaxY = SIMD_T::add_epi32(vMaxY, value);
    }

    bbox.xmin = vMinX;
    bbox.xmax = vMaxX;
    bbox.ymin = vMinY;
    bbox.ymax = vMaxY;
}

//////////////////////////////////////////////////////////////////////////
/// @brief  Gather scissor rect data based on per-prim viewport indices.
/// @param pScissorsInFixedPoint - array of scissor rects in 16.8 fixed point.
/// @param pViewportIndex - array of per-primitive viewport indexes.
/// @param scisXmin - output vector of per-primitive scissor rect Xmin data.
/// @param scisYmin - output vector of per-primitive scissor rect Ymin data.
/// @param scisXmax - output vector of per-primitive scissor rect Xmax data.
/// @param scisYmax - output vector of per-primitive scissor rect Ymax data.
//
/// @todo:  Look at speeding this up -- weigh against corresponding costs in rasterizer.
static void GatherScissors(const SWR_RECT* pScissorsInFixedPoint,
                           const uint32_t* pViewportIndex,
                           simdscalari&    scisXmin,
                           simdscalari&    scisYmin,
                           simdscalari&    scisXmax,
                           simdscalari&    scisYmax)
{
    scisXmin = _simd_set_epi32(pScissorsInFixedPoint[pViewportIndex[7]].xmin,
                               pScissorsInFixedPoint[pViewportIndex[6]].xmin,
                               pScissorsInFixedPoint[pViewportIndex[5]].xmin,
                               pScissorsInFixedPoint[pViewportIndex[4]].xmin,
                               pScissorsInFixedPoint[pViewportIndex[3]].xmin,
                               pScissorsInFixedPoint[pViewportIndex[2]].xmin,
                               pScissorsInFixedPoint[pViewportIndex[1]].xmin,
                               pScissorsInFixedPoint[pViewportIndex[0]].xmin);
    scisYmin = _simd_set_epi32(pScissorsInFixedPoint[pViewportIndex[7]].ymin,
                               pScissorsInFixedPoint[pViewportIndex[6]].ymin,
                               pScissorsInFixedPoint[pViewportIndex[5]].ymin,
                               pScissorsInFixedPoint[pViewportIndex[4]].ymin,
                               pScissorsInFixedPoint[pViewportIndex[3]].ymin,
                               pScissorsInFixedPoint[pViewportIndex[2]].ymin,
                               pScissorsInFixedPoint[pViewportIndex[1]].ymin,
                               pScissorsInFixedPoint[pViewportIndex[0]].ymin);
    scisXmax = _simd_set_epi32(pScissorsInFixedPoint[pViewportIndex[7]].xmax,
                               pScissorsInFixedPoint[pViewportIndex[6]].xmax,
                               pScissorsInFixedPoint[pViewportIndex[5]].xmax,
                               pScissorsInFixedPoint[pViewportIndex[4]].xmax,
                               pScissorsInFixedPoint[pViewportIndex[3]].xmax,
                               pScissorsInFixedPoint[pViewportIndex[2]].xmax,
                               pScissorsInFixedPoint[pViewportIndex[1]].xmax,
                               pScissorsInFixedPoint[pViewportIndex[0]].xmax);
    scisYmax = _simd_set_epi32(pScissorsInFixedPoint[pViewportIndex[7]].ymax,
                               pScissorsInFixedPoint[pViewportIndex[6]].ymax,
                               pScissorsInFixedPoint[pViewportIndex[5]].ymax,
                               pScissorsInFixedPoint[pViewportIndex[4]].ymax,
                               pScissorsInFixedPoint[pViewportIndex[3]].ymax,
                               pScissorsInFixedPoint[pViewportIndex[2]].ymax,
                               pScissorsInFixedPoint[pViewportIndex[1]].ymax,
                               pScissorsInFixedPoint[pViewportIndex[0]].ymax);
}

static void GatherScissors(const SWR_RECT* pScissorsInFixedPoint,
                           const uint32_t* pViewportIndex,
                           simd16scalari&  scisXmin,
                           simd16scalari&  scisYmin,
                           simd16scalari&  scisXmax,
                           simd16scalari&  scisYmax)
{
    scisXmin = _simd16_set_epi32(pScissorsInFixedPoint[pViewportIndex[15]].xmin,
                                 pScissorsInFixedPoint[pViewportIndex[14]].xmin,
                                 pScissorsInFixedPoint[pViewportIndex[13]].xmin,
                                 pScissorsInFixedPoint[pViewportIndex[12]].xmin,
                                 pScissorsInFixedPoint[pViewportIndex[11]].xmin,
                                 pScissorsInFixedPoint[pViewportIndex[10]].xmin,
                                 pScissorsInFixedPoint[pViewportIndex[9]].xmin,
                                 pScissorsInFixedPoint[pViewportIndex[8]].xmin,
                                 pScissorsInFixedPoint[pViewportIndex[7]].xmin,
                                 pScissorsInFixedPoint[pViewportIndex[6]].xmin,
                                 pScissorsInFixedPoint[pViewportIndex[5]].xmin,
                                 pScissorsInFixedPoint[pViewportIndex[4]].xmin,
                                 pScissorsInFixedPoint[pViewportIndex[3]].xmin,
                                 pScissorsInFixedPoint[pViewportIndex[2]].xmin,
                                 pScissorsInFixedPoint[pViewportIndex[1]].xmin,
                                 pScissorsInFixedPoint[pViewportIndex[0]].xmin);

    scisYmin = _simd16_set_epi32(pScissorsInFixedPoint[pViewportIndex[15]].ymin,
                                 pScissorsInFixedPoint[pViewportIndex[14]].ymin,
                                 pScissorsInFixedPoint[pViewportIndex[13]].ymin,
                                 pScissorsInFixedPoint[pViewportIndex[12]].ymin,
                                 pScissorsInFixedPoint[pViewportIndex[11]].ymin,
                                 pScissorsInFixedPoint[pViewportIndex[10]].ymin,
                                 pScissorsInFixedPoint[pViewportIndex[9]].ymin,
                                 pScissorsInFixedPoint[pViewportIndex[8]].ymin,
                                 pScissorsInFixedPoint[pViewportIndex[7]].ymin,
                                 pScissorsInFixedPoint[pViewportIndex[6]].ymin,
                                 pScissorsInFixedPoint[pViewportIndex[5]].ymin,
                                 pScissorsInFixedPoint[pViewportIndex[4]].ymin,
                                 pScissorsInFixedPoint[pViewportIndex[3]].ymin,
                                 pScissorsInFixedPoint[pViewportIndex[2]].ymin,
                                 pScissorsInFixedPoint[pViewportIndex[1]].ymin,
                                 pScissorsInFixedPoint[pViewportIndex[0]].ymin);

    scisXmax = _simd16_set_epi32(pScissorsInFixedPoint[pViewportIndex[15]].xmax,
                                 pScissorsInFixedPoint[pViewportIndex[14]].xmax,
                                 pScissorsInFixedPoint[pViewportIndex[13]].xmax,
                                 pScissorsInFixedPoint[pViewportIndex[12]].xmax,
                                 pScissorsInFixedPoint[pViewportIndex[11]].xmax,
                                 pScissorsInFixedPoint[pViewportIndex[10]].xmax,
                                 pScissorsInFixedPoint[pViewportIndex[9]].xmax,
                                 pScissorsInFixedPoint[pViewportIndex[8]].xmax,
                                 pScissorsInFixedPoint[pViewportIndex[7]].xmax,
                                 pScissorsInFixedPoint[pViewportIndex[6]].xmax,
                                 pScissorsInFixedPoint[pViewportIndex[5]].xmax,
                                 pScissorsInFixedPoint[pViewportIndex[4]].xmax,
                                 pScissorsInFixedPoint[pViewportIndex[3]].xmax,
                                 pScissorsInFixedPoint[pViewportIndex[2]].xmax,
                                 pScissorsInFixedPoint[pViewportIndex[1]].xmax,
                                 pScissorsInFixedPoint[pViewportIndex[0]].xmax);

    scisYmax = _simd16_set_epi32(pScissorsInFixedPoint[pViewportIndex[15]].ymax,
                                 pScissorsInFixedPoint[pViewportIndex[14]].ymax,
                                 pScissorsInFixedPoint[pViewportIndex[13]].ymax,
                                 pScissorsInFixedPoint[pViewportIndex[12]].ymax,
                                 pScissorsInFixedPoint[pViewportIndex[11]].ymax,
                                 pScissorsInFixedPoint[pViewportIndex[10]].ymax,
                                 pScissorsInFixedPoint[pViewportIndex[9]].ymax,
                                 pScissorsInFixedPoint[pViewportIndex[8]].ymax,
                                 pScissorsInFixedPoint[pViewportIndex[7]].ymax,
                                 pScissorsInFixedPoint[pViewportIndex[6]].ymax,
                                 pScissorsInFixedPoint[pViewportIndex[5]].ymax,
                                 pScissorsInFixedPoint[pViewportIndex[4]].ymax,
                                 pScissorsInFixedPoint[pViewportIndex[3]].ymax,
                                 pScissorsInFixedPoint[pViewportIndex[2]].ymax,
                                 pScissorsInFixedPoint[pViewportIndex[1]].ymax,
                                 pScissorsInFixedPoint[pViewportIndex[0]].ymax);
}