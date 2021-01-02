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
* @file Convert.h
* 
* @brief Conversion utility functions
* 
******************************************************************************/
#pragma once

#if defined(_MSC_VER)
// disable "potential divide by 0"
#pragma warning(disable: 4723)
#endif

#include <cmath>

//////////////////////////////////////////////////////////////////////////
/// @brief Convert an IEEE 754 16-bit float to an 32-bit single precision
///        float
/// @param val - 16-bit float
/// @todo Maybe move this outside of this file into a header?
static INLINE float ConvertSmallFloatTo32(UINT val)
{
    UINT result;
    if ((val & 0x7fff) == 0)
    {
        result = ((uint32_t)(val & 0x8000)) << 16;
    }
    else if ((val & 0x7c00) == 0x7c00)
    {
        result = ((val & 0x3ff) == 0) ? 0x7f800000 : 0x7fc00000;
        result |= ((uint32_t)val & 0x8000) << 16;
    }
    else
    {
        uint32_t sign = (val & 0x8000) << 16;
        uint32_t mant = (val & 0x3ff) << 13;
        uint32_t exp = (val >> 10) & 0x1f;
        if ((exp == 0) && (mant != 0)) // Adjust exponent and mantissa for denormals
        {
            mant <<= 1;
            while (mant < (0x400 << 13))
            {
                exp--;
                mant <<= 1;
            }
            mant &= (0x3ff << 13);
        }
        exp = ((exp - 15 + 127) & 0xff) << 23;
        result = sign | exp | mant;
    }

    return *(float*)&result;
}

//////////////////////////////////////////////////////////////////////////
/// @brief Convert an IEEE 754 32-bit single precision float to an 
///        unsigned small float with 5 exponent bits and a variable
///        number of mantissa bits.
/// @param val - 32-bit float
/// @todo Maybe move this outside of this file into a header?
template<UINT numMantissaBits>
static UINT Convert32ToSmallFloat(float val)
{
    uint32_t sign, exp, mant;
    uint32_t roundBits;

    // Extract the sign, exponent, and mantissa
    UINT uf = *(UINT*)&val;

    sign = (uf & 0x80000000) >> 31;
    exp = (uf & 0x7F800000) >> 23;
    mant = uf & 0x007FFFFF;

    // 10/11 bit floats are unsigned.  Negative values are clamped to 0.
    if (sign != 0)
    {
        exp = mant = 0;
    }
    // Check for out of range
    else if ((exp == 0xFF) && (mant != 0)) // NaN
    {
        exp = 0x1F;
        mant = 1 << numMantissaBits;
    }
    else if ((exp == 0xFF) && (mant == 0)) // INF
    {
        exp = 0x1F;
        mant = 0;
    }
    else if (exp > (0x70 + 0x1E)) // Too big to represent
    {
        exp = 0x1Eu;
        mant = (1 << numMantissaBits) - 1;  // 0x3F for 6 bit mantissa.
    }
    else if ((exp <= 0x70) && (exp >= 0x66)) // It's a denorm
    {
        mant |= 0x00800000;
        for (; exp <= 0x70; mant >>= 1, exp++)
            ;
        exp = 0;
        mant = mant >> (23 - numMantissaBits);
    }
    else if (exp < 0x66) // Too small to represent -> Zero
    {
        exp = 0;
        mant = 0;
    }
    else
    {
        // Saves bits that will be shifted off for rounding
        roundBits = mant & 0x1FFFu;
        // convert exponent and mantissa to 16 bit format
        exp = exp - 0x70u;
        mant = mant >> (23 - numMantissaBits);

        // Essentially RTZ, but round up if off by only 1 lsb
        if (roundBits == 0x1FFFu)
        {
            mant++;
            // check for overflow
            if ((mant & (0x3 << numMantissaBits)) != 0) // 0x60 = 0x3 << (num Mantissa Bits)
                exp++;
            // make sure only the needed bits are used
            mant &= (1 << numMantissaBits) - 1;
        }
    }

    UINT tmpVal = (exp << numMantissaBits) | mant;
    return tmpVal;
}

