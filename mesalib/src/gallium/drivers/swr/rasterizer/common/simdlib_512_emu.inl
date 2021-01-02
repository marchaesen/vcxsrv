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
#if !defined(__SIMD_LIB_AVX_HPP__)
#error Do not include this file directly, use "simdlib.hpp" instead.
#endif

//============================================================================
// SIMD16 AVX (1) implementation
//============================================================================

static const int TARGET_SIMD_WIDTH = 8;
using SIMD128T                     = SIMD128Impl::AVXImpl;

#define SIMD_WRAPPER_1(op)                              \
    static SIMDINLINE Float SIMDCALL op(Float const& a) \
    {                                                   \
        return Float{                                   \
            SIMD256T::op(a.v8[0]),                      \
            SIMD256T::op(a.v8[1]),                      \
        };                                              \
    }

#define SIMD_WRAPPER_2(op)                                              \
    static SIMDINLINE Float SIMDCALL op(Float const& a, Float const& b) \
    {                                                                   \
        return Float{                                                   \
            SIMD256T::op(a.v8[0], b.v8[0]),                             \
            SIMD256T::op(a.v8[1], b.v8[1]),                             \
        };                                                              \
    }

#define SIMD_WRAPPER_2I(op)                                                              \
    template <int ImmT>                                                                  \
    static SIMDINLINE Float SIMDCALL op(Float const& a, Float const& b)                  \
    {                                                                                    \
        return Float{                                                                    \
            SIMD256T::template op<0xFF & ImmT>(a.v8[0], b.v8[0]),                        \
            SIMD256T::template op<0xFF & (ImmT >> TARGET_SIMD_WIDTH)>(a.v8[1], b.v8[1]), \
        };                                                                               \
    }

#define SIMD_WRAPPER_2I_1(op)                                           \
    template <int ImmT>                                                 \
    static SIMDINLINE Float SIMDCALL op(Float const& a, Float const& b) \
    {                                                                   \
        return Float{                                                   \
            SIMD256T::template op<ImmT>(a.v8[0], b.v8[0]),              \
            SIMD256T::template op<ImmT>(a.v8[1], b.v8[1]),              \
        };                                                              \
    }

#define SIMD_WRAPPER_3(op)                                                              \
    static SIMDINLINE Float SIMDCALL op(Float const& a, Float const& b, Float const& c) \
    {                                                                                   \
        return Float{                                                                   \
            SIMD256T::op(a.v8[0], b.v8[0], c.v8[0]),                                    \
            SIMD256T::op(a.v8[1], b.v8[1], c.v8[1]),                                    \
        };                                                                              \
    }

#define SIMD_IWRAPPER_1(op)                                 \
    static SIMDINLINE Integer SIMDCALL op(Integer const& a) \
    {                                                       \
        return Integer{                                     \
            SIMD256T::op(a.v8[0]),                          \
            SIMD256T::op(a.v8[1]),                          \
        };                                                  \
    }

#define SIMD_IWRAPPER_2(op)                                                   \
    static SIMDINLINE Integer SIMDCALL op(Integer const& a, Integer const& b) \
    {                                                                         \
        return Integer{                                                       \
            SIMD256T::op(a.v8[0], b.v8[0]),                                   \
            SIMD256T::op(a.v8[1], b.v8[1]),                                   \
        };                                                                    \
    }

#define SIMD_IWRAPPER_2I(op)                                                             \
    template <int ImmT>                                                                  \
    static SIMDINLINE Integer SIMDCALL op(Integer const& a, Integer const& b)            \
    {                                                                                    \
        return Integer{                                                                  \
            SIMD256T::template op<0xFF & ImmT>(a.v8[0], b.v8[0]),                        \
            SIMD256T::template op<0xFF & (ImmT >> TARGET_SIMD_WIDTH)>(a.v8[1], b.v8[1]), \
        };                                                                               \
    }

#define SIMD_IWRAPPER_2I_1(op)                                                \
    template <int ImmT>                                                       \
    static SIMDINLINE Integer SIMDCALL op(Integer const& a, Integer const& b) \
    {                                                                         \
        return Integer{                                                       \
            SIMD256T::template op<ImmT>(a.v8[0], b.v8[0]),                    \
            SIMD256T::template op<ImmT>(a.v8[1], b.v8[1]),                    \
        };                                                                    \
    }

#define SIMD_IWRAPPER_2I_2(op)                                                \
    template <int ImmT>                                                       \
    static SIMDINLINE Integer SIMDCALL op(Integer const& a, Integer const& b) \
    {                                                                         \
        return Integer{                                                       \
            SIMD256T::template op<0xF & ImmT>(a.v8[0], b.v8[0]),              \
            SIMD256T::template op<0xF & (ImmT >> 4)>(a.v8[1], b.v8[1]),       \
        };                                                                    \
    }

