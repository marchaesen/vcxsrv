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

/* This pass promotes reads from uniforms from load/store ops to uniform
 * registers if it is beneficial to do so. Normally, this saves both
 * instructions and total register pressure, but it does take a toll on the
 * number of work registers that are available, so this is a balance.
 *
 * To cope, we take as an argument the maximum work register pressure in the
 * program so we allow that many registers through at minimum, to prevent
 * spilling. If we spill anyway, I mean, it's a lose-lose at that point. */

static unsigned
mir_ubo_offset(midgard_instruction *ins)
{
        assert(ins->type == TAG_LOAD_STORE_4);
        assert(OP_IS_UBO_READ(ins->load_store.op));

        /* Grab the offset as the hw understands it */
        unsigned lo = ins->load_store.varying_parameters >> 7;
        unsigned hi = ins->load_store.address;
        unsigned raw = ((hi << 3) | lo);

        /* Account for the op's shift */
        unsigned shift = mir_ubo_shift(ins->load_store.op);
        return (raw << shift);
}

void
midgard_promote_uniforms(compiler_context *ctx, unsigned promoted_count)
{
        mir_foreach_instr_global_safe(ctx, ins) {
                if (ins->type != TAG_LOAD_STORE_4) continue;
                if (!OP_IS_UBO_READ(ins->load_store.op)) continue;

                /* Get the offset. TODO: can we promote unaligned access? */
                unsigned off = mir_ubo_offset(ins);
                if (off & 0xF) continue;

                unsigned address = off / 16;

                /* Check this is UBO 0 */
                if (ins->load_store.arg_1) continue;

                /* Check we're accessing directly */
                if (ins->load_store.arg_2 != 0x1E) continue;

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

                /* Ensure this is a contiguous X-bound mask. It should be since
                 * we haven't done RA and per-component masked UBO reads don't
                 * make much sense. */

                assert(((ins->mask + 1) & ins->mask) == 0);

                /* Check the component count from the mask so we can setup a
                 * swizzle appropriately when promoting. The idea is to ensure
                 * the component count is preserved so RA can be smarter if we
                 * need to spill */

                unsigned nr_components = util_bitcount(ins->mask);

                if (needs_move) {
                        midgard_instruction mov = v_mov(promoted, blank_alu_src, ins->dest);
                        mov.mask = ins->mask;
                        mir_insert_instruction_before(ctx, ins, mov);
                } else {
                        mir_rewrite_index_src_swizzle(ctx, ins->dest,
                                        promoted, swizzle_of(nr_components));
                }

                mir_remove_instruction(ins);
        }
}
