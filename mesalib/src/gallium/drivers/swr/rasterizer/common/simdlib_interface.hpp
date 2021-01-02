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
#if 0
//===========================================================================
// Placeholder name representing either SIMD4, SIMD256, or SIMD16 structures.
//===========================================================================
struct SIMD256 // or SIMD4 or SIMD16
{
    //=======================================================================
    // SIMD Types
    //
    // These typedefs are examples. The SIMD256 and SIMD16 implementations will
    // use different base types with this same naming.
    using Float     = __m256;  // Packed single-precision float vector
    using Double    = __m256d; // Packed double-precision float vector
    using Integer   = __m256i; // Packed integer vector (mutable element widths)
    using Mask      = uint8_t; // Integer representing mask bits

    //=======================================================================
    // Standard interface
    // (available in both SIMD256 and SIMD16 widths)
    //=======================================================================

    //-----------------------------------------------------------------------
    // Single precision floating point arithmetic operations
    //-----------------------------------------------------------------------
    static Float    add_ps(Float a, Float b);               // return a + b
    static Float    div_ps(Float a, Float b);               // return a / b
    static Float    fmadd_ps(Float a, Float b, Float c);    // return (a * b) + c
    static Float    fmsub_ps(Float a, Float b, Float c);    // return (a * b) - c
    static Float    max_ps(Float a, Float b);               // return (a > b) ? a : b
    static Float    min_ps(Float a, Float b);               // return (a < b) ? a : b
    static Float    mul_ps(Float a, Float b);               // return a * b
    static Float    rcp_ps(Float a);                        // return 1.0f / a
    static Float    rsqrt_ps(Float a);                      // return 1.0f / sqrt(a)
    static Float    sub_ps(Float a, Float b);               // return a - b

    enum class RoundMode
    {
        TO_NEAREST_INT  = 0x00, // Round to nearest integer == TRUNCATE(value + (signof(value))0.5)
        TO_NEG_INF      = 0x01, // Round to negative infinity
        TO_POS_INF      = 0x02, // Round to positive infinity
        TO_ZERO         = 0x03, // Round to 0 a.k.a. truncate
        CUR_DIRECTION   = 0x04, // Round in direction set in MXCSR register

        RAISE_EXC       = 0x00, // Raise exception on overflow
        NO_EXC          = 0x08, // Suppress exceptions

        NINT            = static_cast<int>(TO_NEAREST_INT)  | static_cast<int>(RAISE_EXC),
        NINT_NOEXC      = static_cast<int>(TO_NEAREST_INT)  | static_cast<int>(NO_EXC),
        FLOOR           = static_cast<int>(TO_NEG_INF)      | static_cast<int>(RAISE_EXC),
        FLOOR_NOEXC     = static_cast<int>(TO_NEG_INF)      | static_cast<int>(NO_EXC),
        CEIL            = static_cast<int>(TO_POS_INF)      | static_cast<int>(RAISE_EXC),
        CEIL_NOEXC      = static_cast<int>(TO_POS_INF)      | static_cast<int>(NO_EXC),
        TRUNC           = static_cast<int>(TO_ZERO)         | static_cast<int>(RAISE_EXC),
        TRUNC_NOEXC     = static_cast<int>(TO_ZERO)         | static_cast<int>(NO_EXC),
        RINT            = static_cast<int>(CUR_DIRECTION)   | static_cast<int>(RAISE_EXC),
        NEARBYINT       = static_cast<int>(CUR_DIRECTION)   | static_cast<int>(NO_EXC),
    };

    // return round_func(a)
    //
    // round_func is chosen on the RMT template parameter.  See the documentation
    // for the RoundMode enumeration above.
    template <RoundMode RMT>
    static Float    round_ps(Float a);                  // return round(a) 


