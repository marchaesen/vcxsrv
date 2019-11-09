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
#include "util/u_memory.h"

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

#define SHADOW_R0  17
#define SHADOW_R28 18
#define SHADOW_R29 19

/* Prepacked masks/swizzles for virtual register types */
static unsigned reg_type_to_mask[WORK_STRIDE] = {
        0xF,                                    /* xyzw */
        0x7, 0x7 << 1,                          /* xyz */
                 0x3, 0x3 << 1, 0x3 << 2,                /* xy */
                 0x1, 0x1 << 1, 0x1 << 2, 0x1 << 3       /* x */
};

struct phys_reg {
        /* Physical register: 0-31 */
        unsigned reg;

        /* Byte offset into the physical register: 0-15 */
        unsigned offset;

        /* Number of bytes in a component of this register */
        unsigned size;
};

/* Shift up by reg_offset and horizontally by dst_offset. */

static void
offset_swizzle(unsigned *swizzle, unsigned reg_offset, unsigned srcsize, unsigned dst_offset)
{
        unsigned out[MIR_VEC_COMPONENTS];

        signed reg_comp = reg_offset / srcsize;
        signed dst_comp = dst_offset / srcsize;

        unsigned max_component = (16 / srcsize) - 1;

        assert(reg_comp * srcsize == reg_offset);
        assert(dst_comp * srcsize == dst_offset);

        for (signed c = 0; c < MIR_VEC_COMPONENTS; ++c) {
                signed comp = MAX2(c - dst_comp, 0);
                out[c] = MIN2(swizzle[comp] + reg_comp, max_component);
        }

        memcpy(swizzle, out, sizeof(out));
}

/* Helper to return the default phys_reg for a given register */

static struct phys_reg
default_phys_reg(int reg, midgard_reg_mode size)
{
        struct phys_reg r = {
                .reg = reg,
                .offset = 0,
                .size = mir_bytes_for_mode(size)
        };

        return r;
}

/* Determine which physical register, swizzle, and mask a virtual
 * register corresponds to */

