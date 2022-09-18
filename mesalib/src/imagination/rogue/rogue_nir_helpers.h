/*
 * Copyright © 2022 Imagination Technologies Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef ROGUE_NIR_HELPERS_H
#define ROGUE_NIR_HELPERS_H

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "nir/nir.h"
#include "util/bitscan.h"

/**
 * \file rogue_nir.c
 *
 * \brief Contains various NIR helper functions.
 */

static inline unsigned nir_alu_dest_regindex(const nir_alu_instr *alu)
{
   assert(!alu->dest.dest.is_ssa);

   return alu->dest.dest.reg.reg->index;
}

static inline unsigned nir_alu_dest_comp(const nir_alu_instr *alu)
{
   assert(!alu->dest.dest.is_ssa);
   assert(util_is_power_of_two_nonzero(alu->dest.write_mask));

   return ffs(alu->dest.write_mask) - 1;
}

static inline unsigned nir_alu_src_regindex(const nir_alu_instr *alu,
                                            size_t src)
{
   assert(src < nir_op_infos[alu->op].num_inputs);
   assert(!alu->src[src].src.is_ssa);

   return alu->src[src].src.reg.reg->index;
}

static inline uint32_t nir_alu_src_const(const nir_alu_instr *alu, size_t src)
{
   assert(src < nir_op_infos[alu->op].num_inputs);
   assert(alu->src[src].src.is_ssa);

   nir_const_value *const_value = nir_src_as_const_value(alu->src[src].src);

   return nir_const_value_as_uint(*const_value, 32);
}

static inline bool nir_alu_src_is_const(const nir_alu_instr *alu, size_t src)
{
   assert(src < nir_op_infos[alu->op].num_inputs);

   if (!alu->src[src].src.is_ssa)
      return false;

   assert(alu->src[src].src.ssa->parent_instr);

   return (alu->src[src].src.ssa->parent_instr->type ==
           nir_instr_type_load_const);
}

static inline unsigned nir_intr_dest_regindex(const nir_intrinsic_instr *intr)
{
   assert(!intr->dest.is_ssa);

   return intr->dest.reg.reg->index;
}

static inline unsigned nir_intr_src_regindex(const nir_intrinsic_instr *intr,
                                             size_t src)
{
   assert(src < nir_intrinsic_infos[intr->intrinsic].num_srcs);
   assert(!intr->src[src].is_ssa);

   return intr->src[src].reg.reg->index;
}

static inline uint32_t nir_intr_src_const(const nir_intrinsic_instr *intr,
                                          size_t src)
{
   assert(src < nir_intrinsic_infos[intr->intrinsic].num_srcs);
   assert(intr->src[src].is_ssa);

   nir_const_value *const_value = nir_src_as_const_value(intr->src[src]);

   return nir_const_value_as_uint(*const_value, 32);
}

static inline uint32_t nir_intr_src_comp_const(const nir_intrinsic_instr *intr,
                                               size_t src,
                                               size_t comp)
{
   assert(src < nir_intrinsic_infos[intr->intrinsic].num_srcs);
   assert(intr->src[src].is_ssa);
   assert(comp < nir_src_num_components(intr->src[src]));

   return nir_src_comp_as_uint(intr->src[src], comp);
}

static inline bool nir_intr_src_is_const(const nir_intrinsic_instr *intr,
                                         size_t src)
{
   assert(src < nir_intrinsic_infos[intr->intrinsic].num_srcs);

   if (!intr->src[src].is_ssa)
      return false;

   assert(intr->src[src].ssa->parent_instr);

   return (intr->src[src].ssa->parent_instr->type == nir_instr_type_load_const);
}

static inline size_t nir_count_variables_with_modes(const nir_shader *nir,
                                                    nir_variable_mode mode)
{
   size_t count = 0;

   nir_foreach_variable_with_modes (var, nir, mode)
      ++count;

   return count;
}

#endif /* ROGUE_NIR_HELPERS_H */