#if KNOB_ARCH == KNOB_ARCH_AVX
//////////////////////////////////////////////////////////////////////////
/// @brief Convert an IEEE 754 32-bit single precision float to an
///        16 bit float with 5 exponent bits and a variable
///        number of mantissa bits.
/// @param val - 32-bit float
/// @todo Maybe move this outside of this file into a header?
static uint16_t Convert32To16Float(float val)
{
    uint32_t sign, exp, mant;
    uint32_t roundBits;

    // Extract the sign, exponent, and mantissa
    uint32_t uf = *(uint32_t*)&val;
    sign = (uf & 0x80000000) >> 31;
    exp = (uf & 0x7F800000) >> 23;
    mant = uf & 0x007FFFFF;

    // Check for out of range
    if (std::isnan(val))
    {
        exp = 0x1F;
        mant = 0x200;
        sign = 1;                     // set the sign bit for NANs
    }
    else if (std::isinf(val))
    {
        exp = 0x1f;
        mant = 0x0;
    }
    else if (exp > (0x70 + 0x1E)) // Too big to represent -> max representable value
    {
        exp = 0x1E;
        mant = 0x3FF;
    }
    else if ((exp <= 0x70) && (exp >= 0x66)) // It's a denorm
    {
        mant |= 0x00800000;
        for (; exp <= 0x70; mant >>= 1, exp++)
            ;
        exp = 0;
        mant = mant >> 13;
    }
    else if (exp < 0x66) // Too small to represent -> Zero
    {
        exp = 0;
        mant = 0;
    }
    else
    {
        // Saves bits that will be shifted off for rounding
        roundBits = mant & 0x1FFFu;
        // convert exponent and mantissa to 16 bit format
        exp = exp - 0x70;
        mant = mant >> 13;

        // Essentially RTZ, but round up if off by only 1 lsb
        if (roundBits == 0x1FFFu)
        {
            mant++;
            // check for overflow
            if ((mant & 0xC00u) != 0)
                exp++;
            // make sure only the needed bits are used
            mant &= 0x3FF;
        }
    }

    uint32_t tmpVal = (sign << 15) | (exp << 10) | mant;
    return (uint16_t)tmpVal;
}
#endif

//////////////////////////////////////////////////////////////////////////
/// @brief Retrieve color from hot tile source which is always float.
/// @param pDstPixel - Pointer to destination pixel.
/// @param srcPixel - Pointer to source pixel (pre-swizzled according to dest).
template<SWR_FORMAT DstFormat>
static void ConvertPixelFromFloat(
    uint8_t* pDstPixel,
    const float srcPixel[4])
{
    uint32_t outColor[4] = { 0 };  // typeless bits

    // Store component
    for (UINT comp = 0; comp < FormatTraits<DstFormat>::numComps; ++comp)
    {
        SWR_TYPE type = FormatTraits<DstFormat>::GetType(comp);

        float src = srcPixel[comp];

        switch (type)
        {
        case SWR_TYPE_UNORM:
        {
            // Force NaN to 0. IEEE standard, comparisons involving NaN always evaluate to false.
            src = (src != src) ? 0.0f : src;

            // Clamp [0, 1]
            src = std::max(src, 0.0f);
            src = std::min(src, 1.0f);

            // SRGB
            if (FormatTraits<DstFormat>::isSRGB && comp != 3)
            {
                src = (src <= 0.0031308f) ? (12.92f * src) : (1.055f * powf(src, (1.0f / 2.4f)) - 0.055f);
            }

            // Float scale to integer scale.
            UINT scale = (1 << FormatTraits<DstFormat>::GetBPC(comp)) - 1;
            src = (float)scale * src;
            src = roundf(src);
            outColor[comp] = (UINT)src; // Drop fractional part.
            break;
        }
        case SWR_TYPE_SNORM:
        {
            SWR_ASSERT(!FormatTraits<DstFormat>::isSRGB);

            // Force NaN to 0. IEEE standard, comparisons involving NaN always evaluate to false.
            src = (src != src) ? 0.0f : src;

            // Clamp [-1, 1]
            src = std::max(src, -1.0f);
            src = std::min(src, 1.0f);

            // Float scale to integer scale.
            UINT scale = (1 << (FormatTraits<DstFormat>::GetBPC(comp) - 1)) - 1;
            src = (float)scale * src;

            // Round
            src += (src >= 0) ? 0.5f : -0.5f;

            INT out = (INT)src;

            outColor[comp] = *(UINT*)&out;

            break;
        }
        case SWR_TYPE_UINT:
        {
            ///@note The *(UINT*)& is currently necessary as the hot tile appears to always be float.
            //       However, the number in the hot tile should be unsigned integer. So doing this
            //       to preserve bits intead of doing a float -> integer conversion.
            if (FormatTraits<DstFormat>::GetBPC(comp) == 32)
            {
                outColor[comp] = *(UINT*)&src;
            }
            else
            {
                outColor[comp] = *(UINT*)&src;
                UINT max = (1 << FormatTraits<DstFormat>::GetBPC(comp)) - 1;  // 2^numBits - 1

                outColor[comp] = std::min(max, outColor[comp]);
            }
            break;
        }
        case SWR_TYPE_SINT:
        {
            if (FormatTraits<DstFormat>::GetBPC(comp) == 32)
            {
                outColor[comp] = *(UINT*)&src;
            }
            else
            {
                INT out = *(INT*)&src;  // Hot tile format is SINT?
                INT max = (1 << (FormatTraits<DstFormat>::GetBPC(comp) - 1)) - 1;
                INT min = -1 - max;

                ///@note The output is unsigned integer (bag of bits) and so performing
                //       the clamping here based on range of output component. Also, manually adding
                //       the sign bit in the appropriate spot. Maybe a better way?
                out = std::max(out, min);
                out = std::min(out, max);

                outColor[comp] = *(UINT*)&out;
            }
            break;
        }
        case SWR_TYPE_FLOAT:
        {
            if (FormatTraits<DstFormat>::GetBPC(comp) == 16)
            {
                // Convert from 32-bit float to 16-bit float using _mm_cvtps_ph
                // @todo 16bit float instruction support is orthogonal to avx support.  need to
                // add check for F16C support instead.
#if KNOB_ARCH >= KNOB_ARCH_AVX2
                __m128 src128 = _mm_set1_ps(src);
                __m128i srci128 = _mm_cvtps_ph(src128, _MM_FROUND_TRUNC);
                UINT value = _mm_extract_epi16(srci128, 0);
#else
                UINT value = Convert32To16Float(src);
#endif

                outColor[comp] = value;
            }
            else if (FormatTraits<DstFormat>::GetBPC(comp) == 11)
            {
                outColor[comp] = Convert32ToSmallFloat<6>(src);
            }
            else if (FormatTraits<DstFormat>::GetBPC(comp) == 10)
            {
                outColor[comp] = Convert32ToSmallFloat<5>(src);
            }
            else
            {
                outColor[comp] = *(UINT*)&src;
            }

            break;
        }
        default:
            SWR_INVALID("Invalid type: %d", type);
            break;
        }
    }

    typename FormatTraits<DstFormat>::FormatT* pPixel = (typename FormatTraits<DstFormat>::FormatT*)pDstPixel;

    switch (FormatTraits<DstFormat>::numComps)
    {
    case 4:
        pPixel->a = outColor[3];
    case 3:
        pPixel->b = outColor[2];
    case 2:
        pPixel->g = outColor[1];
    case 1:
        pPixel->r = outColor[0];
        break;
    default:
        SWR_INVALID("Invalid # of comps: %d", FormatTraits<DstFormat>::numComps);
    }
}

