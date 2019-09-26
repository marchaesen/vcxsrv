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

unsigned
mir_get_swizzle(midgard_instruction *ins, unsigned idx)
{
        if (ins->type == TAG_ALU_4) {
                if (idx == 2)
                        return ins->csel_swizzle;

                unsigned b = (idx == 0) ? ins->alu.src1 : ins->alu.src2;

                midgard_vector_alu_src s =
                        vector_alu_from_unsigned(b);

                return s.swizzle;
        } else if (ins->type == TAG_LOAD_STORE_4) {
                /* Main swizzle of a load is on the destination */
                if (!OP_IS_STORE(ins->load_store.op))
                        idx++;

                switch (idx) {
                case 0:
                        return ins->load_store.swizzle;
                case 1:
                case 2: {
                        uint8_t raw =
                                (idx == 2) ? ins->load_store.arg_2 : ins->load_store.arg_1;

                        return component_to_swizzle(midgard_ldst_select(raw).component);
                }
                default:
                        unreachable("Unknown load/store source");
                }
        } else if (ins->type == TAG_TEXTURE_4) {
                switch (idx) {
                case 0:
                        return ins->texture.in_reg_swizzle;
                case 1:
                        /* Swizzle on bias doesn't make sense */
                        return 0;
                default:
                        unreachable("Unknown texture source");
                }
        } else {
                unreachable("Unknown type");
        }
}

void
mir_set_swizzle(midgard_instruction *ins, unsigned idx, unsigned new)
{
        if (ins->type == TAG_ALU_4) {
                unsigned b = (idx == 0) ? ins->alu.src1 : ins->alu.src2;

                midgard_vector_alu_src s =
                        vector_alu_from_unsigned(b);

                s.swizzle = new;
                unsigned pack = vector_alu_srco_unsigned(s);

                if (idx == 0)
                        ins->alu.src1 = pack;
                else
                        ins->alu.src2 = pack;
        } else if (ins->type == TAG_LOAD_STORE_4) {
                /* Main swizzle of a load is on the destination */
                if (!OP_IS_STORE(ins->load_store.op))
                        idx++;

                switch (idx) {
                case 0:
                        ins->load_store.swizzle = new;
                        break;
                case 1:
                case 2: {
                        uint8_t raw =
                                (idx == 2) ? ins->load_store.arg_2 : ins->load_store.arg_1;

                        midgard_ldst_register_select sel
                                = midgard_ldst_select(raw);
                        sel.component = swizzle_to_component(new);
                        uint8_t packed = midgard_ldst_pack(sel);

                        if (idx == 2)
                                ins->load_store.arg_2 = packed;
                        else
                                ins->load_store.arg_1 = packed;

                        break;
                }
                default:
                        assert(new == 0);
                        break;
                }
        } else if (ins->type == TAG_TEXTURE_4) {
                switch (idx) {
                case 0:
                        ins->texture.in_reg_swizzle = new;
                        break;
                default:
                        assert(new == 0);
                        break;
                }
        } else {
                unreachable("Unknown type");
        }
}

