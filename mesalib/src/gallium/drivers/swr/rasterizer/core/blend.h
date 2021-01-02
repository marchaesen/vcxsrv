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
 * @file blend.cpp
 *
 * @brief Implementation for blending operations.
 *
 ******************************************************************************/
#include "state.h"

template <bool Color, bool Alpha>
INLINE void GenerateBlendFactor(SWR_BLEND_FACTOR func,
                                simdvector&      constantColor,
                                simdvector&      src,
                                simdvector&      src1,
                                simdvector&      dst,
                                simdvector&      out)
{
    simdvector result;

    switch (func)
    {
    case BLENDFACTOR_ZERO:
        result.x = _simd_setzero_ps();
        result.y = _simd_setzero_ps();
        result.z = _simd_setzero_ps();
        result.w = _simd_setzero_ps();
        break;

    case BLENDFACTOR_ONE:
        result.x = _simd_set1_ps(1.0);
        result.y = _simd_set1_ps(1.0);
        result.z = _simd_set1_ps(1.0);
        result.w = _simd_set1_ps(1.0);
        break;

    case BLENDFACTOR_SRC_COLOR:
        result = src;
        break;

    case BLENDFACTOR_DST_COLOR:
        result = dst;
        break;

    case BLENDFACTOR_INV_SRC_COLOR:
        result.x = _simd_sub_ps(_simd_set1_ps(1.0), src.x);
        result.y = _simd_sub_ps(_simd_set1_ps(1.0), src.y);
        result.z = _simd_sub_ps(_simd_set1_ps(1.0), src.z);
        result.w = _simd_sub_ps(_simd_set1_ps(1.0), src.w);
        break;

    case BLENDFACTOR_INV_DST_COLOR:
        result.x = _simd_sub_ps(_simd_set1_ps(1.0), dst.x);
        result.y = _simd_sub_ps(_simd_set1_ps(1.0), dst.y);
        result.z = _simd_sub_ps(_simd_set1_ps(1.0), dst.z);
        result.w = _simd_sub_ps(_simd_set1_ps(1.0), dst.w);
        break;

    case BLENDFACTOR_SRC_ALPHA:
        result.x = src.w;
        result.y = src.w;
        result.z = src.w;
        result.w = src.w;
        break;

    case BLENDFACTOR_INV_SRC_ALPHA:
    {
        simdscalar oneMinusSrcA = _simd_sub_ps(_simd_set1_ps(1.0), src.w);
        result.x                = oneMinusSrcA;
        result.y                = oneMinusSrcA;
        result.z                = oneMinusSrcA;
        result.w                = oneMinusSrcA;
        break;
    }

    case BLENDFACTOR_DST_ALPHA:
        result.x = dst.w;
        result.y = dst.w;
        result.z = dst.w;
        result.w = dst.w;
        break;

    case BLENDFACTOR_INV_DST_ALPHA:
    {
        simdscalar oneMinusDstA = _simd_sub_ps(_simd_set1_ps(1.0), dst.w);
        result.x                = oneMinusDstA;
        result.y                = oneMinusDstA;
        result.z                = oneMinusDstA;
        result.w                = oneMinusDstA;
        break;
    }

    case BLENDFACTOR_SRC_ALPHA_SATURATE:
    {
        simdscalar sat = _simd_min_ps(src.w, _simd_sub_ps(_simd_set1_ps(1.0), dst.w));
        result.x       = sat;
        result.y       = sat;
        result.z       = sat;
        result.w       = _simd_set1_ps(1.0);
        break;
    }

    case BLENDFACTOR_CONST_COLOR:
        result.x = constantColor[0];
        result.y = constantColor[1];
        result.z = constantColor[2];
        result.w = constantColor[3];
        break;

    case BLENDFACTOR_CONST_ALPHA:
        result.x = result.y = result.z = result.w = constantColor[3];
        break;

    case BLENDFACTOR_INV_CONST_COLOR:
    {
        result.x = _simd_sub_ps(_simd_set1_ps(1.0f), constantColor[0]);
        result.y = _simd_sub_ps(_simd_set1_ps(1.0f), constantColor[1]);
        result.z = _simd_sub_ps(_simd_set1_ps(1.0f), constantColor[2]);
        result.w = _simd_sub_ps(_simd_set1_ps(1.0f), constantColor[3]);
        break;
    }

    case BLENDFACTOR_INV_CONST_ALPHA:
    {
        result.x = result.y = result.z = result.w =
            _simd_sub_ps(_simd_set1_ps(1.0f), constantColor[3]);
        break;
    }

    case BLENDFACTOR_SRC1_COLOR:
        result.x = src1.x;
        result.y = src1.y;
        result.z = src1.z;
        result.w = src1.w;
        break;

    case BLENDFACTOR_SRC1_ALPHA:
        result.x = result.y = result.z = result.w = src1.w;
        break;

    case BLENDFACTOR_INV_SRC1_COLOR:
        result.x = _simd_sub_ps(_simd_set1_ps(1.0f), src1.x);
        result.y = _simd_sub_ps(_simd_set1_ps(1.0f), src1.y);
        result.z = _simd_sub_ps(_simd_set1_ps(1.0f), src1.z);
        result.w = _simd_sub_ps(_simd_set1_ps(1.0f), src1.w);
        break;

    case BLENDFACTOR_INV_SRC1_ALPHA:
        result.x = result.y = result.z = result.w = _simd_sub_ps(_simd_set1_ps(1.0f), src1.w);
        break;

    default:
        SWR_INVALID("Unimplemented blend factor: %d", func);
    }

    if (Color)
    {
        out.x = result.x;
        out.y = result.y;
        out.z = result.z;
    }
    if (Alpha)
    {
        out.w = result.w;
    }
}

