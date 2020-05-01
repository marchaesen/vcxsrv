/*
 * Copyright (C) 2019 Connor Abbott <cwabbott0@gmail.com>
 * Copyright (C) 2019 Lyude Paul <thatslyude@gmail.com>
 * Copyright (C) 2019 Ryan Houdek <Sonicadvance1@gmail.com>
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

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <inttypes.h>
#include <string.h>

#include "bifrost.h"
#include "disassemble.h"
#include "bi_print.h"
#include "util/macros.h"

// return bits (high, lo]
static uint64_t bits(uint32_t word, unsigned lo, unsigned high)
{
        if (high == 32)
                return word >> lo;
        return (word & ((1 << high) - 1)) >> lo;
}

// each of these structs represents an instruction that's dispatched in one
// cycle. Note that these instructions are packed in funny ways within the
// clause, hence the need for a separate struct.
struct bifrost_alu_inst {
        uint32_t fma_bits;
        uint32_t add_bits;
        uint64_t reg_bits;
};

static unsigned get_reg0(struct bifrost_regs regs)
{
        if (regs.ctrl == 0)
                return regs.reg0 | ((regs.reg1 & 0x1) << 5);

        return regs.reg0 <= regs.reg1 ? regs.reg0 : 63 - regs.reg0;
}

static unsigned get_reg1(struct bifrost_regs regs)
{
        return regs.reg0 <= regs.reg1 ? regs.reg1 : 63 - regs.reg1;
}

// this represents the decoded version of the ctrl register field.
struct bifrost_reg_ctrl {
        bool read_reg0;
        bool read_reg1;
        bool read_reg3;
        enum bifrost_reg_write_unit fma_write_unit;
        enum bifrost_reg_write_unit add_write_unit;
        bool clause_start;
};

enum fma_src_type {
        FMA_ONE_SRC,
        FMA_TWO_SRC,
        FMA_FADD,
        FMA_FMINMAX,
        FMA_FADD16,
        FMA_FMINMAX16,
        FMA_FCMP,
        FMA_FCMP16,
        FMA_THREE_SRC,
        FMA_SHIFT,
        FMA_FMA,
        FMA_FMA16,
        FMA_CSEL4,
        FMA_FMA_MSCALE,
        FMA_SHIFT_ADD64,
};

struct fma_op_info {
        bool extended;
        unsigned op;
        char name[30];
        enum fma_src_type src_type;
};

enum add_src_type {
        ADD_ONE_SRC,
        ADD_TWO_SRC,
        ADD_FADD,
        ADD_FMINMAX,
        ADD_FADD16,
        ADD_FMINMAX16,
        ADD_THREE_SRC,
        ADD_SHIFT,
        ADD_FADDMscale,
        ADD_FCMP,
        ADD_FCMP16,
        ADD_TEX_COMPACT, // texture instruction with embedded sampler
        ADD_TEX, // texture instruction with sampler/etc. in uniform port
        ADD_VARYING_INTERP,
        ADD_BLENDING,
        ADD_LOAD_ATTR,
        ADD_VARYING_ADDRESS,
        ADD_BRANCH,
};

struct add_op_info {
        unsigned op;
        char name[30];
        enum add_src_type src_type;
        bool has_data_reg;
};

void dump_header(FILE *fp, struct bifrost_header header, bool verbose);
void dump_instr(FILE *fp, const struct bifrost_alu_inst *instr,
                struct bifrost_regs next_regs, uint64_t *consts,
                unsigned data_reg, unsigned offset, bool verbose);
bool dump_clause(FILE *fp, uint32_t *words, unsigned *size, unsigned offset, bool verbose);

void dump_header(FILE *fp, struct bifrost_header header, bool verbose)
{
        fprintf(fp, "id(%du) ", header.scoreboard_index);

        if (header.clause_type != 0) {
                const char *name = bi_clause_type_name(header.clause_type);

                if (name[0] == '?')
                        fprintf(fp, "unk%u ", header.clause_type);
                else
                        fprintf(fp, "%s ", name);
        }

        if (header.scoreboard_deps != 0) {
                fprintf(fp, "next-wait(");
                bool first = true;
                for (unsigned i = 0; i < 8; i++) {
                        if (header.scoreboard_deps & (1 << i)) {
                                if (!first) {
                                        fprintf(fp, ", ");
                                }
                                fprintf(fp, "%d", i);
                                first = false;
                        }
                }
                fprintf(fp, ") ");
        }

        if (header.datareg_writebarrier)
                fprintf(fp, "data-reg-barrier ");

        if (!header.no_end_of_shader)
                fprintf(fp, "eos ");

        if (!header.back_to_back) {
                fprintf(fp, "nbb ");
                if (header.branch_cond)
                        fprintf(fp, "branch-cond ");
                else
                        fprintf(fp, "branch-uncond ");
        }

        if (header.elide_writes)
                fprintf(fp, "we ");

        if (header.suppress_inf)
                fprintf(fp, "suppress-inf ");
        if (header.suppress_nan)
                fprintf(fp, "suppress-nan ");

        if (header.unk0)
                fprintf(fp, "unk0 ");
        if (header.unk1)
                fprintf(fp, "unk1 ");
        if  (header.unk2)
                fprintf(fp, "unk2 ");
        if (header.unk3)
                fprintf(fp, "unk3 ");
        if (header.unk4)
                fprintf(fp, "unk4 ");

        fprintf(fp, "\n");

        if (verbose) {
                fprintf(fp, "# clause type %d, next clause type %d\n",
                       header.clause_type, header.next_clause_type);
        }
}

static struct bifrost_reg_ctrl DecodeRegCtrl(FILE *fp, struct bifrost_regs regs)
{
        struct bifrost_reg_ctrl decoded = {};
        unsigned ctrl;
        if (regs.ctrl == 0) {
                ctrl = regs.reg1 >> 2;
                decoded.read_reg0 = !(regs.reg1 & 0x2);
                decoded.read_reg1 = false;
        } else {
                ctrl = regs.ctrl;
                decoded.read_reg0 = decoded.read_reg1 = true;
        }
        switch (ctrl) {
        case 1:
                decoded.fma_write_unit = REG_WRITE_TWO;
                break;
        case 2:
        case 3:
                decoded.fma_write_unit = REG_WRITE_TWO;
                decoded.read_reg3 = true;
                break;
        case 4:
                decoded.read_reg3 = true;
                break;
        case 5:
                decoded.add_write_unit = REG_WRITE_TWO;
                break;
        case 6:
                decoded.add_write_unit = REG_WRITE_TWO;
                decoded.read_reg3 = true;
                break;
        case 8:
                decoded.clause_start = true;
                break;
        case 9:
                decoded.fma_write_unit = REG_WRITE_TWO;
                decoded.clause_start = true;
                break;
        case 11:
                break;
        case 12:
                decoded.read_reg3 = true;
                decoded.clause_start = true;
                break;
        case 13:
                decoded.add_write_unit = REG_WRITE_TWO;
                decoded.clause_start = true;
                break;

        case 7:
        case 15:
                decoded.fma_write_unit = REG_WRITE_THREE;
                decoded.add_write_unit = REG_WRITE_TWO;
                break;
        default:
                fprintf(fp, "# unknown reg ctrl %d\n", ctrl);
        }

        return decoded;
}

// Pass in the add_write_unit or fma_write_unit, and this returns which register
// the ADD/FMA units are writing to
static unsigned GetRegToWrite(enum bifrost_reg_write_unit unit, struct bifrost_regs regs)
{
        switch (unit) {
        case REG_WRITE_TWO:
                return regs.reg2;
        case REG_WRITE_THREE:
                return regs.reg3;
        default: /* REG_WRITE_NONE */
                assert(0);
                return 0;
        }
}

static void dump_regs(FILE *fp, struct bifrost_regs srcs)
{
        struct bifrost_reg_ctrl ctrl = DecodeRegCtrl(fp, srcs);
        fprintf(fp, "# ");
        if (ctrl.read_reg0)
                fprintf(fp, "port 0: R%d ", get_reg0(srcs));
        if (ctrl.read_reg1)
                fprintf(fp, "port 1: R%d ", get_reg1(srcs));

        if (ctrl.fma_write_unit == REG_WRITE_TWO)
                fprintf(fp, "port 2: R%d (write FMA) ", srcs.reg2);
        else if (ctrl.add_write_unit == REG_WRITE_TWO)
                fprintf(fp, "port 2: R%d (write ADD) ", srcs.reg2);

        if (ctrl.fma_write_unit == REG_WRITE_THREE)
                fprintf(fp, "port 3: R%d (write FMA) ", srcs.reg3);
        else if (ctrl.add_write_unit == REG_WRITE_THREE)
                fprintf(fp, "port 3: R%d (write ADD) ", srcs.reg3);
        else if (ctrl.read_reg3)
                fprintf(fp, "port 3: R%d (read) ", srcs.reg3);

        if (srcs.uniform_const) {
                if (srcs.uniform_const & 0x80) {
                        fprintf(fp, "uniform: U%d", (srcs.uniform_const & 0x7f) * 2);
                }
        }

        fprintf(fp, "\n");
}
static void dump_const_imm(FILE *fp, uint32_t imm)
{
        union {
                float f;
                uint32_t i;
        } fi;
        fi.i = imm;
        fprintf(fp, "0x%08x /* %f */", imm, fi.f);
}

static uint64_t get_const(uint64_t *consts, struct bifrost_regs srcs)
{
        unsigned low_bits = srcs.uniform_const & 0xf;
        uint64_t imm;
        switch (srcs.uniform_const >> 4) {
        case 4:
                imm = consts[0];
                break;
        case 5:
                imm = consts[1];
                break;
        case 6:
                imm = consts[2];
                break;
        case 7:
                imm = consts[3];
                break;
        case 2:
                imm = consts[4];
                break;
        case 3:
                imm = consts[5];
                break;
        default:
                assert(0);
                break;
        }
        return imm | low_bits;
}

static void dump_uniform_const_src(FILE *fp, struct bifrost_regs srcs, uint64_t *consts, bool high32)
{
        if (srcs.uniform_const & 0x80) {
                unsigned uniform = (srcs.uniform_const & 0x7f) * 2;
                fprintf(fp, "U%d", uniform + (high32 ? 1 : 0));
        } else if (srcs.uniform_const >= 0x20) {
                uint64_t imm = get_const(consts, srcs);
                if (high32)
                        dump_const_imm(fp, imm >> 32);
                else
                        dump_const_imm(fp, imm);
        } else {
                switch (srcs.uniform_const) {
                case 0:
                        fprintf(fp, "0");
                        break;
                case 5:
                        fprintf(fp, "atest-data");
                        break;
                case 6:
                        fprintf(fp, "sample-ptr");
                        break;
                case 8:
                case 9:
                case 10:
                case 11:
                case 12:
                case 13:
                case 14:
                case 15:
                        fprintf(fp, "blend-descriptor%u", (unsigned) srcs.uniform_const - 8);
                        break;
                default:
                        fprintf(fp, "unkConst%u", (unsigned) srcs.uniform_const);
                        break;
                }

                if (high32)
                        fprintf(fp, ".y");
                else
                        fprintf(fp, ".x");
        }
}