#define SIMD_IWRAPPER_3(op)                                                                     \
    static SIMDINLINE Integer SIMDCALL op(Integer const& a, Integer const& b, Integer const& c) \
    {                                                                                           \
        return Integer{                                                                         \
            SIMD256T::op(a.v8[0], b.v8[0], c.v8[0]),                                            \
            SIMD256T::op(a.v8[1], b.v8[1], c.v8[1]),                                            \
        };                                                                                      \
    }

//-----------------------------------------------------------------------
// Single precision floating point arithmetic operations
//-----------------------------------------------------------------------
SIMD_WRAPPER_2(add_ps);   // return a + b
SIMD_WRAPPER_2(div_ps);   // return a / b
SIMD_WRAPPER_3(fmadd_ps); // return (a * b) + c
SIMD_WRAPPER_3(fmsub_ps); // return (a * b) - c
SIMD_WRAPPER_2(max_ps);   // return (a > b) ? a : b
SIMD_WRAPPER_2(min_ps);   // return (a < b) ? a : b
SIMD_WRAPPER_2(mul_ps);   // return a * b
SIMD_WRAPPER_1(rcp_ps);   // return 1.0f / a
SIMD_WRAPPER_1(rsqrt_ps); // return 1.0f / sqrt(a)
SIMD_WRAPPER_2(sub_ps);   // return a - b

template <RoundMode RMT>
static SIMDINLINE Float SIMDCALL round_ps(Float const& a)
{
    return Float{
        SIMD256T::template round_ps<RMT>(a.v8[0]),
        SIMD256T::template round_ps<RMT>(a.v8[1]),
    };
}

static SIMDINLINE Float SIMDCALL ceil_ps(Float const& a)
{
    return round_ps<RoundMode::CEIL_NOEXC>(a);
}
static SIMDINLINE Float SIMDCALL floor_ps(Float const& a)
{
    return round_ps<RoundMode::FLOOR_NOEXC>(a);
}

//-----------------------------------------------------------------------
// Integer (various width) arithmetic operations
//-----------------------------------------------------------------------
SIMD_IWRAPPER_1(abs_epi32); // return absolute_value(a) (int32)
SIMD_IWRAPPER_2(add_epi32); // return a + b (int32)
SIMD_IWRAPPER_2(add_epi8);  // return a + b (int8)
SIMD_IWRAPPER_2(adds_epu8); // return ((a + b) > 0xff) ? 0xff : (a + b) (uint8)
SIMD_IWRAPPER_2(max_epi32); // return (a > b) ? a : b (int32)
SIMD_IWRAPPER_2(max_epu32); // return (a > b) ? a : b (uint32)
SIMD_IWRAPPER_2(min_epi32); // return (a < b) ? a : b (int32)
SIMD_IWRAPPER_2(min_epu32); // return (a < b) ? a : b (uint32)
SIMD_IWRAPPER_2(mul_epi32); // return a * b (int32)

// return (a * b) & 0xFFFFFFFF
//
// Multiply the packed 32-bit integers in a and b, producing intermediate 64-bit integers,
// and store the low 32 bits of the intermediate integers in dst.
SIMD_IWRAPPER_2(mullo_epi32);
SIMD_IWRAPPER_2(sub_epi32); // return a - b (int32)
SIMD_IWRAPPER_2(sub_epi64); // return a - b (int64)
SIMD_IWRAPPER_2(subs_epu8); // return (b > a) ? 0 : (a - b) (uint8)

//-----------------------------------------------------------------------
// Logical operations
//-----------------------------------------------------------------------
SIMD_WRAPPER_2(and_ps);     // return a & b       (float treated as int)
SIMD_IWRAPPER_2(and_si);    // return a & b       (int)
SIMD_WRAPPER_2(andnot_ps);  // return (~a) & b    (float treated as int)
SIMD_IWRAPPER_2(andnot_si); // return (~a) & b    (int)
SIMD_WRAPPER_2(or_ps);      // return a | b       (float treated as int)
SIMD_IWRAPPER_2(or_si);     // return a | b       (int)
SIMD_WRAPPER_2(xor_ps);     // return a ^ b       (float treated as int)
SIMD_IWRAPPER_2(xor_si);    // return a ^ b       (int)

