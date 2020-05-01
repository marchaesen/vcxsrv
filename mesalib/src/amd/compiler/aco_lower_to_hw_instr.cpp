/*
 * Copyright © 2018 Valve Corporation
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Daniel Schürmann (daniel.schuermann@campus.tu-berlin.de)
 *
 */

#include <map>

#include "aco_ir.h"
#include "aco_builder.h"
#include "util/u_math.h"
#include "sid.h"
#include "vulkan/radv_shader.h"


namespace aco {

struct lower_context {
   Program *program;
   std::vector<aco_ptr<Instruction>> instructions;
};

aco_opcode get_reduce_opcode(chip_class chip, ReduceOp op) {
   switch (op) {
   case iadd32: return chip >= GFX9 ? aco_opcode::v_add_u32 : aco_opcode::v_add_co_u32;
   case imul32: return aco_opcode::v_mul_lo_u32;
   case fadd32: return aco_opcode::v_add_f32;
   case fmul32: return aco_opcode::v_mul_f32;
   case imax32: return aco_opcode::v_max_i32;
   case imin32: return aco_opcode::v_min_i32;
   case umin32: return aco_opcode::v_min_u32;
   case umax32: return aco_opcode::v_max_u32;
   case fmin32: return aco_opcode::v_min_f32;
   case fmax32: return aco_opcode::v_max_f32;
   case iand32: return aco_opcode::v_and_b32;
   case ixor32: return aco_opcode::v_xor_b32;
   case ior32: return aco_opcode::v_or_b32;
   case iadd64: return aco_opcode::num_opcodes;
   case imul64: return aco_opcode::num_opcodes;
   case fadd64: return aco_opcode::v_add_f64;
   case fmul64: return aco_opcode::v_mul_f64;
   case imin64: return aco_opcode::num_opcodes;
   case imax64: return aco_opcode::num_opcodes;
   case umin64: return aco_opcode::num_opcodes;
   case umax64: return aco_opcode::num_opcodes;
   case fmin64: return aco_opcode::v_min_f64;
   case fmax64: return aco_opcode::v_max_f64;
   case iand64: return aco_opcode::num_opcodes;
   case ior64: return aco_opcode::num_opcodes;
   case ixor64: return aco_opcode::num_opcodes;
   default: return aco_opcode::num_opcodes;
   }
}

void emit_vadd32(Builder& bld, Definition def, Operand src0, Operand src1)
{
   Instruction *instr = bld.vadd32(def, src0, src1, false, Operand(s2), true);
   if (instr->definitions.size() >= 2) {
      assert(instr->definitions[1].regClass() == bld.lm);
      instr->definitions[1].setFixed(vcc);
   }
}

void emit_int64_dpp_op(lower_context *ctx, PhysReg dst_reg, PhysReg src0_reg, PhysReg src1_reg,
                       PhysReg vtmp_reg, ReduceOp op,
                       unsigned dpp_ctrl, unsigned row_mask, unsigned bank_mask, bool bound_ctrl,
                       Operand *identity=NULL)
{
   Builder bld(ctx->program, &ctx->instructions);
   Definition dst[] = {Definition(dst_reg, v1), Definition(PhysReg{dst_reg+1}, v1)};
   Definition vtmp_def[] = {Definition(vtmp_reg, v1), Definition(PhysReg{vtmp_reg+1}, v1)};
   Operand src0[] = {Operand(src0_reg, v1), Operand(PhysReg{src0_reg+1}, v1)};
   Operand src1[] = {Operand(src1_reg, v1), Operand(PhysReg{src1_reg+1}, v1)};
   Operand src1_64 = Operand(src1_reg, v2);
   Operand vtmp_op[] = {Operand(vtmp_reg, v1), Operand(PhysReg{vtmp_reg+1}, v1)};
   Operand vtmp_op64 = Operand(vtmp_reg, v2);
   if (op == iadd64) {
      if (ctx->program->chip_class >= GFX10) {
         if (identity)
            bld.vop1(aco_opcode::v_mov_b32, vtmp_def[0], identity[0]);
         bld.vop1_dpp(aco_opcode::v_mov_b32, vtmp_def[0], src0[0],
                      dpp_ctrl, row_mask, bank_mask, bound_ctrl);
         bld.vop3(aco_opcode::v_add_co_u32_e64, dst[0], bld.def(bld.lm, vcc), vtmp_op[0], src1[0]);
      } else {
         bld.vop2_dpp(aco_opcode::v_add_co_u32, dst[0], bld.def(bld.lm, vcc), src0[0], src1[0],
                      dpp_ctrl, row_mask, bank_mask, bound_ctrl);
      }
      bld.vop2_dpp(aco_opcode::v_addc_co_u32, dst[1], bld.def(bld.lm, vcc), src0[1], src1[1], Operand(vcc, bld.lm),
                   dpp_ctrl, row_mask, bank_mask, bound_ctrl);
   } else if (op == iand64) {
      bld.vop2_dpp(aco_opcode::v_and_b32, dst[0], src0[0], src1[0],
                   dpp_ctrl, row_mask, bank_mask, bound_ctrl);
      bld.vop2_dpp(aco_opcode::v_and_b32, dst[1], src0[1], src1[1],
                   dpp_ctrl, row_mask, bank_mask, bound_ctrl);
   } else if (op == ior64) {
      bld.vop2_dpp(aco_opcode::v_or_b32, dst[0], src0[0], src1[0],
                   dpp_ctrl, row_mask, bank_mask, bound_ctrl);
      bld.vop2_dpp(aco_opcode::v_or_b32, dst[1], src0[1], src1[1],
                   dpp_ctrl, row_mask, bank_mask, bound_ctrl);
   } else if (op == ixor64) {
      bld.vop2_dpp(aco_opcode::v_xor_b32, dst[0], src0[0], src1[0],
                   dpp_ctrl, row_mask, bank_mask, bound_ctrl);
      bld.vop2_dpp(aco_opcode::v_xor_b32, dst[1], src0[1], src1[1],
                   dpp_ctrl, row_mask, bank_mask, bound_ctrl);
   } else if (op == umin64 || op == umax64 || op == imin64 || op == imax64) {
      aco_opcode cmp = aco_opcode::num_opcodes;
      switch (op) {
      case umin64:
         cmp = aco_opcode::v_cmp_gt_u64;
         break;
      case umax64:
         cmp = aco_opcode::v_cmp_lt_u64;
         break;
      case imin64:
         cmp = aco_opcode::v_cmp_gt_i64;
         break;
      case imax64:
         cmp = aco_opcode::v_cmp_lt_i64;
         break;
      default:
         break;
      }

      if (identity) {
         bld.vop1(aco_opcode::v_mov_b32, vtmp_def[0], identity[0]);
         bld.vop1(aco_opcode::v_mov_b32, vtmp_def[1], identity[1]);
      }
      bld.vop1_dpp(aco_opcode::v_mov_b32, vtmp_def[0], src0[0],
                   dpp_ctrl, row_mask, bank_mask, bound_ctrl);
      bld.vop1_dpp(aco_opcode::v_mov_b32, vtmp_def[1], src0[1],
                   dpp_ctrl, row_mask, bank_mask, bound_ctrl);

      bld.vopc(cmp, bld.def(bld.lm, vcc), vtmp_op64, src1_64);
      bld.vop2(aco_opcode::v_cndmask_b32, dst[0], vtmp_op[0], src1[0], Operand(vcc, bld.lm));
      bld.vop2(aco_opcode::v_cndmask_b32, dst[1], vtmp_op[1], src1[1], Operand(vcc, bld.lm));
   } else if (op == imul64) {
      /* t4 = dpp(x_hi)
       * t1 = umul_lo(t4, y_lo)
       * t3 = dpp(x_lo)
       * t0 = umul_lo(t3, y_hi)
       * t2 = iadd(t0, t1)
       * t5 = umul_hi(t3, y_lo)
       * res_hi = iadd(t2, t5)
       * res_lo = umul_lo(t3, y_lo)
       * Requires that res_hi != src0[0] and res_hi != src1[0]
       * and that vtmp[0] != res_hi.
       */
      if (identity)
         bld.vop1(aco_opcode::v_mov_b32, vtmp_def[0], identity[1]);
      bld.vop1_dpp(aco_opcode::v_mov_b32, vtmp_def[0], src0[1],
                   dpp_ctrl, row_mask, bank_mask, bound_ctrl);
      bld.vop3(aco_opcode::v_mul_lo_u32, vtmp_def[1], vtmp_op[0], src1[0]);
      if (identity)
         bld.vop1(aco_opcode::v_mov_b32, vtmp_def[0], identity[0]);
      bld.vop1_dpp(aco_opcode::v_mov_b32, vtmp_def[0], src0[0],
                   dpp_ctrl, row_mask, bank_mask, bound_ctrl);
      bld.vop3(aco_opcode::v_mul_lo_u32, vtmp_def[0], vtmp_op[0], src1[1]);
      emit_vadd32(bld, vtmp_def[1], vtmp_op[0], vtmp_op[1]);
      if (identity)
         bld.vop1(aco_opcode::v_mov_b32, vtmp_def[0], identity[0]);
      bld.vop1_dpp(aco_opcode::v_mov_b32, vtmp_def[0], src0[0],
                   dpp_ctrl, row_mask, bank_mask, bound_ctrl);
      bld.vop3(aco_opcode::v_mul_hi_u32, vtmp_def[0], vtmp_op[0], src1[0]);
      emit_vadd32(bld, dst[1], vtmp_op[1], vtmp_op[0]);
      if (identity)
         bld.vop1(aco_opcode::v_mov_b32, vtmp_def[0], identity[0]);
      bld.vop1_dpp(aco_opcode::v_mov_b32, vtmp_def[0], src0[0],
                   dpp_ctrl, row_mask, bank_mask, bound_ctrl);
      bld.vop3(aco_opcode::v_mul_lo_u32, dst[0], vtmp_op[0], src1[0]);
   }
}

void emit_int64_op(lower_context *ctx, PhysReg dst_reg, PhysReg src0_reg, PhysReg src1_reg, PhysReg vtmp, ReduceOp op)
{
   Builder bld(ctx->program, &ctx->instructions);
   Definition dst[] = {Definition(dst_reg, v1), Definition(PhysReg{dst_reg+1}, v1)};
   RegClass src0_rc = src0_reg.reg() >= 256 ? v1 : s1;
   Operand src0[] = {Operand(src0_reg, src0_rc), Operand(PhysReg{src0_reg+1}, src0_rc)};
   Operand src1[] = {Operand(src1_reg, v1), Operand(PhysReg{src1_reg+1}, v1)};
   Operand src0_64 = Operand(src0_reg, src0_reg.reg() >= 256 ? v2 : s2);
   Operand src1_64 = Operand(src1_reg, v2);

   if (src0_rc == s1 &&
       (op == imul64 || op == umin64 || op == umax64 || op == imin64 || op == imax64)) {
      assert(vtmp.reg() != 0);
      bld.vop1(aco_opcode::v_mov_b32, Definition(vtmp, v1), src0[0]);
      bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg{vtmp+1}, v1), src0[1]);
      src0_reg = vtmp;
      src0[0] = Operand(vtmp, v1);
      src0[1] = Operand(PhysReg{vtmp+1}, v1);
      src0_64 = Operand(vtmp, v2);
   } else if (src0_rc == s1 && op == iadd64) {
      assert(vtmp.reg() != 0);
      bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg{vtmp+1}, v1), src0[1]);
      src0[1] = Operand(PhysReg{vtmp+1}, v1);
   }

   if (op == iadd64) {
      if (ctx->program->chip_class >= GFX10) {
         bld.vop3(aco_opcode::v_add_co_u32_e64, dst[0], bld.def(bld.lm, vcc), src0[0], src1[0]);
      } else {
         bld.vop2(aco_opcode::v_add_co_u32, dst[0], bld.def(bld.lm, vcc), src0[0], src1[0]);
      }
      bld.vop2(aco_opcode::v_addc_co_u32, dst[1], bld.def(bld.lm, vcc), src0[1], src1[1], Operand(vcc, bld.lm));
   } else if (op == iand64) {
      bld.vop2(aco_opcode::v_and_b32, dst[0], src0[0], src1[0]);
      bld.vop2(aco_opcode::v_and_b32, dst[1], src0[1], src1[1]);
   } else if (op == ior64) {
      bld.vop2(aco_opcode::v_or_b32, dst[0], src0[0], src1[0]);
      bld.vop2(aco_opcode::v_or_b32, dst[1], src0[1], src1[1]);
   } else if (op == ixor64) {
      bld.vop2(aco_opcode::v_xor_b32, dst[0], src0[0], src1[0]);
      bld.vop2(aco_opcode::v_xor_b32, dst[1], src0[1], src1[1]);
   } else if (op == umin64 || op == umax64 || op == imin64 || op == imax64) {
      aco_opcode cmp = aco_opcode::num_opcodes;
      switch (op) {
      case umin64:
         cmp = aco_opcode::v_cmp_gt_u64;
         break;
      case umax64:
         cmp = aco_opcode::v_cmp_lt_u64;
         break;
      case imin64:
         cmp = aco_opcode::v_cmp_gt_i64;
         break;
      case imax64:
         cmp = aco_opcode::v_cmp_lt_i64;
         break;
      default:
         break;
      }

      bld.vopc(cmp, bld.def(bld.lm, vcc), src0_64, src1_64);
      bld.vop2(aco_opcode::v_cndmask_b32, dst[0], src0[0], src1[0], Operand(vcc, bld.lm));
      bld.vop2(aco_opcode::v_cndmask_b32, dst[1], src0[1], src1[1], Operand(vcc, bld.lm));
   } else if (op == imul64) {
      if (src1_reg == dst_reg) {
         /* it's fine if src0==dst but not if src1==dst */
         std::swap(src0_reg, src1_reg);
         std::swap(src0[0], src1[0]);
         std::swap(src0[1], src1[1]);
         std::swap(src0_64, src1_64);
      }
      assert(!(src0_reg == src1_reg));
      /* t1 = umul_lo(x_hi, y_lo)
       * t0 = umul_lo(x_lo, y_hi)
       * t2 = iadd(t0, t1)
       * t5 = umul_hi(x_lo, y_lo)
       * res_hi = iadd(t2, t5)
       * res_lo = umul_lo(x_lo, y_lo)
       * assumes that it's ok to modify x_hi/y_hi, since we might not have vtmp
       */
      Definition tmp0_def(PhysReg{src0_reg+1}, v1);
      Definition tmp1_def(PhysReg{src1_reg+1}, v1);
      Operand tmp0_op = src0[1];
      Operand tmp1_op = src1[1];
      bld.vop3(aco_opcode::v_mul_lo_u32, tmp0_def, src0[1], src1[0]);
      bld.vop3(aco_opcode::v_mul_lo_u32, tmp1_def, src0[0], src1[1]);
      emit_vadd32(bld, tmp0_def, tmp1_op, tmp0_op);
      bld.vop3(aco_opcode::v_mul_hi_u32, tmp1_def, src0[0], src1[0]);
      emit_vadd32(bld, dst[1], tmp0_op, tmp1_op);
      bld.vop3(aco_opcode::v_mul_lo_u32, dst[0], src0[0], src1[0]);
   }
}

