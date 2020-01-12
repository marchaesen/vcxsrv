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
#include "util/u_math.h"
#include "util/u_memory.h"
#include "lcra.h"
#include "midgard_quirks.h"

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
index_to_reg(compiler_context *ctx, struct lcra_state *l, unsigned reg, midgard_reg_mode size)
{
        /* Check for special cases */
        if (reg == ~0)
                return default_phys_reg(REGISTER_UNUSED, size);
        else if (reg >= SSA_FIXED_MINIMUM)
                return default_phys_reg(SSA_REG_FROM_FIXED(reg), size);
        else if (!l)
                return default_phys_reg(REGISTER_UNUSED, size);

        struct phys_reg r = {
                .reg = l->solutions[reg] / 16,
                .offset = l->solutions[reg] & 0xF,
                .size = mir_bytes_for_mode(size)
        };

        /* Report that we actually use this register, and return it */

        if (r.reg < 16)
                ctx->work_registers = MAX2(ctx->work_registers, r.reg);

        return r;
}

static void
set_class(unsigned *classes, unsigned node, unsigned class)
{
        if (node < SSA_FIXED_MINIMUM && class != classes[node]) {
                assert(classes[node] == REG_CLASS_WORK);
                classes[node] = class;
        }
}

/* Special register classes impose special constraints on who can read their
 * values, so check that */

static bool
check_read_class(unsigned *classes, unsigned tag, unsigned node)
{
        /* Non-nodes are implicitly ok */
        if (node >= SSA_FIXED_MINIMUM)
                return true;

        switch (classes[node]) {
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

        switch (classes[node]) {
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
                struct lcra_state *l,
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

                                unsigned mask = mir_bytemask(bun->instructions[q]);
                                unsigned rmask = mir_bytemask_of_read_components(bun->instructions[j], bun->instructions[j]->src[s]);
                                lcra_add_node_interference(l, bun->instructions[q]->dest, mask, bun->instructions[j]->src[s], rmask);
                        }
                }
        }
}

static void
mir_compute_bundle_interference(
                compiler_context *ctx,
                struct lcra_state *l,
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
                struct lcra_state *l)
{
        /* First, we need liveness information to be computed per block */
        mir_compute_liveness(ctx);

        /* We need to force r1.w live throughout a blend shader */

        if (ctx->is_blend) {
                unsigned r1w = ~0;

                mir_foreach_block(ctx, block) {
                        mir_foreach_instr_in_block_rev(block, ins) {
                                if (ins->writeout)
                                        r1w = ins->src[2];
                        }

                        if (r1w != ~0)
                                break;
                }

                mir_foreach_instr_global(ctx, ins) {
                        if (ins->dest < ctx->temp_count)
                                lcra_add_node_interference(l, ins->dest, mir_bytemask(ins), r1w, 0xF);
                }
        }

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
                                        if (live[i]) {
                                                unsigned mask = mir_bytemask(ins);
                                                lcra_add_node_interference(l, dest, mask, i, live[i]);
                                        }
                        }

                        /* Update live_in */
                        mir_liveness_ins_update(live, ins, ctx->temp_count);
                }

                mir_foreach_bundle_in_block(blk, bun)
                        mir_compute_bundle_interference(ctx, l, bun);

                free(live);
        }
}

/* This routine performs the actual register allocation. It should be succeeded
 * by install_registers */