    //-----------------------------------------------------------------------
    // Integer (various width) arithmetic operations
    //-----------------------------------------------------------------------
    static Integer  abs_epi32(Integer a);               // return absolute_value(a) (int32)
    static Integer  add_epi32(Integer a, Integer b);    // return a + b (int32)
    static Integer  add_epi8(Integer a, Integer b);     // return a + b (int8)
    static Integer  adds_epu8(Integer a, Integer b);    // return ((a + b) > 0xff) ? 0xff : (a + b) (uint8) 
    static Integer  max_epi32(Integer a, Integer b);    // return (a > b) ? a : b (int32)
    static Integer  max_epu32(Integer a, Integer b);    // return (a > b) ? a : b (uint32)
    static Integer  min_epi32(Integer a, Integer b);    // return (a < b) ? a : b (int32)
    static Integer  min_epu32(Integer a, Integer b);    // return (a < b) ? a : b (uint32)
    static Integer  mul_epi32(Integer a, Integer b);    // return a * b (int32)

    // return (a * b) & 0xFFFFFFFF
    //
    // Multiply the packed 32-bit integers in a and b, producing intermediate 64-bit integers,
    // and store the low 32 bits of the intermediate integers in dst.
    static Float    mullo_epi32(Integer a, Integer b);

    static Integer  sub_epi32(Integer a, Integer b);    // return a - b (int32)
    static Integer  sub_epi64(Integer a, Integer b);    // return a - b (int64)
    static Integer  subs_epu8(Integer a, Integer b);    // return (b > a) ? 0 : (a - b) (uint8)

    //-----------------------------------------------------------------------
    // Logical operations
    //-----------------------------------------------------------------------
    static Float    and_ps(Float a, Float b);           // return a & b       (float treated as int)
    static Integer  and_si(Integer a, Integer b);       // return a & b       (int)
    static Float    andnot_ps(Float a, Float b);        // return (~a) & b    (float treated as int)
    static Integer  andnot_si(Integer a, Integer b);    // return (~a) & b    (int)
    static Float    or_ps(Float a, Float b);            // return a | b       (float treated as int)
    static Float    or_si(Integer a, Integer b);        // return a | b       (int)
    static Float    xor_ps(Float a, Float b);           // return a ^ b       (float treated as int)
    static Integer  xor_si(Integer a, Integer b);       // return a ^ b       (int)

    //-----------------------------------------------------------------------
    // Shift operations
    //-----------------------------------------------------------------------
    template<int ImmT>
    static Integer  slli_epi32(Integer a);              // return a << ImmT
    static Integer  sllv_epi32(Integer a, Integer b);   // return a << b
    template<int ImmT>
    static Integer  srai_epi32(Integer a);              // return a >> ImmT   (int32)
    template<int ImmT>
    static Integer  srli_epi32(Integer a);              // return a >> ImmT   (uint32)
    template<int ImmT>                                  // for each 128-bit lane:
    static Integer  srli_si(Integer a);                 //  return a >> (ImmT*8) (uint)
    template<int ImmT>
    static Float    srlisi_ps(Float a);                 // same as srli_si, but with Float cast to int
    static Integer  srlv_epi32(Integer a, Integer b);   // return a >> b      (uint32)

    //-----------------------------------------------------------------------
    // Conversion operations
    //-----------------------------------------------------------------------
    static Float    castpd_ps(Double a);                // return *(Float*)(&a)
    static Integer  castps_si(Float a);                 // return *(Integer*)(&a)
    static Double   castsi_pd(Integer a);               // return *(Double*)(&a)
    static Double   castps_pd(Float a);                 // return *(Double*)(&a)
    static Float    castsi_ps(Integer a);               // return *(Float*)(&a)
    static Float    cvtepi32_ps(Integer a);             // return (float)a    (int32 --> float)
    static Integer  cvtepu8_epi16(Integer a);           // return (int16)a    (uint8 --> int16)
    static Integer  cvtepu8_epi32(Integer a);           // return (int32)a    (uint8 --> int32)
    static Integer  cvtepu16_epi32(Integer a);          // return (int32)a    (uint16 --> int32)
    static Integer  cvtepu16_epi64(Integer a);          // return (int64)a    (uint16 --> int64)
    static Integer  cvtepu32_epi64(Integer a);          // return (int64)a    (uint32 --> int64)
    static Integer  cvtps_epi32(Float a);               // return (int32)a    (float --> int32)
    static Integer  cvttps_epi32(Float a);              // return (int32)a    (rnd_to_zero(float) --> int32)

