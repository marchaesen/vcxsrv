/*
 * Copyright © 2020 Intel Corporation
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
#include "nir_builder.h"
#include "nir_conversion_builder.h"

static bool
try_simplify_convert_intrin(nir_intrinsic_instr *conv)
{
   bool progress = false;

   nir_alu_type src_type = nir_intrinsic_src_type(conv);
   nir_alu_type dest_type = nir_intrinsic_dest_type(conv);

   nir_rounding_mode rounding = nir_intrinsic_rounding_mode(conv);
   nir_rounding_mode simple_rounding =
      nir_simplify_conversion_rounding(src_type, dest_type, rounding);
   if (rounding != simple_rounding) {
      nir_intrinsic_set_rounding_mode(conv, simple_rounding);
      progress = true;
   }

   if (nir_intrinsic_saturate(conv) &&
       nir_alu_type_range_contains_type_range(dest_type, src_type)) {
      nir_intrinsic_set_saturate(conv, false);
      progress = true;
   }

   return progress;
}

static bool
lower_convert_alu_types_instr(nir_builder *b, nir_intrinsic_instr *conv, void *data)
{
   bool (*cb)(nir_intrinsic_instr *) = data;
   if (conv->intrinsic != nir_intrinsic_convert_alu_types || (cb && !cb(conv)))
      return false;

   b->cursor = nir_instr_remove(&conv->instr);
   nir_def *val =
      nir_convert_with_rounding(b, conv->src[0].ssa,
                                nir_intrinsic_src_type(conv),
                                nir_intrinsic_dest_type(conv),
                                nir_intrinsic_rounding_mode(conv),
                                nir_intrinsic_saturate(conv));
   nir_def_rewrite_uses(&conv->def, val);
   return true;
}

static bool
opt_simplify(nir_builder *b, nir_intrinsic_instr *intr, void *_)
{
   if (intr->intrinsic != nir_intrinsic_convert_alu_types)
      return false;

   bool progress = try_simplify_convert_intrin(intr);

   if (nir_intrinsic_rounding_mode(intr) == nir_rounding_mode_undef &&
       !nir_intrinsic_saturate(intr)) {
      lower_convert_alu_types_instr(b, intr, NULL);
      progress = true;
   }

   return progress;
}

bool
nir_opt_simplify_convert_alu_types(nir_shader *shader)
{
   return nir_shader_intrinsics_pass(shader, opt_simplify,
                                     nir_metadata_control_flow, NULL);
}

bool
nir_lower_convert_alu_types(nir_shader *shader,
                            bool (*should_lower)(nir_intrinsic_instr *))
{
   return nir_shader_intrinsics_pass(shader, lower_convert_alu_types_instr,
                                     nir_metadata_control_flow, should_lower);
}

static bool
is_constant(nir_intrinsic_instr *conv)
{
   assert(conv->intrinsic == nir_intrinsic_convert_alu_types);
   return nir_src_is_const(conv->src[0]);
}

bool
nir_lower_constant_convert_alu_types(nir_shader *shader)
{
   return nir_lower_convert_alu_types(shader, is_constant);
}

static bool
is_alu_conversion(const nir_instr *instr, UNUSED const void *_data)
{
   return instr->type == nir_instr_type_alu &&
          nir_op_infos[nir_instr_as_alu(instr)->op].is_conversion;
}

static nir_def *
lower_alu_conversion(nir_builder *b, nir_instr *instr, UNUSED void *_data)
{
   nir_alu_instr *alu = nir_instr_as_alu(instr);
   nir_def *src = nir_ssa_for_alu_src(b, alu, 0);
   nir_alu_type src_type = nir_op_infos[alu->op].input_types[0] | src->bit_size;
   nir_alu_type dst_type = nir_op_infos[alu->op].output_type;
   return nir_convert_alu_types(b, alu->def.bit_size, src,
                                .src_type = src_type, .dest_type = dst_type,
                                .rounding_mode = nir_rounding_mode_undef,
                                .saturate = false);
}

bool
nir_lower_alu_conversion_to_intrinsic(nir_shader *shader)
{
   return nir_shader_lower_instructions(shader, is_alu_conversion,
                                        lower_alu_conversion, NULL);
}
