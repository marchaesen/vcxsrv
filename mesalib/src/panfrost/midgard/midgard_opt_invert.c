/*
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

/* Lowers the invert field on instructions to a dedicated inot (inor)
 * instruction instead, as invert is not always supported natively by the
 * hardware */

void
midgard_lower_invert(compiler_context *ctx, midgard_block *block)
{
        mir_foreach_instr_in_block_safe(block, ins) {
                if (ins->type != TAG_ALU_4) continue;
                if (!ins->invert) continue;

                unsigned temp = make_compiler_temp(ctx);

                midgard_instruction not = {
                        .type = TAG_ALU_4,
                        .mask = ins->mask,
                        .src = { temp, ~0, ~0, ~0 },
                        .swizzle = SWIZZLE_IDENTITY,
                        .dest = ins->dest,
                        .has_inline_constant = true,
                        .alu = {
                                .op = midgard_alu_op_inor,
                                /* TODO: i16 */
                                .reg_mode = midgard_reg_mode_32,
                                .dest_override = midgard_dest_override_none,
                                .outmod = midgard_outmod_int_wrap
                        },
                };

                ins->dest = temp;
                ins->invert = false;
                mir_insert_instruction_before(ctx, mir_next_op(ins), not);
        }
}

/* Propagate the .not up to the source */

bool
midgard_opt_not_propagate(compiler_context *ctx, midgard_block *block)
{
        bool progress = false;

        mir_foreach_instr_in_block_safe(block, ins) {
                if (ins->type != TAG_ALU_4) continue;
                if (ins->alu.op != midgard_alu_op_imov) continue;
                if (!ins->invert) continue;
                if (mir_nontrivial_source2_mod_simple(ins)) continue;
                if (ins->src[1] & IS_REG) continue;

                /* Is it beneficial to propagate? */
                if (!mir_single_use(ctx, ins->src[1])) continue;

                /* We found an imov.not, propagate the invert back */

                mir_foreach_instr_in_block_from_rev(block, v, mir_prev_op(ins)) {
                        if (v->dest != ins->src[1]) continue;
                        if (v->type != TAG_ALU_4) break;

                        v->invert = !v->invert;
                        ins->invert = false;
                        progress |= true;
                        break;
                }
        }

        return progress;
}

/* With that lowering out of the way, we can focus on more interesting
 * optimizations. One easy one is fusing inverts into bitwise operations:
 *
 * ~iand = inand
 * ~ior  = inor
 * ~ixor = inxor
 */

static bool
mir_is_bitwise(midgard_instruction *ins)
{
        switch (ins->alu.op) {
        case midgard_alu_op_iand:
        case midgard_alu_op_ior:
        case midgard_alu_op_ixor:
                return true;
        default:
                return false;
        }
}

static midgard_alu_op
mir_invert_op(midgard_alu_op op)
{
        switch (op) {
        case midgard_alu_op_iand:
                return midgard_alu_op_inand;
        case midgard_alu_op_ior:
                return midgard_alu_op_inor;
        case midgard_alu_op_ixor:
                return midgard_alu_op_inxor;
        default:
                unreachable("Op not invertible");
        }
}

static midgard_alu_op
mir_demorgan_op(midgard_alu_op op)
{
        switch (op) {
        case midgard_alu_op_iand:
                return midgard_alu_op_inor;
        case midgard_alu_op_ior:
                return midgard_alu_op_inand;
        default:
                unreachable("Op not De Morgan-able");
        }
}

static midgard_alu_op
mir_notright_op(midgard_alu_op op)
{
        switch (op) {
        case midgard_alu_op_iand:
                return midgard_alu_op_iandnot;
        case midgard_alu_op_ior:
                return midgard_alu_op_iornot;
        default:
                unreachable("Op not right able");
        }
}

