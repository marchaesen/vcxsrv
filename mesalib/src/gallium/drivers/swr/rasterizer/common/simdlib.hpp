/****************************************************************************
 * Copyright (C) 2017 Intel Corporation.   All Rights Reserved.
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
 ****************************************************************************/
#pragma once

#include "simdlib_types.hpp"

// For documentation, please see the following include...
// #include "simdlib_interface.hpp"

namespace SIMDImpl
{
    namespace SIMD128Impl
    {
#if SIMD_ARCH >= SIMD_ARCH_AVX
        struct AVXImpl
        {
#define __SIMD_LIB_AVX_HPP__
#include "simdlib_128_avx.inl"
#undef __SIMD_LIB_AVX_HPP__
        }; // struct AVXImpl
#endif     // #if SIMD_ARCH >= SIMD_ARCH_AVX

#if SIMD_ARCH >= SIMD_ARCH_AVX2
        struct AVX2Impl : AVXImpl
        {
#define __SIMD_LIB_AVX2_HPP__
#include "simdlib_128_avx2.inl"
#undef __SIMD_LIB_AVX2_HPP__
        }; // struct AVX2Impl
#endif     // #if SIMD_ARCH >= SIMD_ARCH_AVX2

#if SIMD_ARCH >= SIMD_ARCH_AVX512
        struct AVX512Impl : AVX2Impl
        {
#if defined(SIMD_OPT_128_AVX512)
#define __SIMD_LIB_AVX512_HPP__
#include "simdlib_128_avx512.inl"
#if defined(SIMD_ARCH_KNIGHTS)
#include "simdlib_128_avx512_knights.inl"
#else // optimize for core
#include "simdlib_128_avx512_core.inl"
#endif // defined(SIMD_ARCH_KNIGHTS)
#undef __SIMD_LIB_AVX512_HPP__
#endif     // SIMD_OPT_128_AVX512
        }; // struct AVX2Impl
#endif     // #if SIMD_ARCH >= SIMD_ARCH_AVX512

        struct Traits : SIMDImpl::Traits
        {
#if SIMD_ARCH == SIMD_ARCH_AVX
            using IsaImpl = AVXImpl;
#elif SIMD_ARCH == SIMD_ARCH_AVX2
            using IsaImpl = AVX2Impl;
#elif SIMD_ARCH == SIMD_ARCH_AVX512
            using IsaImpl = AVX512Impl;
#else
#error Invalid value for SIMD_ARCH
#endif

            using Float   = SIMD128Impl::Float;
            using Double  = SIMD128Impl::Double;
            using Integer = SIMD128Impl::Integer;
            using Vec4    = SIMD128Impl::Vec4;
            using Mask    = SIMD128Impl::Mask;
        };
    } // namespace SIMD128Impl

    namespace SIMD256Impl
    {
#if SIMD_ARCH >= SIMD_ARCH_AVX
        struct AVXImpl
        {
#define __SIMD_LIB_AVX_HPP__
#include "simdlib_256_avx.inl"
#undef __SIMD_LIB_AVX_HPP__
        }; // struct AVXImpl
#endif     // #if SIMD_ARCH >= SIMD_ARCH_AVX

#if SIMD_ARCH >= SIMD_ARCH_AVX2
        struct AVX2Impl : AVXImpl
        {
#define __SIMD_LIB_AVX2_HPP__
#include "simdlib_256_avx2.inl"
#undef __SIMD_LIB_AVX2_HPP__
        }; // struct AVX2Impl
#endif     // #if SIMD_ARCH >= SIMD_ARCH_AVX2

#if SIMD_ARCH >= SIMD_ARCH_AVX512
        struct AVX512Impl : AVX2Impl
        {
#if defined(SIMD_OPT_256_AVX512)
#define __SIMD_LIB_AVX512_HPP__
#include "simdlib_256_avx512.inl"
#if defined(SIMD_ARCH_KNIGHTS)
#include "simdlib_256_avx512_knights.inl"
#else // optimize for core
#include "simdlib_256_avx512_core.inl"
#endif // defined(SIMD_ARCH_KNIGHTS)
#undef __SIMD_LIB_AVX512_HPP__
#endif     // SIMD_OPT_256_AVX512
        }; // struct AVX2Impl
#endif     // #if SIMD_ARCH >= SIMD_ARCH_AVX512

