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
#include "bi_builder.h"
#include "util/u_memory.h"

struct lcra_state {
        unsigned node_count;
        uint64_t *affinity;

        /* Linear constraints imposed. Nested array sized upfront, organized as
         * linear[node_left][node_right]. That is, calculate indices as:
         *
         * Each element is itself a bit field denoting whether (c_j - c_i) bias
         * is present or not, including negative biases.
         *
         * Note for Bifrost, there are 4 components so the bias is in range
         * [-3, 3] so encoded by 8-bit field. */

        uint8_t *linear;

        /* Before solving, forced registers; after solving, solutions. */
        unsigned *solutions;

        /** Node which caused register allocation to fail */
        unsigned spill_node;
};

/* This module is an implementation of "Linearly Constrained
 * Register Allocation". The paper is available in PDF form
 * (https://people.collabora.com/~alyssa/LCRA.pdf) as well as Markdown+LaTeX
 * (https://gitlab.freedesktop.org/alyssa/lcra/blob/master/LCRA.md)
 */

static struct lcra_state *
lcra_alloc_equations(unsigned node_count)
{
        struct lcra_state *l = calloc(1, sizeof(*l));

        l->node_count = node_count;

        l->linear = calloc(sizeof(l->linear[0]), node_count * node_count);
        l->solutions = calloc(sizeof(l->solutions[0]), node_count);
        l->affinity = calloc(sizeof(l->affinity[0]), node_count);

        memset(l->solutions, ~0, sizeof(l->solutions[0]) * node_count);

        return l;
}

static void
lcra_free(struct lcra_state *l)
{
        free(l->linear);
        free(l->affinity);
        free(l->solutions);
        free(l);
}

static void
lcra_add_node_interference(struct lcra_state *l, unsigned i, unsigned cmask_i, unsigned j, unsigned cmask_j)
{
        if (i == j)
                return;

        uint8_t constraint_fw = 0;
        uint8_t constraint_bw = 0;

        for (unsigned D = 0; D < 4; ++D) {
                if (cmask_i & (cmask_j << D)) {
                        constraint_bw |= (1 << (3 + D));
                        constraint_fw |= (1 << (3 - D));
                }

                if (cmask_i & (cmask_j >> D)) {
                        constraint_fw |= (1 << (3 + D));
                        constraint_bw |= (1 << (3 - D));
                }
        }

        l->linear[j * l->node_count + i] |= constraint_fw;
        l->linear[i * l->node_count + j] |= constraint_bw;
}

static bool
lcra_test_linear(struct lcra_state *l, unsigned *solutions, unsigned i)
{
        uint8_t *row = &l->linear[i * l->node_count];
        signed constant = solutions[i];

        for (unsigned j = 0; j < l->node_count; ++j) {
                if (solutions[j] == ~0) continue;

                signed lhs = solutions[j] - constant;

                if (lhs < -3 || lhs > 3)
                        continue;

                if (row[j] & (1 << (lhs + 3)))
                        return false;
        }

        return true;
}

static bool
lcra_solve(struct lcra_state *l)
{
        for (unsigned step = 0; step < l->node_count; ++step) {
                if (l->solutions[step] != ~0) continue;
                if (l->affinity[step] == 0) continue;

                bool succ = false;

                u_foreach_bit64(r, l->affinity[step]) {
                        l->solutions[step] = r;

                        if (lcra_test_linear(l, l->solutions, step)) {
                                succ = true;
                                break;
                        }
                }

                /* Out of registers - prepare to spill */
                if (!succ) {
                        l->spill_node = step;
                        return false;
                }
        }

        return true;
}

/* Register spilling is implemented with a cost-benefit system. Costs are set
 * by the user. Benefits are calculated from the constraints. */

static unsigned
lcra_count_constraints(struct lcra_state *l, unsigned i)
{
        unsigned count = 0;
        uint8_t *constraints = &l->linear[i * l->node_count];

        for (unsigned j = 0; j < l->node_count; ++j)
                count += util_bitcount(constraints[j]);

        return count;
}