bool
midgard_opt_fuse_dest_invert(compiler_context *ctx, midgard_block *block)
{
        bool progress = false;

        mir_foreach_instr_in_block_safe(block, ins) {
                /* Search for inverted bitwise */
                if (ins->type != TAG_ALU_4) continue;
                if (!mir_is_bitwise(ins)) continue;
                if (!ins->invert) continue;

                ins->alu.op = mir_invert_op(ins->alu.op);
                ins->invert = false;
                progress |= true;
        }

        return progress;
}

/* Next up, we can fuse inverts into the sources of bitwise ops:
 *
 * ~a & b = b & ~a = iandnot(b, a)
 * a & ~b = iandnot(a, b)
 * ~a & ~b = ~(a | b) = inor(a, b)
 *
 * ~a | b = b | ~a = iornot(b, a)
 *  a | ~b = iornot(a, b)
 * ~a | ~b = ~(a & b) = inand(a, b)
 *
 * ~a ^ b = ~(a ^ b) = inxor(a, b)
 * a ^ ~b = ~(a ^ b) + inxor(a, b)
 * ~a ^ ~b = a ^ b
 * ~(a ^ b) = inxor(a, b)
 */

static bool
mir_strip_inverted(compiler_context *ctx, unsigned node)
{
        if (node >= SSA_FIXED_MINIMUM)
                return false;

       /* Strips and returns the invert off a node */
       mir_foreach_instr_global(ctx, ins) {
               if (ins->compact_branch) continue;
               if (ins->dest != node) continue;

               bool status = ins->invert;
               ins->invert = false;
               return status;
       }

       unreachable("Invalid node stripped");
}

static bool
is_ssa_or_constant(unsigned node)
{
        return !(node & IS_REG) || (node == SSA_FIXED_REGISTER(26));
}

bool
midgard_opt_fuse_src_invert(compiler_context *ctx, midgard_block *block)
{
        bool progress = false;

        mir_foreach_instr_in_block_safe(block, ins) {
                /* Search for inverted bitwise */
                if (ins->type != TAG_ALU_4) continue;
                if (!mir_is_bitwise(ins)) continue;
                if (ins->invert) continue;

                if (!is_ssa_or_constant(ins->src[0])) continue;
                if (!is_ssa_or_constant(ins->src[1])) continue;
                if (!mir_single_use(ctx, ins->src[0])) continue;
                if (!ins->has_inline_constant && !mir_single_use(ctx, ins->src[1])) continue;

                bool not_a = mir_strip_inverted(ctx, ins->src[0]);
                bool not_b =
                        ins->has_inline_constant ? false :
                        mir_strip_inverted(ctx, ins->src[1]);

                /* Edge case: if src0 == src1, it'll've been stripped */
                if ((ins->src[0] == ins->src[1]) && !ins->has_inline_constant)
                        not_b = not_a;

                progress |= (not_a || not_b);

                /* No point */
                if (!(not_a || not_b)) continue;

                bool both = not_a && not_b;
                bool left = not_a && !not_b;
                bool right = !not_a && not_b;

                /* No-op, but we got to strip the inverts */
                if (both && ins->alu.op == midgard_alu_op_ixor)
                        continue;

                if (both) {
                        ins->alu.op = mir_demorgan_op(ins->alu.op);
                } else if (right || (left && !ins->has_inline_constant)) {
                        /* Commute arguments */
                        if (left)
                                mir_flip(ins);

                        ins->alu.op = mir_notright_op(ins->alu.op);
                } else if (left && ins->has_inline_constant) {
                        /* Some special transformations:
                         *
                         * ~A & c = ~(~(~A) | (~c)) = ~(A | ~c) = inor(A, ~c)
                         * ~A | c = ~(~(~A) & (~c)) = ~(A & ~c) = inand(A, ~c)
                         */

                        ins->alu.op = mir_demorgan_op(ins->alu.op);
                        ins->inline_constant = ~ins->inline_constant;
                }
        }

        return progress;
}