//-----------------------------------------------------------------------
// Shift operations
//-----------------------------------------------------------------------
template <int ImmT>
static SIMDINLINE Integer SIMDCALL slli_epi32(Integer const& a) // return a << ImmT
{
    return Integer{
        SIMD256T::template slli_epi32<ImmT>(a.v8[0]),
        SIMD256T::template slli_epi32<ImmT>(a.v8[1]),
    };
}

SIMD_IWRAPPER_2(sllv_epi32); // return a << b      (uint32)

template <int ImmT>
static SIMDINLINE Integer SIMDCALL srai_epi32(Integer const& a) // return a >> ImmT   (int32)
{
    return Integer{
        SIMD256T::template srai_epi32<ImmT>(a.v8[0]),
        SIMD256T::template srai_epi32<ImmT>(a.v8[1]),
    };
}

template <int ImmT>
static SIMDINLINE Integer SIMDCALL srli_epi32(Integer const& a) // return a >> ImmT   (uint32)
{
    return Integer{
        SIMD256T::template srli_epi32<ImmT>(a.v8[0]),
        SIMD256T::template srli_epi32<ImmT>(a.v8[1]),
    };
}

template <int ImmT>                                          // for each 128-bit lane:
static SIMDINLINE Integer SIMDCALL srli_si(Integer const& a) //  return a >> (ImmT*8) (uint)
{
    return Integer{
        SIMD256T::template srli_si<ImmT>(a.v8[0]),
        SIMD256T::template srli_si<ImmT>(a.v8[1]),
    };
}
template <int ImmT>
static SIMDINLINE Float SIMDCALL
                        srlisi_ps(Float const& a) // same as srli_si, but with Float cast to int
{
    return Float{
        SIMD256T::template srlisi_ps<ImmT>(a.v8[0]),
        SIMD256T::template srlisi_ps<ImmT>(a.v8[1]),
    };
}

SIMD_IWRAPPER_2(srlv_epi32); // return a >> b      (uint32)

//-----------------------------------------------------------------------
// Conversion operations
//-----------------------------------------------------------------------
static SIMDINLINE Float SIMDCALL castpd_ps(Double const& a) // return *(Float*)(&a)
{
    return Float{
        SIMD256T::castpd_ps(a.v8[0]),
        SIMD256T::castpd_ps(a.v8[1]),
    };
}

static SIMDINLINE Integer SIMDCALL castps_si(Float const& a) // return *(Integer*)(&a)
{
    return Integer{
        SIMD256T::castps_si(a.v8[0]),
        SIMD256T::castps_si(a.v8[1]),
    };
}

static SIMDINLINE Double SIMDCALL castsi_pd(Integer const& a) // return *(Double*)(&a)
{
    return Double{
        SIMD256T::castsi_pd(a.v8[0]),
        SIMD256T::castsi_pd(a.v8[1]),
    };
}

static SIMDINLINE Double SIMDCALL castps_pd(Float const& a) // return *(Double*)(&a)
{
    return Double{
        SIMD256T::castps_pd(a.v8[0]),
        SIMD256T::castps_pd(a.v8[1]),
    };
}

static SIMDINLINE Float SIMDCALL castsi_ps(Integer const& a) // return *(Float*)(&a)
{
    return Float{
        SIMD256T::castsi_ps(a.v8[0]),
        SIMD256T::castsi_ps(a.v8[1]),
    };
}

static SIMDINLINE Float SIMDCALL
                        cvtepi32_ps(Integer const& a) // return (float)a    (int32 --> float)
{
    return Float{
        SIMD256T::cvtepi32_ps(a.v8[0]),
        SIMD256T::cvtepi32_ps(a.v8[1]),
    };
}

static SIMDINLINE Integer SIMDCALL
                          cvtepu8_epi16(SIMD256Impl::Integer const& a) // return (int16)a    (uint8 --> int16)
{
    return Integer{
        SIMD256T::cvtepu8_epi16(a.v4[0]),
        SIMD256T::cvtepu8_epi16(a.v4[1]),
    };
}

static SIMDINLINE Integer SIMDCALL
                          cvtepu8_epi32(SIMD256Impl::Integer const& a) // return (int32)a    (uint8 --> int32)
{
    return Integer{
        SIMD256T::cvtepu8_epi32(a.v4[0]),
        SIMD256T::cvtepu8_epi32(SIMD128T::template srli_si<8>(a.v4[0])),
    };
}

static SIMDINLINE Integer SIMDCALL
                          cvtepu16_epi32(SIMD256Impl::Integer const& a) // return (int32)a    (uint16 --> int32)
{
    return Integer{
        SIMD256T::cvtepu16_epi32(a.v4[0]),
        SIMD256T::cvtepu16_epi32(a.v4[1]),
    };
}

