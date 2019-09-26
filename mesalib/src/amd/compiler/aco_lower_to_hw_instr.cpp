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


namespace aco {

struct lower_context {
   Program *program;
   std::vector<aco_ptr<Instruction>> instructions;
};

void emit_dpp_op(lower_context *ctx, PhysReg dst, PhysReg src0, PhysReg src1, PhysReg vtmp, PhysReg wrtmp,
                 aco_opcode op, Format format, bool clobber_vcc, unsigned dpp_ctrl,
                 unsigned row_mask, unsigned bank_mask, bool bound_ctrl_zero, unsigned size,
                 Operand *identity=NULL) /* for VOP3 with sparse writes */
{
   RegClass rc = RegClass(RegType::vgpr, size);
   if (format == Format::VOP3) {
      Builder bld(ctx->program, &ctx->instructions);

      if (identity)
         bld.vop1(aco_opcode::v_mov_b32, Definition(vtmp, v1), identity[0]);
      if (identity && size >= 2)
         bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg{vtmp+1}, v1), identity[1]);

      for (unsigned i = 0; i < size; i++)
         bld.vop1_dpp(aco_opcode::v_mov_b32, Definition(PhysReg{vtmp+i}, v1), Operand(PhysReg{src0+i}, v1),
                      dpp_ctrl, row_mask, bank_mask, bound_ctrl_zero);

      if (clobber_vcc)
         bld.vop3(op, Definition(dst, rc), Definition(vcc, s2), Operand(vtmp, rc), Operand(src1, rc));
      else
         bld.vop3(op, Definition(dst, rc), Operand(vtmp, rc), Operand(src1, rc));
   } else {
      assert(format == Format::VOP2 || format == Format::VOP1);
      assert(size == 1 || (op == aco_opcode::v_mov_b32));

      for (unsigned i = 0; i < size; i++) {
         aco_ptr<DPP_instruction> dpp{create_instruction<DPP_instruction>(
            op, (Format) ((uint32_t) format | (uint32_t) Format::DPP),
            format == Format::VOP2 ? 2 : 1, clobber_vcc ? 2 : 1)};
         dpp->operands[0] = Operand(PhysReg{src0+i}, rc);
         if (format == Format::VOP2)
            dpp->operands[1] = Operand(PhysReg{src1+i}, rc);
         dpp->definitions[0] = Definition(PhysReg{dst+i}, rc);
         if (clobber_vcc)
            dpp->definitions[1] = Definition(vcc, s2);
         dpp->dpp_ctrl = dpp_ctrl;
         dpp->row_mask = row_mask;
         dpp->bank_mask = bank_mask;
         dpp->bound_ctrl = bound_ctrl_zero;
         ctx->instructions.emplace_back(std::move(dpp));
      }
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
   }
   unreachable("Invalid reduction operation");
}

aco_opcode get_reduction_opcode(lower_context *ctx, ReduceOp op, bool *clobber_vcc, Format *format)
{
   *clobber_vcc = false;
   *format = Format::VOP2;
   switch (op) {
   case iadd32:
      *clobber_vcc = ctx->program->chip_class < GFX9;
      return ctx->program->chip_class < GFX9 ? aco_opcode::v_add_co_u32 : aco_opcode::v_add_u32;
   case imul32:
      *format = Format::VOP3;
      return aco_opcode::v_mul_lo_u32;
   case fadd32:
      return aco_opcode::v_add_f32;
   case fmul32:
      return aco_opcode::v_mul_f32;
   case imax32:
      return aco_opcode::v_max_i32;
   case imin32:
      return aco_opcode::v_min_i32;
   case umin32:
      return aco_opcode::v_min_u32;
   case umax32:
      return aco_opcode::v_max_u32;
   case fmin32:
      return aco_opcode::v_min_f32;
   case fmax32:
      return aco_opcode::v_max_f32;
   case iand32:
      return aco_opcode::v_and_b32;
   case ixor32:
      return aco_opcode::v_xor_b32;
   case ior32:
      return aco_opcode::v_or_b32;
   case iadd64:
   case imul64:
      assert(false);
      break;
   case fadd64:
      *format = Format::VOP3;
      return aco_opcode::v_add_f64;
   case fmul64:
      *format = Format::VOP3;
      return aco_opcode::v_mul_f64;
   case imin64:
   case imax64:
   case umin64:
   case umax64:
      assert(false);
      break;
   case fmin64:
      *format = Format::VOP3;
      return aco_opcode::v_min_f64;
   case fmax64:
      *format = Format::VOP3;
      return aco_opcode::v_max_f64;
   case iand64:
   case ior64:
   case ixor64:
      assert(false);
      break;
   }
   unreachable("Invalid reduction operation");
   return aco_opcode::v_min_u32;
}