void emit_dpp_op(lower_context *ctx, PhysReg dst_reg, PhysReg src0_reg, PhysReg src1_reg,
                 PhysReg vtmp, ReduceOp op, unsigned size,
                 unsigned dpp_ctrl, unsigned row_mask, unsigned bank_mask, bool bound_ctrl,
                 Operand *identity=NULL) /* for VOP3 with sparse writes */
{
   Builder bld(ctx->program, &ctx->instructions);
   RegClass rc = RegClass(RegType::vgpr, size);
   Definition dst(dst_reg, rc);
   Operand src0(src0_reg, rc);
   Operand src1(src1_reg, rc);

   aco_opcode opcode = get_reduce_opcode(ctx->program->chip_class, op);
   bool vop3 = op == imul32 || size == 2;

   if (!vop3) {
      if (opcode == aco_opcode::v_add_co_u32)
         bld.vop2_dpp(opcode, dst, bld.def(bld.lm, vcc), src0, src1, dpp_ctrl, row_mask, bank_mask, bound_ctrl);
      else
         bld.vop2_dpp(opcode, dst, src0, src1, dpp_ctrl, row_mask, bank_mask, bound_ctrl);
      return;
   }

   if (opcode == aco_opcode::num_opcodes) {
      emit_int64_dpp_op(ctx, dst_reg ,src0_reg, src1_reg, vtmp, op,
                        dpp_ctrl, row_mask, bank_mask, bound_ctrl, identity);
      return;
   }

   if (identity)
      bld.vop1(aco_opcode::v_mov_b32, Definition(vtmp, v1), identity[0]);
   if (identity && size >= 2)
      bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg{vtmp+1}, v1), identity[1]);

   for (unsigned i = 0; i < size; i++)
      bld.vop1_dpp(aco_opcode::v_mov_b32, Definition(PhysReg{vtmp+i}, v1), Operand(PhysReg{src0_reg+i}, v1),
                   dpp_ctrl, row_mask, bank_mask, bound_ctrl);

   bld.vop3(opcode, dst, Operand(vtmp, rc), src1);
}

void emit_op(lower_context *ctx, PhysReg dst_reg, PhysReg src0_reg, PhysReg src1_reg,
             PhysReg vtmp, ReduceOp op, unsigned size)
{
   Builder bld(ctx->program, &ctx->instructions);
   RegClass rc = RegClass(RegType::vgpr, size);
   Definition dst(dst_reg, rc);
   Operand src0(src0_reg, RegClass(src0_reg.reg() >= 256 ? RegType::vgpr : RegType::sgpr, size));
   Operand src1(src1_reg, rc);

   aco_opcode opcode = get_reduce_opcode(ctx->program->chip_class, op);
   bool vop3 = op == imul32 || size == 2;

   if (opcode == aco_opcode::num_opcodes) {
      emit_int64_op(ctx, dst_reg, src0_reg, src1_reg, vtmp, op);
      return;
   }

   if (vop3) {
      bld.vop3(opcode, dst, src0, src1);
   } else if (opcode == aco_opcode::v_add_co_u32) {
      bld.vop2(opcode, dst, bld.def(bld.lm, vcc), src0, src1);
   } else {
      bld.vop2(opcode, dst, src0, src1);
   }
}

