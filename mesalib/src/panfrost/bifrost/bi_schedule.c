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
 *
 * Authors (Collabora):
 *      Alyssa Rosenzweig <alyssa.rosenzweig@collabora.com>
 */

#include "compiler.h"

/* Determines messsage type by checking the table and a few special cases. Only
 * case missing is tilebuffer instructions that access depth/stencil, which
 * require a Z_STENCIL message (to implement
 * ARM_shader_framebuffer_fetch_depth_stencil) */

static enum bifrost_message_type
bi_message_type_for_instr(bi_instr *ins)
{
        enum bifrost_message_type msg = bi_opcode_props[ins->op].message;
        bool ld_var_special = (ins->op == BI_OPCODE_LD_VAR_SPECIAL);

        if (ld_var_special && ins->varying_name == BI_VARYING_NAME_FRAG_Z)
                return BIFROST_MESSAGE_Z_STENCIL;

        if (msg == BIFROST_MESSAGE_LOAD && ins->seg == BI_SEG_UBO)
                return BIFROST_MESSAGE_ATTRIBUTE;

        return msg;
}

/* To work out the back-to-back flag, we need to detect branches and
 * "fallthrough" branches, implied in the last clause of a block that falls
 * through to another block with *multiple predecessors*. */

static bool
bi_back_to_back(bi_block *block)
{
        /* Last block of a program */
        if (!block->base.successors[0]) {
                assert(!block->base.successors[1]);
                return false;
        }

        /* Multiple successors? We're branching */
        if (block->base.successors[1])
                return false;

        struct pan_block *succ = block->base.successors[0];
        assert(succ->predecessors);
        unsigned count = succ->predecessors->entries;

        /* Back to back only if the successor has only a single predecessor */
        return (count == 1);
}

/* Insert a clause wrapping a single instruction */

bi_clause *
bi_singleton(void *memctx, bi_instr *ins,
                bi_block *block,
                unsigned scoreboard_id,
                unsigned dependencies,
                bool osrb)
{
        bi_clause *u = rzalloc(memctx, bi_clause);
        u->bundle_count = 1;

        ASSERTED bool can_fma = bi_opcode_props[ins->op].fma;
        bool can_add = bi_opcode_props[ins->op].add;
        assert(can_fma || can_add);

        if (can_add)
                u->bundles[0].add = ins;
        else
                u->bundles[0].fma = ins;

        u->scoreboard_id = scoreboard_id;
        u->staging_barrier = osrb;
        u->dependencies = dependencies;

        if (ins->op == BI_OPCODE_ATEST)
                u->dependencies |= (1 << 6);

        if (ins->op == BI_OPCODE_BLEND)
                u->dependencies |= (1 << 6) | (1 << 7);

        /* Let's be optimistic, we'll fix up later */
        u->flow_control = BIFROST_FLOW_NBTB;

        /* Build up a combined constant, count in 32-bit words */
        uint64_t combined_constant = 0;
        unsigned constant_count = 0;

        bi_foreach_src(ins, s) {
                if (ins->src[s].type != BI_INDEX_CONSTANT) continue;
                unsigned value = ins->src[s].value;

                /* Allow fast zero */
                if (value == 0 && u->bundles[0].fma) continue;

                if (constant_count == 0) {
                        combined_constant = ins->src[s].value;
                } else if (constant_count == 1) {
                        /* Allow reuse */
                        if (combined_constant == value)
                                continue;

                        combined_constant |= ((uint64_t) value) << 32ull;
                } else {
                        /* No more room! */
                        assert((combined_constant & 0xffffffff) == value ||
                                        (combined_constant >> 32ull) == value);
                }

                constant_count++;
        }

        if (ins->branch_target)
                u->branch_constant = true;

        /* XXX: Investigate errors when constants are not used */
        if (constant_count || u->branch_constant || true) {
                /* Clause in 64-bit, above in 32-bit */
                u->constant_count = 1;
                u->constants[0] = combined_constant;
        }

        u->next_clause_prefetch = (ins->op != BI_OPCODE_JUMP);
        u->message_type = bi_message_type_for_instr(ins);
        u->block = block;

        return u;
}

/* Eventually, we'll need a proper scheduling, grouping instructions
 * into clauses and ordering/assigning grouped instructions to the
 * appropriate FMA/ADD slots. Right now we do the dumbest possible
 * thing just to have the scheduler stubbed out so we can focus on
 * codegen */

void
bi_schedule(bi_context *ctx)
{
        bool is_first = true;

        bi_foreach_block(ctx, block) {
                bi_block *bblock = (bi_block *) block;

                list_inithead(&bblock->clauses);

                bi_foreach_instr_in_block(bblock, ins) {
                        bi_clause *u = bi_singleton(ctx, ins,
                                        bblock, 0, (1 << 0),
                                        !is_first);

                        is_first = false;
                        list_addtail(&u->link, &bblock->clauses);
                }

                /* Back-to-back bit affects only the last clause of a block,
                 * the rest are implicitly true */

                if (!list_is_empty(&bblock->clauses)) {
                        bi_clause *last_clause = list_last_entry(&bblock->clauses, bi_clause, link);
                        if (!bi_back_to_back(bblock))
                                last_clause->flow_control = BIFROST_FLOW_NBTB_UNCONDITIONAL;
                }

                bblock->scheduled = true;
        }
}
