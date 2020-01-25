/*
 * Copyright (C) 2019 Alyssa Rosenzweig <alyssa@rosenzweig.io>
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

void mir_rewrite_index_src_single(midgard_instruction *ins, unsigned old, unsigned new)
{
        for (unsigned i = 0; i < ARRAY_SIZE(ins->src); ++i) {
                if (ins->src[i] == old)
                        ins->src[i] = new;
        }
}

void mir_rewrite_index_dst_single(midgard_instruction *ins, unsigned old, unsigned new)
{
        if (ins->dest == old)
                ins->dest = new;
}

static midgard_vector_alu_src
mir_get_alu_src(midgard_instruction *ins, unsigned idx)
{
        unsigned b = (idx == 0) ? ins->alu.src1 : ins->alu.src2;
        return vector_alu_from_unsigned(b);
}

static void
mir_rewrite_index_src_single_swizzle(midgard_instruction *ins, unsigned old, unsigned new, unsigned *swizzle)
{
        for (unsigned i = 0; i < ARRAY_SIZE(ins->src); ++i) {
                if (ins->src[i] != old) continue;

                ins->src[i] = new;
                mir_compose_swizzle(ins->swizzle[i], swizzle, ins->swizzle[i]);
        }
}

void
mir_rewrite_index_src(compiler_context *ctx, unsigned old, unsigned new)
{
        mir_foreach_instr_global(ctx, ins) {
                mir_rewrite_index_src_single(ins, old, new);
        }
}

void
mir_rewrite_index_src_swizzle(compiler_context *ctx, unsigned old, unsigned new, unsigned *swizzle)
{
        mir_foreach_instr_global(ctx, ins) {
                mir_rewrite_index_src_single_swizzle(ins, old, new, swizzle);
        }
}

void
mir_rewrite_index_dst(compiler_context *ctx, unsigned old, unsigned new)
{
        mir_foreach_instr_global(ctx, ins) {
                mir_rewrite_index_dst_single(ins, old, new);
        }
}

void
mir_rewrite_index(compiler_context *ctx, unsigned old, unsigned new)
{
        mir_rewrite_index_src(ctx, old, new);
        mir_rewrite_index_dst(ctx, old, new);
}

unsigned
mir_use_count(compiler_context *ctx, unsigned value)
{
        unsigned used_count = 0;

        mir_foreach_instr_global(ctx, ins) {
                if (mir_has_arg(ins, value))
                        ++used_count;
        }

        return used_count;
}

/* Checks if a value is used only once (or totally dead), which is an important
 * heuristic to figure out if certain optimizations are Worth It (TM) */

bool
mir_single_use(compiler_context *ctx, unsigned value)
{
        /* We can replicate constants in places so who cares */
        if (value == SSA_FIXED_REGISTER(REGISTER_CONSTANT))
                return true;

        return mir_use_count(ctx, value) <= 1;
}

static bool
mir_nontrivial_raw_mod(midgard_vector_alu_src src, bool is_int)
{
        if (is_int)
                return src.mod == midgard_int_shift;
        else
                return src.mod;
}

static bool
mir_nontrivial_mod(midgard_vector_alu_src src, bool is_int, unsigned mask, unsigned *swizzle)
{
        if (mir_nontrivial_raw_mod(src, is_int)) return true;

        /* size-conversion */
        if (src.half) return true;

        for (unsigned c = 0; c < 16; ++c) {
                if (!(mask & (1 << c))) continue;
                if (swizzle[c] != c) return true;
        }

        return false;
}

bool
mir_nontrivial_source2_mod(midgard_instruction *ins)
{
        bool is_int = midgard_is_integer_op(ins->alu.op);

        midgard_vector_alu_src src2 =
                vector_alu_from_unsigned(ins->alu.src2);

        return mir_nontrivial_mod(src2, is_int, ins->mask, ins->swizzle[1]);
}

bool
mir_nontrivial_source2_mod_simple(midgard_instruction *ins)
{
        bool is_int = midgard_is_integer_op(ins->alu.op);

        midgard_vector_alu_src src2 =
                vector_alu_from_unsigned(ins->alu.src2);

        return mir_nontrivial_raw_mod(src2, is_int) || src2.half;
}

bool
mir_nontrivial_outmod(midgard_instruction *ins)
{
        bool is_int = midgard_is_integer_op(ins->alu.op);
        unsigned mod = ins->alu.outmod;

        /* Pseudo-outmod */
        if (ins->invert)
                return true;

        /* Type conversion is a sort of outmod */
        if (ins->alu.dest_override != midgard_dest_override_none)
                return true;

        if (is_int)
                return mod != midgard_outmod_int_wrap;
        else
                return mod != midgard_outmod_none;
}

