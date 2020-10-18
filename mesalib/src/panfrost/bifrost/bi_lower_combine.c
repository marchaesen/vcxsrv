/*
 * Copyright (C) 2020 Collabora, Ltd.
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

/* NIR creates vectors as vecN ops, which we represent by a synthetic
 * BI_COMBINE instruction, e.g.:
 *
 *      v = combine x, y, z, w
 *
 * These combines need to be lowered by the pass in this file. Fix a given
 * source at component c.
 *
 * First suppose the source is SSA. If it is also scalar, then we may rewrite
 * the destination of the generating instruction (unique by SSA+scalar) to
 * write to v.c, and rewrite each of its uses to swizzle out .c instead of .x
 * (the original by scalar). If it is vector, there are two cases. If the
 * component c is `x`, we are accessing v.x, and each of the succeeding
 * components y, z... up to the last component of the vector are accessed
 * sequentially, then we may perform the same rewrite. If this is not the case,
 * rewriting would require more complex vector features, so we fallback on a
 * move.
 *
 * Otherwise is the source is not SSA, we also fallback on a move. We could
 * probably do better.
 */

static void
bi_combine_mov32(bi_context *ctx, bi_instruction *parent, unsigned comp, unsigned R)
{
        bi_instruction move = {
                .type = BI_MOV,
                .dest = R,
                .dest_type = nir_type_uint32,
                .dest_offset = comp,
                .src = { parent->src[comp] },
                .src_types = { nir_type_uint32 },
                .swizzle = { { parent->swizzle[comp][0] } }
        };

        bi_emit_before(ctx, parent, move);
}

static void
bi_combine_sel16(bi_context *ctx, bi_instruction *parent, unsigned comp, unsigned R)
{
        bi_instruction sel = {
                .type = BI_SELECT,
                .dest = R,
                .dest_type = nir_type_uint32,
                .dest_offset = comp >> 1,
                .src = { parent->src[comp], parent->src[comp + 1] },
                .src_types = { nir_type_uint16, nir_type_uint16 },
                .swizzle = {
                        { parent->swizzle[comp][0] },
                        { parent->swizzle[comp + 1][0] },
                }
        };

        /* In case we have a combine from a vec3 */
        if (!sel.src[1])
                sel.src[1] = BIR_INDEX_ZERO;

        bi_emit_before(ctx, parent, sel);
}

/* Copies result of combine from the temp R to the instruction destination,
 * given a bitsize sz */

static void
bi_combine_copy(bi_context *ctx, bi_instruction *ins, unsigned R, unsigned sz)
{
        bi_foreach_src(ins, s) {
                if (!ins->src[s])
                        continue;

                /* Iterate by 32-bits */
                unsigned shift = (sz == 8) ? 2 :
                        (sz == 16) ? 1 : 0;

                if (s & ((1 << shift) - 1))
                        continue;

                bi_instruction copy = {
                        .type = BI_MOV,
                        .dest = ins->dest,
                        .dest_type = nir_type_uint32,
                        .dest_offset = s >> shift,
                        .src = { R },
                        .src_types = { nir_type_uint32 },
                        .swizzle = { { s >> shift } }
                };

                bi_emit_before(ctx, ins, copy);
        }
}

void
bi_lower_combine(bi_context *ctx, bi_block *block)
{
        bi_foreach_instr_in_block_safe(block, ins) {
                if (ins->type != BI_COMBINE) continue;

                /* If a register COMBINE reads its own output, we need a
                 * temporary move to allow for swapping. TODO: Could do a bit
                 * better for pairwise swaps of 16-bit vectors */
                bool reads_self = false;

                bi_foreach_src(ins, s) {
                        if(ins->src[s] == ins->dest)
                                reads_self = true;
                }

                bool needs_rewrite = !(ins->dest & PAN_IS_REG);
                bool needs_copy = (ins->dest & PAN_IS_REG) && reads_self;
                bool needs_temp = needs_rewrite || needs_copy;

                unsigned R = needs_temp ? bi_make_temp_reg(ctx) : ins->dest;
                unsigned sz = nir_alu_type_get_type_size(ins->dest_type);

                bi_foreach_src(ins, s) {
                        /* We're done early for vec2/3 */
                        if (!ins->src[s])
                                continue;

                        if (sz == 32)
                                bi_combine_mov32(ctx, ins, s, R);
                        else if (sz == 16) {
                                bi_combine_sel16(ctx, ins, s, R);
                                s++;
                        } else {
                                unreachable("Unknown COMBINE size");
                        }
                }

                if (needs_rewrite)
                        bi_rewrite_uses(ctx, ins->dest, 0, R, 0);
                else if (needs_copy)
                        bi_combine_copy(ctx, ins, R, sz);

                bi_remove_instruction(ins);
        }
}