static struct lcra_state *
allocate_registers(compiler_context *ctx, bool *spilled)
{
        /* The number of vec4 work registers available depends on when the
         * uniforms start, so compute that first */
        int work_count = 16 - MAX2((ctx->uniform_cutoff - 8), 0);

       /* No register allocation to do with no SSA */

        if (!ctx->temp_count)
                return NULL;

        struct lcra_state *l = lcra_alloc_equations(ctx->temp_count, 1, 8, 16, 5);

        /* Starts of classes, in bytes */
        l->class_start[REG_CLASS_WORK]  = 16 * 0;
        l->class_start[REG_CLASS_LDST]  = 16 * 26;
        l->class_start[REG_CLASS_TEXR]  = 16 * 28;
        l->class_start[REG_CLASS_TEXW]  = 16 * 28;

        l->class_size[REG_CLASS_WORK] = 16 * work_count;
        l->class_size[REG_CLASS_LDST]  = 16 * 2;
        l->class_size[REG_CLASS_TEXR]  = 16 * 2;
        l->class_size[REG_CLASS_TEXW]  = 16 * 2;

        lcra_set_disjoint_class(l, REG_CLASS_TEXR, REG_CLASS_TEXW);

        /* To save space on T*20, we don't have real texture registers.
         * Instead, tex inputs reuse the load/store pipeline registers, and
         * tex outputs use work r0/r1. Note we still use TEXR/TEXW classes,
         * noting that this handles interferences and sizes correctly. */

        if (ctx->quirks & MIDGARD_INTERPIPE_REG_ALIASING) {
                l->class_start[REG_CLASS_TEXR] = l->class_start[REG_CLASS_LDST];
                l->class_start[REG_CLASS_TEXW] = l->class_start[REG_CLASS_WORK];
        }

        unsigned *found_class = calloc(sizeof(unsigned), ctx->temp_count);
        unsigned *min_alignment = calloc(sizeof(unsigned), ctx->temp_count);

        mir_foreach_instr_global(ctx, ins) {
                if (ins->dest >= SSA_FIXED_MINIMUM) continue;

                /* 0 for x, 1 for xy, 2 for xyz, 3 for xyzw */
                int class = util_logbase2(ins->mask);

                /* Use the largest class if there's ambiguity, this
                 * handles partial writes */

                int dest = ins->dest;
                found_class[dest] = MAX2(found_class[dest], class);

                /* XXX: Ensure swizzles align the right way with more LCRA constraints? */
                if (ins->type == TAG_ALU_4 && ins->alu.reg_mode != midgard_reg_mode_32)
                        min_alignment[dest] = 3; /* (1 << 3) = 8 */

                if (ins->type == TAG_LOAD_STORE_4 && ins->load_64)
                        min_alignment[dest] = 3;

                /* We don't have a swizzle for the conditional and we don't
                 * want to muck with the conditional itself, so just force
                 * alignment for now */

                if (ins->type == TAG_ALU_4 && OP_IS_CSEL_V(ins->alu.op))
                        min_alignment[dest] = 4; /* 1 << 4= 16-byte = vec4 */

        }

        for (unsigned i = 0; i < ctx->temp_count; ++i) {
                lcra_set_alignment(l, i, min_alignment[i] ? min_alignment[i] : 2);
                lcra_restrict_range(l, i, (found_class[i] + 1) * 4);
        }
        
        free(found_class);
        free(min_alignment);

        /* Next, we'll determine semantic class. We default to zero (work).
         * But, if we're used with a special operation, that will force us to a
         * particular class. Each node must be assigned to exactly one class; a
         * prepass before RA should have lowered what-would-have-been
         * multiclass nodes into a series of moves to break it up into multiple
         * nodes (TODO) */

        mir_foreach_instr_global(ctx, ins) {
                /* Check if this operation imposes any classes */

                if (ins->type == TAG_LOAD_STORE_4) {
                        set_class(l->class, ins->src[0], REG_CLASS_LDST);
                        set_class(l->class, ins->src[1], REG_CLASS_LDST);
                        set_class(l->class, ins->src[2], REG_CLASS_LDST);

                        if (OP_IS_VEC4_ONLY(ins->load_store.op)) {
                                lcra_restrict_range(l, ins->dest, 16);
                                lcra_restrict_range(l, ins->src[0], 16);
                                lcra_restrict_range(l, ins->src[1], 16);
                                lcra_restrict_range(l, ins->src[2], 16);
                        }
                } else if (ins->type == TAG_TEXTURE_4) {
                        set_class(l->class, ins->dest, REG_CLASS_TEXW);
                        set_class(l->class, ins->src[0], REG_CLASS_TEXR);
                        set_class(l->class, ins->src[1], REG_CLASS_TEXR);
                        set_class(l->class, ins->src[2], REG_CLASS_TEXR);
                        set_class(l->class, ins->src[3], REG_CLASS_TEXR);

                        /* Texture offsets need to be aligned to vec4, since
                         * the swizzle for x is forced to x in hardware, while
                         * the other components are free. TODO: Relax to 8 for
                         * half-registers if that ever occurs. */

                        //lcra_restrict_range(l, ins->src[3], 16);
                }
        }

        /* Check that the semantics of the class are respected */
        mir_foreach_instr_global(ctx, ins) {
                assert(check_write_class(l->class, ins->type, ins->dest));
                assert(check_read_class(l->class, ins->type, ins->src[0]));
                assert(check_read_class(l->class, ins->type, ins->src[1]));
                assert(check_read_class(l->class, ins->type, ins->src[2]));
        }

        /* Mark writeout to r0, render target to r1.z, unknown to r1.w */
        mir_foreach_instr_global(ctx, ins) {
                if (!(ins->compact_branch && ins->writeout)) continue;

                if (ins->src[0] < ctx->temp_count)
                        l->solutions[ins->src[0]] = 0;

                if (ins->src[1] < ctx->temp_count)
                        l->solutions[ins->src[1]] = (16 * 1) + COMPONENT_Z * 4;

                if (ins->src[2] < ctx->temp_count)
                        l->solutions[ins->src[2]] = (16 * 1) + COMPONENT_W * 4;
        }
        
        mir_compute_interference(ctx, l);

        *spilled = !lcra_solve(l);
        return l;
}


