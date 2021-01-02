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
// SIMD128 AVX (1) implementation
//============================================================================

#define SIMD_WRAPPER_1(op) \
    static SIMDINLINE Float SIMDCALL op(Float a) { return _mm_##op(a); }

#define SIMD_WRAPPER_2(op) \
    static SIMDINLINE Float SIMDCALL op(Float a, Float b) { return _mm_##op(a, b); }

#define SIMD_DWRAPPER_2(op) \
    static SIMDINLINE Double SIMDCALL op(Double a, Double b) { return _mm_##op(a, b); }

#define SIMD_WRAPPER_2I(op)                               \
    template <int ImmT>                                   \
    static SIMDINLINE Float SIMDCALL op(Float a, Float b) \
    {                                                     \
        return _mm_##op(a, b, ImmT);                      \
    }

#define SIMD_DWRAPPER_2I(op)                                 \
    template <int ImmT>                                      \
    static SIMDINLINE Double SIMDCALL op(Double a, Double b) \
    {                                                        \
        return _mm_##op(a, b, ImmT);                         \
    }

#define SIMD_WRAPPER_3(op) \
    static SIMDINLINE Float SIMDCALL op(Float a, Float b, Float c) { return _mm_##op(a, b, c); }

#define SIMD_IWRAPPER_1(op) \
    static SIMDINLINE Integer SIMDCALL op(Integer a) { return _mm_##op(a); }

#define SIMD_IWRAPPER_1I_(op, intrin)                \
    template <int ImmT>                              \
    static SIMDINLINE Integer SIMDCALL op(Integer a) \
    {                                                \
        return intrin(a, ImmT);                      \
    }
#define SIMD_IWRAPPER_1I(op) SIMD_IWRAPPER_1I_(op, _mm_##op)

#define SIMD_IWRAPPER_2_(op, intrin) \
    static SIMDINLINE Integer SIMDCALL op(Integer a, Integer b) { return intrin(a, b); }

#define SIMD_IWRAPPER_2(op) \
    static SIMDINLINE Integer SIMDCALL op(Integer a, Integer b) { return _mm_##op(a, b); }

#define SIMD_IFWRAPPER_2(op, intrin)                            \
    static SIMDINLINE Integer SIMDCALL op(Integer a, Integer b) \
    {                                                           \
        return castps_si(intrin(castsi_ps(a), castsi_ps(b)));   \
    }

#define SIMD_IWRAPPER_2I(op)                                    \
    template <int ImmT>                                         \
    static SIMDINLINE Integer SIMDCALL op(Integer a, Integer b) \
    {                                                           \
        return _mm_##op(a, b, ImmT);                            \
    }

//-----------------------------------------------------------------------
// Single precision floating point arithmetic operations
//-----------------------------------------------------------------------
SIMD_WRAPPER_2(add_ps);   // return a + b
SIMD_WRAPPER_2(div_ps);   // return a / b
SIMD_WRAPPER_2(max_ps);   // return (a > b) ? a : b
SIMD_WRAPPER_2(min_ps);   // return (a < b) ? a : b
SIMD_WRAPPER_2(mul_ps);   // return a * b
SIMD_WRAPPER_1(rcp_ps);   // return 1.0f / a
SIMD_WRAPPER_1(rsqrt_ps); // return 1.0f / sqrt(a)
SIMD_WRAPPER_2(sub_ps);   // return a - b

static SIMDINLINE Float SIMDCALL fmadd_ps(Float a, Float b, Float c) // return (a * b) + c
{
    return add_ps(mul_ps(a, b), c);
}
static SIMDINLINE Float SIMDCALL fmsub_ps(Float a, Float b, Float c) // return (a * b) - c
{
    return sub_ps(mul_ps(a, b), c);
}

template <RoundMode RMT>
static SIMDINLINE Float SIMDCALL round_ps(Float a)
{
    return _mm_round_ps(a, static_cast<int>(RMT));
}

static SIMDINLINE Float SIMDCALL ceil_ps(Float a)
{
    return round_ps<RoundMode::CEIL_NOEXC>(a);
}
static SIMDINLINE Float SIMDCALL floor_ps(Float a)
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
SIMD_WRAPPER_2(and_ps);                        // return a & b       (float treated as int)
SIMD_IWRAPPER_2_(and_si, _mm_and_si128);       // return a & b       (int)
SIMD_WRAPPER_2(andnot_ps);                     // return (~a) & b    (float treated as int)
SIMD_IWRAPPER_2_(andnot_si, _mm_andnot_si128); // return (~a) & b    (int)
SIMD_WRAPPER_2(or_ps);                         // return a | b       (float treated as int)
SIMD_IWRAPPER_2_(or_si, _mm_or_si128);         // return a | b       (int)
SIMD_WRAPPER_2(xor_ps);                        // return a ^ b       (float treated as int)
SIMD_IWRAPPER_2_(xor_si, _mm_xor_si128);       // return a ^ b       (int)

//-----------------------------------------------------------------------
// Shift operations
//-----------------------------------------------------------------------
SIMD_IWRAPPER_1I(slli_epi32); // return a << ImmT
SIMD_IWRAPPER_1I(slli_epi64); // return a << ImmT

static SIMDINLINE Integer SIMDCALL sllv_epi32(Integer vA, Integer vB) // return a << b      (uint32)
{
    int32_t a, count;
    a     = _mm_extract_epi32(vA, 0);
    count = _mm_extract_epi32(vB, 0);
    a <<= count;
    vA = _mm_insert_epi32(vA, a, 0);

    a     = _mm_extract_epi32(vA, 1);
    count = _mm_extract_epi32(vB, 1);
    a <<= count;
    vA = _mm_insert_epi32(vA, a, 1);

    a     = _mm_extract_epi32(vA, 2);
    count = _mm_extract_epi32(vB, 2);
    a <<= count;
    vA = _mm_insert_epi32(vA, a, 2);

    a     = _mm_extract_epi32(vA, 3);
    count = _mm_extract_epi32(vB, 3);
    a <<= count;
    vA = _mm_insert_epi32(vA, a, 3);

    return vA;
}

SIMD_IWRAPPER_1I(srai_epi32);               // return a >> ImmT   (int32)
SIMD_IWRAPPER_1I(srli_epi32);               // return a >> ImmT   (uint32)
SIMD_IWRAPPER_1I_(srli_si, _mm_srli_si128); // return a >> (ImmT*8) (uint)

static SIMDINLINE Integer SIMDCALL srl_epi64(Integer a, Integer n)
{
    return _mm_srl_epi64(a, n);
}

template <int ImmT> // same as srli_si, but with Float cast to int
static SIMDINLINE Float SIMDCALL srlisi_ps(Float a)
{
    return castsi_ps(srli_si<ImmT>(castps_si(a)));
}

static SIMDINLINE Integer SIMDCALL srlv_epi32(Integer vA, Integer vB) // return a >> b      (uint32)
{
    int32_t a, count;
    a     = _mm_extract_epi32(vA, 0);
    count = _mm_extract_epi32(vB, 0);
    a >>= count;
    vA = _mm_insert_epi32(vA, a, 0);

    a     = _mm_extract_epi32(vA, 1);
    count = _mm_extract_epi32(vB, 1);
    a >>= count;
    vA = _mm_insert_epi32(vA, a, 1);

    a     = _mm_extract_epi32(vA, 2);
    count = _mm_extract_epi32(vB, 2);
    a >>= count;
    vA = _mm_insert_epi32(vA, a, 2);

    a     = _mm_extract_epi32(vA, 3);
    count = _mm_extract_epi32(vB, 3);
    a >>= count;
    vA = _mm_insert_epi32(vA, a, 3);

    return vA;
}

//-----------------------------------------------------------------------
// Conversion operations
//-----------------------------------------------------------------------
static SIMDINLINE Float SIMDCALL castpd_ps(Double a) // return *(Float*)(&a)
{
    return _mm_castpd_ps(a);
}

static SIMDINLINE Integer SIMDCALL castps_si(Float a) // return *(Integer*)(&a)
{
    return _mm_castps_si128(a);
}

static SIMDINLINE Double SIMDCALL castsi_pd(Integer a) // return *(Double*)(&a)
{
    return _mm_castsi128_pd(a);
}

static SIMDINLINE Double SIMDCALL castps_pd(Float a) // return *(Double*)(&a)
{
    return _mm_castps_pd(a);
}

static SIMDINLINE Float SIMDCALL castsi_ps(Integer a) // return *(Float*)(&a)
{
    return _mm_castsi128_ps(a);
}

static SIMDINLINE Float SIMDCALL cvtepi32_ps(Integer a) // return (float)a    (int32 --> float)
{
    return _mm_cvtepi32_ps(a);
}

static SIMDINLINE int32_t SIMDCALL cvtsi128_si32(Integer a) // return a.v[0]
{
    return _mm_cvtsi128_si32(a);
}

static SIMDINLINE Integer SIMDCALL cvtsi32_si128(int32_t n) // return a[0] = n, a[1]...a[3] = 0
{
    return _mm_cvtsi32_si128(n);
}

SIMD_IWRAPPER_1(cvtepu8_epi16);  // return (int16)a    (uint8 --> int16)
SIMD_IWRAPPER_1(cvtepu8_epi32);  // return (int32)a    (uint8 --> int32)
SIMD_IWRAPPER_1(cvtepu16_epi32); // return (int32)a    (uint16 --> int32)
SIMD_IWRAPPER_1(cvtepu16_epi64); // return (int64)a    (uint16 --> int64)
SIMD_IWRAPPER_1(cvtepu32_epi64); // return (int64)a    (uint32 --> int64)

static SIMDINLINE Integer SIMDCALL cvtps_epi32(Float a) // return (int32)a    (float --> int32)
{
    return _mm_cvtps_epi32(a);
}

static SIMDINLINE Integer SIMDCALL
                          cvttps_epi32(Float a) // return (int32)a    (rnd_to_zero(float) --> int32)
{
    return _mm_cvttps_epi32(a);
}

//-----------------------------------------------------------------------
// Comparison operations
//-----------------------------------------------------------------------
template <CompareType CmpTypeT>
static SIMDINLINE Float SIMDCALL cmp_ps(Float a, Float b) // return a (CmpTypeT) b
{
    return _mm_cmp_ps(a, b, static_cast<const int>(CmpTypeT));
}
static SIMDINLINE Float SIMDCALL cmplt_ps(Float a, Float b)
{
    return cmp_ps<CompareType::LT_OQ>(a, b);
}
static SIMDINLINE Float SIMDCALL cmpgt_ps(Float a, Float b)
{
    return cmp_ps<CompareType::GT_OQ>(a, b);
}
static SIMDINLINE Float SIMDCALL cmpneq_ps(Float a, Float b)
{
    return cmp_ps<CompareType::NEQ_OQ>(a, b);
}
static SIMDINLINE Float SIMDCALL cmpeq_ps(Float a, Float b)
{
    return cmp_ps<CompareType::EQ_OQ>(a, b);
}
static SIMDINLINE Float SIMDCALL cmpge_ps(Float a, Float b)
{
    return cmp_ps<CompareType::GE_OQ>(a, b);
}
static SIMDINLINE Float SIMDCALL cmple_ps(Float a, Float b)
{
    return cmp_ps<CompareType::LE_OQ>(a, b);
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

static SIMDINLINE bool SIMDCALL testz_ps(Float a,
                                         Float b) // return all_lanes_zero(a & b) ? 1 : 0 (float)
{
    return 0 != _mm_testz_ps(a, b);
}

static SIMDINLINE bool SIMDCALL testz_si(Integer a,
                                         Integer b) // return all_lanes_zero(a & b) ? 1 : 0 (int)
{
    return 0 != _mm_testz_si128(a, b);
}

//-----------------------------------------------------------------------
// Blend / shuffle / permute operations
//-----------------------------------------------------------------------
SIMD_WRAPPER_2I(blend_ps); // return ImmT ? b : a  (float)
SIMD_WRAPPER_3(blendv_ps); // return mask ? b : a  (float)

static SIMDINLINE Integer SIMDCALL blendv_epi32(Integer a,
                                                Integer b,
                                                Float   mask) // return mask ? b : a (int)
{
    return castps_si(blendv_ps(castsi_ps(a), castsi_ps(b), mask));
}

static SIMDINLINE Integer SIMDCALL blendv_epi32(Integer a,
                                                Integer b,
                                                Integer mask) // return mask ? b : a (int)
{
    return castps_si(blendv_ps(castsi_ps(a), castsi_ps(b), castsi_ps(mask)));
}

static SIMDINLINE Float SIMDCALL
                        broadcast_ss(float const* p) // return *p (all elements in vector get same value)
{
    return _mm_broadcast_ss(p);
}

SIMD_IWRAPPER_2(packs_epi16);  // See documentation for _mm_packs_epi16 and _mm512_packs_epi16
SIMD_IWRAPPER_2(packs_epi32);  // See documentation for _mm_packs_epi32 and _mm512_packs_epi32
SIMD_IWRAPPER_2(packus_epi16); // See documentation for _mm_packus_epi16 and _mm512_packus_epi16
SIMD_IWRAPPER_2(packus_epi32); // See documentation for _mm_packus_epi32 and _mm512_packus_epi32

static SIMDINLINE Integer SIMDCALL
                          permute_epi32(Integer a, Integer swiz) // return a[swiz[i]] for each 32-bit lane i (float)
{
    return castps_si(_mm_permutevar_ps(castsi_ps(a), swiz));
}

static SIMDINLINE Float SIMDCALL
                        permute_ps(Float a, Integer swiz) // return a[swiz[i]] for each 32-bit lane i (float)
{
    return _mm_permutevar_ps(a, swiz);
}

SIMD_IWRAPPER_1I(shuffle_epi32);

template <int ImmT>
static SIMDINLINE Integer SIMDCALL shuffle_epi64(Integer a, Integer b) = delete;

SIMD_IWRAPPER_2(shuffle_epi8);
SIMD_DWRAPPER_2I(shuffle_pd);
SIMD_WRAPPER_2I(shuffle_ps);
SIMD_IWRAPPER_2(unpackhi_epi16);

// SIMD_IFWRAPPER_2(unpackhi_epi32, _mm_unpackhi_ps);
static SIMDINLINE Integer SIMDCALL unpackhi_epi32(Integer a, Integer b)
{
    return castps_si(_mm_unpackhi_ps(castsi_ps(a), castsi_ps(b)));
}

SIMD_IWRAPPER_2(unpackhi_epi64);
SIMD_IWRAPPER_2(unpackhi_epi8);
SIMD_DWRAPPER_2(unpackhi_pd);
SIMD_WRAPPER_2(unpackhi_ps);
SIMD_IWRAPPER_2(unpacklo_epi16);
SIMD_IFWRAPPER_2(unpacklo_epi32, _mm_unpacklo_ps);
SIMD_IWRAPPER_2(unpacklo_epi64);
SIMD_IWRAPPER_2(unpacklo_epi8);
SIMD_DWRAPPER_2(unpacklo_pd);
SIMD_WRAPPER_2(unpacklo_ps);

//-----------------------------------------------------------------------
// Load / store operations
//-----------------------------------------------------------------------
template <ScaleFactor ScaleT = ScaleFactor::SF_1>
static SIMDINLINE Float SIMDCALL
                        i32gather_ps(float const* p, Integer idx) // return *(float*)(((int8*)p) + (idx * ScaleT))
{
    uint32_t* pOffsets = (uint32_t*)&idx;
    Float     vResult;
    float*    pResult = (float*)&vResult;
    for (uint32_t i = 0; i < SIMD_WIDTH; ++i)
    {
        uint32_t offset = pOffsets[i];
        offset          = offset * static_cast<uint32_t>(ScaleT);
        pResult[i]      = *(float const*)(((uint8_t const*)p + offset));
    }

    return vResult;
}

static SIMDINLINE Float SIMDCALL
                        load1_ps(float const* p) // return *p    (broadcast 1 value to all elements)
{
    return broadcast_ss(p);
}

static SIMDINLINE Float SIMDCALL
                        load_ps(float const* p) // return *p    (loads SIMD width elements from memory)
{
    return _mm_load_ps(p);
}

static SIMDINLINE Integer SIMDCALL load_si(Integer const* p) // return *p
{
    return _mm_load_si128(&p->v);
}

static SIMDINLINE Float SIMDCALL
                        loadu_ps(float const* p) // return *p    (same as load_ps but allows for unaligned mem)
{
    return _mm_loadu_ps(p);
}

static SIMDINLINE Integer SIMDCALL
                          loadu_si(Integer const* p) // return *p    (same as load_si but allows for unaligned mem)
{
    return _mm_lddqu_si128(&p->v);
}

// for each element: (mask & (1 << 31)) ? (i32gather_ps<ScaleT>(p, idx), mask = 0) : old
template <ScaleFactor ScaleT = ScaleFactor::SF_1>
static SIMDINLINE Float SIMDCALL
                        mask_i32gather_ps(Float old, float const* p, Integer idx, Float mask)
{
    uint32_t* pOffsets = (uint32_t*)&idx;
    Float     vResult  = old;
    float*    pResult  = (float*)&vResult;
    unsigned long index;
    uint32_t  umask = movemask_ps(mask);
    while (_BitScanForward(&index, umask))
    {
        umask &= ~(1 << index);
        uint32_t offset = pOffsets[index];
        offset          = offset * static_cast<uint32_t>(ScaleT);
        pResult[index]  = *(float const*)(((uint8_t const*)p + offset));
    }

    return vResult;
}

static SIMDINLINE void SIMDCALL maskstore_ps(float* p, Integer mask, Float src)
{
    _mm_maskstore_ps(p, mask, src);
}

static SIMDINLINE uint32_t SIMDCALL movemask_epi8(Integer a)
{
    return static_cast<uint32_t>(_mm_movemask_epi8(a));
}

static SIMDINLINE uint32_t SIMDCALL movemask_pd(Double a)
{
    return static_cast<uint32_t>(_mm_movemask_pd(a));
}
static SIMDINLINE uint32_t SIMDCALL movemask_ps(Float a)
{
    return static_cast<uint32_t>(_mm_movemask_ps(a));
}

static SIMDINLINE Integer SIMDCALL set1_epi32(int i) // return i (all elements are same value)
{
    return _mm_set1_epi32(i);
}

static SIMDINLINE Integer SIMDCALL set1_epi8(char i) // return i (all elements are same value)
{
    return _mm_set1_epi8(i);
}

static SIMDINLINE Float SIMDCALL set1_ps(float f) // return f (all elements are same value)
{
    return _mm_set1_ps(f);
}

static SIMDINLINE Float SIMDCALL setzero_ps() // return 0 (float)
{
    return _mm_setzero_ps();
}

static SIMDINLINE Integer SIMDCALL setzero_si() // return 0 (integer)
{
    return _mm_setzero_si128();
}

static SIMDINLINE void SIMDCALL
                       store_ps(float* p, Float a) // *p = a   (stores all elements contiguously in memory)
{
    _mm_store_ps(p, a);
}

static SIMDINLINE void SIMDCALL store_si(Integer* p, Integer a) // *p = a
{
    _mm_store_si128(&p->v, a);
}

static SIMDINLINE void SIMDCALL
                       storeu_si(Integer* p, Integer a) // *p = a    (same as store_si but allows for unaligned mem)
{
    _mm_storeu_si128(&p->v, a);
}

static SIMDINLINE void SIMDCALL
                       stream_ps(float* p, Float a) // *p = a   (same as store_ps, but doesn't keep memory in cache)
{
    _mm_stream_ps(p, a);
}

static SIMDINLINE Float SIMDCALL set_ps(float in3, float in2, float in1, float in0)
{
    return _mm_set_ps(in3, in2, in1, in0);
}

static SIMDINLINE Integer SIMDCALL set_epi32(int in3, int in2, int in1, int in0)
{
    return _mm_set_epi32(in3, in2, in1, in0);
}

template <int ImmT>
static SIMDINLINE float SIMDCALL extract_ps(Float a)
{
    int tmp = _mm_extract_ps(a, ImmT);
    return *reinterpret_cast<float*>(&tmp);
}

static SIMDINLINE Float SIMDCALL vmask_ps(int32_t mask)
{
    Integer       vec = set1_epi32(mask);
    const Integer bit = set_epi32(0x08, 0x04, 0x02, 0x01);
    vec               = and_si(vec, bit);
    vec               = cmplt_epi32(setzero_si(), vec);
    return castsi_ps(vec);
}

#undef SIMD_WRAPPER_1
#undef SIMD_WRAPPER_2
#undef SIMD_DWRAPPER_2
#undef SIMD_DWRAPPER_2I
#undef SIMD_WRAPPER_2I
#undef SIMD_WRAPPER_3
#undef SIMD_IWRAPPER_1
#undef SIMD_IWRAPPER_2
#undef SIMD_IFWRAPPER_2
#undef SIMD_IWRAPPER_2I
#undef SIMD_IWRAPPER_1
#undef SIMD_IWRAPPER_1I
#undef SIMD_IWRAPPER_1I_
#undef SIMD_IWRAPPER_2
#undef SIMD_IWRAPPER_2_
#undef SIMD_IWRAPPER_2I
