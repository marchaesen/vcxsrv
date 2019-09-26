/*
 * Copyright (C) 2018 Alyssa Rosenzweig
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

/* Basic dead code elimination on the MIR itself */

bool
midgard_opt_dead_code_eliminate(compiler_context *ctx, midgard_block *block)
{
        bool progress = false;

        mir_foreach_instr_in_block_safe(block, ins) {
                if (ins->type != TAG_ALU_4) continue;
                if (ins->compact_branch) continue;

                if (ins->dest >= SSA_FIXED_MINIMUM) continue;
                if (mir_is_live_after(ctx, block, ins, ins->dest)) continue;

                mir_remove_instruction(ins);
                progress = true;
        }

        return progress;
}

/* Removes dead moves, that is, moves with a destination overwritten before
 * being read. Normally handled implicitly as part of DCE, but this has to run
 * after the out-of-SSA pass */

bool
midgard_opt_dead_move_eliminate(compiler_context *ctx, midgard_block *block)
{
        bool progress = false;

        mir_foreach_instr_in_block_safe(block, ins) {
                if (ins->type != TAG_ALU_4) continue;
                if (ins->compact_branch) continue;
                if (!OP_IS_MOVE(ins->alu.op)) continue;

                /* Check if it's overwritten in this block before being read */
                bool overwritten = false;

                mir_foreach_instr_in_block_from(block, q, mir_next_op(ins)) {
                        /* Check if used */
                        if (mir_has_arg(q, ins->dest))
                                break;

                        /* Check if overwritten */
                        if (q->dest == ins->dest) {
                                /* Special case to vec4; component tracking is
                                 * harder */

                                overwritten = (q->mask == 0xF);
                                break;
                        }
                }

                if (overwritten) {
                        mir_remove_instruction(ins);
                        progress = true;
                }
        }

        return progress;
}
