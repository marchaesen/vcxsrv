/*
 * Copyright (C) 2020 Collabora, Ltd.
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
 * Authors (Collabora):
 *   Alyssa Rosenzweig <alyssa.rosenzweig@collabora.com>
 */

/* Fuses f2f16 modifiers into loads */

#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"
#include "panfrost/util/pan_ir.h"

bool nir_fuse_io_16(nir_shader *shader);

static bool
nir_src_is_f2fmp(nir_src *use)
{
   nir_instr *parent = use->parent_instr;

   if (parent->type != nir_instr_type_alu)
      return false;

   nir_alu_instr *alu = nir_instr_as_alu(parent);
   return (alu->op == nir_op_f2fmp);
}

bool
nir_fuse_io_16(nir_shader *shader)
{
   bool progress = false;

   nir_foreach_function(function, shader) {
      if (!function->impl) continue;

      nir_builder b;
      nir_builder_init(&b, function->impl);

      nir_foreach_block(block, function->impl) {
         nir_foreach_instr_safe(instr, block) {
            if (instr->type != nir_instr_type_intrinsic) continue;

            nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

            if (intr->intrinsic != nir_intrinsic_load_interpolated_input)
                    continue;

            if (nir_dest_bit_size(intr->dest) != 32)
                    continue;

            /* We swizzle at a 32-bit level so need a multiple of 2. We could
             * do a bit better and handle even components though */
            if (nir_intrinsic_component(intr))
               continue;

            if (!intr->dest.is_ssa)
               continue;

            if (!list_is_empty(&intr->dest.ssa.if_uses))
               return false;

            bool valid = true;

            nir_foreach_use(src, &intr->dest.ssa)
               valid &= nir_src_is_f2fmp(src);

            if (!valid)
               continue;

            intr->dest.ssa.bit_size = 16;

            nir_builder b;
            nir_builder_init(&b, function->impl);
            b.cursor = nir_after_instr(instr);

            /* The f2f32(f2fmp(x)) will cancel by opt_algebraic */
            nir_ssa_def *conv = nir_f2f32(&b, &intr->dest.ssa);
            nir_ssa_def_rewrite_uses_after(&intr->dest.ssa, nir_src_for_ssa(conv), conv->parent_instr);

            progress |= true;
         }
      }

      nir_metadata_preserve(function->impl, nir_metadata_block_index | nir_metadata_dominance);

   }

   return progress;
}