void emit_vopn(lower_context *ctx, PhysReg dst, PhysReg src0, PhysReg src1,
               RegClass rc, aco_opcode op, Format format, bool clobber_vcc)
{
   aco_ptr<Instruction> instr;
   switch (format) {
   case Format::VOP2:
      instr.reset(create_instruction<VOP2_instruction>(op, format, 2, clobber_vcc ? 2 : 1));
      break;
   case Format::VOP3:
      instr.reset(create_instruction<VOP3A_instruction>(op, format, 2, clobber_vcc ? 2 : 1));
      break;
   default:
      assert(false);
   }
   instr->operands[0] = Operand(src0, rc);
   instr->operands[1] = Operand(src1, rc);
   instr->definitions[0] = Definition(dst, rc);
   if (clobber_vcc)
      instr->definitions[1] = Definition(vcc, s2);
   ctx->instructions.emplace_back(std::move(instr));
}

void emit_reduction(lower_context *ctx, aco_opcode op, ReduceOp reduce_op, unsigned cluster_size, PhysReg tmp,
                    PhysReg stmp, PhysReg vtmp, PhysReg sitmp, Operand src, Definition dst)
{
   assert(cluster_size == 64 || op == aco_opcode::p_reduce);

   Builder bld(ctx->program, &ctx->instructions);

   PhysReg wrtmp{0}; /* should never be needed */

   Format format;
   bool should_clobber_vcc;
   aco_opcode reduce_opcode = get_reduction_opcode(ctx, reduce_op, &should_clobber_vcc, &format);
   Operand identity[2];
   identity[0] = Operand(get_reduction_identity(reduce_op, 0));
   identity[1] = Operand(get_reduction_identity(reduce_op, 1));
   Operand vcndmask_identity[2] = {identity[0], identity[1]};

   /* First, copy the source to tmp and set inactive lanes to the identity */
   // note: this clobbers SCC!
   bld.sop1(aco_opcode::s_or_saveexec_b64, Definition(stmp, s2), Definition(scc, s1), Definition(exec, s2), Operand(UINT64_MAX), Operand(exec, s2));

   for (unsigned i = 0; i < src.size(); i++) {
      /* p_exclusive_scan needs it to be a sgpr or inline constant for the v_writelane_b32 */
      if (identity[i].isLiteral() && op == aco_opcode::p_exclusive_scan) {
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
                   Operand(stmp, s2));
   }

   bool exec_restored = false;
   bool dst_written = false;
   switch (op) {
   case aco_opcode::p_reduce:
      if (cluster_size == 1) break;
      emit_dpp_op(ctx, tmp, tmp, tmp, vtmp, wrtmp, reduce_opcode, format, should_clobber_vcc,
                  dpp_quad_perm(1, 0, 3, 2), 0xf, 0xf, false, src.size());
      if (cluster_size == 2) break;
      emit_dpp_op(ctx, tmp, tmp, tmp, vtmp, wrtmp, reduce_opcode, format, should_clobber_vcc,
                  dpp_quad_perm(2, 3, 0, 1), 0xf, 0xf, false, src.size());
      if (cluster_size == 4) break;
      emit_dpp_op(ctx, tmp, tmp, tmp, vtmp, wrtmp, reduce_opcode, format, should_clobber_vcc,
                  dpp_row_half_mirror, 0xf, 0xf, false, src.size());
      if (cluster_size == 8) break;
      emit_dpp_op(ctx, tmp, tmp, tmp, vtmp, wrtmp, reduce_opcode, format, should_clobber_vcc,
                  dpp_row_mirror, 0xf, 0xf, false, src.size());
      if (cluster_size == 16) break;
      if (cluster_size == 32) {
         for (unsigned i = 0; i < src.size(); i++)
            bld.ds(aco_opcode::ds_swizzle_b32, Definition(PhysReg{vtmp+i}, v1), Operand(PhysReg{tmp+i}, s1), ds_pattern_bitmode(0x1f, 0, 0x10));
         bld.sop1(aco_opcode::s_mov_b64, Definition(exec, s2), Operand(stmp, s2));
         exec_restored = true;
         emit_vopn(ctx, dst.physReg(), vtmp, tmp, src.regClass(), reduce_opcode, format, should_clobber_vcc);
         dst_written = true;
      } else {
         assert(cluster_size == 64);
         emit_dpp_op(ctx, tmp, tmp, tmp, vtmp, wrtmp, reduce_opcode, format, should_clobber_vcc,
                     dpp_row_bcast15, 0xa, 0xf, false, src.size());
         emit_dpp_op(ctx, tmp, tmp, tmp, vtmp, wrtmp, reduce_opcode, format, should_clobber_vcc,
                     dpp_row_bcast31, 0xc, 0xf, false, src.size());
      }
      break;
   case aco_opcode::p_exclusive_scan:
      emit_dpp_op(ctx, tmp, tmp, tmp, vtmp, wrtmp, aco_opcode::v_mov_b32, Format::VOP1, false,
                  dpp_wf_sr1, 0xf, 0xf, true, src.size());
      for (unsigned i = 0; i < src.size(); i++) {
         if (!identity[i].isConstant() || identity[i].constantValue()) { /* bound_ctrl should take case of this overwise */
            assert((identity[i].isConstant() && !identity[i].isLiteral()) || identity[i].physReg() == PhysReg{sitmp+i});
            bld.vop3(aco_opcode::v_writelane_b32, Definition(PhysReg{tmp+i}, v1),
                     identity[i], Operand(0u));
         }
      }
      /* fall through */
   case aco_opcode::p_inclusive_scan:
      assert(cluster_size == 64);
      emit_dpp_op(ctx, tmp, tmp, tmp, vtmp, wrtmp, reduce_opcode, format, should_clobber_vcc,
                  dpp_row_sr(1), 0xf, 0xf, false, src.size(), identity);
      emit_dpp_op(ctx, tmp, tmp, tmp, vtmp, wrtmp, reduce_opcode, format, should_clobber_vcc,
                  dpp_row_sr(2), 0xf, 0xf, false, src.size(), identity);
      emit_dpp_op(ctx, tmp, tmp, tmp, vtmp, wrtmp, reduce_opcode, format, should_clobber_vcc,
                  dpp_row_sr(4), 0xf, 0xf, false, src.size(), identity);
      emit_dpp_op(ctx, tmp, tmp, tmp, vtmp, wrtmp, reduce_opcode, format, should_clobber_vcc,
                  dpp_row_sr(8), 0xf, 0xf, false, src.size(), identity);
      emit_dpp_op(ctx, tmp, tmp, tmp, vtmp, wrtmp, reduce_opcode, format, should_clobber_vcc,
                  dpp_row_bcast15, 0xa, 0xf, false, src.size(), identity);
      emit_dpp_op(ctx, tmp, tmp, tmp, vtmp, wrtmp, reduce_opcode, format, should_clobber_vcc,
                  dpp_row_bcast31, 0xc, 0xf, false, src.size(), identity);
      break;
   default:
      unreachable("Invalid reduction mode");
   }

   if (!exec_restored)
      bld.sop1(aco_opcode::s_mov_b64, Definition(exec, s2), Operand(stmp, s2));

   if (op == aco_opcode::p_reduce && cluster_size == 64) {
      for (unsigned k = 0; k < src.size(); k++) {
         bld.vop3(aco_opcode::v_readlane_b32, Definition(PhysReg{dst.physReg() + k}, s1),
                  Operand(PhysReg{tmp + k}, v1), Operand(63u));
      }
   } else if (!(dst.physReg() == tmp) && !dst_written) {
      for (unsigned k = 0; k < src.size(); k++) {
         bld.vop1(aco_opcode::v_mov_b32, Definition(PhysReg{dst.physReg() + k}, s1),
                  Operand(PhysReg{tmp + k}, v1));
      }
   }
}

