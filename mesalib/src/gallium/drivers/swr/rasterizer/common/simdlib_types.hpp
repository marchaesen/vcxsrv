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

#if !defined(__cplusplus)
#error C++ compilation required
#endif

#include <immintrin.h>
#include <inttypes.h>
#include <stdint.h>

#define SIMD_ARCH_AVX 0
#define SIMD_ARCH_AVX2 1
#define SIMD_ARCH_AVX512 2

#if !defined(SIMD_ARCH)
#define SIMD_ARCH SIMD_ARCH_AVX
#endif

#if defined(_MSC_VER)
#define SIMDCALL __vectorcall
#define SIMDINLINE __forceinline
#define SIMDALIGN(type_, align_) __declspec(align(align_)) type_
#else
#define SIMDCALL
#define SIMDINLINE inline
#define SIMDALIGN(type_, align_) type_ __attribute__((aligned(align_)))
#endif

// For documentation, please see the following include...
// #include "simdlib_interface.hpp"

namespace SIMDImpl
{
    enum class CompareType
    {
        EQ_OQ    = 0x00, // Equal (ordered, nonsignaling)
        LT_OS    = 0x01, // Less-than (ordered, signaling)
        LE_OS    = 0x02, // Less-than-or-equal (ordered, signaling)
        UNORD_Q  = 0x03, // Unordered (nonsignaling)
        NEQ_UQ   = 0x04, // Not-equal (unordered, nonsignaling)
        NLT_US   = 0x05, // Not-less-than (unordered, signaling)
        NLE_US   = 0x06, // Not-less-than-or-equal (unordered, signaling)
        ORD_Q    = 0x07, // Ordered (nonsignaling)
        EQ_UQ    = 0x08, // Equal (unordered, non-signaling)
        NGE_US   = 0x09, // Not-greater-than-or-equal (unordered, signaling)
        NGT_US   = 0x0A, // Not-greater-than (unordered, signaling)
        FALSE_OQ = 0x0B, // False (ordered, nonsignaling)
        NEQ_OQ   = 0x0C, // Not-equal (ordered, non-signaling)
        GE_OS    = 0x0D, // Greater-than-or-equal (ordered, signaling)
        GT_OS    = 0x0E, // Greater-than (ordered, signaling)
        TRUE_UQ  = 0x0F, // True (unordered, non-signaling)
        EQ_OS    = 0x10, // Equal (ordered, signaling)
        LT_OQ    = 0x11, // Less-than (ordered, nonsignaling)
        LE_OQ    = 0x12, // Less-than-or-equal (ordered, nonsignaling)
        UNORD_S  = 0x13, // Unordered (signaling)
        NEQ_US   = 0x14, // Not-equal (unordered, signaling)
        NLT_UQ   = 0x15, // Not-less-than (unordered, nonsignaling)
        NLE_UQ   = 0x16, // Not-less-than-or-equal (unordered, nonsignaling)
        ORD_S    = 0x17, // Ordered (signaling)
        EQ_US    = 0x18, // Equal (unordered, signaling)
        NGE_UQ   = 0x19, // Not-greater-than-or-equal (unordered, nonsignaling)
        NGT_UQ   = 0x1A, // Not-greater-than (unordered, nonsignaling)
        FALSE_OS = 0x1B, // False (ordered, signaling)
        NEQ_OS   = 0x1C, // Not-equal (ordered, signaling)
        GE_OQ    = 0x1D, // Greater-than-or-equal (ordered, nonsignaling)
        GT_OQ    = 0x1E, // Greater-than (ordered, nonsignaling)
        TRUE_US  = 0x1F, // True (unordered, signaling)
    };

#if SIMD_ARCH >= SIMD_ARCH_AVX512
    enum class CompareTypeInt
    {
        EQ = _MM_CMPINT_EQ, // Equal
        LT = _MM_CMPINT_LT, // Less than
        LE = _MM_CMPINT_LE, // Less than or Equal
        NE = _MM_CMPINT_NE, // Not Equal
        GE = _MM_CMPINT_GE, // Greater than or Equal
        GT = _MM_CMPINT_GT, // Greater than
    };
#endif // SIMD_ARCH >= SIMD_ARCH_AVX512

