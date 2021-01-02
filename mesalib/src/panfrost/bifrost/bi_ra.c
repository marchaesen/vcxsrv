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
#include "bi_builder.h"
#include "panfrost/util/lcra.h"
#include "util/u_memory.h"

static void
bi_compute_interference(bi_context *ctx, struct lcra_state *l)
{
        bi_compute_liveness(ctx);

        bi_foreach_block(ctx, _blk) {
                bi_block *blk = (bi_block *) _blk;
                uint16_t *live = mem_dup(_blk->live_out, l->node_count * sizeof(uint16_t));

                bi_foreach_instr_in_block_rev(blk, ins) {
                        /* Mark all registers live after the instruction as
                         * interfering with the destination */

                        for (unsigned d = 0; d < ARRAY_SIZE(ins->dest); ++d) {
                                if (bi_get_node(ins->dest[d]) >= l->node_count)
                                        continue;

                                for (unsigned i = 1; i < l->node_count; ++i) {
                                        if (live[i])
                                                lcra_add_node_interference(l, bi_get_node(ins->dest[d]), bi_writemask(ins), i, live[i]);
                                }
                        }

                        /* Update live_in */
                        bi_liveness_ins_update(live, ins, l->node_count);
                }

                free(live);
        }
}

enum {
        BI_REG_CLASS_WORK = 0,
} bi_reg_class;

static struct lcra_state *
bi_allocate_registers(bi_context *ctx, bool *success)
{
        unsigned node_count = bi_max_temp(ctx);

        struct lcra_state *l =
                lcra_alloc_equations(node_count, 1);

        if (ctx->is_blend) {
                /* R0-R3 are reserved for the blend input */
                l->class_start[BI_REG_CLASS_WORK] = 4 * 4;
                l->class_size[BI_REG_CLASS_WORK] = 64 * 4;
        } else {
                /* R0 - R63, all 32-bit */
                l->class_start[BI_REG_CLASS_WORK] = 0;
                l->class_size[BI_REG_CLASS_WORK] = 59 * 4;
        }

        bi_foreach_instr_global(ctx, ins) {
                unsigned dest = bi_get_node(ins->dest[0]);

                /* Blend shaders expect the src colour to be in r0-r3 */
                if (ins->op == BI_OPCODE_BLEND && !ctx->is_blend) {
                        unsigned node = bi_get_node(ins->src[0]);
                        assert(node < node_count);
                        l->solutions[node] = 0;
                }

                if (!dest || (dest >= node_count))
                        continue;

                l->class[dest] = BI_REG_CLASS_WORK;
                lcra_set_alignment(l, dest, 2, 16); /* 2^2 = 4 */
                lcra_restrict_range(l, dest, 4);

        }

        bi_compute_interference(ctx, l);

        *success = lcra_solve(l);

        return l;
}

static bi_index
bi_reg_from_index(struct lcra_state *l, bi_index index)
{
        /* Offsets can only be applied when we register allocated an index, or
         * alternatively for FAU's encoding */

        ASSERTED bool is_offset = (index.offset > 0) &&
                (index.type != BI_INDEX_FAU);

        /* Did we run RA for this index at all */
        if (bi_get_node(index) >= l->node_count) {
                assert(!is_offset);
                return index;
        }

        /* LCRA didn't bother solving this index (how lazy!) */
        signed solution = l->solutions[bi_get_node(index)];
        if (solution < 0) {
                assert(!is_offset);
                return index;
        }

        assert((solution & 0x3) == 0);
        unsigned reg = solution / 4;
        reg += index.offset;

        /* todo: do we want to compose with the subword swizzle? */
        bi_index new_index = bi_register(reg);
        new_index.swizzle = index.swizzle;
        new_index.abs = index.abs;
        new_index.neg = index.neg;
        return new_index;
}

static void
bi_install_registers(bi_context *ctx, struct lcra_state *l)
{
        bi_foreach_instr_global(ctx, ins) {
                ins->dest[0] = bi_reg_from_index(l, ins->dest[0]);

                bi_foreach_src(ins, s)
                        ins->src[s] = bi_reg_from_index(l, ins->src[s]);
        }
}

static void
bi_rewrite_index_src_single(bi_instr *ins, bi_index old, bi_index new)
{
        bi_foreach_src(ins, i) {
                if (bi_is_equiv(ins->src[i], old)) {
                        ins->src[i].type = new.type;
                        ins->src[i].reg = new.reg;
                        ins->src[i].value = new.value;
                }
        }
}

/* Get the single instruction in a singleton clause. Precondition: clause
 * contains exactly 1 instruction.
 *
 * More complex scheduling implies tougher constraints on spilling. We'll cross
 * that bridge when we get to it. For now, just grab the one and only
 * instruction in the clause */