struct copy_operation {
   Operand op;
   Definition def;
   unsigned uses;
   unsigned size;
};

void handle_operands(std::map<PhysReg, copy_operation>& copy_map, lower_context* ctx, chip_class chip_class, Pseudo_instruction *pi)
{
   Builder bld(ctx->program, &ctx->instructions);
   aco_ptr<Instruction> mov;
   std::map<PhysReg, copy_operation>::iterator it = copy_map.begin();
   std::map<PhysReg, copy_operation>::iterator target;
   bool writes_scc = false;

   /* count the number of uses for each dst reg */
   while (it != copy_map.end()) {
      if (it->second.op.isConstant()) {
         ++it;
         continue;
      }

      if (it->second.def.physReg() == scc)
         writes_scc = true;

      assert(!pi->tmp_in_scc || !(it->second.def.physReg() == pi->scratch_sgpr));

      /* if src and dst reg are the same, remove operation */
      if (it->first == it->second.op.physReg()) {
         it = copy_map.erase(it);
         continue;
      }
      /* check if the operand reg may be overwritten by another copy operation */
      target = copy_map.find(it->second.op.physReg());
      if (target != copy_map.end()) {
         target->second.uses++;
      }

      ++it;
   }

   /* first, handle paths in the location transfer graph */
   bool preserve_scc = pi->tmp_in_scc && !writes_scc;
   it = copy_map.begin();
   while (it != copy_map.end()) {

      /* the target reg is not used as operand for any other copy */
      if (it->second.uses == 0) {

         /* try to coalesce 32-bit sgpr copies to 64-bit copies */
         if (it->second.def.getTemp().type() == RegType::sgpr && it->second.size == 1 &&
             !it->second.op.isConstant() && it->first % 2 == it->second.op.physReg() % 2) {

            PhysReg other_def_reg = PhysReg{it->first % 2 ? it->first - 1 : it->first + 1};
            PhysReg other_op_reg = PhysReg{it->first % 2 ? it->second.op.physReg() - 1 : it->second.op.physReg() + 1};
            std::map<PhysReg, copy_operation>::iterator other = copy_map.find(other_def_reg);

            if (other != copy_map.end() && !other->second.uses && other->second.size == 1 &&
                other->second.op.physReg() == other_op_reg && !other->second.op.isConstant()) {
               std::map<PhysReg, copy_operation>::iterator to_erase = it->first % 2 ? it : other;
               it = it->first % 2 ? other : it;
               copy_map.erase(to_erase);
               it->second.size = 2;
            }
         }

         if (it->second.def.physReg() == scc) {
            bld.sopc(aco_opcode::s_cmp_lg_i32, it->second.def, it->second.op, Operand(0u));
            preserve_scc = true;
         } else if (it->second.size == 2 && it->second.def.getTemp().type() == RegType::sgpr) {
            bld.sop1(aco_opcode::s_mov_b64, it->second.def, Operand(it->second.op.physReg(), s2));
         } else {
            bld.copy(it->second.def, it->second.op);
         }

         /* reduce the number of uses of the operand reg by one */
         if (!it->second.op.isConstant()) {
            for (unsigned i = 0; i < it->second.size; i++) {
               target = copy_map.find(PhysReg{it->second.op.physReg() + i});
               if (target != copy_map.end())
                  target->second.uses--;
            }
         }

         copy_map.erase(it);
         it = copy_map.begin();
         continue;
      } else {
         /* the target reg is used as operand, check the next entry */
         ++it;
      }
   }

   if (copy_map.empty())
      return;

   /* all target regs are needed as operand somewhere which means, all entries are part of a cycle */
   bool constants = false;
   for (it = copy_map.begin(); it != copy_map.end(); ++it) {
      assert(it->second.op.isFixed());
      if (it->first == it->second.op.physReg())
         continue;
      /* do constants later */
      if (it->second.op.isConstant()) {
         constants = true;
         continue;
      }

      if (preserve_scc && it->second.def.getTemp().type() == RegType::sgpr)
         assert(!(it->second.def.physReg() == pi->scratch_sgpr));

      /* to resolve the cycle, we have to swap the src reg with the dst reg */
      copy_operation swap = it->second;
      assert(swap.op.regClass() == swap.def.regClass());
      Operand def_as_op = Operand(swap.def.physReg(), swap.def.regClass());
      Definition op_as_def = Definition(swap.op.physReg(), swap.op.regClass());
      if (chip_class >= GFX9 && swap.def.getTemp().type() == RegType::vgpr) {
         bld.vop1(aco_opcode::v_swap_b32, swap.def, op_as_def, swap.op, def_as_op);
      } else if (swap.op.physReg() == scc || swap.def.physReg() == scc) {
         /* we need to swap scc and another sgpr */
         assert(!preserve_scc);

         PhysReg other = swap.op.physReg() == scc ? swap.def.physReg() : swap.op.physReg();

         bld.sop1(aco_opcode::s_mov_b32, Definition(pi->scratch_sgpr, s1), Operand(scc, s1));
         bld.sopc(aco_opcode::s_cmp_lg_i32, Definition(scc, s1), Operand(other, s1), Operand(0u));
         bld.sop1(aco_opcode::s_mov_b32, Definition(other, s1), Operand(pi->scratch_sgpr, s1));
      } else if (swap.def.getTemp().type() == RegType::sgpr) {
         if (preserve_scc) {
            bld.sop1(aco_opcode::s_mov_b32, Definition(pi->scratch_sgpr, s1), swap.op);
            bld.sop1(aco_opcode::s_mov_b32, op_as_def, def_as_op);
            bld.sop1(aco_opcode::s_mov_b32, swap.def, Operand(pi->scratch_sgpr, s1));
         } else {
            bld.sop2(aco_opcode::s_xor_b32, op_as_def, Definition(scc, s1), swap.op, def_as_op);
            bld.sop2(aco_opcode::s_xor_b32, swap.def, Definition(scc, s1), swap.op, def_as_op);
            bld.sop2(aco_opcode::s_xor_b32, op_as_def, Definition(scc, s1), swap.op, def_as_op);
         }
      } else {
         bld.vop2(aco_opcode::v_xor_b32, op_as_def, swap.op, def_as_op);
         bld.vop2(aco_opcode::v_xor_b32, swap.def, swap.op, def_as_op);
         bld.vop2(aco_opcode::v_xor_b32, op_as_def, swap.op, def_as_op);
      }

      /* change the operand reg of the target's use */
      assert(swap.uses == 1);
      target = it;
      for (++target; target != copy_map.end(); ++target) {
         if (target->second.op.physReg() == it->first) {
            target->second.op.setFixed(swap.op.physReg());
            break;
         }
      }
   }

   /* copy constants into a registers which were operands */
   if (constants) {
      for (it = copy_map.begin(); it != copy_map.end(); ++it) {
         if (!it->second.op.isConstant())
            continue;
         if (it->second.def.physReg() == scc) {
            bld.sopc(aco_opcode::s_cmp_lg_i32, Definition(scc, s1), Operand(0u), Operand(it->second.op.constantValue() ? 1u : 0u));
         } else {
            bld.copy(it->second.def, it->second.op);
         }
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

      for (size_t j = 0; j < block->instructions.size(); j++) {
         aco_ptr<Instruction>& instr = block->instructions[j];
         aco_ptr<Instruction> mov;
         if (instr->format == Format::PSEUDO) {
            Pseudo_instruction *pi = (Pseudo_instruction*)instr.get();

            switch (instr->opcode)
            {
            case aco_opcode::p_extract_vector:
            {
               unsigned reg = instr->operands[0].physReg() + instr->operands[1].constantValue() * instr->definitions[0].size();
               RegClass rc = RegClass(instr->operands[0].getTemp().type(), 1);
               RegClass rc_def = RegClass(instr->definitions[0].getTemp().type(), 1);
               if (reg == instr->definitions[0].physReg())
                  break;

               std::map<PhysReg, copy_operation> copy_operations;
               for (unsigned i = 0; i < instr->definitions[0].size(); i++) {
                  Definition def = Definition(PhysReg{instr->definitions[0].physReg() + i}, rc_def);
                  copy_operations[def.physReg()] = {Operand(PhysReg{reg + i}, rc), def, 0, 1};
               }
               handle_operands(copy_operations, &ctx, program->chip_class, pi);
               break;
            }
            case aco_opcode::p_create_vector:
            {
               std::map<PhysReg, copy_operation> copy_operations;
               RegClass rc_def = RegClass(instr->definitions[0].getTemp().type(), 1);
               unsigned reg_idx = 0;
               for (const Operand& op : instr->operands) {
                  if (op.isConstant()) {
                     const PhysReg reg = PhysReg{instr->definitions[0].physReg() + reg_idx};
                     const Definition def = Definition(reg, rc_def);
                     copy_operations[reg] = {op, def, 0, 1};
                     reg_idx++;
                     continue;
                  }

                  RegClass rc_op = RegClass(op.getTemp().type(), 1);
                  for (unsigned j = 0; j < op.size(); j++)
                  {
                     const Operand copy_op = Operand(PhysReg{op.physReg() + j}, rc_op);
                     const Definition def = Definition(PhysReg{instr->definitions[0].physReg() + reg_idx}, rc_def);
                     copy_operations[def.physReg()] = {copy_op, def, 0, 1};
                     reg_idx++;
                  }
               }
               handle_operands(copy_operations, &ctx, program->chip_class, pi);
               break;
            }
            case aco_opcode::p_split_vector:
            {
               std::map<PhysReg, copy_operation> copy_operations;
               RegClass rc_op = instr->operands[0].isConstant() ? s1 : RegClass(instr->operands[0].regClass().type(), 1);
               for (unsigned i = 0; i < instr->definitions.size(); i++) {
                  unsigned k = instr->definitions[i].size();
                  RegClass rc_def = RegClass(instr->definitions[i].getTemp().type(), 1);
                  for (unsigned j = 0; j < k; j++) {
                     Operand op = Operand(PhysReg{instr->operands[0].physReg() + (i*k+j)}, rc_op);
                     Definition def = Definition(PhysReg{instr->definitions[i].physReg() + j}, rc_def);
                     copy_operations[def.physReg()] = {op, def, 0, 1};
                  }
               }
               handle_operands(copy_operations, &ctx, program->chip_class, pi);
               break;
            }
            case aco_opcode::p_parallelcopy:
            case aco_opcode::p_wqm:
            {
               std::map<PhysReg, copy_operation> copy_operations;
               for (unsigned i = 0; i < instr->operands.size(); i++)
               {
                  Operand operand = instr->operands[i];
                  if (operand.isConstant() || operand.size() == 1) {
                     assert(instr->definitions[i].size() == 1);
                     copy_operations[instr->definitions[i].physReg()] = {operand, instr->definitions[i], 0, 1};
                  } else {
                     RegClass def_rc = RegClass(instr->definitions[i].regClass().type(), 1);
                     RegClass op_rc = RegClass(operand.getTemp().type(), 1);
                     for (unsigned j = 0; j < operand.size(); j++)
                     {
                        Operand op = Operand(PhysReg{instr->operands[i].physReg() + j}, op_rc);
                        Definition def = Definition(PhysReg{instr->definitions[i].physReg() + j}, def_rc);
                        copy_operations[def.physReg()] = {op, def, 0, 1};
                     }
                  }
               }
               handle_operands(copy_operations, &ctx, program->chip_class, pi);
               break;
            }
            case aco_opcode::p_discard_if:
            {
               bool early_exit = false;
               if (block->instructions[j + 1]->opcode != aco_opcode::p_logical_end ||
                   block->instructions[j + 2]->opcode != aco_opcode::s_endpgm) {
                  early_exit = true;
               }

               if (early_exit && !discard_block) {
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

               // TODO: optimize uniform conditions
               Definition branch_cond = instr->definitions.back();
               Operand discard_cond = instr->operands.back();
               aco_ptr<Instruction> sop2;
               /* backwards, to finally branch on the global exec mask */
               for (int i = instr->operands.size() - 2; i >= 0; i--) {
                  bld.sop2(aco_opcode::s_andn2_b64,
                           instr->definitions[i], /* new mask */
                           branch_cond, /* scc */
                           instr->operands[i], /* old mask */
                           discard_cond);
               }

               if (early_exit) {
                  bld.sopp(aco_opcode::s_cbranch_scc0, bld.scc(branch_cond.getTemp()), discard_block->index);

                  discard_block->linear_preds.push_back(block->index);
                  block->linear_succs.push_back(discard_block->index);
               }

               break;
            }
            case aco_opcode::p_spill:
            {
               assert(instr->operands[0].regClass() == v1.as_linear());
               for (unsigned i = 0; i < instr->operands[2].size(); i++) {
                  bld.vop3(aco_opcode::v_writelane_b32, bld.def(v1, instr->operands[0].physReg()),
                           Operand(PhysReg{instr->operands[2].physReg() + i}, s1),
                           Operand(instr->operands[1].constantValue() + i));
               }
               break;
            }
            case aco_opcode::p_reload:
            {
               assert(instr->operands[0].regClass() == v1.as_linear());
               for (unsigned i = 0; i < instr->definitions[0].size(); i++) {
                  bld.vop3(aco_opcode::v_readlane_b32,
                           bld.def(s1, PhysReg{instr->definitions[0].physReg() + i}),
                           instr->operands[0], Operand(instr->operands[1].constantValue() + i));
               }
               break;
            }
            case aco_opcode::p_as_uniform:
            {
               if (instr->operands[0].isConstant() || instr->operands[0].regClass().type() == RegType::sgpr) {
                  std::map<PhysReg, copy_operation> copy_operations;
                  Operand operand = instr->operands[0];
                  if (operand.isConstant() || operand.size() == 1) {
                     assert(instr->definitions[0].size() == 1);
                     copy_operations[instr->definitions[0].physReg()] = {operand, instr->definitions[0], 0, 1};
                  } else {
                     for (unsigned i = 0; i < operand.size(); i++)
                     {
                        Operand op = Operand(PhysReg{operand.physReg() + i}, s1);
                        Definition def = Definition(PhysReg{instr->definitions[0].physReg() + i}, s1);
                        copy_operations[def.physReg()] = {op, def, 0, 1};
                     }
                  }

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
            emit_reduction(&ctx, reduce->opcode, reduce->reduce_op, reduce->cluster_size,
                           reduce->operands[1].physReg(), // tmp
                           reduce->definitions[1].physReg(), // stmp
                           reduce->operands[2].physReg(), // vtmp
                           reduce->definitions[2].physReg(), // sitmp
                           reduce->operands[0], reduce->definitions[0]);
         } else {
            ctx.instructions.emplace_back(std::move(instr));
         }

      }
      block->instructions.swap(ctx.instructions);
   }
}

}