    enum class ScaleFactor
    {
        SF_1 = 1, // No scaling
        SF_2 = 2, // Scale offset by 2
        SF_4 = 4, // Scale offset by 4
        SF_8 = 8, // Scale offset by 8
    };

    enum class RoundMode
    {
        TO_NEAREST_INT = 0x00, // Round to nearest integer == TRUNCATE(value + 0.5)
        TO_NEG_INF     = 0x01, // Round to negative infinity
        TO_POS_INF     = 0x02, // Round to positive infinity
        TO_ZERO        = 0x03, // Round to 0 a.k.a. truncate
        CUR_DIRECTION  = 0x04, // Round in direction set in MXCSR register

        RAISE_EXC = 0x00, // Raise exception on overflow
        NO_EXC    = 0x08, // Suppress exceptions

        NINT        = static_cast<int>(TO_NEAREST_INT) | static_cast<int>(RAISE_EXC),
        NINT_NOEXC  = static_cast<int>(TO_NEAREST_INT) | static_cast<int>(NO_EXC),
        FLOOR       = static_cast<int>(TO_NEG_INF) | static_cast<int>(RAISE_EXC),
        FLOOR_NOEXC = static_cast<int>(TO_NEG_INF) | static_cast<int>(NO_EXC),
        CEIL        = static_cast<int>(TO_POS_INF) | static_cast<int>(RAISE_EXC),
        CEIL_NOEXC  = static_cast<int>(TO_POS_INF) | static_cast<int>(NO_EXC),
        TRUNC       = static_cast<int>(TO_ZERO) | static_cast<int>(RAISE_EXC),
        TRUNC_NOEXC = static_cast<int>(TO_ZERO) | static_cast<int>(NO_EXC),
        RINT        = static_cast<int>(CUR_DIRECTION) | static_cast<int>(RAISE_EXC),
        NEARBYINT   = static_cast<int>(CUR_DIRECTION) | static_cast<int>(NO_EXC),
    };

    struct Traits
    {
        using CompareType = SIMDImpl::CompareType;
        using ScaleFactor = SIMDImpl::ScaleFactor;
        using RoundMode   = SIMDImpl::RoundMode;
    };

    // Attribute, 4-dimensional attribute in SIMD SOA layout
    template <typename Float, typename Integer, typename Double>
    union Vec4
    {
        Float   v[4];
        Integer vi[4];
        Double  vd[4];
        struct
        {
            Float x;
            Float y;
            Float z;
            Float w;
        };
        SIMDINLINE Float& SIMDCALL operator[](const int i) { return v[i]; }
        SIMDINLINE Float const& SIMDCALL operator[](const int i) const { return v[i]; }
        SIMDINLINE Vec4& SIMDCALL operator=(Vec4 const& in)
        {
            v[0] = in.v[0];
            v[1] = in.v[1];
            v[2] = in.v[2];
            v[3] = in.v[3];
            return *this;
        }
    };

    namespace SIMD128Impl
    {
        union Float
        {
            SIMDINLINE Float() = default;
            SIMDINLINE Float(__m128 in) : v(in) {}
            SIMDINLINE Float& SIMDCALL operator=(__m128 in)
            {
                v = in;
                return *this;
            }
            SIMDINLINE Float& SIMDCALL operator=(Float const& in)
            {
                v = in.v;
                return *this;
            }
            SIMDINLINE SIMDCALL operator __m128() const { return v; }

            SIMDALIGN(__m128, 16) v;
        };

        union Integer
        {
            SIMDINLINE Integer() = default;
            SIMDINLINE Integer(__m128i in) : v(in) {}
            SIMDINLINE Integer& SIMDCALL operator=(__m128i in)
            {
                v = in;
                return *this;
            }
            SIMDINLINE Integer& SIMDCALL operator=(Integer const& in)
            {
                v = in.v;
                return *this;
            }
            SIMDINLINE SIMDCALL operator __m128i() const { return v; }