/* Checks if an index will be used as a special register -- basically, if we're
 * used as the input to a non-ALU op */

bool
mir_special_index(compiler_context *ctx, unsigned idx)
{
        mir_foreach_instr_global(ctx, ins) {
                bool is_ldst = ins->type == TAG_LOAD_STORE_4;
                bool is_tex = ins->type == TAG_TEXTURE_4;
                bool is_writeout = ins->compact_branch && ins->writeout;

                if (!(is_ldst || is_tex || is_writeout))
                        continue;

                if (mir_has_arg(ins, idx))
                        return true;
        }

        return false;
}

/* Is a node written before a given instruction? */

bool
mir_is_written_before(compiler_context *ctx, midgard_instruction *ins, unsigned node)
{
        if (node >= SSA_FIXED_MINIMUM)
                return true;

        mir_foreach_instr_global(ctx, q) {
                if (q == ins)
                        break;

                if (q->dest == node)
                        return true;
        }

        return false;
}

/* Grabs the type size. */

midgard_reg_mode
mir_typesize(midgard_instruction *ins)
{
        if (ins->compact_branch)
                return midgard_reg_mode_32;

        /* TODO: Type sizes for texture */
        if (ins->type == TAG_TEXTURE_4)
                return midgard_reg_mode_32;

        if (ins->type == TAG_LOAD_STORE_4)
                return GET_LDST_SIZE(load_store_opcode_props[ins->load_store.op].props);

        if (ins->type == TAG_ALU_4) {
                midgard_reg_mode mode = ins->alu.reg_mode;

                /* If we have an override, step down by half */
                if (ins->alu.dest_override != midgard_dest_override_none) {
                        assert(mode > midgard_reg_mode_8);
                        mode--;
                }

                return mode;
        }

        unreachable("Invalid instruction type");
}

/* Grabs the size of a source */

midgard_reg_mode
mir_srcsize(midgard_instruction *ins, unsigned i)
{
        /* TODO: 16-bit textures/ldst */
        if (ins->type == TAG_TEXTURE_4 || ins->type == TAG_LOAD_STORE_4)
                return midgard_reg_mode_32;

        /* TODO: 16-bit branches */
        if (ins->compact_branch)
                return midgard_reg_mode_32;

        if (i >= 2) {
                /* TODO: 16-bit conditions, ffma */
                return midgard_reg_mode_32;
        }

        /* Default to type of the instruction */

        midgard_reg_mode mode = ins->alu.reg_mode;

        /* If we have a half modifier, step down by half */

        if ((mir_get_alu_src(ins, i)).half) {
                assert(mode > midgard_reg_mode_8);
                mode--;
        }

        return mode;
}

midgard_reg_mode
mir_mode_for_destsize(unsigned size)
{
        switch (size) {
        case 8:
                return midgard_reg_mode_8;
        case 16:
                return midgard_reg_mode_16;
        case 32:
                return midgard_reg_mode_32;
        case 64:
                return midgard_reg_mode_64;
        default:
                unreachable("Unknown destination size");
        }
}


/* Converts per-component mask to a byte mask */

uint16_t
mir_to_bytemask(midgard_reg_mode mode, unsigned mask)
{
        switch (mode) {
        case midgard_reg_mode_8:
                return mask;

        case midgard_reg_mode_16: {
                unsigned space =
                        (mask & 0x1) |
                        ((mask & 0x2) << (2 - 1)) |
                        ((mask & 0x4) << (4 - 2)) |
                        ((mask & 0x8) << (6 - 3)) |
                        ((mask & 0x10) << (8 - 4)) |
                        ((mask & 0x20) << (10 - 5)) |
                        ((mask & 0x40) << (12 - 6)) |
                        ((mask & 0x80) << (14 - 7));

                return space | (space << 1);
        }

        case midgard_reg_mode_32: {
                unsigned space =
                        (mask & 0x1) |
                        ((mask & 0x2) << (4 - 1)) |
                        ((mask & 0x4) << (8 - 2)) |
                        ((mask & 0x8) << (12 - 3));

                return space | (space << 1) | (space << 2) | (space << 3);
        }

        case midgard_reg_mode_64: {
                unsigned A = (mask & 0x1) ? 0xFF : 0x00;
                unsigned B = (mask & 0x2) ? 0xFF : 0x00;
                return A | (B << 8);
        }

        default:
                unreachable("Invalid register mode");
        }
}

/* ...and the inverse */

unsigned
mir_bytes_for_mode(midgard_reg_mode mode)
{
        switch (mode) {
        case midgard_reg_mode_8:
                return 1;
        case midgard_reg_mode_16:
                return 2;
        case midgard_reg_mode_32:
                return 4;
        case midgard_reg_mode_64:
                return 8;
        default:
                unreachable("Invalid register mode");
        }
}