void emit_dpp_mov(lower_context *ctx, PhysReg dst, PhysReg src0, unsigned size,
                  unsigned dpp_ctrl, unsigned row_mask, unsigned bank_mask, bool bound_ctrl)
{
   Builder bld(ctx->program, &ctx->instructions);
   for (unsigned i = 0; i < size; i++) {
      bld.vop1_dpp(aco_opcode::v_mov_b32, Definition(PhysReg{dst+i}, v1), Operand(PhysReg{src0+i}, v1),
                   dpp_ctrl, row_mask, bank_mask, bound_ctrl);
   }
}

uint32_t get_reduction_identity(ReduceOp op, unsigned idx)
{
   switch (op) {
   case iadd32:
   case iadd64:
   case fadd32:
   case fadd64:
   case ior32:
   case ior64:
   case ixor32:
   case ixor64:
   case umax32:
   case umax64:
      return 0;
   case imul32:
   case imul64:
      return idx ? 0 : 1;
   case fmul32:
      return 0x3f800000u; /* 1.0 */
   case fmul64:
      return idx ? 0x3ff00000u : 0u; /* 1.0 */
   case imin32:
      return INT32_MAX;
   case imin64:
      return idx ? 0x7fffffffu : 0xffffffffu;
   case imax32:
      return INT32_MIN;
   case imax64:
      return idx ? 0x80000000u : 0;
   case umin32:
   case umin64:
   case iand32:
   case iand64:
      return 0xffffffffu;
   case fmin32:
      return 0x7f800000u; /* infinity */
   case fmin64:
      return idx ? 0x7ff00000u : 0u; /* infinity */
   case fmax32:
      return 0xff800000u; /* negative infinity */
   case fmax64:
      return idx ? 0xfff00000u : 0u; /* negative infinity */
   default:
      unreachable("Invalid reduction operation");
      break;
   }
   return 0;
}

void emit_ds_swizzle(Builder bld, PhysReg dst, PhysReg src, unsigned size, unsigned ds_pattern)
{
   for (unsigned i = 0; i < size; i++) {
      bld.ds(aco_opcode::ds_swizzle_b32, Definition(PhysReg{dst+i}, v1),
             Operand(PhysReg{src+i}, v1), ds_pattern);
   }
}

