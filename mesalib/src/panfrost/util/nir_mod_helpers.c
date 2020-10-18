/*
 * Copyright (C) 2020 Collabora, Ltd.
 * Copyright (C) 2014 Intel Corporation
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
 *    Alyssa Rosenzweig <alyssa@collabora.com>
 *    Jason Ekstrand (jason@jlekstrand.net)
 *
 */

#include "nir.h"
#include "pan_ir.h"

/* Check if a given ALU source is the result of a particular componentwise 1-op
 * ALU source (principally fneg or fabs). If so, return true and rewrite the
 * source to be the argument, respecting swizzles as needed. If not (or it
 * cannot be proven), return false and leave the source untouched.
*/

bool
pan_has_source_mod(nir_alu_src *src, nir_op op)
{
   if (!src->src.is_ssa || src->src.ssa->parent_instr->type != nir_instr_type_alu)
      return false;

   nir_alu_instr *alu = nir_instr_as_alu(src->src.ssa->parent_instr);

   if (alu->op != op)
      return false;

   /* This only works for unary ops */
   assert(nir_op_infos[op].num_inputs == 1);

   /* If the copied source is not SSA, moving it might not be valid */
   if (!alu->src[0].src.is_ssa)
      return false;

   /* Okay - we've found the modifier we wanted. Let's construct the new ALU
    * src. In a scalar world, this is just psrc, but for vector archs we need
    * to respect the swizzle, so we compose. 
    */

   nir_alu_src nsrc = {
      .src = alu->src[0].src,
   };

   for (unsigned i = 0; i < NIR_MAX_VEC_COMPONENTS; ++i) {
      /* (a o b)(i) = a(b(i)) ... swizzle composition is intense. */
      nsrc.swizzle[i] = alu->src[0].swizzle[src->swizzle[i]];
   }

   *src = nsrc;
   return true;
}

/* Check if a given instruction's result will be fed into a
 * componentwise 1-op ALU instruction (principally fsat without
 * swizzles). If so, return true and rewrite the destination. The
 * backend will need to track the new destinations to avoid
 * incorrect double-emits. */

bool
pan_has_dest_mod(nir_dest **odest, nir_op op)
{
   /* This only works for unary ops */
   assert(nir_op_infos[op].num_inputs == 1);

   /* If not SSA, this might not be legal */
   nir_dest *dest = *odest;
   if (!dest->is_ssa)
      return false;

   /* Check the uses. We want a single use, with the op `op` */
   if (!list_is_empty(&dest->ssa.if_uses))
      return false;

   if (!list_is_singular(&dest->ssa.uses))
      return false;

   nir_src *use = list_first_entry(&dest->ssa.uses, nir_src, use_link);
   nir_instr *parent = use->parent_instr;

   /* Check if the op is `op` */
   if (parent->type != nir_instr_type_alu)
      return false;

   nir_alu_instr *alu = nir_instr_as_alu(parent);
   if (alu->op != op)
      return false;

   /* We can't do expansions without a move in the middle */
   unsigned nr_components = nir_dest_num_components(alu->dest.dest);

   if (nir_dest_num_components(*dest) != nr_components)
      return false;

   /* We don't handle swizzles here, so check for the identity */
   for (unsigned i = 0; i < nr_components; ++i) {
      if (alu->src[0].swizzle[i] != i)
         return false;
   }

   if (!alu->dest.dest.is_ssa)
      return false;

   /* Otherwise, we're good */
   *odest = &alu->dest.dest;
   return true;
}
