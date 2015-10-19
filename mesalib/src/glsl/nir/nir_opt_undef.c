/*
 * Copyright © 2015 Broadcom
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

#include "nir.h"

/** @file nir_opt_undef.c
 *
 * Handles optimization of operations involving ssa_undef.  For now, we just
 * make sure that csels between undef and some other value just give the other
 * value (on the assumption that the condition's going to be choosing the
 * defined value).  This reduces work after if flattening when each side of
 * the if is defining a variable.
 *
 * Some day, we may find some use for making other operations consuming an
 * undef arg output undef, but I don't know of any cases currently.
 */

static bool
opt_undef_alu(nir_alu_instr *instr)
{
   if (instr->op != nir_op_bcsel && instr->op != nir_op_fcsel)
      return false;

   assert(instr->dest.dest.is_ssa);

   for (int i = 1; i <= 2; i++) {
      if (!instr->src[i].src.is_ssa)
         continue;

      nir_instr *parent = instr->src[i].src.ssa->parent_instr;
      if (parent->type != nir_instr_type_ssa_undef)
         continue;

      /* We can't just use nir_alu_src_copy, because we need the def/use
       * updated.
       */
      nir_instr_rewrite_src(&instr->instr, &instr->src[0].src,
                            instr->src[i == 1 ? 2 : 1].src);
      nir_alu_src_copy(&instr->src[0], &instr->src[i == 1 ? 2 : 1],
                       ralloc_parent(instr));

      nir_src empty_src;
      memset(&empty_src, 0, sizeof(empty_src));
      nir_instr_rewrite_src(&instr->instr, &instr->src[1].src, empty_src);
      nir_instr_rewrite_src(&instr->instr, &instr->src[2].src, empty_src);
      instr->op = nir_op_imov;

      return true;
   }

   return false;
}

static bool
opt_undef_block(nir_block *block, void *data)
{
   bool *progress = data;

   nir_foreach_instr_safe(block, instr) {
      if (instr->type == nir_instr_type_alu)
         if (opt_undef_alu(nir_instr_as_alu(instr)))
             (*progress) = true;
   }

   return true;
}

bool
nir_opt_undef(nir_shader *shader)
{
   bool progress = false;

   nir_foreach_overload(shader, overload) {
      if (overload->impl) {
         nir_foreach_block(overload->impl, opt_undef_block, &progress);
         if (progress)
            nir_metadata_preserve(overload->impl,
                                  nir_metadata_block_index |
                                  nir_metadata_dominance);
      }
   }

   return progress;
}