    //-----------------------------------------------------------------------
    // Comparison operations
    //-----------------------------------------------------------------------

    // Comparison types used with cmp_ps:
    //   - ordered comparisons are always false if either operand is NaN
    //   - unordered comparisons are always true if either operand is NaN
    //   - signaling comparisons raise an exception if either operand is NaN
    //   - non-signaling comparisons will never raise an exception
    // 
    // Ordered:     return (a != NaN) && (b != NaN) && (a cmp b)
    // Unordered:   return (a == NaN) || (b == NaN) || (a cmp b)
    enum class CompareType
    {
        EQ_OQ      = 0x00, // Equal (ordered, nonsignaling)
        LT_OS      = 0x01, // Less-than (ordered, signaling)
        LE_OS      = 0x02, // Less-than-or-equal (ordered, signaling)
        UNORD_Q    = 0x03, // Unordered (nonsignaling)
        NEQ_UQ     = 0x04, // Not-equal (unordered, nonsignaling)
        NLT_US     = 0x05, // Not-less-than (unordered, signaling)
        NLE_US     = 0x06, // Not-less-than-or-equal (unordered, signaling)
        ORD_Q      = 0x07, // Ordered (nonsignaling)
        EQ_UQ      = 0x08, // Equal (unordered, non-signaling)
        NGE_US     = 0x09, // Not-greater-than-or-equal (unordered, signaling)
        NGT_US     = 0x0A, // Not-greater-than (unordered, signaling)
        FALSE_OQ   = 0x0B, // False (ordered, nonsignaling)
        NEQ_OQ     = 0x0C, // Not-equal (ordered, non-signaling)
        GE_OS      = 0x0D, // Greater-than-or-equal (ordered, signaling)
        GT_OS      = 0x0E, // Greater-than (ordered, signaling)
        TRUE_UQ    = 0x0F, // True (unordered, non-signaling)
        EQ_OS      = 0x10, // Equal (ordered, signaling)
        LT_OQ      = 0x11, // Less-than (ordered, nonsignaling)
        LE_OQ      = 0x12, // Less-than-or-equal (ordered, nonsignaling)
        UNORD_S    = 0x13, // Unordered (signaling)
        NEQ_US     = 0x14, // Not-equal (unordered, signaling)
        NLT_UQ     = 0x15, // Not-less-than (unordered, nonsignaling)
        NLE_UQ     = 0x16, // Not-less-than-or-equal (unordered, nonsignaling)
        ORD_S      = 0x17, // Ordered (signaling)
        EQ_US      = 0x18, // Equal (unordered, signaling)
        NGE_UQ     = 0x19, // Not-greater-than-or-equal (unordered, nonsignaling)
        NGT_UQ     = 0x1A, // Not-greater-than (unordered, nonsignaling)
        FALSE_OS   = 0x1B, // False (ordered, signaling)
        NEQ_OS     = 0x1C, // Not-equal (ordered, signaling)
        GE_OQ      = 0x1D, // Greater-than-or-equal (ordered, nonsignaling)
        GT_OQ      = 0x1E, // Greater-than (ordered, nonsignaling)
        TRUE_US    = 0x1F, // True (unordered, signaling)
    };

