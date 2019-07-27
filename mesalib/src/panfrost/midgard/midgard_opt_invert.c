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
 */

#include "compiler.h"
#include "midgard_ops.h"

/* Lowers the invert field on instructions to a dedicated inot (inor)
 * instruction instead, as invert is not always supported natively by the
 * hardware */

void
midgard_lower_invert(compiler_context *ctx, midgard_block *block)
{
        mir_foreach_instr_in_block_safe(block, ins) {
                if (ins->type != TAG_ALU_4) continue;
                if (!ins->invert) continue;

                unsigned temp = make_compiler_temp(ctx);

                midgard_instruction not = {
                        .type = TAG_ALU_4,
                        .mask = ins->mask,
                        .ssa_args = {
                                .src0 = temp,
                                .src1 = 0,
                                .dest = ins->ssa_args.dest,
                                .inline_constant = true
                        },
                        .alu = {
                                .op = midgard_alu_op_inor,
                                /* TODO: i16 */
                                .reg_mode = midgard_reg_mode_32,
                                .dest_override = midgard_dest_override_none,
                                .outmod = midgard_outmod_int_wrap,
                                .src1 = vector_alu_srco_unsigned(blank_alu_src),
                                .src2 = vector_alu_srco_unsigned(zero_alu_src)
                        },
                };

                ins->ssa_args.dest = temp;
                ins->invert = false;
                mir_insert_instruction_before(mir_next_op(ins), not);
        }
}
