/*
 * Copyright (C) 2020 Collabora, Ltd.
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

#define RETURN_PACKED(str) { \
        uint64_t temp = 0; \
        memcpy(&temp, &str, sizeof(str)); \
        return temp; \
}

/* This file contains the final passes of the compiler. Running after
 * scheduling and RA, the IR is now finalized, so we need to emit it to actual
 * bits on the wire (as well as fixup branches) */

static uint64_t
bi_pack_header(bi_clause *clause, bi_clause *next, bool is_fragment)
{
        struct bifrost_header header = {
                .back_to_back = clause->back_to_back,
                .no_end_of_shader = (next != NULL),
                .elide_writes = is_fragment,
                .branch_cond = clause->branch_conditional,
                .datareg_writebarrier = clause->data_register_write_barrier,
                .datareg = clause->data_register,
                .scoreboard_deps = next ? next->dependencies : 0,
                .scoreboard_index = clause->scoreboard_id,
                .clause_type = clause->clause_type,
                .next_clause_type = next ? next->clause_type : 0,
        };

        uint64_t u = 0;
        memcpy(&u, &header, sizeof(header));
        return u;
}

/* Represents the assignment of ports for a given bundle */

struct bi_registers {
        /* Register to assign to each port */
        unsigned port[4];

        /* Read ports can be disabled */
        bool enabled[2];

        /* Should we write FMA? what about ADD? If only a single port is
         * enabled it is in port 2, else ADD/FMA is 2/3 respectively */
        bool write_fma, write_add;

        /* Should we read with port 3? */
        bool read_port3;

        /* Packed uniform/constant */
        uint8_t uniform_constant;

        /* Whether writes are actually for the last instruction */
        bool first_instruction;
};

/* The uniform/constant slot allows loading a contiguous 64-bit immediate or
 * pushed uniform per bundle. Figure out which one we need in the bundle (the
 * scheduler needs to ensure we only have one type per bundle), validate
 * everything, and rewrite away the register/uniform indices to use 3-bit
 * sources directly. */

static unsigned
bi_lookup_constant(bi_clause *clause, uint64_t cons, bool *hi, bool b64)
{
        uint64_t want = (cons >> 4);

        for (unsigned i = 0; i < clause->constant_count; ++i) {
                /* Only check top 60-bits since that's what's actually embedded
                 * in the clause, the bottom 4-bits are bundle-inline */

                unsigned candidates[2] = {
                        clause->constants[i] >> 4,
                        clause->constants[i] >> 36
                };

                if (!b64)
                        candidates[0] &= 0xFFFFFFFF;

                if (candidates[0] == want)
                        return i;

                if (candidates[1] == want && !b64) {
                        *hi = true;
                        return i;
                }
        }

        unreachable("Invalid constant accessed");
}

static unsigned
bi_constant_field(unsigned idx)
{
        assert(idx <= 5);

        const unsigned values[] = {
                4, 5, 6, 7, 2, 3
        };

        return values[idx] << 4;
}

static bool
bi_assign_uniform_constant_single(
                struct bi_registers *regs,
                bi_clause *clause,
                bi_instruction *ins, bool assigned, bool fast_zero)
{
        if (!ins)
                return assigned;

        bi_foreach_src(ins, s) {
                if (s == 0 && (ins->type == BI_LOAD_VAR_ADDRESS || ins->type == BI_LOAD_ATTR)) continue;

                if (ins->src[s] & BIR_INDEX_CONSTANT) {
                        bool hi = false;
                        bool b64 = nir_alu_type_get_type_size(ins->src_types[s]) > 32;
                        uint64_t cons = bi_get_immediate(ins, ins->src[s]);
                        unsigned idx = bi_lookup_constant(clause, cons, &hi, b64);
                        unsigned f = bi_constant_field(idx) | (cons & 0xF);

                        if (assigned && regs->uniform_constant != f)
                                unreachable("Mismatched uniform/const field: imm");

                        regs->uniform_constant = f;
                        ins->src[s] = BIR_INDEX_PASS | (hi ? BIFROST_SRC_CONST_HI : BIFROST_SRC_CONST_LO);
                        assigned = true;
                } else if (ins->src[s] & BIR_INDEX_ZERO && (ins->type == BI_LOAD_UNIFORM || ins->type == BI_LOAD_VAR)) {
                        /* XXX: HACK UNTIL WE HAVE HI MATCHING DUE TO OVERFLOW XXX */
                        ins->src[s] = BIR_INDEX_PASS | BIFROST_SRC_CONST_HI;
                } else if (ins->src[s] & BIR_INDEX_ZERO && !fast_zero) {
                        /* FMAs have a fast zero port, ADD needs to use the
                         * uniform/const port's special 0 mode handled here */
                        unsigned f = 0;

                        if (assigned && regs->uniform_constant != f)
                                unreachable("Mismatched uniform/const field: 0");

                        regs->uniform_constant = f;
                        ins->src[s] = BIR_INDEX_PASS | BIFROST_SRC_CONST_LO;
                        assigned = true;
                } else if (s & BIR_INDEX_UNIFORM) {
                        unreachable("Push uniforms not implemented yet");
                }
        }

        return assigned;
}

