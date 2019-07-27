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

/* We have overlapping register classes for special registers, handled via
 * shadows */

#define SHADOW_R27 17
#define SHADOW_R28 18
#define SHADOW_R29 19

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

        /* Apply shadow registers */

        if (phys >= SHADOW_R27 && phys <= SHADOW_R29)
                phys += 27 - SHADOW_R27;

        struct phys_reg r = {
                .reg = phys,
                .mask = reg_type_to_mask[type],
                .swizzle = reg_type_to_swizzle[type]
        };

        /* Report that we actually use this register, and return it */

        if (phys < 16)
                ctx->work_registers = MAX2(ctx->work_registers, phys);

        return r;
}

/* This routine creates a register set. Should be called infrequently since
 * it's slow and can be cached. For legibility, variables are named in terms of
 * work registers, although it is also used to create the register set for
 * special register allocation */

static void
add_shadow_conflicts (struct ra_regs *regs, unsigned base, unsigned shadow)
{
        for (unsigned a = 0; a < WORK_STRIDE; ++a) {
                unsigned reg_a = (WORK_STRIDE * base) + a;

                for (unsigned b = 0; b < WORK_STRIDE; ++b) {
                        unsigned reg_b = (WORK_STRIDE * shadow) + b;

                        ra_add_reg_conflict(regs, reg_a, reg_b);
                        ra_add_reg_conflict(regs, reg_b, reg_a);
                }
        }
}

