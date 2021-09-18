/*
 * Copyright (C) 2018 Alyssa Rosenzweig
 * Copyright (C) 2019-2020 Collabora, Ltd.
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

/* A simple liveness-based dead code elimination pass. */

void
bi_opt_dead_code_eliminate(bi_context *ctx)
{
        unsigned temp_count = bi_max_temp(ctx);

        bi_invalidate_liveness(ctx);
        bi_compute_liveness(ctx);

        bi_foreach_block_rev(ctx, block) {
                uint8_t *live = rzalloc_array(block, uint8_t, temp_count);

                bi_foreach_successor(block, succ) {
                        for (unsigned i = 0; i < temp_count; ++i)
                                live[i] |= succ->live_in[i];
                }

                bi_foreach_instr_in_block_safe_rev(block, ins) {
                        bool all_null = true;

                        bi_foreach_dest(ins, d) {
                                unsigned index = bi_get_node(ins->dest[d]);

                                if (index < temp_count && !(live[index] & bi_writemask(ins, d)))
                                        ins->dest[d] = bi_null();

                                all_null &= bi_is_null(ins->dest[d]);
                        }

                        if (all_null && !bi_side_effects(ins->op))
                                bi_remove_instruction(ins);
                        else
                                bi_liveness_ins_update(live, ins, temp_count);
                }

                ralloc_free(block->live_in);
                block->live_in = live;
        }
}

/* Post-RA liveness-based dead code analysis to clean up results of bundling */

uint64_t
bi_postra_liveness_ins(uint64_t live, bi_instr *ins)
{
        bi_foreach_dest(ins, d) {
                if (ins->dest[d].type == BI_INDEX_REGISTER) {
                        unsigned nr = bi_count_write_registers(ins, d);
                        unsigned reg = ins->dest[d].value;
                        live &= ~(BITFIELD64_MASK(nr) << reg);
                }
        }

        bi_foreach_src(ins, s) {
                if (ins->src[s].type == BI_INDEX_REGISTER) {
                        unsigned nr = bi_count_read_registers(ins, s);
                        unsigned reg = ins->src[s].value;
                        live |= (BITFIELD64_MASK(nr) << reg);
                }
        }

        return live;
}

static bool
bi_postra_liveness_block(bi_block *blk)
{
        bi_foreach_successor(blk, succ)
                blk->reg_live_out |= succ->reg_live_in;

        uint64_t live = blk->reg_live_out;

        bi_foreach_instr_in_block_rev(blk, ins)
                live = bi_postra_liveness_ins(live, ins);

        bool progress = blk->reg_live_in != live;
        blk->reg_live_in = live;
        return progress;
}

/* Globally, liveness analysis uses a fixed-point algorithm based on a
 * worklist. We initialize a work list with the exit block. We iterate the work
 * list to compute live_in from live_out for each block on the work list,
 * adding the predecessors of the block to the work list if we made progress.
 */

void
bi_postra_liveness(bi_context *ctx)
{
        struct set *work_list = _mesa_set_create(NULL,
                        _mesa_hash_pointer,
                        _mesa_key_pointer_equal);

        struct set *visited = _mesa_set_create(NULL,
                        _mesa_hash_pointer,
                        _mesa_key_pointer_equal);

        struct set_entry *cur;
        cur = _mesa_set_add(work_list, pan_exit_block(&ctx->blocks));

        bi_foreach_block(ctx, block) {
                block->reg_live_out = block->reg_live_in = 0;
        }

        do {
                bi_block *blk = (struct bi_block *) cur->key;
                _mesa_set_remove(work_list, cur);

                /* Update its liveness information */
                bool progress = bi_postra_liveness_block(blk);

                /* If we made progress, we need to process the predecessors */

                if (progress || !_mesa_set_search(visited, blk)) {
                        bi_foreach_predecessor((blk), pred)
                                _mesa_set_add(work_list, pred);
                }

                _mesa_set_add(visited, blk);
        } while((cur = _mesa_set_next_entry(work_list, NULL)) != NULL);

        _mesa_set_destroy(visited, NULL);
        _mesa_set_destroy(work_list, NULL);
}

void
bi_opt_dce_post_ra(bi_context *ctx)
{
        bi_postra_liveness(ctx);

        bi_foreach_block_rev(ctx, block) {
                uint64_t live = block->reg_live_out;

                bi_foreach_instr_in_block_rev(block, ins) {
                        bi_foreach_dest(ins, d) {
                                if (ins->dest[d].type != BI_INDEX_REGISTER)
                                        continue;

                                unsigned nr = bi_count_write_registers(ins, d);
                                unsigned reg = ins->dest[d].value;
                                uint64_t mask = (BITFIELD64_MASK(nr) << reg);
                                bool cullable = (ins->op != BI_OPCODE_BLEND);

                                if (!(live & mask) && cullable)
                                        ins->dest[d] = bi_null();
                        }

                        live = bi_postra_liveness_ins(live, ins);
                }
        }
}