/* Construct an affinity mask such that the vector with `count` elements does
 * not intersect any of the registers in the bitset `clobber`. In other words,
 * an allocated register r needs to satisfy for each i < count: a + i != b.
 * Equivalently that's a != b - i, so we need a \ne { b - i : i < n }. For the
 * entire clobber set B, we need a \ne union b \in B { b - i : i < n }, where
 * that union is the desired clobber set. That may be written equivalently as
 * the union over i < n of (B - i), where subtraction is defined elementwise
 * and corresponds to a shift of the entire bitset.
 *
 * EVEN_BITS_MASK is an affinity mask for aligned register pairs. Interpreted
 * as a bit set, it is { x : 0 <= x < 64 if x is even }
 */

#define EVEN_BITS_MASK (0x5555555555555555ull)

static uint64_t
bi_make_affinity(uint64_t clobber, unsigned count, bool split_file)
{
        uint64_t clobbered = 0;

        for (unsigned i = 0; i < count; ++i)
                clobbered |= (clobber >> i);

        /* Don't allocate past the end of the register file */
        if (count > 1) {
                unsigned excess = count - 1;
                uint64_t mask = BITFIELD_MASK(excess);
                clobbered |= mask << (64 - excess);

                if (split_file)
                        clobbered |= mask << (16 - excess);
        }

        /* Don't allocate the middle if we split out the middle */
        if (split_file)
                clobbered |= BITFIELD64_MASK(32) << 16;

        /* We can use a register iff it's not clobberred */
        return ~clobbered;
}

static void
bi_mark_interference(bi_block *block, struct lcra_state *l, uint8_t *live, uint64_t preload_live, unsigned node_count, bool is_blend, bool split_file, bool aligned_sr)
{
        bi_foreach_instr_in_block_rev(block, ins) {
                /* Mark all registers live after the instruction as
                 * interfering with the destination */

                bi_foreach_dest(ins, d) {
                        unsigned node = bi_get_node(ins->dest[d]);

                        if (node >= node_count)
                                continue;

                        /* Don't allocate to anything that's read later as a
                         * preloaded register. The affinity is the intersection
                         * of affinity masks for each write. Since writes have
                         * offsets, but the affinity is for the whole node, we
                         * need to offset the affinity opposite the write
                         * offset, so we shift right. */
                        unsigned count = bi_count_write_registers(ins, d);
                        unsigned offset = ins->dest[d].offset;
                        uint64_t affinity = bi_make_affinity(preload_live, count, split_file);

                        /* Valhall needs >= 64-bit staging writes to be pair-aligned */
                        if (aligned_sr && count >= 2)
                                affinity &= EVEN_BITS_MASK;

                        l->affinity[node] &= (affinity >> offset);

                        for (unsigned i = 0; i < node_count; ++i) {
                                if (live[i]) {
                                        lcra_add_node_interference(l, node,
                                                        bi_writemask(ins, d), i, live[i]);
                                }
                        }
                }

                /* Valhall needs >= 64-bit staging reads to be pair-aligned */
                if (aligned_sr && bi_count_read_registers(ins, 0) >= 2) {
                        unsigned node = bi_get_node(ins->src[0]);

                        if (node < node_count)
                                l->affinity[node] &= EVEN_BITS_MASK;
                }

                if (!is_blend && ins->op == BI_OPCODE_BLEND) {
                        /* Blend shaders might clobber r0-r15, r48. */
                        uint64_t clobber = BITFIELD64_MASK(16) | BITFIELD64_BIT(48);

                        for (unsigned i = 0; i < node_count; ++i) {
                                if (live[i])
                                        l->affinity[i] &= ~clobber;
                        }
                }

                /* Update live_in */
                preload_live = bi_postra_liveness_ins(preload_live, ins);
                bi_liveness_ins_update(live, ins, node_count);
        }

        block->reg_live_in = preload_live;
}

static void
bi_compute_interference(bi_context *ctx, struct lcra_state *l, bool full_regs)
{
        unsigned node_count = bi_max_temp(ctx);

        bi_compute_liveness(ctx);
        bi_postra_liveness(ctx);

        bi_foreach_block_rev(ctx, blk) {
                uint8_t *live = mem_dup(blk->live_out, node_count);

                bi_mark_interference(blk, l, live, blk->reg_live_out,
                                node_count, ctx->inputs->is_blend, !full_regs,
                                ctx->arch >= 9);

                free(live);
        }
}