static SIMDINLINE Integer SIMDCALL
                          cvtepu16_epi64(SIMD256Impl::Integer const& a) // return (int64)a    (uint16 --> int64)
{
    return Integer{
        SIMD256T::cvtepu16_epi64(a.v4[0]),
        SIMD256T::cvtepu16_epi64(SIMD128T::template srli_si<8>(a.v4[0])),
    };
}

static SIMDINLINE Integer SIMDCALL
                          cvtepu32_epi64(SIMD256Impl::Integer const& a) // return (int64)a    (uint32 --> int64)
{
    return Integer{
        SIMD256T::cvtepu32_epi64(a.v4[0]),
        SIMD256T::cvtepu32_epi64(a.v4[1]),
    };
}

static SIMDINLINE Integer SIMDCALL
                          cvtps_epi32(Float const& a) // return (int32)a    (float --> int32)
{
    return Integer{
        SIMD256T::cvtps_epi32(a.v8[0]),
        SIMD256T::cvtps_epi32(a.v8[1]),
    };
}

static SIMDINLINE Integer SIMDCALL
                          cvttps_epi32(Float const& a) // return (int32)a    (rnd_to_zero(float) --> int32)
{
    return Integer{
        SIMD256T::cvtps_epi32(a.v8[0]),
        SIMD256T::cvtps_epi32(a.v8[1]),
    };
}

//-----------------------------------------------------------------------
// Comparison operations
//-----------------------------------------------------------------------
template <CompareType CmpTypeT>
static SIMDINLINE Float SIMDCALL cmp_ps(Float const& a, Float const& b) // return a (CmpTypeT) b
{
    return Float{
        SIMD256T::template cmp_ps<CmpTypeT>(a.v8[0], b.v8[0]),
        SIMD256T::template cmp_ps<CmpTypeT>(a.v8[1], b.v8[1]),
    };
}
static SIMDINLINE Float SIMDCALL cmplt_ps(Float const& a, Float const& b)
{
    return cmp_ps<CompareType::LT_OQ>(a, b);
}
static SIMDINLINE Float SIMDCALL cmpgt_ps(Float const& a, Float const& b)
{
    return cmp_ps<CompareType::GT_OQ>(a, b);
}
static SIMDINLINE Float SIMDCALL cmpneq_ps(Float const& a, Float const& b)
{
    return cmp_ps<CompareType::NEQ_OQ>(a, b);
}
static SIMDINLINE Float SIMDCALL cmpeq_ps(Float const& a, Float const& b)
{
    return cmp_ps<CompareType::EQ_OQ>(a, b);
}
static SIMDINLINE Float SIMDCALL cmpge_ps(Float const& a, Float const& b)
{
    return cmp_ps<CompareType::GE_OQ>(a, b);
}
static SIMDINLINE Float SIMDCALL cmple_ps(Float const& a, Float const& b)
{
    return cmp_ps<CompareType::LE_OQ>(a, b);
}

template <CompareType CmpTypeT>
static SIMDINLINE Mask SIMDCALL cmp_ps_mask(Float const& a, Float const& b)
{
    return static_cast<Mask>(movemask_ps(cmp_ps<CmpTypeT>(a, b)));
}

SIMD_IWRAPPER_2(cmpeq_epi8);  // return a == b (int8)
SIMD_IWRAPPER_2(cmpeq_epi16); // return a == b (int16)
SIMD_IWRAPPER_2(cmpeq_epi32); // return a == b (int32)
SIMD_IWRAPPER_2(cmpeq_epi64); // return a == b (int64)
SIMD_IWRAPPER_2(cmpgt_epi8);  // return a > b (int8)
SIMD_IWRAPPER_2(cmpgt_epi16); // return a > b (int16)
SIMD_IWRAPPER_2(cmpgt_epi32); // return a > b (int32)
SIMD_IWRAPPER_2(cmpgt_epi64); // return a > b (int64)
SIMD_IWRAPPER_2(cmplt_epi32); // return a < b (int32)

static SIMDINLINE bool SIMDCALL
                       testz_ps(Float const& a, Float const& b) // return all_lanes_zero(a & b) ? 1 : 0 (float)
{
    return 0 != (SIMD256T::testz_ps(a.v8[0], b.v8[0]) & SIMD256T::testz_ps(a.v8[1], b.v8[1]));
}

static SIMDINLINE bool SIMDCALL
                       testz_si(Integer const& a, Integer const& b) // return all_lanes_zero(a & b) ? 1 : 0 (int)
{
    return 0 != (SIMD256T::testz_si(a.v8[0], b.v8[0]) & SIMD256T::testz_si(a.v8[1], b.v8[1]));
}

