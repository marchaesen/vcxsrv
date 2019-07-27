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

                if (ins->ssa_args.dest >= SSA_FIXED_MINIMUM) continue;
                if (mir_is_live_after(ctx, block, ins, ins->ssa_args.dest)) continue;

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
                        if (q->compact_branch) continue;

                        /* Check if used */
                        if (mir_has_arg(q, ins->ssa_args.dest))
                                break;

                        /* Check if overwritten */
                        if (q->ssa_args.dest == ins->ssa_args.dest) {
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

/* An even further special case - to be run after RA runs but before
 * scheduling, eliminating moves that end up being useless even though they
 * appeared meaningful in the SSA. Part #2 of register coalescing. */

void
midgard_opt_post_move_eliminate(compiler_context *ctx, midgard_block *block, struct ra_graph *g)
{
        mir_foreach_instr_in_block_safe(block, ins) {
                if (ins->type != TAG_ALU_4) continue;
                if (ins->compact_branch) continue;
                if (!OP_IS_MOVE(ins->alu.op)) continue;

                /* Check we're to the same place post-RA */
                unsigned iA = ins->ssa_args.dest;
                unsigned iB = ins->ssa_args.src1;

                if ((iA < 0) || (iB < 0)) continue;

                unsigned A = iA >= SSA_FIXED_MINIMUM ?
                        SSA_REG_FROM_FIXED(iA) : 
                        ra_get_node_reg(g, iA);

                unsigned B = iB >= SSA_FIXED_MINIMUM ?
                        SSA_REG_FROM_FIXED(iB) : 
                        ra_get_node_reg(g, iB);

                if (A != B) continue;
                if (ins->ssa_args.inline_constant) continue;

                /* Check we're in the work zone. TODO: promoted
                 * uniforms? */
                if (A >= 16) continue;

                /* Ensure there aren't side effects */
                if (mir_nontrivial_source2_mod(ins)) continue;
                if (mir_nontrivial_outmod(ins)) continue;
                if (ins->mask != 0xF) continue;

                /* We do want to rewrite to keep the graph sane for pipeline
                 * register creation (TODO: is this the best approach?) */
                mir_rewrite_index_dst(ctx, ins->ssa_args.src1, ins->ssa_args.dest);

                /* We're good to go */
                mir_remove_instruction(ins);

        }

}