static struct lcra_state *
bi_allocate_registers(bi_context *ctx, bool *success, bool full_regs)
{
        unsigned node_count = bi_max_temp(ctx);
        struct lcra_state *l = lcra_alloc_equations(node_count);

        /* Blend shaders are restricted to R0-R15. Other shaders at full
         * occupancy also can access R48-R63. At half occupancy they can access
         * the whole file. */

        uint64_t default_affinity =
                ctx->inputs->is_blend ? BITFIELD64_MASK(16) :
                full_regs ? BITFIELD64_MASK(64) :
                (BITFIELD64_MASK(16) | (BITFIELD64_MASK(16) << 48));

        bi_foreach_instr_global(ctx, ins) {
                bi_foreach_dest(ins, d) {
                        unsigned dest = bi_get_node(ins->dest[d]);

                        if (dest < node_count)
                                l->affinity[dest] = default_affinity;
                }

                /* Blend shaders expect the src colour to be in r0-r3 */
                if (ins->op == BI_OPCODE_BLEND &&
                    !ctx->inputs->is_blend) {
                        unsigned node = bi_get_node(ins->src[0]);
                        assert(node < node_count);
                        l->solutions[node] = 0;

                        /* Dual source blend input in r4-r7 */
                        node = bi_get_node(ins->src[4]);
                        if (node < node_count)
                                l->solutions[node] = 4;
                }
        }

        bi_compute_interference(ctx, l, full_regs);

        *success = lcra_solve(l);

        return l;
}

static bi_index
bi_reg_from_index(bi_context *ctx, struct lcra_state *l, bi_index index)
{
        /* Offsets can only be applied when we register allocated an index, or
         * alternatively for FAU's encoding */

        ASSERTED bool is_offset = (index.offset > 0) &&
                (index.type != BI_INDEX_FAU);
        unsigned node_count = bi_max_temp(ctx);

        /* Did we run RA for this index at all */
        if (bi_get_node(index) >= node_count) {
                assert(!is_offset);
                return index;
        }

        /* LCRA didn't bother solving this index (how lazy!) */
        signed solution = l->solutions[bi_get_node(index)];
        if (solution < 0) {
                assert(!is_offset);
                return index;
        }

        /* todo: do we want to compose with the subword swizzle? */
        bi_index new_index = bi_register(solution + index.offset);
        new_index.swizzle = index.swizzle;
        new_index.abs = index.abs;
        new_index.neg = index.neg;
        return new_index;
}

/* Dual texture instructions write to two sets of staging registers, modeled as
 * two destinations in the IR. The first set is communicated with the usual
 * staging register mechanism. The second set is encoded in the texture
 * operation descriptor. This is quite unusual, and requires the following late
 * fixup.
 */
static void
bi_fixup_dual_tex_register(bi_instr *I)
{
        assert(I->dest[1].type == BI_INDEX_REGISTER);
        assert(I->src[3].type == BI_INDEX_CONSTANT);

        struct bifrost_dual_texture_operation desc = {
                .secondary_register = I->dest[1].value
        };

        I->src[3].value |= bi_dual_tex_as_u32(desc);
}

static void
bi_install_registers(bi_context *ctx, struct lcra_state *l)
{
        bi_foreach_instr_global(ctx, ins) {
                bi_foreach_dest(ins, d)
                        ins->dest[d] = bi_reg_from_index(ctx, l, ins->dest[d]);

                bi_foreach_src(ins, s)
                        ins->src[s] = bi_reg_from_index(ctx, l, ins->src[s]);

                if (ins->op == BI_OPCODE_TEXC && !bi_is_null(ins->dest[1]))
                        bi_fixup_dual_tex_register(ins);
        }
}

static void
bi_rewrite_index_src_single(bi_instr *ins, bi_index old, bi_index new)
{
        bi_foreach_src(ins, i) {
                if (bi_is_equiv(ins->src[i], old)) {
                        ins->src[i].type = new.type;
                        ins->src[i].reg = new.reg;
                        ins->src[i].value = new.value;
                }
        }
}

/* If register allocation fails, find the best spill node */

