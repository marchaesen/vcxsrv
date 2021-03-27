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

/* A simple liveness-based dead code elimination pass. In 'soft' mode, dead
 * instructions are kept but write to null, which is required for correct
 * operation post-schedule pass (where dead instructions correspond to
 * instructions whose destinations are consumed immediately as a passthrough
 * register. If the destinations are not garbage collected, impossible register
 * encodings will result.)
 */

bool
bi_opt_dead_code_eliminate(bi_context *ctx, bool soft)
{
        bool progress = false;
        unsigned temp_count = bi_max_temp(ctx);

        bi_invalidate_liveness(ctx);
        bi_compute_liveness(ctx);

        bi_foreach_block(ctx, _block) {
                bi_block *block = (bi_block *) _block;
                uint16_t *live = mem_dup(block->base.live_out, temp_count * sizeof(uint16_t));

                bi_foreach_instr_in_block_safe_rev(block, ins) {
                        bool all_null = true;

                        bi_foreach_dest(ins, d) {
                                unsigned index = bi_get_node(ins->dest[d]);

                                if (index < temp_count && !(live[index] & bi_writemask(ins, d))) {
                                        ins->dest[d] = bi_null();
                                        progress = true;
                                }

                                all_null &= bi_is_null(ins->dest[d]);
                        }

                        if (all_null && !soft && !bi_side_effects(ins->op)) {
                                bi_remove_instruction(ins);
                                progress = true;
                        }

                        bi_liveness_ins_update(live, ins, temp_count);
                }

                free(live);
        }

        return progress;
}