/* Once registers have been decided via register allocation
 * (allocate_registers), we need to rewrite the MIR to use registers instead of
 * indices */

static void
install_registers_instr(
        compiler_context *ctx,
        struct lcra_state *l,
        midgard_instruction *ins)
{
        switch (ins->type) {
        case TAG_ALU_4:
        case TAG_ALU_8:
        case TAG_ALU_12:
        case TAG_ALU_16: {
                 if (ins->compact_branch)
                         return;

                struct phys_reg src1 = index_to_reg(ctx, l, ins->src[0], mir_srcsize(ins, 0));
                struct phys_reg src2 = index_to_reg(ctx, l, ins->src[1], mir_srcsize(ins, 1));
                struct phys_reg dest = index_to_reg(ctx, l, ins->dest, mir_typesize(ins));

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
                        struct phys_reg src = index_to_reg(ctx, l, ins->src[0], mir_srcsize(ins, 0));
                        assert(src.reg == 26 || src.reg == 27);

                        ins->load_store.reg = src.reg - 26;
                        offset_swizzle(ins->swizzle[0], src.offset, src.size, 0);
               } else {
                        struct phys_reg dst = index_to_reg(ctx, l, ins->dest, mir_typesize(ins));

                        ins->load_store.reg = dst.reg;
                        offset_swizzle(ins->swizzle[0], 0, 4, dst.offset);
                        mir_set_bytemask(ins, mir_bytemask(ins) << dst.offset);
                }

                /* We also follow up by actual arguments */

                unsigned src2 = ins->src[1];
                unsigned src3 = ins->src[2];

                if (src2 != ~0) {
                        struct phys_reg src = index_to_reg(ctx, l, src2, mir_srcsize(ins, 1));
                        unsigned component = src.offset / src.size;
                        assert(component * src.size == src.offset);
                        ins->load_store.arg_1 |= midgard_ldst_reg(src.reg, component);
                }

                if (src3 != ~0) {
                        struct phys_reg src = index_to_reg(ctx, l, src3, mir_srcsize(ins, 2));
                        unsigned component = src.offset / src.size;
                        assert(component * src.size == src.offset);
                        ins->load_store.arg_2 |= midgard_ldst_reg(src.reg, component);
                }
 
                break;
        }

        case TAG_TEXTURE_4: {
                /* Grab RA results */
                struct phys_reg dest = index_to_reg(ctx, l, ins->dest, mir_typesize(ins));
                struct phys_reg coord = index_to_reg(ctx, l, ins->src[1], mir_srcsize(ins, 1));
                struct phys_reg lod = index_to_reg(ctx, l, ins->src[2], mir_srcsize(ins, 2));
                struct phys_reg offset = index_to_reg(ctx, l, ins->src[3], mir_srcsize(ins, 2));

                /* First, install the texture coordinate */
                ins->texture.in_reg_full = 1;
                ins->texture.in_reg_upper = 0;
                ins->texture.in_reg_select = coord.reg & 1;
                offset_swizzle(ins->swizzle[1], coord.offset, coord.size, 0);

                /* Next, install the destination */
                ins->texture.out_full = 1;
                ins->texture.out_upper = 0;
                ins->texture.out_reg_select = dest.reg & 1;
                offset_swizzle(ins->swizzle[0], 0, 4, dest.offset);
                mir_set_bytemask(ins, mir_bytemask(ins) << dest.offset);

                /* If there is a register LOD/bias, use it */
                if (ins->src[2] != ~0) {
                        assert(!(lod.offset & 3));
                        midgard_tex_register_select sel = {
                                .select = lod.reg & 1,
                                .full = 1,
                                .component = lod.offset / 4
                        };

                        uint8_t packed;
                        memcpy(&packed, &sel, sizeof(packed));
                        ins->texture.bias = packed;
                }

                /* If there is an offset register, install it */
                if (ins->src[3] != ~0) {
                        unsigned x = offset.offset / 4;
                        unsigned y = x + 1;
                        unsigned z = x + 2;

                        /* Check range, TODO: half-registers */
                        assert(z < 4);

                        ins->texture.offset =
                                (1)                   | /* full */
                                (offset.reg & 1) << 1 | /* select */
                                (0 << 2)              | /* upper */
                                (x << 3)              | /* swizzle */
                                (y << 5)              | /* swizzle */
                                (z << 7);               /* swizzle */
                }

                break;
        }

        default:
                break;
        }
}

