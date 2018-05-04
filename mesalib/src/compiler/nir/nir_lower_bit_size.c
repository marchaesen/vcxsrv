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

#include "nir_builder.h"

/**
 * Some ALU operations may not be supported in hardware in specific bit-sizes.
 * This pass allows implementations to selectively lower such operations to
 * a bit-size that is supported natively and then converts the result back to
 * the original bit-size.
 */
static nir_ssa_def *
convert_to_bit_size(nir_builder *bld,
                    nir_ssa_def *src,
                    nir_alu_type type,
                    unsigned bit_size)
{
   nir_alu_type base_type = nir_alu_type_get_base_type(type);
   nir_alu_type lowered_type = bit_size | base_type;

   nir_op opcode =
      nir_type_conversion_op(type, lowered_type, nir_rounding_mode_undef);

   return nir_build_alu(bld, opcode, src, NULL, NULL, NULL);
}

static void
lower_instr(nir_builder *bld, nir_alu_instr *alu, unsigned bit_size)
{
   const nir_op op = alu->op;

   bld->cursor = nir_before_instr(&alu->instr);

   /* Convert each source to the requested bit-size */
   nir_ssa_def *srcs[4] = { NULL, NULL, NULL, NULL };
   for (unsigned i = 0; i < nir_op_infos[op].num_inputs; i++) {
      nir_ssa_def *src = nir_ssa_for_alu_src(bld, alu, i);

      nir_alu_type type = nir_op_infos[op].input_types[i];
      if (nir_alu_type_get_type_size(type) == 0)
         srcs[i] = convert_to_bit_size(bld, src, type, bit_size);
      else
         srcs[i] = src;
   }

   /* Emit the lowered ALU instruction */
   nir_ssa_def *lowered_dst =
      nir_build_alu(bld, op, srcs[0], srcs[1], srcs[2], srcs[3]);

   /* Convert result back to the original bit-size */
   unsigned dst_bit_size = alu->dest.dest.ssa.bit_size;
   nir_alu_type type = nir_op_infos[op].output_type;
   nir_ssa_def *dst = convert_to_bit_size(bld, lowered_dst, type, dst_bit_size);
   nir_ssa_def_rewrite_uses(&alu->dest.dest.ssa, nir_src_for_ssa(dst));
}

static bool
lower_impl(nir_function_impl *impl,
           nir_lower_bit_size_callback callback,
           void *callback_data)
{
   nir_builder b;
   nir_builder_init(&b, impl);
   bool progress = false;

   nir_foreach_block(block, impl) {
      nir_foreach_instr_safe(instr, block) {
         if (instr->type != nir_instr_type_alu)
            continue;

         nir_alu_instr *alu = nir_instr_as_alu(instr);
         assert(alu->dest.dest.is_ssa);

         unsigned lower_bit_size = callback(alu, callback_data);
         if (lower_bit_size == 0)
            continue;

         assert(lower_bit_size != alu->dest.dest.ssa.bit_size);

         lower_instr(&b, alu, lower_bit_size);
         progress = true;
      }
   }

   if (progress) {
      nir_metadata_preserve(impl, nir_metadata_block_index |
                                  nir_metadata_dominance);
   }

   return progress;
}

bool
nir_lower_bit_size(nir_shader *shader,
                   nir_lower_bit_size_callback callback,
                   void *callback_data)
{
   bool progress = false;

   nir_foreach_function(function, shader) {
      if (function->impl)
         progress |= lower_impl(function->impl, callback, callback_data);
   }

   return progress;
}