            SIMDALIGN(__m128i, 16) v;
        };

        union Double
        {
            SIMDINLINE Double() = default;
            SIMDINLINE Double(__m128d in) : v(in) {}
            SIMDINLINE Double& SIMDCALL operator=(__m128d in)
            {
                v = in;
                return *this;
            }
            SIMDINLINE Double& SIMDCALL operator=(Double const& in)
            {
                v = in.v;
                return *this;
            }
            SIMDINLINE SIMDCALL operator __m128d() const { return v; }

            SIMDALIGN(__m128d, 16) v;
        };

        using Vec4 = SIMDImpl::Vec4<Float, Integer, Double>;
        using Mask = uint8_t;

        static const uint32_t SIMD_WIDTH = 4;
    } // namespace SIMD128Impl

    namespace SIMD256Impl
    {
        union Float
        {
            SIMDINLINE Float() = default;
            SIMDINLINE Float(__m256 in) : v(in) {}
            SIMDINLINE Float(SIMD128Impl::Float const& in_lo,
                             SIMD128Impl::Float const& in_hi = _mm_setzero_ps())
            {
                v = _mm256_insertf128_ps(_mm256_castps128_ps256(in_lo), in_hi, 0x1);
            }
            SIMDINLINE Float& SIMDCALL operator=(__m256 in)
            {
                v = in;
                return *this;
            }
            SIMDINLINE Float& SIMDCALL operator=(Float const& in)
            {
                v = in.v;
                return *this;
            }
            SIMDINLINE SIMDCALL operator __m256() const { return v; }

            SIMDALIGN(__m256, 32) v;
            SIMD128Impl::Float v4[2];
        };

        union Integer
        {
            SIMDINLINE Integer() = default;
            SIMDINLINE Integer(__m256i in) : v(in) {}
            SIMDINLINE Integer(SIMD128Impl::Integer const& in_lo,
                               SIMD128Impl::Integer const& in_hi = _mm_setzero_si128())
            {
                v = _mm256_insertf128_si256(_mm256_castsi128_si256(in_lo), in_hi, 0x1);
            }
            SIMDINLINE Integer& SIMDCALL operator=(__m256i in)
            {
                v = in;
                return *this;
            }
            SIMDINLINE Integer& SIMDCALL operator=(Integer const& in)
            {
                v = in.v;
                return *this;
            }
            SIMDINLINE SIMDCALL operator __m256i() const { return v; }

            SIMDALIGN(__m256i, 32) v;
            SIMD128Impl::Integer v4[2];
        };

        union Double
        {
            SIMDINLINE Double() = default;
            SIMDINLINE Double(__m256d const& in) : v(in) {}
            SIMDINLINE Double(SIMD128Impl::Double const& in_lo,
                              SIMD128Impl::Double const& in_hi = _mm_setzero_pd())
            {
                v = _mm256_insertf128_pd(_mm256_castpd128_pd256(in_lo), in_hi, 0x1);
            }
            SIMDINLINE Double& SIMDCALL operator=(__m256d in)
            {
                v = in;
                return *this;
            }
            SIMDINLINE Double& SIMDCALL operator=(Double const& in)
            {
                v = in.v;
                return *this;
            }
            SIMDINLINE SIMDCALL operator __m256d() const { return v; }

            SIMDALIGN(__m256d, 32) v;
            SIMD128Impl::Double v4[2];
        };

        using Vec4 = SIMDImpl::Vec4<Float, Integer, Double>;
        using Mask = uint8_t;

        static const uint32_t SIMD_WIDTH = 8;
    } // namespace SIMD256Impl

    namespace SIMD512Impl
    {
#if !(defined(__AVX512F__) || defined(_ZMMINTRIN_H_INCLUDED))
        // Define AVX512 types if not included via immintrin.h.
        // All data members of these types are ONLY to viewed
        // in a debugger.  Do NOT access them via code!
        union __m512
        {
        private:
            float m512_f32[16];
        };
        struct __m512d
        {
        private:
            double m512d_f64[8];
        };

