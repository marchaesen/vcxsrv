/*
 * Copyright © 2014 Intel Corporation
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
 * Authors:
 *    Jason Ekstrand (jason@jlekstrand.net)
 *
 */

#ifndef _NIR_SEARCH_
#define _NIR_SEARCH_

#include "nir.h"
#include "nir_worklist.h"
#include "util/u_dynarray.h"

#define NIR_SEARCH_MAX_VARIABLES 16

struct nir_builder;

typedef enum {
   nir_search_value_expression,
   nir_search_value_variable,
   nir_search_value_constant,
} nir_search_value_type;

typedef struct {
   nir_search_value_type type;

   /**
    * Bit size of the value. It is interpreted as follows:
    *
    * For a search expression:
    * - If bit_size > 0, then the value only matches an SSA value with the
    *   given bit size.
    * - If bit_size <= 0, then the value matches any size SSA value.
    *
    * For a replace expression:
    * - If bit_size > 0, then the value is constructed with the given bit size.
    * - If bit_size == 0, then the value is constructed with the same bit size
    *   as the search value.
    * - If bit_size < 0, then the value is constructed with the same bit size
    *   as variable (-bit_size - 1).
    */
   int bit_size;
} nir_search_value;

typedef struct {
   nir_search_value value;

   /** The variable index;  Must be less than NIR_SEARCH_MAX_VARIABLES */
   unsigned variable;

   /** Indicates that the given variable must be a constant
    *
    * This is only allowed in search expressions and indicates that the
    * given variable is only allowed to match constant values.
    */
   bool is_constant;

   /** Indicates that the given variable must have a certain type
    *
    * This is only allowed in search expressions and indicates that the
    * given variable is only allowed to match values that come from an ALU
    * instruction with the given output type.  A type of nir_type_void
    * means it can match any type.
    *
    * Note: A variable that is both constant and has a non-void type will
    * never match anything.
    */
   nir_alu_type type;

   /** Optional condition fxn ptr
    *
    * This is only allowed in search expressions, and allows additional
    * constraints to be placed on the match.  Typically used for 'is_constant'
    * variables to require, for example, power-of-two in order for the search
    * to match.
    */
   bool (*cond)(struct hash_table *range_ht, nir_alu_instr *instr, unsigned src,
                unsigned num_components, const uint8_t *swizzle);

   /** Swizzle (for replace only) */
   uint8_t swizzle[NIR_MAX_VEC_COMPONENTS];
} nir_search_variable;

typedef struct {
   nir_search_value value;

   nir_alu_type type;

   union {
      uint64_t u;
      int64_t i;
      double d;
   } data;
} nir_search_constant;

enum nir_search_op {
   nir_search_op_i2f = nir_last_opcode + 1,
   nir_search_op_u2f,
   nir_search_op_f2f,
   nir_search_op_f2u,
   nir_search_op_f2i,
   nir_search_op_u2u,
   nir_search_op_i2i,
   nir_search_op_b2f,
   nir_search_op_b2i,
   nir_search_op_i2b,
   nir_search_op_f2b,
   nir_num_search_ops,
};

uint16_t nir_search_op_for_nir_op(nir_op op);

typedef struct {
   nir_search_value value;

   /* When set on a search expression, the expression will only match an SSA
    * value that does *not* have the exact bit set.  If unset, the exact bit
    * on the SSA value is ignored.
    */
   bool inexact;

   /** In a replacement, requests that the instruction be marked exact. */
   bool exact;

   /* Commutative expression index.  This is assigned by opt_algebraic.py when
    * search structures are constructed and is a unique (to this structure)
    * index within the commutative operation bitfield used for searching for
    * all combinations of expressions containing commutative operations.
    */
   int8_t comm_expr_idx;

   /* Number of commutative expressions in this expression including this one
    * (if it is commutative).
    */
   uint8_t comm_exprs;

   /* One of nir_op or nir_search_op */
   uint16_t opcode;
   const nir_search_value *srcs[4];

   /** Optional condition fxn ptr
    *
    * This allows additional constraints on expression matching, it is
    * typically used to match an expressions uses such as the number of times
    * the expression is used, and whether its used by an if.
    */
   bool (*cond)(nir_alu_instr *instr);
} nir_search_expression;

struct per_op_table {
   const uint16_t *filter;
   unsigned num_filtered_states;
   const uint16_t *table;
};

struct transform {
   const nir_search_expression *search;
   const nir_search_value *replace;
   unsigned condition_offset;
};

/* Note: these must match the start states created in
 * TreeAutomaton._build_table()
 */

/* WILDCARD_STATE = 0 is set by zeroing the state array */
static const uint16_t CONST_STATE = 1;

NIR_DEFINE_CAST(nir_search_value_as_variable, nir_search_value,
                nir_search_variable, value,
                type, nir_search_value_variable)
NIR_DEFINE_CAST(nir_search_value_as_constant, nir_search_value,
                nir_search_constant, value,
                type, nir_search_value_constant)
NIR_DEFINE_CAST(nir_search_value_as_expression, nir_search_value,
                nir_search_expression, value,
                type, nir_search_value_expression)

nir_ssa_def *
nir_replace_instr(struct nir_builder *b, nir_alu_instr *instr,
                  struct hash_table *range_ht,
                  struct util_dynarray *states,
                  const struct per_op_table *pass_op_table,
                  const nir_search_expression *search,
                  const nir_search_value *replace,
                  nir_instr_worklist *algebraic_worklist);
bool
nir_algebraic_impl(nir_function_impl *impl,
                   const bool *condition_flags,
                   const struct transform **transforms,
                   const uint16_t *transform_counts,
                   const struct per_op_table *pass_op_table);

#endif /* _NIR_SEARCH_ */
