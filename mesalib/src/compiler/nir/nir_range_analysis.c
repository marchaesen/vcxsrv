/*
 * Copyright © 2018 Intel Corporation
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
 */
#include <math.h>
#include <float.h>
#include "nir.h"
#include "nir_range_analysis.h"
#include "util/hash_table.h"

/**
 * Analyzes a sequence of operations to determine some aspects of the range of
 * the result.
 */

static bool
is_not_negative(enum ssa_ranges r)
{
   return r == gt_zero || r == ge_zero || r == eq_zero;
}

static void *
pack_data(const struct ssa_result_range r)
{
   return (void *)(uintptr_t)(r.range | r.is_integral << 8);
}

static struct ssa_result_range
unpack_data(const void *p)
{
   const uintptr_t v = (uintptr_t) p;

   return (struct ssa_result_range){v & 0xff, (v & 0x0ff00) != 0};
}

static void *
pack_key(const struct nir_alu_instr *instr, nir_alu_type type)
{
   uintptr_t type_encoding;
   uintptr_t ptr = (uintptr_t) instr;

   /* The low 2 bits have to be zero or this whole scheme falls apart. */
   assert((ptr & 0x3) == 0);

   /* NIR is typeless in the sense that sequences of bits have whatever
    * meaning is attached to them by the instruction that consumes them.
    * However, the number of bits must match between producer and consumer.
    * As a result, the number of bits does not need to be encoded here.
    */
   switch (nir_alu_type_get_base_type(type)) {
   case nir_type_int:   type_encoding = 0; break;
   case nir_type_uint:  type_encoding = 1; break;
   case nir_type_bool:  type_encoding = 2; break;
   case nir_type_float: type_encoding = 3; break;
   default: unreachable("Invalid base type.");
   }

   return (void *)(ptr | type_encoding);
}

static nir_alu_type
nir_alu_src_type(const nir_alu_instr *instr, unsigned src)
{
   return nir_alu_type_get_base_type(nir_op_infos[instr->op].input_types[src]) |
          nir_src_bit_size(instr->src[src].src);
}

static struct ssa_result_range
analyze_constant(const struct nir_alu_instr *instr, unsigned src,
                 nir_alu_type use_type)
{
   uint8_t swizzle[NIR_MAX_VEC_COMPONENTS] = { 0, 1, 2, 3,
                                               4, 5, 6, 7,
                                               8, 9, 10, 11,
                                               12, 13, 14, 15 };

   /* If the source is an explicitly sized source, then we need to reset
    * both the number of components and the swizzle.
    */
   const unsigned num_components = nir_ssa_alu_instr_src_components(instr, src);

   for (unsigned i = 0; i < num_components; ++i)
      swizzle[i] = instr->src[src].swizzle[i];

   const nir_load_const_instr *const load =
      nir_instr_as_load_const(instr->src[src].src.ssa->parent_instr);

   struct ssa_result_range r = { unknown, false };

   switch (nir_alu_type_get_base_type(use_type)) {
   case nir_type_float: {
      double min_value = DBL_MAX;
      double max_value = -DBL_MAX;
      bool any_zero = false;
      bool all_zero = true;

      r.is_integral = true;

      for (unsigned i = 0; i < num_components; ++i) {
         const double v = nir_const_value_as_float(load->value[swizzle[i]],
                                                   load->def.bit_size);

         if (floor(v) != v)
            r.is_integral = false;

         any_zero = any_zero || (v == 0.0);
         all_zero = all_zero && (v == 0.0);
         min_value = MIN2(min_value, v);
         max_value = MAX2(max_value, v);
      }

      assert(any_zero >= all_zero);
      assert(isnan(max_value) || max_value >= min_value);

      if (all_zero)
         r.range = eq_zero;
      else if (min_value > 0.0)
         r.range = gt_zero;
      else if (min_value == 0.0)
         r.range = ge_zero;
      else if (max_value < 0.0)
         r.range = lt_zero;
      else if (max_value == 0.0)
         r.range = le_zero;
      else if (!any_zero)
         r.range = ne_zero;
      else
         r.range = unknown;

      return r;
   }

   case nir_type_int:
   case nir_type_bool: {
      int64_t min_value = INT_MAX;
      int64_t max_value = INT_MIN;
      bool any_zero = false;
      bool all_zero = true;

      for (unsigned i = 0; i < num_components; ++i) {
         const int64_t v = nir_const_value_as_int(load->value[swizzle[i]],
                                                  load->def.bit_size);

         any_zero = any_zero || (v == 0);
         all_zero = all_zero && (v == 0);
         min_value = MIN2(min_value, v);
         max_value = MAX2(max_value, v);
      }

      assert(any_zero >= all_zero);
      assert(max_value >= min_value);

      if (all_zero)
         r.range = eq_zero;
      else if (min_value > 0)
         r.range = gt_zero;
      else if (min_value == 0)
         r.range = ge_zero;
      else if (max_value < 0)
         r.range = lt_zero;
      else if (max_value == 0)
         r.range = le_zero;
      else if (!any_zero)
         r.range = ne_zero;
      else
         r.range = unknown;

      return r;
   }

   case nir_type_uint: {
      bool any_zero = false;
      bool all_zero = true;

      for (unsigned i = 0; i < num_components; ++i) {
         const uint64_t v = nir_const_value_as_uint(load->value[swizzle[i]],
                                                    load->def.bit_size);

         any_zero = any_zero || (v == 0);
         all_zero = all_zero && (v == 0);
      }

      assert(any_zero >= all_zero);

      if (all_zero)
         r.range = eq_zero;
      else if (any_zero)
         r.range = ge_zero;
      else
         r.range = gt_zero;

      return r;
   }

   default:
      unreachable("Invalid alu source type");
   }
}

/**
 * Short-hand name for use in the tables in analyze_expression.  If this name
 * becomes a problem on some compiler, we can change it to _.
 */
#define _______ unknown


#if defined(__clang__)
   /* clang wants _Pragma("unroll X") */
   #define pragma_unroll_5 _Pragma("unroll 5")
   #define pragma_unroll_7 _Pragma("unroll 7")
/* gcc wants _Pragma("GCC unroll X") */
#elif defined(__GNUC__)
   #if __GNUC__ >= 8
      #define pragma_unroll_5 _Pragma("GCC unroll 5")
      #define pragma_unroll_7 _Pragma("GCC unroll 7")
   #else
      #pragma GCC optimize ("unroll-loops")
      #define pragma_unroll_5
      #define pragma_unroll_7
   #endif
#else
   /* MSVC doesn't have C99's _Pragma() */
   #define pragma_unroll_5
   #define pragma_unroll_7
#endif


#ifndef NDEBUG
#define ASSERT_TABLE_IS_COMMUTATIVE(t)                        \
   do {                                                       \
      static bool first = true;                               \
      if (first) {                                            \
         first = false;                                       \
         pragma_unroll_7                                      \
         for (unsigned r = 0; r < ARRAY_SIZE(t); r++) {       \
            pragma_unroll_7                                   \
            for (unsigned c = 0; c < ARRAY_SIZE(t[0]); c++)   \
               assert(t[r][c] == t[c][r]);                    \
         }                                                    \
      }                                                       \
   } while (false)

#define ASSERT_TABLE_IS_DIAGONAL(t)                           \
   do {                                                       \
      static bool first = true;                               \
      if (first) {                                            \
         first = false;                                       \
         pragma_unroll_7                                      \
         for (unsigned r = 0; r < ARRAY_SIZE(t); r++)         \
            assert(t[r][r] == r);                             \
      }                                                       \
   } while (false)

