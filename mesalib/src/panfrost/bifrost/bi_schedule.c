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

/* Finds the clause type required or return none */

static bool
bi_is_fragz(bi_instruction *ins)
{
        if (!(ins->src[0] & BIR_INDEX_CONSTANT))
                return false;

        return (ins->constant.u32 == BIFROST_FRAGZ);
}

static enum bifrost_clause_type
bi_clause_type_for_ins(bi_instruction *ins)
{
        unsigned T = ins->type;

        /* Only high latency ops impose clause types */
        if (!(bi_class_props[T] & BI_SCHED_HI_LATENCY))
                return BIFROST_CLAUSE_NONE;

        switch (T) {
        case BI_BRANCH:
        case BI_DISCARD:
                return BIFROST_CLAUSE_NONE;

        case BI_LOAD_VAR:
                if (bi_is_fragz(ins))
                        return BIFROST_CLAUSE_FRAGZ;

                return BIFROST_CLAUSE_LOAD_VARY;

        case BI_LOAD_UNIFORM:
        case BI_LOAD_ATTR:
        case BI_LOAD_VAR_ADDRESS:
                return BIFROST_CLAUSE_UBO;

        case BI_TEX:
                return BIFROST_CLAUSE_TEX;

        case BI_LOAD:
                return BIFROST_CLAUSE_SSBO_LOAD;

        case BI_STORE:
        case BI_STORE_VAR:
                return BIFROST_CLAUSE_SSBO_STORE;

        case BI_BLEND:
                return BIFROST_CLAUSE_BLEND;

        case BI_ATEST:
                return BIFROST_CLAUSE_ATEST;

        default:
                unreachable("Invalid high-latency class");
        }
}

/* There is an encoding restriction against FMA fp16 add/min/max
 * having both sources with abs(..) with a duplicated source. This is
 * due to the packing being order-sensitive, so the ports must end up distinct
 * to handle both having abs(..). The swizzle doesn't matter here. Note
 * BIR_INDEX_REGISTER generally should not be used pre-schedule (TODO: enforce
 * this).
 */

static bool
bi_ambiguous_abs(bi_instruction *ins)
{
        bool classy = bi_class_props[ins->type] & BI_NO_ABS_ABS_FP16_FMA;
        bool typey = ins->dest_type == nir_type_float16;
        bool absy = ins->src_abs[0] && ins->src_abs[1];

        return classy && typey && absy;
}

/* New Bifrost (which?) don't seem to have ICMP on FMA */
static bool
bi_icmp(bi_instruction *ins)
{
        bool ic = nir_alu_type_get_base_type(ins->src_types[0]) != nir_type_float;
        return ic && (ins->type == BI_CMP);
}

/* No 8/16-bit IADD/ISUB on FMA */
static bool
bi_imath_small(bi_instruction *ins)
{
        bool sz = nir_alu_type_get_type_size(ins->src_types[0]) < 32;
        return sz && (ins->type == BI_IMATH);
}

/* Lowers FMOV to ADD #0, since FMOV doesn't exist on the h/w and this is the
 * latest time it's sane to lower (it's useful to distinguish before, but we'll
 * need this handle during scheduling to ensure the ports get modeled
 * correctly with respect to the new zero source) */

static void
bi_lower_fmov(bi_instruction *ins)
{
        if (ins->type != BI_FMOV)
                return;

        ins->type = BI_ADD;
        ins->src[1] = BIR_INDEX_ZERO;
        ins->src_types[1] = ins->src_types[0];
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

/* Eventually, we'll need a proper scheduling, grouping instructions
 * into clauses and ordering/assigning grouped instructions to the
 * appropriate FMA/ADD slots. Right now we do the dumbest possible
 * thing just to have the scheduler stubbed out so we can focus on
 * codegen */

void
bi_schedule(bi_context *ctx)
{
        unsigned ids = 0;
        unsigned last_id = 0;
        bool is_first = true;

        bi_foreach_block(ctx, block) {
                bi_block *bblock = (bi_block *) block;

                list_inithead(&bblock->clauses);

                bi_foreach_instr_in_block(bblock, ins) {
                        /* Convenient time to lower */
                        bi_lower_fmov(ins);

                        unsigned props = bi_class_props[ins->type];

                        bi_clause *u = rzalloc(ctx, bi_clause);
                        u->bundle_count = 1;

                        /* Check for scheduling restrictions */

                        bool can_fma = props & BI_SCHED_FMA;
                        bool can_add = props & BI_SCHED_ADD;

                        can_fma &= !bi_ambiguous_abs(ins);
                        can_fma &= !bi_icmp(ins);
                        can_fma &= !bi_imath_small(ins);

                        assert(can_fma || can_add);

                        if (can_fma)
                                u->bundles[0].fma = ins;
                        else
                                u->bundles[0].add = ins;

                        u->scoreboard_id = ids++;

                        if (is_first)
                                is_first = false;
                        else {
                                /* Rule: first instructions cannot have write barriers */
                                u->dependencies |= (1 << last_id);
                                u->data_register_write_barrier = true;
                        }

                        if (ins->type == BI_ATEST)
                                u->dependencies |= (1 << 6);

                        if (ins->type == BI_BLEND)
                                u->dependencies |= (1 << 6) | (1 << 7);

                        ids = ids & 1;
                        last_id = u->scoreboard_id;

                        /* Let's be optimistic, we'll fix up later */
                        u->back_to_back = true;

                        u->constant_count = 1;
                        u->constants[0] = ins->constant.u64;

                        /* No indirect jumps yet */
                        if (ins->type == BI_BRANCH) {
                                u->branch_constant = true;
                                u->branch_conditional =
                                        (ins->cond != BI_COND_ALWAYS);
                        }

                        u->clause_type = bi_clause_type_for_ins(ins);
                        u->block = (struct bi_block *) block;

                        list_addtail(&u->link, &bblock->clauses);
                }

                /* Back-to-back bit affects only the last clause of a block,
                 * the rest are implicitly true */
                bi_clause *last_clause = list_last_entry(&bblock->clauses, bi_clause, link);

                if (last_clause)
                        last_clause->back_to_back = bi_back_to_back(bblock);

                bblock->scheduled = true;
        }
}
