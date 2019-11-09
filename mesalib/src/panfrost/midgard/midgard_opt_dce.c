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
#include "util/u_memory.h"
#include "midgard_ops.h"

/* SIMD-aware dead code elimination. Perform liveness analysis step-by-step,
 * removing dead components. If an instruction ends up with a zero mask, the
 * instruction in total is dead and should be removed. */

static bool
can_cull_mask(compiler_context *ctx, midgard_instruction *ins)
{
        if (ins->dest >= ctx->temp_count)
                return false;

        if (ins->type == TAG_LOAD_STORE_4)
                if (load_store_opcode_props[ins->load_store.op].props & LDST_SPECIAL_MASK)
                        return false;

        return true;
}

static bool
can_dce(midgard_instruction *ins)
{
        if (ins->mask)
                return false;

        if (ins->compact_branch)
                return false;

        if (ins->type == TAG_LOAD_STORE_4)
                if (load_store_opcode_props[ins->load_store.op].props & LDST_SIDE_FX)
                        return false;

        return true;
}

bool
midgard_opt_dead_code_eliminate(compiler_context *ctx, midgard_block *block)
{
        bool progress = false;

        mir_invalidate_liveness(ctx);
        mir_compute_liveness(ctx);

        uint16_t *live = mem_dup(block->live_out, ctx->temp_count * sizeof(uint16_t));

        mir_foreach_instr_in_block_rev(block, ins) {
                if (can_cull_mask(ctx, ins)) {
                        midgard_reg_mode mode = mir_typesize(ins);
                        unsigned oldmask = ins->mask;

                        unsigned rounded = mir_round_bytemask_down(live[ins->dest], mode);
                        unsigned cmask = mir_from_bytemask(rounded, mode);

                        ins->mask &= cmask;
                        progress |= (ins->mask != oldmask);
                }

                mir_liveness_ins_update(live, ins, ctx->temp_count);
        }

        mir_foreach_instr_in_block_safe(block, ins) {
                if (can_dce(ins)) {
                        mir_remove_instruction(ins);
                        progress = true;
                }
        }

        free(live);

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
