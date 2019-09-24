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
#include "bifrost_ops.h"
#include "disassemble.h"
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

struct bifrost_regs {
        unsigned uniform_const : 8;
        unsigned reg2 : 6;
        unsigned reg3 : 6;
        unsigned reg0 : 5;
        unsigned reg1 : 6;
        unsigned ctrl : 4;
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

enum bifrost_reg_write_unit {
        REG_WRITE_NONE = 0, // don't write
        REG_WRITE_TWO, // write using reg2
        REG_WRITE_THREE, // write using reg3
};

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
        FMA_FMA,
        FMA_FMA16,
        FMA_FOUR_SRC,
        FMA_FMA_MSCALE,
        FMA_SHIFT_ADD64,
};

struct fma_op_info {
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

struct bifrost_tex_ctrl {
        unsigned sampler_index : 4; // also used to signal indirects
        unsigned tex_index : 7;
        bool no_merge_index : 1; // whether to merge (direct) sampler & texture indices
        bool filter : 1; // use the usual filtering pipeline (0 for texelFetch & textureGather)
        unsigned unk0 : 2;
        bool texel_offset : 1; // *Offset()
        bool is_shadow : 1;
        bool is_array : 1;
        unsigned tex_type : 2; // 2D, 3D, Cube, Buffer
        bool compute_lod : 1; // 0 for *Lod()
        bool not_supply_lod : 1; // 0 for *Lod() or when a bias is applied
        bool calc_gradients : 1; // 0 for *Grad()
        unsigned unk1 : 1;
        unsigned result_type : 4; // integer, unsigned, float TODO: why is this 4 bits?
        unsigned unk2 : 4;
};

struct bifrost_dual_tex_ctrl {
        unsigned sampler_index0 : 2;
        unsigned unk0 : 2;
        unsigned tex_index0 : 2;
        unsigned sampler_index1 : 2;
        unsigned tex_index1 : 2;
        unsigned unk1 : 22;
};

enum branch_bit_size {
        BR_SIZE_32 = 0,
        BR_SIZE_16XX = 1,
        BR_SIZE_16YY = 2,
        // For the above combinations of bitsize and location, an extra bit is
        // encoded via comparing the sources. The only possible source of ambiguity
        // would be if the sources were the same, but then the branch condition
        // would be always true or always false anyways, so we can ignore it. But
        // this no longer works when comparing the y component to the x component,
        // since it's valid to compare the y component of a source against its own
        // x component. Instead, the extra bit is encoded via an extra bitsize.
        BR_SIZE_16YX0 = 3,
        BR_SIZE_16YX1 = 4,
        BR_SIZE_32_AND_16X = 5,
        BR_SIZE_32_AND_16Y = 6,
        // Used for comparisons with zero and always-true, see below. I think this
        // only works for integer comparisons.
        BR_SIZE_ZERO = 7,
};

void dump_header(struct bifrost_header header, bool verbose);
void dump_instr(const struct bifrost_alu_inst *instr, struct bifrost_regs next_regs, uint64_t *consts,
                unsigned data_reg, unsigned offset, bool verbose);
bool dump_clause(uint32_t *words, unsigned *size, unsigned offset, bool verbose);

void dump_header(struct bifrost_header header, bool verbose)
{
        if (header.clause_type != 0) {
                printf("id(%du) ", header.scoreboard_index);
        }

        if (header.scoreboard_deps != 0) {
                printf("next-wait(");
                bool first = true;
                for (unsigned i = 0; i < 8; i++) {
                        if (header.scoreboard_deps & (1 << i)) {
                                if (!first) {
                                        printf(", ");
                                }
                                printf("%d", i);
                                first = false;
                        }
                }
                printf(") ");
        }

        if (header.datareg_writebarrier)
                printf("data-reg-barrier ");

        if (!header.no_end_of_shader)
                printf("eos ");

        if (!header.back_to_back) {
                printf("nbb ");
                if (header.branch_cond)
                        printf("branch-cond ");
                else
                        printf("branch-uncond ");
        }

        if (header.elide_writes)
                printf("we ");

        if (header.suppress_inf)
                printf("suppress-inf ");
        if (header.suppress_nan)
                printf("suppress-nan ");

        if (header.unk0)
                printf("unk0 ");
        if (header.unk1)
                printf("unk1 ");
        if  (header.unk2)
                printf("unk2 ");
        if (header.unk3)
                printf("unk3 ");
        if (header.unk4)
                printf("unk4 ");

        printf("\n");

        if (verbose) {
                printf("# clause type %d, next clause type %d\n",
                       header.clause_type, header.next_clause_type);
        }
}

static struct bifrost_reg_ctrl DecodeRegCtrl(struct bifrost_regs regs)
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
                printf("# unknown reg ctrl %d\n", ctrl);
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

static void dump_regs(struct bifrost_regs srcs)
{
        struct bifrost_reg_ctrl ctrl = DecodeRegCtrl(srcs);
        printf("# ");
        if (ctrl.read_reg0)
                printf("port 0: R%d ", get_reg0(srcs));
        if (ctrl.read_reg1)
                printf("port 1: R%d ", get_reg1(srcs));

        if (ctrl.fma_write_unit == REG_WRITE_TWO)
                printf("port 2: R%d (write FMA) ", srcs.reg2);
        else if (ctrl.add_write_unit == REG_WRITE_TWO)
                printf("port 2: R%d (write ADD) ", srcs.reg2);

        if (ctrl.fma_write_unit == REG_WRITE_THREE)
                printf("port 3: R%d (write FMA) ", srcs.reg3);
        else if (ctrl.add_write_unit == REG_WRITE_THREE)
                printf("port 3: R%d (write ADD) ", srcs.reg3);
        else if (ctrl.read_reg3)
                printf("port 3: R%d (read) ", srcs.reg3);

        if (srcs.uniform_const) {
                if (srcs.uniform_const & 0x80) {
                        printf("uniform: U%d", (srcs.uniform_const & 0x7f) * 2);
                }
        }

        printf("\n");
}
static void dump_const_imm(uint32_t imm)
{
        union {
                float f;
                uint32_t i;
        } fi;
        fi.i = imm;
        printf("0x%08x /* %f */", imm, fi.f);
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

static void dump_uniform_const_src(struct bifrost_regs srcs, uint64_t *consts, bool high32)
{
        if (srcs.uniform_const & 0x80) {
                unsigned uniform = (srcs.uniform_const & 0x7f) * 2;
                printf("U%d", uniform + (high32 ? 1 : 0));
        } else if (srcs.uniform_const >= 0x20) {
                uint64_t imm = get_const(consts, srcs);
                if (high32)
                        dump_const_imm(imm >> 32);
                else
                        dump_const_imm(imm);
        } else {
                switch (srcs.uniform_const) {
                case 0:
                        printf("0");
                        break;
                case 5:
                        printf("atest-data");
                        break;
                case 6:
                        printf("sample-ptr");
                        break;
                case 8:
                case 9:
                case 10:
                case 11:
                case 12:
                case 13:
                case 14:
                case 15:
                        printf("blend-descriptor%u", (unsigned) srcs.uniform_const - 8);
                        break;
                default:
                        printf("unkConst%u", (unsigned) srcs.uniform_const);
                        break;
                }

                if (high32)
                        printf(".y");
                else
                        printf(".x");
        }
}

static void dump_src(unsigned src, struct bifrost_regs srcs, uint64_t *consts, bool isFMA)
{
        switch (src) {
        case 0:
                printf("R%d", get_reg0(srcs));
                break;
        case 1:
                printf("R%d", get_reg1(srcs));
                break;
        case 2:
                printf("R%d", srcs.reg3);
                break;
        case 3:
                if (isFMA)
                        printf("0");
                else
                        printf("T"); // i.e. the output of FMA this cycle
                break;
        case 4:
                dump_uniform_const_src(srcs, consts, false);
                break;
        case 5:
                dump_uniform_const_src(srcs, consts, true);
                break;
        case 6:
                printf("T0");
                break;
        case 7:
                printf("T1");
                break;
        }
}

static void dump_output_mod(unsigned mod)
{
        switch (mod) {
        case 0:
                break;
        case 1:
                printf(".clamp_0_inf");
                break; // max(out, 0)
        case 2:
                printf(".clamp_m1_1");
                break; // clamp(out, -1, 1)
        case 3:
                printf(".clamp_0_1");
                break; // clamp(out, 0, 1)
        default:
                break;
        }
}

static void dump_minmax_mode(unsigned mod)
{
        switch (mod) {
        case 0:
                /* Same as fmax() and fmin() -- return the other number if any
                 * number is NaN.  Also always return +0 if one argument is +0 and
                 * the other is -0.
                 */
                break;
        case 1:
                /* Instead of never returning a NaN, always return one. The
                 * "greater"/"lesser" NaN is always returned, first by checking the
                 * sign and then the mantissa bits.
                 */
                printf(".nan_wins");
                break;
        case 2:
                /* For max, implement src0 > src1 ? src0 : src1
                 * For min, implement src0 < src1 ? src0 : src1
                 *
                 * This includes handling NaN's and signedness of 0 differently
                 * from above, since +0 and -0 compare equal and comparisons always
                 * return false for NaN's. As a result, this mode is *not*
                 * commutative.
                 */
                printf(".src1_wins");
                break;
        case 3:
                /* For max, implement src0 < src1 ? src1 : src0
                 * For min, implement src0 > src1 ? src1 : src0
                 */
                printf(".src0_wins");
                break;
        default:
                break;
        }
}

static void dump_round_mode(unsigned mod)
{
        switch (mod) {
        case 0:
                /* roundTiesToEven, the IEEE default. */
                break;
        case 1:
                /* roundTowardPositive in the IEEE spec. */
                printf(".round_pos");
                break;
        case 2:
                /* roundTowardNegative in the IEEE spec. */
                printf(".round_neg");
                break;
        case 3:
                /* roundTowardZero in the IEEE spec. */
                printf(".round_zero");
                break;
        default:
                break;
        }
}

static const struct fma_op_info FMAOpInfos[] = {
        { 0x00000, "FMA.f32",  FMA_FMA },
        { 0x40000, "MAX.f32", FMA_FMINMAX },
        { 0x44000, "MIN.f32", FMA_FMINMAX },
        { 0x48000, "FCMP.GL", FMA_FCMP },
        { 0x4c000, "FCMP.D3D", FMA_FCMP },
        { 0x4ff98, "ADD.i32", FMA_TWO_SRC },
        { 0x4ffd8, "SUB.i32", FMA_TWO_SRC },
        { 0x4fff0, "SUBB.i32", FMA_TWO_SRC },
        { 0x50000, "FMA_MSCALE", FMA_FMA_MSCALE },
        { 0x58000, "ADD.f32", FMA_FADD },
        { 0x5c000, "CSEL.FEQ.f32", FMA_FOUR_SRC },
        { 0x5c200, "CSEL.FGT.f32", FMA_FOUR_SRC },
        { 0x5c400, "CSEL.FGE.f32", FMA_FOUR_SRC },
        { 0x5c600, "CSEL.IEQ.f32", FMA_FOUR_SRC },
        { 0x5c800, "CSEL.IGT.i32", FMA_FOUR_SRC },
        { 0x5ca00, "CSEL.IGE.i32", FMA_FOUR_SRC },
        { 0x5cc00, "CSEL.UGT.i32", FMA_FOUR_SRC },
        { 0x5ce00, "CSEL.UGE.i32", FMA_FOUR_SRC },
        { 0x5d8d0, "ICMP.D3D.GT.v2i16", FMA_TWO_SRC },
        { 0x5d9d0, "UCMP.D3D.GT.v2i16", FMA_TWO_SRC },
        { 0x5dad0, "ICMP.D3D.GE.v2i16", FMA_TWO_SRC },
        { 0x5dbd0, "UCMP.D3D.GE.v2i16", FMA_TWO_SRC },
        { 0x5dcd0, "ICMP.D3D.EQ.v2i16", FMA_TWO_SRC },
        { 0x5de40, "ICMP.GL.GT.i32", FMA_TWO_SRC }, // src0 > src1 ? 1 : 0
        { 0x5de48, "ICMP.GL.GE.i32", FMA_TWO_SRC },
        { 0x5de50, "UCMP.GL.GT.i32", FMA_TWO_SRC },
        { 0x5de58, "UCMP.GL.GE.i32", FMA_TWO_SRC },
        { 0x5de60, "ICMP.GL.EQ.i32", FMA_TWO_SRC },
        { 0x5dec0, "ICMP.D3D.GT.i32", FMA_TWO_SRC }, // src0 > src1 ? ~0 : 0
        { 0x5dec8, "ICMP.D3D.GE.i32", FMA_TWO_SRC },
        { 0x5ded0, "UCMP.D3D.GT.i32", FMA_TWO_SRC },
        { 0x5ded8, "UCMP.D3D.GE.i32", FMA_TWO_SRC },
        { 0x5dee0, "ICMP.D3D.EQ.i32", FMA_TWO_SRC },
        { 0x60200, "RSHIFT_NAND.i32", FMA_THREE_SRC },
        { 0x603c0, "RSHIFT_NAND.v2i16", FMA_THREE_SRC },
        { 0x60e00, "RSHIFT_OR.i32", FMA_THREE_SRC },
        { 0x60fc0, "RSHIFT_OR.v2i16", FMA_THREE_SRC },
        { 0x61200, "RSHIFT_AND.i32", FMA_THREE_SRC },
        { 0x613c0, "RSHIFT_AND.v2i16", FMA_THREE_SRC },
        { 0x61e00, "RSHIFT_NOR.i32", FMA_THREE_SRC }, // ~((src0 << src2) | src1)
        { 0x61fc0, "RSHIFT_NOR.v2i16", FMA_THREE_SRC }, // ~((src0 << src2) | src1)
        { 0x62200, "LSHIFT_NAND.i32", FMA_THREE_SRC },
        { 0x623c0, "LSHIFT_NAND.v2i16", FMA_THREE_SRC },
        { 0x62e00, "LSHIFT_OR.i32",  FMA_THREE_SRC }, // (src0 << src2) | src1
        { 0x62fc0, "LSHIFT_OR.v2i16",  FMA_THREE_SRC }, // (src0 << src2) | src1
        { 0x63200, "LSHIFT_AND.i32", FMA_THREE_SRC }, // (src0 << src2) & src1
        { 0x633c0, "LSHIFT_AND.v2i16", FMA_THREE_SRC },
        { 0x63e00, "LSHIFT_NOR.i32", FMA_THREE_SRC },
        { 0x63fc0, "LSHIFT_NOR.v2i16", FMA_THREE_SRC },
        { 0x64200, "RSHIFT_XOR.i32", FMA_THREE_SRC },
        { 0x643c0, "RSHIFT_XOR.v2i16", FMA_THREE_SRC },
        { 0x64600, "RSHIFT_XNOR.i32", FMA_THREE_SRC }, // ~((src0 >> src2) ^ src1)
        { 0x647c0, "RSHIFT_XNOR.v2i16", FMA_THREE_SRC }, // ~((src0 >> src2) ^ src1)
        { 0x64a00, "LSHIFT_XOR.i32", FMA_THREE_SRC },
        { 0x64bc0, "LSHIFT_XOR.v2i16", FMA_THREE_SRC },
        { 0x64e00, "LSHIFT_XNOR.i32", FMA_THREE_SRC }, // ~((src0 >> src2) ^ src1)
        { 0x64fc0, "LSHIFT_XNOR.v2i16", FMA_THREE_SRC }, // ~((src0 >> src2) ^ src1)
        { 0x65200, "LSHIFT_ADD.i32", FMA_THREE_SRC },
        { 0x65600, "LSHIFT_SUB.i32", FMA_THREE_SRC }, // (src0 << src2) - src1
        { 0x65a00, "LSHIFT_RSUB.i32", FMA_THREE_SRC }, // src1 - (src0 << src2)
        { 0x65e00, "RSHIFT_ADD.i32", FMA_THREE_SRC },
        { 0x66200, "RSHIFT_SUB.i32", FMA_THREE_SRC },
        { 0x66600, "RSHIFT_RSUB.i32", FMA_THREE_SRC },
        { 0x66a00, "ARSHIFT_ADD.i32", FMA_THREE_SRC },
        { 0x66e00, "ARSHIFT_SUB.i32", FMA_THREE_SRC },
        { 0x67200, "ARSHIFT_RSUB.i32", FMA_THREE_SRC },
        { 0x80000, "FMA.v2f16",  FMA_FMA16 },
        { 0xc0000, "MAX.v2f16", FMA_FMINMAX16 },
        { 0xc4000, "MIN.v2f16", FMA_FMINMAX16 },
        { 0xc8000, "FCMP.GL", FMA_FCMP16 },
        { 0xcc000, "FCMP.D3D", FMA_FCMP16 },
        { 0xcf900, "ADD.v2i16", FMA_TWO_SRC },
        { 0xcfc10, "ADDC.i32", FMA_TWO_SRC },
        { 0xcfd80, "ADD.i32.i16.X", FMA_TWO_SRC },
        { 0xcfd90, "ADD.i32.u16.X", FMA_TWO_SRC },
        { 0xcfdc0, "ADD.i32.i16.Y", FMA_TWO_SRC },
        { 0xcfdd0, "ADD.i32.u16.Y", FMA_TWO_SRC },
        { 0xd8000, "ADD.v2f16", FMA_FADD16 },
        { 0xdc000, "CSEL.FEQ.v2f16", FMA_FOUR_SRC },
        { 0xdc200, "CSEL.FGT.v2f16", FMA_FOUR_SRC },
        { 0xdc400, "CSEL.FGE.v2f16", FMA_FOUR_SRC },
        { 0xdc600, "CSEL.IEQ.v2f16", FMA_FOUR_SRC },
        { 0xdc800, "CSEL.IGT.v2i16", FMA_FOUR_SRC },
        { 0xdca00, "CSEL.IGE.v2i16", FMA_FOUR_SRC },
        { 0xdcc00, "CSEL.UGT.v2i16", FMA_FOUR_SRC },
        { 0xdce00, "CSEL.UGE.v2i16", FMA_FOUR_SRC },
        { 0xdd000, "F32_TO_F16", FMA_TWO_SRC },
        { 0xe0046, "F16_TO_I16.XX", FMA_ONE_SRC },
        { 0xe0047, "F16_TO_U16.XX", FMA_ONE_SRC },
        { 0xe004e, "F16_TO_I16.YX", FMA_ONE_SRC },
        { 0xe004f, "F16_TO_U16.YX", FMA_ONE_SRC },
        { 0xe0056, "F16_TO_I16.XY", FMA_ONE_SRC },
        { 0xe0057, "F16_TO_U16.XY", FMA_ONE_SRC },
        { 0xe005e, "F16_TO_I16.YY", FMA_ONE_SRC },
        { 0xe005f, "F16_TO_U16.YY", FMA_ONE_SRC },
        { 0xe00c0, "I16_TO_F16.XX", FMA_ONE_SRC },
        { 0xe00c1, "U16_TO_F16.XX", FMA_ONE_SRC },
        { 0xe00c8, "I16_TO_F16.YX", FMA_ONE_SRC },
        { 0xe00c9, "U16_TO_F16.YX", FMA_ONE_SRC },
        { 0xe00d0, "I16_TO_F16.XY", FMA_ONE_SRC },
        { 0xe00d1, "U16_TO_F16.XY", FMA_ONE_SRC },
        { 0xe00d8, "I16_TO_F16.YY", FMA_ONE_SRC },
        { 0xe00d9, "U16_TO_F16.YY", FMA_ONE_SRC },
        { 0xe0136, "F32_TO_I32", FMA_ONE_SRC },
        { 0xe0137, "F32_TO_U32", FMA_ONE_SRC },
        { 0xe0178, "I32_TO_F32", FMA_ONE_SRC },
        { 0xe0179, "U32_TO_F32", FMA_ONE_SRC },
        { 0xe0198, "I16_TO_I32.X", FMA_ONE_SRC },
        { 0xe0199, "U16_TO_U32.X", FMA_ONE_SRC },
        { 0xe019a, "I16_TO_I32.Y", FMA_ONE_SRC },
        { 0xe019b, "U16_TO_U32.Y", FMA_ONE_SRC },
        { 0xe019c, "I16_TO_F32.X", FMA_ONE_SRC },
        { 0xe019d, "U16_TO_F32.X", FMA_ONE_SRC },
        { 0xe019e, "I16_TO_F32.Y", FMA_ONE_SRC },
        { 0xe019f, "U16_TO_F32.Y", FMA_ONE_SRC },
        { 0xe01a2, "F16_TO_F32.X", FMA_ONE_SRC },
        { 0xe01a3, "F16_TO_F32.Y", FMA_ONE_SRC },
        { 0xe032c, "NOP",  FMA_ONE_SRC },
        { 0xe032d, "MOV",  FMA_ONE_SRC },
        { 0xe032f, "SWZ.YY.v2i16",  FMA_ONE_SRC },
        // From the ARM patent US20160364209A1:
        // "Decompose v (the input) into numbers x1 and s such that v = x1 * 2^s,
        // and x1 is a floating point value in a predetermined range where the
        // value 1 is within the range and not at one extremity of the range (e.g.
        // choose a range where 1 is towards middle of range)."
        //
        // This computes x1.
        { 0xe0345, "LOG_FREXPM", FMA_ONE_SRC },
        // Given a floating point number m * 2^e, returns m * 2^{-1}. This is
        // exactly the same as the mantissa part of frexp().
        { 0xe0365, "FRCP_FREXPM", FMA_ONE_SRC },
        // Given a floating point number m * 2^e, returns m * 2^{-2} if e is even,
        // and m * 2^{-1} if e is odd. In other words, scales by powers of 4 until
        // within the range [0.25, 1). Used for square-root and reciprocal
        // square-root.
        { 0xe0375, "FSQRT_FREXPM", FMA_ONE_SRC },
        // Given a floating point number m * 2^e, computes -e - 1 as an integer.
        // Zero and infinity/NaN return 0.
        { 0xe038d, "FRCP_FREXPE", FMA_ONE_SRC },
        // Computes floor(e/2) + 1.
        { 0xe03a5, "FSQRT_FREXPE", FMA_ONE_SRC },
        // Given a floating point number m * 2^e, computes -floor(e/2) - 1 as an
        // integer.
        { 0xe03ad, "FRSQ_FREXPE", FMA_ONE_SRC },
        { 0xe03c5, "LOG_FREXPE", FMA_ONE_SRC },
        { 0xe03fa, "CLZ", FMA_ONE_SRC },
        { 0xe0b80, "IMAX3", FMA_THREE_SRC },
        { 0xe0bc0, "UMAX3", FMA_THREE_SRC },
        { 0xe0c00, "IMIN3", FMA_THREE_SRC },
        { 0xe0c40, "UMIN3", FMA_THREE_SRC },
        { 0xe0ec5, "ROUND", FMA_ONE_SRC },
        { 0xe0f40, "CSEL", FMA_THREE_SRC }, // src2 != 0 ? src1 : src0
        { 0xe0fc0, "MUX.i32", FMA_THREE_SRC }, // see ADD comment
        { 0xe1805, "ROUNDEVEN", FMA_ONE_SRC },
        { 0xe1845, "CEIL", FMA_ONE_SRC },
        { 0xe1885, "FLOOR", FMA_ONE_SRC },
        { 0xe18c5, "TRUNC", FMA_ONE_SRC },
        { 0xe19b0, "ATAN_LDEXP.Y.f32", FMA_TWO_SRC },
        { 0xe19b8, "ATAN_LDEXP.X.f32", FMA_TWO_SRC },
        // These instructions in the FMA slot, together with LSHIFT_ADD_HIGH32.i32
        // in the ADD slot, allow one to do a 64-bit addition with an extra small
        // shift on one of the sources. There are three possible scenarios:
        //
        // 1) Full 64-bit addition. Do:
        // out.x = LSHIFT_ADD_LOW32.i64 src1.x, src2.x, shift
        // out.y = LSHIFT_ADD_HIGH32.i32 src1.y, src2.y
        //
        // The shift amount is applied to src2 before adding. The shift amount, and
        // any extra bits from src2 plus the overflow bit, are sent directly from
        // FMA to ADD instead of being passed explicitly. Hence, these two must be
        // bundled together into the same instruction.
        //
        // 2) Add a 64-bit value src1 to a zero-extended 32-bit value src2. Do:
        // out.x = LSHIFT_ADD_LOW32.u32 src1.x, src2, shift
        // out.y = LSHIFT_ADD_HIGH32.i32 src1.x, 0
        //
        // Note that in this case, the second argument to LSHIFT_ADD_HIGH32 is
        // ignored, so it can actually be anything. As before, the shift is applied
        // to src2 before adding.
        //
        // 3) Add a 64-bit value to a sign-extended 32-bit value src2. Do:
        // out.x = LSHIFT_ADD_LOW32.i32 src1.x, src2, shift
        // out.y = LSHIFT_ADD_HIGH32.i32 src1.x, 0
        //
        // The only difference is the .i32 instead of .u32. Otherwise, this is
        // exactly the same as before.
        //
        // In all these instructions, the shift amount is stored where the third
        // source would be, so the shift has to be a small immediate from 0 to 7.
        // This is fine for the expected use-case of these instructions, which is
        // manipulating 64-bit pointers.
        //
        // These instructions can also be combined with various load/store
        // instructions which normally take a 64-bit pointer in order to add a
        // 32-bit or 64-bit offset to the pointer before doing the operation,
        // optionally shifting the offset. The load/store op implicity does
        // LSHIFT_ADD_HIGH32.i32 internally. Letting ptr be the pointer, and offset
        // the desired offset, the cases go as follows:
        //
        // 1) Add a 64-bit offset:
        // LSHIFT_ADD_LOW32.i64 ptr.x, offset.x, shift
        // ld_st_op ptr.y, offset.y, ...
        //
        // Note that the output of LSHIFT_ADD_LOW32.i64 is not used, instead being
        // implicitly sent to the load/store op to serve as the low 32 bits of the
        // pointer.
        //
        // 2) Add a 32-bit unsigned offset:
        // temp = LSHIFT_ADD_LOW32.u32 ptr.x, offset, shift
        // ld_st_op temp, ptr.y, ...
        //
        // Now, the low 32 bits of offset << shift + ptr are passed explicitly to
        // the ld_st_op, to match the case where there is no offset and ld_st_op is
        // called directly.
        //
        // 3) Add a 32-bit signed offset:
        // temp = LSHIFT_ADD_LOW32.i32 ptr.x, offset, shift
        // ld_st_op temp, ptr.y, ...
        //
        // Again, the same as the unsigned case except for the offset.
        { 0xe1c80, "LSHIFT_ADD_LOW32.u32", FMA_SHIFT_ADD64 },
        { 0xe1cc0, "LSHIFT_ADD_LOW32.i64", FMA_SHIFT_ADD64 },
        { 0xe1d80, "LSHIFT_ADD_LOW32.i32", FMA_SHIFT_ADD64 },
        { 0xe1e00, "SEL.XX.i16", FMA_TWO_SRC },
        { 0xe1e08, "SEL.YX.i16", FMA_TWO_SRC },
        { 0xe1e10, "SEL.XY.i16", FMA_TWO_SRC },
        { 0xe1e18, "SEL.YY.i16", FMA_TWO_SRC },
        { 0xe7800, "IMAD", FMA_THREE_SRC },
        { 0xe78db, "POPCNT", FMA_ONE_SRC },
};

static struct fma_op_info find_fma_op_info(unsigned op)
{
        for (unsigned i = 0; i < ARRAY_SIZE(FMAOpInfos); i++) {
                unsigned opCmp = ~0;
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
                case FMA_FOUR_SRC:
                        opCmp = op & ~0x1ff;
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

static void dump_fcmp(unsigned op)
{
        switch (op) {
        case 0:
                printf(".OEQ");
                break;
        case 1:
                printf(".OGT");
                break;
        case 2:
                printf(".OGE");
                break;
        case 3:
                printf(".UNE");
                break;
        case 4:
                printf(".OLT");
                break;
        case 5:
                printf(".OLE");
                break;
        default:
                printf(".unk%d", op);
                break;
        }
}

static void dump_16swizzle(unsigned swiz)
{
        if (swiz == 2)
                return;
        printf(".%c%c", "xy"[swiz & 1], "xy"[(swiz >> 1) & 1]);
}

static void dump_fma_expand_src0(unsigned ctrl)
{
        switch (ctrl) {
        case 3:
        case 4:
        case 6:
                printf(".x");
                break;
        case 5:
        case 7:
                printf(".y");
                break;
        case 0:
        case 1:
        case 2:
                break;
        default:
                printf(".unk");
                break;
        }
}

static void dump_fma_expand_src1(unsigned ctrl)
{
        switch (ctrl) {
        case 1:
        case 3:
                printf(".x");
                break;
        case 2:
        case 4:
        case 5:
                printf(".y");
                break;
        case 0:
        case 6:
        case 7:
                break;
        default:
                printf(".unk");
                break;
        }
}

static void dump_fma(uint64_t word, struct bifrost_regs regs, struct bifrost_regs next_regs, uint64_t *consts, bool verbose)
{
        if (verbose) {
                printf("# FMA: %016" PRIx64 "\n", word);
        }
        struct bifrost_fma_inst FMA;
        memcpy((char *) &FMA, (char *) &word, sizeof(struct bifrost_fma_inst));
        struct fma_op_info info = find_fma_op_info(FMA.op);

        printf("%s", info.name);
        if (info.src_type == FMA_FADD ||
            info.src_type == FMA_FMINMAX ||
            info.src_type == FMA_FMA ||
            info.src_type == FMA_FADD16 ||
            info.src_type == FMA_FMINMAX16 ||
            info.src_type == FMA_FMA16) {
                dump_output_mod(bits(FMA.op, 12, 14));
                switch (info.src_type) {
                case FMA_FADD:
                case FMA_FMA:
                case FMA_FADD16:
                case FMA_FMA16:
                        dump_round_mode(bits(FMA.op, 10, 12));
                        break;
                case FMA_FMINMAX:
                case FMA_FMINMAX16:
                        dump_minmax_mode(bits(FMA.op, 10, 12));
                        break;
                default:
                        assert(0);
                }
        } else if (info.src_type == FMA_FCMP || info.src_type == FMA_FCMP16) {
                dump_fcmp(bits(FMA.op, 10, 13));
                if (info.src_type == FMA_FCMP)
                        printf(".f32");
                else
                        printf(".v2f16");
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
                                printf(".rcp_mode");
                                break;
                        case 3:
                                /* Similar to the above, but src0 always wins when multiplying
                                 * 0 by infinity.
                                 */
                                printf(".sqrt_mode");
                                break;
                        default:
                                printf(".unk%d_mode", (int) (FMA.op >> 9) & 0x3);
                        }
                } else {
                        dump_output_mod(bits(FMA.op, 9, 11));
                }
        }

        printf(" ");

        struct bifrost_reg_ctrl next_ctrl = DecodeRegCtrl(next_regs);
        if (next_ctrl.fma_write_unit != REG_WRITE_NONE) {
                printf("{R%d, T0}, ", GetRegToWrite(next_ctrl.fma_write_unit, next_regs));
        } else {
                printf("T0, ");
        }

        switch (info.src_type) {
        case FMA_ONE_SRC:
                dump_src(FMA.src0, regs, consts, true);
                break;
        case FMA_TWO_SRC:
                dump_src(FMA.src0, regs, consts, true);
                printf(", ");
                dump_src(FMA.op & 0x7, regs, consts, true);
                break;
        case FMA_FADD:
        case FMA_FMINMAX:
                if (FMA.op & 0x10)
                        printf("-");
                if (FMA.op & 0x200)
                        printf("abs(");
                dump_src(FMA.src0, regs, consts, true);
                dump_fma_expand_src0((FMA.op >> 6) & 0x7);
                if (FMA.op & 0x200)
                        printf(")");
                printf(", ");
                if (FMA.op & 0x20)
                        printf("-");
                if (FMA.op & 0x8)
                        printf("abs(");
                dump_src(FMA.op & 0x7, regs, consts, true);
                dump_fma_expand_src1((FMA.op >> 6) & 0x7);
                if (FMA.op & 0x8)
                        printf(")");
                break;
        case FMA_FADD16:
        case FMA_FMINMAX16: {
                bool abs1 = FMA.op & 0x8;
                bool abs2 = (FMA.op & 0x7) < FMA.src0;
                if (FMA.op & 0x10)
                        printf("-");
                if (abs1 || abs2)
                        printf("abs(");
                dump_src(FMA.src0, regs, consts, true);
                dump_16swizzle((FMA.op >> 6) & 0x3);
                if (abs1 || abs2)
                        printf(")");
                printf(", ");
                if (FMA.op & 0x20)
                        printf("-");
                if (abs1 && abs2)
                        printf("abs(");
                dump_src(FMA.op & 0x7, regs, consts, true);
                dump_16swizzle((FMA.op >> 8) & 0x3);
                if (abs1 && abs2)
                        printf(")");
                break;
        }
        case FMA_FCMP:
                if (FMA.op & 0x200)
                        printf("abs(");
                dump_src(FMA.src0, regs, consts, true);
                dump_fma_expand_src0((FMA.op >> 6) & 0x7);
                if (FMA.op & 0x200)
                        printf(")");
                printf(", ");
                if (FMA.op & 0x20)
                        printf("-");
                if (FMA.op & 0x8)
                        printf("abs(");
                dump_src(FMA.op & 0x7, regs, consts, true);
                dump_fma_expand_src1((FMA.op >> 6) & 0x7);
                if (FMA.op & 0x8)
                        printf(")");
                break;
        case FMA_FCMP16:
                dump_src(FMA.src0, regs, consts, true);
                // Note: this is kinda a guess, I haven't seen the blob set this to
                // anything other than the identity, but it matches FMA_TWO_SRCFmod16
                dump_16swizzle((FMA.op >> 6) & 0x3);
                printf(", ");
                dump_src(FMA.op & 0x7, regs, consts, true);
                dump_16swizzle((FMA.op >> 8) & 0x3);
                break;
        case FMA_SHIFT_ADD64:
                dump_src(FMA.src0, regs, consts, true);
                printf(", ");
                dump_src(FMA.op & 0x7, regs, consts, true);
                printf(", ");
                printf("shift:%u", (FMA.op >> 3) & 0x7);
                break;
        case FMA_THREE_SRC:
                dump_src(FMA.src0, regs, consts, true);
                printf(", ");
                dump_src(FMA.op & 0x7, regs, consts, true);
                printf(", ");
                dump_src((FMA.op >> 3) & 0x7, regs, consts, true);
                break;
        case FMA_FMA:
                if (FMA.op & (1 << 14))
                        printf("-");
                if (FMA.op & (1 << 9))
                        printf("abs(");
                dump_src(FMA.src0, regs, consts, true);
                dump_fma_expand_src0((FMA.op >> 6) & 0x7);
                if (FMA.op & (1 << 9))
                        printf(")");
                printf(", ");
                if (FMA.op & (1 << 16))
                        printf("abs(");
                dump_src(FMA.op & 0x7, regs, consts, true);
                dump_fma_expand_src1((FMA.op >> 6) & 0x7);
                if (FMA.op & (1 << 16))
                        printf(")");
                printf(", ");
                if (FMA.op & (1 << 15))
                        printf("-");
                if (FMA.op & (1 << 17))
                        printf("abs(");
                dump_src((FMA.op >> 3) & 0x7, regs, consts, true);
                if (FMA.op & (1 << 17))
                        printf(")");
                break;
        case FMA_FMA16:
                if (FMA.op & (1 << 14))
                        printf("-");
                dump_src(FMA.src0, regs, consts, true);
                dump_16swizzle((FMA.op >> 6) & 0x3);
                printf(", ");
                dump_src(FMA.op & 0x7, regs, consts, true);
                dump_16swizzle((FMA.op >> 8) & 0x3);
                printf(", ");
                if (FMA.op & (1 << 15))
                        printf("-");
                dump_src((FMA.op >> 3) & 0x7, regs, consts, true);
                dump_16swizzle((FMA.op >> 16) & 0x3);
                break;
        case FMA_FOUR_SRC:
                dump_src(FMA.src0, regs, consts, true);
                printf(", ");
                dump_src(FMA.op & 0x7, regs, consts, true);
                printf(", ");
                dump_src((FMA.op >> 3) & 0x7, regs, consts, true);
                printf(", ");
                dump_src((FMA.op >> 6) & 0x7, regs, consts, true);
                break;
        case FMA_FMA_MSCALE:
                if (FMA.op & (1 << 12))
                        printf("abs(");
                dump_src(FMA.src0, regs, consts, true);
                if (FMA.op & (1 << 12))
                        printf(")");
                printf(", ");
                if (FMA.op & (1 << 13))
                        printf("-");
                dump_src(FMA.op & 0x7, regs, consts, true);
                printf(", ");
                if (FMA.op & (1 << 14))
                        printf("-");
                dump_src((FMA.op >> 3) & 0x7, regs, consts, true);
                printf(", ");
                dump_src((FMA.op >> 6) & 0x7, regs, consts, true);
                break;
        }
        printf("\n");
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
        { 0x07936, "F32_TO_I32", ADD_ONE_SRC },
        { 0x07937, "F32_TO_U32", ADD_ONE_SRC },
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
        // take the low 16 bits, and expand it to a 32-bit float
        { 0x079a2, "F16_TO_F32.X", ADD_ONE_SRC },
        // take the high 16 bits, ...
        { 0x079a3, "F16_TO_F32.Y", ADD_ONE_SRC },
        { 0x07b2b, "SWZ.YX.v2i16",  ADD_ONE_SRC },
        { 0x07b2c, "NOP",  ADD_ONE_SRC },
        { 0x07b29, "SWZ.XX.v2i16",  ADD_ONE_SRC },
        // Logically, this should be SWZ.XY, but that's equivalent to a move, and
        // this seems to be the canonical way the blob generates a MOV.
        { 0x07b2d, "MOV",  ADD_ONE_SRC },
        { 0x07b2f, "SWZ.YY.v2i16",  ADD_ONE_SRC },
        // Given a floating point number m * 2^e, returns m ^ 2^{-1}.
        { 0x07b65, "FRCP_FREXPM", ADD_ONE_SRC },
        { 0x07b75, "FSQRT_FREXPM", ADD_ONE_SRC },
        { 0x07b8d, "FRCP_FREXPE", ADD_ONE_SRC },
        { 0x07ba5, "FSQRT_FREXPE", ADD_ONE_SRC },
        { 0x07bad, "FRSQ_FREXPE", ADD_ONE_SRC },
        // From the ARM patent US20160364209A1:
        // "Decompose v (the input) into numbers x1 and s such that v = x1 * 2^s,
        // and x1 is a floating point value in a predetermined range where the
        // value 1 is within the range and not at one extremity of the range (e.g.
        // choose a range where 1 is towards middle of range)."
        //
        // This computes s.
        { 0x07bc5, "FLOG_FREXPE", ADD_ONE_SRC },
        { 0x07d45, "CEIL", ADD_ONE_SRC },
        { 0x07d85, "FLOOR", ADD_ONE_SRC },
        { 0x07dc5, "TRUNC", ADD_ONE_SRC },
        { 0x07f18, "LSHIFT_ADD_HIGH32.i32", ADD_TWO_SRC },
        { 0x08000, "LD_ATTR.f16", ADD_LOAD_ATTR, true },
        { 0x08100, "LD_ATTR.v2f16", ADD_LOAD_ATTR, true },
        { 0x08200, "LD_ATTR.v3f16", ADD_LOAD_ATTR, true },
        { 0x08300, "LD_ATTR.v4f16", ADD_LOAD_ATTR, true },
        { 0x08400, "LD_ATTR.f32", ADD_LOAD_ATTR, true },
        { 0x08500, "LD_ATTR.v3f32", ADD_LOAD_ATTR, true },
        { 0x08600, "LD_ATTR.v3f32", ADD_LOAD_ATTR, true },
        { 0x08700, "LD_ATTR.v4f32", ADD_LOAD_ATTR, true },
        { 0x08800, "LD_ATTR.i32", ADD_LOAD_ATTR, true },
        { 0x08900, "LD_ATTR.v3i32", ADD_LOAD_ATTR, true },
        { 0x08a00, "LD_ATTR.v3i32", ADD_LOAD_ATTR, true },
        { 0x08b00, "LD_ATTR.v4i32", ADD_LOAD_ATTR, true },
        { 0x08c00, "LD_ATTR.u32", ADD_LOAD_ATTR, true },
        { 0x08d00, "LD_ATTR.v3u32", ADD_LOAD_ATTR, true },
        { 0x08e00, "LD_ATTR.v3u32", ADD_LOAD_ATTR, true },
        { 0x08f00, "LD_ATTR.v4u32", ADD_LOAD_ATTR, true },
        { 0x0a000, "LD_VAR.32", ADD_VARYING_INTERP, true },
        { 0x0b000, "TEX", ADD_TEX_COMPACT, true },
        { 0x0c188, "LOAD.i32", ADD_TWO_SRC, true },
        { 0x0c1a0, "LD_UBO.i32", ADD_TWO_SRC, true },
        { 0x0c1b8, "LD_SCRATCH.v2i32", ADD_TWO_SRC, true },
        { 0x0c1c8, "LOAD.v2i32", ADD_TWO_SRC, true },
        { 0x0c1e0, "LD_UBO.v2i32", ADD_TWO_SRC, true },
        { 0x0c1f8, "LD_SCRATCH.v2i32", ADD_TWO_SRC, true },
        { 0x0c208, "LOAD.v4i32", ADD_TWO_SRC, true },
        // src0 = offset, src1 = binding
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
        // *_FAST does not exist on G71 (added to G51, G72, and everything after)
        { 0x0cc00, "FRCP_FAST.f32", ADD_ONE_SRC },
        { 0x0cc20, "FRSQ_FAST.f32", ADD_ONE_SRC },
        // Given a floating point number m * 2^e, produces a table-based
        // approximation of 2/m using the top 17 bits. Includes special cases for
        // infinity, NaN, and zero, and copies the sign bit.
        { 0x0ce00, "FRCP_TABLE", ADD_ONE_SRC },
        // Exists on G71
        { 0x0ce10, "FRCP_FAST.f16.X", ADD_ONE_SRC },
        // A similar table for inverse square root, using the high 17 bits of the
        // mantissa as well as the low bit of the exponent.
        { 0x0ce20, "FRSQ_TABLE", ADD_ONE_SRC },
        { 0x0ce30, "FRCP_FAST.f16.Y", ADD_ONE_SRC },
        { 0x0ce50, "FRSQ_FAST.f16.X", ADD_ONE_SRC },
        // Used in the argument reduction for log. Given a floating-point number
        // m * 2^e, uses the top 4 bits of m to produce an approximation to 1/m
        // with the exponent forced to 0 and only the top 5 bits are nonzero. 0,
        // infinity, and NaN all return 1.0.
        // See the ARM patent for more information.
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
        // For each bit i, return src2[i] ? src0[i] : src1[i]. In other words, this
        // is the same as (src2 & src0) | (~src2 & src1).
        { 0x0e8c0, "MUX", ADD_THREE_SRC },
        { 0x0e9b0, "ATAN_LDEXP.Y.f32", ADD_TWO_SRC },
        { 0x0e9b8, "ATAN_LDEXP.X.f32", ADD_TWO_SRC },
        { 0x0ea60, "SEL.XX.i16", ADD_TWO_SRC },
        { 0x0ea70, "SEL.XY.i16", ADD_TWO_SRC },
        { 0x0ea68, "SEL.YX.i16", ADD_TWO_SRC },
        { 0x0ea78, "SEL.YY.i16", ADD_TWO_SRC },
        { 0x0ec00, "F32_TO_F16", ADD_TWO_SRC },
        { 0x0f640, "ICMP.GL.GT", ADD_TWO_SRC }, // src0 > src1 ? 1 : 0
        { 0x0f648, "ICMP.GL.GE", ADD_TWO_SRC },
        { 0x0f650, "UCMP.GL.GT", ADD_TWO_SRC },
        { 0x0f658, "UCMP.GL.GE", ADD_TWO_SRC },
        { 0x0f660, "ICMP.GL.EQ", ADD_TWO_SRC },
        { 0x0f6c0, "ICMP.D3D.GT", ADD_TWO_SRC }, // src0 > src1 ? ~0 : 0
        { 0x0f6c8, "ICMP.D3D.GE", ADD_TWO_SRC },
        { 0x0f6d0, "UCMP.D3D.GT", ADD_TWO_SRC },
        { 0x0f6d8, "UCMP.D3D.GE", ADD_TWO_SRC },
        { 0x0f6e0, "ICMP.D3D.EQ", ADD_TWO_SRC },
        { 0x10000, "MAX.v2f16", ADD_FMINMAX16 },
        { 0x11000, "ADD_MSCALE.f32", ADD_FADDMscale },
        { 0x12000, "MIN.v2f16", ADD_FMINMAX16 },
        { 0x14000, "ADD.v2f16", ADD_FADD16 },
        { 0x17000, "FCMP.D3D", ADD_FCMP16 },
        { 0x178c0, "ADD.i32",  ADD_TWO_SRC },
        { 0x17900, "ADD.v2i16", ADD_TWO_SRC },
        { 0x17ac0, "SUB.i32",  ADD_TWO_SRC },
        { 0x17c10, "ADDC.i32", ADD_TWO_SRC }, // adds src0 to the bottom bit of src1
        { 0x17d80, "ADD.i32.i16.X", ADD_TWO_SRC },
        { 0x17d90, "ADD.i32.u16.X", ADD_TWO_SRC },
        { 0x17dc0, "ADD.i32.i16.Y", ADD_TWO_SRC },
        { 0x17dd0, "ADD.i32.u16.Y", ADD_TWO_SRC },
        // Compute varying address and datatype (for storing in the vertex shader),
        // and store the vec3 result in the data register. The result is passed as
        // the 3 normal arguments to ST_VAR.
        { 0x18000, "LD_VAR_ADDR.f16", ADD_VARYING_ADDRESS, true },
        { 0x18100, "LD_VAR_ADDR.f32", ADD_VARYING_ADDRESS, true },
        { 0x18200, "LD_VAR_ADDR.i32", ADD_VARYING_ADDRESS, true },
        { 0x18300, "LD_VAR_ADDR.u32", ADD_VARYING_ADDRESS, true },
        // Implements alpha-to-coverage, as well as possibly the late depth and
        // stencil tests. The first source is the existing sample mask in R60
        // (possibly modified by gl_SampleMask), and the second source is the alpha
        // value.  The sample mask is written right away based on the
        // alpha-to-coverage result using the normal register write mechanism,
        // since that doesn't need to read from any memory, and then written again
        // later based on the result of the stencil and depth tests using the
        // special register.
        { 0x191e8, "ATEST.f32", ADD_TWO_SRC, true },
        { 0x191f0, "ATEST.X.f16", ADD_TWO_SRC, true },
        { 0x191f8, "ATEST.Y.f16", ADD_TWO_SRC, true },
        // store a varying given the address and datatype from LD_VAR_ADDR
        { 0x19300, "ST_VAR.v1", ADD_THREE_SRC, true },
        { 0x19340, "ST_VAR.v2", ADD_THREE_SRC, true },
        { 0x19380, "ST_VAR.v3", ADD_THREE_SRC, true },
        { 0x193c0, "ST_VAR.v4", ADD_THREE_SRC, true },
        // This takes the sample coverage mask (computed by ATEST above) as a
        // regular argument, in addition to the vec4 color in the special register.
        { 0x1952c, "BLEND", ADD_BLENDING, true },
        { 0x1a000, "LD_VAR.16", ADD_VARYING_INTERP, true },
        { 0x1ae60, "TEX", ADD_TEX, true },
        { 0x1c000, "RSHIFT_NAND.i32", ADD_THREE_SRC },
        { 0x1c300, "RSHIFT_OR.i32", ADD_THREE_SRC },
        { 0x1c400, "RSHIFT_AND.i32", ADD_THREE_SRC },
        { 0x1c700, "RSHIFT_NOR.i32", ADD_THREE_SRC },
        { 0x1c800, "LSHIFT_NAND.i32", ADD_THREE_SRC },
        { 0x1cb00, "LSHIFT_OR.i32", ADD_THREE_SRC },
        { 0x1cc00, "LSHIFT_AND.i32", ADD_THREE_SRC },
        { 0x1cf00, "LSHIFT_NOR.i32", ADD_THREE_SRC },
        { 0x1d000, "RSHIFT_XOR.i32", ADD_THREE_SRC },
        { 0x1d100, "RSHIFT_XNOR.i32", ADD_THREE_SRC },
        { 0x1d200, "LSHIFT_XOR.i32", ADD_THREE_SRC },
        { 0x1d300, "LSHIFT_XNOR.i32", ADD_THREE_SRC },
        { 0x1d400, "LSHIFT_ADD.i32", ADD_THREE_SRC },
        { 0x1d500, "LSHIFT_SUB.i32", ADD_THREE_SRC },
        { 0x1d500, "LSHIFT_RSUB.i32", ADD_THREE_SRC },
        { 0x1d700, "RSHIFT_ADD.i32", ADD_THREE_SRC },
        { 0x1d800, "RSHIFT_SUB.i32", ADD_THREE_SRC },
        { 0x1d900, "RSHIFT_RSUB.i32", ADD_THREE_SRC },
        { 0x1da00, "ARSHIFT_ADD.i32", ADD_THREE_SRC },
        { 0x1db00, "ARSHIFT_SUB.i32", ADD_THREE_SRC },
        { 0x1dc00, "ARSHIFT_RSUB.i32", ADD_THREE_SRC },
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
                        opCmp = op & ~0xff;
                        break;
                case ADD_LOAD_ATTR:
                        opCmp = op & ~0x7f;
                        break;
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

static void dump_add(uint64_t word, struct bifrost_regs regs, struct bifrost_regs next_regs, uint64_t *consts,
                     unsigned data_reg, unsigned offset, bool verbose)
{
        if (verbose) {
                printf("# ADD: %016" PRIx64 "\n", word);
        }
        struct bifrost_add_inst ADD;
        memcpy((char *) &ADD, (char *) &word, sizeof(ADD));
        struct add_op_info info = find_add_op_info(ADD.op);

        printf("%s", info.name);

        // float16 seems like it doesn't support output modifiers
        if (info.src_type == ADD_FADD || info.src_type == ADD_FMINMAX) {
                // output modifiers
                dump_output_mod(bits(ADD.op, 8, 10));
                if (info.src_type == ADD_FADD)
                        dump_round_mode(bits(ADD.op, 10, 12));
                else
                        dump_minmax_mode(bits(ADD.op, 10, 12));
        } else if (info.src_type == ADD_FCMP || info.src_type == ADD_FCMP16) {
                dump_fcmp(bits(ADD.op, 3, 6));
                if (info.src_type == ADD_FCMP)
                        printf(".f32");
                else
                        printf(".v2f16");
        } else if (info.src_type == ADD_FADDMscale) {
                switch ((ADD.op >> 6) & 0x7) {
                case 0:
                        break;
                // causes GPU hangs on G71
                case 1:
                        printf(".invalid");
                        break;
                // Same as usual outmod value.
                case 2:
                        printf(".clamp_0_1");
                        break;
                // If src0 is infinite or NaN, flush it to zero so that the other
                // source is passed through unmodified.
                case 3:
                        printf(".flush_src0_inf_nan");
                        break;
                // Vice versa.
                case 4:
                        printf(".flush_src1_inf_nan");
                        break;
                // Every other case seems to behave the same as the above?
                default:
                        printf(".unk%d", (ADD.op >> 6) & 0x7);
                        break;
                }
        } else if (info.src_type == ADD_VARYING_INTERP) {
                if (ADD.op & 0x200)
                        printf(".reuse");
                if (ADD.op & 0x400)
                        printf(".flat");
                switch ((ADD.op >> 7) & 0x3) {
                case 0:
                        printf(".per_frag");
                        break;
                case 1:
                        printf(".centroid");
                        break;
                case 2:
                        break;
                case 3:
                        printf(".explicit");
                        break;
                }
                printf(".v%d", ((ADD.op >> 5) & 0x3) + 1);
        } else if (info.src_type == ADD_BRANCH) {
                enum branch_code branchCode = (enum branch_code) ((ADD.op >> 6) & 0x3f);
                if (branchCode == BR_ALWAYS) {
                        // unconditional branch
                } else {
                        enum branch_cond cond = (enum branch_cond) ((ADD.op >> 6) & 0x7);
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
                                        printf(".LT.u");
                                else
                                        printf(".LT.i");
                                break;
                        case BR_COND_LE:
                                if (size == BR_SIZE_32_AND_16X || size == BR_SIZE_32_AND_16Y) {
                                        printf(".UNE.f");
                                } else {
                                        if (portSwapped)
                                                printf(".LE.u");
                                        else
                                                printf(".LE.i");
                                }
                                break;
                        case BR_COND_GT:
                                if (portSwapped)
                                        printf(".GT.u");
                                else
                                        printf(".GT.i");
                                break;
                        case BR_COND_GE:
                                if (portSwapped)
                                        printf(".GE.u");
                                else
                                        printf(".GE.i");
                                break;
                        case BR_COND_EQ:
                                if (portSwapped)
                                        printf(".NE.i");
                                else
                                        printf(".EQ.i");
                                break;
                        case BR_COND_OEQ:
                                if (portSwapped)
                                        printf(".UNE.f");
                                else
                                        printf(".OEQ.f");
                                break;
                        case BR_COND_OGT:
                                if (portSwapped)
                                        printf(".OGT.unk.f");
                                else
                                        printf(".OGT.f");
                                break;
                        case BR_COND_OLT:
                                if (portSwapped)
                                        printf(".OLT.unk.f");
                                else
                                        printf(".OLT.f");
                                break;
                        }
                        switch (size) {
                        case BR_SIZE_32:
                        case BR_SIZE_32_AND_16X:
                        case BR_SIZE_32_AND_16Y:
                                printf("32");
                                break;
                        case BR_SIZE_16XX:
                        case BR_SIZE_16YY:
                        case BR_SIZE_16YX0:
                        case BR_SIZE_16YX1:
                                printf("16");
                                break;
                        case BR_SIZE_ZERO: {
                                unsigned ctrl = (ADD.op >> 1) & 0x3;
                                if (ctrl == 0)
                                        printf("32.Z");
                                else
                                        printf("16.Z");
                                break;
                        }
                        }
                }
        }
        printf(" ");

        struct bifrost_reg_ctrl next_ctrl = DecodeRegCtrl(next_regs);
        if (next_ctrl.add_write_unit != REG_WRITE_NONE) {
                printf("{R%d, T1}, ", GetRegToWrite(next_ctrl.add_write_unit, next_regs));
        } else {
                printf("T1, ");
        }

        switch (info.src_type) {
        case ADD_BLENDING:
                // Note: in this case, regs.uniform_const == location | 0x8
                // This probably means we can't load uniforms or immediates in the
                // same instruction. This re-uses the encoding that normally means
                // "disabled", where the low 4 bits are ignored. Perhaps the extra
                // 0x8 or'd in indicates this is happening.
                printf("location:%d, ", regs.uniform_const & 0x7);
        // fallthrough
        case ADD_ONE_SRC:
                dump_src(ADD.src0, regs, consts, false);
                break;
        case ADD_TEX:
        case ADD_TEX_COMPACT: {
                int tex_index;
                int sampler_index;
                bool dualTex = false;
                if (info.src_type == ADD_TEX_COMPACT) {
                        tex_index = (ADD.op >> 3) & 0x7;
                        sampler_index = (ADD.op >> 7) & 0x7;
                        bool unknown = (ADD.op & 0x40);
                        // TODO: figure out if the unknown bit is ever 0
                        if (!unknown)
                                printf("unknown ");
                } else {
                        uint64_t constVal = get_const(consts, regs);
                        uint32_t controlBits = (ADD.op & 0x8) ? (constVal >> 32) : constVal;
                        struct bifrost_tex_ctrl ctrl;
                        memcpy((char *) &ctrl, (char *) &controlBits, sizeof(ctrl));

                        // TODO: figure out what actually triggers dual-tex
                        if (ctrl.result_type == 9) {
                                struct bifrost_dual_tex_ctrl dualCtrl;
                                memcpy((char *) &dualCtrl, (char *) &controlBits, sizeof(ctrl));
                                printf("(dualtex) tex0:%d samp0:%d tex1:%d samp1:%d ",
                                       dualCtrl.tex_index0, dualCtrl.sampler_index0,
                                       dualCtrl.tex_index1, dualCtrl.sampler_index1);
                                if (dualCtrl.unk0 != 3)
                                        printf("unk:%d ", dualCtrl.unk0);
                                dualTex = true;
                        } else {
                                if (ctrl.no_merge_index) {
                                        tex_index = ctrl.tex_index;
                                        sampler_index = ctrl.sampler_index;
                                } else {
                                        tex_index = sampler_index = ctrl.tex_index;
                                        unsigned unk = ctrl.sampler_index >> 2;
                                        if (unk != 3)
                                                printf("unk:%d ", unk);
                                        if (ctrl.sampler_index & 1)
                                                tex_index = -1;
                                        if (ctrl.sampler_index & 2)
                                                sampler_index = -1;
                                }

                                if (ctrl.unk0 != 3)
                                        printf("unk0:%d ", ctrl.unk0);
                                if (ctrl.unk1)
                                        printf("unk1 ");
                                if (ctrl.unk2 != 0xf)
                                        printf("unk2:%x ", ctrl.unk2);

                                switch (ctrl.result_type) {
                                case 0x4:
                                        printf("f32 ");
                                        break;
                                case 0xe:
                                        printf("i32 ");
                                        break;
                                case 0xf:
                                        printf("u32 ");
                                        break;
                                default:
                                        printf("unktype(%x) ", ctrl.result_type);
                                }

                                switch (ctrl.tex_type) {
                                case 0:
                                        printf("cube ");
                                        break;
                                case 1:
                                        printf("buffer ");
                                        break;
                                case 2:
                                        printf("2D ");
                                        break;
                                case 3:
                                        printf("3D ");
                                        break;
                                }

                                if (ctrl.is_shadow)
                                        printf("shadow ");
                                if (ctrl.is_array)
                                        printf("array ");

                                if (!ctrl.filter) {
                                        if (ctrl.calc_gradients) {
                                                int comp = (controlBits >> 20) & 0x3;
                                                printf("txg comp:%d ", comp);
                                        } else {
                                                printf("txf ");
                                        }
                                } else {
                                        if (!ctrl.not_supply_lod) {
                                                if (ctrl.compute_lod)
                                                        printf("lod_bias ");
                                                else
                                                        printf("lod ");
                                        }

                                        if (!ctrl.calc_gradients)
                                                printf("grad ");
                                }

                                if (ctrl.texel_offset)
                                        printf("offset ");
                        }
                }

                if (!dualTex) {
                        if (tex_index == -1)
                                printf("tex:indirect ");
                        else
                                printf("tex:%d ", tex_index);

                        if (sampler_index == -1)
                                printf("samp:indirect ");
                        else
                                printf("samp:%d ", sampler_index);
                }
                break;
        }
        case ADD_VARYING_INTERP: {
                unsigned addr = ADD.op & 0x1f;
                if (addr < 0b10100) {
                        // direct addr
                        printf("%d", addr);
                } else if (addr < 0b11000) {
                        if (addr == 22)
                                printf("fragw");
                        else if (addr == 23)
                                printf("fragz");
                        else
                                printf("unk%d", addr);
                } else {
                        dump_src(ADD.op & 0x7, regs, consts, false);
                }
                printf(", ");
                dump_src(ADD.src0, regs, consts, false);
                break;
        }
        case ADD_VARYING_ADDRESS: {
                dump_src(ADD.src0, regs, consts, false);
                printf(", ");
                dump_src(ADD.op & 0x7, regs, consts, false);
                printf(", ");
                unsigned location = (ADD.op >> 3) & 0x1f;
                if (location < 16) {
                        printf("location:%d", location);
                } else if (location == 20) {
                        printf("location:%u", (uint32_t) get_const(consts, regs));
                } else if (location == 21) {
                        printf("location:%u", (uint32_t) (get_const(consts, regs) >> 32));
                } else {
                        printf("location:%d(unk)", location);
                }
                break;
        }
        case ADD_LOAD_ATTR:
                printf("location:%d, ", (ADD.op >> 3) & 0xf);
        case ADD_TWO_SRC:
                dump_src(ADD.src0, regs, consts, false);
                printf(", ");
                dump_src(ADD.op & 0x7, regs, consts, false);
                break;
        case ADD_THREE_SRC:
                dump_src(ADD.src0, regs, consts, false);
                printf(", ");
                dump_src(ADD.op & 0x7, regs, consts, false);
                printf(", ");
                dump_src((ADD.op >> 3) & 0x7, regs, consts, false);
                break;
        case ADD_FADD:
        case ADD_FMINMAX:
                if (ADD.op & 0x10)
                        printf("-");
                if (ADD.op & 0x1000)
                        printf("abs(");
                dump_src(ADD.src0, regs, consts, false);
                switch ((ADD.op >> 6) & 0x3) {
                case 3:
                        printf(".x");
                        break;
                default:
                        break;
                }
                if (ADD.op & 0x1000)
                        printf(")");
                printf(", ");
                if (ADD.op & 0x20)
                        printf("-");
                if (ADD.op & 0x8)
                        printf("abs(");
                dump_src(ADD.op & 0x7, regs, consts, false);
                switch ((ADD.op >> 6) & 0x3) {
                case 1:
                case 3:
                        printf(".x");
                        break;
                case 2:
                        printf(".y");
                        break;
                case 0:
                        break;
                default:
                        printf(".unk");
                        break;
                }
                if (ADD.op & 0x8)
                        printf(")");
                break;
        case ADD_FADD16:
                if (ADD.op & 0x10)
                        printf("-");
                if (ADD.op & 0x1000)
                        printf("abs(");
                dump_src(ADD.src0, regs, consts, false);
                if (ADD.op & 0x1000)
                        printf(")");
                dump_16swizzle((ADD.op >> 6) & 0x3);
                printf(", ");
                if (ADD.op & 0x20)
                        printf("-");
                if (ADD.op & 0x8)
                        printf("abs(");
                dump_src(ADD.op & 0x7, regs, consts, false);
                dump_16swizzle((ADD.op >> 8) & 0x3);
                if (ADD.op & 0x8)
                        printf(")");
                break;
        case ADD_FMINMAX16: {
                bool abs1 = ADD.op & 0x8;
                bool abs2 = (ADD.op & 0x7) < ADD.src0;
                if (ADD.op & 0x10)
                        printf("-");
                if (abs1 || abs2)
                        printf("abs(");
                dump_src(ADD.src0, regs, consts, false);
                dump_16swizzle((ADD.op >> 6) & 0x3);
                if (abs1 || abs2)
                        printf(")");
                printf(", ");
                if (ADD.op & 0x20)
                        printf("-");
                if (abs1 && abs2)
                        printf("abs(");
                dump_src(ADD.op & 0x7, regs, consts, false);
                dump_16swizzle((ADD.op >> 8) & 0x3);
                if (abs1 && abs2)
                        printf(")");
                break;
        }
        case ADD_FADDMscale: {
                if (ADD.op & 0x400)
                        printf("-");
                if (ADD.op & 0x200)
                        printf("abs(");
                dump_src(ADD.src0, regs, consts, false);
                if (ADD.op & 0x200)
                        printf(")");

                printf(", ");

                if (ADD.op & 0x800)
                        printf("-");
                dump_src(ADD.op & 0x7, regs, consts, false);

                printf(", ");

                dump_src((ADD.op >> 3) & 0x7, regs, consts, false);
                break;
        }
        case ADD_FCMP:
                if (ADD.op & 0x400) {
                        printf("-");
                }
                if (ADD.op & 0x100) {
                        printf("abs(");
                }
                dump_src(ADD.src0, regs, consts, false);
                switch ((ADD.op >> 6) & 0x3) {
                case 3:
                        printf(".x");
                        break;
                default:
                        break;
                }
                if (ADD.op & 0x100) {
                        printf(")");
                }
                printf(", ");
                if (ADD.op & 0x200) {
                        printf("abs(");
                }
                dump_src(ADD.op & 0x7, regs, consts, false);
                switch ((ADD.op >> 6) & 0x3) {
                case 1:
                case 3:
                        printf(".x");
                        break;
                case 2:
                        printf(".y");
                        break;
                case 0:
                        break;
                default:
                        printf(".unk");
                        break;
                }
                if (ADD.op & 0x200) {
                        printf(")");
                }
                break;
        case ADD_FCMP16:
                dump_src(ADD.src0, regs, consts, false);
                dump_16swizzle((ADD.op >> 6) & 0x3);
                printf(", ");
                dump_src(ADD.op & 0x7, regs, consts, false);
                dump_16swizzle((ADD.op >> 8) & 0x3);
                break;
        case ADD_BRANCH: {
                enum branch_code code = (enum branch_code) ((ADD.op >> 6) & 0x3f);
                enum branch_bit_size size = (enum branch_bit_size) ((ADD.op >> 9) & 0x7);
                if (code != BR_ALWAYS) {
                        dump_src(ADD.src0, regs, consts, false);
                        switch (size) {
                        case BR_SIZE_16XX:
                                printf(".x");
                                break;
                        case BR_SIZE_16YY:
                        case BR_SIZE_16YX0:
                        case BR_SIZE_16YX1:
                                printf(".y");
                                break;
                        case BR_SIZE_ZERO: {
                                unsigned ctrl = (ADD.op >> 1) & 0x3;
                                switch (ctrl) {
                                case 1:
                                        printf(".y");
                                        break;
                                case 2:
                                        printf(".x");
                                        break;
                                default:
                                        break;
                                }
                        }
                        default:
                                break;
                        }
                        printf(", ");
                }
                if (code != BR_ALWAYS && size != BR_SIZE_ZERO) {
                        dump_src(ADD.op & 0x7, regs, consts, false);
                        switch (size) {
                        case BR_SIZE_16XX:
                        case BR_SIZE_16YX0:
                        case BR_SIZE_16YX1:
                        case BR_SIZE_32_AND_16X:
                                printf(".x");
                                break;
                        case BR_SIZE_16YY:
                        case BR_SIZE_32_AND_16Y:
                                printf(".y");
                                break;
                        default:
                                break;
                        }
                        printf(", ");
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
                        printf("clause_%d", offset + branch_offset);
                } else {
                        dump_src(offsetSrc, regs, consts, false);
                }
        }
        }
        if (info.has_data_reg) {
                printf(", R%d", data_reg);
        }
        printf("\n");
}

void dump_instr(const struct bifrost_alu_inst *instr, struct bifrost_regs next_regs, uint64_t *consts,
                unsigned data_reg, unsigned offset, bool verbose)
{
        struct bifrost_regs regs;
        memcpy((char *) &regs, (char *) &instr->reg_bits, sizeof(regs));

        if (verbose) {
                printf("# regs: %016" PRIx64 "\n", instr->reg_bits);
                dump_regs(regs);
        }
        dump_fma(instr->fma_bits, regs, next_regs, consts, verbose);
        dump_add(instr->add_bits, regs, next_regs, consts, data_reg, offset, verbose);
}

bool dump_clause(uint32_t *words, unsigned *size, unsigned offset, bool verbose)
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
                        printf("# ");
                        for (int j = 0; j < 4; j++)
                                printf("%08x ", words[3 - j]); // low bit on the right
                        printf("\n");
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
                        printf("# tag: 0x%02x\n", tag);
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
                                        printf("unknown tag bits 0x%02x\n", tag);
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
                                        printf("# unknown pos 0x%x\n", pos);
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
                printf("# header: %012" PRIx64 "\n", header_bits);
        }