        union __m512i
        {
        private:
            int8_t   m512i_i8[64];
            int16_t  m512i_i16[32];
            int32_t  m512i_i32[16];
            int64_t  m512i_i64[8];
            uint8_t  m512i_u8[64];
            uint16_t m512i_u16[32];
            uint32_t m512i_u32[16];
            uint64_t m512i_u64[8];
        };

        using __mmask16 = uint16_t;
#endif

#if defined(__INTEL_COMPILER) || (SIMD_ARCH >= SIMD_ARCH_AVX512)
#define SIMD_ALIGNMENT_BYTES 64
#else
#define SIMD_ALIGNMENT_BYTES 32
#endif

        union Float
        {
            SIMDINLINE Float() = default;
            SIMDINLINE Float(__m512 in) : v(in) {}
            SIMDINLINE Float(SIMD256Impl::Float const& in_lo,
                             SIMD256Impl::Float const& in_hi = _mm256_setzero_ps())
            {
                v8[0] = in_lo;
                v8[1] = in_hi;
            }
            SIMDINLINE Float& SIMDCALL operator=(__m512 in)
            {
                v = in;
                return *this;
            }
            SIMDINLINE Float& SIMDCALL operator=(Float const& in)
            {
#if SIMD_ARCH >= SIMD_ARCH_AVX512
                v = in.v;
#else
                v8[0] = in.v8[0];
                v8[1] = in.v8[1];
#endif
                return *this;
            }
            SIMDINLINE SIMDCALL operator __m512() const { return v; }

            SIMDALIGN(__m512, SIMD_ALIGNMENT_BYTES) v;
            SIMD256Impl::Float v8[2];
        };

        union Integer
        {
            SIMDINLINE Integer() = default;
            SIMDINLINE Integer(__m512i in) : v(in) {}
            SIMDINLINE Integer(SIMD256Impl::Integer const& in_lo,
                               SIMD256Impl::Integer const& in_hi = _mm256_setzero_si256())
            {
                v8[0] = in_lo;
                v8[1] = in_hi;
            }
            SIMDINLINE Integer& SIMDCALL operator=(__m512i in)
            {
                v = in;
                return *this;
            }
            SIMDINLINE Integer& SIMDCALL operator=(Integer const& in)
            {
#if SIMD_ARCH >= SIMD_ARCH_AVX512
                v = in.v;
#else
                v8[0] = in.v8[0];
                v8[1] = in.v8[1];
#endif
                return *this;
            }

            SIMDINLINE SIMDCALL operator __m512i() const { return v; }

            SIMDALIGN(__m512i, SIMD_ALIGNMENT_BYTES) v;
            SIMD256Impl::Integer v8[2];
        };

        union Double
        {
            SIMDINLINE Double() = default;
            SIMDINLINE Double(__m512d in) : v(in) {}
            SIMDINLINE Double(SIMD256Impl::Double const& in_lo,
                              SIMD256Impl::Double const& in_hi = _mm256_setzero_pd())
            {
                v8[0] = in_lo;
                v8[1] = in_hi;
            }
            SIMDINLINE Double& SIMDCALL operator=(__m512d in)
            {
                v = in;
                return *this;
            }
            SIMDINLINE Double& SIMDCALL operator=(Double const& in)
            {
#if SIMD_ARCH >= SIMD_ARCH_AVX512
                v = in.v;
#else
                v8[0] = in.v8[0];
                v8[1] = in.v8[1];
#endif
                return *this;
            }

            SIMDINLINE SIMDCALL operator __m512d() const { return v; }

            SIMDALIGN(__m512d, SIMD_ALIGNMENT_BYTES) v;
            SIMD256Impl::Double v8[2];
        };

        typedef SIMDImpl::Vec4<Float, Integer, Double> SIMDALIGN(Vec4, 64);
        using Mask = __mmask16;

        static const uint32_t SIMD_WIDTH = 16;

#undef SIMD_ALIGNMENT_BYTES
    } // namespace SIMD512Impl
} // namespace SIMDImpl