static void dump_src(FILE *fp, unsigned src, struct bifrost_regs srcs, uint64_t *consts, bool isFMA)
{
        switch (src) {
        case 0:
                fprintf(fp, "R%d", get_reg0(srcs));
                break;
        case 1:
                fprintf(fp, "R%d", get_reg1(srcs));
                break;
        case 2:
                fprintf(fp, "R%d", srcs.reg3);
                break;
        case 3:
                if (isFMA)
                        fprintf(fp, "0");
                else
                        fprintf(fp, "T"); // i.e. the output of FMA this cycle
                break;
        case 4:
                dump_uniform_const_src(fp, srcs, consts, false);
                break;
        case 5:
                dump_uniform_const_src(fp, srcs, consts, true);
                break;
        case 6:
                fprintf(fp, "T0");
                break;
        case 7:
                fprintf(fp, "T1");
                break;
        }
}

static const struct fma_op_info FMAOpInfos[] = {
        { false, 0x00000, "FMA.f32",  FMA_FMA },
        { false, 0x40000, "MAX.f32", FMA_FMINMAX },
        { false, 0x44000, "MIN.f32", FMA_FMINMAX },
        { false, 0x48000, "FCMP.GL", FMA_FCMP },
        { false, 0x4c000, "FCMP.D3D", FMA_FCMP },
        { false, 0x4ff98, "ADD.i32", FMA_TWO_SRC },
        { false, 0x4ffd8, "SUB.i32", FMA_TWO_SRC },
        { false, 0x4fff0, "SUBB.i32", FMA_TWO_SRC },
        { false, 0x50000, "FMA_MSCALE", FMA_FMA_MSCALE },
        { false, 0x58000, "ADD.f32", FMA_FADD },
        { false, 0x5c000, "CSEL4", FMA_CSEL4 },
        { false, 0x5d8d0, "ICMP.D3D.GT.v2i16", FMA_TWO_SRC },
        { false, 0x5d9d0, "UCMP.D3D.GT.v2i16", FMA_TWO_SRC },
        { false, 0x5dad0, "ICMP.D3D.GE.v2i16", FMA_TWO_SRC },
        { false, 0x5dbd0, "UCMP.D3D.GE.v2i16", FMA_TWO_SRC },
        { false, 0x5dcd0, "ICMP.D3D.EQ.v2i16", FMA_TWO_SRC },
        { false, 0x5de40, "ICMP.GL.GT.i32", FMA_TWO_SRC }, // src0 > src1 ? 1 : 0
        { false, 0x5de48, "ICMP.GL.GE.i32", FMA_TWO_SRC },
        { false, 0x5de50, "UCMP.GL.GT.i32", FMA_TWO_SRC },
        { false, 0x5de58, "UCMP.GL.GE.i32", FMA_TWO_SRC },
        { false, 0x5de60, "ICMP.GL.EQ.i32", FMA_TWO_SRC },
        { false, 0x5dec0, "ICMP.D3D.GT.i32", FMA_TWO_SRC }, // src0 > src1 ? ~0 : 0
        { false, 0x5dec8, "ICMP.D3D.GE.i32", FMA_TWO_SRC },
        { false, 0x5ded0, "UCMP.D3D.GT.i32", FMA_TWO_SRC },
        { false, 0x5ded8, "UCMP.D3D.GE.i32", FMA_TWO_SRC },
        { false, 0x5dee0, "ICMP.D3D.EQ.i32", FMA_TWO_SRC },
        { false, 0x60000, "RSHIFT_NAND", FMA_SHIFT },
        { false, 0x61000, "RSHIFT_AND", FMA_SHIFT },
        { false, 0x62000, "LSHIFT_NAND", FMA_SHIFT },
        { false, 0x63000, "LSHIFT_AND", FMA_SHIFT }, // (src0 << src2) & src1
        { false, 0x64000, "RSHIFT_XOR", FMA_SHIFT },
        { false, 0x65200, "LSHIFT_ADD.i32", FMA_THREE_SRC },
        { false, 0x65600, "LSHIFT_SUB.i32", FMA_THREE_SRC }, // (src0 << src2) - src1
        { false, 0x65a00, "LSHIFT_RSUB.i32", FMA_THREE_SRC }, // src1 - (src0 << src2)
        { false, 0x65e00, "RSHIFT_ADD.i32", FMA_THREE_SRC },
        { false, 0x66200, "RSHIFT_SUB.i32", FMA_THREE_SRC },
        { false, 0x66600, "RSHIFT_RSUB.i32", FMA_THREE_SRC },
        { false, 0x66a00, "ARSHIFT_ADD.i32", FMA_THREE_SRC },
        { false, 0x66e00, "ARSHIFT_SUB.i32", FMA_THREE_SRC },
        { false, 0x67200, "ARSHIFT_RSUB.i32", FMA_THREE_SRC },
        { false, 0x80000, "FMA.v2f16",  FMA_FMA16 },
        { false, 0xc0000, "MAX.v2f16", FMA_FMINMAX16 },
        { false, 0xc4000, "MIN.v2f16", FMA_FMINMAX16 },
        { false, 0xc8000, "FCMP.GL", FMA_FCMP16 },
        { false, 0xcc000, "FCMP.D3D", FMA_FCMP16 },
        { false, 0xcf900, "ADD.v2i16", FMA_TWO_SRC },
        { false, 0xcfc10, "ADDC.i32", FMA_TWO_SRC },
        { false, 0xcfd80, "ADD.i32.i16.X", FMA_TWO_SRC },
        { false, 0xcfd90, "ADD.i32.u16.X", FMA_TWO_SRC },
        { false, 0xcfdc0, "ADD.i32.i16.Y", FMA_TWO_SRC },
        { false, 0xcfdd0, "ADD.i32.u16.Y", FMA_TWO_SRC },
        { false, 0xd8000, "ADD.v2f16", FMA_FADD16 },
        { false, 0xdc000, "CSEL4.v16", FMA_CSEL4 },
        { false, 0xdd000, "F32_TO_F16", FMA_TWO_SRC },

        /* TODO: Combine to bifrost_fma_f2i_i2f16 */
        { true,  0x00046, "F16_TO_I16.XX", FMA_ONE_SRC },
        { true,  0x00047, "F16_TO_U16.XX", FMA_ONE_SRC },
        { true,  0x0004e, "F16_TO_I16.YX", FMA_ONE_SRC },
        { true,  0x0004f, "F16_TO_U16.YX", FMA_ONE_SRC },
        { true,  0x00056, "F16_TO_I16.XY", FMA_ONE_SRC },
        { true,  0x00057, "F16_TO_U16.XY", FMA_ONE_SRC },
        { true,  0x0005e, "F16_TO_I16.YY", FMA_ONE_SRC },
        { true,  0x0005f, "F16_TO_U16.YY", FMA_ONE_SRC },
        { true,  0x000c0, "I16_TO_F16.XX", FMA_ONE_SRC },
        { true,  0x000c1, "U16_TO_F16.XX", FMA_ONE_SRC },
        { true,  0x000c8, "I16_TO_F16.YX", FMA_ONE_SRC },
        { true,  0x000c9, "U16_TO_F16.YX", FMA_ONE_SRC },
        { true,  0x000d0, "I16_TO_F16.XY", FMA_ONE_SRC },
        { true,  0x000d1, "U16_TO_F16.XY", FMA_ONE_SRC },
        { true,  0x000d8, "I16_TO_F16.YY", FMA_ONE_SRC },
        { true,  0x000d9, "U16_TO_F16.YY", FMA_ONE_SRC },

        { true,  0x00136, "F32_TO_I32", FMA_ONE_SRC },
        { true,  0x00137, "F32_TO_U32", FMA_ONE_SRC },
        { true,  0x00178, "I32_TO_F32", FMA_ONE_SRC },
        { true,  0x00179, "U32_TO_F32", FMA_ONE_SRC },

        /* TODO: cleanup to use bifrost_fma_int16_to_32 */
        { true,  0x00198, "I16_TO_I32.X", FMA_ONE_SRC },
        { true,  0x00199, "U16_TO_U32.X", FMA_ONE_SRC },
        { true,  0x0019a, "I16_TO_I32.Y", FMA_ONE_SRC },
        { true,  0x0019b, "U16_TO_U32.Y", FMA_ONE_SRC },
        { true,  0x0019c, "I16_TO_F32.X", FMA_ONE_SRC },
        { true,  0x0019d, "U16_TO_F32.X", FMA_ONE_SRC },
        { true,  0x0019e, "I16_TO_F32.Y", FMA_ONE_SRC },
        { true,  0x0019f, "U16_TO_F32.Y", FMA_ONE_SRC },

        { true,  0x001a2, "F16_TO_F32.X", FMA_ONE_SRC },
        { true,  0x001a3, "F16_TO_F32.Y", FMA_ONE_SRC },

        { true,  0x0032c, "NOP",  FMA_ONE_SRC },
        { true,  0x0032d, "MOV",  FMA_ONE_SRC },
        { true,  0x0032f, "SWZ.YY.v2i16",  FMA_ONE_SRC },
        { true,  0x00345, "LOG_FREXPM", FMA_ONE_SRC },
        { true,  0x00365, "FRCP_FREXPM", FMA_ONE_SRC },
        { true,  0x00375, "FSQRT_FREXPM", FMA_ONE_SRC },
        { true,  0x0038d, "FRCP_FREXPE", FMA_ONE_SRC },
        { true,  0x003a5, "FSQRT_FREXPE", FMA_ONE_SRC },
        { true,  0x003ad, "FRSQ_FREXPE", FMA_ONE_SRC },
        { true,  0x003c5, "LOG_FREXPE", FMA_ONE_SRC },
        { true,  0x003fa, "CLZ", FMA_ONE_SRC },
        { true,  0x00b80, "IMAX3", FMA_THREE_SRC },
        { true,  0x00bc0, "UMAX3", FMA_THREE_SRC },
        { true,  0x00c00, "IMIN3", FMA_THREE_SRC },
        { true,  0x00c40, "UMIN3", FMA_THREE_SRC },
        { true,  0x00ec2, "ROUND.v2f16", FMA_ONE_SRC },
        { true,  0x00ec5, "ROUND.f32", FMA_ONE_SRC },
        { true,  0x00f40, "CSEL", FMA_THREE_SRC }, // src2 != 0 ? src1 : src0
        { true,  0x00fc0, "MUX.i32", FMA_THREE_SRC }, // see ADD comment
        { true,  0x01802, "ROUNDEVEN.v2f16", FMA_ONE_SRC },
        { true,  0x01805, "ROUNDEVEN.f32", FMA_ONE_SRC },
        { true,  0x01842, "CEIL.v2f16", FMA_ONE_SRC },
        { true,  0x01845, "CEIL.f32", FMA_ONE_SRC },
        { true,  0x01882, "FLOOR.v2f16", FMA_ONE_SRC },
        { true,  0x01885, "FLOOR.f32", FMA_ONE_SRC },
        { true,  0x018c2, "TRUNC.v2f16", FMA_ONE_SRC },
        { true,  0x018c5, "TRUNC.f32", FMA_ONE_SRC },
        { true,  0x019b0, "ATAN_LDEXP.Y.f32", FMA_TWO_SRC },
        { true,  0x019b8, "ATAN_LDEXP.X.f32", FMA_TWO_SRC },
        { true,  0x01c80, "LSHIFT_ADD_LOW32.u32", FMA_SHIFT_ADD64 },
        { true,  0x01cc0, "LSHIFT_ADD_LOW32.i64", FMA_SHIFT_ADD64 },
        { true,  0x01d80, "LSHIFT_ADD_LOW32.i32", FMA_SHIFT_ADD64 },
        { true,  0x01e00, "SEL.XX.i16", FMA_TWO_SRC },
        { true,  0x01e08, "SEL.YX.i16", FMA_TWO_SRC },
        { true,  0x01e10, "SEL.XY.i16", FMA_TWO_SRC },
        { true,  0x01e18, "SEL.YY.i16", FMA_TWO_SRC },
        { true,  0x01e80, "ADD_FREXPM.f32", FMA_TWO_SRC },
        { true,  0x02000, "SWZ.XXXX.v4i8", FMA_ONE_SRC },
        { true,  0x03e00, "SWZ.ZZZZ.v4i8", FMA_ONE_SRC },
        { true,  0x00800, "IMAD", FMA_THREE_SRC },
        { true,  0x078db, "POPCNT", FMA_ONE_SRC },
};