static enum ssa_ranges
union_ranges(enum ssa_ranges a, enum ssa_ranges b)
{
   static const enum ssa_ranges union_table[last_range + 1][last_range + 1] = {
      /* left\right   unknown  lt_zero  le_zero  gt_zero  ge_zero  ne_zero  eq_zero */
      /* unknown */ { _______, _______, _______, _______, _______, _______, _______ },
      /* lt_zero */ { _______, lt_zero, le_zero, ne_zero, _______, ne_zero, le_zero },
      /* le_zero */ { _______, le_zero, le_zero, _______, _______, _______, le_zero },
      /* gt_zero */ { _______, ne_zero, _______, gt_zero, ge_zero, ne_zero, ge_zero },
      /* ge_zero */ { _______, _______, _______, ge_zero, ge_zero, _______, ge_zero },
      /* ne_zero */ { _______, ne_zero, _______, ne_zero, _______, ne_zero, _______ },
      /* eq_zero */ { _______, le_zero, le_zero, ge_zero, ge_zero, _______, eq_zero },
   };

   ASSERT_TABLE_IS_COMMUTATIVE(union_table);
   ASSERT_TABLE_IS_DIAGONAL(union_table);

   return union_table[a][b];
}

/* Verify that the 'unknown' entry in each row (or column) of the table is the
 * union of all the other values in the row (or column).
 */
#define ASSERT_UNION_OF_OTHERS_MATCHES_UNKNOWN_2_SOURCE(t)              \
   do {                                                                 \
      static bool first = true;                                         \
      if (first) {                                                      \
         first = false;                                                 \
         pragma_unroll_7                                                \
         for (unsigned i = 0; i < last_range; i++) {                    \
            enum ssa_ranges col_range = t[i][unknown + 1];              \
            enum ssa_ranges row_range = t[unknown + 1][i];              \
                                                                        \
            pragma_unroll_5                                             \
            for (unsigned j = unknown + 2; j < last_range; j++) {       \
               col_range = union_ranges(col_range, t[i][j]);            \
               row_range = union_ranges(row_range, t[j][i]);            \
            }                                                           \
                                                                        \
            assert(col_range == t[i][unknown]);                         \
            assert(row_range == t[unknown][i]);                         \
         }                                                              \
      }                                                                 \
   } while (false)

/* For most operations, the union of ranges for a strict inequality and
 * equality should be the range of the non-strict inequality (e.g.,
 * union_ranges(range(op(lt_zero), range(op(eq_zero))) == range(op(le_zero)).
 *
 * Does not apply to selection-like opcodes (bcsel, fmin, fmax, etc.).
 */
#define ASSERT_UNION_OF_EQ_AND_STRICT_INEQ_MATCHES_NONSTRICT_1_SOURCE(t) \
   do {                                                                 \
      assert(union_ranges(t[lt_zero], t[eq_zero]) == t[le_zero]);       \
      assert(union_ranges(t[gt_zero], t[eq_zero]) == t[ge_zero]);       \
   } while (false)

#define ASSERT_UNION_OF_EQ_AND_STRICT_INEQ_MATCHES_NONSTRICT_2_SOURCE(t) \
   do {                                                                 \
      static bool first = true;                                         \
      if (first) {                                                      \
         first = false;                                                 \
         pragma_unroll_7                                                \
         for (unsigned i = 0; i < last_range; i++) {                    \
            assert(union_ranges(t[i][lt_zero], t[i][eq_zero]) == t[i][le_zero]); \
            assert(union_ranges(t[i][gt_zero], t[i][eq_zero]) == t[i][ge_zero]); \
            assert(union_ranges(t[lt_zero][i], t[eq_zero][i]) == t[le_zero][i]); \
            assert(union_ranges(t[gt_zero][i], t[eq_zero][i]) == t[ge_zero][i]); \
         }                                                              \
      }                                                                 \
   } while (false)

/* Several other unordered tuples span the range of "everything."  Each should
 * have the same value as unknown: (lt_zero, ge_zero), (le_zero, gt_zero), and
 * (eq_zero, ne_zero).  union_ranges is already commutative, so only one
 * ordering needs to be checked.
 *
 * Does not apply to selection-like opcodes (bcsel, fmin, fmax, etc.).
 *
 * In cases where this can be used, it is unnecessary to also use
 * ASSERT_UNION_OF_OTHERS_MATCHES_UNKNOWN_*_SOURCE.  For any range X,
 * union_ranges(X, X) == X.  The disjoint ranges cover all of the non-unknown
 * possibilities, so the union of all the unions of disjoint ranges is
 * equivalent to the union of "others."
 */
#define ASSERT_UNION_OF_DISJOINT_MATCHES_UNKNOWN_1_SOURCE(t)            \
   do {                                                                 \
      assert(union_ranges(t[lt_zero], t[ge_zero]) == t[unknown]);       \
      assert(union_ranges(t[le_zero], t[gt_zero]) == t[unknown]);       \
      assert(union_ranges(t[eq_zero], t[ne_zero]) == t[unknown]);       \
   } while (false)

#define ASSERT_UNION_OF_DISJOINT_MATCHES_UNKNOWN_2_SOURCE(t)            \
   do {                                                                 \
      static bool first = true;                                         \
      if (first) {                                                      \
         first = false;                                                 \
         pragma_unroll_7                                                \
         for (unsigned i = 0; i < last_range; i++) {                    \
            assert(union_ranges(t[i][lt_zero], t[i][ge_zero]) ==        \
                   t[i][unknown]);                                      \
            assert(union_ranges(t[i][le_zero], t[i][gt_zero]) ==        \
                   t[i][unknown]);                                      \
            assert(union_ranges(t[i][eq_zero], t[i][ne_zero]) ==        \
                   t[i][unknown]);                                      \
                                                                        \
            assert(union_ranges(t[lt_zero][i], t[ge_zero][i]) ==        \
                   t[unknown][i]);                                      \
            assert(union_ranges(t[le_zero][i], t[gt_zero][i]) ==        \
                   t[unknown][i]);                                      \
            assert(union_ranges(t[eq_zero][i], t[ne_zero][i]) ==        \
                   t[unknown][i]);                                      \
         }                                                              \
      }                                                                 \
   } while (false)

#else
#define ASSERT_TABLE_IS_COMMUTATIVE(t)
#define ASSERT_TABLE_IS_DIAGONAL(t)
#define ASSERT_UNION_OF_OTHERS_MATCHES_UNKNOWN_2_SOURCE(t)
#define ASSERT_UNION_OF_EQ_AND_STRICT_INEQ_MATCHES_NONSTRICT_1_SOURCE(t)
#define ASSERT_UNION_OF_EQ_AND_STRICT_INEQ_MATCHES_NONSTRICT_2_SOURCE(t)
#define ASSERT_UNION_OF_DISJOINT_MATCHES_UNKNOWN_1_SOURCE(t)
#define ASSERT_UNION_OF_DISJOINT_MATCHES_UNKNOWN_2_SOURCE(t)
#endif

/**
 * Analyze an expression to determine the range of its result
 *
 * The end result of this analysis is a token that communicates something
 * about the range of values.  There's an implicit grammar that produces
 * tokens from sequences of literal values, other tokens, and operations.
 * This function implements this grammar as a recursive-descent parser.  Some
 * (but not all) of the grammar is listed in-line in the function.
 */
static struct ssa_result_range
analyze_expression(const nir_alu_instr *instr, unsigned src,
                   struct hash_table *ht, nir_alu_type use_type)
{
   /* Ensure that the _Pragma("GCC unroll 7") above are correct. */
   STATIC_ASSERT(last_range + 1 == 7);