//-----------------------------------------------------------------------
// Blend / shuffle / permute operations
//-----------------------------------------------------------------------
SIMD_WRAPPER_2I(blend_ps);     // return ImmT ? b : a  (float)
SIMD_IWRAPPER_2I(blend_epi32); // return ImmT ? b : a  (int32)
SIMD_WRAPPER_3(blendv_ps);     // return mask ? b : a  (float)
static SIMDINLINE Integer SIMDCALL blendv_epi32(Integer const& a,
                                                Integer const& b,
                                                Float const&   mask) // return mask ? b : a (int)
{
    return Integer{
        SIMD256T::blendv_epi32(a.v8[0], b.v8[0], mask.v8[0]),
        SIMD256T::blendv_epi32(a.v8[1], b.v8[1], mask.v8[1]),
    };
}

static SIMDINLINE Integer SIMDCALL blendv_epi32(Integer const& a,
                                                Integer const& b,
                                                Integer const& mask) // return mask ? b : a (int)
{
    return Integer{
        SIMD256T::blendv_epi32(a.v8[0], b.v8[0], mask.v8[0]),
        SIMD256T::blendv_epi32(a.v8[1], b.v8[1], mask.v8[1]),
    };
}

static SIMDINLINE Float SIMDCALL
                        broadcast_ss(float const* p) // return *p (all elements in vector get same value)
{
    float f = *p;
    return Float{
        SIMD256T::set1_ps(f),
        SIMD256T::set1_ps(f),
    };
}

template <int imm>
static SIMDINLINE SIMD256Impl::Float SIMDCALL extract_ps(Float const& a)
{
    SWR_ASSERT(imm == 0 || imm == 1, "Invalid control code: %d", imm);
    return a.v8[imm];
}

template <int imm>
static SIMDINLINE SIMD256Impl::Double SIMDCALL extract_pd(Double const& a)
{
    SWR_ASSERT(imm == 0 || imm == 1, "Invalid control code: %d", imm);
    return a.v8[imm];
}

template <int imm>
static SIMDINLINE SIMD256Impl::Integer SIMDCALL extract_si(Integer const& a)
{
    SWR_ASSERT(imm == 0 || imm == 1, "Invalid control code: %d", imm);
    return a.v8[imm];
}

template <int imm>
static SIMDINLINE Float SIMDCALL insert_ps(Float const& a, SIMD256Impl::Float const& b)
{
    SWR_ASSERT(imm == 0 || imm == 1, "Invalid control code: %d", imm);
    Float r   = a;
    r.v8[imm] = b;
    return r;
}

template <int imm>
static SIMDINLINE Double SIMDCALL insert_pd(Double const& a, SIMD256Impl::Double const& b)
{
    SWR_ASSERT(imm == 0 || imm == 1, "Invalid control code: %d", imm);
    Double r  = a;
    r.v8[imm] = b;
    return r;
}

template <int imm>
static SIMDINLINE Integer SIMDCALL insert_si(Integer const& a, SIMD256Impl::Integer const& b)
{
    SWR_ASSERT(imm == 0 || imm == 1, "Invalid control code: %d", imm);
    Integer r = a;
    r.v8[imm] = b;
    return r;
}

SIMD_IWRAPPER_2(packs_epi16);  // See documentation for _mm256_packs_epi16 and _mm512_packs_epi16
SIMD_IWRAPPER_2(packs_epi32);  // See documentation for _mm256_packs_epi32 and _mm512_packs_epi32
SIMD_IWRAPPER_2(packus_epi16); // See documentation for _mm256_packus_epi16 and _mm512_packus_epi16
SIMD_IWRAPPER_2(packus_epi32); // See documentation for _mm256_packus_epi32 and _mm512_packus_epi32

template <int ImmT>
static SIMDINLINE Float SIMDCALL permute_ps(Float const& a)
{
    return Float{
        SIMD256T::template permute_ps<ImmT>(a.v8[0]),
        SIMD256T::template permute_ps<ImmT>(a.v8[1]),
    };
}

static SIMDINLINE Integer SIMDCALL permute_epi32(
    Integer const& a, Integer const& swiz) // return a[swiz[i]] for each 32-bit lane i (int32)
{
    return castps_si(permute_ps(castsi_ps(a), swiz));
}

