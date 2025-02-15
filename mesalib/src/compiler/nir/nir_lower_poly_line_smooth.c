/*
 * Copyright © 2022 Advanced Micro Devices, Inc.
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

/**
 * This NIR lowers pass for polygon and line smoothing by modifying the alpha
 * value of the first fragment output using the sample coverage mask.
 */

static bool
lower_polylinesmooth(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   unsigned *num_smooth_aa_sample = data;

   if (intr->intrinsic != nir_intrinsic_store_output)
      return false;

   int location = nir_intrinsic_io_semantics(intr).location;
   int alpha_comp = 3 - nir_intrinsic_component(intr);
   if ((location != FRAG_RESULT_COLOR && location != FRAG_RESULT_DATA0) ||
       nir_alu_type_get_base_type(nir_intrinsic_src_type(intr)) != nir_type_float ||
       !(nir_intrinsic_write_mask(intr) & BITFIELD_BIT(alpha_comp)))
      return false;

   b->cursor = nir_before_instr(&intr->instr);

   nir_def *coverage = nir_load_sample_mask_in(b);

   /* coverage = (coverage) / num_smooth_aa_sample */
   coverage = nir_bit_count(b, coverage);
   coverage = nir_u2fN(b, coverage, intr->src[0].ssa->bit_size);
   coverage = nir_fmul_imm(b, coverage, 1.0 / *num_smooth_aa_sample);

   nir_def *smooth_enabled = nir_load_poly_line_smooth_enabled(b);
   nir_def *alpha = nir_channel(b, intr->src[0].ssa, alpha_comp);
   nir_def *smooth_alpha = nir_fmul(b, alpha, coverage);
   nir_def *new_alpha = nir_bcsel(b, smooth_enabled, smooth_alpha, alpha);

   nir_def *new_src = nir_vector_insert_imm(b, intr->src[0].ssa, new_alpha, alpha_comp);

   nir_src_rewrite(&intr->src[0], new_src);
   return true;
}

bool
nir_lower_poly_line_smooth(nir_shader *shader, unsigned num_smooth_aa_sample)
{
   assert(shader->info.stage == MESA_SHADER_FRAGMENT);
   return nir_shader_intrinsics_pass(shader, lower_polylinesmooth,
                                     nir_metadata_control_flow,
                                     &num_smooth_aa_sample);
}
