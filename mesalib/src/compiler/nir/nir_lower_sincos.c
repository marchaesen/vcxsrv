/*
 * Copyright (c) 2014 Scott Mansell
 * Copyright © 2014 Broadcom
 * Copyright (c) 2019 Vasily Khoruzhick <anarsoul@gmail.com>
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

#include "util/u_math.h"
#include "nir.h"
#include "nir_builder.h"

static nir_ssa_def *
shrink_input(nir_builder *b, nir_ssa_def *x)
{
   nir_ssa_def *scaled_x = nir_fmul_imm(b, x, 1.0 / (M_PI * 2));

   nir_ssa_def *xfrac = nir_ffract(b, scaled_x);
   /* Map [0.5, 1] to [-0.5, 0] */
   nir_ssa_def *xfrac_0_5_1 = nir_fadd_imm(b, xfrac, -1.0);
   /* Map [-1, -0.5] to [0, 0.5] */
   nir_ssa_def *xfrac_n1_n0_5 = nir_fadd_imm(b, xfrac, 1.0);

   nir_ssa_def *geq_0_5 = nir_build_alu(b, nir_op_fge, xfrac, nir_imm_float(b, 0.5), NULL, NULL);
   nir_ssa_def *less_m0_5 = nir_build_alu(b, nir_op_flt, xfrac, nir_imm_float(b, -0.5), NULL, NULL);

   nir_ssa_def *sel1 = nir_build_alu(b, nir_op_bcsel, geq_0_5, xfrac_0_5_1, xfrac, NULL );
   nir_ssa_def *sel2 = nir_build_alu(b, nir_op_bcsel, less_m0_5, xfrac_n1_n0_5, sel1, NULL);

   return sel2;
}

static nir_ssa_def *
lower_sincos(nir_builder *b, nir_ssa_def *src, bool do_cos)
{
   /* Fast sin/cos implementation, see
    * https://web.archive.org/web/20180105155939/http://forum.devmaster.net/t/fast-and-accurate-sine-cosine/9648
    */
   const float B = 8.0; //(M_PI * 2.0) * (4.0 / M_PI);
   const float C = -16.0; //(M_PI * 2.0) * (M_PI * 2.0) * (-4.0 / (M_PI * M_PI));
   const float P = 0.225;

   if (do_cos)
      src = nir_fadd_imm(b, src, M_PI / 2.0);

   nir_ssa_def *x = shrink_input(b, src);

   nir_ssa_def *bx = nir_fmul_imm(b, x, B);
   nir_ssa_def *cx = nir_fmul_imm(b, x, C);
   nir_ssa_def *absx = nir_fabs(b, x);
   nir_ssa_def *cxabsx = nir_fmul(b, cx, absx);

   /* Y1 = B * x + C * x * fabs(x) */
   nir_ssa_def *y1 = nir_fadd(b, bx, cxabsx);

   /* Precision step: Y = P * (Y1 * fabs(Y1) - Y1) + Y1 */
   nir_ssa_def *y = nir_fabs(b, y1);
   y = nir_fmul(b, y, y1);
   y = nir_fsub(b, y, y1);
   y = nir_fmul_imm(b, y, P);
   y = nir_fadd(b, y, y1);

   return y;
}

static bool
lower_sincos_impl(nir_function_impl *impl)
{
   bool progress = false;

   nir_builder b;
   nir_builder_init(&b, impl);

   nir_foreach_block(block, impl) {
      nir_foreach_instr_safe(instr, block) {
         if (instr->type != nir_instr_type_alu)
            continue;

         nir_alu_instr *alu_instr = nir_instr_as_alu(instr);
         nir_ssa_def *lower;

         b.cursor = nir_before_instr(instr);

         switch (alu_instr->op) {
         case nir_op_fsin:
            lower = lower_sincos(&b, nir_ssa_for_alu_src(&b, alu_instr, 0), false);
            break;
         case nir_op_fcos:
            lower = lower_sincos(&b, nir_ssa_for_alu_src(&b, alu_instr, 0), true);
            break;
         default:
            continue;
         }

         nir_ssa_def_rewrite_uses(&alu_instr->dest.dest.ssa,
                                  nir_src_for_ssa(lower));
         nir_instr_remove(instr);
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
nir_lower_sincos(nir_shader *shader)
{
   bool progress = false;

   nir_foreach_function(function, shader) {
      if (function->impl)
         progress |= lower_sincos_impl(function->impl);
   }

   return progress;
}