void emit_reduction(lower_context *ctx, aco_opcode op, ReduceOp reduce_op, unsigned cluster_size, PhysReg tmp,
                    PhysReg stmp, PhysReg vtmp, PhysReg sitmp, Operand src, Definition dst)
{
   assert(cluster_size == ctx->program->wave_size || op == aco_opcode::p_reduce);
   assert(cluster_size <= ctx->program->wave_size);

   Builder bld(ctx->program, &ctx->instructions);

   Operand identity[2];
   identity[0] = Operand(get_reduction_identity(reduce_op, 0));
   identity[1] = Operand(get_reduction_identity(reduce_op, 1));
   Operand vcndmask_identity[2] = {identity[0], identity[1]};

   /* First, copy the source to tmp and set inactive lanes to the identity */
   bld.sop1(Builder::s_or_saveexec, Definition(stmp, bld.lm), Definition(scc, s1), Definition(exec, bld.lm), Operand(UINT64_MAX), Operand(exec, bld.lm));

   for (unsigned i = 0; i < src.size(); i++) {
      /* p_exclusive_scan needs it to be a sgpr or inline constant for the v_writelane_b32
       * except on GFX10, where v_writelane_b32 can take a literal. */
      if (identity[i].isLiteral() && op == aco_opcode::p_exclusive_scan && ctx->program->chip_class < GFX10) {
         bld.sop1(aco_opcode::s_mov_b32, Definition(PhysReg{sitmp+i}, s1), identity[i]);
         identity[i] = Operand(PhysReg{sitmp+i}, s1);

         bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg{tmp+i}, v1), identity[i]);
         vcndmask_identity[i] = Operand(PhysReg{tmp+i}, v1);
      } else if (identity[i].isLiteral()) {
         bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg{tmp+i}, v1), identity[i]);
         vcndmask_identity[i] = Operand(PhysReg{tmp+i}, v1);
      }
   }

   for (unsigned i = 0; i < src.size(); i++) {
      bld.vop2_e64(aco_opcode::v_cndmask_b32, Definition(PhysReg{tmp + i}, v1),
                   vcndmask_identity[i], Operand(PhysReg{src.physReg() + i}, v1),
                   Operand(stmp, bld.lm));
   }

   bool reduction_needs_last_op = false;
   switch (op) {
   case aco_opcode::p_reduce:
      if (cluster_size == 1) break;

      if (ctx->program->chip_class <= GFX7) {
         reduction_needs_last_op = true;
         emit_ds_swizzle(bld, vtmp, tmp, src.size(), (1 << 15) | dpp_quad_perm(1, 0, 3, 2));
         if (cluster_size == 2) break;
         emit_op(ctx, tmp, vtmp, tmp, PhysReg{0}, reduce_op, src.size());
         emit_ds_swizzle(bld, vtmp, tmp, src.size(), (1 << 15) | dpp_quad_perm(2, 3, 0, 1));
         if (cluster_size == 4) break;
         emit_op(ctx, tmp, vtmp, tmp, PhysReg{0}, reduce_op, src.size());
         emit_ds_swizzle(bld, vtmp, tmp, src.size(), ds_pattern_bitmode(0x1f, 0, 0x04));
         if (cluster_size == 8) break;
         emit_op(ctx, tmp, vtmp, tmp, PhysReg{0}, reduce_op, src.size());
         emit_ds_swizzle(bld, vtmp, tmp, src.size(), ds_pattern_bitmode(0x1f, 0, 0x08));
         if (cluster_size == 16) break;
         emit_op(ctx, tmp, vtmp, tmp, PhysReg{0}, reduce_op, src.size());
         emit_ds_swizzle(bld, vtmp, tmp, src.size(), ds_pattern_bitmode(0x1f, 0, 0x10));
         if (cluster_size == 32) break;
         emit_op(ctx, tmp, vtmp, tmp, PhysReg{0}, reduce_op, src.size());
         for (unsigned i = 0; i < src.size(); i++)
            bld.readlane(Definition(PhysReg{dst.physReg() + i}, s1), Operand(PhysReg{tmp + i}, v1), Operand(0u));
         // TODO: it would be more effective to do the last reduction step on SALU
         emit_op(ctx, tmp, dst.physReg(), tmp, vtmp, reduce_op, src.size());
         reduction_needs_last_op = false;
         break;
      }

      emit_dpp_op(ctx, tmp, tmp, tmp, vtmp, reduce_op, src.size(), dpp_quad_perm(1, 0, 3, 2), 0xf, 0xf, false);
      if (cluster_size == 2) break;
      emit_dpp_op(ctx, tmp, tmp, tmp, vtmp, reduce_op, src.size(), dpp_quad_perm(2, 3, 0, 1), 0xf, 0xf, false);
      if (cluster_size == 4) break;
      emit_dpp_op(ctx, tmp, tmp, tmp, vtmp, reduce_op, src.size(), dpp_row_half_mirror, 0xf, 0xf, false);
      if (cluster_size == 8) break;
      emit_dpp_op(ctx, tmp, tmp, tmp, vtmp, reduce_op, src.size(), dpp_row_mirror, 0xf, 0xf, false);
      if (cluster_size == 16) break;

      if (ctx->program->chip_class >= GFX10) {
         /* GFX10+ doesn't support row_bcast15 and row_bcast31 */
         for (unsigned i = 0; i < src.size(); i++)
            bld.vop3(aco_opcode::v_permlanex16_b32, Definition(PhysReg{vtmp+i}, v1), Operand(PhysReg{tmp+i}, v1), Operand(0u), Operand(0u));

         if (cluster_size == 32) {
            reduction_needs_last_op = true;
            break;
         }

         emit_op(ctx, tmp, tmp, vtmp, PhysReg{0}, reduce_op, src.size());
         for (unsigned i = 0; i < src.size(); i++)
            bld.readlane(Definition(PhysReg{dst.physReg() + i}, s1), Operand(PhysReg{tmp+i}, v1), Operand(0u));
         // TODO: it would be more effective to do the last reduction step on SALU
         emit_op(ctx, tmp, dst.physReg(), tmp, vtmp, reduce_op, src.size());
         break;
      }

      if (cluster_size == 32) {
         emit_ds_swizzle(bld, vtmp, tmp, src.size(), ds_pattern_bitmode(0x1f, 0, 0x10));
         reduction_needs_last_op = true;
         break;
      }
      assert(cluster_size == 64);
      emit_dpp_op(ctx, tmp, tmp, tmp, vtmp, reduce_op, src.size(), dpp_row_bcast15, 0xa, 0xf, false);
      emit_dpp_op(ctx, tmp, tmp, tmp, vtmp, reduce_op, src.size(), dpp_row_bcast31, 0xc, 0xf, false);
      break;
   case aco_opcode::p_exclusive_scan:
      if (ctx->program->chip_class >= GFX10) { /* gfx10 doesn't support wf_sr1, so emulate it */
         /* shift rows right */
         emit_dpp_mov(ctx, vtmp, tmp, src.size(), dpp_row_sr(1), 0xf, 0xf, true);

         /* fill in the gaps in rows 1 and 3 */
         bld.sop1(aco_opcode::s_mov_b32, Definition(exec_lo, s1), Operand(0x10000u));
         bld.sop1(aco_opcode::s_mov_b32, Definition(exec_hi, s1), Operand(0x10000u));
         for (unsigned i = 0; i < src.size(); i++) {
            Instruction *perm = bld.vop3(aco_opcode::v_permlanex16_b32,
                                         Definition(PhysReg{vtmp+i}, v1),
                                         Operand(PhysReg{tmp+i}, v1),
                                         Operand(0xffffffffu), Operand(0xffffffffu)).instr;
            static_cast<VOP3A_instruction*>(perm)->opsel = 1; /* FI (Fetch Inactive) */
         }
         bld.sop1(Builder::s_mov, Definition(exec, bld.lm), Operand(UINT64_MAX));

         if (ctx->program->wave_size == 64) {
            /* fill in the gap in row 2 */
            for (unsigned i = 0; i < src.size(); i++) {
               bld.readlane(Definition(PhysReg{sitmp+i}, s1), Operand(PhysReg{tmp+i}, v1), Operand(31u));
               bld.writelane(Definition(PhysReg{vtmp+i}, v1), Operand(PhysReg{sitmp+i}, s1), Operand(32u), Operand(PhysReg{vtmp+i}, v1));
            }
         }
         std::swap(tmp, vtmp);
      } else if (ctx->program->chip_class >= GFX8) {
         emit_dpp_mov(ctx, tmp, tmp, src.size(), dpp_wf_sr1, 0xf, 0xf, true);
      } else {
         // TODO: use LDS on CS with a single write and shifted read
         /* wavefront shift_right by 1 on SI/CI */
         emit_ds_swizzle(bld, vtmp, tmp, src.size(), (1 << 15) | dpp_quad_perm(0, 0, 1, 2));
         emit_ds_swizzle(bld, tmp, tmp, src.size(), ds_pattern_bitmode(0x1F, 0x00, 0x07)); /* mirror(8) */
         bld.sop1(aco_opcode::s_mov_b32, Definition(exec_lo, s1), Operand(0x10101010u));
         bld.sop1(aco_opcode::s_mov_b32, Definition(exec_hi, s1), Operand(exec_lo, s1));
         for (unsigned i = 0; i < src.size(); i++)
            bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg{vtmp+i}, v1), Operand(PhysReg{tmp+i}, v1));

         bld.sop1(aco_opcode::s_mov_b64, Definition(exec, s2), Operand(UINT64_MAX));
         emit_ds_swizzle(bld, tmp, tmp, src.size(), ds_pattern_bitmode(0x1F, 0x00, 0x08)); /* swap(8) */
         bld.sop1(aco_opcode::s_mov_b32, Definition(exec_lo, s1), Operand(0x01000100u));
         bld.sop1(aco_opcode::s_mov_b32, Definition(exec_hi, s1), Operand(exec_lo, s1));
         for (unsigned i = 0; i < src.size(); i++)
            bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg{vtmp+i}, v1), Operand(PhysReg{tmp+i}, v1));

         bld.sop1(aco_opcode::s_mov_b64, Definition(exec, s2), Operand(UINT64_MAX));
         emit_ds_swizzle(bld, tmp, tmp, src.size(), ds_pattern_bitmode(0x1F, 0x00, 0x10)); /* swap(16) */
         bld.sop2(aco_opcode::s_bfm_b32, Definition(exec_lo, s1), Operand(1u), Operand(16u));
         bld.sop2(aco_opcode::s_bfm_b32, Definition(exec_hi, s1), Operand(1u), Operand(16u));
         for (unsigned i = 0; i < src.size(); i++)
            bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg{vtmp+i}, v1), Operand(PhysReg{tmp+i}, v1));

         bld.sop1(aco_opcode::s_mov_b64, Definition(exec, s2), Operand(UINT64_MAX));
         for (unsigned i = 0; i < src.size(); i++) {
            bld.writelane(Definition(PhysReg{vtmp+i}, v1), identity[i], Operand(0u), Operand(PhysReg{vtmp+i}, v1));
            bld.readlane(Definition(PhysReg{sitmp+i}, s1), Operand(PhysReg{tmp+i}, v1), Operand(0u));
            bld.writelane(Definition(PhysReg{vtmp+i}, v1), Operand(PhysReg{sitmp+i}, s1), Operand(32u), Operand(PhysReg{vtmp+i}, v1));
            identity[i] = Operand(0u); /* prevent further uses of identity */
         }
         std::swap(tmp, vtmp);
      }

      for (unsigned i = 0; i < src.size(); i++) {
         if (!identity[i].isConstant() || identity[i].constantValue()) { /* bound_ctrl should take care of this overwise */
            if (ctx->program->chip_class < GFX10)
               assert((identity[i].isConstant() && !identity[i].isLiteral()) || identity[i].physReg() == PhysReg{sitmp+i});
            bld.writelane(Definition(PhysReg{tmp+i}, v1), identity[i], Operand(0u), Operand(PhysReg{tmp+i}, v1));
         }
      }
      /* fall through */
   case aco_opcode::p_inclusive_scan:
      assert(cluster_size == ctx->program->wave_size);
      if (ctx->program->chip_class <= GFX7) {
         emit_ds_swizzle(bld, vtmp, tmp, src.size(), ds_pattern_bitmode(0x1e, 0x00, 0x00));
         bld.sop1(aco_opcode::s_mov_b32, Definition(exec_lo, s1), Operand(0xAAAAAAAAu));
         bld.sop1(aco_opcode::s_mov_b32, Definition(exec_hi, s1), Operand(exec_lo, s1));
         emit_op(ctx, tmp, tmp, vtmp, PhysReg{0}, reduce_op, src.size());

         bld.sop1(aco_opcode::s_mov_b64, Definition(exec, s2), Operand(UINT64_MAX));
         emit_ds_swizzle(bld, vtmp, tmp, src.size(), ds_pattern_bitmode(0x1c, 0x01, 0x00));
         bld.sop1(aco_opcode::s_mov_b32, Definition(exec_lo, s1), Operand(0xCCCCCCCCu));
         bld.sop1(aco_opcode::s_mov_b32, Definition(exec_hi, s1), Operand(exec_lo, s1));
         emit_op(ctx, tmp, tmp, vtmp, PhysReg{0}, reduce_op, src.size());

         bld.sop1(aco_opcode::s_mov_b64, Definition(exec, s2), Operand(UINT64_MAX));
         emit_ds_swizzle(bld, vtmp, tmp, src.size(), ds_pattern_bitmode(0x18, 0x03, 0x00));
         bld.sop1(aco_opcode::s_mov_b32, Definition(exec_lo, s1), Operand(0xF0F0F0F0u));
         bld.sop1(aco_opcode::s_mov_b32, Definition(exec_hi, s1), Operand(exec_lo, s1));
         emit_op(ctx, tmp, tmp, vtmp, PhysReg{0}, reduce_op, src.size());

         bld.sop1(aco_opcode::s_mov_b64, Definition(exec, s2), Operand(UINT64_MAX));
         emit_ds_swizzle(bld, vtmp, tmp, src.size(), ds_pattern_bitmode(0x10, 0x07, 0x00));
         bld.sop1(aco_opcode::s_mov_b32, Definition(exec_lo, s1), Operand(0xFF00FF00u));
         bld.sop1(aco_opcode::s_mov_b32, Definition(exec_hi, s1), Operand(exec_lo, s1));
         emit_op(ctx, tmp, tmp, vtmp, PhysReg{0}, reduce_op, src.size());

         bld.sop1(aco_opcode::s_mov_b64, Definition(exec, s2), Operand(UINT64_MAX));
         emit_ds_swizzle(bld, vtmp, tmp, src.size(), ds_pattern_bitmode(0x00, 0x0f, 0x00));
         bld.sop2(aco_opcode::s_bfm_b32, Definition(exec_lo, s1), Operand(16u), Operand(16u));
         bld.sop2(aco_opcode::s_bfm_b32, Definition(exec_hi, s1), Operand(16u), Operand(16u));
         emit_op(ctx, tmp, tmp, vtmp, PhysReg{0}, reduce_op, src.size());

         for (unsigned i = 0; i < src.size(); i++)
            bld.readlane(Definition(PhysReg{sitmp+i}, s1), Operand(PhysReg{tmp+i}, v1), Operand(31u));
         bld.sop2(aco_opcode::s_bfm_b64, Definition(exec, s2), Operand(32u), Operand(32u));
         emit_op(ctx, tmp, sitmp, tmp, vtmp, reduce_op, src.size());
         break;
      }

      emit_dpp_op(ctx, tmp, tmp, tmp, vtmp, reduce_op, src.size(),
                  dpp_row_sr(1), 0xf, 0xf, false, identity);
      emit_dpp_op(ctx, tmp, tmp, tmp, vtmp, reduce_op, src.size(),
                  dpp_row_sr(2), 0xf, 0xf, false, identity);
      emit_dpp_op(ctx, tmp, tmp, tmp, vtmp, reduce_op, src.size(),
                  dpp_row_sr(4), 0xf, 0xf, false, identity);
      emit_dpp_op(ctx, tmp, tmp, tmp, vtmp, reduce_op, src.size(),
                  dpp_row_sr(8), 0xf, 0xf, false, identity);
      if (ctx->program->chip_class >= GFX10) {
         bld.sop2(aco_opcode::s_bfm_b32, Definition(exec_lo, s1), Operand(16u), Operand(16u));
         bld.sop2(aco_opcode::s_bfm_b32, Definition(exec_hi, s1), Operand(16u), Operand(16u));
         for (unsigned i = 0; i < src.size(); i++) {
            Instruction *perm = bld.vop3(aco_opcode::v_permlanex16_b32,
                                         Definition(PhysReg{vtmp+i}, v1),
                                         Operand(PhysReg{tmp+i}, v1),
                                         Operand(0xffffffffu), Operand(0xffffffffu)).instr;
            static_cast<VOP3A_instruction*>(perm)->opsel = 1; /* FI (Fetch Inactive) */
         }
         emit_op(ctx, tmp, tmp, vtmp, PhysReg{0}, reduce_op, src.size());

         if (ctx->program->wave_size == 64) {
            bld.sop2(aco_opcode::s_bfm_b64, Definition(exec, s2), Operand(32u), Operand(32u));
            for (unsigned i = 0; i < src.size(); i++)
               bld.readlane(Definition(PhysReg{sitmp+i}, s1), Operand(PhysReg{tmp+i}, v1), Operand(31u));
            emit_op(ctx, tmp, sitmp, tmp, vtmp, reduce_op, src.size());
         }
      } else {
         emit_dpp_op(ctx, tmp, tmp, tmp, vtmp, reduce_op, src.size(),
                     dpp_row_bcast15, 0xa, 0xf, false, identity);
         emit_dpp_op(ctx, tmp, tmp, tmp, vtmp, reduce_op, src.size(),
                     dpp_row_bcast31, 0xc, 0xf, false, identity);
      }
      break;
   default:
      unreachable("Invalid reduction mode");
   }


   if (op == aco_opcode::p_reduce) {
      if (reduction_needs_last_op && dst.regClass().type() == RegType::vgpr) {
         bld.sop1(Builder::s_mov, Definition(exec, bld.lm), Operand(stmp, bld.lm));
         emit_op(ctx, dst.physReg(), tmp, vtmp, PhysReg{0}, reduce_op, src.size());
         return;
      }

      if (reduction_needs_last_op)
         emit_op(ctx, tmp, vtmp, tmp, PhysReg{0}, reduce_op, src.size());
   }

   /* restore exec */
   bld.sop1(Builder::s_mov, Definition(exec, bld.lm), Operand(stmp, bld.lm));

   if (dst.regClass().type() == RegType::sgpr) {
      for (unsigned k = 0; k < src.size(); k++) {
         bld.readlane(Definition(PhysReg{dst.physReg() + k}, s1),
                      Operand(PhysReg{tmp + k}, v1), Operand(ctx->program->wave_size - 1));
      }
   } else if (dst.physReg() != tmp) {
      for (unsigned k = 0; k < src.size(); k++) {
         bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg{dst.physReg() + k}, v1),
                  Operand(PhysReg{tmp + k}, v1));
      }
   }
}