static struct ra_regs *
create_register_set(unsigned work_count, unsigned *classes)
{
        int virtual_count = 32 * WORK_STRIDE;

        /* First, initialize the RA */
        struct ra_regs *regs = ra_alloc_reg_set(NULL, virtual_count, true);

        for (unsigned c = 0; c < NR_REG_CLASSES; ++c) {
                int work_vec4 = ra_alloc_reg_class(regs);
                int work_vec3 = ra_alloc_reg_class(regs);
                int work_vec2 = ra_alloc_reg_class(regs);
                int work_vec1 = ra_alloc_reg_class(regs);

                classes[4*c + 0] = work_vec1;
                classes[4*c + 1] = work_vec2;
                classes[4*c + 2] = work_vec3;
                classes[4*c + 3] = work_vec4;

                /* Special register classes have other register counts */
                unsigned count =
                        (c == REG_CLASS_WORK)   ? work_count :
                        (c == REG_CLASS_LDST27) ? 1 : 2;

                /* We arbitraily pick r17 (RA unused) as the shadow for r27 */
                unsigned first_reg =
                        (c == REG_CLASS_LDST)   ? 26 :
                        (c == REG_CLASS_LDST27) ? SHADOW_R27 :
                        (c == REG_CLASS_TEXR)   ? 28 :
                        (c == REG_CLASS_TEXW)   ? SHADOW_R28 :
                        0;

                /* Add the full set of work registers */
                for (unsigned i = first_reg; i < (first_reg + count); ++i) {
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
        }


        /* We have duplicate classes */
        add_shadow_conflicts(regs, 27, SHADOW_R27);
        add_shadow_conflicts(regs, 28, SHADOW_R28);
        add_shadow_conflicts(regs, 29, SHADOW_R29);

        /* We're done setting up */
        ra_set_finalize(regs, NULL);

        return regs;
}

/* This routine gets a precomputed register set off the screen if it's able, or
 * otherwise it computes one on the fly */

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

/* Assign a (special) class, ensuring that it is compatible with whatever class
 * was already set */

static void
set_class(unsigned *classes, unsigned node, unsigned class)
{
        /* Check that we're even a node */
        if ((node < 0) || (node >= SSA_FIXED_MINIMUM))
                return;

        /* First 4 are work, next 4 are load/store.. */
        unsigned current_class = classes[node] >> 2;

        /* Nothing to do */
        if (class == current_class)
                return;


        if ((current_class == REG_CLASS_LDST27) && (class == REG_CLASS_LDST))
                return;

        /* If we're changing, we must not have already assigned a special class
         */

        bool compat = current_class == REG_CLASS_WORK;
        compat |= (current_class == REG_CLASS_LDST) && (class == REG_CLASS_LDST27);

        assert(compat);

        classes[node] &= 0x3;
        classes[node] |= (class << 2);
}

static void
force_vec4(unsigned *classes, unsigned node)
{
        if ((node < 0) || (node >= SSA_FIXED_MINIMUM))
                return;

        /* Force vec4 = 3 */
        classes[node] |= 0x3;
}

/* Special register classes impose special constraints on who can read their
 * values, so check that */

static bool
check_read_class(unsigned *classes, unsigned tag, unsigned node)
{
        /* Non-nodes are implicitly ok */
        if ((node < 0) || (node >= SSA_FIXED_MINIMUM))
                return true;

        unsigned current_class = classes[node] >> 2;

        switch (current_class) {
        case REG_CLASS_LDST:
        case REG_CLASS_LDST27:
                return (tag == TAG_LOAD_STORE_4);
        case REG_CLASS_TEXR:
                return (tag == TAG_TEXTURE_4);
        case REG_CLASS_TEXW:
                return (tag != TAG_LOAD_STORE_4);
        case REG_CLASS_WORK:
                return (tag == TAG_ALU_4);
        default:
                unreachable("Invalid class");
        }
}

static bool
check_write_class(unsigned *classes, unsigned tag, unsigned node)
{
        /* Non-nodes are implicitly ok */
        if ((node < 0) || (node >= SSA_FIXED_MINIMUM))
                return true;

        unsigned current_class = classes[node] >> 2;

        switch (current_class) {
        case REG_CLASS_TEXR:
                return true;
        case REG_CLASS_TEXW:
                return (tag == TAG_TEXTURE_4);
        case REG_CLASS_LDST:
        case REG_CLASS_LDST27:
        case REG_CLASS_WORK:
                return (tag == TAG_ALU_4) || (tag == TAG_LOAD_STORE_4);
        default:
                unreachable("Invalid class");
        }
}

/* Prepass before RA to ensure special class restrictions are met. The idea is
 * to create a bit field of types of instructions that read a particular index.
 * Later, we'll add moves as appropriate and rewrite to specialize by type. */

static void
mark_node_class (unsigned *bitfield, unsigned node)
{
        if ((node >= 0) && (node < SSA_FIXED_MINIMUM))
                BITSET_SET(bitfield, node);
}

void
mir_lower_special_reads(compiler_context *ctx)
{
        size_t sz = BITSET_WORDS(ctx->temp_count) * sizeof(BITSET_WORD);

        /* Bitfields for the various types of registers we could have */

        unsigned *alur = calloc(sz, 1);
        unsigned *aluw = calloc(sz, 1);
        unsigned *ldst = calloc(sz, 1);
        unsigned *texr = calloc(sz, 1);
        unsigned *texw = calloc(sz, 1);

        /* Pass #1 is analysis, a linear scan to fill out the bitfields */

        mir_foreach_instr_global(ctx, ins) {
                if (ins->compact_branch) continue;

                switch (ins->type) {
                case TAG_ALU_4:
                        mark_node_class(aluw, ins->ssa_args.dest);
                        mark_node_class(alur, ins->ssa_args.src0);

                        if (!ins->ssa_args.inline_constant)
                                mark_node_class(alur, ins->ssa_args.src1);

                        break;
                case TAG_LOAD_STORE_4:
                        mark_node_class(ldst, ins->ssa_args.src0);
                        mark_node_class(ldst, ins->ssa_args.src1);
                        break;
                case TAG_TEXTURE_4:
                        mark_node_class(texr, ins->ssa_args.src0);
                        mark_node_class(texr, ins->ssa_args.src1);
                        mark_node_class(texw, ins->ssa_args.dest);
                        break;
                }
        }

        /* Pass #2 is lowering now that we've analyzed all the classes.
         * Conceptually, if an index is only marked for a single type of use,
         * there is nothing to lower. If it is marked for different uses, we
         * split up based on the number of types of uses. To do so, we divide
         * into N distinct classes of use (where N>1 by definition), emit N-1
         * moves from the index to copies of the index, and finally rewrite N-1
         * of the types of uses to use the corresponding move */

        unsigned spill_idx = ctx->temp_count;

        for (unsigned i = 0; i < ctx->temp_count; ++i) {
                bool is_alur = BITSET_TEST(alur, i);
                bool is_aluw = BITSET_TEST(aluw, i);
                bool is_ldst = BITSET_TEST(ldst, i);
                bool is_texr = BITSET_TEST(texr, i);
                bool is_texw = BITSET_TEST(texw, i);

                /* Analyse to check how many distinct uses there are. ALU ops
                 * (alur) can read the results of the texture pipeline (texw)
                 * but not ldst or texr. Load/store ops (ldst) cannot read
                 * anything but load/store inputs. Texture pipeline cannot read
                 * anything but texture inputs. TODO: Simplify.  */

                bool collision =
                        (is_alur && (is_ldst || is_texr)) ||
                        (is_ldst && (is_alur || is_texr || is_texw)) ||
                        (is_texr && (is_alur || is_ldst)) ||
                        (is_texw && (is_aluw || is_ldst));
        
                if (!collision)
                        continue;

                /* Use the index as-is as the work copy. Emit copies for
                 * special uses */

                unsigned classes[] = { TAG_LOAD_STORE_4, TAG_TEXTURE_4, TAG_TEXTURE_4 };
                bool collisions[] = { is_ldst, is_texr, is_texw && is_aluw };

                for (unsigned j = 0; j < ARRAY_SIZE(collisions); ++j) {
                        if (!collisions[j]) continue;

                        /* When the hazard is from reading, we move and rewrite
                         * sources (typical case). When it's from writing, we
                         * flip the move and rewrite destinations (obscure,
                         * only from control flow -- impossible in SSA) */

                        bool hazard_write = (j == 2);

                        unsigned idx = spill_idx++;

                        midgard_instruction m = hazard_write ?
                                v_mov(idx, blank_alu_src, i) :
                                v_mov(i, blank_alu_src, idx);

                        /* Insert move after each write */
                        mir_foreach_instr_global_safe(ctx, pre_use) {
                                if (pre_use->compact_branch) continue;
                                if (pre_use->ssa_args.dest != i)
                                        continue;

                                /* If the hazard is writing, we need to
                                 * specific insert moves for the contentious
                                 * class. If the hazard is reading, we insert
                                 * moves whenever it is written */

                                if (hazard_write && pre_use->type != classes[j])
                                        continue;

                                midgard_instruction *use = mir_next_op(pre_use);
                                assert(use);
                                mir_insert_instruction_before(use, m);
                        }

                        /* Rewrite to use */
                        if (hazard_write)
                                mir_rewrite_index_dst_tag(ctx, i, idx, classes[j]);
                        else
                                mir_rewrite_index_src_tag(ctx, i, idx, classes[j]);
                }
        }

        free(alur);
        free(aluw);
        free(ldst);
        free(texr);
        free(texw);
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
        
        /* Register class (as known to the Mesa register allocator) is actually
         * the product of both semantic class (work, load/store, texture..) and
         * size (vec2/vec3..). First, we'll go through and determine the
         * minimum size needed to hold values */

        unsigned *found_class = calloc(sizeof(unsigned), ctx->temp_count);

        mir_foreach_instr_global(ctx, ins) {
                if (ins->compact_branch) continue;
                if (ins->ssa_args.dest < 0) continue;
                if (ins->ssa_args.dest >= SSA_FIXED_MINIMUM) continue;

                /* 0 for x, 1 for xy, 2 for xyz, 3 for xyzw */
                int class = util_logbase2(ins->mask);

                /* Use the largest class if there's ambiguity, this
                 * handles partial writes */

                int dest = ins->ssa_args.dest;
                found_class[dest] = MAX2(found_class[dest], class);
        }

        /* Next, we'll determine semantic class. We default to zero (work).
         * But, if we're used with a special operation, that will force us to a
         * particular class. Each node must be assigned to exactly one class; a
         * prepass before RA should have lowered what-would-have-been
         * multiclass nodes into a series of moves to break it up into multiple
         * nodes (TODO) */

        mir_foreach_instr_global(ctx, ins) {
                if (ins->compact_branch) continue;

                /* Check if this operation imposes any classes */

                if (ins->type == TAG_LOAD_STORE_4) {
                        bool force_r27 = OP_IS_R27_ONLY(ins->load_store.op);
                        unsigned class = force_r27 ? REG_CLASS_LDST27 : REG_CLASS_LDST;

                        set_class(found_class, ins->ssa_args.src0, class);
                        set_class(found_class, ins->ssa_args.src1, class);

                        if (force_r27) {
                                force_vec4(found_class, ins->ssa_args.dest);
                                force_vec4(found_class, ins->ssa_args.src0);
                                force_vec4(found_class, ins->ssa_args.src1);
                        }
                } else if (ins->type == TAG_TEXTURE_4) {
                        set_class(found_class, ins->ssa_args.dest, REG_CLASS_TEXW);
                        set_class(found_class, ins->ssa_args.src0, REG_CLASS_TEXR);
                        set_class(found_class, ins->ssa_args.src1, REG_CLASS_TEXR);
                }
        }

        /* Check that the semantics of the class are respected */
        mir_foreach_instr_global(ctx, ins) {
                if (ins->compact_branch) continue;

                assert(check_write_class(found_class, ins->type, ins->ssa_args.dest));
                assert(check_read_class(found_class, ins->type, ins->ssa_args.src0));

                if (!ins->ssa_args.inline_constant)
                        assert(check_read_class(found_class, ins->type, ins->ssa_args.src1));
        }

        for (unsigned i = 0; i < ctx->temp_count; ++i) {
                unsigned class = found_class[i];
                ra_set_node_class(g, i, classes[class]);
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

                        if (ins->ssa_args.dest < SSA_FIXED_MINIMUM) {
                                /* If this destination is not yet live, it is
                                 * now since we just wrote it */

                                int dest = ins->ssa_args.dest;

                                if (dest >= 0 && live_start[dest] == -1)
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
                bool fixed = args.src0 >= SSA_FIXED_MINIMUM;

                if (OP_IS_STORE_R26(ins->load_store.op) && fixed) {
                        ins->load_store.reg = SSA_REG_FROM_FIXED(args.src0);
                } else if (OP_IS_STORE_VARY(ins->load_store.op)) {
                        struct phys_reg src = index_to_reg(ctx, g, args.src0);
                        assert(src.reg == 26 || src.reg == 27);

                        ins->load_store.reg = src.reg - 26;

                        /* TODO: swizzle/mask */
                } else {
                        /* Which physical register we read off depends on
                         * whether we are loading or storing -- think about the
                         * logical dataflow */

                        bool encodes_src =
                                OP_IS_STORE(ins->load_store.op) &&
                                ins->load_store.op != midgard_op_st_cubemap_coords;

                        unsigned r = encodes_src ?
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

        case TAG_TEXTURE_4: {
                /* Grab RA results */
                struct phys_reg dest = index_to_reg(ctx, g, args.dest);
                struct phys_reg coord = index_to_reg(ctx, g, args.src0);
                struct phys_reg lod = index_to_reg(ctx, g, args.src1);

                assert(dest.reg == 28 || dest.reg == 29);
                assert(coord.reg == 28 || coord.reg == 29);

                /* First, install the texture coordinate */
                ins->texture.in_reg_full = 1;
                ins->texture.in_reg_upper = 0;
                ins->texture.in_reg_select = coord.reg - 28;
                ins->texture.in_reg_swizzle =
                        compose_swizzle(ins->texture.in_reg_swizzle, 0xF, coord, dest);

                /* Next, install the destination */
                ins->texture.out_full = 1;
                ins->texture.out_upper = 0;
                ins->texture.out_reg_select = dest.reg - 28;
                ins->texture.swizzle = dest.swizzle;
                ins->texture.mask = dest.mask;

                /* If there is a register LOD/bias, use it */
                if (args.src1 > -1) {
                        midgard_tex_register_select sel = {
                                .select = lod.reg,
                                .full = 1,
                                .component = lod.swizzle & 3,
                        };

                        uint8_t packed;
                        memcpy(&packed, &sel, sizeof(packed));
                        ins->texture.bias = packed;
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