        struct Traits : SIMDImpl::Traits
        {
#if SIMD_ARCH == SIMD_ARCH_AVX
            using IsaImpl = AVXImpl;
#elif SIMD_ARCH == SIMD_ARCH_AVX2
            using IsaImpl = AVX2Impl;
#elif SIMD_ARCH == SIMD_ARCH_AVX512
            using IsaImpl = AVX512Impl;
#else
#error Invalid value for SIMD_ARCH
#endif

            using Float   = SIMD256Impl::Float;
            using Double  = SIMD256Impl::Double;
            using Integer = SIMD256Impl::Integer;
            using Vec4    = SIMD256Impl::Vec4;
            using Mask    = SIMD256Impl::Mask;
        };
    } // namespace SIMD256Impl

    namespace SIMD512Impl
    {
#if SIMD_ARCH >= SIMD_ARCH_AVX
        template <typename SIMD256T>
        struct AVXImplBase
        {
#define __SIMD_LIB_AVX_HPP__
#include "simdlib_512_emu.inl"
#include "simdlib_512_emu_masks.inl"
#undef __SIMD_LIB_AVX_HPP__
        }; // struct AVXImplBase
        using AVXImpl = AVXImplBase<SIMD256Impl::AVXImpl>;
#endif // #if SIMD_ARCH >= SIMD_ARCH_AVX

#if SIMD_ARCH >= SIMD_ARCH_AVX2
        using AVX2Impl = AVXImplBase<SIMD256Impl::AVX2Impl>;
#endif // #if SIMD_ARCH >= SIMD_ARCH_AVX2

#if SIMD_ARCH >= SIMD_ARCH_AVX512
        struct AVX512Impl : AVXImplBase<SIMD256Impl::AVX512Impl>
        {
#define __SIMD_LIB_AVX512_HPP__
#include "simdlib_512_avx512.inl"
#include "simdlib_512_avx512_masks.inl"
#if defined(SIMD_ARCH_KNIGHTS)
#include "simdlib_512_avx512_knights.inl"
#include "simdlib_512_avx512_masks_knights.inl"
#else // optimize for core
#include "simdlib_512_avx512_core.inl"
#include "simdlib_512_avx512_masks_core.inl"
#endif // defined(SIMD_ARCH_KNIGHTS)
#undef __SIMD_LIB_AVX512_HPP__
        }; // struct AVX512ImplBase
#endif     // #if SIMD_ARCH >= SIMD_ARCH_AVX512

        struct Traits : SIMDImpl::Traits
        {
#if SIMD_ARCH == SIMD_ARCH_AVX
            using IsaImpl = AVXImpl;
#elif SIMD_ARCH == SIMD_ARCH_AVX2
            using IsaImpl = AVX2Impl;
#elif SIMD_ARCH == SIMD_ARCH_AVX512
            using IsaImpl = AVX512Impl;
#else
#error Invalid value for SIMD_ARCH
#endif

            using Float   = SIMD512Impl::Float;
            using Double  = SIMD512Impl::Double;
            using Integer = SIMD512Impl::Integer;
            using Vec4    = SIMD512Impl::Vec4;
            using Mask    = SIMD512Impl::Mask;
        };
    } // namespace SIMD512Impl
} // namespace SIMDImpl

template <typename Traits>
struct SIMDBase : Traits::IsaImpl
{
    using CompareType = typename Traits::CompareType;
    using ScaleFactor = typename Traits::ScaleFactor;
    using RoundMode   = typename Traits::RoundMode;
    using SIMD        = typename Traits::IsaImpl;
    using Float       = typename Traits::Float;
    using Double      = typename Traits::Double;
    using Integer     = typename Traits::Integer;
    using Vec4        = typename Traits::Vec4;
    using Mask        = typename Traits::Mask;
}; // struct SIMDBase

using SIMD128 = SIMDBase<SIMDImpl::SIMD128Impl::Traits>;
using SIMD256 = SIMDBase<SIMDImpl::SIMD256Impl::Traits>;
using SIMD512 = SIMDBase<SIMDImpl::SIMD512Impl::Traits>;

template <typename SIMD_T>
using CompareType = typename SIMD_T::CompareType;
template <typename SIMD_T>
using ScaleFactor = typename SIMD_T::ScaleFactor;
template <typename SIMD_T>
using RoundMode = typename SIMD_T::RoundMode;
template <typename SIMD_T>
using Float = typename SIMD_T::Float;
template <typename SIMD_T>
using Double = typename SIMD_T::Double;
template <typename SIMD_T>
using Integer = typename SIMD_T::Integer;
template <typename SIMD_T>
using Vec4 = typename SIMD_T::Vec4;
template <typename SIMD_T>
using Mask = typename SIMD_T::Mask;

