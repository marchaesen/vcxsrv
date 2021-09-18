/*
 * Copyright (C) 2021 Collabora, Ltd.
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
#include "bi_builder.h"

/* This optimization pass, intended to run once after code emission but before
 * copy propagation, analyzes direct word-aligned UBO reads and promotes a
 * subset to moves from FAU. It is the sole populator of the UBO push data
 * structure returned back to the command stream. */

static bool
bi_is_ubo(bi_instr *ins)
{
        return (bi_opcode_props[ins->op].message == BIFROST_MESSAGE_LOAD) &&
                (ins->seg == BI_SEG_UBO);
}

static bool
bi_is_direct_aligned_ubo(bi_instr *ins)
{
        return bi_is_ubo(ins) &&
                (ins->src[0].type == BI_INDEX_CONSTANT) &&
                (ins->src[1].type == BI_INDEX_CONSTANT) &&
                ((ins->src[0].value & 0x3) == 0);
}

/* Represents use data for a single UBO */

#define MAX_UBO_WORDS (65536 / 16)

struct bi_ubo_block {
        BITSET_DECLARE(pushed, MAX_UBO_WORDS);
        uint8_t range[MAX_UBO_WORDS];
};

struct bi_ubo_analysis {
        /* Per block analysis */
        unsigned nr_blocks;
        struct bi_ubo_block *blocks;
};

static struct bi_ubo_analysis
bi_analyze_ranges(bi_context *ctx)
{
        struct bi_ubo_analysis res = {
                .nr_blocks = ctx->nir->info.num_ubos + 1,
        };

        res.blocks = calloc(res.nr_blocks, sizeof(struct bi_ubo_block));

        bi_foreach_instr_global(ctx, ins) {
                if (!bi_is_direct_aligned_ubo(ins)) continue;

                unsigned ubo = ins->src[1].value;
                unsigned word = ins->src[0].value / 4;
                unsigned channels = bi_opcode_props[ins->op].sr_count;

                assert(ubo < res.nr_blocks);
                assert(channels > 0 && channels <= 4);

                if (word >= MAX_UBO_WORDS) continue;

                /* Must use max if the same base is read with different channel
                 * counts, which is possible with nir_opt_shrink_vectors */
                uint8_t *range = res.blocks[ubo].range;
                range[word] = MAX2(range[word], channels);
        }

        return res;
}

/* Select UBO words to push. A sophisticated implementation would consider the
 * number of uses and perhaps the control flow to estimate benefit. This is not
 * sophisticated. Select from the last UBO first to prioritize sysvals. */

static void
bi_pick_ubo(struct panfrost_ubo_push *push, struct bi_ubo_analysis *analysis)
{
        for (signed ubo = analysis->nr_blocks - 1; ubo >= 0; --ubo) {
                struct bi_ubo_block *block = &analysis->blocks[ubo];

                for (unsigned r = 0; r < MAX_UBO_WORDS; ++r) {
                        unsigned range = block->range[r];

                        /* Don't push something we don't access */
                        if (range == 0) continue;

                        /* Don't push more than possible */
                        if (push->count > PAN_MAX_PUSH - range)
                                return;

                        for (unsigned offs = 0; offs < range; ++offs) {
                                struct panfrost_ubo_word word = {
                                        .ubo = ubo,
                                        .offset = (r + offs) * 4
                                };

                                push->words[push->count++] = word;
                        }

                        /* Mark it as pushed so we can rewrite */
                        BITSET_SET(block->pushed, r);
                }
        }
}

void
bi_opt_push_ubo(bi_context *ctx)
{
        /* This pass only runs once */
        assert(ctx->info->push.count == 0);

        struct bi_ubo_analysis analysis = bi_analyze_ranges(ctx);
        bi_pick_ubo(&ctx->info->push, &analysis);

        ctx->ubo_mask = 0;

        bi_foreach_instr_global_safe(ctx, ins) {
                if (!bi_is_ubo(ins)) continue;

                unsigned ubo = ins->src[1].value;
                unsigned offset = ins->src[0].value;

                if (!bi_is_direct_aligned_ubo(ins)) {
                        /* The load can't be pushed, so this UBO needs to be
                         * uploaded conventionally */
                        if (ins->src[1].type == BI_INDEX_CONSTANT)
                                ctx->ubo_mask |= BITSET_BIT(ubo);
                        else
                                ctx->ubo_mask = ~0;

                        continue;
                }

                /* Check if we decided to push this */
                assert(ubo < analysis.nr_blocks);
                if (!BITSET_TEST(analysis.blocks[ubo].pushed, offset / 4)) {
                        ctx->ubo_mask |= BITSET_BIT(ubo);
                        continue;
                }

                /* Replace the UBO load with moves from FAU */
                bi_builder b = bi_init_builder(ctx, bi_after_instr(ins));

                unsigned channels = bi_opcode_props[ins->op].sr_count;

                for (unsigned w = 0; w < channels; ++w) {
                        /* FAU is grouped in pairs (2 x 4-byte) */
                        unsigned base =
                                pan_lookup_pushed_ubo(&ctx->info->push, ubo,
                                                      (offset + 4 * w));

                        unsigned fau_idx = (base >> 1);
                        unsigned fau_hi = (base & 1);

                        bi_mov_i32_to(&b,
                                bi_word(ins->dest[0], w),
                                bi_fau(BIR_FAU_UNIFORM | fau_idx, fau_hi));
                }

                bi_remove_instruction(ins);
        }

        free(analysis.blocks);
}
