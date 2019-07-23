/*
 * Copyright (C) 2018-2019 Alyssa Rosenzweig <alyssa@rosenzweig.io>
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
 */

#include "compiler.h"
#include "midgard_ops.h"
#include "util/register_allocate.h"
#include "util/u_math.h"

/* For work registers, we can subdivide in various ways. So we create
 * classes for the various sizes and conflict accordingly, keeping in
 * mind that physical registers are divided along 128-bit boundaries.
 * The important part is that 128-bit boundaries are not crossed.
 *
 * For each 128-bit register, we can subdivide to 32-bits 10 ways
 *
 * vec4: xyzw
 * vec3: xyz, yzw
 * vec2: xy, yz, zw,
 * vec1: x, y, z, w
 *
 * For each 64-bit register, we can subdivide similarly to 16-bit
 * (TODO: half-float RA, not that we support fp16 yet)
 */

#define WORK_STRIDE 10

/* Prepacked masks/swizzles for virtual register types */
static unsigned reg_type_to_mask[WORK_STRIDE] = {
        0xF,                                    /* xyzw */
        0x7, 0x7 << 1,                          /* xyz */
                 0x3, 0x3 << 1, 0x3 << 2,                /* xy */
                 0x1, 0x1 << 1, 0x1 << 2, 0x1 << 3       /* x */
};

static unsigned reg_type_to_swizzle[WORK_STRIDE] = {
        SWIZZLE(COMPONENT_X, COMPONENT_Y, COMPONENT_Z, COMPONENT_W),

        SWIZZLE(COMPONENT_X, COMPONENT_Y, COMPONENT_Z, COMPONENT_W),
        SWIZZLE(COMPONENT_Y, COMPONENT_Z, COMPONENT_W, COMPONENT_W),

        SWIZZLE(COMPONENT_X, COMPONENT_Y, COMPONENT_Z, COMPONENT_W),
        SWIZZLE(COMPONENT_Y, COMPONENT_Z, COMPONENT_Z, COMPONENT_W),
        SWIZZLE(COMPONENT_Z, COMPONENT_W, COMPONENT_Z, COMPONENT_W),

        SWIZZLE(COMPONENT_X, COMPONENT_Y, COMPONENT_Z, COMPONENT_W),
        SWIZZLE(COMPONENT_Y, COMPONENT_Y, COMPONENT_Z, COMPONENT_W),
        SWIZZLE(COMPONENT_Z, COMPONENT_Y, COMPONENT_Z, COMPONENT_W),
        SWIZZLE(COMPONENT_W, COMPONENT_Y, COMPONENT_Z, COMPONENT_W),
};

struct phys_reg {
        unsigned reg;
        unsigned mask;
        unsigned swizzle;
};

/* Given the mask/swizzle of both the register and the original source,
 * compose to find the actual mask/swizzle to give the hardware */

static unsigned
compose_writemask(unsigned mask, struct phys_reg reg)
{
        /* Note: the reg mask is guaranteed to be contiguous. So we shift
         * into the X place, compose via a simple AND, and shift back */

        unsigned shift = __builtin_ctz(reg.mask);
        return ((reg.mask >> shift) & mask) << shift;
}

static unsigned
compose_swizzle(unsigned swizzle, unsigned mask,
                struct phys_reg reg, struct phys_reg dst)
{
        unsigned out = pan_compose_swizzle(swizzle, reg.swizzle);

        /* Based on the register mask, we need to adjust over. E.g if we're
         * writing to yz, a base swizzle of xy__ becomes _xy_. Save the
         * original first component (x). But to prevent duplicate shifting
         * (only applies to ALU -- mask param is set to xyzw out on L/S to
         * prevent changes), we have to account for the shift inherent to the
         * original writemask */

        unsigned rep = out & 0x3;
        unsigned shift = __builtin_ctz(dst.mask) - __builtin_ctz(mask);
        unsigned shifted = out << (2*shift);

        /* ..but we fill in the gaps so it appears to replicate */