static void
mir_rewrite_index_src_single_swizzle(midgard_instruction *ins, unsigned old, unsigned new, unsigned swizzle)
{
        for (unsigned i = 0; i < ARRAY_SIZE(ins->src); ++i) {
                if (ins->src[i] != old) continue;

                ins->src[i] = new;

                mir_set_swizzle(ins, i,
                        pan_compose_swizzle(mir_get_swizzle(ins, i), swizzle));
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
mir_rewrite_index_src_swizzle(compiler_context *ctx, unsigned old, unsigned new, unsigned swizzle)
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

bool
mir_nontrivial_mod(midgard_vector_alu_src src, bool is_int, unsigned mask)
{
        if (mir_nontrivial_raw_mod(src, is_int)) return true;

        /* size-conversion */
        if (src.half) return true;

        /* swizzle */
        for (unsigned c = 0; c < 4; ++c) {
                if (!(mask & (1 << c))) continue;
                if (((src.swizzle >> (2*c)) & 3) != c) return true;
        }

        return false;
}

bool
mir_nontrivial_source2_mod(midgard_instruction *ins)
{
        bool is_int = midgard_is_integer_op(ins->alu.op);

        midgard_vector_alu_src src2 =
                vector_alu_from_unsigned(ins->alu.src2);

        return mir_nontrivial_mod(src2, is_int, ins->mask);
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

/* Creates a mask of the components of a node read by an instruction, by
 * analyzing the swizzle with respect to the instruction's mask. E.g.:
 *
 *  fadd r0.xz, r1.yyyy, r2.zwyx
 *
 * will return a mask of Z/Y for r2
 */

static unsigned
mir_mask_of_read_components_single(unsigned swizzle, unsigned outmask)
{
        unsigned mask = 0;

        for (unsigned c = 0; c < 4; ++c) {
                if (!(outmask & (1 << c))) continue;

                unsigned comp = (swizzle >> (2*c)) & 3;
                mask |= (1 << comp);
        }

        return mask;
}

static unsigned
mir_source_count(midgard_instruction *ins)
{
        if (ins->type == TAG_ALU_4) {
                /* ALU is always binary, except csel */
                return OP_IS_CSEL(ins->alu.op) ? 3 : 2;
        } else if (ins->type == TAG_LOAD_STORE_4) {
                bool load = !OP_IS_STORE(ins->load_store.op);
                return (load ? 2 : 3);
        } else if (ins->type == TAG_TEXTURE_4) {
                /* Coords, bias.. TODO: Offsets? */
                return 2;
        } else {
                unreachable("Invalid instruction type");
        }
}

static unsigned
mir_component_count_implicit(midgard_instruction *ins, unsigned i)
{
        if (ins->type == TAG_LOAD_STORE_4) {
                switch (ins->load_store.op) {
                        /* Address implicitly 64-bit */
                case midgard_op_ld_int4:
                        return (i == 0) ? 1 : 0;

                case midgard_op_st_int4:
                        return (i == 1) ? 1 : 0;

                default:
                        return 0;
                }
        }

        return 0;
}

unsigned
mir_mask_of_read_components(midgard_instruction *ins, unsigned node)
{
        unsigned mask = 0;

        for (unsigned i = 0; i < mir_source_count(ins); ++i) {
                if (ins->src[i] != node) continue;

                /* Branch writeout uses all components */
                if (ins->compact_branch && ins->writeout && (i == 0))
                        return 0xF;

                unsigned swizzle = mir_get_swizzle(ins, i);
                unsigned m = mir_mask_of_read_components_single(swizzle, ins->mask);

                /* Sometimes multi-arg ops are passed implicitly */
                unsigned implicit = mir_component_count_implicit(ins, i);
                assert(implicit < 2);

                /* Extend the mask */
                if (implicit == 1) {
                        /* Ensure it's a single bit currently */
                        assert((m >> __builtin_ctz(m)) == 0x1);

                        /* Set the next bit to extend one*/
                        m |= (m << 1);
                }

                /* Handle dot products and things */
                if (ins->type == TAG_ALU_4 && !ins->compact_branch) {
                        unsigned channel_override =
                                GET_CHANNEL_COUNT(alu_opcode_props[ins->alu.op].props);

                        if (channel_override)
                                m = mask_of(channel_override);
                }

                mask |= m;
        }

        return mask;
}

unsigned
mir_ubo_shift(midgard_load_store_op op)
{
        switch (op) {
        case midgard_op_ld_ubo_char:
                return 0;
        case midgard_op_ld_ubo_char2:
                return 1;
        case midgard_op_ld_ubo_char4:
                return 2;
        case midgard_op_ld_ubo_short4:
                return 3;
        case midgard_op_ld_ubo_int4:
                return 4;
        default:
                unreachable("Invalid op");
        }
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
}

void
mir_insert_instruction_after_scheduled(
        compiler_context *ctx,
        midgard_block *block,
        midgard_instruction *tag,
        midgard_instruction ins)
{
        unsigned after = mir_bundle_idx_for_ins(tag, block);
        size_t count = util_dynarray_num_elements(&block->bundles, midgard_bundle);
        UNUSED void *unused = util_dynarray_grow(&block->bundles, midgard_bundle, 1);

        midgard_bundle *bundles = (midgard_bundle *) block->bundles.data;
        memmove(bundles + after + 2, bundles + after + 1, (count - after - 1) * sizeof(midgard_bundle));
        midgard_bundle *after_bundle_1 = bundles + after + 2;

        midgard_bundle new = mir_bundle_for_op(ctx, ins);
        memcpy(bundles + after + 1, &new, sizeof(new));
        list_addtail(&new.instructions[0]->link, &after_bundle_1->instructions[0]->link);
}
