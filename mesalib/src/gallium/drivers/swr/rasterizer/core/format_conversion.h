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
 * @file format_conversion.h
 *
 * @brief API implementation
 *
 ******************************************************************************/
#include "format_types.h"
#include "format_traits.h"

//////////////////////////////////////////////////////////////////////////
/// @brief Load SIMD packed pixels in SOA format and converts to
///        SOA RGBA32_FLOAT format.
/// @param pSrc - source data in SOA form
/// @param dst - output data in SOA form
template <typename SIMD_T, SWR_FORMAT SrcFormat>
INLINE void SIMDCALL LoadSOA(const uint8_t* pSrc, Vec4<SIMD_T>& dst)
{
    // fast path for float32
    if ((FormatTraits<SrcFormat>::GetType(0) == SWR_TYPE_FLOAT) &&
        (FormatTraits<SrcFormat>::GetBPC(0) == 32))
    {
        auto lambda = [&](int comp)
        {
            Float<SIMD_T> vComp =
                SIMD_T::load_ps(reinterpret_cast<const float*>(pSrc + comp * sizeof(Float<SIMD_T>)));

            dst.v[FormatTraits<SrcFormat>::swizzle(comp)] = vComp;
        };

        UnrollerL<0, FormatTraits<SrcFormat>::numComps, 1>::step(lambda);
        return;
    }

    auto lambda = [&](int comp)
    {
        // load SIMD components
        Float<SIMD_T> vComp;
        FormatTraits<SrcFormat>::loadSOA(comp, pSrc, vComp);

        // unpack
        vComp = FormatTraits<SrcFormat>::unpack(comp, vComp);

        // convert
        if (FormatTraits<SrcFormat>::isNormalized(comp))
        {
            vComp = SIMD_T::cvtepi32_ps(SIMD_T::castps_si(vComp));
            vComp = SIMD_T::mul_ps(vComp, SIMD_T::set1_ps(FormatTraits<SrcFormat>::toFloat(comp)));
        }

        dst.v[FormatTraits<SrcFormat>::swizzle(comp)] = vComp;

        // is there a better way to get this from the SIMD traits?
        const uint32_t SIMD_WIDTH = sizeof(typename SIMD_T::Float) / sizeof(float);

        pSrc += (FormatTraits<SrcFormat>::GetBPC(comp) * SIMD_WIDTH) / 8;
    };

    UnrollerL<0, FormatTraits<SrcFormat>::numComps, 1>::step(lambda);
}

template <SWR_FORMAT SrcFormat>
INLINE void SIMDCALL LoadSOA(const uint8_t* pSrc, simdvector& dst)
{
    LoadSOA<SIMD256, SrcFormat>(pSrc, dst);
}

template <SWR_FORMAT SrcFormat>
INLINE void SIMDCALL LoadSOA(const uint8_t* pSrc, simd16vector& dst)
{
    LoadSOA<SIMD512, SrcFormat>(pSrc, dst);
}

//////////////////////////////////////////////////////////////////////////
/// @brief Clamps the given component based on the requirements on the
///        Format template arg
/// @param vComp - SIMD vector of floats
/// @param Component - component
template <typename SIMD_T, SWR_FORMAT Format>
INLINE Float<SIMD_T> SIMDCALL Clamp(Float<SIMD_T> const& v, uint32_t Component)
{
    Float<SIMD_T> vComp = v;
    if (Component >= 4 || Component < 0)
    {
	// Component shouldn't out of <0;3> range
	assert(false);
	return vComp;
    }
    if (FormatTraits<Format>::isNormalized(Component))
    {
        if (FormatTraits<Format>::GetType(Component) == SWR_TYPE_UNORM)
        {
            vComp = SIMD_T::max_ps(vComp, SIMD_T::setzero_ps());
        }

        if (FormatTraits<Format>::GetType(Component) == SWR_TYPE_SNORM)
        {
            vComp = SIMD_T::max_ps(vComp, SIMD_T::set1_ps(-1.0f));
        }
        vComp = SIMD_T::min_ps(vComp, SIMD_T::set1_ps(1.0f));
    }
    else if (FormatTraits<Format>::GetBPC(Component) < 32)
    {
        if (FormatTraits<Format>::GetType(Component) == SWR_TYPE_UINT)
        {
            int           iMax = (1 << FormatTraits<Format>::GetBPC(Component)) - 1;
            int           iMin = 0;
            Integer<SIMD_T> vCompi = SIMD_T::castps_si(vComp);
            vCompi = SIMD_T::max_epu32(vCompi, SIMD_T::set1_epi32(iMin));
            vCompi = SIMD_T::min_epu32(vCompi, SIMD_T::set1_epi32(iMax));
            vComp = SIMD_T::castsi_ps(vCompi);
        }
        else if (FormatTraits<Format>::GetType(Component) == SWR_TYPE_SINT)
        {
            int           iMax = (1 << (FormatTraits<Format>::GetBPC(Component) - 1)) - 1;
            int           iMin = -1 - iMax;
            Integer<SIMD_T> vCompi = SIMD_T::castps_si(vComp);
            vCompi = SIMD_T::max_epi32(vCompi, SIMD_T::set1_epi32(iMin));
            vCompi = SIMD_T::min_epi32(vCompi, SIMD_T::set1_epi32(iMax));
            vComp = SIMD_T::castsi_ps(vCompi);
        }
    }

    return vComp;
}