static struct fma_op_info find_fma_op_info(unsigned op, bool extended)
{
        for (unsigned i = 0; i < ARRAY_SIZE(FMAOpInfos); i++) {
                unsigned opCmp = ~0;

                if (FMAOpInfos[i].extended != extended)
                        continue;

                if (extended)
                        op &= ~0xe0000;

                switch (FMAOpInfos[i].src_type) {
                case FMA_ONE_SRC:
                        opCmp = op;
                        break;
                case FMA_TWO_SRC:
                        opCmp = op & ~0x7;
                        break;
                case FMA_FCMP:
                case FMA_FCMP16:
                        opCmp = op & ~0x1fff;
                        break;
                case FMA_THREE_SRC:
                case FMA_SHIFT_ADD64:
                        opCmp = op & ~0x3f;
                        break;
                case FMA_FADD:
                case FMA_FMINMAX:
                case FMA_FADD16:
                case FMA_FMINMAX16:
                        opCmp = op & ~0x3fff;
                        break;
                case FMA_FMA:
                case FMA_FMA16:
                        opCmp = op & ~0x3ffff;
                        break;
                case FMA_CSEL4:
                case FMA_SHIFT:
                        opCmp = op & ~0xfff;
                        break;
                case FMA_FMA_MSCALE:
                        opCmp = op & ~0x7fff;
                        break;
                default:
                        opCmp = ~0;
                        break;
                }
                if (FMAOpInfos[i].op == opCmp)
                        return FMAOpInfos[i];
        }

        struct fma_op_info info;
        snprintf(info.name, sizeof(info.name), "op%04x", op);
        info.op = op;
        info.src_type = FMA_THREE_SRC;
        return info;
}

static void dump_fcmp(FILE *fp, unsigned op)
{
        switch (op) {
        case 0:
                fprintf(fp, ".OEQ");
                break;
        case 1:
                fprintf(fp, ".OGT");
                break;
        case 2:
                fprintf(fp, ".OGE");
                break;
        case 3:
                fprintf(fp, ".UNE");
                break;
        case 4:
                fprintf(fp, ".OLT");
                break;
        case 5:
                fprintf(fp, ".OLE");
                break;
        default:
                fprintf(fp, ".unk%d", op);
                break;
        }
}

static void dump_16swizzle(FILE *fp, unsigned swiz)
{
        if (swiz == 2)
                return;
        fprintf(fp, ".%c%c", "xy"[swiz & 1], "xy"[(swiz >> 1) & 1]);
}

static void dump_fma_expand_src0(FILE *fp, unsigned ctrl)
{
        switch (ctrl) {
        case 3:
        case 4:
        case 6:
                fprintf(fp, ".x");
                break;
        case 5:
        case 7:
                fprintf(fp, ".y");
                break;
        case 0:
        case 1:
        case 2:
                break;
        default:
                fprintf(fp, ".unk");
                break;
        }
}

static void dump_fma_expand_src1(FILE *fp, unsigned ctrl)
{
        switch (ctrl) {
        case 1:
        case 3:
                fprintf(fp, ".x");
                break;
        case 2:
        case 4:
        case 5:
                fprintf(fp, ".y");
                break;
        case 0:
        case 6:
        case 7:
                break;
        default:
                fprintf(fp, ".unk");
                break;
        }
}

static void dump_fma(FILE *fp, uint64_t word, struct bifrost_regs regs, struct bifrost_regs next_regs, uint64_t *consts, bool verbose)
{
        if (verbose) {
                fprintf(fp, "# FMA: %016" PRIx64 "\n", word);
        }
        struct bifrost_fma_inst FMA;
        memcpy((char *) &FMA, (char *) &word, sizeof(struct bifrost_fma_inst));
        struct fma_op_info info = find_fma_op_info(FMA.op, (FMA.op & 0xe0000) == 0xe0000);

        fprintf(fp, "%s", info.name);
        if (info.src_type == FMA_FADD ||
            info.src_type == FMA_FMINMAX ||
            info.src_type == FMA_FMA ||
            info.src_type == FMA_FADD16 ||
            info.src_type == FMA_FMINMAX16 ||
            info.src_type == FMA_FMA16) {
                fprintf(fp, "%s", bi_output_mod_name(bits(FMA.op, 12, 14)));
                switch (info.src_type) {
                case FMA_FADD:
                case FMA_FMA:
                case FMA_FADD16:
                case FMA_FMA16:
                        fprintf(fp, "%s", bi_round_mode_name(bits(FMA.op, 10, 12)));
                        break;
                case FMA_FMINMAX:
                case FMA_FMINMAX16:
                        fprintf(fp, "%s", bi_minmax_mode_name(bits(FMA.op, 10, 12)));
                        break;
                default:
                        assert(0);
                }
        } else if (info.src_type == FMA_FCMP || info.src_type == FMA_FCMP16) {
                dump_fcmp(fp, bits(FMA.op, 10, 13));
                if (info.src_type == FMA_FCMP)
                        fprintf(fp, ".f32");
                else
                        fprintf(fp, ".v2f16");
        } else if (info.src_type == FMA_FMA_MSCALE) {
                if (FMA.op & (1 << 11)) {
                        switch ((FMA.op >> 9) & 0x3) {
                        case 0:
                                /* This mode seems to do a few things:
                                 * - Makes 0 * infinity (and incidentally 0 * nan) return 0,
                                 *   since generating a nan would poison the result of
                                 *   1/infinity and 1/0.
                                 * - Fiddles with which nan is returned in nan * nan,
                                 *   presumably to make sure that the same exact nan is
                                 *   returned for 1/nan.
                                 */
                                fprintf(fp, ".rcp_mode");
                                break;
                        case 3:
                                /* Similar to the above, but src0 always wins when multiplying
                                 * 0 by infinity.
                                 */
                                fprintf(fp, ".sqrt_mode");
                                break;
                        default:
                                fprintf(fp, ".unk%d_mode", (int) (FMA.op >> 9) & 0x3);
                        }
                } else {
                        fprintf(fp, "%s", bi_output_mod_name(bits(FMA.op, 9, 11)));
                }
        } else if (info.src_type == FMA_SHIFT) {
                struct bifrost_shift_fma shift;
                memcpy(&shift, &FMA, sizeof(shift));

                if (shift.half == 0x7)
                        fprintf(fp, ".v2i16");
                else if (shift.half == 0)
                        fprintf(fp, ".i32");
                else if (shift.half == 0x4)
                        fprintf(fp, ".v4i8");
                else
                        fprintf(fp, ".unk%u", shift.half);

                if (!shift.unk)
                        fprintf(fp, ".no_unk");

                if (shift.invert_1)
                        fprintf(fp, ".invert_1");

                if (shift.invert_2)
                        fprintf(fp, ".invert_2");
        }

        fprintf(fp, " ");

        struct bifrost_reg_ctrl next_ctrl = DecodeRegCtrl(fp, next_regs);
        if (next_ctrl.fma_write_unit != REG_WRITE_NONE) {
                fprintf(fp, "{R%d, T0}, ", GetRegToWrite(next_ctrl.fma_write_unit, next_regs));
        } else {
                fprintf(fp, "T0, ");
        }

        switch (info.src_type) {
        case FMA_ONE_SRC:
                dump_src(fp, FMA.src0, regs, consts, true);
                break;
        case FMA_TWO_SRC:
                dump_src(fp, FMA.src0, regs, consts, true);
                fprintf(fp, ", ");
                dump_src(fp, FMA.op & 0x7, regs, consts, true);
                break;
        case FMA_FADD:
        case FMA_FMINMAX:
                if (FMA.op & 0x10)
                        fprintf(fp, "-");
                if (FMA.op & 0x200)
                        fprintf(fp, "abs(");
                dump_src(fp, FMA.src0, regs, consts, true);
                dump_fma_expand_src0(fp, (FMA.op >> 6) & 0x7);
                if (FMA.op & 0x200)
                        fprintf(fp, ")");
                fprintf(fp, ", ");
                if (FMA.op & 0x20)
                        fprintf(fp, "-");
                if (FMA.op & 0x8)
                        fprintf(fp, "abs(");
                dump_src(fp, FMA.op & 0x7, regs, consts, true);
                dump_fma_expand_src1(fp, (FMA.op >> 6) & 0x7);
                if (FMA.op & 0x8)
                        fprintf(fp, ")");
                break;
        case FMA_FADD16:
        case FMA_FMINMAX16: {
                bool abs1 = FMA.op & 0x8;
                bool abs2 = (FMA.op & 0x7) < FMA.src0;
                if (FMA.op & 0x10)
                        fprintf(fp, "-");
                if (abs1 || abs2)
                        fprintf(fp, "abs(");
                dump_src(fp, FMA.src0, regs, consts, true);
                dump_16swizzle(fp, (FMA.op >> 6) & 0x3);
                if (abs1 || abs2)
                        fprintf(fp, ")");
                fprintf(fp, ", ");
                if (FMA.op & 0x20)
                        fprintf(fp, "-");
                if (abs1 && abs2)
                        fprintf(fp, "abs(");
                dump_src(fp, FMA.op & 0x7, regs, consts, true);
                dump_16swizzle(fp, (FMA.op >> 8) & 0x3);
                if (abs1 && abs2)
                        fprintf(fp, ")");
                break;
        }
        case FMA_FCMP:
                if (FMA.op & 0x200)
                        fprintf(fp, "abs(");
                dump_src(fp, FMA.src0, regs, consts, true);
                dump_fma_expand_src0(fp, (FMA.op >> 6) & 0x7);
                if (FMA.op & 0x200)
                        fprintf(fp, ")");
                fprintf(fp, ", ");
                if (FMA.op & 0x20)
                        fprintf(fp, "-");
                if (FMA.op & 0x8)
                        fprintf(fp, "abs(");
                dump_src(fp, FMA.op & 0x7, regs, consts, true);
                dump_fma_expand_src1(fp, (FMA.op >> 6) & 0x7);
                if (FMA.op & 0x8)
                        fprintf(fp, ")");
                break;
        case FMA_FCMP16:
                dump_src(fp, FMA.src0, regs, consts, true);
                // Note: this is kinda a guess, I haven't seen the blob set this to
                // anything other than the identity, but it matches FMA_TWO_SRCFmod16
                dump_16swizzle(fp, (FMA.op >> 6) & 0x3);
                fprintf(fp, ", ");
                dump_src(fp, FMA.op & 0x7, regs, consts, true);
                dump_16swizzle(fp, (FMA.op >> 8) & 0x3);
                break;
        case FMA_SHIFT_ADD64:
                dump_src(fp, FMA.src0, regs, consts, true);
                fprintf(fp, ", ");
                dump_src(fp, FMA.op & 0x7, regs, consts, true);
                fprintf(fp, ", ");
                fprintf(fp, "shift:%u", (FMA.op >> 3) & 0x7);
                break;
        case FMA_THREE_SRC:
                dump_src(fp, FMA.src0, regs, consts, true);
                fprintf(fp, ", ");
                dump_src(fp, FMA.op & 0x7, regs, consts, true);
                fprintf(fp, ", ");
                dump_src(fp, (FMA.op >> 3) & 0x7, regs, consts, true);
                break;
        case FMA_SHIFT: {
                struct bifrost_shift_fma shift;
                memcpy(&shift, &FMA, sizeof(shift));

                dump_src(fp, shift.src0, regs, consts, true);
                fprintf(fp, ", ");
                dump_src(fp, shift.src1, regs, consts, true);
                fprintf(fp, ", ");
                dump_src(fp, shift.src2, regs, consts, true);
                break;
        }
        case FMA_FMA:
                if (FMA.op & (1 << 14))
                        fprintf(fp, "-");
                if (FMA.op & (1 << 9))
                        fprintf(fp, "abs(");
                dump_src(fp, FMA.src0, regs, consts, true);
                dump_fma_expand_src0(fp, (FMA.op >> 6) & 0x7);
                if (FMA.op & (1 << 9))
                        fprintf(fp, ")");
                fprintf(fp, ", ");
                if (FMA.op & (1 << 16))
                        fprintf(fp, "abs(");
                dump_src(fp, FMA.op & 0x7, regs, consts, true);
                dump_fma_expand_src1(fp, (FMA.op >> 6) & 0x7);
                if (FMA.op & (1 << 16))
                        fprintf(fp, ")");
                fprintf(fp, ", ");
                if (FMA.op & (1 << 15))
                        fprintf(fp, "-");
                if (FMA.op & (1 << 17))
                        fprintf(fp, "abs(");
                dump_src(fp, (FMA.op >> 3) & 0x7, regs, consts, true);
                if (FMA.op & (1 << 17))
                        fprintf(fp, ")");
                break;
        case FMA_FMA16:
                if (FMA.op & (1 << 14))
                        fprintf(fp, "-");
                dump_src(fp, FMA.src0, regs, consts, true);
                dump_16swizzle(fp, (FMA.op >> 6) & 0x3);
                fprintf(fp, ", ");
                dump_src(fp, FMA.op & 0x7, regs, consts, true);
                dump_16swizzle(fp, (FMA.op >> 8) & 0x3);
                fprintf(fp, ", ");
                if (FMA.op & (1 << 15))
                        fprintf(fp, "-");
                dump_src(fp, (FMA.op >> 3) & 0x7, regs, consts, true);
                dump_16swizzle(fp, (FMA.op >> 16) & 0x3);
                break;
        case FMA_CSEL4: {
                struct bifrost_csel4 csel;
                memcpy(&csel, &FMA, sizeof(csel));
                fprintf(fp, ".%s ", bi_csel_cond_name(csel.cond));

                dump_src(fp, csel.src0, regs, consts, true);
                fprintf(fp, ", ");
                dump_src(fp, csel.src1, regs, consts, true);
                fprintf(fp, ", ");
                dump_src(fp, csel.src2, regs, consts, true);
                fprintf(fp, ", ");
                dump_src(fp, csel.src3, regs, consts, true);
                break;
        }
        case FMA_FMA_MSCALE:
                if (FMA.op & (1 << 12))
                        fprintf(fp, "abs(");
                dump_src(fp, FMA.src0, regs, consts, true);
                if (FMA.op & (1 << 12))
                        fprintf(fp, ")");
                fprintf(fp, ", ");
                if (FMA.op & (1 << 13))
                        fprintf(fp, "-");
                dump_src(fp, FMA.op & 0x7, regs, consts, true);
                fprintf(fp, ", ");
                if (FMA.op & (1 << 14))
                        fprintf(fp, "-");
                dump_src(fp, (FMA.op >> 3) & 0x7, regs, consts, true);
                fprintf(fp, ", ");
                dump_src(fp, (FMA.op >> 6) & 0x7, regs, consts, true);
                break;
        }
        fprintf(fp, "\n");
}