static signed
bi_choose_spill_node(bi_context *ctx, struct lcra_state *l)
{
        /* Pick a node satisfying bi_spill_register's preconditions */
        BITSET_WORD *no_spill = calloc(sizeof(BITSET_WORD), BITSET_WORDS(l->node_count));

        bi_foreach_instr_global(ctx, ins) {
                bi_foreach_dest(ins, d) {
                        unsigned node = bi_get_node(ins->dest[d]);

                        if (node < l->node_count && ins->no_spill)
                                BITSET_SET(no_spill, node);
                }
        }

        unsigned best_benefit = 0.0;
        signed best_node = -1;

        for (unsigned i = 0; i < l->node_count; ++i) {
                if (BITSET_TEST(no_spill, i)) continue;

                /* Only spill nodes that interfere with the node failing
                 * register allocation. It's pointless to spill anything else */
                if (!l->linear[(l->spill_node * l->node_count) + i]) continue;

                unsigned benefit = lcra_count_constraints(l, i);

                if (benefit > best_benefit) {
                        best_benefit = benefit;
                        best_node = i;
                }
        }

        free(no_spill);
        return best_node;
}

static unsigned
bi_count_read_index(bi_instr *I, bi_index index)
{
        unsigned max = 0;

        bi_foreach_src(I, s) {
                if (bi_is_equiv(I->src[s], index)) {
                        unsigned count = bi_count_read_registers(I, s);
                        max = MAX2(max, count + I->src[s].offset);
                }
        }

        return max;
}

/* Once we've chosen a spill node, spill it and returns bytes spilled */

static unsigned
bi_spill_register(bi_context *ctx, bi_index index, uint32_t offset)
{
        bi_builder b = { .shader = ctx };
        unsigned channels = 0;

        /* Spill after every store, fill before every load */
        bi_foreach_instr_global_safe(ctx, I) {
                bi_foreach_dest(I, d) {
                        if (!bi_is_equiv(I->dest[d], index)) continue;

                        unsigned extra = I->dest[d].offset;
                        bi_index tmp = bi_temp(ctx);

                        I->dest[d] = bi_replace_index(I->dest[d], tmp);
                        I->no_spill = true;

                        unsigned count = bi_count_write_registers(I, d);
                        unsigned bits = count * 32;

                        b.cursor = bi_after_instr(I);
                        bi_index loc = bi_imm_u32(offset + 4 * extra);
                        bi_store(&b, bits, tmp, loc, bi_zero(), BI_SEG_TL);

                        ctx->spills++;
                        channels = MAX2(channels, extra + count);
                }

                if (bi_has_arg(I, index)) {
                        b.cursor = bi_before_instr(I);
                        bi_index tmp = bi_temp(ctx);

                        unsigned bits = bi_count_read_index(I, index) * 32;
                        bi_rewrite_index_src_single(I, index, tmp);

                        bi_instr *ld = bi_load_to(&b, bits, tmp,
                                        bi_imm_u32(offset), bi_zero(), BI_SEG_TL);
                        ld->no_spill = true;
                        ctx->fills++;
                }
        }

        return (channels * 4);
}

void
bi_register_allocate(bi_context *ctx)
{
        struct lcra_state *l = NULL;
        bool success = false;

        unsigned iter_count = 1000; /* max iterations */

        /* Number of bytes of memory we've spilled into */
        unsigned spill_count = ctx->info.tls_size;

        /* Try with reduced register pressure to improve thread count on v7 */
        if (ctx->arch == 7) {
                bi_invalidate_liveness(ctx);
                l = bi_allocate_registers(ctx, &success, false);

                if (success) {
                        ctx->info.work_reg_count = 32;
                } else {
                        lcra_free(l);
                        l = NULL;
                }
        }

        /* Otherwise, use the register file and spill until we succeed */
        while (!success && ((iter_count--) > 0)) {
                bi_invalidate_liveness(ctx);
                l = bi_allocate_registers(ctx, &success, true);

                if (success) {
                        ctx->info.work_reg_count = 64;
                } else {
                        signed spill_node = bi_choose_spill_node(ctx, l);
                        lcra_free(l);
                        l = NULL;

                        if (spill_node == -1)
                                unreachable("Failed to choose spill node\n");

                        spill_count += bi_spill_register(ctx,
                                        bi_node_to_index(spill_node, bi_max_temp(ctx)),
                                        spill_count);
                }
        }

        assert(success);
        assert(l != NULL);

        ctx->info.tls_size = spill_count;
        bi_install_registers(ctx, l);

        lcra_free(l);
}