static struct phys_reg
index_to_reg(compiler_context *ctx, struct ra_graph *g, unsigned reg, midgard_reg_mode size)
{
        /* Check for special cases */
        if (reg == ~0)
                return default_phys_reg(REGISTER_UNUSED, size);
        else if (reg >= SSA_FIXED_MINIMUM)
                return default_phys_reg(SSA_REG_FROM_FIXED(reg), size);
        else if (!g)
                return default_phys_reg(REGISTER_UNUSED, size);

        /* Special cases aside, we pick the underlying register */
        int virt = ra_get_node_reg(g, reg);

        /* Divide out the register and classification */
        int phys = virt / WORK_STRIDE;
        int type = virt % WORK_STRIDE;

        /* Apply shadow registers */

        if (phys >= SHADOW_R28 && phys <= SHADOW_R29)
                phys += 28 - SHADOW_R28;
        else if (phys == SHADOW_R0)
                phys = 0;

        unsigned bytes = mir_bytes_for_mode(size);

        struct phys_reg r = {
                .reg = phys,
                .offset = __builtin_ctz(reg_type_to_mask[type]) * bytes,
                .size = bytes
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
add_shadow_conflicts (struct ra_regs *regs, unsigned base, unsigned shadow, unsigned shadow_count)
{
        for (unsigned a = 0; a < WORK_STRIDE; ++a) {
                unsigned reg_a = (WORK_STRIDE * base) + a;

                for (unsigned b = 0; b < shadow_count; ++b) {
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

        for (unsigned c = 0; c < (NR_REG_CLASSES - 1); ++c) {
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
                        (c == REG_CLASS_WORK)   ? work_count : 2;

                unsigned first_reg =
                        (c == REG_CLASS_LDST)   ? 26 :
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

        int fragc = ra_alloc_reg_class(regs);

        classes[4*REG_CLASS_FRAGC + 0] = fragc;
        classes[4*REG_CLASS_FRAGC + 1] = fragc;
        classes[4*REG_CLASS_FRAGC + 2] = fragc;
        classes[4*REG_CLASS_FRAGC + 3] = fragc;
        ra_class_add_reg(regs, fragc, WORK_STRIDE * SHADOW_R0);

        /* We have duplicate classes */
        add_shadow_conflicts(regs,  0, SHADOW_R0,  1);
        add_shadow_conflicts(regs, 28, SHADOW_R28, WORK_STRIDE);
        add_shadow_conflicts(regs, 29, SHADOW_R29, WORK_STRIDE);

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
        if (node >= SSA_FIXED_MINIMUM)
                return;

        /* First 4 are work, next 4 are load/store.. */
        unsigned current_class = classes[node] >> 2;

        /* Nothing to do */
        if (class == current_class)
                return;

        /* If we're changing, we haven't assigned a special class */
        assert(current_class == REG_CLASS_WORK);

        classes[node] &= 0x3;
        classes[node] |= (class << 2);
}

static void
force_vec4(unsigned *classes, unsigned node)
{
        if (node >= SSA_FIXED_MINIMUM)
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
        if (node >= SSA_FIXED_MINIMUM)
                return true;

        unsigned current_class = classes[node] >> 2;

        switch (current_class) {
        case REG_CLASS_LDST:
                return (tag == TAG_LOAD_STORE_4);
        case REG_CLASS_TEXR:
                return (tag == TAG_TEXTURE_4);
        case REG_CLASS_TEXW:
                return (tag != TAG_LOAD_STORE_4);
        case REG_CLASS_WORK:
                return IS_ALU(tag);
        default:
                unreachable("Invalid class");
        }
}

static bool
check_write_class(unsigned *classes, unsigned tag, unsigned node)
{
        /* Non-nodes are implicitly ok */
        if (node >= SSA_FIXED_MINIMUM)
                return true;

        unsigned current_class = classes[node] >> 2;

        switch (current_class) {
        case REG_CLASS_TEXR:
                return true;
        case REG_CLASS_TEXW:
                return (tag == TAG_TEXTURE_4);
        case REG_CLASS_LDST:
        case REG_CLASS_WORK:
                return IS_ALU(tag) || (tag == TAG_LOAD_STORE_4);
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
        if (node < SSA_FIXED_MINIMUM)
                BITSET_SET(bitfield, node);
}

void
mir_lower_special_reads(compiler_context *ctx)
{
        size_t sz = BITSET_WORDS(ctx->temp_count) * sizeof(BITSET_WORD);

        /* Bitfields for the various types of registers we could have. aluw can
         * be written by either ALU or load/store */

        unsigned *alur = calloc(sz, 1);
        unsigned *aluw = calloc(sz, 1);
        unsigned *brar = calloc(sz, 1);
        unsigned *ldst = calloc(sz, 1);
        unsigned *texr = calloc(sz, 1);
        unsigned *texw = calloc(sz, 1);

        /* Pass #1 is analysis, a linear scan to fill out the bitfields */

        mir_foreach_instr_global(ctx, ins) {
                switch (ins->type) {
                case TAG_ALU_4:
                        mark_node_class(aluw, ins->dest);
                        mark_node_class(alur, ins->src[0]);
                        mark_node_class(alur, ins->src[1]);
                        mark_node_class(alur, ins->src[2]);

                        if (ins->compact_branch && ins->writeout)
                                mark_node_class(brar, ins->src[0]);

                        break;

                case TAG_LOAD_STORE_4:
                        mark_node_class(aluw, ins->dest);
                        mark_node_class(ldst, ins->src[0]);
                        mark_node_class(ldst, ins->src[1]);
                        mark_node_class(ldst, ins->src[2]);
                        break;

                case TAG_TEXTURE_4:
                        mark_node_class(texr, ins->src[0]);
                        mark_node_class(texr, ins->src[1]);
                        mark_node_class(texr, ins->src[2]);
                        mark_node_class(texw, ins->dest);
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
                bool is_brar = BITSET_TEST(brar, i);
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
                        (is_texr && (is_alur || is_ldst || is_texw)) ||
                        (is_texw && (is_aluw || is_ldst || is_texr)) ||
                        (is_brar && is_texw);
        
                if (!collision)
                        continue;

                /* Use the index as-is as the work copy. Emit copies for
                 * special uses */

                unsigned classes[] = { TAG_LOAD_STORE_4, TAG_TEXTURE_4, TAG_TEXTURE_4, TAG_ALU_4};
                bool collisions[] = { is_ldst, is_texr, is_texw && is_aluw, is_brar };

                for (unsigned j = 0; j < ARRAY_SIZE(collisions); ++j) {
                        if (!collisions[j]) continue;

                        /* When the hazard is from reading, we move and rewrite
                         * sources (typical case). When it's from writing, we
                         * flip the move and rewrite destinations (obscure,
                         * only from control flow -- impossible in SSA) */

                        bool hazard_write = (j == 2);

                        unsigned idx = spill_idx++;

                        midgard_instruction m = hazard_write ?
                                v_mov(idx, i) : v_mov(i, idx);

                        /* Insert move before each read/write, depending on the
                         * hazard we're trying to account for */

                        mir_foreach_instr_global_safe(ctx, pre_use) {
                                if (pre_use->type != classes[j])
                                        continue;

                                if (hazard_write) {
                                        if (pre_use->dest != i)
                                                continue;
                                } else {
                                        if (!mir_has_arg(pre_use, i))
                                                continue;
                                }

                                if (hazard_write) {
                                        midgard_instruction *use = mir_next_op(pre_use);
                                        assert(use);
                                        mir_insert_instruction_before(ctx, use, m);
                                        mir_rewrite_index_dst_single(pre_use, i, idx);
                                } else {
                                        idx = spill_idx++;
                                        m = v_mov(i, idx);
                                        m.mask = mir_from_bytemask(mir_bytemask_of_read_components(pre_use, i), midgard_reg_mode_32);
                                        mir_insert_instruction_before(ctx, pre_use, m);
                                        mir_rewrite_index_src_single(pre_use, i, idx);
                                }
                        }
                }
        }

        free(alur);
        free(aluw);
        free(brar);
        free(ldst);
        free(texr);
        free(texw);
}

/* We register allocate after scheduling, so we need to ensure instructions
 * executing in parallel within a segment of a bundle don't clobber each
 * other's registers. This is mostly a non-issue thanks to scheduling, but
 * there are edge cases. In particular, after a register is written in a
 * segment, it interferes with anything reading. */

static void
mir_compute_segment_interference(
                compiler_context *ctx,
                struct ra_graph *l,
                midgard_bundle *bun,
                unsigned pivot,
                unsigned i)
{
        for (unsigned j = pivot; j < i; ++j) {
                mir_foreach_src(bun->instructions[j], s) {
                        if (bun->instructions[j]->src[s] >= ctx->temp_count)
                                continue;

                        for (unsigned q = pivot; q < i; ++q) {
                                if (bun->instructions[q]->dest >= ctx->temp_count)
                                        continue;

                                /* See dEQP-GLES2.functional.shaders.return.output_write_in_func_dynamic_fragment */

                                if (q >= j) {
                                        if (!(bun->instructions[j]->unit == UNIT_SMUL && bun->instructions[q]->unit == UNIT_VLUT))
                                                continue;
                                }

                                ra_add_node_interference(l, bun->instructions[q]->dest, bun->instructions[j]->src[s]);
                        }
                }
        }
}

static void
mir_compute_bundle_interference(
                compiler_context *ctx,
                struct ra_graph *l,
                midgard_bundle *bun)
{
        if (!IS_ALU(bun->tag))
                return;

        bool old = bun->instructions[0]->unit >= UNIT_VADD;
        unsigned pivot = 0;

        for (unsigned i = 1; i < bun->instruction_count; ++i) {
                bool new = bun->instructions[i]->unit >= UNIT_VADD;

                if (old != new) {
                        mir_compute_segment_interference(ctx, l, bun, 0, i);
                        pivot = i;
                        break;
                }
        }

        mir_compute_segment_interference(ctx, l, bun, pivot, bun->instruction_count);
}

static void
mir_compute_interference(
                compiler_context *ctx,
                struct ra_graph *g)
{
        /* First, we need liveness information to be computed per block */
        mir_compute_liveness(ctx);

        /* Now that every block has live_in/live_out computed, we can determine
         * interference by walking each block linearly. Take live_out at the
         * end of each block and walk the block backwards. */

        mir_foreach_block(ctx, blk) {
                uint16_t *live = mem_dup(blk->live_out, ctx->temp_count * sizeof(uint16_t));

                mir_foreach_instr_in_block_rev(blk, ins) {
                        /* Mark all registers live after the instruction as
                         * interfering with the destination */

                        unsigned dest = ins->dest;

                        if (dest < ctx->temp_count) {
                                for (unsigned i = 0; i < ctx->temp_count; ++i)
                                        if (live[i])
                                                ra_add_node_interference(g, dest, i);
                        }

                        /* Update live_in */
                        mir_liveness_ins_update(live, ins, ctx->temp_count);
                }

                mir_foreach_bundle_in_block(blk, bun)
                        mir_compute_bundle_interference(ctx, g, bun);

                free(live);
        }
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
                if (ins->dest >= SSA_FIXED_MINIMUM) continue;

                /* 0 for x, 1 for xy, 2 for xyz, 3 for xyzw */
                int class = util_logbase2(ins->mask);

                /* Use the largest class if there's ambiguity, this
                 * handles partial writes */

                int dest = ins->dest;
                found_class[dest] = MAX2(found_class[dest], class);
        }

        /* Next, we'll determine semantic class. We default to zero (work).
         * But, if we're used with a special operation, that will force us to a
         * particular class. Each node must be assigned to exactly one class; a
         * prepass before RA should have lowered what-would-have-been
         * multiclass nodes into a series of moves to break it up into multiple
         * nodes (TODO) */

        mir_foreach_instr_global(ctx, ins) {
                /* Check if this operation imposes any classes */

                if (ins->type == TAG_LOAD_STORE_4) {
                        bool force_vec4_only = OP_IS_VEC4_ONLY(ins->load_store.op);

                        set_class(found_class, ins->src[0], REG_CLASS_LDST);
                        set_class(found_class, ins->src[1], REG_CLASS_LDST);
                        set_class(found_class, ins->src[2], REG_CLASS_LDST);

                        if (force_vec4_only) {
                                force_vec4(found_class, ins->dest);
                                force_vec4(found_class, ins->src[0]);
                                force_vec4(found_class, ins->src[1]);
                                force_vec4(found_class, ins->src[2]);
                        }
                } else if (ins->type == TAG_TEXTURE_4) {
                        set_class(found_class, ins->dest, REG_CLASS_TEXW);
                        set_class(found_class, ins->src[0], REG_CLASS_TEXR);
                        set_class(found_class, ins->src[1], REG_CLASS_TEXR);
                        set_class(found_class, ins->src[2], REG_CLASS_TEXR);
                }
        }

        /* Check that the semantics of the class are respected */
        mir_foreach_instr_global(ctx, ins) {
                assert(check_write_class(found_class, ins->type, ins->dest));
                assert(check_read_class(found_class, ins->type, ins->src[0]));
                assert(check_read_class(found_class, ins->type, ins->src[1]));
                assert(check_read_class(found_class, ins->type, ins->src[2]));
        }

        /* Mark writeout to r0 */
        mir_foreach_instr_global(ctx, ins) {
                if (ins->compact_branch && ins->writeout)
                        set_class(found_class, ins->src[0], REG_CLASS_FRAGC);
        }

        for (unsigned i = 0; i < ctx->temp_count; ++i) {
                unsigned class = found_class[i];
                ra_set_node_class(g, i, classes[class]);
        }

        mir_compute_interference(ctx, g);

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
        switch (ins->type) {
        case TAG_ALU_4:
        case TAG_ALU_8:
        case TAG_ALU_12:
        case TAG_ALU_16: {
                 if (ins->compact_branch)
                         return;

                struct phys_reg src1 = index_to_reg(ctx, g, ins->src[0], mir_srcsize(ins, 0));
                struct phys_reg src2 = index_to_reg(ctx, g, ins->src[1], mir_srcsize(ins, 1));
                struct phys_reg dest = index_to_reg(ctx, g, ins->dest, mir_typesize(ins));

                mir_set_bytemask(ins, mir_bytemask(ins) << dest.offset);

                unsigned dest_offset =
                        GET_CHANNEL_COUNT(alu_opcode_props[ins->alu.op].props) ? 0 :
                        dest.offset;

                offset_swizzle(ins->swizzle[0], src1.offset, src1.size, dest_offset);

                ins->registers.src1_reg = src1.reg;

                ins->registers.src2_imm = ins->has_inline_constant;

                if (ins->has_inline_constant) {
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
                        offset_swizzle(ins->swizzle[1], src2.offset, src2.size, dest_offset);
                        ins->alu.src2 = vector_alu_srco_unsigned(mod2);

                        ins->registers.src2_reg = src2.reg;
                }

                ins->registers.out_reg = dest.reg;
                break;
        }

        case TAG_LOAD_STORE_4: {
                /* Which physical register we read off depends on
                 * whether we are loading or storing -- think about the
                 * logical dataflow */

                bool encodes_src = OP_IS_STORE(ins->load_store.op);

                if (encodes_src) {
                        struct phys_reg src = index_to_reg(ctx, g, ins->src[0], mir_srcsize(ins, 0));
                        assert(src.reg == 26 || src.reg == 27);

                        ins->load_store.reg = src.reg - 26;
                        offset_swizzle(ins->swizzle[0], src.offset, src.size, 0);
               } else {
                        struct phys_reg dst = index_to_reg(ctx, g, ins->dest, mir_typesize(ins));

                        ins->load_store.reg = dst.reg;
                        offset_swizzle(ins->swizzle[0], 0, 4, dst.offset);
                        mir_set_bytemask(ins, mir_bytemask(ins) << dst.offset);
                }

                /* We also follow up by actual arguments */

                unsigned src2 = ins->src[1];
                unsigned src3 = ins->src[2];

                if (src2 != ~0) {
                        struct phys_reg src = index_to_reg(ctx, g, src2, mir_srcsize(ins, 1));
                        unsigned component = src.offset / src.size;
                        assert(component * src.size == src.offset);
                        ins->load_store.arg_1 |= midgard_ldst_reg(src.reg, component);
                }

                if (src3 != ~0) {
                        struct phys_reg src = index_to_reg(ctx, g, src3, mir_srcsize(ins, 2));
                        unsigned component = src.offset / src.size;
                        assert(component * src.size == src.offset);
                        ins->load_store.arg_2 |= midgard_ldst_reg(src.reg, component);
                }
 
                break;
        }

        case TAG_TEXTURE_4: {
                /* Grab RA results */
                struct phys_reg dest = index_to_reg(ctx, g, ins->dest, mir_typesize(ins));
                struct phys_reg coord = index_to_reg(ctx, g, ins->src[1], mir_srcsize(ins, 1));
                struct phys_reg lod = index_to_reg(ctx, g, ins->src[2], mir_srcsize(ins, 2));

                assert(dest.reg == 28 || dest.reg == 29);
                assert(coord.reg == 28 || coord.reg == 29);

                /* First, install the texture coordinate */
                ins->texture.in_reg_full = 1;
                ins->texture.in_reg_upper = 0;
                ins->texture.in_reg_select = coord.reg - 28;
                offset_swizzle(ins->swizzle[1], coord.offset, coord.size, 0);

                /* Next, install the destination */
                ins->texture.out_full = 1;
                ins->texture.out_upper = 0;
                ins->texture.out_reg_select = dest.reg - 28;
                offset_swizzle(ins->swizzle[0], 0, 4, dest.offset);
                mir_set_bytemask(ins, mir_bytemask(ins) << dest.offset);

                /* If there is a register LOD/bias, use it */
                if (ins->src[2] != ~0) {
                        assert(!(lod.offset & 3));
                        midgard_tex_register_select sel = {
                                .select = lod.reg,
                                .full = 1,
                                .component = lod.offset / 4
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
        mir_foreach_instr_global(ctx, ins)
                install_registers_instr(ctx, g, ins);
}