static void
bi_assign_uniform_constant(
                bi_clause *clause,
                struct bi_registers *regs,
                bi_bundle bundle)
{
        bool assigned =
                bi_assign_uniform_constant_single(regs, clause, bundle.fma, false, true);

        bi_assign_uniform_constant_single(regs, clause, bundle.add, assigned, false);
}

/* Assigns a port for reading, before anything is written */

static void
bi_assign_port_read(struct bi_registers *regs, unsigned src)
{
        /* We only assign for registers */
        if (!(src & BIR_INDEX_REGISTER))
                return;

        unsigned reg = src & ~BIR_INDEX_REGISTER;

        /* Check if we already assigned the port */
        for (unsigned i = 0; i <= 1; ++i) {
                if (regs->port[i] == reg && regs->enabled[i])
                        return;
        }

        if (regs->port[3] == reg && regs->read_port3)
                return;

        /* Assign it now */

        for (unsigned i = 0; i <= 1; ++i) {
                if (!regs->enabled[i]) {
                        regs->port[i] = reg;
                        regs->enabled[i] = true;
                        return;
                }
        }

        if (!regs->read_port3) {
                regs->port[3] = reg;
                regs->read_port3 = true;
        }
}

static struct bi_registers
bi_assign_ports(bi_bundle now, bi_bundle prev)
{
        struct bi_registers regs = { 0 };

        /* We assign ports for the main register mechanism. Special ops
         * use the data registers, which has its own mechanism entirely
         * and thus gets skipped over here. */

        unsigned read_dreg = now.add &&
                bi_class_props[now.add->type] & BI_DATA_REG_SRC;

        unsigned write_dreg = prev.add &&
                bi_class_props[prev.add->type] & BI_DATA_REG_DEST;

        /* First, assign reads */

        if (now.fma)
                bi_foreach_src(now.fma, src)
                        bi_assign_port_read(&regs, now.fma->src[src]);

        if (now.add) {
                bi_foreach_src(now.add, src) {
                        if (!(src == 0 && read_dreg))
                                bi_assign_port_read(&regs, now.add->src[src]);
                }
        }

        /* Next, assign writes */

        if (prev.fma && prev.fma->dest & BIR_INDEX_REGISTER) {
                regs.port[2] = prev.fma->dest & ~BIR_INDEX_REGISTER;
                regs.write_fma = true;
        }

        if (prev.add && prev.add->dest & BIR_INDEX_REGISTER && !write_dreg) {
                unsigned r = prev.add->dest & ~BIR_INDEX_REGISTER;

                if (regs.write_fma) {
                        /* Scheduler constraint: cannot read 3 and write 2 */
                        assert(!regs.read_port3);
                        regs.port[3] = r;
                } else {
                        regs.port[2] = r;
                }

                regs.write_add = true;
        }

        /* Finally, ensure port 1 > port 0 for the 63-x trick to function */

        if (regs.enabled[0] && regs.enabled[1] && regs.port[1] < regs.port[0]) {
                unsigned temp = regs.port[0];
                regs.port[0] = regs.port[1];
                regs.port[1] = temp;
        }

        return regs;
}

/* Determines the register control field, ignoring the first? flag */

static enum bifrost_reg_control
bi_pack_register_ctrl_lo(struct bi_registers r)
{
        if (r.write_fma) {
                if (r.write_add) {
                        assert(!r.read_port3);
                        return BIFROST_WRITE_ADD_P2_FMA_P3;
                } else {
                        if (r.read_port3)
                                return BIFROST_WRITE_FMA_P2_READ_P3;
                        else
                                return BIFROST_WRITE_FMA_P2;
                }
        } else if (r.write_add) {
                if (r.read_port3)
                        return BIFROST_WRITE_ADD_P2_READ_P3;
                else
                        return BIFROST_WRITE_ADD_P2;
        } else if (r.read_port3)
                return BIFROST_READ_P3;
        else
                return BIFROST_REG_NONE;
}

/* Ditto but account for the first? flag this time */

static enum bifrost_reg_control
bi_pack_register_ctrl(struct bi_registers r)
{
        enum bifrost_reg_control ctrl = bi_pack_register_ctrl_lo(r);

        if (r.first_instruction) {
                if (ctrl == BIFROST_REG_NONE)
                        ctrl = BIFROST_FIRST_NONE;
                else
                        ctrl |= BIFROST_FIRST_NONE;
        }

        return ctrl;
}