    // return a (CmpTypeT) b (float)
    //
    // See documentation for CompareType above for valid values for CmpTypeT.
    template<CompareType CmpTypeT>
    static Float    cmp_ps(Float a, Float b);           // return a (CmtTypeT) b (see above)
    static Float    cmpgt_ps(Float a, Float b);         // return cmp_ps<CompareType::GT_OQ>(a, b)
    static Float    cmple_ps(Float a, Float b);         // return cmp_ps<CompareType::LE_OQ>(a, b)
    static Float    cmplt_ps(Float a, Float b);         // return cmp_ps<CompareType::LT_OQ>(a, b)
    static Float    cmpneq_ps(Float a, Float b);        // return cmp_ps<CompareType::NEQ_OQ>(a, b)
    static Float    cmpeq_ps(Float a, Float b);         // return cmp_ps<CompareType::EQ_OQ>(a, b)
    static Float    cmpge_ps(Float a, Float b);         // return cmp_ps<CompareType::GE_OQ>(a, b)
    static Integer  cmpeq_epi8(Integer a, Integer b);   // return a == b (int8)
    static Integer  cmpeq_epi16(Integer a, Integer b);  // return a == b (int16)
    static Integer  cmpeq_epi32(Integer a, Integer b);  // return a == b (int32)
    static Integer  cmpeq_epi64(Integer a, Integer b);  // return a == b (int64)
    static Integer  cmpgt_epi8(Integer a, Integer b);   // return a > b (int8)
    static Integer  cmpgt_epi16(Integer a, Integer b);  // return a > b (int16)
    static Integer  cmpgt_epi32(Integer a, Integer b);  // return a > b (int32)
    static Integer  cmpgt_epi64(Integer a, Integer b);  // return a > b (int64)
    static Integer  cmplt_epi32(Integer a, Integer b);  // return a < b (int32)
    static bool     testz_ps(Float a, Float b);         // return all_lanes_zero(a & b) ? 1 : 0 (float)
    static bool     testz_si(Integer a, Integer b);     // return all_lanes_zero(a & b) ? 1 : 0 (int)

    //-----------------------------------------------------------------------
    // Blend / shuffle / permute operations
    //-----------------------------------------------------------------------
    template<int ImmT>
    static Float    blend_ps(Float a, Float b);                     // return ImmT ? b : a  (float)
    static Integer  blendv_epi32(Integer a, Integer b, Float mask); // return mask ? b : a (int)
    static Float    blendv_ps(Float a, Float b, Float mask);        // return mask ? b : a (float)
    static Float    broadcast_ss(float const *p);                   // return *p (all elements in vector get same value)
    static Integer  packs_epi16(Integer a, Integer b);              // See documentation for _mm256_packs_epi16 and _mm512_packs_epi16
    static Integer  packs_epi32(Integer a, Integer b);              // See documentation for _mm256_packs_epi32 and _mm512_packs_epi32
    static Integer  packus_epi16(Integer a, Integer b);             // See documentation for _mm256_packus_epi16 and _mm512_packus_epi16
    static Integer  packus_epi32(Integer a, Integer b);             // See documentation for _mm256_packus_epi32 and _mm512_packus_epi32
    static Float    permute_epi32(Integer a, Integer swiz);         // return a[swiz[i]] for each 32-bit lane i (int32)
    static Float    permute_ps(Float a, Integer swiz);              // return a[swiz[i]] for each 32-bit lane i (float)
    template<int SwizT>
    static Integer  shuffle_epi32(Integer a, Integer b);    
    template<int SwizT>
    static Integer  shuffle_epi64(Integer a, Integer b);
    static Integer  shuffle_epi8(Integer a, Integer b);
    template<int SwizT>
    static Float    shuffle_pd(Double a, Double b);
    template<int SwizT>
    static Float    shuffle_ps(Float a, Float b);
    static Integer  unpackhi_epi16(Integer a, Integer b);
    static Integer  unpackhi_epi32(Integer a, Integer b);
    static Integer  unpackhi_epi64(Integer a, Integer b);
    static Integer  unpackhi_epi8(Integer a, Integer b);
    static Float    unpackhi_pd(Double a, Double b);
    static Float    unpackhi_ps(Float a, Float b);
    static Integer  unpacklo_epi16(Integer a, Integer b);
    static Integer  unpacklo_epi32(Integer a, Integer b);
    static Integer  unpacklo_epi64(Integer a, Integer b);
    static Integer  unpacklo_epi8(Integer a, Integer b);
    static Float    unpacklo_pd(Double a, Double b);
    static Float    unpacklo_ps(Float a, Float b);