   if (!instr->src[src].src.is_ssa)
      return (struct ssa_result_range){unknown, false};

   if (nir_src_is_const(instr->src[src].src))
      return analyze_constant(instr, src, use_type);

   if (instr->src[src].src.ssa->parent_instr->type != nir_instr_type_alu)
      return (struct ssa_result_range){unknown, false};

   const struct nir_alu_instr *const alu =
       nir_instr_as_alu(instr->src[src].src.ssa->parent_instr);

   /* Bail if the type of the instruction generating the value does not match
    * the type the value will be interpreted as.  int/uint/bool can be
    * reinterpreted trivially.  The most important cases are between float and
    * non-float.
    */
   if (alu->op != nir_op_mov && alu->op != nir_op_bcsel) {
      const nir_alu_type use_base_type =
         nir_alu_type_get_base_type(use_type);
      const nir_alu_type src_base_type =
         nir_alu_type_get_base_type(nir_op_infos[alu->op].output_type);

      if (use_base_type != src_base_type &&
          (use_base_type == nir_type_float ||
           src_base_type == nir_type_float)) {
         return (struct ssa_result_range){unknown, false};
      }
   }

   struct hash_entry *he = _mesa_hash_table_search(ht, pack_key(alu, use_type));
   if (he != NULL)
      return unpack_data(he->data);

   struct ssa_result_range r = {unknown, false};

   /* ge_zero: ge_zero + ge_zero
    *
    * gt_zero: gt_zero + eq_zero
    *        | gt_zero + ge_zero
    *        | eq_zero + gt_zero   # Addition is commutative
    *        | ge_zero + gt_zero   # Addition is commutative
    *        | gt_zero + gt_zero
    *        ;
    *
    * le_zero: le_zero + le_zero
    *
    * lt_zero: lt_zero + eq_zero
    *        | lt_zero + le_zero
    *        | eq_zero + lt_zero   # Addition is commutative
    *        | le_zero + lt_zero   # Addition is commutative
    *        | lt_zero + lt_zero
    *        ;
    *
    * ne_zero: eq_zero + ne_zero
    *        | ne_zero + eq_zero   # Addition is commutative
    *        ;
    *
    * eq_zero: eq_zero + eq_zero
    *        ;
    *
    * All other cases are 'unknown'.  The seeming odd entry is (ne_zero,
    * ne_zero), but that could be (-5, +5) which is not ne_zero.
    */
   static const enum ssa_ranges fadd_table[last_range + 1][last_range + 1] = {
      /* left\right   unknown  lt_zero  le_zero  gt_zero  ge_zero  ne_zero  eq_zero */
      /* unknown */ { _______, _______, _______, _______, _______, _______, _______ },
      /* lt_zero */ { _______, lt_zero, lt_zero, _______, _______, _______, lt_zero },
      /* le_zero */ { _______, lt_zero, le_zero, _______, _______, _______, le_zero },
      /* gt_zero */ { _______, _______, _______, gt_zero, gt_zero, _______, gt_zero },
      /* ge_zero */ { _______, _______, _______, gt_zero, ge_zero, _______, ge_zero },
      /* ne_zero */ { _______, _______, _______, _______, _______, _______, ne_zero },
      /* eq_zero */ { _______, lt_zero, le_zero, gt_zero, ge_zero, ne_zero, eq_zero },
   };

   ASSERT_TABLE_IS_COMMUTATIVE(fadd_table);
   ASSERT_UNION_OF_DISJOINT_MATCHES_UNKNOWN_2_SOURCE(fadd_table);
   ASSERT_UNION_OF_EQ_AND_STRICT_INEQ_MATCHES_NONSTRICT_2_SOURCE(fadd_table);

   /* Due to flush-to-zero semanatics of floating-point numbers with very
    * small mangnitudes, we can never really be sure a result will be
    * non-zero.
    *
    * ge_zero: ge_zero * ge_zero
    *        | ge_zero * gt_zero
    *        | ge_zero * eq_zero
    *        | le_zero * lt_zero
    *        | lt_zero * le_zero  # Multiplication is commutative
    *        | le_zero * le_zero
    *        | gt_zero * ge_zero  # Multiplication is commutative
    *        | eq_zero * ge_zero  # Multiplication is commutative
    *        | a * a              # Left source == right source
    *        | gt_zero * gt_zero
    *        | lt_zero * lt_zero
    *        ;
    *
    * le_zero: ge_zero * le_zero
    *        | ge_zero * lt_zero
    *        | lt_zero * ge_zero  # Multiplication is commutative
    *        | le_zero * ge_zero  # Multiplication is commutative
    *        | le_zero * gt_zero
    *        | lt_zero * gt_zero
    *        | gt_zero * lt_zero  # Multiplication is commutative
    *        ;
    *
    * eq_zero: eq_zero * <any>
    *          <any> * eq_zero    # Multiplication is commutative
    *
    * All other cases are 'unknown'.
    */
   static const enum ssa_ranges fmul_table[last_range + 1][last_range + 1] = {
      /* left\right   unknown  lt_zero  le_zero  gt_zero  ge_zero  ne_zero  eq_zero */
      /* unknown */ { _______, _______, _______, _______, _______, _______, eq_zero },
      /* lt_zero */ { _______, ge_zero, ge_zero, le_zero, le_zero, _______, eq_zero },
      /* le_zero */ { _______, ge_zero, ge_zero, le_zero, le_zero, _______, eq_zero },
      /* gt_zero */ { _______, le_zero, le_zero, ge_zero, ge_zero, _______, eq_zero },
      /* ge_zero */ { _______, le_zero, le_zero, ge_zero, ge_zero, _______, eq_zero },
      /* ne_zero */ { _______, _______, _______, _______, _______, _______, eq_zero },
      /* eq_zero */ { eq_zero, eq_zero, eq_zero, eq_zero, eq_zero, eq_zero, eq_zero }
   };

   ASSERT_TABLE_IS_COMMUTATIVE(fmul_table);
   ASSERT_UNION_OF_DISJOINT_MATCHES_UNKNOWN_2_SOURCE(fmul_table);
   ASSERT_UNION_OF_EQ_AND_STRICT_INEQ_MATCHES_NONSTRICT_2_SOURCE(fmul_table);

   static const enum ssa_ranges fneg_table[last_range + 1] = {
   /* unknown  lt_zero  le_zero  gt_zero  ge_zero  ne_zero  eq_zero */
      _______, gt_zero, ge_zero, lt_zero, le_zero, ne_zero, eq_zero
   };

   ASSERT_UNION_OF_DISJOINT_MATCHES_UNKNOWN_1_SOURCE(fneg_table);
   ASSERT_UNION_OF_EQ_AND_STRICT_INEQ_MATCHES_NONSTRICT_1_SOURCE(fneg_table);