static const struct add_op_info add_op_infos[] = {
        { 0x00000, "MAX.f32", ADD_FMINMAX },
        { 0x02000, "MIN.f32", ADD_FMINMAX },
        { 0x04000, "ADD.f32", ADD_FADD },
        { 0x06000, "FCMP.GL", ADD_FCMP },
        { 0x07000, "FCMP.D3D", ADD_FCMP },
        { 0x07856, "F16_TO_I16", ADD_ONE_SRC },
        { 0x07857, "F16_TO_U16", ADD_ONE_SRC },
        { 0x078c0, "I16_TO_F16.XX", ADD_ONE_SRC },
        { 0x078c1, "U16_TO_F16.XX", ADD_ONE_SRC },
        { 0x078c8, "I16_TO_F16.YX", ADD_ONE_SRC },
        { 0x078c9, "U16_TO_F16.YX", ADD_ONE_SRC },
        { 0x078d0, "I16_TO_F16.XY", ADD_ONE_SRC },
        { 0x078d1, "U16_TO_F16.XY", ADD_ONE_SRC },
        { 0x078d8, "I16_TO_F16.YY", ADD_ONE_SRC },
        { 0x078d9, "U16_TO_F16.YY", ADD_ONE_SRC },
        { 0x07909, "B1_TO_F16", ADD_ONE_SRC },
        { 0x07936, "F32_TO_I32", ADD_ONE_SRC },
        { 0x07937, "F32_TO_U32", ADD_ONE_SRC },
        { 0x07971, "B1_TO_F32", ADD_ONE_SRC },
        { 0x07978, "I32_TO_F32", ADD_ONE_SRC },
        { 0x07979, "U32_TO_F32", ADD_ONE_SRC },
        { 0x07998, "I16_TO_I32.X", ADD_ONE_SRC },
        { 0x07999, "U16_TO_U32.X", ADD_ONE_SRC },
        { 0x0799a, "I16_TO_I32.Y", ADD_ONE_SRC },
        { 0x0799b, "U16_TO_U32.Y", ADD_ONE_SRC },
        { 0x0799c, "I16_TO_F32.X", ADD_ONE_SRC },
        { 0x0799d, "U16_TO_F32.X", ADD_ONE_SRC },
        { 0x0799e, "I16_TO_F32.Y", ADD_ONE_SRC },
        { 0x0799f, "U16_TO_F32.Y", ADD_ONE_SRC },
        { 0x079a2, "F16_TO_F32.X", ADD_ONE_SRC },
        { 0x079a3, "F16_TO_F32.Y", ADD_ONE_SRC },
        { 0x07b2b, "SWZ.YX.v2i16",  ADD_ONE_SRC },
        { 0x07b2c, "NOP",  ADD_ONE_SRC },
        { 0x07b29, "SWZ.XX.v2i16",  ADD_ONE_SRC },
        { 0x07b2d, "MOV",  ADD_ONE_SRC },
        { 0x07b2f, "SWZ.YY.v2i16",  ADD_ONE_SRC },
        { 0x07b65, "FRCP_FREXPM", ADD_ONE_SRC },
        { 0x07b75, "FSQRT_FREXPM", ADD_ONE_SRC },
        { 0x07b8d, "FRCP_FREXPE", ADD_ONE_SRC },
        { 0x07ba5, "FSQRT_FREXPE", ADD_ONE_SRC },
        { 0x07bad, "FRSQ_FREXPE", ADD_ONE_SRC },
        { 0x07bc5, "FLOG_FREXPE", ADD_ONE_SRC },
        { 0x07d42, "CEIL.v2f16", ADD_ONE_SRC },
        { 0x07d45, "CEIL.f32", ADD_ONE_SRC },
        { 0x07d82, "FLOOR.v2f16", ADD_ONE_SRC },
        { 0x07d85, "FLOOR.f32", ADD_ONE_SRC },
        { 0x07dc2, "TRUNC.v2f16", ADD_ONE_SRC },
        { 0x07dc5, "TRUNC.f32", ADD_ONE_SRC },
        { 0x07f18, "LSHIFT_ADD_HIGH32.i32", ADD_TWO_SRC },
        { 0x08000, "LD_ATTR", ADD_LOAD_ATTR, true },
        { 0x0a000, "LD_VAR.32", ADD_VARYING_INTERP, true },
        { 0x0b000, "TEX", ADD_TEX_COMPACT, true },
        { 0x0c188, "LOAD.i32", ADD_TWO_SRC, true },
        { 0x0c1a0, "LD_UBO.i32", ADD_TWO_SRC, true },
        { 0x0c1b8, "LD_SCRATCH.v2i32", ADD_TWO_SRC, true },
        { 0x0c1c8, "LOAD.v2i32", ADD_TWO_SRC, true },
        { 0x0c1e0, "LD_UBO.v2i32", ADD_TWO_SRC, true },
        { 0x0c1f8, "LD_SCRATCH.v2i32", ADD_TWO_SRC, true },
        { 0x0c208, "LOAD.v4i32", ADD_TWO_SRC, true },
        { 0x0c220, "LD_UBO.v4i32", ADD_TWO_SRC, true },
        { 0x0c238, "LD_SCRATCH.v4i32", ADD_TWO_SRC, true },
        { 0x0c248, "STORE.v4i32", ADD_TWO_SRC, true },
        { 0x0c278, "ST_SCRATCH.v4i32", ADD_TWO_SRC, true },
        { 0x0c588, "STORE.i32", ADD_TWO_SRC, true },
        { 0x0c5b8, "ST_SCRATCH.i32", ADD_TWO_SRC, true },
        { 0x0c5c8, "STORE.v2i32", ADD_TWO_SRC, true },
        { 0x0c5f8, "ST_SCRATCH.v2i32", ADD_TWO_SRC, true },
        { 0x0c648, "LOAD.u16", ADD_TWO_SRC, true }, // zero-extends
        { 0x0ca88, "LOAD.v3i32", ADD_TWO_SRC, true },
        { 0x0caa0, "LD_UBO.v3i32", ADD_TWO_SRC, true },
        { 0x0cab8, "LD_SCRATCH.v3i32", ADD_TWO_SRC, true },
        { 0x0cb88, "STORE.v3i32", ADD_TWO_SRC, true },
        { 0x0cbb8, "ST_SCRATCH.v3i32", ADD_TWO_SRC, true },
        { 0x0cc00, "FRCP_FAST.f32", ADD_ONE_SRC },
        { 0x0cc20, "FRSQ_FAST.f32", ADD_ONE_SRC },
        { 0x0cc68, "FLOG2_U.f32", ADD_ONE_SRC },
        { 0x0cd58, "FEXP2_FAST.f32", ADD_ONE_SRC },
        { 0x0ce00, "FRCP_TABLE", ADD_ONE_SRC },
        { 0x0ce10, "FRCP_FAST.f16.X", ADD_ONE_SRC },
        { 0x0ce20, "FRSQ_TABLE", ADD_ONE_SRC },
        { 0x0ce30, "FRCP_FAST.f16.Y", ADD_ONE_SRC },
        { 0x0ce50, "FRSQ_FAST.f16.X", ADD_ONE_SRC },
        { 0x0ce60, "FRCP_APPROX", ADD_ONE_SRC },
        { 0x0ce70, "FRSQ_FAST.f16.Y", ADD_ONE_SRC },
        { 0x0cf40, "ATAN_ASSIST", ADD_TWO_SRC },
        { 0x0cf48, "ATAN_TABLE", ADD_TWO_SRC },
        { 0x0cf50, "SIN_TABLE", ADD_ONE_SRC },
        { 0x0cf51, "COS_TABLE", ADD_ONE_SRC },
        { 0x0cf58, "EXP_TABLE", ADD_ONE_SRC },
        { 0x0cf60, "FLOG2_TABLE", ADD_ONE_SRC },
        { 0x0cf64, "FLOGE_TABLE", ADD_ONE_SRC },
        { 0x0d000, "BRANCH", ADD_BRANCH },
        { 0x0e8c0, "MUX", ADD_THREE_SRC },
        { 0x0e9b0, "ATAN_LDEXP.Y.f32", ADD_TWO_SRC },
        { 0x0e9b8, "ATAN_LDEXP.X.f32", ADD_TWO_SRC },
        { 0x0ea60, "SEL.XX.i16", ADD_TWO_SRC },
        { 0x0ea70, "SEL.XY.i16", ADD_TWO_SRC },
        { 0x0ea68, "SEL.YX.i16", ADD_TWO_SRC },
        { 0x0ea78, "SEL.YY.i16", ADD_TWO_SRC },
        { 0x0ec00, "F32_TO_F16", ADD_TWO_SRC },
        { 0x0e840, "CSEL.64",    ADD_THREE_SRC }, // u2u32(src2) ? src0 : src1
        { 0x0e940, "CSEL.8",    ADD_THREE_SRC }, // (src2 != 0) ? src0 : src1
        { 0x0f640, "ICMP.GL.GT", ADD_TWO_SRC }, // src0 > src1 ? 1 : 0
        { 0x0f648, "ICMP.GL.GE", ADD_TWO_SRC },
        { 0x0f650, "UCMP.GL.GT", ADD_TWO_SRC },
        { 0x0f658, "UCMP.GL.GE", ADD_TWO_SRC },
        { 0x0f660, "ICMP.GL.EQ", ADD_TWO_SRC },
        { 0x0f669, "ICMP.GL.NEQ", ADD_TWO_SRC },
        { 0x0f690, "UCMP.8.GT", ADD_TWO_SRC },
        { 0x0f698, "UCMP.8.GE", ADD_TWO_SRC },
        { 0x0f6a8, "ICMP.8.NE", ADD_TWO_SRC },
        { 0x0f6c0, "ICMP.D3D.GT", ADD_TWO_SRC }, // src0 > src1 ? ~0 : 0
        { 0x0f6c8, "ICMP.D3D.GE", ADD_TWO_SRC },
        { 0x0f6d0, "UCMP.D3D.GT", ADD_TWO_SRC },
        { 0x0f6d8, "UCMP.D3D.GE", ADD_TWO_SRC },
        { 0x0f6e0, "ICMP.D3D.EQ", ADD_TWO_SRC },
        { 0x0f700, "ICMP.64.GT.PT1", ADD_TWO_SRC },
        { 0x0f708, "ICMP.64.GE.PT1", ADD_TWO_SRC },
        { 0x0f710, "UCMP.64.GT.PT1", ADD_TWO_SRC },
        { 0x0f718, "UCMP.64.GE.PT1", ADD_TWO_SRC },
        { 0x0f720, "ICMP.64.EQ.PT1", ADD_TWO_SRC },
        { 0x0f728, "ICMP.64.NE.PT1", ADD_TWO_SRC },
        { 0x0f7c0, "ICMP.64.PT2", ADD_THREE_SRC }, // src3 = result of PT1
        { 0x10000, "MAX.v2f16", ADD_FMINMAX16 },
        { 0x11000, "ADD_MSCALE.f32", ADD_FADDMscale },
        { 0x12000, "MIN.v2f16", ADD_FMINMAX16 },
        { 0x14000, "ADD.v2f16", ADD_FADD16 },
        { 0x16000, "FCMP.GL", ADD_FCMP16 },
        { 0x17000, "FCMP.D3D", ADD_FCMP16 },
        { 0x17880, "ADD.v4i8", ADD_TWO_SRC },
        { 0x178c0, "ADD.i32",  ADD_TWO_SRC },
        { 0x17900, "ADD.v2i16", ADD_TWO_SRC },
        { 0x17ac0, "SUB.i32",  ADD_TWO_SRC },
        { 0x17c10, "ADDC.i32", ADD_TWO_SRC }, // adds src0 to the bottom bit of src1
        { 0x17d80, "ADD.i32.i16.X", ADD_TWO_SRC },
        { 0x17d90, "ADD.i32.u16.X", ADD_TWO_SRC },
        { 0x17dc0, "ADD.i32.i16.Y", ADD_TWO_SRC },
        { 0x17dd0, "ADD.i32.u16.Y", ADD_TWO_SRC },
        { 0x18000, "LD_VAR_ADDR", ADD_VARYING_ADDRESS, true },
        { 0x19181, "DISCARD.FEQ.f32", ADD_TWO_SRC, true },
        { 0x19189, "DISCARD.FNE.f32", ADD_TWO_SRC, true },
        { 0x1918C, "DISCARD.GL.f32", ADD_TWO_SRC, true }, /* Consumes ICMP.GL/etc with fixed 0 argument */
        { 0x19190, "DISCARD.FLE.f32", ADD_TWO_SRC, true },
        { 0x19198, "DISCARD.FLT.f32", ADD_TWO_SRC, true },
        { 0x191e8, "ATEST.f32", ADD_TWO_SRC, true },
        { 0x191f0, "ATEST.X.f16", ADD_TWO_SRC, true },
        { 0x191f8, "ATEST.Y.f16", ADD_TWO_SRC, true },
        { 0x19300, "ST_VAR.v1", ADD_THREE_SRC, true },
        { 0x19340, "ST_VAR.v2", ADD_THREE_SRC, true },
        { 0x19380, "ST_VAR.v3", ADD_THREE_SRC, true },
        { 0x193c0, "ST_VAR.v4", ADD_THREE_SRC, true },
        { 0x1952c, "BLEND", ADD_BLENDING, true },
        { 0x1a000, "LD_VAR.16", ADD_VARYING_INTERP, true },
        { 0x1ae60, "TEX", ADD_TEX, true },
        { 0x1b000, "TEX.f16", ADD_TEX_COMPACT, true },
        { 0x1c000, "RSHIFT_NAND.i32", ADD_SHIFT },
        { 0x1c400, "RSHIFT_AND.i32", ADD_SHIFT },
        { 0x1c800, "LSHIFT_NAND.i32", ADD_SHIFT },
        { 0x1cc00, "LSHIFT_AND.i32", ADD_SHIFT },
        { 0x1d000, "RSHIFT_XOR.i32", ADD_SHIFT },
        { 0x1d400, "LSHIFT_ADD.i32", ADD_SHIFT },
        { 0x1d800, "RSHIFT_SUB.i32", ADD_SHIFT },
        { 0x1dd18, "OR.i32",  ADD_TWO_SRC },
        { 0x1dd20, "AND.i32",  ADD_TWO_SRC },
        { 0x1dd60, "LSHIFT.i32", ADD_TWO_SRC },
        { 0x1dd50, "XOR.i32",  ADD_TWO_SRC },
        { 0x1dd80, "RSHIFT.i32", ADD_TWO_SRC },
        { 0x1dda0, "ARSHIFT.i32", ADD_TWO_SRC },
};

