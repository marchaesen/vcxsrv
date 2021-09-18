/*
 * Copyright (C) 2020 Collabora Ltd.
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
 */

#include "compiler.h"
#include "bi_builder.h"

/* Not all 8-bit and 16-bit instructions support all swizzles on all sources.
 * These passes, intended to run after NIR->BIR but before scheduling/RA, lower
 * away swizzles that cannot be represented. In the future, we should try to
 * recombine swizzles where we can as an optimization.
 */

static void
bi_lower_swizzle_16(bi_context *ctx, bi_instr *ins, unsigned src)
{
        /* Identity is ok */
        if (ins->src[src].swizzle == BI_SWIZZLE_H01)
                return;

        /* TODO: Use the opcode table and be a lot more methodical about this... */
        switch (ins->op) {
        /* Some instructions used with 16-bit data never have swizzles */
        case BI_OPCODE_CSEL_V2F16:
        case BI_OPCODE_CSEL_V2I16:
        case BI_OPCODE_CSEL_V2S16:
        case BI_OPCODE_CSEL_V2U16:

        /* Despite ostensibly being 32-bit instructions, CLPER does not
         * inherently interpret the data, so it can be used for v2f16
         * derivatives, which might require swizzle lowering */
        case BI_OPCODE_CLPER_I32:
        case BI_OPCODE_CLPER_V6_I32:
            break;

        case BI_OPCODE_IADD_V2S16:
        case BI_OPCODE_IADD_V2U16:
        case BI_OPCODE_ISUB_V2S16:
        case BI_OPCODE_ISUB_V2U16:
            if (src == 0 && ins->src[src].swizzle != BI_SWIZZLE_H10)
                    break;
            else
                    return;
        case BI_OPCODE_LSHIFT_AND_V2I16:
        case BI_OPCODE_LSHIFT_OR_V2I16:
        case BI_OPCODE_LSHIFT_XOR_V2I16:
        case BI_OPCODE_RSHIFT_AND_V2I16:
        case BI_OPCODE_RSHIFT_OR_V2I16:
        case BI_OPCODE_RSHIFT_XOR_V2I16:
            if (src == 2)
                    return;
            else
                    break;

        /* We don't want to deal with reswizzling logic in modifier prop. Move
         * the swizzle outside, it's easier for clamp propagation. */
        case BI_OPCODE_FCLAMP_V2F16:
        {
                bi_builder b = bi_init_builder(ctx, bi_after_instr(ins));
                bi_index dest = ins->dest[0];
                bi_index tmp = bi_temp(ctx);

                ins->dest[0] = tmp;
                bi_swz_v2i16_to(&b, dest, bi_replace_index(ins->src[0], tmp));
                return;
        }

        default:
            return;
        }

        /* If the instruction is scalar we can ignore the other component */
        if (ins->dest[0].swizzle == BI_SWIZZLE_H00 &&
                        ins->src[src].swizzle == BI_SWIZZLE_H00)
        {
                ins->src[src].swizzle = BI_SWIZZLE_H01;
                return;
        }

        /* Lower it away */
        bi_builder b = bi_init_builder(ctx, bi_before_instr(ins));
        ins->src[src] = bi_replace_index(ins->src[src],
                        bi_swz_v2i16(&b, ins->src[src]));
        ins->src[src].swizzle = BI_SWIZZLE_H01;
}

void
bi_lower_swizzle(bi_context *ctx)
{
        bi_foreach_instr_global_safe(ctx, ins) {
                bi_foreach_src(ins, s) {
                        if (!bi_is_null(ins->src[s]))
                                bi_lower_swizzle_16(ctx, ins, s);
                }
        }
}