struct copy_operation {
   Operand op;
   Definition def;
   unsigned bytes;
   union {
      uint8_t uses[8];
      uint64_t is_used = 0;
   };
};

void split_copy(unsigned offset, Definition *def, Operand *op, const copy_operation& src, bool ignore_uses, unsigned max_size)
{
   PhysReg def_reg = src.def.physReg();
   PhysReg op_reg = src.op.physReg();
   def_reg.reg_b += offset;
   op_reg.reg_b += offset;

   max_size = MIN2(max_size, src.def.regClass().type() == RegType::vgpr ? 4 : 8);

   /* make sure the size is a power of two and reg % bytes == 0 */
   unsigned bytes = 1;
   for (; bytes <= max_size; bytes *= 2) {
      unsigned next = bytes * 2u;
      bool can_increase = def_reg.reg_b % next == 0 &&
                          offset + next <= src.bytes && next <= max_size;
      if (!src.op.isConstant() && can_increase)
         can_increase = op_reg.reg_b % next == 0;
      for (unsigned i = 0; !ignore_uses && can_increase && (i < bytes); i++)
         can_increase = (src.uses[offset + bytes + i] == 0) == (src.uses[offset] == 0);
      if (!can_increase)
         break;
   }

   RegClass def_cls = bytes % 4 == 0 ? RegClass(src.def.regClass().type(), bytes / 4u) :
                      RegClass(src.def.regClass().type(), bytes).as_subdword();
   *def = Definition(src.def.tempId(), def_reg, def_cls);
   if (src.op.isConstant()) {
      assert(offset == 0 || (offset == 4 && src.op.bytes() == 8));
      if (src.op.bytes() == 8 && bytes == 4)
         *op = Operand(uint32_t(src.op.constantValue64() >> (offset * 8u)));
      else
         *op  = src.op;
   } else {
      RegClass op_cls = bytes % 4 == 0 ? RegClass(src.op.regClass().type(), bytes / 4u) :
                        RegClass(src.op.regClass().type(), bytes).as_subdword();
      *op = Operand(op_reg, op_cls);
      op->setTemp(Temp(src.op.tempId(), op_cls));
   }
}

uint32_t get_intersection_mask(int a_start, int a_size,
                               int b_start, int b_size)
{
   int intersection_start = MAX2(b_start - a_start, 0);
   int intersection_end = MAX2(b_start + b_size - a_start, 0);
   if (intersection_start >= a_size || intersection_end == 0)
      return 0;

   uint32_t mask = u_bit_consecutive(0, a_size);
   return u_bit_consecutive(intersection_start, intersection_end - intersection_start) & mask;
}

bool do_copy(lower_context* ctx, Builder& bld, const copy_operation& copy, bool *preserve_scc)
{
   bool did_copy = false;
   for (unsigned offset = 0; offset < copy.bytes;) {
      if (copy.uses[offset]) {
         offset++;
         continue;
      }

      Definition def;
      Operand op;
      split_copy(offset, &def, &op, copy, false, 8);

      if (def.physReg() == scc) {
         bld.sopc(aco_opcode::s_cmp_lg_i32, def, op, Operand(0u));
         *preserve_scc = true;
      } else if (def.bytes() == 8 && def.getTemp().type() == RegType::sgpr) {
         bld.sop1(aco_opcode::s_mov_b64, def, Operand(op.physReg(), s2));
      } else {
         bld.copy(def, op);
      }

      ctx->program->statistics[statistic_copies]++;

      did_copy = true;
      offset += def.bytes();
   }
   return did_copy;
}