/* Optimizes a .not away when used as the source of a conditional select:
 *
 * csel(a, b, c)  = { b if a, c if !a }
 * csel(!a, b, c) = { b if !a, c if !(!a) } = { c if a, b if !a } = csel(a, c, b)
 * csel(!a, b, c) = csel(a, c, b)
 */

bool
midgard_opt_csel_invert(compiler_context *ctx, midgard_block *block)
{
        bool progress = false;

        mir_foreach_instr_in_block_safe(block, ins) {
                if (ins->type != TAG_ALU_4) continue;
                if (!OP_IS_CSEL(ins->alu.op)) continue;
                if (!mir_single_use(ctx, ins->src[2])) continue;
                if (!mir_strip_inverted(ctx, ins->src[2])) continue;

                mir_flip(ins);
                progress |= true;
        }

        return progress;
}


static bool
mir_is_inverted(compiler_context *ctx, unsigned node)
{
        mir_foreach_instr_global(ctx, ins) {
                if (ins->compact_branch) continue;
                if (ins->dest != node) continue;

                return ins->invert;
        }

        unreachable("Invalid node passed");
}



/* Optimizes comparisions which invert both arguments
 *
 *
 * ieq(not(a), not(b)) = ieq(a, b)
 * ine(not(a), not(b)) = ine(a, b)
 *
 * This does apply for ilt and ile if we flip the argument order:
 * Proofs below provided by Alyssa Rosenzweig
 *
 * not(x) = −(x+1)
 *
 * ( not(A) <= not(B) ) <=> ( −(A+1) <= −(B+1) )
 *                      <=> ( A+1 >= B+1)
 *                      <=> ( B <= A )
 *
 * On unsigned comparisons (ult / ule) we can perform the same optimization
 * with the additional restriction that the source registers must
 * have the same size.
 *
 * TODO: We may not need them to be of the same size, if we can
 *       prove that they are the same after sext/zext
 *
 * not(x) = 2n−x−1
 *
 * ( not(A) <= not(B) ) <=> ( 2n−A−1 <= 2n−B−1 )
 *                      <=> ( −A <= −B )
 *                      <=> ( B <= A )
 */
bool
midgard_opt_drop_cmp_invert(compiler_context *ctx, midgard_block *block)
{

        bool progress = false;

        mir_foreach_instr_in_block_safe(block, ins) {
                if (ins->type != TAG_ALU_4) continue;
                if (!OP_IS_INTEGER_CMP(ins->alu.op)) continue;

                if ((ins->src[0] & IS_REG) || (ins->src[1] & IS_REG)) continue;
                if (!mir_single_use(ctx, ins->src[0]) || !mir_single_use(ctx, ins->src[1])) continue;

                bool a_inverted = mir_is_inverted(ctx, ins->src[0]);
                bool b_inverted = mir_is_inverted(ctx, ins->src[1]);

                if (!a_inverted || !b_inverted) continue;
                if (OP_IS_UNSIGNED_CMP(ins->alu.op) && mir_srcsize(ins, 0) != mir_srcsize(ins, 1)) continue;


                mir_strip_inverted(ctx, ins->src[0]);
                mir_strip_inverted(ctx, ins->src[1]);

                if (ins->alu.op != midgard_alu_op_ieq && ins->alu.op != midgard_alu_op_ine)
                        mir_flip(ins);

                progress |= true;
        }

        return progress;
}

/* Optimizes branches with inverted arguments by inverting the
 * branch condition instead of the argument condition.
 */
bool
midgard_opt_invert_branch(compiler_context *ctx, midgard_block *block)
{
        bool progress = false;

        mir_foreach_instr_in_block_safe(block, ins) {
                if (ins->type != TAG_ALU_4) continue;
                if (!midgard_is_branch_unit(ins->unit)) continue;
                if (!ins->branch.conditional) continue;
                if (ins->src[0] & IS_REG) continue;

                if (mir_strip_inverted(ctx, ins->src[0])) {
                        ins->branch.invert_conditional = !ins->branch.invert_conditional;

                        progress |= true;
		}
        }

        return progress;
}