//////////////////////////////////////////////////////////////////////////
/// @brief Convert pixel in any format to float32
/// @param pDstPixel - Pointer to destination pixel.
/// @param srcPixel - Pointer to source pixel
template<SWR_FORMAT SrcFormat>
INLINE static void ConvertPixelToFloat(
    float dstPixel[4],
    const uint8_t* pSrc)
{
    uint32_t srcColor[4];  // typeless bits

    // unpack src pixel
    typename FormatTraits<SrcFormat>::FormatT* pPixel = (typename FormatTraits<SrcFormat>::FormatT*)pSrc;

    // apply format defaults
    for (uint32_t comp = 0; comp < 4; ++comp)
    {
        uint32_t def = FormatTraits<SrcFormat>::GetDefault(comp);
        dstPixel[comp] = *(float*)&def;
    }

    // load format data
    switch (FormatTraits<SrcFormat>::numComps)
    {
    case 4:
        srcColor[3] = pPixel->a;
    case 3:
        srcColor[2] = pPixel->b;
    case 2:
        srcColor[1] = pPixel->g;
    case 1:
        srcColor[0] = pPixel->r;
        break;
    default:
        SWR_INVALID("Invalid # of comps: %d", FormatTraits<SrcFormat>::numComps);
    }

    // Convert components
    for (uint32_t comp = 0; comp < FormatTraits<SrcFormat>::numComps; ++comp)
    {
        SWR_TYPE type = FormatTraits<SrcFormat>::GetType(comp);

        uint32_t src = srcColor[comp];

        switch (type)
        {
        case SWR_TYPE_UNORM:
        {
            float dst;
            if (FormatTraits<SrcFormat>::isSRGB && comp != 3)
            {
                dst = *(float*)&srgb8Table[src];
            }
            else
            {
                // component sizes > 16 must use fp divide to maintain ulp requirements
                if (FormatTraits<SrcFormat>::GetBPC(comp) > 16)
                {
                    dst = (float)src / (float)((1 << FormatTraits<SrcFormat>::GetBPC(comp)) - 1);
                }
                else
                {
                    const float scale = (1.0f / (float)((1 << FormatTraits<SrcFormat>::GetBPC(comp)) - 1));
                    dst = (float)src * scale;
                }
            }
            dstPixel[FormatTraits<SrcFormat>::swizzle(comp)] = dst;
            break;
        }
        case SWR_TYPE_SNORM:
        {
            SWR_ASSERT(!FormatTraits<SrcFormat>::isSRGB);

            float dst;
            if (src == 0x10)
            {
                dst = -1.0f;
            }
            else
            {
                switch (FormatTraits<SrcFormat>::GetBPC(comp))
                {
                case 8:
                    dst = (float)((int8_t)src);
                    break;
                case 16:
                    dst = (float)((int16_t)src);
                    break;
                case 32:
                    dst = (float)((int32_t)src);
                    break;
                default:
                    assert(0 && "attempted to load from SNORM with unsupported bpc");
                    dst = 0.0f;
                    break;
                }
                dst = dst * (1.0f / ((1 << (FormatTraits<SrcFormat>::GetBPC(comp) - 1)) - 1));
            }
            dstPixel[FormatTraits<SrcFormat>::swizzle(comp)] = dst;
            break;
        }
        case SWR_TYPE_UINT:
        {
            uint32_t dst = (uint32_t)src;
            dstPixel[FormatTraits<SrcFormat>::swizzle(comp)] = *(float*)&dst;
            break;
        }
        case SWR_TYPE_SINT:
        {
            int dst;
            switch (FormatTraits<SrcFormat>::GetBPC(comp))
            {
            case 8:
                dst = (int8_t)src;
                break;
            case 16:
                dst = (int16_t)src;
                break;
            case 32:
                dst = (int32_t)src;
                break;
            default:
                assert(0 && "attempted to load from SINT with unsupported bpc");
                dst = 0;
                break;
            }
            dstPixel[FormatTraits<SrcFormat>::swizzle(comp)] = *(float*)&dst;
            break;
        }
        case SWR_TYPE_FLOAT:
        {
            float dst;
            if (FormatTraits<SrcFormat>::GetBPC(comp) == 16)
            {
#if KNOB_ARCH >= KNOB_ARCH_AVX2
                // Convert from 16-bit float to 32-bit float using _mm_cvtph_ps
                // @todo 16bit float instruction support is orthogonal to avx support.  need to
                // add check for F16C support instead.
                __m128i src128 = _mm_set1_epi32(src);
                __m128 res = _mm_cvtph_ps(src128);
                _mm_store_ss(&dst, res);
#else
                dst = ConvertSmallFloatTo32(src);
#endif
            }
            else if (FormatTraits<SrcFormat>::GetBPC(comp) == 11)
            {
                dst = ConvertSmallFloatTo32(src << 4);
            }
            else if (FormatTraits<SrcFormat>::GetBPC(comp) == 10)
            {
                dst = ConvertSmallFloatTo32(src << 5);
            }
            else
            {
                dst = *(float*)&src;
            }

            dstPixel[FormatTraits<SrcFormat>::swizzle(comp)] = *(float*)&dst;
            break;
        }
        default:
            SWR_INVALID("Invalid type: %d", type);
            break;
        }
    }
}