template <SWR_FORMAT Format>
INLINE simdscalar SIMDCALL Clamp(simdscalar const& v, uint32_t Component)
{
    return Clamp<SIMD256, Format>(v, Component);
}

template <SWR_FORMAT Format>
INLINE simd16scalar SIMDCALL Clamp(simd16scalar const& v, uint32_t Component)
{
    return Clamp<SIMD512, Format>(v, Component);
}

//////////////////////////////////////////////////////////////////////////
/// @brief Normalize the given component based on the requirements on the
///        Format template arg
/// @param vComp - SIMD vector of floats
/// @param Component - component
template <typename SIMD_T, SWR_FORMAT Format>
INLINE Float<SIMD_T> SIMDCALL Normalize(Float<SIMD_T> const& vComp, uint32_t Component)
{
    Float<SIMD_T> r = vComp;
    if (FormatTraits<Format>::isNormalized(Component))
    {
        r = SIMD_T::mul_ps(r, SIMD_T::set1_ps(FormatTraits<Format>::fromFloat(Component)));
        r = SIMD_T::castsi_ps(SIMD_T::cvtps_epi32(r));
    }
    return r;
}

template <SWR_FORMAT Format>
INLINE simdscalar SIMDCALL Normalize(simdscalar const& vComp, uint32_t Component)
{
    return Normalize<SIMD256, Format>(vComp, Component);
}

template <SWR_FORMAT Format>
INLINE simd16scalar SIMDCALL Normalize(simd16scalar const& vComp, uint32_t Component)
{
    return Normalize<SIMD512, Format>(vComp, Component);
}

//////////////////////////////////////////////////////////////////////////
/// @brief Convert and store simdvector of pixels in SOA
///        RGBA32_FLOAT to SOA format
/// @param src - source data in SOA form
/// @param dst - output data in SOA form
template <typename SIMD_T, SWR_FORMAT DstFormat>
INLINE void SIMDCALL StoreSOA(const Vec4<SIMD_T>& src, uint8_t* pDst)
{
    // fast path for float32
    if ((FormatTraits<DstFormat>::GetType(0) == SWR_TYPE_FLOAT) &&
        (FormatTraits<DstFormat>::GetBPC(0) == 32))
    {
        for (uint32_t comp = 0; comp < FormatTraits<DstFormat>::numComps; ++comp)
        {
            Float<SIMD_T> vComp = src.v[FormatTraits<DstFormat>::swizzle(comp)];

            // Gamma-correct
            if (FormatTraits<DstFormat>::isSRGB)
            {
                if (comp < 3) // Input format is always RGBA32_FLOAT.
                {
                    vComp = FormatTraits<R32G32B32A32_FLOAT>::convertSrgb(comp, vComp);
                }
            }

            SIMD_T::store_ps(reinterpret_cast<float*>(pDst + comp * sizeof(simd16scalar)), vComp);
        }
        return;
    }

    auto lambda = [&](int comp) {
        Float<SIMD_T> vComp = src.v[FormatTraits<DstFormat>::swizzle(comp)];

        // Gamma-correct
        if (FormatTraits<DstFormat>::isSRGB)
        {
            if (comp < 3) // Input format is always RGBA32_FLOAT.
            {
                vComp = FormatTraits<R32G32B32A32_FLOAT>::convertSrgb(comp, vComp);
            }
        }

        // clamp
        vComp = Clamp<SIMD_T, DstFormat>(vComp, comp);

        // normalize
        vComp = Normalize<SIMD_T, DstFormat>(vComp, comp);

        // pack
        vComp = FormatTraits<DstFormat>::pack(comp, vComp);

        // store
        FormatTraits<DstFormat>::storeSOA(comp, pDst, vComp);

        // is there a better way to get this from the SIMD traits?
        const uint32_t SIMD_WIDTH = sizeof(typename SIMD_T::Float) / sizeof(float);

        pDst += (FormatTraits<DstFormat>::GetBPC(comp) * SIMD_WIDTH) / 8;
    };

    UnrollerL<0, FormatTraits<DstFormat>::numComps, 1>::step(lambda);
}

template <SWR_FORMAT DstFormat>
INLINE void SIMDCALL StoreSOA(const simdvector& src, uint8_t* pDst)
{
    StoreSOA<SIMD256, DstFormat>(src, pDst);
}

template <SWR_FORMAT DstFormat>
INLINE void SIMDCALL StoreSOA(const simd16vector& src, uint8_t* pDst)
{
    StoreSOA<SIMD512, DstFormat>(src, pDst);
}