static uint64_t
bi_pack_registers(struct bi_registers regs)
{
        enum bifrost_reg_control ctrl = bi_pack_register_ctrl(regs);
        struct bifrost_regs s;
        uint64_t packed = 0;

        if (regs.enabled[1]) {
                /* Gotta save that bit!~ Required by the 63-x trick */
                assert(regs.port[1] > regs.port[0]);
                assert(regs.enabled[0]);

                /* Do the 63-x trick, see docs/disasm */
                if (regs.port[0] > 31) {
                        regs.port[0] = 63 - regs.port[0];
                        regs.port[1] = 63 - regs.port[1];
                }

                assert(regs.port[0] <= 31);
                assert(regs.port[1] <= 63);

                s.ctrl = ctrl;
                s.reg1 = regs.port[1];
                s.reg0 = regs.port[0];
        } else {
                /* Port 1 disabled, so set to zero and use port 1 for ctrl */
                s.reg1 = ctrl << 2;

                if (regs.enabled[0]) {
                        /* Bit 0 upper bit of port 0 */
                        s.reg1 |= (regs.port[0] >> 5);

                        /* Rest of port 0 in usual spot */
                        s.reg0 = (regs.port[0] & 0b11111);
                } else {
                        /* Bit 1 set if port 0 also disabled */
                        s.reg1 |= (1 << 1);
                }
        }

        /* When port 3 isn't used, we have to set it to port 2, and vice versa,
         * or we an INSTR_INVALID_ENC is raised. The reason is unknown. */

        if (!regs.port[3])
                regs.port[3] = regs.port[2];

        if (!regs.port[2])
                regs.port[2] = regs.port[3];

        s.reg3 = regs.port[3];
        s.reg2 = regs.port[2];
        s.uniform_const = regs.uniform_constant;

        memcpy(&packed, &s, sizeof(s));
        return packed;
}

static void
bi_set_data_register(bi_clause *clause, unsigned idx)
{
        assert(idx & BIR_INDEX_REGISTER);
        unsigned reg = idx & ~BIR_INDEX_REGISTER;
        assert(reg <= 63);
        clause->data_register = reg;
}

static void
bi_read_data_register(bi_clause *clause, bi_instruction *ins)
{
        bi_set_data_register(clause, ins->src[0]);
}

static void
bi_write_data_register(bi_clause *clause, bi_instruction *ins)
{
        bi_set_data_register(clause, ins->dest);
}

static enum bifrost_packed_src
bi_get_src_reg_port(struct bi_registers *regs, unsigned src)
{
        unsigned reg = src & ~BIR_INDEX_REGISTER;

        if (regs->port[0] == reg && regs->enabled[0])
                return BIFROST_SRC_PORT0;
        else if (regs->port[1] == reg && regs->enabled[1])
                return BIFROST_SRC_PORT1;
        else if (regs->port[3] == reg && regs->read_port3)
                return BIFROST_SRC_PORT3;
        else
                unreachable("Tried to access register with no port");
}

static enum bifrost_packed_src
bi_get_src(bi_instruction *ins, struct bi_registers *regs, unsigned s, bool is_fma)
{
        unsigned src = ins->src[s];

        if (src & BIR_INDEX_REGISTER)
                return bi_get_src_reg_port(regs, src);
        else if (src & BIR_INDEX_ZERO && is_fma)
                return BIFROST_SRC_STAGE;
        else if (src & BIR_INDEX_PASS)
                return src & ~BIR_INDEX_PASS;
        else
                unreachable("Unknown src");
}

/* Constructs a packed 2-bit swizzle for a 16-bit vec2 source. Source must be
 * 16-bit and written components must correspond to valid swizzles (component x
 * or y). */

static unsigned
bi_swiz16(bi_instruction *ins, unsigned src)
{
        assert(nir_alu_type_get_type_size(ins->src_types[src]) == 16);
        unsigned swizzle = 0;

        for (unsigned c = 0; c < 2; ++c) {
                if (!bi_writes_component(ins, src)) continue;

                unsigned k = ins->swizzle[src][c];
                assert(k < 1);
                swizzle |= (k << c);
        }

        return swizzle;
}