void do_swap(lower_context *ctx, Builder& bld, const copy_operation& copy, bool preserve_scc, Pseudo_instruction *pi)
{
   unsigned offset = 0;

   if (copy.bytes == 3 && (copy.def.physReg().reg_b % 4 <= 1) &&
       (copy.def.physReg().reg_b % 4) == (copy.op.physReg().reg_b % 4)) {
      /* instead of doing a 2-byte and 1-byte swap, do a 4-byte swap and then fixup with a 1-byte swap */
      PhysReg op = copy.op.physReg();
      PhysReg def = copy.def.physReg();
      op.reg_b &= ~0x3;
      def.reg_b &= ~0x3;

      copy_operation tmp;
      tmp.op = Operand(op, v1);
      tmp.def = Definition(def, v1);
      tmp.bytes = 4;
      memset(tmp.uses, 1, 4);
      do_swap(ctx, bld, tmp, preserve_scc, pi);

      op.reg_b += copy.def.physReg().reg_b % 4 == 0 ? 3 : 0;
      def.reg_b += copy.def.physReg().reg_b % 4 == 0 ? 3 : 0;
      tmp.op = Operand(op, v1b);
      tmp.def = Definition(def, v1b);
      tmp.bytes = 1;
      tmp.uses[0] = 1;
      do_swap(ctx, bld, tmp, preserve_scc, pi);

      offset = copy.bytes;
   }

   for (; offset < copy.bytes;) {
      Definition def;
      Operand op;
      split_copy(offset, &def, &op, copy, true, 8);

      assert(op.regClass() == def.regClass());
      Operand def_as_op = Operand(def.physReg(), def.regClass());
      Definition op_as_def = Definition(op.physReg(), op.regClass());
      if (ctx->program->chip_class >= GFX9 && def.regClass() == v1) {
         bld.vop1(aco_opcode::v_swap_b32, def, op_as_def, op, def_as_op);
         ctx->program->statistics[statistic_copies]++;
      } else if (def.regClass() == v1) {
         bld.vop2(aco_opcode::v_xor_b32, op_as_def, op, def_as_op);
         bld.vop2(aco_opcode::v_xor_b32, def, op, def_as_op);
         bld.vop2(aco_opcode::v_xor_b32, op_as_def, op, def_as_op);
         ctx->program->statistics[statistic_copies] += 3;
      } else if (op.physReg() == scc || def.physReg() == scc) {
         /* we need to swap scc and another sgpr */
         assert(!preserve_scc);

         PhysReg other = op.physReg() == scc ? def.physReg() : op.physReg();

         bld.sop1(aco_opcode::s_mov_b32, Definition(pi->scratch_sgpr, s1), Operand(scc, s1));
         bld.sopc(aco_opcode::s_cmp_lg_i32, Definition(scc, s1), Operand(other, s1), Operand(0u));
         bld.sop1(aco_opcode::s_mov_b32, Definition(other, s1), Operand(pi->scratch_sgpr, s1));
         ctx->program->statistics[statistic_copies] += 3;
      } else if (def.regClass() == s1) {
         if (preserve_scc) {
            bld.sop1(aco_opcode::s_mov_b32, Definition(pi->scratch_sgpr, s1), op);
            bld.sop1(aco_opcode::s_mov_b32, op_as_def, def_as_op);
            bld.sop1(aco_opcode::s_mov_b32, def, Operand(pi->scratch_sgpr, s1));
         } else {
            bld.sop2(aco_opcode::s_xor_b32, op_as_def, Definition(scc, s1), op, def_as_op);
            bld.sop2(aco_opcode::s_xor_b32, def, Definition(scc, s1), op, def_as_op);
            bld.sop2(aco_opcode::s_xor_b32, op_as_def, Definition(scc, s1), op, def_as_op);
         }
         ctx->program->statistics[statistic_copies] += 3;
      } else if (def.regClass() == s2) {
         if (preserve_scc)
            bld.sop1(aco_opcode::s_mov_b32, Definition(pi->scratch_sgpr, s1), Operand(scc, s1));
         bld.sop2(aco_opcode::s_xor_b64, op_as_def, Definition(scc, s1), op, def_as_op);
         bld.sop2(aco_opcode::s_xor_b64, def, Definition(scc, s1), op, def_as_op);
         bld.sop2(aco_opcode::s_xor_b64, op_as_def, Definition(scc, s1), op, def_as_op);
         if (preserve_scc)
            bld.sopc(aco_opcode::s_cmp_lg_i32, Definition(scc, s1), Operand(pi->scratch_sgpr, s1), Operand(0u));
         ctx->program->statistics[statistic_copies] += 3;
      } else if (ctx->program->chip_class >= GFX9 && def.bytes() == 2 && def.physReg().reg() == op.physReg().reg()) {
         aco_ptr<VOP3P_instruction> vop3p{create_instruction<VOP3P_instruction>(aco_opcode::v_pk_add_u16, Format::VOP3P, 2, 1)};
         vop3p->operands[0] = Operand(PhysReg{op.physReg().reg()}, v1);
         vop3p->operands[1] = Operand(0u);
         vop3p->definitions[0] = Definition(PhysReg{op.physReg().reg()}, v1);
         vop3p->opsel_lo = 0x1;
         vop3p->opsel_hi = 0x2;
         bld.insert(std::move(vop3p));
      } else {
         assert(def.regClass().is_subdword());
         bld.vop2_sdwa(aco_opcode::v_xor_b32, op_as_def, op, def_as_op);
         bld.vop2_sdwa(aco_opcode::v_xor_b32, def, op, def_as_op);
         bld.vop2_sdwa(aco_opcode::v_xor_b32, op_as_def, op, def_as_op);
         ctx->program->statistics[statistic_copies] += 3;
      }

      offset += def.bytes();
   }

   /* fixup in case we swapped bytes we shouldn't have */
   copy_operation tmp_copy = copy;
   tmp_copy.op.setFixed(copy.def.physReg());
   tmp_copy.def.setFixed(copy.op.physReg());
   do_copy(ctx, bld, tmp_copy, &preserve_scc);
}

void handle_operands(std::map<PhysReg, copy_operation>& copy_map, lower_context* ctx, chip_class chip_class, Pseudo_instruction *pi)
{
   Builder bld(ctx->program, &ctx->instructions);
   aco_ptr<Instruction> mov;
   std::map<PhysReg, copy_operation>::iterator it = copy_map.begin();
   std::map<PhysReg, copy_operation>::iterator target;
   bool writes_scc = false;

   /* count the number of uses for each dst reg */
   while (it != copy_map.end()) {

      if (it->second.def.physReg() == scc)
         writes_scc = true;

      assert(!pi->tmp_in_scc || !(it->second.def.physReg() == pi->scratch_sgpr));

      /* if src and dst reg are the same, remove operation */
      if (it->first == it->second.op.physReg()) {
         it = copy_map.erase(it);
         continue;
      }

      /* split large copies */
      if (it->second.bytes > 8) {
         assert(!it->second.op.isConstant());
         assert(!it->second.def.regClass().is_subdword());
         RegClass rc = RegClass(it->second.def.regClass().type(), it->second.def.size() - 2);
         Definition hi_def = Definition(PhysReg{it->first + 2}, rc);
         rc = RegClass(it->second.op.regClass().type(), it->second.op.size() - 2);
         Operand hi_op = Operand(PhysReg{it->second.op.physReg() + 2}, rc);
         copy_operation copy = {hi_op, hi_def, it->second.bytes - 8};
         copy_map[hi_def.physReg()] = copy;
         assert(it->second.op.physReg().byte() == 0 && it->second.def.physReg().byte() == 0);
         it->second.op = Operand(it->second.op.physReg(), it->second.op.regClass().type() == RegType::sgpr ? s2 : v2);
         it->second.def = Definition(it->second.def.physReg(), it->second.def.regClass().type() == RegType::sgpr ? s2 : v2);
         it->second.bytes = 8;
      }

      /* check if the definition reg is used by another copy operation */
      for (std::pair<const PhysReg, copy_operation>& copy : copy_map) {
         if (copy.second.op.isConstant())
            continue;
         for (uint16_t i = 0; i < it->second.bytes; i++) {
            /* distance might underflow */
            unsigned distance = it->first.reg_b + i - copy.second.op.physReg().reg_b;
            if (distance < copy.second.bytes)
               it->second.uses[i] += 1;
         }
      }

      ++it;
   }

   /* first, handle paths in the location transfer graph */
   bool preserve_scc = pi->tmp_in_scc && !writes_scc;
   it = copy_map.begin();
   while (it != copy_map.end()) {

      /* try to coalesce 32-bit sgpr copies to 64-bit copies */
      if (it->second.is_used == 0 &&
          it->second.def.getTemp().type() == RegType::sgpr && it->second.bytes == 4 &&
          !it->second.op.isConstant() && it->first % 2 == it->second.op.physReg() % 2) {

         PhysReg other_def_reg = PhysReg{it->first % 2 ? it->first - 1 : it->first + 1};
         PhysReg other_op_reg = PhysReg{it->first % 2 ? it->second.op.physReg() - 1 : it->second.op.physReg() + 1};
         std::map<PhysReg, copy_operation>::iterator other = copy_map.find(other_def_reg);

         if (other != copy_map.end() && !other->second.is_used && other->second.bytes == 4 &&
             other->second.op.physReg() == other_op_reg && !other->second.op.isConstant()) {
            std::map<PhysReg, copy_operation>::iterator to_erase = it->first % 2 ? it : other;
            it = it->first % 2 ? other : it;
            copy_map.erase(to_erase);
            it->second.bytes = 8;
         }
      }
      // TODO: try to coalesce subdword copies

      /* find portions where the target reg is not used as operand for any other copy */
      if (it->second.is_used) {
         if (it->second.op.isConstant()) {
            /* we have to skip constants until is_used=0 */
            ++it;
            continue;
         }

         unsigned has_zero_use_bytes = 0;
         for (unsigned i = 0; i < it->second.bytes; i++)
            has_zero_use_bytes |= (it->second.uses[i] == 0) << i;

         if (has_zero_use_bytes) {
            /* Skipping partial copying and doing a v_swap_b32 and then fixup
             * copies is usually beneficial for sub-dword copies, but if doing
             * a partial copy allows further copies, it should be done instead. */
            bool partial_copy = (has_zero_use_bytes == 0xf) || (has_zero_use_bytes == 0xf0);
            for (std::pair<const PhysReg, copy_operation>& copy : copy_map) {
               if (partial_copy)
                  break;
               for (uint16_t i = 0; i < copy.second.bytes; i++) {
                  /* distance might underflow */
                  unsigned distance = copy.first.reg_b + i - it->second.op.physReg().reg_b;
                  if (distance < it->second.bytes && copy.second.uses[i] == 1 &&
                      !it->second.uses[distance])
                     partial_copy = true;
               }
            }

            if (!partial_copy) {
               ++it;
               continue;
            }
         } else {
            /* full target reg is used: register swapping needed */
            ++it;
            continue;
         }
      }

      bool did_copy = do_copy(ctx, bld, it->second, &preserve_scc);

      std::pair<PhysReg, copy_operation> copy = *it;

      if (it->second.is_used == 0) {
         /* the target reg is not used as operand for any other copy, so we
          * copied to all of it */
         copy_map.erase(it);
         it = copy_map.begin();
      } else {
         /* we only performed some portions of this copy, so split it to only
          * leave the portions that still need to be done */
         copy_operation original = it->second; /* the map insertion below can overwrite this */
         copy_map.erase(it);
         for (unsigned offset = 0; offset < original.bytes;) {
            if (original.uses[offset] == 0) {
               offset++;
               continue;
            }
            Definition def;
            Operand op;
            split_copy(offset, &def, &op, original, false, 8);

            copy_operation copy = {op, def, def.bytes()};
            for (unsigned i = 0; i < copy.bytes; i++)
               copy.uses[i] = original.uses[i + offset];
            copy_map[def.physReg()] = copy;

            offset += def.bytes();
         }

         it = copy_map.begin();
      }

      /* Reduce the number of uses of the operand reg by one. Do this after
       * splitting the copy or removing it in case the copy writes to it's own
       * operand (for example, v[7:8] = v[8:9]) */
      if (did_copy && !copy.second.op.isConstant()) {
         for (std::pair<const PhysReg, copy_operation>& other : copy_map) {
             for (uint16_t i = 0; i < other.second.bytes; i++) {
               /* distance might underflow */
               unsigned distance = other.first.reg_b + i - copy.second.op.physReg().reg_b;
               if (distance < copy.second.bytes && !copy.second.uses[distance])
                  other.second.uses[i] -= 1;
            }
         }
      }
   }

   if (copy_map.empty())
      return;

   /* all target regs are needed as operand somewhere which means, all entries are part of a cycle */
   unsigned largest = 0;
   for (const std::pair<PhysReg, copy_operation>& op : copy_map)
      largest = MAX2(largest, op.second.bytes);

   while (!copy_map.empty()) {

      /* Perform larger swaps first, so that we don't have to split the uses of
       * registers we swap (we don't have to because of alignment restrictions) and
       * larger swaps swaps can make other swaps unnecessary. */
      auto it = copy_map.begin();
      for (auto it2 = copy_map.begin(); it2 != copy_map.end(); ++it2) {
         if (it2->second.bytes > it->second.bytes) {
            it = it2;
            if (it->second.bytes == largest)
               break;
         }
      }

      /* should already be done */
      assert(!it->second.op.isConstant());

      assert(it->second.op.isFixed());
      assert(it->second.def.regClass() == it->second.op.regClass());

      if (it->first == it->second.op.physReg()) {
         copy_map.erase(it);
         continue;
      }

      if (preserve_scc && it->second.def.getTemp().type() == RegType::sgpr)
         assert(!(it->second.def.physReg() == pi->scratch_sgpr));

      /* to resolve the cycle, we have to swap the src reg with the dst reg */
      copy_operation swap = it->second;

      /* if this is self-intersecting, we have to split it because
       * self-intersecting swaps don't make sense */
      PhysReg lower = swap.def.physReg();
      PhysReg higher = swap.op.physReg();
      if (lower.reg_b > higher.reg_b)
         std::swap(lower, higher);
      if (higher.reg_b - lower.reg_b < (int)swap.bytes) {
         unsigned offset = higher.reg_b - lower.reg_b;
         RegType type = swap.def.regClass().type();

         copy_operation middle;
         lower.reg_b += offset;
         higher.reg_b += offset;
         middle.bytes = swap.bytes - offset * 2;
         memcpy(middle.uses, swap.uses + offset, middle.bytes);
         middle.op = Operand(lower, RegClass::get(type, middle.bytes));
         middle.def = Definition(higher, RegClass::get(type, middle.bytes));
         copy_map[higher] = middle;

         copy_operation end;
         lower.reg_b += middle.bytes;
         higher.reg_b += middle.bytes;
         end.bytes = swap.bytes - (offset + middle.bytes);
         memcpy(end.uses, swap.uses + offset + middle.bytes, end.bytes);
         end.op = Operand(lower, RegClass::get(type, end.bytes));
         end.def = Definition(higher, RegClass::get(type, end.bytes));
         copy_map[higher] = end;

         memset(swap.uses + offset, 0, swap.bytes - offset);
         swap.bytes = offset;
      }

      do_swap(ctx, bld, swap, preserve_scc, pi);

      /* remove from map */
      copy_map.erase(it);

      /* change the operand reg of the target's use and split uses if needed */
      target = copy_map.begin();
      uint32_t bytes_left = u_bit_consecutive(0, swap.bytes);
      for (; target != copy_map.end(); ++target) {
         if (target->second.op.physReg() == swap.def.physReg() && swap.bytes == target->second.bytes) {
            target->second.op.setFixed(swap.op.physReg());
            break;
         }

         uint32_t imask = get_intersection_mask(swap.def.physReg().reg_b, swap.bytes,
                                                target->second.op.physReg().reg_b, target->second.bytes);

         if (!imask)
            continue;

         assert(target->second.bytes < swap.bytes);

         PhysReg new_reg = swap.op.physReg();
         new_reg.reg_b += target->second.op.physReg().reg_b - swap.def.physReg().reg_b;
         target->second.op.setFixed(new_reg);

         bytes_left &= ~imask;
         if (!bytes_left)
            break;
      }
   }
}