static SIMDINLINE Float SIMDCALL
                        permute_ps(Float const& a, Integer const& swiz) // return a[swiz[i]] for each 32-bit lane i (float)
{
    const auto mask = SIMD256T::set1_epi32(7);

    auto lolo = SIMD256T::permute_ps(a.v8[0], SIMD256T::and_si(swiz.v8[0], mask));
    auto lohi = SIMD256T::permute_ps(a.v8[1], SIMD256T::and_si(swiz.v8[0], mask));

    auto hilo = SIMD256T::permute_ps(a.v8[0], SIMD256T::and_si(swiz.v8[1], mask));
    auto hihi = SIMD256T::permute_ps(a.v8[1], SIMD256T::and_si(swiz.v8[1], mask));

    return Float{
        SIMD256T::blendv_ps(
            lolo, lohi, SIMD256T::castsi_ps(SIMD256T::cmpgt_epi32(swiz.v8[0], mask))),
        SIMD256T::blendv_ps(
            hilo, hihi, SIMD256T::castsi_ps(SIMD256T::cmpgt_epi32(swiz.v8[1], mask))),
    };
}

// All of the 512-bit permute2f128_XX intrinsics do the following:
//
//      SELECT4(src, control) {
//          CASE(control[1:0])
//              0 : tmp[127:0] : = src[127:0]
//              1 : tmp[127:0] : = src[255:128]
//              2 : tmp[127:0] : = src[383:256]
//              3 : tmp[127:0] : = src[511:384]
//              ESAC
//              RETURN tmp[127:0]
//      }
//
//      dst[127:0]   : = SELECT4(a[511:0], imm8[1:0])
//      dst[255:128] : = SELECT4(a[511:0], imm8[3:2])
//      dst[383:256] : = SELECT4(b[511:0], imm8[5:4])
//      dst[511:384] : = SELECT4(b[511:0], imm8[7:6])
//      dst[MAX:512] : = 0
//
// Since the 256-bit AVX instructions use a 4-bit control field (instead
// of 2-bit for AVX512), we need to expand the control bits sent to the
// AVX instructions for emulation.
//
template <int shuf>
static SIMDINLINE Float SIMDCALL permute2f128_ps(Float const& a, Float const& b)
{
    return Float{
        SIMD256T::template permute2f128_ps<((shuf & 0x03) << 0) | ((shuf & 0x0C) << 2)>(a.v8[0],
                                                                                        a.v8[1]),
        SIMD256T::template permute2f128_ps<((shuf & 0x30) >> 4) | ((shuf & 0xC0) >> 2)>(b.v8[0],
                                                                                        b.v8[1]),
    };
}

template <int shuf>
static SIMDINLINE Double SIMDCALL permute2f128_pd(Double const& a, Double const& b)
{
    return Double{
        SIMD256T::template permute2f128_pd<((shuf & 0x03) << 0) | ((shuf & 0x0C) << 2)>(a.v8[0],
                                                                                        a.v8[1]),
        SIMD256T::template permute2f128_pd<((shuf & 0x30) >> 4) | ((shuf & 0xC0) >> 2)>(b.v8[0],
                                                                                        b.v8[1]),
    };
}

template <int shuf>
static SIMDINLINE Integer SIMDCALL permute2f128_si(Integer const& a, Integer const& b)
{
    return Integer{
        SIMD256T::template permute2f128_si<((shuf & 0x03) << 0) | ((shuf & 0x0C) << 2)>(a.v8[0],
                                                                                        a.v8[1]),
        SIMD256T::template permute2f128_si<((shuf & 0x30) >> 4) | ((shuf & 0xC0) >> 2)>(b.v8[0],
                                                                                        b.v8[1]),
    };
}

SIMD_IWRAPPER_2I_1(shuffle_epi32);
SIMD_IWRAPPER_2I_2(shuffle_epi64);
SIMD_IWRAPPER_2(shuffle_epi8);
SIMD_WRAPPER_2I_1(shuffle_pd);
SIMD_WRAPPER_2I_1(shuffle_ps);
SIMD_IWRAPPER_2(unpackhi_epi16);
SIMD_IWRAPPER_2(unpackhi_epi32);
SIMD_IWRAPPER_2(unpackhi_epi64);
SIMD_IWRAPPER_2(unpackhi_epi8);
SIMD_WRAPPER_2(unpackhi_pd);
SIMD_WRAPPER_2(unpackhi_ps);
SIMD_IWRAPPER_2(unpacklo_epi16);
SIMD_IWRAPPER_2(unpacklo_epi32);
SIMD_IWRAPPER_2(unpacklo_epi64);
SIMD_IWRAPPER_2(unpacklo_epi8);
SIMD_WRAPPER_2(unpacklo_pd);
SIMD_WRAPPER_2(unpacklo_ps);