static unsigned
bi_pack_fma_fma(bi_instruction *ins, struct bi_registers *regs)
{
        /* (-a)(-b) = ab, so we only need one negate bit */
        bool negate_mul = ins->src_neg[0] ^ ins->src_neg[1];

        if (ins->dest_type == nir_type_float32) {
                struct bifrost_fma_fma pack = {
                        .src0 = bi_get_src(ins, regs, 0, true),
                        .src1 = bi_get_src(ins, regs, 1, true),
                        .src2 = bi_get_src(ins, regs, 2, true),
                        .src0_abs = ins->src_abs[0],
                        .src1_abs = ins->src_abs[1],
                        .src2_abs = ins->src_abs[2],
                        .src0_neg = negate_mul,
                        .src2_neg = ins->src_neg[2],
                        .outmod = ins->outmod,
                        .roundmode = ins->roundmode,
                        .op = BIFROST_FMA_OP_FMA
                };

                RETURN_PACKED(pack);
        } else if (ins->dest_type == nir_type_float16) {
                struct bifrost_fma_fma16 pack = {
                        .src0 = bi_get_src(ins, regs, 0, true),
                        .src1 = bi_get_src(ins, regs, 1, true),
                        .src2 = bi_get_src(ins, regs, 2, true),
                        .swizzle_0 = bi_swiz16(ins, 0),
                        .swizzle_1 = bi_swiz16(ins, 1),
                        .swizzle_2 = bi_swiz16(ins, 2),
                        .src0_neg = negate_mul,
                        .src2_neg = ins->src_neg[2],
                        .outmod = ins->outmod,
                        .roundmode = ins->roundmode,
                        .op = BIFROST_FMA_OP_FMA16
                };

                RETURN_PACKED(pack);
        } else {
                unreachable("Invalid fma dest type");
        }
}

static unsigned
bi_pack_fma_add_f32(bi_instruction *ins, struct bi_registers *regs)
{
        struct bifrost_fma_add pack = {
                .src0 = bi_get_src(ins, regs, 0, true),
                .src1 = bi_get_src(ins, regs, 1, true),
                .src0_abs = ins->src_abs[0],
                .src1_abs = ins->src_abs[1],
                .src0_neg = ins->src_neg[0],
                .src1_neg = ins->src_neg[1],
                .unk = 0x0,
                .outmod = ins->outmod,
                .roundmode = ins->roundmode,
                .op = BIFROST_FMA_OP_FADD32
        };

        RETURN_PACKED(pack);
}

static unsigned
bi_pack_fma_addmin_f16(bi_instruction *ins, struct bi_registers *regs)
{
        unsigned op =
                (ins->type == BI_ADD) ? BIFROST_FMA_OP_FADD16 :
                (ins->op.minmax == BI_MINMAX_MIN) ? BIFROST_FMA_OP_FMIN16 :
                BIFROST_FMA_OP_FMAX16;

        /* Absolute values are packed in a quirky way. Let k = src1 < src0. Let
         * l be an auxiliary bit we encode. Then the hardware determines:
         *
         *      abs0 = l || k
         *      abs1 = l && k
         *
         * Since add/min/max are commutative, this saves a bit by using the
         * order of the operands as a bit (k). To pack this, first note:
         *
         *      (l && k) implies (l || k).
         *
         * That is, if the second argument is abs'd, then the first argument
         * also has abs. So there are three cases:
         *
         * Case 0: Neither src has absolute value. Then we have l = k = 0.
         *
         * Case 1: Exactly one src has absolute value. Assign that source to
         * src0 and the other source to src1. Compute k = src1 < src0 based on
         * that assignment. Then l = ~k.
         *
         * Case 2: Both sources have absolute value. Then we have l = k = 1.
         * Note to force k = 1 requires that (src1 < src0) OR (src0 < src1).
         * That is, this encoding is only valid if src1 and src0 are distinct.
         * This is a scheduling restriction (XXX); if an op of this type
         * requires both identical sources to have abs value, then we must
         * schedule to ADD (which does not use this ordering trick).
         */

        unsigned abs_0 = ins->src_abs[0], abs_1 = ins->src_abs[1];
        unsigned src_0 = bi_get_src(ins, regs, 0, true);
        unsigned src_1 = bi_get_src(ins, regs, 0, true);
        bool l = false;

        if (!abs_0 && !abs_1) {
                /* Force k = 0 <===> NOT(src1 < src0) <==> src1 >= src0 */
                if (src_0 < src_1) {
                        unsigned tmp = src_0;
                        src_0 = src_1;
                        src_1 = tmp;
                }
        } else if (abs_0 && !abs_1) {
                l = src_1 >= src_0;
        } else if (abs_1 && !abs_0) {
                unsigned tmp = src_0;
                src_0 = src_1;
                src_0 = tmp;

                l = src_1 >= src_0;
        } else {
                if (src_0 >= src_1) {
                        unsigned tmp = src_0;
                        src_0 = src_1;
                        src_1 = tmp;
                }

                l = true;
        }

        struct bifrost_fma_add_minmax16 pack = {
                .src0 = src_0,
                .src1 = src_1,
                .src0_neg = ins->src_neg[0],
                .src1_neg = ins->src_neg[1],
                .abs1 = l,
                .outmod = ins->outmod,
                .mode = (ins->type == BI_ADD) ? ins->roundmode : ins->minmax,
                .op = op
        };

        RETURN_PACKED(pack);
}