        struct bifrost_header header;
        memcpy((char *) &header, (char *) &header_bits, sizeof(struct bifrost_header));
        dump_header(header, verbose);
        if (!header.no_end_of_shader)
                stopbit = true;

        printf("{\n");
        for (i = 0; i < num_instrs; i++) {
                struct bifrost_regs next_regs;
                if (i + 1 == num_instrs) {
                        memcpy((char *) &next_regs, (char *) &instrs[0].reg_bits,
                               sizeof(next_regs));
                } else {
                        memcpy((char *) &next_regs, (char *) &instrs[i + 1].reg_bits,
                               sizeof(next_regs));
                }

                dump_instr(&instrs[i], next_regs, consts, header.datareg, offset, verbose);
        }
        printf("}\n");

        if (verbose) {
                for (unsigned i = 0; i < num_consts; i++) {
                        printf("# const%d: %08" PRIx64 "\n", 2 * i, consts[i] & 0xffffffff);
                        printf("# const%d: %08" PRIx64 "\n", 2 * i + 1, consts[i] >> 32);
                }
        }
        return stopbit;
}

void disassemble_bifrost(uint8_t *code, size_t size, bool verbose)
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
                printf("clause_%d:\n", offset);
                unsigned size;
                if (dump_clause(words, &size, offset, verbose) == true) {
                        break;
                }
                words += size * 4;
                offset += size;
        }
}