template <bool Color, bool Alpha>
INLINE void BlendFunc(SWR_BLEND_OP blendOp,
                      simdvector&  src,
                      simdvector&  srcFactor,
                      simdvector&  dst,
                      simdvector&  dstFactor,
                      simdvector&  out)
{
    simdvector result;

    switch (blendOp)
    {
    case BLENDOP_ADD:
        result.x = _simd_fmadd_ps(srcFactor.x, src.x, _simd_mul_ps(dstFactor.x, dst.x));
        result.y = _simd_fmadd_ps(srcFactor.y, src.y, _simd_mul_ps(dstFactor.y, dst.y));
        result.z = _simd_fmadd_ps(srcFactor.z, src.z, _simd_mul_ps(dstFactor.z, dst.z));
        result.w = _simd_fmadd_ps(srcFactor.w, src.w, _simd_mul_ps(dstFactor.w, dst.w));
        break;

    case BLENDOP_SUBTRACT:
        result.x = _simd_fmsub_ps(srcFactor.x, src.x, _simd_mul_ps(dstFactor.x, dst.x));
        result.y = _simd_fmsub_ps(srcFactor.y, src.y, _simd_mul_ps(dstFactor.y, dst.y));
        result.z = _simd_fmsub_ps(srcFactor.z, src.z, _simd_mul_ps(dstFactor.z, dst.z));
        result.w = _simd_fmsub_ps(srcFactor.w, src.w, _simd_mul_ps(dstFactor.w, dst.w));
        break;

    case BLENDOP_REVSUBTRACT:
        result.x = _simd_fmsub_ps(dstFactor.x, dst.x, _simd_mul_ps(srcFactor.x, src.x));
        result.y = _simd_fmsub_ps(dstFactor.y, dst.y, _simd_mul_ps(srcFactor.y, src.y));
        result.z = _simd_fmsub_ps(dstFactor.z, dst.z, _simd_mul_ps(srcFactor.z, src.z));
        result.w = _simd_fmsub_ps(dstFactor.w, dst.w, _simd_mul_ps(srcFactor.w, src.w));
        break;

    case BLENDOP_MIN:
        result.x = _simd_min_ps(_simd_mul_ps(srcFactor.x, src.x), _simd_mul_ps(dstFactor.x, dst.x));
        result.y = _simd_min_ps(_simd_mul_ps(srcFactor.y, src.y), _simd_mul_ps(dstFactor.y, dst.y));
        result.z = _simd_min_ps(_simd_mul_ps(srcFactor.z, src.z), _simd_mul_ps(dstFactor.z, dst.z));
        result.w = _simd_min_ps(_simd_mul_ps(srcFactor.w, src.w), _simd_mul_ps(dstFactor.w, dst.w));
        break;

    case BLENDOP_MAX:
        result.x = _simd_max_ps(_simd_mul_ps(srcFactor.x, src.x), _simd_mul_ps(dstFactor.x, dst.x));
        result.y = _simd_max_ps(_simd_mul_ps(srcFactor.y, src.y), _simd_mul_ps(dstFactor.y, dst.y));
        result.z = _simd_max_ps(_simd_mul_ps(srcFactor.z, src.z), _simd_mul_ps(dstFactor.z, dst.z));
        result.w = _simd_max_ps(_simd_mul_ps(srcFactor.w, src.w), _simd_mul_ps(dstFactor.w, dst.w));
        break;

    default:
        SWR_INVALID("Unimplemented blend function: %d", blendOp);
    }

    if (Color)
    {
        out.x = result.x;
        out.y = result.y;
        out.z = result.z;
    }
    if (Alpha)
    {
        out.w = result.w;
    }
}