uint16_t
mir_from_bytemask(uint16_t bytemask, midgard_reg_mode mode)
{
        unsigned value = 0;
        unsigned count = mir_bytes_for_mode(mode);

        for (unsigned c = 0, d = 0; c < 16; c += count, ++d) {
                bool a = (bytemask & (1 << c)) != 0;

                for (unsigned q = c; q < count; ++q)
                        assert(((bytemask & (1 << q)) != 0) == a);

                value |= (a << d);
        }

        return value;
}

/* Rounds up a bytemask to fill a given component count. Iterate each
 * component, and check if any bytes in the component are masked on */

uint16_t
mir_round_bytemask_up(uint16_t mask, midgard_reg_mode mode)
{
        unsigned bytes = mir_bytes_for_mode(mode);
        unsigned maxmask = mask_of(bytes);
        unsigned channels = 16 / bytes;

        for (unsigned c = 0; c < channels; ++c) {
                unsigned submask = maxmask << (c * bytes);

                if (mask & submask)
                        mask |= submask;
        }

        return mask;
}

/* Grabs the per-byte mask of an instruction (as opposed to per-component) */

uint16_t
mir_bytemask(midgard_instruction *ins)
{
        return mir_to_bytemask(mir_typesize(ins), ins->mask);
}

void
mir_set_bytemask(midgard_instruction *ins, uint16_t bytemask)
{
        ins->mask = mir_from_bytemask(bytemask, mir_typesize(ins));
}

/* Checks if we should use an upper destination override, rather than the lower
 * one in the IR. Returns zero if no, returns the bytes to shift otherwise */

unsigned
mir_upper_override(midgard_instruction *ins)
{
        /* If there is no override, there is no upper override, tautology */
        if (ins->alu.dest_override == midgard_dest_override_none)
                return 0;

        /* Make sure we didn't already lower somehow */
        assert(ins->alu.dest_override == midgard_dest_override_lower);

        /* What is the mask in terms of currently? */
        midgard_reg_mode type = mir_typesize(ins);

        /* There are 16 bytes per vector, so there are (16/bytes)
         * components per vector. So the magic half is half of
         * (16/bytes), which simplifies to 8/bytes */

        unsigned threshold = 8 / mir_bytes_for_mode(type);

        /* How many components did we shift over? */
        unsigned zeroes = __builtin_ctz(ins->mask);

        /* Did we hit the threshold? */
        return (zeroes >= threshold) ? threshold : 0;
}

/* Creates a mask of the components of a node read by an instruction, by
 * analyzing the swizzle with respect to the instruction's mask. E.g.:
 *
 *  fadd r0.xz, r1.yyyy, r2.zwyx
 *
 * will return a mask of Z/Y for r2
 */

static uint16_t
mir_bytemask_of_read_components_single(unsigned *swizzle, unsigned inmask, midgard_reg_mode mode)
{
        unsigned cmask = 0;

        for (unsigned c = 0; c < MIR_VEC_COMPONENTS; ++c) {
                if (!(inmask & (1 << c))) continue;
                cmask |= (1 << swizzle[c]);
        }

        return mir_to_bytemask(mode, cmask);
}

uint16_t
mir_bytemask_of_read_components(midgard_instruction *ins, unsigned node)
{
        uint16_t mask = 0;

        if (node == ~0)
                return 0;

        mir_foreach_src(ins, i) {
                if (ins->src[i] != node) continue;

                /* Branch writeout uses all components */
                if (ins->compact_branch && ins->writeout && (i == 0))
                        return 0xFFFF;

                /* Conditional branches read one 32-bit component = 4 bytes (TODO: multi branch??) */
                if (ins->compact_branch && ins->branch.conditional && (i == 0))
                        return 0xF;

                /* ALU ops act componentwise so we need to pay attention to
                 * their mask. Texture/ldst does not so we don't clamp source
                 * readmasks based on the writemask */
                unsigned qmask = (ins->type == TAG_ALU_4) ? ins->mask : ~0;

                /* Handle dot products and things */
                if (ins->type == TAG_ALU_4 && !ins->compact_branch) {
                        unsigned props = alu_opcode_props[ins->alu.op].props;

                        unsigned channel_override = GET_CHANNEL_COUNT(props);

                        if (channel_override)
                                qmask = mask_of(channel_override);
                }

                mask |= mir_bytemask_of_read_components_single(ins->swizzle[i], qmask, mir_srcsize(ins, i));
        }

        return mask;
}

/* Register allocation occurs after instruction scheduling, which is fine until
 * we start needing to spill registers and therefore insert instructions into
 * an already-scheduled program. We don't have to be terribly efficient about
 * this, since spilling is already slow. So just semantically we need to insert
 * the instruction into a new bundle before/after the bundle of the instruction
 * in question */