    //-----------------------------------------------------------------------
    // Load / store operations
    //-----------------------------------------------------------------------
    enum class ScaleFactor
    {
        SF_1,   // No scaling
        SF_2,   // Scale offset by 2
        SF_4,   // Scale offset by 4
        SF_8,   // Scale offset by 8
    };

    template<ScaleFactor ScaleT = ScaleFactor::SF_1>
    static Float    i32gather_ps(float const* p, Integer idx);  // return *(float*)(((int8*)p) + (idx * ScaleT))
    static Float    load1_ps(float const *p);                   // return *p    (broadcast 1 value to all elements)
    static Float    load_ps(float const *p);                    // return *p    (loads SIMD width elements from memory)
    static Integer  load_si(Integer const *p);                  // return *p
    static Float    loadu_ps(float const *p);                   // return *p    (same as load_ps but allows for unaligned mem)
    static Integer  loadu_si(Integer const *p);                 // return *p    (same as load_si but allows for unaligned mem)

    // for each element: (mask & (1 << 31)) ? (i32gather_ps<ScaleT>(p, idx), mask = 0) : old
    template<int ScaleT>
    static Float    mask_i32gather_ps(Float old, float const* p, Integer idx, Float mask);

    static void     maskstore_ps(float *p, Integer mask, Float src);
    static int      movemask_epi8(Integer a);
    static int      movemask_pd(Double a);
    static int      movemask_ps(Float a);
    static Integer  set1_epi32(int i);                          // return i (all elements are same value)
    static Integer  set1_epi8(char i);                          // return i (all elements are same value)
    static Float    set1_ps(float f);                           // return f (all elements are same value)
    static Float    setzero_ps();                               // return 0 (float)
    static Integer  setzero_si();                               // return 0 (integer)
    static void     store_ps(float *p, Float a);                // *p = a   (stores all elements contiguously in memory)
    static void     store_si(Integer *p, Integer a);            // *p = a
    static void     stream_ps(float *p, Float a);               // *p = a   (same as store_ps, but doesn't keep memory in cache)

    //=======================================================================
    // Legacy interface (available only in SIMD256 width)
    //=======================================================================

    static Float    broadcast_ps(__m128 const *p);
    template<int ImmT>
    static __m128d  extractf128_pd(Double a);
    template<int ImmT>
    static __m128   extractf128_ps(Float a);
    template<int ImmT>
    static __m128i  extractf128_si(Integer a);
    template<int ImmT>
    static Double   insertf128_pd(Double a, __m128d b);
    template<int ImmT>
    static Float    insertf128_ps(Float a, __m128 b);
    template<int ImmT>
    static Integer  insertf128_si(Integer a, __m128i b);
    static Integer  loadu2_si(__m128 const* phi, __m128 const* plo);
    template<int ImmT>
    static Double   permute2f128_pd(Double a, Double b);
    template<int ImmT>
    static Float    permute2f128_ps(Float a, Float b);
    template<int ImmT>
    static Integer  permute2f128_si(Integer a, Integer b);
    static Integer  set_epi32(int i7, int i6, int i5, int i4, int i3, int i2, int i1, int i0);
    static void     storeu2_si(__m128i *phi, __m128i *plo, Integer src);

    //=======================================================================
    // Advanced masking interface (currently available only in SIMD16 width)
    //=======================================================================
};
#endif // #if 0