static unsigned
bi_pack_fma_add(bi_instruction *ins, struct bi_registers *regs)
{
        if (ins->dest_type == nir_type_float32)
                return bi_pack_fma_add_f32(ins, regs);
        else if(ins->dest_type == nir_type_float16)
                return bi_pack_fma_addmin_f16(ins, regs);
        else
                unreachable("Unknown FMA/ADD type");
}

static unsigned
bi_pack_fma_1src(bi_instruction *ins, struct bi_registers *regs, unsigned op)
{
        struct bifrost_fma_inst pack = {
                .src0 = bi_get_src(ins, regs, 0, true),
                .op = op
        };

        RETURN_PACKED(pack);
}

static enum bifrost_csel_cond
bi_cond_to_csel(enum bi_cond cond, bool *flip, bool *invert, nir_alu_type T)
{
        nir_alu_type B = nir_alu_type_get_base_type(T);
        unsigned idx = (B == nir_type_float) ? 0 :
                ((B == nir_type_int) ? 1 : 2);

        switch (cond){
        case BI_COND_LT:
                *flip = true;
        case BI_COND_GT: {
                const enum bifrost_csel_cond ops[] = {
                        BIFROST_FGT_F,
                        BIFROST_IGT_I,
                        BIFROST_UGT_I
                };

                return ops[idx];
        }
        case BI_COND_LE:
                *flip = true;
        case BI_COND_GE: {
                const enum bifrost_csel_cond ops[] = {
                        BIFROST_FGE_F,
                        BIFROST_IGE_I,
                        BIFROST_UGE_I
                };

                return ops[idx];
        }
        case BI_COND_NE:
                *invert = true;
        case BI_COND_EQ: {
                const enum bifrost_csel_cond ops[] = {
                        BIFROST_FEQ_F,
                        BIFROST_IEQ_F,
                        BIFROST_IEQ_F /* sign is irrelevant */
                };

                return ops[idx];
        }
        default:
                unreachable("Invalid op for csel");
        }
}

static unsigned
bi_pack_fma_csel(bi_instruction *ins, struct bi_registers *regs)
{
        /* TODO: Use csel3 as well */
        bool flip = false, invert = false;

        enum bifrost_csel_cond cond =
                bi_cond_to_csel(ins->csel_cond, &flip, &invert, ins->src_types[0]);

        unsigned size = nir_alu_type_get_type_size(ins->dest_type);

        unsigned cmp_0 = (flip ? 3 : 0);
        unsigned cmp_1 = (flip ? 0 : 3);
        unsigned res_0 = (invert ? 2 : 1);
        unsigned res_1 = (invert ? 1 : 2);
        
        struct bifrost_csel4 pack = {
                .src0 = bi_get_src(ins, regs, cmp_0, true),
                .src1 = bi_get_src(ins, regs, cmp_1, true),
                .src2 = bi_get_src(ins, regs, res_0, true),
                .src3 = bi_get_src(ins, regs, res_1, true),
                .cond = cond,
                .op = (size == 16) ? BIFROST_FMA_OP_CSEL4_V16 :
                        BIFROST_FMA_OP_CSEL4
        };

        RETURN_PACKED(pack);
}

/* We have a single convert opcode in the IR but a number of opcodes that could
 * come out. In particular we have native opcodes for:
 *
 * [ui]16 --> [fui]32           -- int16_to_32
 * f16     --> f32              -- float16_to_32
 * f32     --> f16              -- float32_to_16
 * f32     --> [ui]32           -- float32_to_int
 * [ui]32  --> f32              -- int_to_float32
 * [fui]16 --> [fui]16          -- f2i_i2f16
 */