   switch (alu->op) {
   case nir_op_b2f32:
   case nir_op_b2i32:
      r = (struct ssa_result_range){ge_zero, alu->op == nir_op_b2f32};
      break;

   case nir_op_bcsel: {
      const struct ssa_result_range left =
         analyze_expression(alu, 1, ht, use_type);
      const struct ssa_result_range right =
         analyze_expression(alu, 2, ht, use_type);

      r.is_integral = left.is_integral && right.is_integral;

      /* le_zero: bcsel(<any>, le_zero, lt_zero)
       *        | bcsel(<any>, eq_zero, lt_zero)
       *        | bcsel(<any>, le_zero, eq_zero)
       *        | bcsel(<any>, lt_zero, le_zero)
       *        | bcsel(<any>, lt_zero, eq_zero)
       *        | bcsel(<any>, eq_zero, le_zero)
       *        | bcsel(<any>, le_zero, le_zero)
       *        ;
       *
       * lt_zero: bcsel(<any>, lt_zero, lt_zero)
       *        ;
       *
       * ge_zero: bcsel(<any>, ge_zero, ge_zero)
       *        | bcsel(<any>, ge_zero, gt_zero)
       *        | bcsel(<any>, ge_zero, eq_zero)
       *        | bcsel(<any>, gt_zero, ge_zero)
       *        | bcsel(<any>, eq_zero, ge_zero)
       *        ;
       *
       * gt_zero: bcsel(<any>, gt_zero, gt_zero)
       *        ;
       *
       * ne_zero: bcsel(<any>, ne_zero, gt_zero)
       *        | bcsel(<any>, ne_zero, lt_zero)
       *        | bcsel(<any>, gt_zero, lt_zero)
       *        | bcsel(<any>, gt_zero, ne_zero)
       *        | bcsel(<any>, lt_zero, ne_zero)
       *        | bcsel(<any>, lt_zero, gt_zero)
       *        | bcsel(<any>, ne_zero, ne_zero)
       *        ;
       *
       * eq_zero: bcsel(<any>, eq_zero, eq_zero)
       *        ;
       *
       * All other cases are 'unknown'.
       *
       * The ranges could be tightened if the range of the first source is
       * known.  However, opt_algebraic will (eventually) elminiate the bcsel
       * if the condition is known.
       */
      static const enum ssa_ranges table[last_range + 1][last_range + 1] = {
         /* left\right   unknown  lt_zero  le_zero  gt_zero  ge_zero  ne_zero  eq_zero */
         /* unknown */ { _______, _______, _______, _______, _______, _______, _______ },
         /* lt_zero */ { _______, lt_zero, le_zero, ne_zero, _______, ne_zero, le_zero },
         /* le_zero */ { _______, le_zero, le_zero, _______, _______, _______, le_zero },
         /* gt_zero */ { _______, ne_zero, _______, gt_zero, ge_zero, ne_zero, ge_zero },
         /* ge_zero */ { _______, _______, _______, ge_zero, ge_zero, _______, ge_zero },
         /* ne_zero */ { _______, ne_zero, _______, ne_zero, _______, ne_zero, _______ },
         /* eq_zero */ { _______, le_zero, le_zero, ge_zero, ge_zero, _______, eq_zero },
      };

      ASSERT_TABLE_IS_COMMUTATIVE(table);
      ASSERT_TABLE_IS_DIAGONAL(table);
      ASSERT_UNION_OF_OTHERS_MATCHES_UNKNOWN_2_SOURCE(table);

      r.range = table[left.range][right.range];
      break;
   }

   case nir_op_i2f32:
   case nir_op_u2f32:
      r = analyze_expression(alu, 0, ht, nir_alu_src_type(alu, 0));

      r.is_integral = true;

      if (r.range == unknown && alu->op == nir_op_u2f32)
         r.range = ge_zero;

      break;

   case nir_op_fabs:
      r = analyze_expression(alu, 0, ht, nir_alu_src_type(alu, 0));

      switch (r.range) {
      case unknown:
      case le_zero:
      case ge_zero:
         r.range = ge_zero;
         break;

      case lt_zero:
      case gt_zero:
      case ne_zero:
         r.range = gt_zero;
         break;

      case eq_zero:
         break;
      }

      break;

   case nir_op_fadd: {
      const struct ssa_result_range left =
         analyze_expression(alu, 0, ht, nir_alu_src_type(alu, 0));
      const struct ssa_result_range right =
         analyze_expression(alu, 1, ht, nir_alu_src_type(alu, 1));

      r.is_integral = left.is_integral && right.is_integral;
      r.range = fadd_table[left.range][right.range];
      break;
   }

   case nir_op_fexp2: {
      /* If the parameter might be less than zero, the mathematically result
       * will be on (0, 1).  For sufficiently large magnitude negative
       * parameters, the result will flush to zero.
       */
      static const enum ssa_ranges table[last_range + 1] = {
      /* unknown  lt_zero  le_zero  gt_zero  ge_zero  ne_zero  eq_zero */
         ge_zero, ge_zero, ge_zero, gt_zero, gt_zero, ge_zero, gt_zero
      };

      r = analyze_expression(alu, 0, ht, nir_alu_src_type(alu, 0));

      ASSERT_UNION_OF_DISJOINT_MATCHES_UNKNOWN_1_SOURCE(table);
      ASSERT_UNION_OF_EQ_AND_STRICT_INEQ_MATCHES_NONSTRICT_1_SOURCE(table);

      r.is_integral = r.is_integral && is_not_negative(r.range);
      r.range = table[r.range];
      break;
   }

   case nir_op_fmax: {
      const struct ssa_result_range left =
         analyze_expression(alu, 0, ht, nir_alu_src_type(alu, 0));
      const struct ssa_result_range right =
         analyze_expression(alu, 1, ht, nir_alu_src_type(alu, 1));

      r.is_integral = left.is_integral && right.is_integral;

      /* gt_zero: fmax(gt_zero, *)
       *        | fmax(*, gt_zero)        # Treat fmax as commutative
       *        ;
       *
       * ge_zero: fmax(ge_zero, ne_zero)
       *        | fmax(ge_zero, lt_zero)
       *        | fmax(ge_zero, le_zero)
       *        | fmax(ge_zero, eq_zero)
       *        | fmax(ne_zero, ge_zero)  # Treat fmax as commutative
       *        | fmax(lt_zero, ge_zero)  # Treat fmax as commutative
       *        | fmax(le_zero, ge_zero)  # Treat fmax as commutative
       *        | fmax(eq_zero, ge_zero)  # Treat fmax as commutative
       *        | fmax(ge_zero, ge_zero)
       *        ;
       *
       * le_zero: fmax(le_zero, lt_zero)
       *        | fmax(lt_zero, le_zero)  # Treat fmax as commutative
       *        | fmax(le_zero, le_zero)
       *        ;
       *
       * lt_zero: fmax(lt_zero, lt_zero)
       *        ;
       *
       * ne_zero: fmax(ne_zero, lt_zero)
       *        | fmax(lt_zero, ne_zero)  # Treat fmax as commutative
       *        | fmax(ne_zero, ne_zero)
       *        ;
       *
       * eq_zero: fmax(eq_zero, le_zero)
       *        | fmax(eq_zero, lt_zero)
       *        | fmax(le_zero, eq_zero)  # Treat fmax as commutative
       *        | fmax(lt_zero, eq_zero)  # Treat fmax as commutative
       *        | fmax(eq_zero, eq_zero)
       *        ;
       *
       * All other cases are 'unknown'.
       */
      static const enum ssa_ranges table[last_range + 1][last_range + 1] = {
         /* left\right   unknown  lt_zero  le_zero  gt_zero  ge_zero  ne_zero  eq_zero */
         /* unknown */ { _______, _______, _______, gt_zero, ge_zero, _______, _______ },
         /* lt_zero */ { _______, lt_zero, le_zero, gt_zero, ge_zero, ne_zero, eq_zero },
         /* le_zero */ { _______, le_zero, le_zero, gt_zero, ge_zero, _______, eq_zero },
         /* gt_zero */ { gt_zero, gt_zero, gt_zero, gt_zero, gt_zero, gt_zero, gt_zero },
         /* ge_zero */ { ge_zero, ge_zero, ge_zero, gt_zero, ge_zero, ge_zero, ge_zero },
         /* ne_zero */ { _______, ne_zero, _______, gt_zero, ge_zero, ne_zero, _______ },
         /* eq_zero */ { _______, eq_zero, eq_zero, gt_zero, ge_zero, _______, eq_zero }
      };

      /* Treat fmax as commutative. */
      ASSERT_TABLE_IS_COMMUTATIVE(table);
      ASSERT_TABLE_IS_DIAGONAL(table);
      ASSERT_UNION_OF_OTHERS_MATCHES_UNKNOWN_2_SOURCE(table);

      r.range = table[left.range][right.range];
      break;
   }

   case nir_op_fmin: {
      const struct ssa_result_range left =
         analyze_expression(alu, 0, ht, nir_alu_src_type(alu, 0));
      const struct ssa_result_range right =
         analyze_expression(alu, 1, ht, nir_alu_src_type(alu, 1));

      r.is_integral = left.is_integral && right.is_integral;

      /* lt_zero: fmin(lt_zero, *)
       *        | fmin(*, lt_zero)        # Treat fmin as commutative
       *        ;
       *
       * le_zero: fmin(le_zero, ne_zero)
       *        | fmin(le_zero, gt_zero)
       *        | fmin(le_zero, ge_zero)
       *        | fmin(le_zero, eq_zero)
       *        | fmin(ne_zero, le_zero)  # Treat fmin as commutative
       *        | fmin(gt_zero, le_zero)  # Treat fmin as commutative
       *        | fmin(ge_zero, le_zero)  # Treat fmin as commutative
       *        | fmin(eq_zero, le_zero)  # Treat fmin as commutative
       *        | fmin(le_zero, le_zero)
       *        ;
       *
       * ge_zero: fmin(ge_zero, gt_zero)
       *        | fmin(gt_zero, ge_zero)  # Treat fmin as commutative
       *        | fmin(ge_zero, ge_zero)
       *        ;
       *
       * gt_zero: fmin(gt_zero, gt_zero)
       *        ;
       *
       * ne_zero: fmin(ne_zero, gt_zero)
       *        | fmin(gt_zero, ne_zero)  # Treat fmin as commutative
       *        | fmin(ne_zero, ne_zero)
       *        ;
       *
       * eq_zero: fmin(eq_zero, ge_zero)
       *        | fmin(eq_zero, gt_zero)
       *        | fmin(ge_zero, eq_zero)  # Treat fmin as commutative
       *        | fmin(gt_zero, eq_zero)  # Treat fmin as commutative
       *        | fmin(eq_zero, eq_zero)
       *        ;
       *
       * All other cases are 'unknown'.
       */
      static const enum ssa_ranges table[last_range + 1][last_range + 1] = {
         /* left\right   unknown  lt_zero  le_zero  gt_zero  ge_zero  ne_zero  eq_zero */
         /* unknown */ { _______, lt_zero, le_zero, _______, _______, _______, _______ },
         /* lt_zero */ { lt_zero, lt_zero, lt_zero, lt_zero, lt_zero, lt_zero, lt_zero },
         /* le_zero */ { le_zero, lt_zero, le_zero, le_zero, le_zero, le_zero, le_zero },
         /* gt_zero */ { _______, lt_zero, le_zero, gt_zero, ge_zero, ne_zero, eq_zero },
         /* ge_zero */ { _______, lt_zero, le_zero, ge_zero, ge_zero, _______, eq_zero },
         /* ne_zero */ { _______, lt_zero, le_zero, ne_zero, _______, ne_zero, _______ },
         /* eq_zero */ { _______, lt_zero, le_zero, eq_zero, eq_zero, _______, eq_zero }
      };

      /* Treat fmin as commutative. */
      ASSERT_TABLE_IS_COMMUTATIVE(table);
      ASSERT_TABLE_IS_DIAGONAL(table);
      ASSERT_UNION_OF_OTHERS_MATCHES_UNKNOWN_2_SOURCE(table);

      r.range = table[left.range][right.range];
      break;
   }

   case nir_op_fmul: {
      const struct ssa_result_range left =
         analyze_expression(alu, 0, ht, nir_alu_src_type(alu, 0));
      const struct ssa_result_range right =
         analyze_expression(alu, 1, ht, nir_alu_src_type(alu, 1));

      r.is_integral = left.is_integral && right.is_integral;

      /* x * x => ge_zero */
      if (left.range != eq_zero && nir_alu_srcs_equal(alu, alu, 0, 1)) {
         /* Even if x > 0, the result of x*x can be zero when x is, for
          * example, a subnormal number.
          */
         r.range = ge_zero;
      } else if (left.range != eq_zero && nir_alu_srcs_negative_equal(alu, alu, 0, 1)) {
         /* -x * x => le_zero. */
         r.range = le_zero;
      } else
         r.range = fmul_table[left.range][right.range];

      break;
   }

   case nir_op_frcp:
      r = (struct ssa_result_range){
         analyze_expression(alu, 0, ht, nir_alu_src_type(alu, 0)).range,
         false
      };
      break;

   case nir_op_mov:
      r = analyze_expression(alu, 0, ht, use_type);
      break;

   case nir_op_fneg:
      r = analyze_expression(alu, 0, ht, nir_alu_src_type(alu, 0));

      r.range = fneg_table[r.range];
      break;

   case nir_op_fsat:
      r = analyze_expression(alu, 0, ht, nir_alu_src_type(alu, 0));

      switch (r.range) {
      case le_zero:
      case lt_zero:
         r.range = eq_zero;
         r.is_integral = true;
         break;

      case eq_zero:
         assert(r.is_integral);
         FALLTHROUGH;
      case gt_zero:
      case ge_zero:
         /* The fsat doesn't add any information in these cases. */
         break;

      case ne_zero:
      case unknown:
         /* Since the result must be in [0, 1], the value must be >= 0. */
         r.range = ge_zero;
         break;
      }
      break;

   case nir_op_fsign:
      r = (struct ssa_result_range){
         analyze_expression(alu, 0, ht, nir_alu_src_type(alu, 0)).range,
         true
      };
      break;

   case nir_op_fsqrt:
   case nir_op_frsq:
      r = (struct ssa_result_range){ge_zero, false};
      break;

   case nir_op_ffloor: {
      const struct ssa_result_range left =
         analyze_expression(alu, 0, ht, nir_alu_src_type(alu, 0));

      r.is_integral = true;

      if (left.is_integral || left.range == le_zero || left.range == lt_zero)
         r.range = left.range;
      else if (left.range == ge_zero || left.range == gt_zero)
         r.range = ge_zero;
      else if (left.range == ne_zero)
         r.range = unknown;

      break;
   }

   case nir_op_fceil: {
      const struct ssa_result_range left =
         analyze_expression(alu, 0, ht, nir_alu_src_type(alu, 0));

      r.is_integral = true;

      if (left.is_integral || left.range == ge_zero || left.range == gt_zero)
         r.range = left.range;
      else if (left.range == le_zero || left.range == lt_zero)
         r.range = le_zero;
      else if (left.range == ne_zero)
         r.range = unknown;

      break;
   }

   case nir_op_ftrunc: {
      const struct ssa_result_range left =
         analyze_expression(alu, 0, ht, nir_alu_src_type(alu, 0));

      r.is_integral = true;

      if (left.is_integral)
         r.range = left.range;
      else if (left.range == ge_zero || left.range == gt_zero)
         r.range = ge_zero;
      else if (left.range == le_zero || left.range == lt_zero)
         r.range = le_zero;
      else if (left.range == ne_zero)
         r.range = unknown;

      break;
   }

   case nir_op_flt:
   case nir_op_fge:
   case nir_op_feq:
   case nir_op_fneu:
   case nir_op_ilt:
   case nir_op_ige:
   case nir_op_ieq:
   case nir_op_ine:
   case nir_op_ult:
   case nir_op_uge:
      /* Boolean results are 0 or -1. */
      r = (struct ssa_result_range){le_zero, false};
      break;

   case nir_op_fpow: {
      /* Due to flush-to-zero semanatics of floating-point numbers with very
       * small mangnitudes, we can never really be sure a result will be
       * non-zero.
       *
       * NIR uses pow() and powf() to constant evaluate nir_op_fpow.  The man
       * page for that function says:
       *
       *    If y is 0, the result is 1.0 (even if x is a NaN).
       *
       * gt_zero: pow(*, eq_zero)
       *        | pow(eq_zero, lt_zero)   # 0^-y = +inf
       *        | pow(eq_zero, le_zero)   # 0^-y = +inf or 0^0 = 1.0
       *        ;
       *
       * eq_zero: pow(eq_zero, gt_zero)
       *        ;
       *
       * ge_zero: pow(gt_zero, gt_zero)
       *        | pow(gt_zero, ge_zero)
       *        | pow(gt_zero, lt_zero)
       *        | pow(gt_zero, le_zero)
       *        | pow(gt_zero, ne_zero)
       *        | pow(gt_zero, unknown)
       *        | pow(ge_zero, gt_zero)
       *        | pow(ge_zero, ge_zero)
       *        | pow(ge_zero, lt_zero)
       *        | pow(ge_zero, le_zero)
       *        | pow(ge_zero, ne_zero)
       *        | pow(ge_zero, unknown)
       *        | pow(eq_zero, ge_zero)  # 0^0 = 1.0 or 0^+y = 0.0
       *        | pow(eq_zero, ne_zero)  # 0^-y = +inf or 0^+y = 0.0
       *        | pow(eq_zero, unknown)  # union of all other y cases
       *        ;
       *
       * All other cases are unknown.
       *
       * We could do better if the right operand is a constant, integral
       * value.
       */
      static const enum ssa_ranges table[last_range + 1][last_range + 1] = {
         /* left\right   unknown  lt_zero  le_zero  gt_zero  ge_zero  ne_zero  eq_zero */
         /* unknown */ { _______, _______, _______, _______, _______, _______, gt_zero },
         /* lt_zero */ { _______, _______, _______, _______, _______, _______, gt_zero },
         /* le_zero */ { _______, _______, _______, _______, _______, _______, gt_zero },
         /* gt_zero */ { ge_zero, ge_zero, ge_zero, ge_zero, ge_zero, ge_zero, gt_zero },
         /* ge_zero */ { ge_zero, ge_zero, ge_zero, ge_zero, ge_zero, ge_zero, gt_zero },
         /* ne_zero */ { _______, _______, _______, _______, _______, _______, gt_zero },
         /* eq_zero */ { ge_zero, gt_zero, gt_zero, eq_zero, ge_zero, ge_zero, gt_zero },
      };

      const struct ssa_result_range left =
         analyze_expression(alu, 0, ht, nir_alu_src_type(alu, 0));
      const struct ssa_result_range right =
         analyze_expression(alu, 1, ht, nir_alu_src_type(alu, 1));

      ASSERT_UNION_OF_DISJOINT_MATCHES_UNKNOWN_2_SOURCE(table);
      ASSERT_UNION_OF_EQ_AND_STRICT_INEQ_MATCHES_NONSTRICT_2_SOURCE(table);

      r.is_integral = left.is_integral && right.is_integral &&
                      is_not_negative(right.range);
      r.range = table[left.range][right.range];
      break;
   }

   case nir_op_ffma: {
      const struct ssa_result_range first =
         analyze_expression(alu, 0, ht, nir_alu_src_type(alu, 0));
      const struct ssa_result_range second =
         analyze_expression(alu, 1, ht, nir_alu_src_type(alu, 1));
      const struct ssa_result_range third =
         analyze_expression(alu, 2, ht, nir_alu_src_type(alu, 2));

      r.is_integral = first.is_integral && second.is_integral &&
                      third.is_integral;

      enum ssa_ranges fmul_range;

      if (first.range != eq_zero && nir_alu_srcs_equal(alu, alu, 0, 1)) {
         /* See handling of nir_op_fmul for explanation of why ge_zero is the
          * range.
          */
         fmul_range = ge_zero;
      } else if (first.range != eq_zero && nir_alu_srcs_negative_equal(alu, alu, 0, 1)) {
         /* -x * x => le_zero */
         fmul_range = le_zero;
      } else
         fmul_range = fmul_table[first.range][second.range];

      r.range = fadd_table[fmul_range][third.range];
      break;
   }

   case nir_op_flrp: {
      const struct ssa_result_range first =
         analyze_expression(alu, 0, ht, nir_alu_src_type(alu, 0));
      const struct ssa_result_range second =
         analyze_expression(alu, 1, ht, nir_alu_src_type(alu, 1));
      const struct ssa_result_range third =
         analyze_expression(alu, 2, ht, nir_alu_src_type(alu, 2));

      r.is_integral = first.is_integral && second.is_integral &&
                      third.is_integral;

      /* Decompose the flrp to first + third * (second + -first) */
      const enum ssa_ranges inner_fadd_range =
         fadd_table[second.range][fneg_table[first.range]];

      const enum ssa_ranges fmul_range =
         fmul_table[third.range][inner_fadd_range];

      r.range = fadd_table[first.range][fmul_range];
      break;
   }

   default:
      r = (struct ssa_result_range){unknown, false};
      break;
   }