        for (unsigned s = 0; s < shift; ++s)
                shifted |= rep << (2*s);

        return shifted;
}

/* Helper to return the default phys_reg for a given register */

static struct phys_reg
default_phys_reg(int reg)
{
        struct phys_reg r = {
                .reg = reg,
                .mask = 0xF, /* xyzw */
                .swizzle = 0xE4 /* xyzw */
        };

        return r;
}

/* Determine which physical register, swizzle, and mask a virtual
 * register corresponds to */

static struct phys_reg
index_to_reg(compiler_context *ctx, struct ra_graph *g, int reg)
{
        /* Check for special cases */
        if (reg >= SSA_FIXED_MINIMUM)
                return default_phys_reg(SSA_REG_FROM_FIXED(reg));
        else if ((reg < 0) || !g)
                return default_phys_reg(REGISTER_UNUSED);

        /* Special cases aside, we pick the underlying register */
        int virt = ra_get_node_reg(g, reg);

        /* Divide out the register and classification */
        int phys = virt / WORK_STRIDE;
        int type = virt % WORK_STRIDE;

        struct phys_reg r = {
                .reg = phys,
                .mask = reg_type_to_mask[type],
                .swizzle = reg_type_to_swizzle[type]
        };

        /* Report that we actually use this register, and return it */
        ctx->work_registers = MAX2(ctx->work_registers, phys);
        return r;
}

/* This routine creates a register set. Should be called infrequently since
 * it's slow and can be cached */

static struct ra_regs *
create_register_set(unsigned work_count, unsigned *classes)
{
        int virtual_count = work_count * WORK_STRIDE;

        /* First, initialize the RA */
        struct ra_regs *regs = ra_alloc_reg_set(NULL, virtual_count, true);

        int work_vec4 = ra_alloc_reg_class(regs);
        int work_vec3 = ra_alloc_reg_class(regs);
        int work_vec2 = ra_alloc_reg_class(regs);
        int work_vec1 = ra_alloc_reg_class(regs);

        classes[0] = work_vec1;
        classes[1] = work_vec2;
        classes[2] = work_vec3;
        classes[3] = work_vec4;

        /* Add the full set of work registers */
        for (unsigned i = 0; i < work_count; ++i) {
                int base = WORK_STRIDE * i;

                /* Build a full set of subdivisions */
                ra_class_add_reg(regs, work_vec4, base);
                ra_class_add_reg(regs, work_vec3, base + 1);
                ra_class_add_reg(regs, work_vec3, base + 2);
                ra_class_add_reg(regs, work_vec2, base + 3);
                ra_class_add_reg(regs, work_vec2, base + 4);
                ra_class_add_reg(regs, work_vec2, base + 5);
                ra_class_add_reg(regs, work_vec1, base + 6);
                ra_class_add_reg(regs, work_vec1, base + 7);
                ra_class_add_reg(regs, work_vec1, base + 8);
                ra_class_add_reg(regs, work_vec1, base + 9);

                for (unsigned a = 0; a < 10; ++a) {
                        unsigned mask1 = reg_type_to_mask[a];

                        for (unsigned b = 0; b < 10; ++b) {
                                unsigned mask2 = reg_type_to_mask[b];

                                if (mask1 & mask2)
                                        ra_add_reg_conflict(regs,
                                                            base + a, base + b);
                        }
                }
        }

        /* We're done setting up */
        ra_set_finalize(regs, NULL);

        return regs;
}

/* This routine gets a precomputed register set off the screen if it's able, or otherwise it computes one on the fly */

static struct ra_regs *
get_register_set(struct midgard_screen *screen, unsigned work_count, unsigned **classes)
{
        /* Bounds check */
        assert(work_count >= 8);
        assert(work_count <= 16);

        /* Compute index */
        unsigned index = work_count - 8;

        /* Find the reg set */
        struct ra_regs *cached = screen->regs[index];

        if (cached) {
                assert(screen->reg_classes[index]);
                *classes = screen->reg_classes[index];
                return cached;
        }

        /* Otherwise, create one */
        struct ra_regs *created = create_register_set(work_count, screen->reg_classes[index]);

        /* Cache it and use it */
        screen->regs[index] = created;

        *classes = screen->reg_classes[index];
        return created;
}