// non-templated version of conversion functions
INLINE static void ConvertPixelFromFloat(
    SWR_FORMAT format,
    uint8_t* pDst,
    const float srcPixel[4])
{
    switch (format)
    {
    case R32G32B32A32_FLOAT: ConvertPixelFromFloat<R32G32B32A32_FLOAT>(pDst, srcPixel); break;
    case R32G32B32A32_SINT: ConvertPixelFromFloat<R32G32B32A32_SINT>(pDst, srcPixel); break;
    case R32G32B32A32_UINT: ConvertPixelFromFloat<R32G32B32A32_UINT>(pDst, srcPixel); break;
    case R32G32B32X32_FLOAT: ConvertPixelFromFloat<R32G32B32X32_FLOAT>(pDst, srcPixel); break;
    case R32G32B32A32_SSCALED: ConvertPixelFromFloat<R32G32B32A32_SSCALED>(pDst, srcPixel); break;
    case R32G32B32A32_USCALED: ConvertPixelFromFloat<R32G32B32A32_USCALED>(pDst, srcPixel); break;
    case R32G32B32_FLOAT: ConvertPixelFromFloat<R32G32B32_FLOAT>(pDst, srcPixel); break;
    case R32G32B32_SINT: ConvertPixelFromFloat<R32G32B32_SINT>(pDst, srcPixel); break;
    case R32G32B32_UINT: ConvertPixelFromFloat<R32G32B32_UINT>(pDst, srcPixel); break;
    case R32G32B32_SSCALED: ConvertPixelFromFloat<R32G32B32_SSCALED>(pDst, srcPixel); break;
    case R32G32B32_USCALED: ConvertPixelFromFloat<R32G32B32_USCALED>(pDst, srcPixel); break;
    case R16G16B16A16_UNORM: ConvertPixelFromFloat<R16G16B16A16_UNORM>(pDst, srcPixel); break;
    case R16G16B16A16_SNORM: ConvertPixelFromFloat<R16G16B16A16_SNORM>(pDst, srcPixel); break;
    case R16G16B16A16_SINT: ConvertPixelFromFloat<R16G16B16A16_SINT>(pDst, srcPixel); break;
    case R16G16B16A16_UINT: ConvertPixelFromFloat<R16G16B16A16_UINT>(pDst, srcPixel); break;
    case R16G16B16A16_FLOAT: ConvertPixelFromFloat<R16G16B16A16_FLOAT>(pDst, srcPixel); break;
    case R32G32_FLOAT: ConvertPixelFromFloat<R32G32_FLOAT>(pDst, srcPixel); break;
    case R32G32_SINT: ConvertPixelFromFloat<R32G32_SINT>(pDst, srcPixel); break;
    case R32G32_UINT: ConvertPixelFromFloat<R32G32_UINT>(pDst, srcPixel); break;
    case R32_FLOAT_X8X24_TYPELESS: ConvertPixelFromFloat<R32_FLOAT_X8X24_TYPELESS>(pDst, srcPixel); break;
    case X32_TYPELESS_G8X24_UINT: ConvertPixelFromFloat<X32_TYPELESS_G8X24_UINT>(pDst, srcPixel); break;
    case L32A32_FLOAT: ConvertPixelFromFloat<L32A32_FLOAT>(pDst, srcPixel); break;
    case R16G16B16X16_UNORM: ConvertPixelFromFloat<R16G16B16X16_UNORM>(pDst, srcPixel); break;
    case R16G16B16X16_FLOAT: ConvertPixelFromFloat<R16G16B16X16_FLOAT>(pDst, srcPixel); break;
    case L32X32_FLOAT: ConvertPixelFromFloat<L32X32_FLOAT>(pDst, srcPixel); break;
    case I32X32_FLOAT: ConvertPixelFromFloat<I32X32_FLOAT>(pDst, srcPixel); break;
    case R16G16B16A16_SSCALED: ConvertPixelFromFloat<R16G16B16A16_SSCALED>(pDst, srcPixel); break;
    case R16G16B16A16_USCALED: ConvertPixelFromFloat<R16G16B16A16_USCALED>(pDst, srcPixel); break;
    case R32G32_SSCALED: ConvertPixelFromFloat<R32G32_SSCALED>(pDst, srcPixel); break;
    case R32G32_USCALED: ConvertPixelFromFloat<R32G32_USCALED>(pDst, srcPixel); break;
    case B8G8R8A8_UNORM: ConvertPixelFromFloat<B8G8R8A8_UNORM>(pDst, srcPixel); break;
    case B8G8R8A8_UNORM_SRGB: ConvertPixelFromFloat<B8G8R8A8_UNORM_SRGB>(pDst, srcPixel); break;
    case R10G10B10A2_UNORM: ConvertPixelFromFloat<R10G10B10A2_UNORM>(pDst, srcPixel); break;
    case R10G10B10A2_UNORM_SRGB: ConvertPixelFromFloat<R10G10B10A2_UNORM_SRGB>(pDst, srcPixel); break;
    case R10G10B10A2_UINT: ConvertPixelFromFloat<R10G10B10A2_UINT>(pDst, srcPixel); break;
    case R8G8B8A8_UNORM: ConvertPixelFromFloat<R8G8B8A8_UNORM>(pDst, srcPixel); break;
    case R8G8B8A8_UNORM_SRGB: ConvertPixelFromFloat<R8G8B8A8_UNORM_SRGB>(pDst, srcPixel); break;
    case R8G8B8A8_SNORM: ConvertPixelFromFloat<R8G8B8A8_SNORM>(pDst, srcPixel); break;
    case R8G8B8A8_SINT: ConvertPixelFromFloat<R8G8B8A8_SINT>(pDst, srcPixel); break;
    case R8G8B8A8_UINT: ConvertPixelFromFloat<R8G8B8A8_UINT>(pDst, srcPixel); break;
    case R16G16_UNORM: ConvertPixelFromFloat<R16G16_UNORM>(pDst, srcPixel); break;
    case R16G16_SNORM: ConvertPixelFromFloat<R16G16_SNORM>(pDst, srcPixel); break;
    case R16G16_SINT: ConvertPixelFromFloat<R16G16_SINT>(pDst, srcPixel); break;
    case R16G16_UINT: ConvertPixelFromFloat<R16G16_UINT>(pDst, srcPixel); break;
    case R16G16_FLOAT: ConvertPixelFromFloat<R16G16_FLOAT>(pDst, srcPixel); break;
    case B10G10R10A2_UNORM: ConvertPixelFromFloat<B10G10R10A2_UNORM>(pDst, srcPixel); break;
    case B10G10R10A2_UNORM_SRGB: ConvertPixelFromFloat<B10G10R10A2_UNORM_SRGB>(pDst, srcPixel); break;
    case R11G11B10_FLOAT: ConvertPixelFromFloat<R11G11B10_FLOAT>(pDst, srcPixel); break;
    case R10G10B10_FLOAT_A2_UNORM: ConvertPixelFromFloat<R10G10B10_FLOAT_A2_UNORM>(pDst, srcPixel); break;
    case R32_SINT: ConvertPixelFromFloat<R32_SINT>(pDst, srcPixel); break;
    case R32_UINT: ConvertPixelFromFloat<R32_UINT>(pDst, srcPixel); break;
    case R32_FLOAT: ConvertPixelFromFloat<R32_FLOAT>(pDst, srcPixel); break;
    case R24_UNORM_X8_TYPELESS: ConvertPixelFromFloat<R24_UNORM_X8_TYPELESS>(pDst, srcPixel); break;
    case X24_TYPELESS_G8_UINT: ConvertPixelFromFloat<X24_TYPELESS_G8_UINT>(pDst, srcPixel); break;
    case L32_UNORM: ConvertPixelFromFloat<L32_UNORM>(pDst, srcPixel); break;
    case L16A16_UNORM: ConvertPixelFromFloat<L16A16_UNORM>(pDst, srcPixel); break;
    case I24X8_UNORM: ConvertPixelFromFloat<I24X8_UNORM>(pDst, srcPixel); break;
    case L24X8_UNORM: ConvertPixelFromFloat<L24X8_UNORM>(pDst, srcPixel); break;
    case I32_FLOAT: ConvertPixelFromFloat<I32_FLOAT>(pDst, srcPixel); break;
    case L32_FLOAT: ConvertPixelFromFloat<L32_FLOAT>(pDst, srcPixel); break;
    case A32_FLOAT: ConvertPixelFromFloat<A32_FLOAT>(pDst, srcPixel); break;
    case B8G8R8X8_UNORM: ConvertPixelFromFloat<B8G8R8X8_UNORM>(pDst, srcPixel); break;
    case B8G8R8X8_UNORM_SRGB: ConvertPixelFromFloat<B8G8R8X8_UNORM_SRGB>(pDst, srcPixel); break;
    case R8G8B8X8_UNORM: ConvertPixelFromFloat<R8G8B8X8_UNORM>(pDst, srcPixel); break;
    case R8G8B8X8_UNORM_SRGB: ConvertPixelFromFloat<R8G8B8X8_UNORM_SRGB>(pDst, srcPixel); break;
    case R9G9B9E5_SHAREDEXP: ConvertPixelFromFloat<R9G9B9E5_SHAREDEXP>(pDst, srcPixel); break;
    case B10G10R10X2_UNORM: ConvertPixelFromFloat<B10G10R10X2_UNORM>(pDst, srcPixel); break;
    case L16A16_FLOAT: ConvertPixelFromFloat<L16A16_FLOAT>(pDst, srcPixel); break;
    case R10G10B10X2_USCALED: ConvertPixelFromFloat<R10G10B10X2_USCALED>(pDst, srcPixel); break;
    case R8G8B8A8_SSCALED: ConvertPixelFromFloat<R8G8B8A8_SSCALED>(pDst, srcPixel); break;
    case R8G8B8A8_USCALED: ConvertPixelFromFloat<R8G8B8A8_USCALED>(pDst, srcPixel); break;
    case R16G16_SSCALED: ConvertPixelFromFloat<R16G16_SSCALED>(pDst, srcPixel); break;
    case R16G16_USCALED: ConvertPixelFromFloat<R16G16_USCALED>(pDst, srcPixel); break;
    case R32_SSCALED: ConvertPixelFromFloat<R32_SSCALED>(pDst, srcPixel); break;
    case R32_USCALED: ConvertPixelFromFloat<R32_USCALED>(pDst, srcPixel); break;
    case B5G6R5_UNORM: ConvertPixelFromFloat<B5G6R5_UNORM>(pDst, srcPixel); break;
    case B5G6R5_UNORM_SRGB: ConvertPixelFromFloat<B5G6R5_UNORM_SRGB>(pDst, srcPixel); break;
    case B5G5R5A1_UNORM: ConvertPixelFromFloat<B5G5R5A1_UNORM>(pDst, srcPixel); break;
    case B5G5R5A1_UNORM_SRGB: ConvertPixelFromFloat<B5G5R5A1_UNORM_SRGB>(pDst, srcPixel); break;
    case B4G4R4A4_UNORM: ConvertPixelFromFloat<B4G4R4A4_UNORM>(pDst, srcPixel); break;
    case B4G4R4A4_UNORM_SRGB: ConvertPixelFromFloat<B4G4R4A4_UNORM_SRGB>(pDst, srcPixel); break;
    case R8G8_UNORM: ConvertPixelFromFloat<R8G8_UNORM>(pDst, srcPixel); break;
    case R8G8_SNORM: ConvertPixelFromFloat<R8G8_SNORM>(pDst, srcPixel); break;
    case R8G8_SINT: ConvertPixelFromFloat<R8G8_SINT>(pDst, srcPixel); break;
    case R8G8_UINT: ConvertPixelFromFloat<R8G8_UINT>(pDst, srcPixel); break;
    case R16_UNORM: ConvertPixelFromFloat<R16_UNORM>(pDst, srcPixel); break;
    case R16_SNORM: ConvertPixelFromFloat<R16_SNORM>(pDst, srcPixel); break;
    case R16_SINT: ConvertPixelFromFloat<R16_SINT>(pDst, srcPixel); break;
    case R16_UINT: ConvertPixelFromFloat<R16_UINT>(pDst, srcPixel); break;
    case R16_FLOAT: ConvertPixelFromFloat<R16_FLOAT>(pDst, srcPixel); break;
    case I16_UNORM: ConvertPixelFromFloat<I16_UNORM>(pDst, srcPixel); break;
    case L16_UNORM: ConvertPixelFromFloat<L16_UNORM>(pDst, srcPixel); break;
    case A16_UNORM: ConvertPixelFromFloat<A16_UNORM>(pDst, srcPixel); break;
    case L8A8_UNORM: ConvertPixelFromFloat<L8A8_UNORM>(pDst, srcPixel); break;
    case I16_FLOAT: ConvertPixelFromFloat<I16_FLOAT>(pDst, srcPixel); break;
    case L16_FLOAT: ConvertPixelFromFloat<L16_FLOAT>(pDst, srcPixel); break;
    case A16_FLOAT: ConvertPixelFromFloat<A16_FLOAT>(pDst, srcPixel); break;
    case L8A8_UNORM_SRGB: ConvertPixelFromFloat<L8A8_UNORM_SRGB>(pDst, srcPixel); break;
    case B5G5R5X1_UNORM: ConvertPixelFromFloat<B5G5R5X1_UNORM>(pDst, srcPixel); break;
    case B5G5R5X1_UNORM_SRGB: ConvertPixelFromFloat<B5G5R5X1_UNORM_SRGB>(pDst, srcPixel); break;
    case R8G8_SSCALED: ConvertPixelFromFloat<R8G8_SSCALED>(pDst, srcPixel); break;
    case R8G8_USCALED: ConvertPixelFromFloat<R8G8_USCALED>(pDst, srcPixel); break;
    case R16_SSCALED: ConvertPixelFromFloat<R16_SSCALED>(pDst, srcPixel); break;
    case R16_USCALED: ConvertPixelFromFloat<R16_USCALED>(pDst, srcPixel); break;
    case A1B5G5R5_UNORM: ConvertPixelFromFloat<A1B5G5R5_UNORM>(pDst, srcPixel); break;
    case A4B4G4R4_UNORM: ConvertPixelFromFloat<A4B4G4R4_UNORM>(pDst, srcPixel); break;
    case L8A8_UINT: ConvertPixelFromFloat<L8A8_UINT>(pDst, srcPixel); break;
    case L8A8_SINT: ConvertPixelFromFloat<L8A8_SINT>(pDst, srcPixel); break;
    case R8_UNORM: ConvertPixelFromFloat<R8_UNORM>(pDst, srcPixel); break;
    case R8_SNORM: ConvertPixelFromFloat<R8_SNORM>(pDst, srcPixel); break;
    case R8_SINT: ConvertPixelFromFloat<R8_SINT>(pDst, srcPixel); break;
    case R8_UINT: ConvertPixelFromFloat<R8_UINT>(pDst, srcPixel); break;
    case A8_UNORM: ConvertPixelFromFloat<A8_UNORM>(pDst, srcPixel); break;
    case I8_UNORM: ConvertPixelFromFloat<I8_UNORM>(pDst, srcPixel); break;
    case L8_UNORM: ConvertPixelFromFloat<L8_UNORM>(pDst, srcPixel); break;
    case R8_SSCALED: ConvertPixelFromFloat<R8_SSCALED>(pDst, srcPixel); break;
    case R8_USCALED: ConvertPixelFromFloat<R8_USCALED>(pDst, srcPixel); break;
    case L8_UNORM_SRGB: ConvertPixelFromFloat<L8_UNORM_SRGB>(pDst, srcPixel); break;
    case L8_UINT: ConvertPixelFromFloat<L8_UINT>(pDst, srcPixel); break;
    case L8_SINT: ConvertPixelFromFloat<L8_SINT>(pDst, srcPixel); break;
    case I8_UINT: ConvertPixelFromFloat<I8_UINT>(pDst, srcPixel); break;
    case I8_SINT: ConvertPixelFromFloat<I8_SINT>(pDst, srcPixel); break;
    case YCRCB_SWAPUVY: ConvertPixelFromFloat<YCRCB_SWAPUVY>(pDst, srcPixel); break;
    case BC1_UNORM: ConvertPixelFromFloat<BC1_UNORM>(pDst, srcPixel); break;
    case BC2_UNORM: ConvertPixelFromFloat<BC2_UNORM>(pDst, srcPixel); break;
    case BC3_UNORM: ConvertPixelFromFloat<BC3_UNORM>(pDst, srcPixel); break;
    case BC4_UNORM: ConvertPixelFromFloat<BC4_UNORM>(pDst, srcPixel); break;
    case BC5_UNORM: ConvertPixelFromFloat<BC5_UNORM>(pDst, srcPixel); break;
    case BC1_UNORM_SRGB: ConvertPixelFromFloat<BC1_UNORM_SRGB>(pDst, srcPixel); break;
    case BC2_UNORM_SRGB: ConvertPixelFromFloat<BC2_UNORM_SRGB>(pDst, srcPixel); break;
    case BC3_UNORM_SRGB: ConvertPixelFromFloat<BC3_UNORM_SRGB>(pDst, srcPixel); break;
    case YCRCB_SWAPUV: ConvertPixelFromFloat<YCRCB_SWAPUV>(pDst, srcPixel); break;
    case R8G8B8_UNORM: ConvertPixelFromFloat<R8G8B8_UNORM>(pDst, srcPixel); break;
    case R8G8B8_SNORM: ConvertPixelFromFloat<R8G8B8_SNORM>(pDst, srcPixel); break;
    case R8G8B8_SSCALED: ConvertPixelFromFloat<R8G8B8_SSCALED>(pDst, srcPixel); break;
    case R8G8B8_USCALED: ConvertPixelFromFloat<R8G8B8_USCALED>(pDst, srcPixel); break;
    case BC4_SNORM: ConvertPixelFromFloat<BC4_SNORM>(pDst, srcPixel); break;
    case BC5_SNORM: ConvertPixelFromFloat<BC5_SNORM>(pDst, srcPixel); break;
    case R16G16B16_FLOAT: ConvertPixelFromFloat<R16G16B16_FLOAT>(pDst, srcPixel); break;
    case R16G16B16_UNORM: ConvertPixelFromFloat<R16G16B16_UNORM>(pDst, srcPixel); break;
    case R16G16B16_SNORM: ConvertPixelFromFloat<R16G16B16_SNORM>(pDst, srcPixel); break;
    case R16G16B16_SSCALED: ConvertPixelFromFloat<R16G16B16_SSCALED>(pDst, srcPixel); break;
    case R16G16B16_USCALED: ConvertPixelFromFloat<R16G16B16_USCALED>(pDst, srcPixel); break;
    case BC6H_SF16: ConvertPixelFromFloat<BC6H_SF16>(pDst, srcPixel); break;
    case BC7_UNORM: ConvertPixelFromFloat<BC7_UNORM>(pDst, srcPixel); break;
    case BC7_UNORM_SRGB: ConvertPixelFromFloat<BC7_UNORM_SRGB>(pDst, srcPixel); break;
    case BC6H_UF16: ConvertPixelFromFloat<BC6H_UF16>(pDst, srcPixel); break;
    case R8G8B8_UNORM_SRGB: ConvertPixelFromFloat<R8G8B8_UNORM_SRGB>(pDst, srcPixel); break;
    case R16G16B16_UINT: ConvertPixelFromFloat<R16G16B16_UINT>(pDst, srcPixel); break;
    case R16G16B16_SINT: ConvertPixelFromFloat<R16G16B16_SINT>(pDst, srcPixel); break;
    case R10G10B10A2_SNORM: ConvertPixelFromFloat<R10G10B10A2_SNORM>(pDst, srcPixel); break;
    case R10G10B10A2_USCALED: ConvertPixelFromFloat<R10G10B10A2_USCALED>(pDst, srcPixel); break;
    case R10G10B10A2_SSCALED: ConvertPixelFromFloat<R10G10B10A2_SSCALED>(pDst, srcPixel); break;
    case R10G10B10A2_SINT: ConvertPixelFromFloat<R10G10B10A2_SINT>(pDst, srcPixel); break;
    case B10G10R10A2_SNORM: ConvertPixelFromFloat<B10G10R10A2_SNORM>(pDst, srcPixel); break;
    case B10G10R10A2_USCALED: ConvertPixelFromFloat<B10G10R10A2_USCALED>(pDst, srcPixel); break;
    case B10G10R10A2_SSCALED: ConvertPixelFromFloat<B10G10R10A2_SSCALED>(pDst, srcPixel); break;
    case B10G10R10A2_UINT: ConvertPixelFromFloat<B10G10R10A2_UINT>(pDst, srcPixel); break;
    case B10G10R10A2_SINT: ConvertPixelFromFloat<B10G10R10A2_SINT>(pDst, srcPixel); break;
    case R8G8B8_UINT: ConvertPixelFromFloat<R8G8B8_UINT>(pDst, srcPixel); break;
    case R8G8B8_SINT: ConvertPixelFromFloat<R8G8B8_SINT>(pDst, srcPixel); break;
    case RAW: ConvertPixelFromFloat<RAW>(pDst, srcPixel); break;
    default:
        SWR_INVALID("Invalid format: %d", format);
        break;
    }
}