   if (r.range == eq_zero)
      r.is_integral = true;

   _mesa_hash_table_insert(ht, pack_key(alu, use_type), pack_data(r));
   return r;
}

#undef _______

struct ssa_result_range
nir_analyze_range(struct hash_table *range_ht,
                  const nir_alu_instr *instr, unsigned src)
{
   return analyze_expression(instr, src, range_ht,
                             nir_alu_src_type(instr, src));
}

static uint32_t bitmask(uint32_t size) {
   return size >= 32 ? 0xffffffffu : ((uint32_t)1 << size) - 1u;
}

static uint64_t mul_clamp(uint32_t a, uint32_t b)
{
   if (a != 0 && (a * b) / a != b)
      return (uint64_t)UINT32_MAX + 1;
   else
      return a * b;
}

/* recursively gather at most "buf_size" phi/bcsel sources */
static unsigned
search_phi_bcsel(nir_ssa_scalar scalar, nir_ssa_scalar *buf, unsigned buf_size, struct set *visited)
{
   if (_mesa_set_search(visited, scalar.def))
      return 0;
   _mesa_set_add(visited, scalar.def);

   if (scalar.def->parent_instr->type == nir_instr_type_phi) {
      nir_phi_instr *phi = nir_instr_as_phi(scalar.def->parent_instr);
      unsigned num_sources_left = exec_list_length(&phi->srcs);
      if (buf_size >= num_sources_left) {
         unsigned total_added = 0;
         nir_foreach_phi_src(src, phi) {
            num_sources_left--;
            unsigned added = search_phi_bcsel(
               (nir_ssa_scalar){src->src.ssa, 0}, buf + total_added, buf_size - num_sources_left, visited);
            assert(added <= buf_size);
            buf_size -= added;
            total_added += added;
         }
         return total_added;
      }
   }

   if (nir_ssa_scalar_is_alu(scalar)) {
      nir_op op = nir_ssa_scalar_alu_op(scalar);

      if ((op == nir_op_bcsel || op == nir_op_b32csel) && buf_size >= 2) {
         nir_ssa_scalar src0 = nir_ssa_scalar_chase_alu_src(scalar, 0);
         nir_ssa_scalar src1 = nir_ssa_scalar_chase_alu_src(scalar, 1);

         unsigned added = search_phi_bcsel(src0, buf, buf_size - 1, visited);
         buf_size -= added;
         added += search_phi_bcsel(src1, buf + added, buf_size, visited);
         return added;
      }
   }

   buf[0] = scalar;
   return 1;
}