//-----------------------------------------------------------------------
// Load / store operations
//-----------------------------------------------------------------------
template <ScaleFactor ScaleT = ScaleFactor::SF_1>
static SIMDINLINE Float SIMDCALL
                        i32gather_ps(float const* p, Integer const& idx) // return *(float*)(((int8*)p) + (idx * ScaleT))
{
    return Float{
        SIMD256T::template i32gather_ps<ScaleT>(p, idx.v8[0]),
        SIMD256T::template i32gather_ps<ScaleT>(p, idx.v8[1]),
    };
}

template <ScaleFactor ScaleT = ScaleFactor::SF_1>
static SIMDINLINE Float SIMDCALL
                        sw_i32gather_ps(float const* p, Integer const& idx) // return *(float*)(((int8*)p) + (idx * ScaleT))
{
    return Float{
        SIMD256T::template sw_i32gather_ps<ScaleT>(p, idx.v8[0]),
        SIMD256T::template sw_i32gather_ps<ScaleT>(p, idx.v8[1]),
    };
}

static SIMDINLINE Float SIMDCALL
                        load1_ps(float const* p) // return *p    (broadcast 1 value to all elements)
{
    return broadcast_ss(p);
}

static SIMDINLINE Float SIMDCALL
                        load_ps(float const* p) // return *p    (loads SIMD width elements from memory)
{
    return Float{SIMD256T::load_ps(p), SIMD256T::load_ps(p + TARGET_SIMD_WIDTH)};
}

static SIMDINLINE Integer SIMDCALL load_si(Integer const* p) // return *p
{
    return Integer{
        SIMD256T::load_si(&p->v8[0]),
        SIMD256T::load_si(&p->v8[1]),
    };
}

static SIMDINLINE Float SIMDCALL
                        loadu_ps(float const* p) // return *p    (same as load_ps but allows for unaligned mem)
{
    return Float{SIMD256T::loadu_ps(p), SIMD256T::loadu_ps(p + TARGET_SIMD_WIDTH)};
}

static SIMDINLINE Integer SIMDCALL
                          loadu_si(Integer const* p) // return *p    (same as load_si but allows for unaligned mem)
{
    return Integer{
        SIMD256T::loadu_si(&p->v8[0]),
        SIMD256T::loadu_si(&p->v8[1]),
    };
}

// for each element: (mask & (1 << 31)) ? (i32gather_ps<ScaleT>(p, idx), mask = 0) : old
template <ScaleFactor ScaleT = ScaleFactor::SF_1>
static SIMDINLINE Float SIMDCALL
                        mask_i32gather_ps(Float const& old, float const* p, Integer const& idx, Float const& mask)
{
    return Float{
        SIMD256T::template mask_i32gather_ps<ScaleT>(old.v8[0], p, idx.v8[0], mask.v8[0]),
        SIMD256T::template mask_i32gather_ps<ScaleT>(old.v8[1], p, idx.v8[1], mask.v8[1]),
    };
}

template <ScaleFactor ScaleT = ScaleFactor::SF_1>
static SIMDINLINE Float SIMDCALL
                        sw_mask_i32gather_ps(Float const& old, float const* p, Integer const& idx, Float const& mask)
{
    return Float{
        SIMD256T::template sw_mask_i32gather_ps<ScaleT>(old.v8[0], p, idx.v8[0], mask.v8[0]),
        SIMD256T::template sw_mask_i32gather_ps<ScaleT>(old.v8[1], p, idx.v8[1], mask.v8[1]),
    };
}

static SIMDINLINE void SIMDCALL maskstore_ps(float* p, Integer const& mask, Float const& src)
{
    SIMD256T::maskstore_ps(p, mask.v8[0], src.v8[0]);
    SIMD256T::maskstore_ps(p + TARGET_SIMD_WIDTH, mask.v8[1], src.v8[1]);
}

static SIMDINLINE uint64_t SIMDCALL movemask_epi8(Integer const& a)
{
    uint64_t mask = static_cast<uint64_t>(SIMD256T::movemask_epi8(a.v8[0]));
    mask |= static_cast<uint64_t>(SIMD256T::movemask_epi8(a.v8[1])) << (TARGET_SIMD_WIDTH * 4);

    return mask;
}

static SIMDINLINE uint32_t SIMDCALL movemask_pd(Double const& a)
{
    uint32_t mask = static_cast<uint32_t>(SIMD256T::movemask_pd(a.v8[0]));
    mask |= static_cast<uint32_t>(SIMD256T::movemask_pd(a.v8[1])) << (TARGET_SIMD_WIDTH / 2);

    return mask;
}
static SIMDINLINE uint32_t SIMDCALL movemask_ps(Float const& a)
{
    uint32_t mask = static_cast<uint32_t>(SIMD256T::movemask_ps(a.v8[0]));
    mask |= static_cast<uint32_t>(SIMD256T::movemask_ps(a.v8[1])) << TARGET_SIMD_WIDTH;

    return mask;
}

