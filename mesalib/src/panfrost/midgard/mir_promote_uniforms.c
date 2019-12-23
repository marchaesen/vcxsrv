/*
 * Copyright (C) 2019 Collabora, Ltd.
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
 *   Alyssa Rosenzweig <alyssa.rosenzweig@collabora.com>
 */

#include "compiler.h"
#include "util/u_math.h"
#include "util/u_memory.h"

/* This pass promotes reads from uniforms from load/store ops to uniform
 * registers if it is beneficial to do so. Normally, this saves both
 * instructions and total register pressure, but it does take a toll on the
 * number of work registers that are available, so this is a balance.
 *
 * We use a heuristic to determine the ideal count, implemented by
 * mir_work_heuristic, which returns the ideal number of work registers.
 */

static bool
mir_is_promoteable_ubo(midgard_instruction *ins)
{
        /* TODO: promote unaligned access via swizzle? */

        return (ins->type == TAG_LOAD_STORE_4) &&
                (OP_IS_UBO_READ(ins->load_store.op)) &&
                !(ins->constants[0] & 0xF) &&
                !(ins->load_store.arg_1) &&
                (ins->load_store.arg_2 == 0x1E) &&
                ((ins->constants[0] / 16) < 16);
}

static unsigned
mir_promoteable_uniform_count(compiler_context *ctx)
{
        unsigned count = 0;

        mir_foreach_instr_global(ctx, ins) {
                if (mir_is_promoteable_ubo(ins))
                        count = MAX2(count, ins->constants[0] / 16);
        }

        return count;
}

static unsigned
mir_count_live(uint16_t *live, unsigned temp_count)
{
        unsigned count = 0;

        for (unsigned i = 0; i < temp_count; ++i)
                count += util_bitcount(live[i]);

        return count;
}

static unsigned
mir_estimate_pressure(compiler_context *ctx)
{
        mir_invalidate_liveness(ctx);
        mir_compute_liveness(ctx);

        unsigned max_live = 0;

        mir_foreach_block(ctx, block) {
                uint16_t *live = mem_dup(block->live_out, ctx->temp_count * sizeof(uint16_t));

                mir_foreach_instr_in_block_rev(block, ins) {
                        unsigned count = mir_count_live(live, ctx->temp_count);
                        max_live = MAX2(max_live, count);
                        mir_liveness_ins_update(live, ins, ctx->temp_count);
                }

                free(live);
        }

        return DIV_ROUND_UP(max_live, 16);
}

static unsigned
mir_work_heuristic(compiler_context *ctx)
{
        unsigned uniform_count = mir_promoteable_uniform_count(ctx);

        /* If there are 8 or fewer uniforms, it doesn't matter what we do, so
         * allow as many work registers as needed */

        if (uniform_count <= 8)
                return 16;

        /* Otherwise, estimate the register pressure */

        unsigned pressure = mir_estimate_pressure(ctx);

        /* Prioritize not spilling above all else. The relation between the
         * pressure estimate and the actual register pressure is a little
         * murkier than we might like (due to scheduling, pipeline registers,
         * failure to pack vector registers, load/store registers, texture
         * registers...), hence why this is a heuristic parameter */

        if (pressure > 6)
                return 16;

        /* If there's no chance of spilling, prioritize UBOs and thread count */

        return 8;
}

void
midgard_promote_uniforms(compiler_context *ctx)
{
        unsigned work_count = mir_work_heuristic(ctx);
        unsigned promoted_count = 24 - work_count;

        mir_foreach_instr_global_safe(ctx, ins) {
                if (!mir_is_promoteable_ubo(ins)) continue;

                unsigned off = ins->constants[0];
                unsigned address = off / 16;

                /* Check if it's a promotable range */
                unsigned uniform_reg = 23 - address;

                if (address >= promoted_count) continue;

                /* It is, great! Let's promote */

                ctx->uniform_cutoff = MAX2(ctx->uniform_cutoff, address + 1);
                unsigned promoted = SSA_FIXED_REGISTER(uniform_reg);

                /* We do need the move for safety for a non-SSA dest, or if
                 * we're being fed into a special class */

                bool needs_move = ins->dest & IS_REG;
                needs_move |= mir_special_index(ctx, ins->dest);

                if (needs_move) {
                        midgard_instruction mov = v_mov(promoted, ins->dest);

                        if (ins->load_64)
                                mov.alu.reg_mode = midgard_reg_mode_64;

                        mir_set_bytemask(&mov, mir_bytemask(ins));
                        mir_insert_instruction_before(ctx, ins, mov);
                } else {
                        mir_rewrite_index_src(ctx, ins->dest, promoted);
                }

                mir_remove_instruction(ins);
        }
}