static void
install_registers(compiler_context *ctx, struct lcra_state *l)
{
        mir_foreach_instr_global(ctx, ins)
                install_registers_instr(ctx, l, ins);
}


/* If register allocation fails, find the best spill node */

static signed
mir_choose_spill_node(
                compiler_context *ctx,
                struct lcra_state *l)
{
        /* We can't spill a previously spilled value or an unspill */

        mir_foreach_instr_global(ctx, ins) {
                if (ins->no_spill & (1 << l->spill_class)) {
                        lcra_set_node_spill_cost(l, ins->dest, -1);

                        if (l->spill_class != REG_CLASS_WORK) {
                                mir_foreach_src(ins, s)
                                        lcra_set_node_spill_cost(l, ins->src[s], -1);
                        }
                }
        }

        return lcra_get_best_spill_node(l);
}

/* Once we've chosen a spill node, spill it */

static void
mir_spill_register(
                compiler_context *ctx,
                unsigned spill_node,
                unsigned spill_class,
                unsigned *spill_count)
{
        unsigned spill_index = ctx->temp_count;

        /* We have a spill node, so check the class. Work registers
         * legitimately spill to TLS, but special registers just spill to work
         * registers */

        bool is_special = spill_class != REG_CLASS_WORK;
        bool is_special_w = spill_class == REG_CLASS_TEXW;

        /* Allocate TLS slot (maybe) */
        unsigned spill_slot = !is_special ? (*spill_count)++ : 0;

        /* For TLS, replace all stores to the spilled node. For
         * special reads, just keep as-is; the class will be demoted
         * implicitly. For special writes, spill to a work register */

        if (!is_special || is_special_w) {
                if (is_special_w)
                        spill_slot = spill_index++;

                mir_foreach_block(ctx, block) {
                mir_foreach_instr_in_block_safe(block, ins) {
                        if (ins->dest != spill_node) continue;

                        midgard_instruction st;

                        if (is_special_w) {
                                st = v_mov(spill_node, spill_slot);
                                st.no_spill |= (1 << spill_class);
                        } else {
                                ins->dest = spill_index++;
                                ins->no_spill |= (1 << spill_class);
                                st = v_load_store_scratch(ins->dest, spill_slot, true, ins->mask);
                        }

                        /* Hint: don't rewrite this node */
                        st.hint = true;

                        mir_insert_instruction_after_scheduled(ctx, block, ins, st);

                        if (!is_special)
                                ctx->spills++;
                }
                }
        }

        /* For special reads, figure out how many bytes we need */
        unsigned read_bytemask = 0;

        mir_foreach_instr_global_safe(ctx, ins) {
                read_bytemask |= mir_bytemask_of_read_components(ins, spill_node);
        }

        /* Insert a load from TLS before the first consecutive
         * use of the node, rewriting to use spilled indices to
         * break up the live range. Or, for special, insert a
         * move. Ironically the latter *increases* register
         * pressure, but the two uses of the spilling mechanism
         * are somewhat orthogonal. (special spilling is to use
         * work registers to back special registers; TLS
         * spilling is to use memory to back work registers) */

        mir_foreach_block(ctx, block) {
                mir_foreach_instr_in_block(block, ins) {
                        /* We can't rewrite the moves used to spill in the
                         * first place. These moves are hinted. */
                        if (ins->hint) continue;

                        /* If we don't use the spilled value, nothing to do */
                        if (!mir_has_arg(ins, spill_node)) continue;

                        unsigned index = 0;

                        if (!is_special_w) {
                                index = ++spill_index;

                                midgard_instruction *before = ins;
                                midgard_instruction st;

                                if (is_special) {
                                        /* Move */
                                        st = v_mov(spill_node, index);
                                        st.no_spill |= (1 << spill_class);
                                } else {
                                        /* TLS load */
                                        st = v_load_store_scratch(index, spill_slot, false, 0xF);
                                }

                                /* Mask the load based on the component count
                                 * actually needed to prevent RA loops */

                                st.mask = mir_from_bytemask(read_bytemask, midgard_reg_mode_32);

                                mir_insert_instruction_before_scheduled(ctx, block, before, st);
                        } else {
                                /* Special writes already have their move spilled in */
                                index = spill_slot;
                        }


                        /* Rewrite to use */
                        mir_rewrite_index_src_single(ins, spill_node, index);

                        if (!is_special)
                                ctx->fills++;
                }
        }

        /* Reset hints */

        mir_foreach_instr_global(ctx, ins) {
                ins->hint = false;
        }
}

