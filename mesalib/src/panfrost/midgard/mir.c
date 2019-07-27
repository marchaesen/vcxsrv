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
        if (ins->ssa_args.src0 == old)
                ins->ssa_args.src0 = new;

        if (ins->ssa_args.src1 == old &&
            !ins->ssa_args.inline_constant)
                ins->ssa_args.src1 = new;
}


void
mir_rewrite_index_src(compiler_context *ctx, unsigned old, unsigned new)
{
        mir_foreach_instr_global(ctx, ins) {
                mir_rewrite_index_src_single(ins, old, new);
        }
}

void
mir_rewrite_index_src_tag(compiler_context *ctx, unsigned old, unsigned new, unsigned tag)
{
        mir_foreach_instr_global(ctx, ins) {
                if (ins->type != tag)
                        continue;

                mir_rewrite_index_src_single(ins, old, new);
        }
}



void
mir_rewrite_index_dst(compiler_context *ctx, unsigned old, unsigned new)
{
        mir_foreach_instr_global(ctx, ins) {
                if (ins->ssa_args.dest == old)
                        ins->ssa_args.dest = new;
        }
}

void
mir_rewrite_index_dst_tag(compiler_context *ctx, unsigned old, unsigned new, unsigned tag)
{
        mir_foreach_instr_global(ctx, ins) {
                if (ins->type != tag)
                        continue;

                if (ins->ssa_args.dest == old)
                        ins->ssa_args.dest = new;
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
        return mir_use_count(ctx, value) <= 1;
}

bool
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

                if (!(is_ldst || is_tex))
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
        if ((node < 0) || (node >= SSA_FIXED_MINIMUM))
                return true;

        mir_foreach_instr_global(ctx, q) {
                if (q == ins)
                        break;

                if (q->ssa_args.dest == node)
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
mir_mask_of_read_components_single(unsigned src, unsigned outmask)
{
        midgard_vector_alu_src s = vector_alu_from_unsigned(src);
        unsigned mask = 0;

        for (unsigned c = 0; c < 4; ++c) {
                if (!(outmask & (1 << c))) continue;

                unsigned comp = (s.swizzle >> (2*c)) & 3;
                mask |= (1 << comp);
        }

        return mask;
}

unsigned
mir_mask_of_read_components(midgard_instruction *ins, unsigned node)
{
        assert(ins->type == TAG_ALU_4);

        unsigned mask = 0;

        if (ins->ssa_args.src0 == node)
                mask |= mir_mask_of_read_components_single(ins->alu.src1, ins->mask);

        if (ins->ssa_args.src1 == node && !ins->ssa_args.inline_constant)
                mask |= mir_mask_of_read_components_single(ins->alu.src2, ins->mask);

        return mask;
}