void lower_to_hw_instr(Program* program)
{
   Block *discard_block = NULL;

   for (size_t i = 0; i < program->blocks.size(); i++)
   {
      Block *block = &program->blocks[i];
      lower_context ctx;
      ctx.program = program;
      Builder bld(program, &ctx.instructions);

      bool set_mode = i == 0 && block->fp_mode.val != program->config->float_mode;
      for (unsigned pred : block->linear_preds) {
         if (program->blocks[pred].fp_mode.val != block->fp_mode.val) {
            set_mode = true;
            break;
         }
      }
      if (set_mode) {
         /* only allow changing modes at top-level blocks so this doesn't break
          * the "jump over empty blocks" optimization */
         assert(block->kind & block_kind_top_level);
         uint32_t mode = block->fp_mode.val;
         /* "((size - 1) << 11) | register" (MODE is encoded as register 1) */
         bld.sopk(aco_opcode::s_setreg_imm32_b32, Operand(mode), (7 << 11) | 1);
      }

      for (size_t j = 0; j < block->instructions.size(); j++) {
         aco_ptr<Instruction>& instr = block->instructions[j];
         aco_ptr<Instruction> mov;
         if (instr->format == Format::PSEUDO) {
            Pseudo_instruction *pi = (Pseudo_instruction*)instr.get();

            switch (instr->opcode)
            {
            case aco_opcode::p_extract_vector:
            {
               PhysReg reg = instr->operands[0].physReg();
               Definition& def = instr->definitions[0];
               reg.reg_b += instr->operands[1].constantValue() * def.bytes();

               if (reg == def.physReg())
                  break;

               RegClass op_rc = def.regClass().is_subdword() ? def.regClass() :
                                RegClass(instr->operands[0].getTemp().type(), def.size());
               std::map<PhysReg, copy_operation> copy_operations;
               copy_operations[def.physReg()] = {Operand(reg, op_rc), def, def.bytes()};
               handle_operands(copy_operations, &ctx, program->chip_class, pi);
               break;
            }
            case aco_opcode::p_create_vector:
            {
               std::map<PhysReg, copy_operation> copy_operations;
               PhysReg reg = instr->definitions[0].physReg();

               for (const Operand& op : instr->operands) {
                  if (op.isConstant()) {
                     const Definition def = Definition(reg, RegClass(instr->definitions[0].getTemp().type(), op.size()));
                     copy_operations[reg] = {op, def, op.bytes()};
                     reg.reg_b += op.bytes();
                     continue;
                  }
                  if (op.isUndefined()) {
                     // TODO: coalesce subdword copies if dst byte is 0
                     reg.reg_b += op.bytes();
                     continue;
                  }

                  RegClass rc_def = op.regClass().is_subdword() ? op.regClass() :
                                    RegClass(instr->definitions[0].getTemp().type(), op.size());
                  const Definition def = Definition(reg, rc_def);
                  copy_operations[def.physReg()] = {op, def, op.bytes()};
                  reg.reg_b += op.bytes();
               }
               handle_operands(copy_operations, &ctx, program->chip_class, pi);
               break;
            }
            case aco_opcode::p_split_vector:
            {
               std::map<PhysReg, copy_operation> copy_operations;
               PhysReg reg = instr->operands[0].physReg();

               for (const Definition& def : instr->definitions) {
                  RegClass rc_op = def.regClass().is_subdword() ? def.regClass() :
                                   RegClass(instr->operands[0].getTemp().type(), def.size());
                  const Operand op = Operand(reg, rc_op);
                  copy_operations[def.physReg()] = {op, def, def.bytes()};
                  reg.reg_b += def.bytes();
               }
               handle_operands(copy_operations, &ctx, program->chip_class, pi);
               break;
            }
            case aco_opcode::p_parallelcopy:
            case aco_opcode::p_wqm:
            {
               std::map<PhysReg, copy_operation> copy_operations;
               for (unsigned i = 0; i < instr->operands.size(); i++) {
                  assert(instr->definitions[i].bytes() == instr->operands[i].bytes());
                  copy_operations[instr->definitions[i].physReg()] = {instr->operands[i], instr->definitions[i], instr->operands[i].bytes()};
               }
               handle_operands(copy_operations, &ctx, program->chip_class, pi);
               break;
            }
            case aco_opcode::p_exit_early_if:
            {
               /* don't bother with an early exit near the end of the program */
               if ((block->instructions.size() - 1 - j) <= 4 &&
                    block->instructions.back()->opcode == aco_opcode::s_endpgm) {
                  unsigned null_exp_dest = (ctx.program->stage & hw_fs) ? 9 /* NULL */ : V_008DFC_SQ_EXP_POS;
                  bool ignore_early_exit = true;

                  for (unsigned k = j + 1; k < block->instructions.size(); ++k) {
                     const aco_ptr<Instruction> &instr = block->instructions[k];
                     if (instr->opcode == aco_opcode::s_endpgm ||
                         instr->opcode == aco_opcode::p_logical_end)
                        continue;
                     else if (instr->opcode == aco_opcode::exp &&
                              static_cast<Export_instruction *>(instr.get())->dest == null_exp_dest)
                        continue;
                     else if (instr->opcode == aco_opcode::p_parallelcopy &&
                         instr->definitions[0].isFixed() &&
                         instr->definitions[0].physReg() == exec)
                        continue;

                     ignore_early_exit = false;
                  }

                  if (ignore_early_exit)
                     break;
               }

               if (!discard_block) {
                  discard_block = program->create_and_insert_block();
                  block = &program->blocks[i];

                  bld.reset(discard_block);
                  bld.exp(aco_opcode::exp, Operand(v1), Operand(v1), Operand(v1), Operand(v1),
                          0, V_008DFC_SQ_EXP_NULL, false, true, true);
                  if (program->wb_smem_l1_on_end)
                     bld.smem(aco_opcode::s_dcache_wb);
                  bld.sopp(aco_opcode::s_endpgm);

                  bld.reset(&ctx.instructions);
               }

               //TODO: exec can be zero here with block_kind_discard

               assert(instr->operands[0].physReg() == scc);
               bld.sopp(aco_opcode::s_cbranch_scc0, instr->operands[0], discard_block->index);

               discard_block->linear_preds.push_back(block->index);
               block->linear_succs.push_back(discard_block->index);
               break;
            }
            case aco_opcode::p_spill:
            {
               assert(instr->operands[0].regClass() == v1.as_linear());
               for (unsigned i = 0; i < instr->operands[2].size(); i++)
                  bld.writelane(bld.def(v1, instr->operands[0].physReg()),
                                Operand(PhysReg{instr->operands[2].physReg() + i}, s1),
                                Operand(instr->operands[1].constantValue() + i),
                                instr->operands[0]);
               break;
            }
            case aco_opcode::p_reload:
            {
               assert(instr->operands[0].regClass() == v1.as_linear());
               for (unsigned i = 0; i < instr->definitions[0].size(); i++)
                  bld.readlane(bld.def(s1, PhysReg{instr->definitions[0].physReg() + i}),
                               instr->operands[0],
                               Operand(instr->operands[1].constantValue() + i));
               break;
            }
            case aco_opcode::p_as_uniform:
            {
               if (instr->operands[0].isConstant() || instr->operands[0].regClass().type() == RegType::sgpr) {
                  std::map<PhysReg, copy_operation> copy_operations;
                  copy_operations[instr->definitions[0].physReg()] = {instr->operands[0], instr->definitions[0], instr->definitions[0].bytes()};
                  handle_operands(copy_operations, &ctx, program->chip_class, pi);
               } else {
                  assert(instr->operands[0].regClass().type() == RegType::vgpr);
                  assert(instr->definitions[0].regClass().type() == RegType::sgpr);
                  assert(instr->operands[0].size() == instr->definitions[0].size());
                  for (unsigned i = 0; i < instr->definitions[0].size(); i++) {
                     bld.vop1(aco_opcode::v_readfirstlane_b32,
                              bld.def(s1, PhysReg{instr->definitions[0].physReg() + i}),
                              Operand(PhysReg{instr->operands[0].physReg() + i}, v1));
                  }
               }
               break;
            }
            default:
               break;
            }
         } else if (instr->format == Format::PSEUDO_BRANCH) {
            Pseudo_branch_instruction* branch = static_cast<Pseudo_branch_instruction*>(instr.get());
            /* check if all blocks from current to target are empty */
            bool can_remove = block->index < branch->target[0];
            for (unsigned i = block->index + 1; can_remove && i < branch->target[0]; i++) {
               if (program->blocks[i].instructions.size())
                  can_remove = false;
            }
            if (can_remove)
               continue;

            switch (instr->opcode) {
               case aco_opcode::p_branch:
                  assert(block->linear_succs[0] == branch->target[0]);
                  bld.sopp(aco_opcode::s_branch, branch->target[0]);
                  break;
               case aco_opcode::p_cbranch_nz:
                  assert(block->linear_succs[1] == branch->target[0]);
                  if (branch->operands[0].physReg() == exec)
                     bld.sopp(aco_opcode::s_cbranch_execnz, branch->target[0]);
                  else if (branch->operands[0].physReg() == vcc)
                     bld.sopp(aco_opcode::s_cbranch_vccnz, branch->target[0]);
                  else {
                     assert(branch->operands[0].physReg() == scc);
                     bld.sopp(aco_opcode::s_cbranch_scc1, branch->target[0]);
                  }
                  break;
               case aco_opcode::p_cbranch_z:
                  assert(block->linear_succs[1] == branch->target[0]);
                  if (branch->operands[0].physReg() == exec)
                     bld.sopp(aco_opcode::s_cbranch_execz, branch->target[0]);
                  else if (branch->operands[0].physReg() == vcc)
                     bld.sopp(aco_opcode::s_cbranch_vccz, branch->target[0]);
                  else {
                     assert(branch->operands[0].physReg() == scc);
                     bld.sopp(aco_opcode::s_cbranch_scc0, branch->target[0]);
                  }
                  break;
               default:
                  unreachable("Unknown Pseudo branch instruction!");
            }

         } else if (instr->format == Format::PSEUDO_REDUCTION) {
            Pseudo_reduction_instruction* reduce = static_cast<Pseudo_reduction_instruction*>(instr.get());
            if (reduce->reduce_op == gfx10_wave64_bpermute) {
               /* Only makes sense on GFX10 wave64 */
               assert(program->chip_class >= GFX10);
               assert(program->info->wave_size == 64);
               assert(instr->definitions[0].regClass() == v1); /* Destination */
               assert(instr->definitions[1].regClass() == s2); /* Temp EXEC */
               assert(instr->definitions[1].physReg() != vcc);
               assert(instr->definitions[2].physReg() == scc); /* SCC clobber */
               assert(instr->operands[0].physReg() == vcc); /* Compare */
               assert(instr->operands[1].regClass() == v2.as_linear()); /* Temp VGPR pair */
               assert(instr->operands[2].regClass() == v1); /* Indices x4 */
               assert(instr->operands[3].regClass() == v1); /* Input data */

               PhysReg shared_vgpr_reg_lo = PhysReg(align(program->config->num_vgprs, 4) + 256);
               PhysReg shared_vgpr_reg_hi = PhysReg(shared_vgpr_reg_lo + 1);
               Operand compare = instr->operands[0];
               Operand tmp1(instr->operands[1].physReg(), v1);
               Operand tmp2(PhysReg(instr->operands[1].physReg() + 1), v1);
               Operand index_x4 = instr->operands[2];
               Operand input_data = instr->operands[3];
               Definition shared_vgpr_lo(shared_vgpr_reg_lo, v1);
               Definition shared_vgpr_hi(shared_vgpr_reg_hi, v1);
               Definition def_temp1(tmp1.physReg(), v1);
               Definition def_temp2(tmp2.physReg(), v1);

               /* Save EXEC and set it for all lanes */
               bld.sop1(aco_opcode::s_or_saveexec_b64, instr->definitions[1], instr->definitions[2],
                        Definition(exec, s2), Operand((uint64_t)-1), Operand(exec, s2));

               /* HI: Copy data from high lanes 32-63 to shared vgpr */
               bld.vop1_dpp(aco_opcode::v_mov_b32, shared_vgpr_hi, input_data, dpp_quad_perm(0, 1, 2, 3), 0xc, 0xf, false);

               /* LO: Copy data from low lanes 0-31 to shared vgpr */
               bld.vop1_dpp(aco_opcode::v_mov_b32, shared_vgpr_lo, input_data, dpp_quad_perm(0, 1, 2, 3), 0x3, 0xf, false);
               /* LO: Copy shared vgpr (high lanes' data) to output vgpr */
               bld.vop1_dpp(aco_opcode::v_mov_b32, def_temp1, Operand(shared_vgpr_reg_hi, v1), dpp_quad_perm(0, 1, 2, 3), 0x3, 0xf, false);

               /* HI: Copy shared vgpr (low lanes' data) to output vgpr */
               bld.vop1_dpp(aco_opcode::v_mov_b32, def_temp1, Operand(shared_vgpr_reg_lo, v1), dpp_quad_perm(0, 1, 2, 3), 0xc, 0xf, false);

               /* Permute the original input */
               bld.ds(aco_opcode::ds_bpermute_b32, def_temp2, index_x4, input_data);
               /* Permute the swapped input */
               bld.ds(aco_opcode::ds_bpermute_b32, def_temp1, index_x4, tmp1);

               /* Restore saved EXEC */
               bld.sop1(aco_opcode::s_mov_b64, Definition(exec, s2), Operand(instr->definitions[1].physReg(), s2));
               /* Choose whether to use the original or swapped */
               bld.vop2(aco_opcode::v_cndmask_b32, instr->definitions[0], tmp1, tmp2, compare);
            } else {
               emit_reduction(&ctx, reduce->opcode, reduce->reduce_op, reduce->cluster_size,
                              reduce->operands[1].physReg(), // tmp
                              reduce->definitions[1].physReg(), // stmp
                              reduce->operands[2].physReg(), // vtmp
                              reduce->definitions[2].physReg(), // sitmp
                              reduce->operands[0], reduce->definitions[0]);
            }
         } else {
            ctx.instructions.emplace_back(std::move(instr));
         }

      }
      block->instructions.swap(ctx.instructions);
   }
}

}