/* Run register allocation in a loop, spilling until we succeed */

void
mir_ra(compiler_context *ctx)
{
        struct lcra_state *l = NULL;
        bool spilled = false;
        int iter_count = 1000; /* max iterations */

        /* Number of 128-bit slots in memory we've spilled into */
        unsigned spill_count = 0;


        mir_create_pipeline_registers(ctx);

        do {
                if (spilled) {
                        signed spill_node = mir_choose_spill_node(ctx, l);

                        if (spill_node == -1) {
                                fprintf(stderr, "ERROR: Failed to choose spill node\n");
                                return;
                        }

                        mir_spill_register(ctx, spill_node, l->spill_class, &spill_count);
                }

                mir_squeeze_index(ctx);
                mir_invalidate_liveness(ctx);

                if (l) {
                        lcra_free(l);
                        l = NULL;
                }

                l = allocate_registers(ctx, &spilled);
        } while(spilled && ((iter_count--) > 0));

        if (iter_count <= 0) {
                fprintf(stderr, "panfrost: Gave up allocating registers, rendering will be incomplete\n");
                assert(0);
        }

        /* Report spilling information. spill_count is in 128-bit slots (vec4 x
         * fp32), but tls_size is in bytes, so multiply by 16 */

        ctx->tls_size = spill_count * 16;

        install_registers(ctx, l);

        lcra_free(l);
}