static nir_variable *
lookup_input(nir_shader *shader, unsigned driver_location)
{
   return nir_find_variable_with_driver_location(shader, nir_var_shader_in,
                                                 driver_location);
}

uint32_t
nir_unsigned_upper_bound(nir_shader *shader, struct hash_table *range_ht,
                         nir_ssa_scalar scalar,
                         const nir_unsigned_upper_bound_config *config)
{
   assert(scalar.def->bit_size <= 32);

   if (nir_ssa_scalar_is_const(scalar))
      return nir_ssa_scalar_as_uint(scalar);

   /* keys can't be 0, so we have to add 1 to the index */
   void *key = (void*)(((uintptr_t)(scalar.def->index + 1) << 4) | scalar.comp);
   struct hash_entry *he = _mesa_hash_table_search(range_ht, key);
   if (he != NULL)
      return (uintptr_t)he->data;

   uint32_t max = bitmask(scalar.def->bit_size);

   if (scalar.def->parent_instr->type == nir_instr_type_intrinsic) {
      uint32_t res = max;
      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(scalar.def->parent_instr);
      switch (intrin->intrinsic) {
      case nir_intrinsic_load_local_invocation_index:
         if (shader->info.cs.local_size_variable) {
            res = config->max_work_group_invocations - 1;
         } else {
            res = (shader->info.cs.local_size[0] *
                   shader->info.cs.local_size[1] *
                   shader->info.cs.local_size[2]) - 1u;
         }
         break;
      case nir_intrinsic_load_local_invocation_id:
         if (shader->info.cs.local_size_variable)
            res = config->max_work_group_size[scalar.comp] - 1u;
         else
            res = shader->info.cs.local_size[scalar.comp] - 1u;
         break;
      case nir_intrinsic_load_work_group_id:
         res = config->max_work_group_count[scalar.comp] - 1u;
         break;
      case nir_intrinsic_load_num_work_groups:
         res = config->max_work_group_count[scalar.comp];
         break;
      case nir_intrinsic_load_global_invocation_id:
         if (shader->info.cs.local_size_variable) {
            res = mul_clamp(config->max_work_group_size[scalar.comp],
                            config->max_work_group_count[scalar.comp]) - 1u;
         } else {
            res = (shader->info.cs.local_size[scalar.comp] *
                   config->max_work_group_count[scalar.comp]) - 1u;
         }
         break;
      case nir_intrinsic_load_subgroup_invocation:
      case nir_intrinsic_first_invocation:
      case nir_intrinsic_mbcnt_amd:
         res = config->max_subgroup_size - 1;
         break;
      case nir_intrinsic_load_subgroup_size:
         res = config->max_subgroup_size;
         break;
      case nir_intrinsic_load_subgroup_id:
      case nir_intrinsic_load_num_subgroups: {
         uint32_t work_group_size = config->max_work_group_invocations;
         if (!shader->info.cs.local_size_variable) {
            work_group_size = shader->info.cs.local_size[0] *
                              shader->info.cs.local_size[1] *
                              shader->info.cs.local_size[2];
         }
         res = (work_group_size + config->min_subgroup_size - 1) / config->min_subgroup_size;
         if (intrin->intrinsic == nir_intrinsic_load_subgroup_id)
            res--;
         break;
      }
      case nir_intrinsic_load_input: {
         if (shader->info.stage == MESA_SHADER_VERTEX && nir_src_is_const(intrin->src[0])) {
            nir_variable *var = lookup_input(shader, nir_intrinsic_base(intrin));
            if (var) {
               int loc = var->data.location - VERT_ATTRIB_GENERIC0;
               if (loc >= 0)
                  res = config->vertex_attrib_max[loc];
            }
         }
         break;
      }
      case nir_intrinsic_reduce:
      case nir_intrinsic_inclusive_scan:
      case nir_intrinsic_exclusive_scan: {
         nir_op op = nir_intrinsic_reduction_op(intrin);
         if (op == nir_op_umin || op == nir_op_umax || op == nir_op_imin || op == nir_op_imax)
            res = nir_unsigned_upper_bound(shader, range_ht, (nir_ssa_scalar){intrin->src[0].ssa, 0}, config);
         break;
      }
      case nir_intrinsic_read_first_invocation:
      case nir_intrinsic_read_invocation:
      case nir_intrinsic_shuffle:
      case nir_intrinsic_shuffle_xor:
      case nir_intrinsic_shuffle_up:
      case nir_intrinsic_shuffle_down:
      case nir_intrinsic_quad_broadcast:
      case nir_intrinsic_quad_swap_horizontal:
      case nir_intrinsic_quad_swap_vertical:
      case nir_intrinsic_quad_swap_diagonal:
      case nir_intrinsic_quad_swizzle_amd:
      case nir_intrinsic_masked_swizzle_amd:
         res = nir_unsigned_upper_bound(shader, range_ht, (nir_ssa_scalar){intrin->src[0].ssa, 0}, config);
         break;
      case nir_intrinsic_write_invocation_amd: {
         uint32_t src0 = nir_unsigned_upper_bound(shader, range_ht, (nir_ssa_scalar){intrin->src[0].ssa, 0}, config);
         uint32_t src1 = nir_unsigned_upper_bound(shader, range_ht, (nir_ssa_scalar){intrin->src[1].ssa, 0}, config);
         res = MAX2(src0, src1);
         break;
      }
      default:
         break;
      }
      if (res != max)
         _mesa_hash_table_insert(range_ht, key, (void*)(uintptr_t)res);
      return res;
   }

   if (scalar.def->parent_instr->type == nir_instr_type_phi) {
      bool cyclic = false;
      nir_foreach_phi_src(src, nir_instr_as_phi(scalar.def->parent_instr)) {
         if (nir_block_dominates(scalar.def->parent_instr->block, src->pred)) {
            cyclic = true;
            break;
         }
      }

      uint32_t res = 0;
      if (cyclic) {
         _mesa_hash_table_insert(range_ht, key, (void*)(uintptr_t)max);

         struct set *visited = _mesa_pointer_set_create(NULL);
         nir_ssa_scalar defs[64];
         unsigned def_count = search_phi_bcsel(scalar, defs, 64, visited);
         _mesa_set_destroy(visited, NULL);

         for (unsigned i = 0; i < def_count; i++)
            res = MAX2(res, nir_unsigned_upper_bound(shader, range_ht, defs[i], config));
      } else {
         nir_foreach_phi_src(src, nir_instr_as_phi(scalar.def->parent_instr)) {
            res = MAX2(res, nir_unsigned_upper_bound(
               shader, range_ht, (nir_ssa_scalar){src->src.ssa, 0}, config));
         }
      }

      _mesa_hash_table_insert(range_ht, key, (void*)(uintptr_t)res);
      return res;
   }

   if (nir_ssa_scalar_is_alu(scalar)) {
      nir_op op = nir_ssa_scalar_alu_op(scalar);

      switch (op) {
      case nir_op_umin:
      case nir_op_imin:
      case nir_op_imax:
      case nir_op_umax:
      case nir_op_iand:
      case nir_op_ior:
      case nir_op_ixor:
      case nir_op_ishl:
      case nir_op_imul:
      case nir_op_ushr:
      case nir_op_ishr:
      case nir_op_iadd:
      case nir_op_umod:
      case nir_op_udiv:
      case nir_op_bcsel:
      case nir_op_b32csel:
      case nir_op_ubfe:
      case nir_op_bfm:
      case nir_op_f2u32:
      case nir_op_fmul:
         break;
      default:
         return max;
      }

      uint32_t src0 = nir_unsigned_upper_bound(shader, range_ht, nir_ssa_scalar_chase_alu_src(scalar, 0), config);
      uint32_t src1 = max, src2 = max;
      if (nir_op_infos[op].num_inputs > 1)
         src1 = nir_unsigned_upper_bound(shader, range_ht, nir_ssa_scalar_chase_alu_src(scalar, 1), config);
      if (nir_op_infos[op].num_inputs > 2)
         src2 = nir_unsigned_upper_bound(shader, range_ht, nir_ssa_scalar_chase_alu_src(scalar, 2), config);

      uint32_t res = max;
      switch (op) {
      case nir_op_umin:
         res = src0 < src1 ? src0 : src1;
         break;
      case nir_op_imin:
      case nir_op_imax:
      case nir_op_umax:
         res = src0 > src1 ? src0 : src1;
         break;
      case nir_op_iand:
         res = bitmask(util_last_bit64(src0)) & bitmask(util_last_bit64(src1));
         break;
      case nir_op_ior:
      case nir_op_ixor:
         res = bitmask(util_last_bit64(src0)) | bitmask(util_last_bit64(src1));
         break;
      case nir_op_ishl:
         if (util_last_bit64(src0) + src1 > scalar.def->bit_size)
            res = max; /* overflow */
         else
            res = src0 << MIN2(src1, scalar.def->bit_size - 1u);
         break;
      case nir_op_imul:
         if (src0 != 0 && (src0 * src1) / src0 != src1)
            res = max;
         else
            res = src0 * src1;
         break;
      case nir_op_ushr: {
         nir_ssa_scalar src1_scalar = nir_ssa_scalar_chase_alu_src(scalar, 1);
         if (nir_ssa_scalar_is_const(src1_scalar))
            res = src0 >> nir_ssa_scalar_as_uint(src1_scalar);
         else
            res = src0;
         break;
      }
      case nir_op_ishr: {
         nir_ssa_scalar src1_scalar = nir_ssa_scalar_chase_alu_src(scalar, 1);
         if (src0 <= 2147483647 && nir_ssa_scalar_is_const(src1_scalar))
            res = src0 >> nir_ssa_scalar_as_uint(src1_scalar);
         else
            res = src0;
         break;
      }
      case nir_op_iadd:
         if (src0 + src1 < src0)
            res = max; /* overflow */
         else
            res = src0 + src1;
         break;
      case nir_op_umod:
         res = src1 ? src1 - 1 : 0;
         break;
      case nir_op_udiv: {
         nir_ssa_scalar src1_scalar = nir_ssa_scalar_chase_alu_src(scalar, 1);
         if (nir_ssa_scalar_is_const(src1_scalar))
            res = nir_ssa_scalar_as_uint(src1_scalar) ? src0 / nir_ssa_scalar_as_uint(src1_scalar) : 0;
         else
            res = src0;
         break;
      }
      case nir_op_bcsel:
      case nir_op_b32csel:
         res = src1 > src2 ? src1 : src2;
         break;
      case nir_op_ubfe:
         res = bitmask(MIN2(src2, scalar.def->bit_size));
         break;
      case nir_op_bfm: {
         nir_ssa_scalar src1_scalar = nir_ssa_scalar_chase_alu_src(scalar, 1);
         if (nir_ssa_scalar_is_const(src1_scalar)) {
            src0 = MIN2(src0, 31);
            src1 = nir_ssa_scalar_as_uint(src1_scalar) & 0x1fu;
            res = bitmask(src0) << src1;
         } else {
            src0 = MIN2(src0, 31);
            src1 = MIN2(src1, 31);
            res = bitmask(MIN2(src0 + src1, 32));
         }
         break;
      }
      /* limited floating-point support for f2u32(fmul(load_input(), <constant>)) */
      case nir_op_f2u32:
         /* infinity/NaN starts at 0x7f800000u, negative numbers at 0x80000000 */
         if (src0 < 0x7f800000u) {
            float val;
            memcpy(&val, &src0, 4);
            res = (uint32_t)val;
         }
         break;
      case nir_op_fmul:
         /* infinity/NaN starts at 0x7f800000u, negative numbers at 0x80000000 */
         if (src0 < 0x7f800000u && src1 < 0x7f800000u) {
            float src0_f, src1_f;
            memcpy(&src0_f, &src0, 4);
            memcpy(&src1_f, &src1, 4);
            /* not a proper rounding-up multiplication, but should be good enough */
            float max_f = ceilf(src0_f) * ceilf(src1_f);
            memcpy(&res, &max_f, 4);
         }
         break;
      default:
         res = max;
         break;
      }
      _mesa_hash_table_insert(range_ht, key, (void*)(uintptr_t)res);
      return res;
   }

   return max;
}