template <SWR_TYPE type>
INLINE void Clamp(simdvector& src)
{
    switch (type)
    {
    case SWR_TYPE_FLOAT:
        break;

    case SWR_TYPE_UNORM:
        src.x = _simd_max_ps(src.x, _simd_setzero_ps());
        src.x = _simd_min_ps(src.x, _simd_set1_ps(1.0f));

        src.y = _simd_max_ps(src.y, _simd_setzero_ps());
        src.y = _simd_min_ps(src.y, _simd_set1_ps(1.0f));

        src.z = _simd_max_ps(src.z, _simd_setzero_ps());
        src.z = _simd_min_ps(src.z, _simd_set1_ps(1.0f));

        src.w = _simd_max_ps(src.w, _simd_setzero_ps());
        src.w = _simd_min_ps(src.w, _simd_set1_ps(1.0f));
        break;

    case SWR_TYPE_SNORM:
        src.x = _simd_max_ps(src.x, _simd_set1_ps(-1.0f));
        src.x = _simd_min_ps(src.x, _simd_set1_ps(1.0f));

        src.y = _simd_max_ps(src.y, _simd_set1_ps(-1.0f));
        src.y = _simd_min_ps(src.y, _simd_set1_ps(1.0f));

        src.z = _simd_max_ps(src.z, _simd_set1_ps(-1.0f));
        src.z = _simd_min_ps(src.z, _simd_set1_ps(1.0f));

        src.w = _simd_max_ps(src.w, _simd_set1_ps(-1.0f));
        src.w = _simd_min_ps(src.w, _simd_set1_ps(1.0f));
        break;

    default:
        SWR_INVALID("Unimplemented clamp: %d", type);
        break;
    }
}

template <SWR_TYPE type>
void Blend(const SWR_BLEND_STATE*               pBlendState,
           const SWR_RENDER_TARGET_BLEND_STATE* pState,
           simdvector&                          src,
           simdvector&                          src1,
           uint8_t*                             pDst,
           simdvector&                          result)
{
    // load render target
    simdvector dst;
    LoadSOA<KNOB_COLOR_HOT_TILE_FORMAT>(pDst, dst);

    simdvector constColor;
    constColor.x = _simd_broadcast_ss(&pBlendState->constantColor[0]);
    constColor.y = _simd_broadcast_ss(&pBlendState->constantColor[1]);
    constColor.z = _simd_broadcast_ss(&pBlendState->constantColor[2]);
    constColor.w = _simd_broadcast_ss(&pBlendState->constantColor[3]);

    // clamp src/dst/constant
    Clamp<type>(src);
    Clamp<type>(src1);
    Clamp<type>(dst);
    Clamp<type>(constColor);

    simdvector srcFactor, dstFactor;
    if (pBlendState->independentAlphaBlendEnable)
    {
        GenerateBlendFactor<true, false>(
            (SWR_BLEND_FACTOR)pState->sourceBlendFactor, constColor, src, src1, dst, srcFactor);
        GenerateBlendFactor<false, true>((SWR_BLEND_FACTOR)pState->sourceAlphaBlendFactor,
                                         constColor,
                                         src,
                                         src1,
                                         dst,
                                         srcFactor);

        GenerateBlendFactor<true, false>(
            (SWR_BLEND_FACTOR)pState->destBlendFactor, constColor, src, src1, dst, dstFactor);
        GenerateBlendFactor<false, true>(
            (SWR_BLEND_FACTOR)pState->destAlphaBlendFactor, constColor, src, src1, dst, dstFactor);

        BlendFunc<true, false>(
            (SWR_BLEND_OP)pState->colorBlendFunc, src, srcFactor, dst, dstFactor, result);
        BlendFunc<false, true>(
            (SWR_BLEND_OP)pState->alphaBlendFunc, src, srcFactor, dst, dstFactor, result);
    }
    else
    {
        GenerateBlendFactor<true, true>(
            (SWR_BLEND_FACTOR)pState->sourceBlendFactor, constColor, src, src1, dst, srcFactor);
        GenerateBlendFactor<true, true>(
            (SWR_BLEND_FACTOR)pState->destBlendFactor, constColor, src, src1, dst, dstFactor);

        BlendFunc<true, true>(
            (SWR_BLEND_OP)pState->colorBlendFunc, src, srcFactor, dst, dstFactor, result);
    }
}
