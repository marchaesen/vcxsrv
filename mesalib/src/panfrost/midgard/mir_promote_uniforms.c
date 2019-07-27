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

/* This pass promotes reads from uniforms from load/store ops to uniform
 * registers if it is beneficial to do so. Normally, this saves both
 * instructions and total register pressure, but it does take a toll on the
 * number of work registers that are available, so this is a balance.
 *
 * To cope, we take as an argument the maximum work register pressure in the
 * program so we allow that many registers through at minimum, to prevent
 * spilling. If we spill anyway, I mean, it's a lose-lose at that point. */

void
midgard_promote_uniforms(compiler_context *ctx, unsigned register_pressure)
{
        /* For our purposes, pressure is capped at the number of vec4 work
         * registers, not live values which would consider spills */
        register_pressure = MAX2(register_pressure, 16);

        mir_foreach_instr_global_safe(ctx, ins) {
                if (ins->type != TAG_LOAD_STORE_4) continue;
                if (!OP_IS_UBO_READ(ins->load_store.op)) continue;

                unsigned lo = ins->load_store.varying_parameters >> 7;
                unsigned hi = ins->load_store.address;

                /* TODO: Combine fields logically */
                unsigned address = (hi << 3) | lo;

                /* Check this is UBO 0 */
                if (ins->load_store.unknown & 0xF) continue;

                /* Check we're accessing directly */
                if (ins->load_store.unknown != 0x1E00) continue;

                /* Check if it's a promotable range */
                unsigned uniform_reg = 23 - address;

                if (address > 16) continue;
                if (register_pressure > uniform_reg) continue;

                /* It is, great! Let's promote */

                ctx->uniform_cutoff = MAX2(ctx->uniform_cutoff, address + 1);
                unsigned promoted = SSA_FIXED_REGISTER(uniform_reg);

                /* We do need the move for safety for a non-SSA dest, or if
                 * we're being fed into a special class */

                bool needs_move = ins->ssa_args.dest & IS_REG;
                needs_move |= mir_special_index(ctx, ins->ssa_args.dest);

                if (needs_move) {
                        midgard_instruction mov = v_mov(promoted, blank_alu_src, ins->ssa_args.dest);
                        mir_insert_instruction_before(ins, mov);
                } else {
                        mir_rewrite_index_src(ctx, ins->ssa_args.dest, promoted);
                }

                mir_remove_instruction(ins);
        }
}