static unsigned
bi_pack_fma_convert(bi_instruction *ins, struct bi_registers *regs)
{
        nir_alu_type from_base = nir_alu_type_get_base_type(ins->src_types[0]);
        unsigned from_size = nir_alu_type_get_type_size(ins->src_types[0]);
        bool from_unsigned = from_base == nir_type_uint;

        nir_alu_type to_base = nir_alu_type_get_base_type(ins->dest_type);
        unsigned to_size = nir_alu_type_get_type_size(ins->dest_type);
        bool to_unsigned = to_base == nir_type_uint;

        /* Sanity check */
        assert((from_base != to_base) || (from_size != to_size));
        assert((MAX2(from_size, to_size) / MIN2(from_size, to_size)) <= 2);

        if (from_size == 16 && to_size == 16) {
                /* f2i_i2f16 */
                unreachable("i16 not yet implemented");
        } else if (from_size == 32 && to_size == 32) {
                unsigned op = 0;

                if (from_base == nir_type_float) {
                        op = BIFROST_FMA_FLOAT32_TO_INT(to_unsigned);
                } else {
                        op = BIFROST_FMA_INT_TO_FLOAT32(from_unsigned);
                }

                return bi_pack_fma_1src(ins, regs, op);
        } else if (from_size == 16 && to_size == 32) {
                bool from_y = ins->swizzle[0][0];

                if (from_base == nir_type_float) {
                        return bi_pack_fma_1src(ins, regs,
                                        BIFROST_FMA_FLOAT16_TO_32(from_y));
                } else {
                        unreachable("i16 not yet implemented");
                }
        } else if (from_size == 32 && to_size == 16) {
                if (from_base == nir_type_float) {
                        /* TODO: second vectorized source? */
                        struct bifrost_fma_2src pack = {
                                .src0 = bi_get_src(ins, regs, 0, true),
                                .src1 = BIFROST_SRC_STAGE, /* 0 */
                                .op = BIFROST_FMA_FLOAT32_TO_16
                        };

                        RETURN_PACKED(pack);
                } else {
                        unreachable("i16 not yet implemented");
                }
        }

        unreachable("Unknown convert");
}

static unsigned
bi_pack_fma(bi_clause *clause, bi_bundle bundle, struct bi_registers *regs)
{
        if (!bundle.fma)
                return BIFROST_FMA_NOP;

        switch (bundle.fma->type) {
        case BI_ADD:
                return bi_pack_fma_add(bundle.fma, regs);
        case BI_CMP:
        case BI_BITWISE:
		return BIFROST_FMA_NOP;
        case BI_CONVERT:
		return bi_pack_fma_convert(bundle.fma, regs);
        case BI_CSEL:
		return bi_pack_fma_csel(bundle.fma, regs);
        case BI_FMA:
                return bi_pack_fma_fma(bundle.fma, regs);
        case BI_FREXP:
        case BI_ISUB:
        case BI_MINMAX:
                return BIFROST_FMA_NOP;
        case BI_MOV:
                return bi_pack_fma_1src(bundle.fma, regs, BIFROST_FMA_OP_MOV);
        case BI_SHIFT:
        case BI_SWIZZLE:
        case BI_ROUND:
		return BIFROST_FMA_NOP;
        default:
                unreachable("Cannot encode class as FMA");
        }
}

static unsigned
bi_pack_add_ld_vary(bi_clause *clause, bi_instruction *ins, struct bi_registers *regs)
{
        unsigned size = nir_alu_type_get_type_size(ins->dest_type);
        assert(size == 32 || size == 16);

        unsigned op = (size == 32) ?
                BIFROST_ADD_OP_LD_VAR_32 :
                BIFROST_ADD_OP_LD_VAR_16;

        unsigned cmask = bi_from_bytemask(ins->writemask, size / 8);
        unsigned channels = util_bitcount(cmask);
        assert(cmask == ((1 << channels) - 1));

        unsigned packed_addr = 0;

        if (ins->src[0] & BIR_INDEX_CONSTANT) {
                /* Direct uses address field directly */
                packed_addr = bi_get_immediate(ins, ins->src[0]);
                assert(packed_addr < 0b1000);
        } else {
                /* Indirect gets an extra source */
                packed_addr = bi_get_src(ins, regs, 0, false) | 0b11000;
        }

        /* The destination is thrown in the data register */
        assert(ins->dest & BIR_INDEX_REGISTER);
        clause->data_register = ins->dest & ~BIR_INDEX_REGISTER;

        assert(channels >= 1 && channels <= 4);

        struct bifrost_ld_var pack = {
                .src0 = bi_get_src(ins, regs, 1, false),
                .addr = packed_addr,
                .channels = MALI_POSITIVE(channels),
                .interp_mode = ins->load_vary.interp_mode,
                .reuse = ins->load_vary.reuse,
                .flat = ins->load_vary.flat,
                .op = op
        };

        RETURN_PACKED(pack);
}

static unsigned
bi_pack_add_2src(bi_instruction *ins, struct bi_registers *regs, unsigned op)
{
        struct bifrost_add_2src pack = {
                .src0 = bi_get_src(ins, regs, 0, true),
                .src1 = bi_get_src(ins, regs, 1, true),
                .op = op
        };

        RETURN_PACKED(pack);
}

static unsigned
bi_pack_add_ld_ubo(bi_clause *clause, bi_instruction *ins, struct bi_registers *regs)
{
        unsigned components = bi_load32_components(ins);

        const unsigned ops[4] = {
                BIFROST_ADD_OP_LD_UBO_1,
                BIFROST_ADD_OP_LD_UBO_2,
                BIFROST_ADD_OP_LD_UBO_3,
                BIFROST_ADD_OP_LD_UBO_4
        };

        bi_write_data_register(clause, ins);
        return bi_pack_add_2src(ins, regs, ops[components - 1]);
}

