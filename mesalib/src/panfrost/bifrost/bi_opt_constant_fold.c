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

/* Dead simple constant folding to cleanup compiler frontend patterns. Before
 * adding a new pattern here, check why you need it and whether we can avoid
 * generating the constant BIR at all. */

static uint32_t
bi_fold_constant(bi_instr *I, bool *unsupported)
{
        uint32_t a = I->src[0].value;
        uint32_t b = I->src[1].value;

        switch (I->op) {
        case BI_OPCODE_SWZ_V2I16:
        {
                uint16_t lo = (a & 0xFFFF);
                uint16_t hi = (a >> 16);

                enum bi_swizzle swz = I->src[0].swizzle;
                assert(swz < BI_SWIZZLE_H11);

                /* Note order is H00, H01, H10, H11 */
                return (((swz & (1 << 1)) ? hi : lo) << 0) |
                        (((swz & (1 << 0)) ? hi : lo) << 16);
        }

        case BI_OPCODE_MKVEC_V2I16:
        {
                bool hi_a = I->src[0].swizzle & BI_SWIZZLE_H11;
                bool hi_b = I->src[1].swizzle & BI_SWIZZLE_H11;

                uint16_t lo = (hi_a ? (a >> 16) : (a & 0xFFFF));
                uint16_t hi = (hi_b ? (b >> 16) : (b & 0xFFFF));

                return (hi << 16) | lo;
        }

        default:
                *unsupported = true;
                return 0;
        }
}

static bool
bi_all_srcs_const(bi_instr *I)
{
        bi_foreach_src(I, s) {
                enum bi_index_type type = I->src[s].type;

                if (!(type == BI_INDEX_NULL || type == BI_INDEX_CONSTANT))
                        return false;
        }

        return true;
}

void
bi_opt_constant_fold(bi_context *ctx)
{
        bi_foreach_instr_global_safe(ctx, ins) {
                if (!bi_all_srcs_const(ins)) continue;

                bool unsupported = false;
                uint32_t replace = bi_fold_constant(ins, &unsupported);
                if (unsupported) continue;

                /* Replace with constant move, to be copypropped */
                bi_builder b = bi_init_builder(ctx, bi_after_instr(ins));
                bi_mov_i32_to(&b, ins->dest[0], bi_imm_u32(replace));
                bi_remove_instruction(ins);
        }
}