static struct add_op_info find_add_op_info(unsigned op)
{
        for (unsigned i = 0; i < ARRAY_SIZE(add_op_infos); i++) {
                unsigned opCmp = ~0;
                switch (add_op_infos[i].src_type) {
                case ADD_ONE_SRC:
                case ADD_BLENDING:
                        opCmp = op;
                        break;
                case ADD_TWO_SRC:
                        opCmp = op & ~0x7;
                        break;
                case ADD_THREE_SRC:
                        opCmp = op & ~0x3f;
                        break;
                case ADD_SHIFT:
                        opCmp = op & ~0x3ff;
                        break;
                case ADD_TEX:
                        opCmp = op & ~0xf;
                        break;
                case ADD_FADD:
                case ADD_FMINMAX:
                case ADD_FADD16:
                        opCmp = op & ~0x1fff;
                        break;
                case ADD_FMINMAX16:
                case ADD_FADDMscale:
                        opCmp = op & ~0xfff;
                        break;
                case ADD_FCMP:
                case ADD_FCMP16:
                        opCmp = op & ~0x7ff;
                        break;
                case ADD_TEX_COMPACT:
                        opCmp = op & ~0x3ff;
                        break;
                case ADD_VARYING_INTERP:
                        opCmp = op & ~0x7ff;
                        break;
                case ADD_VARYING_ADDRESS:
                        opCmp = op & ~0xfff;
                        break;
                case ADD_LOAD_ATTR:
                case ADD_BRANCH:
                        opCmp = op & ~0xfff;
                        break;
                default:
                        opCmp = ~0;
                        break;
                }
                if (add_op_infos[i].op == opCmp)
                        return add_op_infos[i];
        }

        struct add_op_info info;
        snprintf(info.name, sizeof(info.name), "op%04x", op);
        info.op = op;
        info.src_type = ADD_TWO_SRC;
        info.has_data_reg = true;
        return info;
}

