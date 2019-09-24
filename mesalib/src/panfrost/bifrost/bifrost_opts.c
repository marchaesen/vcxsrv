/*
 * Copyright (C) 2019 Ryan Houdek <Sonicadvance1@gmail.com>
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

#include "bifrost_opts.h"
#include "compiler_defines.h"

bool
bifrost_opt_branch_fusion(compiler_context *ctx, bifrost_block *block)
{
        bool progress = false;
        mir_foreach_instr_in_block_safe(block, instr) {
                if (instr->op != op_branch) continue;
                if (instr->literal_args[0] != BR_COND_EQ) continue;

                unsigned src1 = instr->ssa_args.src0;

                // Only work on SSA values
                if (src1 >= SSA_FIXED_MINIMUM) continue;

                // Find the source for this conditional branch instruction
                // It'll be a CSEL instruction
                // If it's comparision is one of the ops that our conditional branch supports
                // then we can merge the two
                mir_foreach_instr_in_block_from_rev(block, next_instr, instr) {
                        if (next_instr->op != op_csel_i32) continue;

                        if (next_instr->ssa_args.dest == src1) {
                                // We found the CSEL instruction that is the source here
                                // Check its condition to make sure it matches what we can fuse
                                unsigned cond = next_instr->literal_args[0];
                                if (cond == CSEL_IEQ) {
                                        // This CSEL is doing an IEQ for our conditional branch doing EQ
                                        // We can just emit a conditional branch that does the comparison
                                        struct bifrost_instruction new_instr = {
                                                .op = op_branch,
                                                .dest_components = 0,
                                                .ssa_args = {
                                                        .dest = SSA_INVALID_VALUE,
                                                        .src0 = next_instr->ssa_args.src0,
                                                        .src1 = next_instr->ssa_args.src1,
                                                        .src2 = SSA_INVALID_VALUE,
                                                        .src3 = SSA_INVALID_VALUE,
                                                },
                                                .literal_args[0] = BR_COND_EQ,
                                                .literal_args[1] = instr->literal_args[1],
                                        };
                                        mir_insert_instr_before(instr, new_instr);
                                        mir_remove_instr(instr);
                                        progress |= true;
                                        break;
                                }
                        }
                }
        }

        return progress;
}

