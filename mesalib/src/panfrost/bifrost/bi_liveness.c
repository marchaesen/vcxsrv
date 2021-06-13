/*
 * Copyright (C) 2020 Collabora, Ltd.
 * Copyright (C) 2018-2019 Alyssa Rosenzweig <alyssa@rosenzweig.io>
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

void
bi_liveness_ins_update(uint16_t *live, bi_instr *ins, unsigned max)
{
        /* live_in[s] = GEN[s] + (live_out[s] - KILL[s]) */

        bi_foreach_dest(ins, d) {
                pan_liveness_kill(live, bi_get_node(ins->dest[d]), max,
                                bi_writemask(ins, d));
        }

        bi_foreach_src(ins, src) {
                unsigned count = bi_count_read_registers(ins, src);
                unsigned rmask = (1 << (4 * count)) - 1;
                uint16_t mask = (rmask << (4 * ins->src[src].offset));

                unsigned node = bi_get_node(ins->src[src]);
                pan_liveness_gen(live, node, max, mask);
        }
}

static void
bi_liveness_ins_update_wrap(uint16_t *live, void *ins, unsigned max)
{
        bi_liveness_ins_update(live, (bi_instr *) ins, max);
}

void
bi_compute_liveness(bi_context *ctx)
{
        if (ctx->has_liveness)
                return;

        pan_compute_liveness(&ctx->blocks, bi_max_temp(ctx), bi_liveness_ins_update_wrap);

        ctx->has_liveness = true;
}

/* Once liveness data is no longer valid, call this */

void
bi_invalidate_liveness(bi_context *ctx)
{
        if (ctx->has_liveness)
                pan_free_liveness(&ctx->blocks);

        ctx->has_liveness = false;
}