bool
nir_addition_might_overflow(nir_shader *shader, struct hash_table *range_ht,
                            nir_ssa_scalar ssa, unsigned const_val,
                            const nir_unsigned_upper_bound_config *config)
{
   if (nir_ssa_scalar_is_alu(ssa)) {
      nir_op alu_op = nir_ssa_scalar_alu_op(ssa);

      /* iadd(imul(a, #b), #c) */
      if (alu_op == nir_op_imul || alu_op == nir_op_ishl) {
         nir_ssa_scalar mul_src0 = nir_ssa_scalar_chase_alu_src(ssa, 0);
         nir_ssa_scalar mul_src1 = nir_ssa_scalar_chase_alu_src(ssa, 1);
         uint32_t stride = 1;
         if (nir_ssa_scalar_is_const(mul_src0))
            stride = nir_ssa_scalar_as_uint(mul_src0);
         else if (nir_ssa_scalar_is_const(mul_src1))
            stride = nir_ssa_scalar_as_uint(mul_src1);

         if (alu_op == nir_op_ishl)
            stride = 1u << (stride % 32u);

         if (!stride || const_val <= UINT32_MAX - (UINT32_MAX / stride * stride))
            return false;
      }

      /* iadd(iand(a, #b), #c) */
      if (alu_op == nir_op_iand) {
         nir_ssa_scalar and_src0 = nir_ssa_scalar_chase_alu_src(ssa, 0);
         nir_ssa_scalar and_src1 = nir_ssa_scalar_chase_alu_src(ssa, 1);
         uint32_t mask = 0xffffffff;
         if (nir_ssa_scalar_is_const(and_src0))
            mask = nir_ssa_scalar_as_uint(and_src0);
         else if (nir_ssa_scalar_is_const(and_src1))
            mask = nir_ssa_scalar_as_uint(and_src1);
         if (mask == 0 || const_val < (1u << (ffs(mask) - 1)))
            return false;
      }
   }

   uint32_t ub = nir_unsigned_upper_bound(shader, range_ht, ssa, config);
   return const_val + ub < const_val;
}