static enum bifrost_ldst_type
bi_pack_ldst_type(nir_alu_type T)
{
        switch (T) {
        case nir_type_float16: return BIFROST_LDST_F16;
        case nir_type_float32: return BIFROST_LDST_F32;
        case nir_type_int32:   return BIFROST_LDST_I32;
        case nir_type_uint32:  return BIFROST_LDST_U32;
        default: unreachable("Invalid type loaded");
        }
}

static unsigned
bi_pack_add_ld_var_addr(bi_clause *clause, bi_instruction *ins, struct bi_registers *regs)
{
        struct bifrost_ld_var_addr pack = {
                .src0 = bi_get_src(ins, regs, 1, false),
                .src1 = bi_get_src(ins, regs, 2, false),
                .location = bi_get_immediate(ins, ins->src[0]),
                .type = bi_pack_ldst_type(ins->src_types[3]),
                .op = BIFROST_ADD_OP_LD_VAR_ADDR
        };

        bi_write_data_register(clause, ins);
        RETURN_PACKED(pack);
}

static unsigned
bi_pack_add_ld_attr(bi_clause *clause, bi_instruction *ins, struct bi_registers *regs)
{
        struct bifrost_ld_attr pack = {
                .src0 = bi_get_src(ins, regs, 1, false),
                .src1 = bi_get_src(ins, regs, 2, false),
                .location = bi_get_immediate(ins, ins->src[0]),
                .channels = MALI_POSITIVE(bi_load32_components(ins)),
                .type = bi_pack_ldst_type(ins->dest_type),
                .op = BIFROST_ADD_OP_LD_ATTR
        };

        bi_write_data_register(clause, ins);
        RETURN_PACKED(pack);
}

static unsigned
bi_pack_add_st_vary(bi_clause *clause, bi_instruction *ins, struct bi_registers *regs)
{
        assert(ins->store_channels >= 1 && ins->store_channels <= 4);

        struct bifrost_st_vary pack = {
                .src0 = bi_get_src(ins, regs, 1, false),
                .src1 = bi_get_src(ins, regs, 2, false),
                .src2 = bi_get_src(ins, regs, 3, false),
                .channels = MALI_POSITIVE(ins->store_channels),
                .op = BIFROST_ADD_OP_ST_VAR
        };

        bi_read_data_register(clause, ins);
        RETURN_PACKED(pack);
}

static unsigned
bi_pack_add_atest(bi_clause *clause, bi_instruction *ins, struct bi_registers *regs)
{
        /* TODO: fp16 */
        assert(ins->src_types[1] == nir_type_float32);

        struct bifrost_add_atest pack = {
                .src0 = bi_get_src(ins, regs, 0, false),
                .src1 = bi_get_src(ins, regs, 1, false),
                .component = 1, /* Set for fp32 */
                .op = BIFROST_ADD_OP_ATEST,
        };

        /* Despite *also* writing with the usual mechanism... quirky and
         * perhaps unnecessary, but let's match the blob */
        clause->data_register = ins->dest & ~BIR_INDEX_REGISTER;

        RETURN_PACKED(pack);
}

static unsigned
bi_pack_add_blend(bi_instruction *ins, struct bi_registers *regs)
{
        struct bifrost_add_inst pack = {
                .src0 = bi_get_src(ins, regs, 0, false),
                .op = BIFROST_ADD_OP_BLEND
        };

        /* TODO: Pack location in uniform_const */
        assert(ins->blend_location == 0);

        RETURN_PACKED(pack);
}

static unsigned
bi_pack_add(bi_clause *clause, bi_bundle bundle, struct bi_registers *regs)
{
        if (!bundle.add)
                return BIFROST_ADD_NOP;

        switch (bundle.add->type) {
        case BI_ADD:
                return BIFROST_ADD_NOP;
        case BI_ATEST:
                return bi_pack_add_atest(clause, bundle.add, regs);
        case BI_BRANCH:
        case BI_CMP:
                return BIFROST_ADD_NOP;
        case BI_BLEND:
                return bi_pack_add_blend(bundle.add, regs);
        case BI_BITWISE:
        case BI_CONVERT:
        case BI_DISCARD:
        case BI_FREXP:
        case BI_ISUB:
        case BI_LOAD:
                return BIFROST_ADD_NOP;
        case BI_LOAD_ATTR:
                return bi_pack_add_ld_attr(clause, bundle.add, regs);
        case BI_LOAD_UNIFORM:
                return bi_pack_add_ld_ubo(clause, bundle.add, regs);
        case BI_LOAD_VAR:
                return bi_pack_add_ld_vary(clause, bundle.add, regs);
        case BI_LOAD_VAR_ADDRESS:
                return bi_pack_add_ld_var_addr(clause, bundle.add, regs);
        case BI_MINMAX:
        case BI_MOV:
        case BI_SHIFT:
        case BI_STORE:
                return BIFROST_ADD_NOP;
        case BI_STORE_VAR:
                return bi_pack_add_st_vary(clause, bundle.add, regs);
        case BI_SPECIAL:
        case BI_SWIZZLE:
        case BI_TEX:
        case BI_ROUND:
                return BIFROST_ADD_NOP;
        default:
                unreachable("Cannot encode class as ADD");
        }
}

