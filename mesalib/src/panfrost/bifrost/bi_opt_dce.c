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

bool
bi_opt_dead_code_eliminate(bi_context *ctx, bi_block *block)
{
        bool progress = false;
        unsigned temp_count = bi_max_temp(ctx);

        bi_invalidate_liveness(ctx);
        bi_compute_liveness(ctx);

        uint16_t *live = mem_dup(block->base.live_out, temp_count * sizeof(uint16_t));

        bi_foreach_instr_in_block_safe_rev(block, ins) {
                if (ins->dest && !(ins->dest & BIR_SPECIAL)) {
                        if (!live[ins->dest]) {
                                bi_remove_instruction(ins);
                                progress |= true;
                        }
                }

                bi_liveness_ins_update(live, ins, temp_count);
        }

        free(live);

        return progress;
}