/* This routine performs the actual register allocation. It should be succeeded
 * by install_registers */

struct ra_graph *
allocate_registers(compiler_context *ctx, bool *spilled)
{
        /* The number of vec4 work registers available depends on when the
         * uniforms start, so compute that first */
        int work_count = 16 - MAX2((ctx->uniform_cutoff - 8), 0);
        unsigned *classes = NULL;
        struct ra_regs *regs = get_register_set(ctx->screen, work_count, &classes);

        assert(regs != NULL);
        assert(classes != NULL);

       /* No register allocation to do with no SSA */

        if (!ctx->temp_count)
                return NULL;

        /* Let's actually do register allocation */
        int nodes = ctx->temp_count;
        struct ra_graph *g = ra_alloc_interference_graph(regs, nodes);

        /* Determine minimum size needed to hold values, to indirectly
         * determine class */

        unsigned *found_class = calloc(sizeof(unsigned), ctx->temp_count);

        mir_foreach_block(ctx, block) {
                mir_foreach_instr_in_block(block, ins) {
                        if (ins->compact_branch) continue;
                        if (ins->ssa_args.dest < 0) continue;
                        if (ins->ssa_args.dest >= SSA_FIXED_MINIMUM) continue;

                        int class = util_logbase2(ins->mask) + 1;

                        /* Use the largest class if there's ambiguity, this
                         * handles partial writes */

                        int dest = ins->ssa_args.dest;
                        found_class[dest] = MAX2(found_class[dest], class);
                }
        }

        for (unsigned i = 0; i < ctx->temp_count; ++i) {
                unsigned class = found_class[i];
                if (!class) continue;
                ra_set_node_class(g, i, classes[class - 1]);
        }

        /* Determine liveness */

        int *live_start = malloc(nodes * sizeof(int));
        int *live_end = malloc(nodes * sizeof(int));

        /* Initialize as non-existent */

        for (int i = 0; i < nodes; ++i) {
                live_start[i] = live_end[i] = -1;
        }

        int d = 0;

        mir_foreach_block(ctx, block) {
                mir_foreach_instr_in_block(block, ins) {
                        if (ins->compact_branch) continue;

                        /* Dest is < 0 for st_vary instructions, which break
                         * the usual SSA conventions. Liveness analysis doesn't
                         * make sense on these instructions, so skip them to
                         * avoid memory corruption */

                        if (ins->ssa_args.dest < 0) continue;

                        if (ins->ssa_args.dest < SSA_FIXED_MINIMUM) {
                                /* If this destination is not yet live, it is
                                 * now since we just wrote it */

                                int dest = ins->ssa_args.dest;

                                if (live_start[dest] == -1)
                                        live_start[dest] = d;
                        }

                        /* Since we just used a source, the source might be
                         * dead now. Scan the rest of the block for
                         * invocations, and if there are none, the source dies
                         * */

                        int sources[2] = {
                                ins->ssa_args.src0, ins->ssa_args.src1
                        };

                        for (int src = 0; src < 2; ++src) {
                                int s = sources[src];

                                if (ins->ssa_args.inline_constant && src == 1)
                                        continue;

                                if (s < 0) continue;

                                if (s >= SSA_FIXED_MINIMUM) continue;

                                if (!mir_is_live_after(ctx, block, ins, s)) {
                                        live_end[s] = d;
                                }
                        }

                        ++d;
                }
        }

        /* If a node still hasn't been killed, kill it now */

        for (int i = 0; i < nodes; ++i) {
                /* live_start == -1 most likely indicates a pinned output */

                if (live_end[i] == -1)
                        live_end[i] = d;
        }

        /* Setup interference between nodes that are live at the same time */

        for (int i = 0; i < nodes; ++i) {
                for (int j = i + 1; j < nodes; ++j) {
                        bool j_overlaps_i = live_start[j] < live_end[i];
                        bool i_overlaps_j = live_end[j] < live_start[i];

                        if (i_overlaps_j || j_overlaps_i)
                                ra_add_node_interference(g, i, j);
                }
        }

        /* Cleanup */
        free(live_start);
        free(live_end);

        if (!ra_allocate(g)) {
                *spilled = true;
        } else {
                *spilled = false;
        }

        /* Whether we were successful or not, report the graph so we can
         * compute spill nodes */

        return g;
}

