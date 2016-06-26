/*
 * Copyright © 2015 Intel Corporation
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
 */

#include "nir.h"
#include "nir_builder.h"

/*
 * lowers:
 *
 * packDouble2x32(foo) -> packDouble2x32Split(foo.x, foo.y)
 * unpackDouble2x32(foo) -> vec2(unpackDouble2x32_x(foo), unpackDouble2x32_y(foo))
 */

static nir_ssa_def *
lower_pack_double(nir_builder *b, nir_ssa_def *src)
{
   return nir_pack_double_2x32_split(b, nir_channel(b, src, 0),
                                        nir_channel(b, src, 1));
}

static nir_ssa_def *
lower_unpack_double(nir_builder *b, nir_ssa_def *src)
{
   return nir_vec2(b, nir_unpack_double_2x32_split_x(b, src),
                      nir_unpack_double_2x32_split_y(b, src));
}

static void
lower_double_pack_impl(nir_function_impl *impl)
{
   nir_builder b;
   nir_builder_init(&b, impl);

   nir_foreach_block(block, impl) {
      nir_foreach_instr_safe(instr, block) {
         if (instr->type != nir_instr_type_alu)
            continue;

         nir_alu_instr *alu_instr = (nir_alu_instr *) instr;

         if (alu_instr->op != nir_op_pack_double_2x32 &&
             alu_instr->op != nir_op_unpack_double_2x32)
            continue;

         b.cursor = nir_before_instr(&alu_instr->instr);

         nir_ssa_def *src = nir_ssa_for_alu_src(&b, alu_instr, 0);
         nir_ssa_def *dest =
            alu_instr->op == nir_op_pack_double_2x32 ?
            lower_pack_double(&b, src) :
            lower_unpack_double(&b, src);

         nir_ssa_def_rewrite_uses(&alu_instr->dest.dest.ssa, nir_src_for_ssa(dest));
         nir_instr_remove(&alu_instr->instr);
      }
   }
}

void
nir_lower_double_pack(nir_shader *shader)
{
   nir_foreach_function(function, shader) {
      if (function->impl)
         lower_double_pack_impl(function->impl);
   }
}