struct bi_packed_bundle {
        uint64_t lo;
        uint64_t hi;
};

static struct bi_packed_bundle
bi_pack_bundle(bi_clause *clause, bi_bundle bundle, bi_bundle prev, bool first_bundle)
{
        struct bi_registers regs = bi_assign_ports(bundle, prev);
        bi_assign_uniform_constant(clause, &regs, bundle);
        regs.first_instruction = first_bundle;

        uint64_t reg = bi_pack_registers(regs);
        uint64_t fma = bi_pack_fma(clause, bundle, &regs);
        uint64_t add = bi_pack_add(clause, bundle, &regs);

        struct bi_packed_bundle packed = {
                .lo = reg | (fma << 35) | ((add & 0b111111) << 58),
                .hi = add >> 6
        };

        return packed;
}

/* Packs the next two constants as a dedicated constant quadword at the end of
 * the clause, returning the number packed. */

static unsigned
bi_pack_constants(bi_context *ctx, bi_clause *clause,
                unsigned index,
                struct util_dynarray *emission)
{
        /* After these two, are we done? Determines tag */
        bool done = clause->constant_count <= (index + 2);
        bool only = clause->constant_count <= (index + 1);

        /* TODO: Pos */
        assert(index == 0 && clause->bundle_count == 1);

        struct bifrost_fmt_constant quad = {
                .pos = 0, /* TODO */
                .tag = done ? BIFROST_FMTC_FINAL : BIFROST_FMTC_CONSTANTS,
                .imm_1 = clause->constants[index + 0] >> 4,
                .imm_2 = only ? 0 : clause->constants[index + 1] >> 4
        };

        /* XXX: On G71, Connor observed that the difference of the top 4 bits
         * of the second constant with the first must be less than 8, otherwise
         * we have to swap them. I am not able to reproduce this on G52,
         * further investigation needed. Possibly an errata. XXX */

        util_dynarray_append(emission, struct bifrost_fmt_constant, quad);

        return 2;
}

static void
bi_pack_clause(bi_context *ctx, bi_clause *clause, bi_clause *next,
                struct util_dynarray *emission)
{
        struct bi_packed_bundle ins_1 = bi_pack_bundle(clause, clause->bundles[0], clause->bundles[0], true);
        assert(clause->bundle_count == 1);

        /* Used to decide if we elide writes */
        bool is_fragment = ctx->stage == MESA_SHADER_FRAGMENT;

        /* State for packing constants throughout */
        unsigned constant_index = 0;

        struct bifrost_fmt1 quad_1 = {
                .tag = clause->constant_count ? BIFROST_FMT1_CONSTANTS : BIFROST_FMT1_FINAL,
                .header = bi_pack_header(clause, next, is_fragment),
                .ins_1 = ins_1.lo,
                .ins_2 = ins_1.hi & ((1 << 11) - 1),
                .ins_0 = (ins_1.hi >> 11) & 0b111,
        };

        util_dynarray_append(emission, struct bifrost_fmt1, quad_1);

        /* Pack the remaining constants */

        while (constant_index < clause->constant_count) {
                constant_index += bi_pack_constants(ctx, clause,
                                constant_index, emission);
        }
}

static bi_clause *
bi_next_clause(bi_context *ctx, pan_block *block, bi_clause *clause)
{
        /* Try the next clause in this block */
        if (clause->link.next != &((bi_block *) block)->clauses)
                return list_first_entry(&(clause->link), bi_clause, link);

        /* Try the next block, or the one after that if it's empty, etc .*/
        pan_block *next_block = pan_next_block(block);

        bi_foreach_block_from(ctx, next_block, block) {
                bi_block *blk = (bi_block *) block;

                if (!list_is_empty(&blk->clauses))
                        return list_first_entry(&(blk->clauses), bi_clause, link);
        }

        return NULL;
}

void
bi_pack(bi_context *ctx, struct util_dynarray *emission)
{
        util_dynarray_init(emission, NULL);

        bi_foreach_block(ctx, _block) {
                bi_block *block = (bi_block *) _block;

                bi_foreach_clause_in_block(block, clause) {
                        bi_clause *next = bi_next_clause(ctx, _block, clause);
                        bi_pack_clause(ctx, clause, next, emission);
                }
        }
}