/* Once registers have been decided via register allocation
 * (allocate_registers), we need to rewrite the MIR to use registers instead of
 * indices */

static void
install_registers_instr(
        compiler_context *ctx,
        struct ra_graph *g,
        midgard_instruction *ins)
{
        ssa_args args = ins->ssa_args;

        switch (ins->type) {
        case TAG_ALU_4: {
                int adjusted_src = args.inline_constant ? -1 : args.src1;
                struct phys_reg src1 = index_to_reg(ctx, g, args.src0);
                struct phys_reg src2 = index_to_reg(ctx, g, adjusted_src);
                struct phys_reg dest = index_to_reg(ctx, g, args.dest);

                unsigned uncomposed_mask = ins->mask;
                ins->mask = compose_writemask(uncomposed_mask, dest);

                /* Adjust the dest mask if necessary. Mostly this is a no-op
                 * but it matters for dot products */
                dest.mask = effective_writemask(&ins->alu, ins->mask);

                midgard_vector_alu_src mod1 =
                        vector_alu_from_unsigned(ins->alu.src1);
                mod1.swizzle = compose_swizzle(mod1.swizzle, uncomposed_mask, src1, dest);
                ins->alu.src1 = vector_alu_srco_unsigned(mod1);

                ins->registers.src1_reg = src1.reg;

                ins->registers.src2_imm = args.inline_constant;

                if (args.inline_constant) {
                        /* Encode inline 16-bit constant. See disassembler for
                         * where the algorithm is from */

                        ins->registers.src2_reg = ins->inline_constant >> 11;

                        int lower_11 = ins->inline_constant & ((1 << 12) - 1);
                        uint16_t imm = ((lower_11 >> 8) & 0x7) |
                                       ((lower_11 & 0xFF) << 3);

                        ins->alu.src2 = imm << 2;
                } else {
                        midgard_vector_alu_src mod2 =
                                vector_alu_from_unsigned(ins->alu.src2);
                        mod2.swizzle = compose_swizzle(
                                               mod2.swizzle, uncomposed_mask, src2, dest);
                        ins->alu.src2 = vector_alu_srco_unsigned(mod2);

                        ins->registers.src2_reg = src2.reg;
                }

                ins->registers.out_reg = dest.reg;
                break;
        }

        case TAG_LOAD_STORE_4: {
                if (OP_IS_STORE_R26(ins->load_store.op)) {
                        /* TODO: use ssa_args for st_vary */
                        ins->load_store.reg = 0;
                } else {
                        /* Which physical register we read off depends on
                         * whether we are loading or storing -- think about the
                         * logical dataflow */

                        unsigned r = OP_IS_STORE(ins->load_store.op) ?
                                     args.src0 : args.dest;
                        struct phys_reg src = index_to_reg(ctx, g, r);

                        ins->load_store.reg = src.reg;

                        ins->load_store.swizzle = compose_swizzle(
                                                          ins->load_store.swizzle, 0xF,
                                                          default_phys_reg(0), src);

                        ins->mask = compose_writemask(
                                            ins->mask, src);
                }

                break;
        }

        default:
                break;
        }
}

void
install_registers(compiler_context *ctx, struct ra_graph *g)
{
        mir_foreach_block(ctx, block) {
                mir_foreach_instr_in_block(block, ins) {
                        if (ins->compact_branch) continue;
                        install_registers_instr(ctx, g, ins);
                }
        }

}
