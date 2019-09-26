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
index_to_reg(compiler_context *ctx, struct ra_graph *g, unsigned reg)
{
        /* Check for special cases */
        if (reg == ~0)
                return default_phys_reg(REGISTER_UNUSED);
        else if (reg >= SSA_FIXED_MINIMUM)
                return default_phys_reg(SSA_REG_FROM_FIXED(reg));
        else if (!g)
                return default_phys_reg(REGISTER_UNUSED);

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
                                v_mov(idx, blank_alu_src, i) :
                                v_mov(i, blank_alu_src, idx);

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
                                        m = v_mov(i, blank_alu_src, idx);
                                        m.mask = mir_mask_of_read_components(pre_use, i);
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

/* Routines for liveness analysis */

static void
liveness_gen(uint8_t *live, unsigned node, unsigned max, unsigned mask)
{
        if (node >= max)
                return;

        live[node] |= mask;
}

static void
liveness_kill(uint8_t *live, unsigned node, unsigned max, unsigned mask)
{
        if (node >= max)
                return;

        live[node] &= ~mask;
}

/* Updates live_in for a single instruction */

static void
liveness_ins_update(uint8_t *live, midgard_instruction *ins, unsigned max)
{
        /* live_in[s] = GEN[s] + (live_out[s] - KILL[s]) */

        liveness_kill(live, ins->dest, max, ins->mask);

        mir_foreach_src(ins, src) {
                unsigned node = ins->src[src];
                unsigned mask = mir_mask_of_read_components(ins, node);

                liveness_gen(live, node, max, mask);
        }
}

/* live_out[s] = sum { p in succ[s] } ( live_in[p] ) */

static void
liveness_block_live_out(compiler_context *ctx, midgard_block *blk)
{
        mir_foreach_successor(blk, succ) {
                for (unsigned i = 0; i < ctx->temp_count; ++i)
                        blk->live_out[i] |= succ->live_in[i];
        }
}

/* Liveness analysis is a backwards-may dataflow analysis pass. Within a block,
 * we compute live_out from live_in. The intrablock pass is linear-time. It
 * returns whether progress was made. */

static bool
liveness_block_update(compiler_context *ctx, midgard_block *blk)
{
        bool progress = false;

        liveness_block_live_out(ctx, blk);

        uint8_t *live = mem_dup(blk->live_out, ctx->temp_count);

        mir_foreach_instr_in_block_rev(blk, ins)
                liveness_ins_update(live, ins, ctx->temp_count);

        /* To figure out progress, diff live_in */

        for (unsigned i = 0; (i < ctx->temp_count) && !progress; ++i)
                progress |= (blk->live_in[i] != live[i]);

        free(blk->live_in);
        blk->live_in = live;

        return progress;
}

/* Globally, liveness analysis uses a fixed-point algorithm based on a
 * worklist. We initialize a work list with the exit block. We iterate the work
 * list to compute live_in from live_out for each block on the work list,
 * adding the predecessors of the block to the work list if we made progress.
 */

static void
mir_compute_liveness(
                compiler_context *ctx,
                struct ra_graph *g)
{
        /* List of midgard_block */
        struct set *work_list;

        work_list = _mesa_set_create(ctx,
                        _mesa_hash_pointer,
                        _mesa_key_pointer_equal);

        /* Allocate */

        mir_foreach_block(ctx, block) {
                block->live_in = calloc(ctx->temp_count, 1);
                block->live_out = calloc(ctx->temp_count, 1);
        }

        /* Initialize the work list with the exit block */
        struct set_entry *cur;

        midgard_block *exit = mir_exit_block(ctx);
        cur = _mesa_set_add(work_list, exit);

        /* Iterate the work list */

        do {
                /* Pop off a block */
                midgard_block *blk = (struct midgard_block *) cur->key;
                _mesa_set_remove(work_list, cur);

                /* Update its liveness information */
                bool progress = liveness_block_update(ctx, blk);

                /* If we made progress, we need to process the predecessors */

                if (progress || (blk == exit)) {
                        mir_foreach_predecessor(blk, pred)
                                _mesa_set_add(work_list, pred);
                }
        } while((cur = _mesa_set_next_entry(work_list, NULL)) != NULL);

        /* Now that every block has live_in/live_out computed, we can determine
         * interference by walking each block linearly. Take live_out at the
         * end of each block and walk the block backwards. */

        mir_foreach_block(ctx, blk) {
                uint8_t *live = calloc(ctx->temp_count, 1);

                mir_foreach_successor(blk, succ) {
                        for (unsigned i = 0; i < ctx->temp_count; ++i)
                                live[i] |= succ->live_in[i];
                }

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
                        liveness_ins_update(live, ins, ctx->temp_count);
                }

                free(live);
        }

        mir_foreach_block(ctx, blk) {
                free(blk->live_in);
                free(blk->live_out);
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

        mir_compute_liveness(ctx, g);

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

                struct phys_reg src1 = index_to_reg(ctx, g, ins->src[0]);
                struct phys_reg src2 = index_to_reg(ctx, g, ins->src[1]);
                struct phys_reg dest = index_to_reg(ctx, g, ins->dest);

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
                        mod2.swizzle = compose_swizzle(
                                               mod2.swizzle, uncomposed_mask, src2, dest);
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
                        struct phys_reg src = index_to_reg(ctx, g, ins->src[0]);
                        assert(src.reg == 26 || src.reg == 27);

                        ins->load_store.reg = src.reg - 26;

                        unsigned shift = __builtin_ctz(src.mask);
                        unsigned adjusted_mask = src.mask >> shift;
                        assert(((adjusted_mask + 1) & adjusted_mask) == 0);

                        unsigned new_swizzle = 0;
                        for (unsigned q = 0; q < 4; ++q) {
                                unsigned c = (ins->load_store.swizzle >> (2*q)) & 3;
                                new_swizzle |= (c + shift) << (2*q);
                        }

                        ins->load_store.swizzle = compose_swizzle(
                                                          new_swizzle, src.mask,
                                                          default_phys_reg(0), src);
               } else {
                        struct phys_reg src = index_to_reg(ctx, g, ins->dest);

                        ins->load_store.reg = src.reg;

                        ins->load_store.swizzle = compose_swizzle(
                                                          ins->load_store.swizzle, 0xF,
                                                          default_phys_reg(0), src);

                        ins->mask = compose_writemask(
                                            ins->mask, src);
                }

                /* We also follow up by actual arguments */

                int src2 =
                        encodes_src ? ins->src[1] : ins->src[0];

                int src3 =
                        encodes_src ? ins->src[2] : ins->src[1];

                if (src2 >= 0) {
                        struct phys_reg src = index_to_reg(ctx, g, src2);
                        unsigned component = __builtin_ctz(src.mask);
                        ins->load_store.arg_1 |= midgard_ldst_reg(src.reg, component);
                }

                if (src3 >= 0) {
                        struct phys_reg src = index_to_reg(ctx, g, src3);
                        unsigned component = __builtin_ctz(src.mask);
                        ins->load_store.arg_2 |= midgard_ldst_reg(src.reg, component);
                }
 
                break;
        }

        case TAG_TEXTURE_4: {
                /* Grab RA results */
                struct phys_reg dest = index_to_reg(ctx, g, ins->dest);
                struct phys_reg coord = index_to_reg(ctx, g, ins->src[0]);
                struct phys_reg lod = index_to_reg(ctx, g, ins->src[1]);

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
                ins->texture.swizzle =
                        compose_swizzle(ins->texture.swizzle, dest.mask, dest, dest);
                ins->mask =
                        compose_writemask(ins->mask, dest);

                /* If there is a register LOD/bias, use it */
                if (ins->src[1] != ~0) {
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
        mir_foreach_instr_global(ctx, ins)
                install_registers_instr(ctx, g, ins);
}
