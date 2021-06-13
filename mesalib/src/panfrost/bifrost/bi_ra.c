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

/* A clause may contain 1 message-passing instruction.  No subsequent
 * instruction in the clause may access its registers due to data races.
 * Scheduling ensures this is possible but RA needs to preserve this. The
 * simplest solution is forcing accessed registers live in _all_ words at the
 * end (and consequently throughout) the clause, addressing corner cases where
 * a single component is masked out */

static void
bi_mark_msg_live(bi_block *block, bi_clause *clause, unsigned node_count, uint16_t *live)
{
        bi_foreach_instr_in_clause(block, clause, ins) {
                if (!bi_opcode_props[ins->op].message) continue;

                bi_foreach_dest(ins, d) {
                        unsigned node = bi_get_node(ins->dest[d]);
                        if (node < node_count)
                                live[node] |= bi_writemask(ins, d);
                }

                bi_foreach_src(ins, s) {
                        unsigned node = bi_get_node(ins->src[s]);
                        if (node < node_count) {
                                unsigned count = bi_count_read_registers(ins, s);
                                unsigned rmask = (1 << (4 * count)) - 1;
                                live[node] |= (rmask << (4 * ins->src[s].offset));
                        }
                }

                break;
        }
}

static void
bi_mark_interference(bi_block *block, bi_clause *clause, struct lcra_state *l, uint16_t *live, unsigned node_count, bool is_blend)
{
        bi_foreach_instr_in_clause_rev(block, clause, ins) {
                /* Mark all registers live after the instruction as
                 * interfering with the destination */

                bi_foreach_dest(ins, d) {
                        if (bi_get_node(ins->dest[d]) >= node_count)
                                continue;

                        for (unsigned i = 0; i < node_count; ++i) {
                                if (live[i]) {
                                        lcra_add_node_interference(l, bi_get_node(ins->dest[d]),
                                                        bi_writemask(ins, d), i, live[i]);
                                }
                        }
                }

                if (!is_blend && ins->op == BI_OPCODE_BLEND) {
                        /* Add blend shader interference: blend shaders might
                         * clobber r0-r15. */
                        for (unsigned i = 0; i < node_count; ++i) {
                                if (!live[i])
                                        continue;

                                for (unsigned j = 0; j < 4; j++) {
                                        lcra_add_node_interference(l, node_count + j,
                                                                   0xFFFF,
                                                                   i, live[i]);
                                }
                        }
                }

                /* Update live_in */
                bi_liveness_ins_update(live, ins, node_count);
        }
}

