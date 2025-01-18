/*
 * Copyright (c) 2019 Zodiac Inflight Innovations
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Jonathan Marek <jonathan@marek.ca>
 */

#include "etnaviv_compiler_nir.h"
#include "util/compiler.h"

/* info to translate a nir op to etna_inst */
struct etna_op_info {
   enum isa_opc opcode;
   enum isa_cond cond;
   enum isa_type type;
};

static const struct etna_op_info etna_ops[] = {
   [0 ... nir_num_opcodes - 1] = {0xff},
#undef TRUE
#undef FALSE
#define OPCT(nir, op, cond, type) [nir_op_##nir] = { \
   ISA_OPC_##op, \
   ISA_COND_##cond, \
   ISA_TYPE_##type \
}
#define OPC(nir, op, cond) OPCT(nir, op, cond, F32)
#define IOPC(nir, op, cond) OPCT(nir, op, cond, S32)
#define UOPC(nir, op, cond) OPCT(nir, op, cond, U32)
#define OP(nir, op) OPC(nir, op, TRUE)
#define IOP(nir, op) IOPC(nir, op, TRUE)
#define UOP(nir, op) UOPC(nir, op, TRUE)
   OP(mov, MOV), OP(fneg, MOV), OP(fabs, MOV), OP(fsat, MOV),
   OP(fmul, MUL), OP(fadd, ADD), OP(ffma, MAD),
   OP(fdot2, DP2), OP(fdot3, DP3), OP(fdot4, DP4),
   OPC(fmin, SELECT, GT), OPC(fmax, SELECT, LT),
   OP(ffract, FRC), OP(frcp, RCP), OP(frsq, RSQ),
   OP(fsqrt, SQRT), OP(fsin, SIN), OP(fcos, COS),
   OP(fsign, SIGN), OP(ffloor, FLOOR), OP(fceil, CEIL),
   OP(flog2, LOG), OP(fexp2, EXP),
   OPC(seq, SET, EQ), OPC(sne, SET, NE), OPC(sge, SET, GE), OPC(slt, SET, LT),
   OPC(fcsel, SELECT, NZ),
   OP(fdiv, DIV),

   /* type convert */
   IOP(i2f32, I2F),
   IOP(i2i32, I2I),
   OPCT(i2i16, I2I, TRUE, S16),
   OPCT(i2i8,  I2I, TRUE, S8),
   UOP(u2f32, I2F),
   UOP(u2u32, I2I),
   OPCT(u2u16, I2I, TRUE, U16),
   OPCT(u2u8,  I2I, TRUE, U8),
   IOP(f2i32, F2I),
   OPCT(f2i16, F2I, TRUE, S16),
   OPCT(f2i8,  F2I, TRUE, S8),
   UOP(f2u32, F2I),
   OPCT(f2u16, F2I, TRUE, U16),
   OPCT(f2u8,  F2I, TRUE, U8),
   UOP(b2f32, AND), /* AND with fui(1.0f) */
   UOP(b2i32, AND), /* AND with 1 */
   UOP(b2i8, AND),  /* AND with 1 */

   /* arithmetic */
   IOP(iadd, ADD),
   IOP(imul, IMULLO0),
   /* IOP(imad, IMADLO0), */
   IOP(ineg, ADD), /* ADD 0, -x */
   IOP(iabs, IABS),
   IOP(isign, SIGN),
   IOPC(imin, SELECT, GT),
   IOPC(imax, SELECT, LT),
   UOPC(umin, SELECT, GT),
   UOPC(umax, SELECT, LT),

   /* select */
   UOPC(b32csel, SELECT, NZ),

   /* compare with int result */
    OPC(feq32, CMP, EQ),
    OPC(fneu32, CMP, NE),
    OPC(fge32, CMP, GE),
    OPC(flt32, CMP, LT),
   IOPC(ieq32, CMP, EQ),
   IOPC(ine32, CMP, NE),
   IOPC(ige32, CMP, GE),
   IOPC(ilt32, CMP, LT),
   UOPC(uge32, CMP, GE),
   UOPC(ult32, CMP, LT),

   /* bit ops */
   IOP(ior,  OR),
   IOP(iand, AND),
   IOP(ixor, XOR),
   IOP(inot, NOT),
   IOP(ishl, LSHIFT),
   IOP(ishr, RSHIFT),
   UOP(ushr, RSHIFT),
   UOP(uclz, LEADZERO),
};

void
etna_emit_alu(struct etna_compile *c, nir_op op, struct etna_inst_dst dst,
              struct etna_inst_src src[3], bool saturate)
{
   struct etna_op_info ei = etna_ops[op];
   unsigned swiz_scalar = INST_SWIZ_BROADCAST(ffs(dst.write_mask) - 1);