static SIMDINLINE Integer SIMDCALL set1_epi32(int i) // return i (all elements are same value)
{
    return Integer{SIMD256T::set1_epi32(i), SIMD256T::set1_epi32(i)};
}

static SIMDINLINE Integer SIMDCALL set1_epi8(char i) // return i (all elements are same value)
{
    return Integer{SIMD256T::set1_epi8(i), SIMD256T::set1_epi8(i)};
}

static SIMDINLINE Float SIMDCALL set1_ps(float f) // return f (all elements are same value)
{
    return Float{SIMD256T::set1_ps(f), SIMD256T::set1_ps(f)};
}

static SIMDINLINE Float SIMDCALL setzero_ps() // return 0 (float)
{
    return Float{SIMD256T::setzero_ps(), SIMD256T::setzero_ps()};
}

static SIMDINLINE Integer SIMDCALL setzero_si() // return 0 (integer)
{
    return Integer{SIMD256T::setzero_si(), SIMD256T::setzero_si()};
}

static SIMDINLINE void SIMDCALL
                       store_ps(float* p, Float const& a) // *p = a   (stores all elements contiguously in memory)
{
    SIMD256T::store_ps(p, a.v8[0]);
    SIMD256T::store_ps(p + TARGET_SIMD_WIDTH, a.v8[1]);
}

static SIMDINLINE void SIMDCALL store_si(Integer* p, Integer const& a) // *p = a
{
    SIMD256T::store_si(&p->v8[0], a.v8[0]);
    SIMD256T::store_si(&p->v8[1], a.v8[1]);
}

static SIMDINLINE void SIMDCALL
                       stream_ps(float* p, Float const& a) // *p = a   (same as store_ps, but doesn't keep memory in cache)
{
    SIMD256T::stream_ps(p, a.v8[0]);
    SIMD256T::stream_ps(p + TARGET_SIMD_WIDTH, a.v8[1]);
}

static SIMDINLINE Integer SIMDCALL set_epi32(int i15,
                                             int i14,
                                             int i13,
                                             int i12,
                                             int i11,
                                             int i10,
                                             int i9,
                                             int i8,
                                             int i7,
                                             int i6,
                                             int i5,
                                             int i4,
                                             int i3,
                                             int i2,
                                             int i1,
                                             int i0)
{
    return Integer{SIMD256T::set_epi32(i7, i6, i5, i4, i3, i2, i1, i0),
                   SIMD256T::set_epi32(i15, i14, i13, i12, i11, i10, i9, i8)};
}

static SIMDINLINE Integer SIMDCALL
                          set_epi32(int i7, int i6, int i5, int i4, int i3, int i2, int i1, int i0)
{
    return set_epi32(0, 0, 0, 0, 0, 0, 0, 0, i7, i6, i5, i4, i3, i2, i1, i0);
}

static SIMDINLINE Float SIMDCALL set_ps(float i15,
                                        float i14,
                                        float i13,
                                        float i12,
                                        float i11,
                                        float i10,
                                        float i9,
                                        float i8,
                                        float i7,
                                        float i6,
                                        float i5,
                                        float i4,
                                        float i3,
                                        float i2,
                                        float i1,
                                        float i0)
{
    return Float{SIMD256T::set_ps(i7, i6, i5, i4, i3, i2, i1, i0),
                 SIMD256T::set_ps(i15, i14, i13, i12, i11, i10, i9, i8)};
}

static SIMDINLINE Float SIMDCALL
                        set_ps(float i7, float i6, float i5, float i4, float i3, float i2, float i1, float i0)
{
    return set_ps(0, 0, 0, 0, 0, 0, 0, 0, i7, i6, i5, i4, i3, i2, i1, i0);
}

static SIMDINLINE Float SIMDCALL vmask_ps(int32_t mask)
{
    return Float{SIMD256T::vmask_ps(mask), SIMD256T::vmask_ps(mask >> TARGET_SIMD_WIDTH)};
}

#undef SIMD_WRAPPER_1
#undef SIMD_WRAPPER_2
#undef SIMD_WRAPPER_2I
#undef SIMD_WRAPPER_2I_1
#undef SIMD_WRAPPER_3
#undef SIMD_IWRAPPER_1
#undef SIMD_IWRAPPER_2
#undef SIMD_IWRAPPER_2I
#undef SIMD_IWRAPPER_2I_1
#undef SIMD_IWRAPPER_3
