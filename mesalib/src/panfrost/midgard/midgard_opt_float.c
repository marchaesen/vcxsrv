/*
 * Copyright (C) 2019 Collabora, Ltd.
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors (Collabora):
 *    Alyssa Rosenzweig <alyssa.rosenzweig@collabora.com>
 */

#include "compiler.h"
#include "midgard_ops.h"
#include <math.h>

/* Could a 32-bit value represent exactly a 32-bit floating point? */

static bool
mir_constant_float(uint32_t u)
{
        /* Cast */
        float f = 0;
        memcpy(&f, &u, sizeof(u));

        /* TODO: What exactly is the condition? */
        return !(isnan(f) || isinf(f));
}

/* Promotes imov with a constant to fmov where the constant is exactly
 * representible as a float */

bool
midgard_opt_promote_fmov(compiler_context *ctx, midgard_block *block)
{
        bool progress = false;

        mir_foreach_instr_in_block(block, ins) {
                if (ins->type != TAG_ALU_4) continue;
                if (ins->alu.op != midgard_alu_op_imov) continue;
                if (ins->has_inline_constant) continue;
                if (!ins->has_constants) continue;
                if (mir_nontrivial_source2_mod_simple(ins)) continue;
                if (mir_nontrivial_outmod(ins)) continue;

                /* We found an imov with a constant. Check the constants */
                bool ok = true;

                for (unsigned i = 0; i < ARRAY_SIZE(ins->constants); ++i)
                        ok &= mir_constant_float(ins->constants[i]);

                if (!ok)
                        continue;

                /* Rewrite to fmov */
                ins->alu.op = midgard_alu_op_fmov;
                ins->alu.outmod = 0;

                /* Clear the int mod */
                midgard_vector_alu_src u = vector_alu_from_unsigned(ins->alu.src2);
                u.mod = 0;
                ins->alu.src2 = vector_alu_srco_unsigned(u);

                progress |= true;
        }

        return progress;
}