   if (ei.opcode == 0xff)
      compile_error(c, "Unhandled ALU op: %s\n", nir_op_infos[op].name);

   struct etna_inst inst = {
      .opcode = ei.opcode,
      .type = ei.type,
      .cond = ei.cond,
      .dst = dst,
      .sat = saturate,
      .src[0] = src[0],
      .src[1] = src[1],
      .src[2] = src[2],

   };

   switch (op) {
   case nir_op_fdiv:
   case nir_op_flog2:
   case nir_op_fsin:
   case nir_op_fcos:
      if (c->specs->has_new_transcendentals)
         inst.rounding = ISA_ROUNDING_RTZ;
      FALLTHROUGH;
   case nir_op_frsq:
   case nir_op_frcp:
   case nir_op_fexp2:
   case nir_op_fsqrt:
   case nir_op_imul:
      /* scalar instructions we want src to be in x component */
      inst.src[0].swiz = inst_swiz_compose(src[0].swiz, swiz_scalar);
      inst.src[1].swiz = inst_swiz_compose(src[1].swiz, swiz_scalar);
      break;
   /* deal with instructions which don't have 1:1 mapping */
   case nir_op_fmin:
   case nir_op_fmax:
   case nir_op_imin:
   case nir_op_imax:
   case nir_op_umin:
   case nir_op_umax:
      inst.src[2] = src[0];
      break;
   case nir_op_b2f32:
      inst.src[1] = etna_immediate_float(1.0f);
      break;
   case nir_op_b2i32:
      inst.src[1] = etna_immediate_int(1);
      break;
   case nir_op_ineg:
      /* ADD 0, -x */
      inst.src[0] = etna_immediate_int(0);
      inst.src[1] = src[0];
      inst.src[1].neg = 1;
      break;
   default:
      break;
   }

   /* set the "true" value for CMP instructions */
   if (inst.opcode == ISA_OPC_CMP)
      inst.src[2] = etna_immediate_int(-1);

   emit_inst(c, &inst);
}

void
etna_emit_tex(struct etna_compile *c, nir_texop op, unsigned texid, unsigned dst_swiz,
              struct etna_inst_dst dst, struct etna_inst_src coord,
              struct etna_inst_src src1, struct etna_inst_src src2)
{
   struct etna_inst inst = {
      .dst = dst,
      .tex.id = texid + (is_fs(c) ? 0 : c->specs->vertex_sampler_offset),
      .tex.swiz = dst_swiz,
      .src[0] = coord,
   };

   if (src1.use)
      inst.src[1] = src1;

   if (src2.use)
      inst.src[2] = src2;

   switch (op) {
   case nir_texop_tex: inst.opcode = ISA_OPC_TEXLD; break;
   case nir_texop_txb: inst.opcode = ISA_OPC_TEXLDB; break;
   case nir_texop_txd: inst.opcode = ISA_OPC_TEXLDD; break;
   case nir_texop_txl: inst.opcode = ISA_OPC_TEXLDL; break;
   default:
      compile_error(c, "Unhandled NIR tex type: %d\n", op);
   }

   emit_inst(c, &inst);
}

void
etna_emit_jump(struct etna_compile *c, unsigned block, struct etna_inst_src condition)
{
   if (!condition.use) {
      emit_inst(c, &(struct etna_inst) {.opcode = ISA_OPC_BRANCH, .imm = block });
      return;
   }

   struct etna_inst inst = {
      .opcode = ISA_OPC_BRANCH_UNARY,
      .cond = ISA_COND_NOT,
      .type = ISA_TYPE_U32,
      .src[0] = condition,
      .imm = block,
   };
   inst.src[0].swiz = INST_SWIZ_BROADCAST(inst.src[0].swiz & 3);
   emit_inst(c, &inst);
}

void
etna_emit_discard(struct etna_compile *c, struct etna_inst_src condition)
{
   if (!condition.use) {
      emit_inst(c, &(struct etna_inst) { .opcode = ISA_OPC_TEXKILL });
      return;
   }

   struct etna_inst inst = {
      .opcode = ISA_OPC_TEXKILL,
      .cond = ISA_COND_NZ,
      .type = (c->info->halti < 2) ? ISA_TYPE_F32 : ISA_TYPE_U32,
      .src[0] = condition,
   };
   inst.src[0].swiz = INST_SWIZ_BROADCAST(inst.src[0].swiz & 3);
   emit_inst(c, &inst);
}