static midgard_bundle
mir_bundle_for_op(compiler_context *ctx, midgard_instruction ins)
{
        midgard_instruction *u = mir_upload_ins(ctx, ins);

        midgard_bundle bundle = {
                .tag = ins.type,
                .instruction_count = 1,
                .instructions = { u },
        };

        if (bundle.tag == TAG_ALU_4) {
                assert(OP_IS_MOVE(u->alu.op));
                u->unit = UNIT_VMUL;

                size_t bytes_emitted = sizeof(uint32_t) + sizeof(midgard_reg_info) + sizeof(midgard_vector_alu);
                bundle.padding = ~(bytes_emitted - 1) & 0xF;
                bundle.control = ins.type | u->unit;
        }

        return bundle;
}

static unsigned
mir_bundle_idx_for_ins(midgard_instruction *tag, midgard_block *block)
{
        midgard_bundle *bundles =
                (midgard_bundle *) block->bundles.data;

        size_t count = (block->bundles.size / sizeof(midgard_bundle));

        for (unsigned i = 0; i < count; ++i) {
                for (unsigned j = 0; j < bundles[i].instruction_count; ++j) {
                        if (bundles[i].instructions[j] == tag)
                                return i;
                }
        }

        mir_print_instruction(tag);
        unreachable("Instruction not scheduled in block");
}

void
mir_insert_instruction_before_scheduled(
        compiler_context *ctx,
        midgard_block *block,
        midgard_instruction *tag,
        midgard_instruction ins)
{
        unsigned before = mir_bundle_idx_for_ins(tag, block);
        size_t count = util_dynarray_num_elements(&block->bundles, midgard_bundle);
        UNUSED void *unused = util_dynarray_grow(&block->bundles, midgard_bundle, 1);

        midgard_bundle *bundles = (midgard_bundle *) block->bundles.data;
        memmove(bundles + before + 1, bundles + before, (count - before) * sizeof(midgard_bundle));
        midgard_bundle *before_bundle = bundles + before + 1;

        midgard_bundle new = mir_bundle_for_op(ctx, ins);
        memcpy(bundles + before, &new, sizeof(new));

        list_addtail(&new.instructions[0]->link, &before_bundle->instructions[0]->link);
        block->quadword_count += midgard_word_size[new.tag];
}

void
mir_insert_instruction_after_scheduled(
        compiler_context *ctx,
        midgard_block *block,
        midgard_instruction *tag,
        midgard_instruction ins)
{
        /* We need to grow the bundles array to add our new bundle */
        size_t count = util_dynarray_num_elements(&block->bundles, midgard_bundle);
        UNUSED void *unused = util_dynarray_grow(&block->bundles, midgard_bundle, 1);

        /* Find the bundle that we want to insert after */
        unsigned after = mir_bundle_idx_for_ins(tag, block);

        /* All the bundles after that one, we move ahead by one */
        midgard_bundle *bundles = (midgard_bundle *) block->bundles.data;
        memmove(bundles + after + 2, bundles + after + 1, (count - after - 1) * sizeof(midgard_bundle));
        midgard_bundle *after_bundle = bundles + after;

        midgard_bundle new = mir_bundle_for_op(ctx, ins);
        memcpy(bundles + after + 1, &new, sizeof(new));
        list_add(&new.instructions[0]->link, &after_bundle->instructions[after_bundle->instruction_count - 1]->link);
        block->quadword_count += midgard_word_size[new.tag];
}

/* Flip the first-two arguments of a (binary) op. Currently ALU
 * only, no known uses for ldst/tex */

void
mir_flip(midgard_instruction *ins)
{
        unsigned temp = ins->src[0];
        ins->src[0] = ins->src[1];
        ins->src[1] = temp;

        assert(ins->type == TAG_ALU_4);

        temp = ins->alu.src1;
        ins->alu.src1 = ins->alu.src2;
        ins->alu.src2 = temp;

        unsigned temp_swizzle[16];
        memcpy(temp_swizzle, ins->swizzle[0], sizeof(ins->swizzle[0]));
        memcpy(ins->swizzle[0], ins->swizzle[1], sizeof(ins->swizzle[0]));
        memcpy(ins->swizzle[1], temp_swizzle, sizeof(ins->swizzle[0]));
}

/* Before squashing, calculate ctx->temp_count just by observing the MIR */

void
mir_compute_temp_count(compiler_context *ctx)
{
        if (ctx->temp_count)
                return;

        unsigned max_dest = 0;

        mir_foreach_instr_global(ctx, ins) {
                if (ins->dest < SSA_FIXED_MINIMUM)
                        max_dest = MAX2(max_dest, ins->dest + 1);
        }

        ctx->temp_count = max_dest;
}