static bi_instr *
bi_unwrap_singleton(bi_clause *clause)
{
       assert(clause->bundle_count == 1);
       assert((clause->bundles[0].fma != NULL) ^ (clause->bundles[0].add != NULL));

       return clause->bundles[0].fma ?: clause->bundles[0].add;
}

/* If register allocation fails, find the best spill node */

static signed
bi_choose_spill_node(bi_context *ctx, struct lcra_state *l)
{
        /* Pick a node satisfying bi_spill_register's preconditions */

        bi_foreach_instr_global(ctx, ins) {
                if (ins->no_spill || ins->dest[0].offset || !bi_is_null(ins->dest[1])) {
                        for (unsigned d = 0; d < ARRAY_SIZE(ins->dest); ++d)
                                lcra_set_node_spill_cost(l, bi_get_node(ins->dest[0]), -1);
                }
        }

        for (unsigned i = PAN_IS_REG; i < l->node_count; i += 2)
                lcra_set_node_spill_cost(l, i, -1);

        return lcra_get_best_spill_node(l);
}

static void
bi_spill_dest(bi_builder *b, bi_index index, uint32_t offset,
                bi_clause *clause, bi_block *block, bi_instr *ins,
                uint32_t *channels)
{
        ins->dest[0] = bi_temp(b->shader);
        ins->no_spill = true;

        unsigned newc = util_last_bit(bi_writemask(ins)) >> 2;
        *channels = MAX2(*channels, newc);

        b->cursor = bi_after_instr(ins);

        bi_instr *st = bi_store_to(b, (*channels) * 32, bi_null(),
                        ins->dest[0], bi_imm_u32(offset), bi_zero(),
                        BI_SEG_TL);

        bi_clause *singleton = bi_singleton(b->shader, st, block, 0, (1 << 0),
                        true);

        list_add(&singleton->link, &clause->link);
        b->shader->spills++;
}

static void
bi_fill_src(bi_builder *b, bi_index index, uint32_t offset, bi_clause *clause,
                bi_block *block, bi_instr *ins, unsigned channels)
{
        bi_index temp = bi_temp(b->shader);

        b->cursor = bi_before_instr(ins);
        bi_instr *ld = bi_load_to(b, channels * 32, temp, bi_imm_u32(offset),
                        bi_zero(), BI_SEG_TL);
        ld->no_spill = true;

        bi_clause *singleton = bi_singleton(b->shader, ld, block, 0,
                        (1 << 0), true);

        list_addtail(&singleton->link, &clause->link);

        /* Rewrite to use */
        bi_rewrite_index_src_single(ins, index, temp);
        b->shader->fills++;
}

/* Once we've chosen a spill node, spill it. Precondition: node is a valid
 * SSA node in the non-optimized scheduled IR that was not already
 * spilled (enforced by bi_choose_spill_node). Returns bytes spilled */

static unsigned
bi_spill_register(bi_context *ctx, bi_index index, uint32_t offset)
{
        assert(!index.reg);

        bi_builder _b = { .shader = ctx };
        unsigned channels = 1;

        /* Spill after every store, fill before every load */
        bi_foreach_block(ctx, _block) {
                bi_block *block = (bi_block *) _block;
                bi_foreach_clause_in_block_safe(block, clause) {
                        bi_instr *ins = bi_unwrap_singleton(clause);
                        if (bi_is_equiv(ins->dest[0], index)) {
                                bi_spill_dest(&_b, index, offset, clause,
                                                block, ins, &channels);
                        }

                        if (bi_has_arg(ins, index))
                                bi_fill_src(&_b, index, offset, clause, block, ins, channels);
                }
        }

        return (channels * 4);
}

void
bi_register_allocate(bi_context *ctx)
{
        struct lcra_state *l = NULL;
        bool success = false;

        unsigned iter_count = 1000; /* max iterations */

        /* Number of bytes of memory we've spilled into */
        unsigned spill_count = 0;

        do {
                if (l) {
                        signed spill_node = bi_choose_spill_node(ctx, l);
                        lcra_free(l);
                        l = NULL;

                        if (spill_node == -1)
                                unreachable("Failed to choose spill node\n");

                        spill_count += bi_spill_register(ctx,
                                        bi_node_to_index(spill_node, bi_max_temp(ctx)),
                                        spill_count);
                }

                bi_invalidate_liveness(ctx);
                l = bi_allocate_registers(ctx, &success);
        } while(!success && ((iter_count--) > 0));

        assert(success);

        ctx->tls_size = spill_count;
        bi_install_registers(ctx, l);

        lcra_free(l);
}