static void dump_add(FILE *fp, uint64_t word, struct bifrost_regs regs,
                     struct bifrost_regs next_regs, uint64_t *consts,
                     unsigned data_reg, unsigned offset, bool verbose)
{
        if (verbose) {
                fprintf(fp, "# ADD: %016" PRIx64 "\n", word);
        }
        struct bifrost_add_inst ADD;
        memcpy((char *) &ADD, (char *) &word, sizeof(ADD));
        struct add_op_info info = find_add_op_info(ADD.op);

        fprintf(fp, "%s", info.name);

        // float16 seems like it doesn't support output modifiers
        if (info.src_type == ADD_FADD || info.src_type == ADD_FMINMAX) {
                // output modifiers
                fprintf(fp, "%s", bi_output_mod_name(bits(ADD.op, 8, 10)));
                if (info.src_type == ADD_FADD)
                        fprintf(fp, "%s", bi_round_mode_name(bits(ADD.op, 10, 12)));
                else
                        fprintf(fp, "%s", bi_minmax_mode_name(bits(ADD.op, 10, 12)));
        } else if (info.src_type == ADD_FCMP || info.src_type == ADD_FCMP16) {
                dump_fcmp(fp, bits(ADD.op, 3, 6));
                if (info.src_type == ADD_FCMP)
                        fprintf(fp, ".f32");
                else
                        fprintf(fp, ".v2f16");
        } else if (info.src_type == ADD_FADDMscale) {
                switch ((ADD.op >> 6) & 0x7) {
                case 0:
                        break;
                // causes GPU hangs on G71
                case 1:
                        fprintf(fp, ".invalid");
                        break;
                // Same as usual outmod value.
                case 2:
                        fprintf(fp, ".clamp_0_1");
                        break;
                // If src0 is infinite or NaN, flush it to zero so that the other
                // source is passed through unmodified.
                case 3:
                        fprintf(fp, ".flush_src0_inf_nan");
                        break;
                // Vice versa.
                case 4:
                        fprintf(fp, ".flush_src1_inf_nan");
                        break;
                // Every other case seems to behave the same as the above?
                default:
                        fprintf(fp, ".unk%d", (ADD.op >> 6) & 0x7);
                        break;
                }
        } else if (info.src_type == ADD_VARYING_INTERP) {
                if (ADD.op & 0x200)
                        fprintf(fp, ".reuse");
                if (ADD.op & 0x400)
                        fprintf(fp, ".flat");
                fprintf(fp, "%s", bi_interp_mode_name((ADD.op >> 7) & 0x3));
                fprintf(fp, ".v%d", ((ADD.op >> 5) & 0x3) + 1);
        } else if (info.src_type == ADD_BRANCH) {
                enum bifrost_branch_code branchCode = (enum bifrost_branch_code) ((ADD.op >> 6) & 0x3f);
                if (branchCode == BR_ALWAYS) {
                        // unconditional branch
                } else {
                        enum bifrost_branch_cond cond = (enum bifrost_branch_cond) ((ADD.op >> 6) & 0x7);
                        enum branch_bit_size size = (enum branch_bit_size) ((ADD.op >> 9) & 0x7);
                        bool portSwapped = (ADD.op & 0x7) < ADD.src0;
                        // See the comment in branch_bit_size
                        if (size == BR_SIZE_16YX0)
                                portSwapped = true;
                        if (size == BR_SIZE_16YX1)
                                portSwapped = false;
                        // These sizes are only for floating point comparisons, so the
                        // non-floating-point comparisons are reused to encode the flipped
                        // versions.
                        if (size == BR_SIZE_32_AND_16X || size == BR_SIZE_32_AND_16Y)
                                portSwapped = false;
                        // There's only one argument, so we reuse the extra argument to
                        // encode this.
                        if (size == BR_SIZE_ZERO)
                                portSwapped = !(ADD.op & 1);

                        switch (cond) {
                        case BR_COND_LT:
                                if (portSwapped)
                                        fprintf(fp, ".LT.u");
                                else
                                        fprintf(fp, ".LT.i");
                                break;
                        case BR_COND_LE:
                                if (size == BR_SIZE_32_AND_16X || size == BR_SIZE_32_AND_16Y) {
                                        fprintf(fp, ".UNE.f");
                                } else {
                                        if (portSwapped)
                                                fprintf(fp, ".LE.u");
                                        else
                                                fprintf(fp, ".LE.i");
                                }
                                break;
                        case BR_COND_GT:
                                if (portSwapped)
                                        fprintf(fp, ".GT.u");
                                else
                                        fprintf(fp, ".GT.i");
                                break;
                        case BR_COND_GE:
                                if (portSwapped)
                                        fprintf(fp, ".GE.u");
                                else
                                        fprintf(fp, ".GE.i");
                                break;
                        case BR_COND_EQ:
                                if (portSwapped)
                                        fprintf(fp, ".NE.i");
                                else
                                        fprintf(fp, ".EQ.i");
                                break;
                        case BR_COND_OEQ:
                                if (portSwapped)
                                        fprintf(fp, ".UNE.f");
                                else
                                        fprintf(fp, ".OEQ.f");
                                break;
                        case BR_COND_OGT:
                                if (portSwapped)
                                        fprintf(fp, ".OGT.unk.f");
                                else
                                        fprintf(fp, ".OGT.f");
                                break;
                        case BR_COND_OLT:
                                if (portSwapped)
                                        fprintf(fp, ".OLT.unk.f");
                                else
                                        fprintf(fp, ".OLT.f");
                                break;
                        }
                        switch (size) {
                        case BR_SIZE_32:
                        case BR_SIZE_32_AND_16X:
                        case BR_SIZE_32_AND_16Y:
                                fprintf(fp, "32");
                                break;
                        case BR_SIZE_16XX:
                        case BR_SIZE_16YY:
                        case BR_SIZE_16YX0:
                        case BR_SIZE_16YX1:
                                fprintf(fp, "16");
                                break;
                        case BR_SIZE_ZERO: {
                                unsigned ctrl = (ADD.op >> 1) & 0x3;
                                if (ctrl == 0)
                                        fprintf(fp, "32.Z");
                                else
                                        fprintf(fp, "16.Z");
                                break;
                        }
                        }
                }
        } else if (info.src_type == ADD_SHIFT) {
                struct bifrost_shift_add shift;
                memcpy(&shift, &ADD, sizeof(ADD));

                if (shift.invert_1)
                        fprintf(fp, ".invert_1");

                if (shift.invert_2)
                        fprintf(fp, ".invert_2");

                if (shift.zero)
                        fprintf(fp, ".unk%u", shift.zero);
        } else if (info.src_type == ADD_VARYING_ADDRESS) {
                struct bifrost_ld_var_addr ld;
                memcpy(&ld, &ADD, sizeof(ADD));
                fprintf(fp, ".%s", bi_ldst_type_name(ld.type));
        } else if (info.src_type == ADD_LOAD_ATTR) {
                struct bifrost_ld_attr ld;
                memcpy(&ld, &ADD, sizeof(ADD));

                if (ld.channels)
                        fprintf(fp, ".v%d%s", ld.channels + 1, bi_ldst_type_name(ld.type));
                else
                        fprintf(fp, ".%s", bi_ldst_type_name(ld.type));
        }

        fprintf(fp, " ");

        struct bifrost_reg_ctrl next_ctrl = DecodeRegCtrl(fp, next_regs);
        if (next_ctrl.add_write_unit != REG_WRITE_NONE) {
                fprintf(fp, "{R%d, T1}, ", GetRegToWrite(next_ctrl.add_write_unit, next_regs));
        } else {
                fprintf(fp, "T1, ");
        }

        switch (info.src_type) {
        case ADD_BLENDING:
                // Note: in this case, regs.uniform_const == location | 0x8
                // This probably means we can't load uniforms or immediates in the
                // same instruction. This re-uses the encoding that normally means
                // "disabled", where the low 4 bits are ignored. Perhaps the extra
                // 0x8 or'd in indicates this is happening.
                fprintf(fp, "location:%d, ", regs.uniform_const & 0x7);
        // fallthrough
        case ADD_ONE_SRC:
                dump_src(fp, ADD.src0, regs, consts, false);
                break;
        case ADD_TEX:
        case ADD_TEX_COMPACT: {
                int tex_index;
                int sampler_index;
                bool dualTex = false;

                fprintf(fp, "coords <");
                dump_src(fp, ADD.src0, regs, consts, false);
                fprintf(fp, ", ");
                dump_src(fp, ADD.op & 0x7, regs, consts, false);
                fprintf(fp, ">, ");

                if (info.src_type == ADD_TEX_COMPACT) {
                        tex_index = (ADD.op >> 3) & 0x7;
                        sampler_index = (ADD.op >> 7) & 0x7;
                        bool unknown = (ADD.op & 0x40);
                        // TODO: figure out if the unknown bit is ever 0
                        if (!unknown)
                                fprintf(fp, "unknown ");
                } else {
                        uint64_t constVal = get_const(consts, regs);
                        uint32_t controlBits = (ADD.op & 0x8) ? (constVal >> 32) : constVal;
                        struct bifrost_tex_ctrl ctrl;
                        memcpy((char *) &ctrl, (char *) &controlBits, sizeof(ctrl));

                        /* Dual-tex triggered for adjacent texturing
                         * instructions with the same coordinates to different
                         * textures/samplers. Observed for the compact
                         * (2D/normal) case. */

                        if ((ctrl.result_type & 7) == 1) {
                                bool f32 = ctrl.result_type & 8;

                                struct bifrost_dual_tex_ctrl dualCtrl;
                                memcpy((char *) &dualCtrl, (char *) &controlBits, sizeof(ctrl));
                                fprintf(fp, "(dualtex) tex0:%d samp0:%d tex1:%d samp1:%d %s",
                                       dualCtrl.tex_index0, dualCtrl.sampler_index0,
                                       dualCtrl.tex_index1, dualCtrl.sampler_index1,
                                       f32 ? "f32" : "f16");
                                if (dualCtrl.unk0 != 3)
                                        fprintf(fp, "unk:%d ", dualCtrl.unk0);
                                dualTex = true;
                        } else {
                                if (ctrl.no_merge_index) {
                                        tex_index = ctrl.tex_index;
                                        sampler_index = ctrl.sampler_index;
                                } else {
                                        tex_index = sampler_index = ctrl.tex_index;
                                        unsigned unk = ctrl.sampler_index >> 2;
                                        if (unk != 3)
                                                fprintf(fp, "unk:%d ", unk);
                                        if (ctrl.sampler_index & 1)
                                                tex_index = -1;
                                        if (ctrl.sampler_index & 2)
                                                sampler_index = -1;
                                }

                                if (ctrl.unk0 != 3)
                                        fprintf(fp, "unk0:%d ", ctrl.unk0);
                                if (ctrl.unk1)
                                        fprintf(fp, "unk1 ");
                                if (ctrl.unk2 != 0xf)
                                        fprintf(fp, "unk2:%x ", ctrl.unk2);

                                switch (ctrl.result_type) {
                                case 0x4:
                                        fprintf(fp, "f32 ");
                                        break;
                                case 0xe:
                                        fprintf(fp, "i32 ");
                                        break;
                                case 0xf:
                                        fprintf(fp, "u32 ");
                                        break;
                                default:
                                        fprintf(fp, "unktype(%x) ", ctrl.result_type);
                                }

                                switch (ctrl.tex_type) {
                                case 0:
                                        fprintf(fp, "cube ");
                                        break;
                                case 1:
                                        fprintf(fp, "buffer ");
                                        break;
                                case 2:
                                        fprintf(fp, "2D ");
                                        break;
                                case 3:
                                        fprintf(fp, "3D ");
                                        break;
                                }

                                if (ctrl.is_shadow)
                                        fprintf(fp, "shadow ");
                                if (ctrl.is_array)
                                        fprintf(fp, "array ");

                                if (!ctrl.filter) {
                                        if (ctrl.calc_gradients) {
                                                int comp = (controlBits >> 20) & 0x3;
                                                fprintf(fp, "txg comp:%d ", comp);
                                        } else {
                                                fprintf(fp, "txf ");
                                        }
                                } else {
                                        if (!ctrl.not_supply_lod) {
                                                if (ctrl.compute_lod)
                                                        fprintf(fp, "lod_bias ");
                                                else
                                                        fprintf(fp, "lod ");
                                        }

                                        if (!ctrl.calc_gradients)
                                                fprintf(fp, "grad ");
                                }

                                if (ctrl.texel_offset)
                                        fprintf(fp, "offset ");
                        }
                }

                if (!dualTex) {
                        if (tex_index == -1)
                                fprintf(fp, "tex:indirect ");
                        else
                                fprintf(fp, "tex:%d ", tex_index);

                        if (sampler_index == -1)
                                fprintf(fp, "samp:indirect ");
                        else
                                fprintf(fp, "samp:%d ", sampler_index);
                }
                break;
        }
        case ADD_VARYING_INTERP: {
                unsigned addr = ADD.op & 0x1f;
                if (addr < 0b10100) {
                        // direct addr
                        fprintf(fp, "%d", addr);
                } else if (addr < 0b11000) {
                        if (addr == 22)
                                fprintf(fp, "fragw");
                        else if (addr == 23)
                                fprintf(fp, "fragz");
                        else
                                fprintf(fp, "unk%d", addr);
                } else {
                        dump_src(fp, ADD.op & 0x7, regs, consts, false);
                }
                fprintf(fp, ", ");
                dump_src(fp, ADD.src0, regs, consts, false);
                break;
        }
        case ADD_VARYING_ADDRESS: {
                dump_src(fp, ADD.src0, regs, consts, false);
                fprintf(fp, ", ");
                dump_src(fp, ADD.op & 0x7, regs, consts, false);
                fprintf(fp, ", ");
                unsigned location = (ADD.op >> 3) & 0x1f;
                if (location < 16) {
                        fprintf(fp, "location:%d", location);
                } else if (location == 20) {
                        fprintf(fp, "location:%u", (uint32_t) get_const(consts, regs));
                } else if (location == 21) {
                        fprintf(fp, "location:%u", (uint32_t) (get_const(consts, regs) >> 32));
                } else {
                        fprintf(fp, "location:%d(unk)", location);
                }
                break;
        }
        case ADD_LOAD_ATTR:
                fprintf(fp, "location:%d, ", (ADD.op >> 3) & 0x1f);
        case ADD_TWO_SRC:
                dump_src(fp, ADD.src0, regs, consts, false);
                fprintf(fp, ", ");
                dump_src(fp, ADD.op & 0x7, regs, consts, false);
                break;
        case ADD_THREE_SRC:
                dump_src(fp, ADD.src0, regs, consts, false);
                fprintf(fp, ", ");
                dump_src(fp, ADD.op & 0x7, regs, consts, false);
                fprintf(fp, ", ");
                dump_src(fp, (ADD.op >> 3) & 0x7, regs, consts, false);
                break;
        case ADD_SHIFT: {
                struct bifrost_shift_add shift;
                memcpy(&shift, &ADD, sizeof(ADD));
                dump_src(fp, shift.src0, regs, consts, false);
                fprintf(fp, ", ");
                dump_src(fp, shift.src1, regs, consts, false);
                fprintf(fp, ", ");
                dump_src(fp, shift.src2, regs, consts, false);
                break;
        }
        case ADD_FADD:
        case ADD_FMINMAX:
                if (ADD.op & 0x10)
                        fprintf(fp, "-");
                if (ADD.op & 0x1000)
                        fprintf(fp, "abs(");
                dump_src(fp, ADD.src0, regs, consts, false);
                switch ((ADD.op >> 6) & 0x3) {
                case 3:
                        fprintf(fp, ".x");
                        break;
                default:
                        break;
                }
                if (ADD.op & 0x1000)
                        fprintf(fp, ")");
                fprintf(fp, ", ");
                if (ADD.op & 0x20)
                        fprintf(fp, "-");
                if (ADD.op & 0x8)
                        fprintf(fp, "abs(");
                dump_src(fp, ADD.op & 0x7, regs, consts, false);
                switch ((ADD.op >> 6) & 0x3) {
                case 1:
                case 3:
                        fprintf(fp, ".x");
                        break;
                case 2:
                        fprintf(fp, ".y");
                        break;
                case 0:
                        break;
                default:
                        fprintf(fp, ".unk");
                        break;
                }
                if (ADD.op & 0x8)
                        fprintf(fp, ")");
                break;
        case ADD_FADD16:
                if (ADD.op & 0x10)
                        fprintf(fp, "-");
                if (ADD.op & 0x1000)
                        fprintf(fp, "abs(");
                dump_src(fp, ADD.src0, regs, consts, false);
                if (ADD.op & 0x1000)
                        fprintf(fp, ")");
                dump_16swizzle(fp, (ADD.op >> 6) & 0x3);
                fprintf(fp, ", ");
                if (ADD.op & 0x20)
                        fprintf(fp, "-");
                if (ADD.op & 0x8)
                        fprintf(fp, "abs(");
                dump_src(fp, ADD.op & 0x7, regs, consts, false);
                dump_16swizzle(fp, (ADD.op >> 8) & 0x3);
                if (ADD.op & 0x8)
                        fprintf(fp, ")");
                break;
        case ADD_FMINMAX16: {
                bool abs1 = ADD.op & 0x8;
                bool abs2 = (ADD.op & 0x7) < ADD.src0;
                if (ADD.op & 0x10)
                        fprintf(fp, "-");
                if (abs1 || abs2)
                        fprintf(fp, "abs(");
                dump_src(fp, ADD.src0, regs, consts, false);
                dump_16swizzle(fp, (ADD.op >> 6) & 0x3);
                if (abs1 || abs2)
                        fprintf(fp, ")");
                fprintf(fp, ", ");
                if (ADD.op & 0x20)
                        fprintf(fp, "-");
                if (abs1 && abs2)
                        fprintf(fp, "abs(");
                dump_src(fp, ADD.op & 0x7, regs, consts, false);
                dump_16swizzle(fp, (ADD.op >> 8) & 0x3);
                if (abs1 && abs2)
                        fprintf(fp, ")");
                fprintf(fp, "/* %X */\n", (ADD.op >> 10) & 0x3); /* mode */
                break;
        }
        case ADD_FADDMscale: {
                if (ADD.op & 0x400)
                        fprintf(fp, "-");
                if (ADD.op & 0x200)
                        fprintf(fp, "abs(");
                dump_src(fp, ADD.src0, regs, consts, false);
                if (ADD.op & 0x200)
                        fprintf(fp, ")");

                fprintf(fp, ", ");

                if (ADD.op & 0x800)
                        fprintf(fp, "-");
                dump_src(fp, ADD.op & 0x7, regs, consts, false);

                fprintf(fp, ", ");

                dump_src(fp, (ADD.op >> 3) & 0x7, regs, consts, false);
                break;
        }
        case ADD_FCMP:
                if (ADD.op & 0x400) {
                        fprintf(fp, "-");
                }
                if (ADD.op & 0x100) {
                        fprintf(fp, "abs(");
                }
                dump_src(fp, ADD.src0, regs, consts, false);
                switch ((ADD.op >> 6) & 0x3) {
                case 3:
                        fprintf(fp, ".x");
                        break;
                default:
                        break;
                }
                if (ADD.op & 0x100) {
                        fprintf(fp, ")");
                }
                fprintf(fp, ", ");
                if (ADD.op & 0x200) {
                        fprintf(fp, "abs(");
                }
                dump_src(fp, ADD.op & 0x7, regs, consts, false);
                switch ((ADD.op >> 6) & 0x3) {
                case 1:
                case 3:
                        fprintf(fp, ".x");
                        break;
                case 2:
                        fprintf(fp, ".y");
                        break;
                case 0:
                        break;
                default:
                        fprintf(fp, ".unk");
                        break;
                }
                if (ADD.op & 0x200) {
                        fprintf(fp, ")");
                }
                break;
        case ADD_FCMP16:
                dump_src(fp, ADD.src0, regs, consts, false);
                dump_16swizzle(fp, (ADD.op >> 6) & 0x3);
                fprintf(fp, ", ");
                dump_src(fp, ADD.op & 0x7, regs, consts, false);
                dump_16swizzle(fp, (ADD.op >> 8) & 0x3);
                break;
        case ADD_BRANCH: {
                enum bifrost_branch_code code = (enum bifrost_branch_code) ((ADD.op >> 6) & 0x3f);
                enum branch_bit_size size = (enum branch_bit_size) ((ADD.op >> 9) & 0x7);
                if (code != BR_ALWAYS) {
                        dump_src(fp, ADD.src0, regs, consts, false);
                        switch (size) {
                        case BR_SIZE_16XX:
                                fprintf(fp, ".x");
                                break;
                        case BR_SIZE_16YY:
                        case BR_SIZE_16YX0:
                        case BR_SIZE_16YX1:
                                fprintf(fp, ".y");
                                break;
                        case BR_SIZE_ZERO: {
                                unsigned ctrl = (ADD.op >> 1) & 0x3;
                                switch (ctrl) {
                                case 1:
                                        fprintf(fp, ".y");
                                        break;
                                case 2:
                                        fprintf(fp, ".x");
                                        break;
                                default:
                                        break;
                                }
                        }
                        default:
                                break;
                        }
                        fprintf(fp, ", ");
                }
                if (code != BR_ALWAYS && size != BR_SIZE_ZERO) {
                        dump_src(fp, ADD.op & 0x7, regs, consts, false);
                        switch (size) {
                        case BR_SIZE_16XX:
                        case BR_SIZE_16YX0:
                        case BR_SIZE_16YX1:
                        case BR_SIZE_32_AND_16X:
                                fprintf(fp, ".x");
                                break;
                        case BR_SIZE_16YY:
                        case BR_SIZE_32_AND_16Y:
                                fprintf(fp, ".y");
                                break;
                        default:
                                break;
                        }
                        fprintf(fp, ", ");
                }
                // I haven't had the chance to test if this actually specifies the
                // branch offset, since I couldn't get it to produce values other
                // than 5 (uniform/const high), but these three bits are always
                // consistent across branch instructions, so it makes sense...
                int offsetSrc = (ADD.op >> 3) & 0x7;
                if (offsetSrc == 4 || offsetSrc == 5) {
                        // If the offset is known/constant, we can decode it
                        uint32_t raw_offset;
                        if (offsetSrc == 4)
                                raw_offset = get_const(consts, regs);
                        else
                                raw_offset = get_const(consts, regs) >> 32;
                        // The high 4 bits are flags, while the rest is the
                        // twos-complement offset in bytes (here we convert to
                        // clauses).
                        int32_t branch_offset = ((int32_t) raw_offset << 4) >> 8;

                        // If high4 is the high 4 bits of the last 64-bit constant,
                        // this is calculated as (high4 + 4) & 0xf, or 0 if the branch
                        // offset itself is the last constant. Not sure if this is
                        // actually used, or just garbage in unused bits, but in any
                        // case, we can just ignore it here since it's redundant. Note
                        // that if there is any padding, this will be 4 since the
                        // padding counts as the last constant.
                        unsigned flags = raw_offset >> 28;
                        (void) flags;

                        // Note: the offset is in bytes, relative to the beginning of the
                        // current clause, so a zero offset would be a loop back to the
                        // same clause (annoyingly different from Midgard).
                        fprintf(fp, "clause_%d", offset + branch_offset);
                } else {
                        dump_src(fp, offsetSrc, regs, consts, false);
                }
        }
        }
        if (info.has_data_reg) {
                fprintf(fp, ", R%d", data_reg);
        }
        fprintf(fp, "\n");
}