static void
bi_compute_interference(bi_context *ctx, struct lcra_state *l)
{
        unsigned node_count = bi_max_temp(ctx);

        bi_compute_liveness(ctx);

        bi_foreach_block(ctx, _blk) {
                bi_block *blk = (bi_block *) _blk;
                uint16_t *live = mem_dup(_blk->live_out, node_count * sizeof(uint16_t));

                bi_foreach_clause_in_block_rev(blk, clause) {
                        bi_mark_msg_live(blk, clause, node_count, live);
                        bi_mark_interference(blk, clause, l, live, node_count,
                                             ctx->inputs->is_blend);
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

        /* We need 4 hidden nodes to encode interference caused by non-terminal
         * BLEND (blend shaders are allowed to use r0-r16).
         */
        struct lcra_state *l =
                lcra_alloc_equations(node_count + 4, 1);

        /* Preset solutions for the blend shader pseudo nodes */
        for (unsigned i = 0; i < 4; i++)
                l->solutions[node_count + i] = i * 16;

        if (ctx->inputs->is_blend) {
                /* R0-R3 are reserved for the blend input */
                l->class_start[BI_REG_CLASS_WORK] = 0;
                l->class_size[BI_REG_CLASS_WORK] = 16 * 4;
        } else {
                /* R0 - R63, all 32-bit */
                l->class_start[BI_REG_CLASS_WORK] = 0;
                l->class_size[BI_REG_CLASS_WORK] = 59 * 4;
        }

        bi_foreach_instr_global(ctx, ins) {
                bi_foreach_dest(ins, d) {
                        unsigned dest = bi_get_node(ins->dest[d]);

                        /* Blend shaders expect the src colour to be in r0-r3 */
                        if (ins->op == BI_OPCODE_BLEND &&
                            !ctx->inputs->is_blend) {
                                unsigned node = bi_get_node(ins->src[0]);
                                assert(node < node_count);
                                l->solutions[node] = 0;
                        }

                        if (dest >= node_count)
                                continue;

                        l->class[dest] = BI_REG_CLASS_WORK;
                        lcra_set_alignment(l, dest, 2, 16); /* 2^2 = 4 */
                        lcra_restrict_range(l, dest, 4);
                }

        }

        bi_compute_interference(ctx, l);

        *success = lcra_solve(l);

        return l;
}

static bi_index
bi_reg_from_index(bi_context *ctx, struct lcra_state *l, bi_index index)
{
        /* Offsets can only be applied when we register allocated an index, or
         * alternatively for FAU's encoding */

        ASSERTED bool is_offset = (index.offset > 0) &&
                (index.type != BI_INDEX_FAU);
        unsigned node_count = bi_max_temp(ctx);

        /* Did we run RA for this index at all */
        if (bi_get_node(index) >= node_count) {
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
                bi_foreach_dest(ins, d)
                        ins->dest[d] = bi_reg_from_index(ctx, l, ins->dest[d]);

                bi_foreach_src(ins, s)
                        ins->src[s] = bi_reg_from_index(ctx, l, ins->src[s]);
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

/* If register allocation fails, find the best spill node */

static signed
bi_choose_spill_node(bi_context *ctx, struct lcra_state *l)
{
        /* Pick a node satisfying bi_spill_register's preconditions */

        bi_foreach_instr_global(ctx, ins) {
                bi_foreach_dest(ins, d) {
                        if (ins->no_spill || ins->dest[d].offset)
                                lcra_set_node_spill_cost(l, bi_get_node(ins->dest[d]), -1);
                }
        }

        unsigned node_count = bi_max_temp(ctx);
        for (unsigned i = PAN_IS_REG; i < node_count; i += 2)
                lcra_set_node_spill_cost(l, i, -1);

        return lcra_get_best_spill_node(l);
}

static void
bi_spill_dest(bi_builder *b, bi_index index, bi_index temp, uint32_t offset,
                bi_clause *clause, bi_block *block, unsigned channels)
{
        b->cursor = bi_after_clause(clause);

        /* setup FAU as [offset][0] */
        bi_instr *st = bi_store(b, channels * 32, temp,
                        bi_passthrough(BIFROST_SRC_FAU_LO),
                        bi_passthrough(BIFROST_SRC_FAU_HI),
                        BI_SEG_TL);

        bi_clause *singleton = bi_singleton(b->shader, st, block, 0, (1 << 0),
                        offset, true);

        list_add(&singleton->link, &clause->link);
        b->shader->spills++;
}

static void
bi_fill_src(bi_builder *b, bi_index index, bi_index temp, uint32_t offset,
                bi_clause *clause, bi_block *block, unsigned channels)
{
        b->cursor = bi_before_clause(clause);
        bi_instr *ld = bi_load_to(b, channels * 32, temp,
                        bi_passthrough(BIFROST_SRC_FAU_LO),
                        bi_passthrough(BIFROST_SRC_FAU_HI),
                        BI_SEG_TL);
        ld->no_spill = true;

        bi_clause *singleton = bi_singleton(b->shader, ld, block, 0,
                        (1 << 0), offset, true);

        list_addtail(&singleton->link, &clause->link);
        b->shader->fills++;
}

static unsigned
bi_clause_mark_spill(bi_context *ctx, bi_block *block,
                bi_clause *clause, bi_index index, bi_index *temp)
{
        unsigned channels = 0;

        bi_foreach_instr_in_clause(block, clause, ins) {
                bi_foreach_dest(ins, d) {
                        if (!bi_is_equiv(ins->dest[d], index)) continue;
                        if (bi_is_null(*temp)) *temp = bi_temp_reg(ctx);
                        ins->no_spill = true;

                        unsigned offset = ins->dest[d].offset;
                        ins->dest[d] = bi_replace_index(ins->dest[d], *temp);
                        ins->dest[d].offset = offset;

                        unsigned newc = util_last_bit(bi_writemask(ins, d)) >> 2;
                        channels = MAX2(channels, newc);
                }
        }

        return channels;
}

static bool
bi_clause_mark_fill(bi_context *ctx, bi_block *block, bi_clause *clause,
                bi_index index, bi_index *temp)
{
        bool fills = false;

        bi_foreach_instr_in_clause(block, clause, ins) {
                if (!bi_has_arg(ins, index)) continue;
                if (bi_is_null(*temp)) *temp = bi_temp_reg(ctx);
                bi_rewrite_index_src_single(ins, index, *temp);
                fills = true;
        }

        return fills;
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
                        bi_index tmp = bi_null();

                        unsigned local_channels = bi_clause_mark_spill(ctx,
                                        block, clause, index, &tmp);

                        channels = MAX2(channels, local_channels);

                        if (local_channels) {
                                bi_spill_dest(&_b, index, tmp, offset,
                                                clause, block, channels);
                        }

                        /* For SSA form, if we write/spill, there was no prior
                         * contents to fill, so don't waste time reading
                         * garbage */

                        bool should_fill = !local_channels || index.reg;
                        should_fill &= bi_clause_mark_fill(ctx, block, clause,
                                        index, &tmp);

                        if (should_fill) {
                                bi_fill_src(&_b, index, tmp, offset, clause,
                                                block, channels);
                        }
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
        unsigned spill_count = ctx->info->tls_size;

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

        ctx->info->tls_size = spill_count;
        bi_install_registers(ctx, l);

        lcra_free(l);
}
