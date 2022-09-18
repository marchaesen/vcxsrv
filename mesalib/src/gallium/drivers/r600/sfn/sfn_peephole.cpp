/* -*- mesa-c++  -*-
 *
 * Copyright (c) 2022 Collabora LTD
 *
 * Author: Gert Wollny <gert.wollny@collabora.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "sfn_peephole.h"

namespace r600 {


class PeepholeVisitor : public InstrVisitor {
public:
   void visit(AluInstr *instr) override;
   void visit(AluGroup *instr) override;
   void visit(TexInstr *instr) override {(void)instr;};
   void visit(ExportInstr *instr) override {(void)instr;}
   void visit(FetchInstr *instr) override {(void)instr;}
   void visit(Block *instr) override;
   void visit(ControlFlowInstr *instr) override {(void)instr;}
   void visit(IfInstr *instr) override;
   void visit(ScratchIOInstr *instr) override {(void)instr;}
   void visit(StreamOutInstr *instr) override {(void)instr;}
   void visit(MemRingOutInstr *instr) override {(void)instr;}
   void visit(EmitVertexInstr *instr) override {(void)instr;}
   void visit(GDSInstr *instr) override {(void)instr;};
   void visit(WriteTFInstr *instr) override {(void)instr;};
   void visit(LDSAtomicInstr *instr) override {(void)instr;};
   void visit(LDSReadInstr *instr) override {(void)instr;};
   void visit(RatInstr *instr) override {(void)instr;};

   bool src_is_zero(PVirtualValue value);
   bool src_is_one(PVirtualValue value);

   void convert_to_mov(AluInstr *alu, int src_idx);


   bool progress{false};
};


bool peephole(Shader& sh)
{
   PeepholeVisitor peephole;
   for(auto b : sh.func())
      b->accept(peephole);
   return peephole.progress;
}

void PeepholeVisitor::visit(AluInstr *instr)
{
   switch (instr->opcode()) {
   case op2_add:
   case op2_add_int:
      if (src_is_zero(instr->psrc(0)))
         convert_to_mov(instr, 1);
      else if (src_is_zero(instr->psrc(1)))
         convert_to_mov(instr, 0);
      break;
   case op2_mul:
   case op2_mul_ieee:
      if (src_is_one(instr->psrc(0)))
         convert_to_mov(instr, 1);
      else if (src_is_one(instr->psrc(1)))
         convert_to_mov(instr, 0);
      break;
   case op3_muladd:
   case op3_muladd_ieee:
      if (src_is_zero(instr->psrc(0)) ||
          src_is_zero(instr->psrc(1)))
         convert_to_mov(instr, 2);
      break;
   default:
      ;
   }
}

bool PeepholeVisitor::src_is_zero(PVirtualValue value)
{
   if (value->as_inline_const() &&
       value->as_inline_const()->sel() == ALU_SRC_0)
      return true;

   if (value->as_literal() &&
       value->as_literal()->value() == 0)
      return true;

   return false;
}

bool PeepholeVisitor::src_is_one(PVirtualValue value)
{
   if (value->as_inline_const() &&
       value->as_inline_const()->sel() == ALU_SRC_1)
      return true;

   if (value->as_literal() &&
       value->as_literal()->value() == 0x3f800000)
      return true;

   return false;
}

void PeepholeVisitor::convert_to_mov(AluInstr *alu, int src_idx)
{
   AluInstr::SrcValues new_src{alu->psrc(src_idx)};
   alu->set_sources(new_src);
   alu->set_op(op1_mov);
   progress = true;
}


void PeepholeVisitor::visit(AluGroup *instr)
{

}

void PeepholeVisitor::visit(Block *instr)
{
   for (auto& i: *instr)
      i->accept(*this);
}

class ReplaceIfPredicate : public AluInstrVisitor {
public:
   ReplaceIfPredicate(AluInstr *pred):
      m_pred(pred) {}

   using AluInstrVisitor::visit;

   void visit(AluInstr *alu) override;

   AluInstr *m_pred;
   bool success{false};
};

void PeepholeVisitor::visit(IfInstr *instr)
{
   auto pred = instr->predicate();

   auto& src1 = pred->src(1);
   if (src1.as_inline_const() &&
       src1.as_inline_const()->sel() == ALU_SRC_0) {
      auto src0 = pred->src(0).as_register();
      if (src0 && src0->is_ssa() && !src0->parents().empty()) {
         assert(src0->parents().size() == 1);
         auto parent = *src0->parents().begin();

         ReplaceIfPredicate visitor(pred);
         parent->accept(visitor);
         progress |= visitor.success;
      }
   }
}

static EAluOp pred_from_op(EAluOp pred_op, EAluOp op)
{
   switch (pred_op) {
   case op2_pred_setne_int:
      switch (op) {
      case op2_setge_dx10 : return op2_pred_setge;
      case op2_setgt_dx10 : return op2_pred_setgt;
      case op2_sete_dx10 : return op2_pred_sete;
      case op2_setne_dx10 : return op2_pred_setne;

      case op2_setge_int : return op2_pred_setge_int;
      case op2_setgt_int : return op2_pred_setgt_int;
      case op2_setge_uint : return op2_pred_setge_uint;
      case op2_setgt_uint : return op2_pred_setgt_uint;
      case op2_sete_int : return op2_prede_int;
      case op2_setne_int : return op2_pred_setne_int;
      default:
         return op0_nop;
      }
   case op2_prede_int:
      switch (op) {
      case op2_sete_int : return op2_pred_setne_int;
      case op2_setne_int : return op2_prede_int;
      default:
         return op0_nop;
      }
   case op2_pred_setne:
      switch (op) {
      case op2_setge : return op2_pred_setge;
      case op2_setgt : return op2_pred_setgt;
      case op2_sete : return op2_pred_sete;
      default:
         return op0_nop;
      }
   default:
      return op0_nop;
   }
}

void ReplaceIfPredicate::visit(AluInstr *alu)
{
   auto new_op = pred_from_op(m_pred->opcode(), alu->opcode());

   if (new_op == op0_nop)
      return;

   /* Have to figure out how to pass the dependency correctly */
   /*for (auto& s : alu->sources()) {
      if (s->as_register() && s->as_register()->addr())
         return;
   }*/

   m_pred->set_op(new_op);
   m_pred->set_sources(alu->sources());

   if (alu->has_alu_flag(alu_src0_abs))
      m_pred->set_alu_flag(alu_src0_abs);
   if (alu->has_alu_flag(alu_src1_abs))
      m_pred->set_alu_flag(alu_src1_abs);

   if (alu->has_alu_flag(alu_src0_neg))
      m_pred->set_alu_flag(alu_src0_neg);

   if (alu->has_alu_flag(alu_src1_neg))
      m_pred->set_alu_flag(alu_src1_neg);

   success = true;
}

}
