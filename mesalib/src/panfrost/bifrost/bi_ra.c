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
                                                lcra_add_node_interference(l, ins->dest, ins->writemask, i, live[i]);
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
                lcra_alloc_equations(node_count, 1, 8, 16, 1);

        l->class_start[BI_REG_CLASS_WORK] = 0;
        l->class_size[BI_REG_CLASS_WORK] = 64 * 4; /* R0 - R63, all 32-bit */

        bi_foreach_instr_global(ctx, ins) {
                unsigned dest = ins->dest;

                if (!dest || (dest >= node_count))
                        continue;

                l->class[dest] = BI_REG_CLASS_WORK;
                lcra_set_alignment(l, dest, 2); /* 2^2 = 4 */
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

        solution += offset;

        assert((solution & 0x3) == 0);
        unsigned reg = solution / 4;
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
                nir_alu_type T = ins->src_types[src];
                unsigned size = nir_alu_type_get_type_size(T);
                assert(size <= 32); /* TODO: 64-bit */
                unsigned comps_per_reg = 32 / size;
                unsigned components = bi_get_component_count(ins, src);

                for (unsigned i = 0; i < components; ++i) {
                        /* If we're not writing the component, who cares? */
                        if (!bi_writes_component(ins, i))
                                continue;

                        unsigned off = ins->swizzle[src][i] / comps_per_reg;
                        off *= 4; /* 32-bit registers */

                        /* We can't cross register boundaries in a swizzle */
                        if (i == 0)
                                offset = off;
                        else
                                assert(off == offset);

                        ins->swizzle[src][i] %= comps_per_reg;
                }
        }

        ins->src[src] = bi_reg_from_index(l, ins->src[src], offset);
}

static void
bi_adjust_dest_ra(bi_instruction *ins, struct lcra_state *l)
{
        if (ins->dest >= l->node_count)
                return;

        bool vector = (bi_class_props[ins->type] & BI_VECTOR);
        unsigned offset = 0;

        if (!vector) {
                /* Look at the writemask to get an offset, specifically the
                 * trailing zeros */

                unsigned tz = __builtin_ctz(ins->writemask);

                /* Recall writemask is one bit per byte, so tz is in eytes */
                unsigned regs = tz / 4;
                offset = regs * 4;

                /* Adjust writemask to compensate */
                ins->writemask >>= offset;
        }

        ins->dest = bi_reg_from_index(l, ins->dest, offset);

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

void
bi_register_allocate(bi_context *ctx)
{
        struct lcra_state *l = NULL;
        bool success = false;

        do {
                if (l) {
                        lcra_free(l);
                        l = NULL;
                }

                bi_invalidate_liveness(ctx);
                l = bi_allocate_registers(ctx, &success);

                /* TODO: Spilling */
                assert(success);
        } while(!success);

        bi_install_registers(ctx, l);

        lcra_free(l);
}
