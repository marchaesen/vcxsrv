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
#include "bi_print.h"
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

                        if (ins->dest && (ins->dest < l->node_count)) {
                                for (unsigned i = 1; i < l->node_count; ++i) {
                                        if (live[i])
                                                lcra_add_node_interference(l, ins->dest, bi_writemask(ins), i, live[i]);
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
                l->class_size[BI_REG_CLASS_WORK] = 63 * 4;
        }

        bi_foreach_instr_global(ctx, ins) {
                unsigned dest = ins->dest;

                /* Blend shaders expect the src colour to be in r0-r3 */
                if (ins->type == BI_BLEND && !ctx->is_blend)
                        l->solutions[ins->src[0]] = 0;

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

static unsigned
bi_reg_from_index(struct lcra_state *l, unsigned index, unsigned offset)
{
        /* Did we run RA for this index at all */
        if (index >= l->node_count)
                return index;

        /* LCRA didn't bother solving this index (how lazy!) */
        signed solution = l->solutions[index];
        if (solution < 0)
                return index;

        assert((solution & 0x3) == 0);
        unsigned reg = solution / 4;
        reg += offset;

        return BIR_INDEX_REGISTER | reg;
}

static void
bi_adjust_src_ra(bi_instruction *ins, struct lcra_state *l, unsigned src)
{
        if (ins->src[src] >= l->node_count)
                return;

        bool vector = (bi_class_props[ins->type] & BI_VECTOR) && src == 0;
        unsigned offset = 0;

        if (vector) {
                /* TODO: Do we do anything here? */
        } else {
                /* Use the swizzle as component select */
                unsigned components = bi_get_component_count(ins, src);

                nir_alu_type T = ins->src_types[src];
                unsigned size = nir_alu_type_get_type_size(T);
 
                /* TODO: 64-bit? */
                unsigned components_per_word = MAX2(32 / size, 1);

                for (unsigned i = 0; i < components; ++i) {
                        unsigned off = ins->swizzle[src][i] / components_per_word;

                        /* We can't cross register boundaries in a swizzle */
                        if (i == 0)
                                offset = off;
                        else
                                assert(off == offset);

                        ins->swizzle[src][i] %= components_per_word;
                }
        }

        ins->src[src] = bi_reg_from_index(l, ins->src[src], offset);
}

static void
bi_adjust_dest_ra(bi_instruction *ins, struct lcra_state *l)
{
        if (ins->dest >= l->node_count)
                return;

        ins->dest = bi_reg_from_index(l, ins->dest, ins->dest_offset);
        ins->dest_offset = 0;
}

static void
bi_install_registers(bi_context *ctx, struct lcra_state *l)
{
        bi_foreach_instr_global(ctx, ins) {
                bi_adjust_dest_ra(ins, l);

                bi_foreach_src(ins, s)
                        bi_adjust_src_ra(ins, l, s);
        }
}

static void
bi_rewrite_index_src_single(bi_instruction *ins, unsigned old, unsigned new)
{
        bi_foreach_src(ins, i) {
                if (ins->src[i] == old)
                        ins->src[i] = new;
        }
}

static bi_instruction
bi_spill(unsigned node, uint64_t offset, unsigned channels)
{
        bi_instruction store = {
                .type = BI_STORE,
                .segment = BI_SEGMENT_TLS,
                .vector_channels = channels,
                .src = {
                        node,
                        BIR_INDEX_CONSTANT,
                        BIR_INDEX_CONSTANT | 32,
                },
                .src_types = {
                        nir_type_uint32,
                        nir_type_uint32,
                        nir_type_uint32
                },
                .constant = { .u64 = offset },
        };

        return store;
}

static bi_instruction
bi_fill(unsigned node, uint64_t offset, unsigned channels)
{
        bi_instruction load = {
                .type = BI_LOAD,
                .segment = BI_SEGMENT_TLS,
                .vector_channels = channels,
                .dest = node,
                .dest_type = nir_type_uint32,
                .src = {
                        BIR_INDEX_CONSTANT,
                        BIR_INDEX_CONSTANT | 32,
                },
                .src_types = {
                        nir_type_uint32,
                        nir_type_uint32
                },
                .constant = { .u64 = offset },
        };

        return load;
}

/* Get the single instruction in a singleton clause. Precondition: clause
 * contains exactly 1 instruction.
 *
 * More complex scheduling implies tougher constraints on spilling. We'll cross
 * that bridge when we get to it. For now, just grab the one and only
 * instruction in the clause */

static bi_instruction *
bi_unwrap_singleton(bi_clause *clause)
{
       assert(clause->bundle_count == 1);
       assert((clause->bundles[0].fma != NULL) ^ (clause->bundles[0].add != NULL));

       return clause->bundles[0].fma ? clause->bundles[0].fma
               : clause->bundles[0].add;
}

static inline void
bi_insert_singleton(void *memctx, bi_clause *cursor, bi_block *block,
                bi_instruction ins, bool before)
{
        bi_instruction *uins = rzalloc(memctx, bi_instruction);
        memcpy(uins, &ins, sizeof(ins));

        /* Get the instruction to pivot around. Should be first/last of clause
         * depending on before setting, those coincide for singletons */
        bi_instruction *cursor_ins = bi_unwrap_singleton(cursor);

        bi_clause *clause = bi_make_singleton(memctx, uins,
                        block, 0, (1 << 0), true);

        if (before) {
                list_addtail(&clause->link, &cursor->link);
                list_addtail(&uins->link, &cursor_ins->link);
        } else {
                list_add(&clause->link, &cursor->link);
                list_add(&uins->link, &cursor_ins->link);
        }
}

/* If register allocation fails, find the best spill node */

static signed
bi_choose_spill_node(bi_context *ctx, struct lcra_state *l)
{
        /* Pick a node satisfying bi_spill_register's preconditions */

        bi_foreach_instr_global(ctx, ins) {
                if (ins->no_spill)
                        lcra_set_node_spill_cost(l, ins->dest, -1);
        }

        for (unsigned i = PAN_IS_REG; i < l->node_count; i += 2)
                lcra_set_node_spill_cost(l, i, -1);

        return lcra_get_best_spill_node(l);
}

/* Once we've chosen a spill node, spill it. Precondition: node is a valid
 * SSA node in the non-optimized scheduled IR that was not already
 * spilled (enforced by bi_choose_spill_node). Returns bytes spilled */

static unsigned
bi_spill_register(bi_context *ctx, unsigned node, unsigned offset)
{
        assert(!(node & PAN_IS_REG));

        unsigned channels = 1;

        /* Spill after every store */
        bi_foreach_block(ctx, _block) {
                bi_block *block = (bi_block *) _block;
                bi_foreach_clause_in_block_safe(block, clause) {
                        bi_instruction *ins = bi_unwrap_singleton(clause);

                        if (ins->dest != node) continue;

                        ins->dest = bi_make_temp(ctx);
                        ins->no_spill = true;
                        channels = MAX2(channels, ins->vector_channels);

                        bi_instruction st = bi_spill(ins->dest, offset, channels);
                        bi_insert_singleton(ctx, clause, block, st, false);
                        ctx->spills++;
                }
        }

        /* Fill before every use */
        bi_foreach_block(ctx, _block) {
                bi_block *block = (bi_block *) _block;
                bi_foreach_clause_in_block_safe(block, clause) {
                        bi_instruction *ins = bi_unwrap_singleton(clause);
                        if (!bi_has_arg(ins, node)) continue;

                        /* Don't rewrite spills themselves */
                        if (ins->segment == BI_SEGMENT_TLS) continue;

                        unsigned index = bi_make_temp(ctx);

                        bi_instruction ld = bi_fill(index, offset, channels);
                        ld.no_spill = true;
                        bi_insert_singleton(ctx, clause, block, ld, true);

                        /* Rewrite to use */
                        bi_rewrite_index_src_single(ins, node, index);
                        ctx->fills++;
                }
        }

        return (channels * 4);
}

void
bi_register_allocate(bi_context *ctx)
{
        struct lcra_state *l = NULL;
        bool success = false;

        unsigned iter_count = 100; /* max iterations */

        /* Number of bytes of memory we've spilled into */
        unsigned spill_count = 0;

        /* For instructions that both read and write from a data register, it's
         * the *same* data register. We enforce that constraint by just doing a
         * quick rewrite. TODO: are there cases where this causes RA to have no
         * solutions due to copyprop? */
        bi_foreach_instr_global(ctx, ins) {
                unsigned props = bi_class_props[ins->type];
                unsigned both = BI_DATA_REG_SRC | BI_DATA_REG_DEST;
                if ((props & both) != both) continue;

                assert(ins->src[0] & PAN_IS_REG);
                bi_rewrite_uses(ctx, ins->dest, 0, ins->src[0], 0);
                ins->dest = ins->src[0];
        }

        do {
                if (l) {
                        signed spill_node = bi_choose_spill_node(ctx, l);
                        lcra_free(l);
                        l = NULL;


                        if (spill_node == -1)
                                unreachable("Failed to choose spill node\n");

                        spill_count += bi_spill_register(ctx, spill_node, spill_count);
                }

                bi_invalidate_liveness(ctx);
                l = bi_allocate_registers(ctx, &success);
        } while(!success && ((iter_count--) > 0));

        assert(success);

        ctx->tls_size = spill_count;
        bi_install_registers(ctx, l);

        lcra_free(l);
}