void dump_instr(FILE *fp, const struct bifrost_alu_inst *instr,
                struct bifrost_regs next_regs, uint64_t *consts,
                unsigned data_reg, unsigned offset, bool verbose)
{
        struct bifrost_regs regs;
        memcpy((char *) &regs, (char *) &instr->reg_bits, sizeof(regs));

        if (verbose) {
                fprintf(fp, "# regs: %016" PRIx64 "\n", instr->reg_bits);
                dump_regs(fp, regs);
        }
        dump_fma(fp, instr->fma_bits, regs, next_regs, consts, verbose);
        dump_add(fp, instr->add_bits, regs, next_regs, consts, data_reg, offset, verbose);
}

bool dump_clause(FILE *fp, uint32_t *words, unsigned *size, unsigned offset, bool verbose)
{
        // State for a decoded clause
        struct bifrost_alu_inst instrs[8] = {};
        uint64_t consts[6] = {};
        unsigned num_instrs = 0;
        unsigned num_consts = 0;
        uint64_t header_bits = 0;
        bool stopbit = false;

        unsigned i;
        for (i = 0; ; i++, words += 4) {
                if (verbose) {
                        fprintf(fp, "# ");
                        for (int j = 0; j < 4; j++)
                                fprintf(fp, "%08x ", words[3 - j]); // low bit on the right
                        fprintf(fp, "\n");
                }
                unsigned tag = bits(words[0], 0, 8);

                // speculatively decode some things that are common between many formats, so we can share some code
                struct bifrost_alu_inst main_instr = {};
                // 20 bits
                main_instr.add_bits = bits(words[2], 2, 32 - 13);
                // 23 bits
                main_instr.fma_bits = bits(words[1], 11, 32) | bits(words[2], 0, 2) << (32 - 11);
                // 35 bits
                main_instr.reg_bits = ((uint64_t) bits(words[1], 0, 11)) << 24 | (uint64_t) bits(words[0], 8, 32);

                uint64_t const0 = bits(words[0], 8, 32) << 4 | (uint64_t) words[1] << 28 | bits(words[2], 0, 4) << 60;
                uint64_t const1 = bits(words[2], 4, 32) << 4 | (uint64_t) words[3] << 32;

                bool stop = tag & 0x40;

                if (verbose) {
                        fprintf(fp, "# tag: 0x%02x\n", tag);
                }
                if (tag & 0x80) {
                        unsigned idx = stop ? 5 : 2;
                        main_instr.add_bits |= ((tag >> 3) & 0x7) << 17;
                        instrs[idx + 1] = main_instr;
                        instrs[idx].add_bits = bits(words[3], 0, 17) | ((tag & 0x7) << 17);
                        instrs[idx].fma_bits |= bits(words[2], 19, 32) << 10;
                        consts[0] = bits(words[3], 17, 32) << 4;
                } else {
                        bool done = false;
                        switch ((tag >> 3) & 0x7) {
                        case 0x0:
                                switch (tag & 0x7) {
                                case 0x3:
                                        main_instr.add_bits |= bits(words[3], 29, 32) << 17;
                                        instrs[1] = main_instr;
                                        num_instrs = 2;
                                        done = stop;
                                        break;
                                case 0x4:
                                        instrs[2].add_bits = bits(words[3], 0, 17) | bits(words[3], 29, 32) << 17;
                                        instrs[2].fma_bits |= bits(words[2], 19, 32) << 10;
                                        consts[0] = const0;
                                        num_instrs = 3;
                                        num_consts = 1;
                                        done = stop;
                                        break;
                                case 0x1:
                                case 0x5:
                                        instrs[2].add_bits = bits(words[3], 0, 17) | bits(words[3], 29, 32) << 17;
                                        instrs[2].fma_bits |= bits(words[2], 19, 32) << 10;
                                        main_instr.add_bits |= bits(words[3], 26, 29) << 17;
                                        instrs[3] = main_instr;
                                        if ((tag & 0x7) == 0x5) {
                                                num_instrs = 4;
                                                done = stop;
                                        }
                                        break;
                                case 0x6:
                                        instrs[5].add_bits = bits(words[3], 0, 17) | bits(words[3], 29, 32) << 17;
                                        instrs[5].fma_bits |= bits(words[2], 19, 32) << 10;
                                        consts[0] = const0;
                                        num_instrs = 6;
                                        num_consts = 1;
                                        done = stop;
                                        break;
                                case 0x7:
                                        instrs[5].add_bits = bits(words[3], 0, 17) | bits(words[3], 29, 32) << 17;
                                        instrs[5].fma_bits |= bits(words[2], 19, 32) << 10;
                                        main_instr.add_bits |= bits(words[3], 26, 29) << 17;
                                        instrs[6] = main_instr;
                                        num_instrs = 7;
                                        done = stop;
                                        break;
                                default:
                                        fprintf(fp, "unknown tag bits 0x%02x\n", tag);
                                }
                                break;
                        case 0x2:
                        case 0x3: {
                                unsigned idx = ((tag >> 3) & 0x7) == 2 ? 4 : 7;
                                main_instr.add_bits |= (tag & 0x7) << 17;
                                instrs[idx] = main_instr;
                                consts[0] |= (bits(words[2], 19, 32) | ((uint64_t) words[3] << 13)) << 19;
                                num_consts = 1;
                                num_instrs = idx + 1;
                                done = stop;
                                break;
                        }
                        case 0x4: {
                                unsigned idx = stop ? 4 : 1;
                                main_instr.add_bits |= (tag & 0x7) << 17;
                                instrs[idx] = main_instr;
                                instrs[idx + 1].fma_bits |= bits(words[3], 22, 32);
                                instrs[idx + 1].reg_bits = bits(words[2], 19, 32) | (bits(words[3], 0, 22) << (32 - 19));
                                break;
                        }
                        case 0x1:
                                // only constants can come after this
                                num_instrs = 1;
                                done = stop;
                        case 0x5:
                                header_bits = bits(words[2], 19, 32) | ((uint64_t) words[3] << (32 - 19));
                                main_instr.add_bits |= (tag & 0x7) << 17;
                                instrs[0] = main_instr;
                                break;
                        case 0x6:
                        case 0x7: {
                                unsigned pos = tag & 0xf;
                                // note that `pos' encodes both the total number of
                                // instructions and the position in the constant stream,
                                // presumably because decoded constants and instructions
                                // share a buffer in the decoder, but we only care about
                                // the position in the constant stream; the total number of
                                // instructions is redundant.
                                unsigned const_idx = 0;
                                switch (pos) {
                                case 0:
                                case 1:
                                case 2:
                                case 6:
                                        const_idx = 0;
                                        break;
                                case 3:
                                case 4:
                                case 7:
                                case 9:
                                        const_idx = 1;
                                        break;
                                case 5:
                                case 0xa:
                                        const_idx = 2;
                                        break;
                                case 8:
                                case 0xb:
                                case 0xc:
                                        const_idx = 3;
                                        break;
                                case 0xd:
                                        const_idx = 4;
                                        break;
                                default:
                                        fprintf(fp, "# unknown pos 0x%x\n", pos);
                                        break;
                                }

                                if (num_consts < const_idx + 2)
                                        num_consts = const_idx + 2;

                                consts[const_idx] = const0;
                                consts[const_idx + 1] = const1;
                                done = stop;
                                break;
                        }
                        default:
                                break;
                        }

                        if (done)
                                break;
                }
        }

        *size = i + 1;

        if (verbose) {
                fprintf(fp, "# header: %012" PRIx64 "\n", header_bits);
        }

        struct bifrost_header header;
        memcpy((char *) &header, (char *) &header_bits, sizeof(struct bifrost_header));
        dump_header(fp, header, verbose);
        if (!header.no_end_of_shader)
                stopbit = true;

        fprintf(fp, "{\n");
        for (i = 0; i < num_instrs; i++) {
                struct bifrost_regs next_regs;
                if (i + 1 == num_instrs) {
                        memcpy((char *) &next_regs, (char *) &instrs[0].reg_bits,
                               sizeof(next_regs));
                } else {
                        memcpy((char *) &next_regs, (char *) &instrs[i + 1].reg_bits,
                               sizeof(next_regs));
                }

                dump_instr(fp, &instrs[i], next_regs, consts, header.datareg, offset, verbose);
        }
        fprintf(fp, "}\n");

        if (verbose) {
                for (unsigned i = 0; i < num_consts; i++) {
                        fprintf(fp, "# const%d: %08" PRIx64 "\n", 2 * i, consts[i] & 0xffffffff);
                        fprintf(fp, "# const%d: %08" PRIx64 "\n", 2 * i + 1, consts[i] >> 32);
                }
        }
        return stopbit;
}

void disassemble_bifrost(FILE *fp, uint8_t *code, size_t size, bool verbose)
{
        uint32_t *words = (uint32_t *) code;
        uint32_t *words_end = words + (size / 4);
        // used for displaying branch targets
        unsigned offset = 0;
        while (words != words_end) {
                // we don't know what the program-end bit is quite yet, so for now just
                // assume that an all-0 quadword is padding
                uint32_t zero[4] = {};
                if (memcmp(words, zero, 4 * sizeof(uint32_t)) == 0)
                        break;
                fprintf(fp, "clause_%d:\n", offset);
                unsigned size;
                if (dump_clause(fp, words, &size, offset, verbose) == true) {
                        break;
                }
                words += size * 4;
                offset += size;
        }
}

