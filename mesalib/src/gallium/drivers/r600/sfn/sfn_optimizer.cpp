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

#include "sfn_optimizer.h"

#include "sfn_instr_alugroup.h"
#include "sfn_instr_controlflow.h"
#include "sfn_instr_export.h"
#include "sfn_instr_tex.h"
#include "sfn_instr_fetch.h"
#include "sfn_instr_lds.h"
#include "sfn_peephole.h"
#include "sfn_debug.h"

#include <sstream>

namespace r600 {

bool optimize(Shader& shader)
{
   bool progress;

   sfn_log << SfnLog::opt  << "Shader before optimization\n";
   if (sfn_log.has_debug_flag(SfnLog::opt)) {
      std::stringstream ss;
      shader.print(ss);
      sfn_log << ss.str() << "\n\n";
   }

   do {
      progress = false;
      progress |= copy_propagation_fwd(shader);
      progress |= dead_code_elimination(shader);
      progress |= copy_propagation_backward(shader);
      progress |= dead_code_elimination(shader);
      progress |= simplify_source_vectors(shader);
      progress |= peephole(shader);
      progress |= dead_code_elimination(shader);
   } while (progress);

   return progress;
}

class DCEVisitor : public InstrVisitor {
public:
   DCEVisitor();

   void visit(AluInstr *instr) override;
   void visit(AluGroup *instr) override;
   void visit(TexInstr  *instr) override;
   void visit(ExportInstr *instr) override {(void)instr;};
   void visit(FetchInstr *instr) override;
   void visit(Block *instr) override;

   void visit(ControlFlowInstr *instr) override {(void)instr;};
   void visit(IfInstr *instr) override {(void)instr;};
   void visit(ScratchIOInstr *instr) override {(void)instr;};
   void visit(StreamOutInstr *instr) override {(void)instr;};
   void visit(MemRingOutInstr *instr) override {(void)instr;};
   void visit(EmitVertexInstr *instr) override {(void)instr;};
   void visit(GDSInstr *instr) override {(void)instr;};
   void visit(WriteTFInstr *instr) override {(void)instr;};
   void visit(LDSAtomicInstr *instr) override {(void)instr;};
   void visit(LDSReadInstr *instr) override;
   void visit(RatInstr *instr) override {(void)instr;};


   bool progress;
};

bool dead_code_elimination(Shader& shader)
{
   DCEVisitor dce;

   do {

      sfn_log << SfnLog::opt << "start dce run\n";

      dce.progress = false;
      for (auto& b : shader.func())
         b->accept(dce);

      sfn_log << SfnLog::opt << "finished dce run\n\n";

   }  while (dce.progress);

   sfn_log << SfnLog::opt  << "Shader after DCE\n";
   if (sfn_log.has_debug_flag(SfnLog::opt)) {
      std::stringstream ss;
      shader.print(ss);
      sfn_log << ss.str() << "\n\n";
   }

   return dce.progress;
}

DCEVisitor::DCEVisitor():progress(false)
{
}

void DCEVisitor::visit(AluInstr *instr)
{
   sfn_log << SfnLog::opt << "DCE: visit '" << *instr;

   if (instr->has_instr_flag(Instr::dead))
      return;

   if (instr->dest() &&
       (instr->dest()->has_uses() || !instr->dest()->is_ssa()) ) {
      sfn_log << SfnLog::opt << " dest used\n";
      return;
   }

   switch (instr->opcode()) {
   case op2_kille:
   case op2_killne:
   case op2_kille_int:
   case op2_killne_int:
   case op2_killge:
   case op2_killge_int:
   case op2_killge_uint:
   case op2_killgt:
   case op2_killgt_int:
   case op2_killgt_uint:
   case op0_group_barrier:
      sfn_log << SfnLog::opt << " never kill\n";
      return;
   default:
      ;
   }

   bool dead = instr->set_dead();
   sfn_log << SfnLog::opt << (dead ? "dead" : "alive") << "\n";
   progress |= dead;
}

void DCEVisitor::visit(LDSReadInstr *instr)
{
   sfn_log << SfnLog::opt << "visit " << *instr << "\n";
   progress |= instr->remove_unused_components();
}

void DCEVisitor::visit(AluGroup *instr)
{
   /* Groups are created because the instructions are used together
    * so don't try to eliminate code there */
   (void)instr;
}

void DCEVisitor::visit(TexInstr *instr)
{
   auto& dest = instr->dst();

   bool has_uses = false;
   RegisterVec4::Swizzle swz = instr->all_dest_swizzle();
   for (int i = 0; i < 4; ++i) {
      if (!dest[i]->has_uses())
         swz[i] = 7;
      else
         has_uses |= true;
   }
   instr->set_dest_swizzle(swz);

   if (has_uses)
      return;

   progress |= instr->set_dead();
}

void DCEVisitor::visit(FetchInstr *instr)
{
   auto& dest = instr->dst();

   bool has_uses = false;
   RegisterVec4::Swizzle swz = instr->all_dest_swizzle();
   for (int i = 0; i < 4; ++i) {
      if (!dest[i]->has_uses())
         swz[i] = 7;
      else
         has_uses |= true;
   }
   instr->set_dest_swizzle(swz);

   if (has_uses)
      return;

   sfn_log << SfnLog::opt << "set dead: " << *instr << "\n";

   progress |= instr->set_dead();
}

void DCEVisitor::visit(Block *block)
{
   auto i = block->begin();
   auto e = block->end();
   while (i != e) {
      auto n = i++;
      if (!(*n)->keep()) {
         (*n)->accept(*this);
         if ((*n)->is_dead()) {
            block->erase(n);
         }
      }
   }
}

class CopyPropFwdVisitor : public InstrVisitor {
public:
   CopyPropFwdVisitor();

   void visit(AluInstr *instr) override;
   void visit(AluGroup *instr) override;
   void visit(TexInstr *instr) override;
   void visit(ExportInstr *instr) override;
   void visit(FetchInstr *instr) override;
   void visit(Block *instr) override;
   void visit(ControlFlowInstr *instr) override {(void)instr;}
   void visit(IfInstr *instr) override {(void)instr;}
   void visit(ScratchIOInstr *instr) override {(void)instr;}
   void visit(StreamOutInstr *instr) override {(void)instr;}
   void visit(MemRingOutInstr *instr) override {(void)instr;}
   void visit(EmitVertexInstr *instr) override {(void)instr;}
   void visit(GDSInstr *instr) override {(void)instr;};
   void visit(WriteTFInstr *instr) override {(void)instr;};
   void visit(RatInstr *instr) override {(void)instr;};

   // TODO: these two should use copy propagation
   void visit(LDSAtomicInstr *instr) override {(void)instr;};
   void visit(LDSReadInstr *instr) override {(void)instr;};

   void propagate_to(RegisterVec4& src, Instr *instr);

   bool progress;
};


class CopyPropBackVisitor : public InstrVisitor {
public:
   CopyPropBackVisitor();

   void visit(AluInstr *instr) override;
   void visit(AluGroup *instr) override;
   void visit(TexInstr *instr) override;
   void visit(ExportInstr *instr) override {(void)instr;}
   void visit(FetchInstr *instr) override;
   void visit(Block *instr) override;
   void visit(ControlFlowInstr *instr) override {(void)instr;}
   void visit(IfInstr *instr) override {(void)instr;}
   void visit(ScratchIOInstr *instr) override {(void)instr;}
   void visit(StreamOutInstr *instr) override {(void)instr;}
   void visit(MemRingOutInstr *instr) override {(void)instr;}
   void visit(EmitVertexInstr *instr) override {(void)instr;}
   void visit(GDSInstr *instr) override {(void)instr;};
   void visit(WriteTFInstr *instr) override {(void)instr;};
   void visit(LDSAtomicInstr *instr) override {(void)instr;};
   void visit(LDSReadInstr *instr) override {(void)instr;};
   void visit(RatInstr *instr) override {(void)instr;};

   bool progress;
};

bool copy_propagation_fwd(Shader& shader)
{
   auto& root = shader.func();
   CopyPropFwdVisitor copy_prop;

   do {
      copy_prop.progress = false;
      for (auto b : root)
         b->accept(copy_prop);
   }  while (copy_prop.progress);

   sfn_log << SfnLog::opt  << "Shader after Copy Prop forward\n";
   if (sfn_log.has_debug_flag(SfnLog::opt)) {
      std::stringstream ss;
      shader.print(ss);
      sfn_log << ss.str() << "\n\n";
   }


   return copy_prop.progress;
}

bool copy_propagation_backward(Shader& shader)
{
   CopyPropBackVisitor copy_prop;

   do {
      copy_prop.progress = false;
      for (auto b: shader.func())
         b->accept(copy_prop);
   }  while (copy_prop.progress);

   sfn_log << SfnLog::opt  << "Shader after Copy Prop backwards\n";
   if (sfn_log.has_debug_flag(SfnLog::opt)) {
      std::stringstream ss;
      shader.print(ss);
      sfn_log << ss.str() << "\n\n";
   }

   return copy_prop.progress;
}

CopyPropFwdVisitor::CopyPropFwdVisitor():
   progress(false)
{}

void CopyPropFwdVisitor::visit(AluInstr *instr)
{
   sfn_log << SfnLog::opt << "CopyPropFwdVisitor:["
           << instr->block_id() << ":" << instr->index() << "] " << *instr
           << " dset=" << instr->dest() << " ";



   if (instr->dest()) {
      sfn_log << SfnLog::opt << "has uses; "
              << instr->dest()->uses().size();
   }

   sfn_log << SfnLog::opt << "\n";

   if (!instr->can_propagate_src()) {
      return;
   }

   auto src = instr->psrc(0);
   auto dest = instr->dest();

   for (auto& i : dest->uses()) {
      /* SSA can always be propagated, registers only in the same block
       * and only if they are assigned in the same block */
      bool can_propagate = dest->is_ssa();

      if (!can_propagate) {

         /* Register can propagate if the assigment was in the same
          * block, and we don't have a second assignment coming later
          * (e.g. helper invocation evaluation does
          *
          * 1: MOV R0.x, -1
          * 2: FETCH R0.0 VPM
          * 3: MOV SN.x, R0.x
          *
          * Here we can't prpagate the move in 1 to SN.x in 3 */
         if ((instr->block_id() == i->block_id() &&
              instr->index() < i->index())) {
            can_propagate = true;
            if (dest->parents().size() > 1) {
               for (auto p : dest->parents()) {
                  if (p->block_id() == i->block_id() &&
                      p->index() > instr->index()) {
                      can_propagate = false;
                      break;
                  }
               }
            }
         }
      }

      if (can_propagate) {
         sfn_log << SfnLog::opt << "   Try replace in "
                 << i->block_id() << ":" << i->index()
                 << *i<< "\n";
         progress |= i->replace_source(dest, src);
      }
   }
   if (instr->dest()) {
      sfn_log << SfnLog::opt << "has uses; "
              << instr->dest()->uses().size();
   }
   sfn_log << SfnLog::opt << "  done\n";
}


void CopyPropFwdVisitor::visit(AluGroup *instr)
{
   (void)instr;
}

void CopyPropFwdVisitor::visit(TexInstr *instr)
{
   propagate_to(instr->src(), instr);
}

void CopyPropFwdVisitor::visit(ExportInstr *instr)
{
   propagate_to(instr->value(), instr);
}

void CopyPropFwdVisitor::propagate_to(RegisterVec4& src, Instr *instr)
{
   AluInstr *parents[4] = {nullptr};
   for (int i = 0; i < 4; ++i) {
      if (src[i]->chan() < 4 && src[i]->is_ssa()) {
         /*  We have a pre-define value, so we can't propagate a copy */
         if (src[i]->parents().empty())
            return;

         assert(src[i]->parents().size() == 1);
         parents[i] = (*src[i]->parents().begin())->as_alu();
      }
   }
   PRegister new_src[4] = {0};

   int sel = -1;
   for (int i = 0; i < 4; ++i) {
      if (!parents[i])
         continue;
      if ((parents[i]->opcode() != op1_mov) ||
          parents[i]->has_alu_flag(alu_src0_neg) ||
          parents[i]->has_alu_flag(alu_src0_abs) ||
          parents[i]->has_alu_flag(alu_dst_clamp) ||
          parents[i]->has_alu_flag(alu_src0_rel)) {
         return;
      } else {
         auto src = parents[i]->src(0).as_register();
         if (!src)
            return;
         else if (!src->is_ssa())
            return;
         else if (sel < 0)
            sel = src->sel();
         else if (sel != src->sel())
            return;
         new_src[i] = src;
      }
   }

   for (int i = 0; i < 4; ++i) {
      if (parents[i]) {
         src.del_use(instr);
         src.set_value(i, new_src[i]);
         if (new_src[i]->pin() != pin_fully) {
            if (new_src[i]->pin() == pin_chan)
               new_src[i]->set_pin(pin_chgr);
            else
               new_src[i]->set_pin(pin_group);
         }
         src.add_use(instr);
         progress |= true;
      }
   }
   if (progress)
      src.validate();
}

void CopyPropFwdVisitor::visit(FetchInstr *instr)
{
   (void)instr;
}

void CopyPropFwdVisitor::visit(Block *instr)
{
   for (auto& i: *instr)
      i->accept(*this);
}

CopyPropBackVisitor::CopyPropBackVisitor():
   progress(false)
{

}

void CopyPropBackVisitor::visit(AluInstr *instr)
{
   bool local_progress = false;

   sfn_log << SfnLog::opt << "CopyPropBackVisitor:["
           << instr->block_id() << ":" << instr->index() << "] " << *instr << "\n";


   if (!instr->can_propagate_dest()) {
      return;
   }

   auto src_reg = instr->psrc(0)->as_register();
   if (!src_reg) {
      return;
   }

   if (src_reg->uses().size() > 1)
      return;

   auto dest = instr->dest();
   if (!dest ||
       !instr->has_alu_flag(alu_write)) {
      return;
   }

   if (!dest->is_ssa() && dest->parents().size() > 1)
      return;

  for (auto& i: src_reg->parents()) {
     sfn_log << SfnLog::opt << "Try replace dest in "
             << i->block_id() << ":" << i->index()
             << *i<< "\n";

     if (i->replace_dest(dest, instr))  {
        dest->del_parent(instr);
        dest->add_parent(i);
        for (auto d : instr->dependend_instr()) {
           d->add_required_instr(i);
        }
        local_progress = true;
     }
  }

  if (local_progress)
     instr->set_dead();

  progress |= local_progress;
}

void CopyPropBackVisitor::visit(AluGroup *instr)
{
   for (auto& i: *instr) {
      if (i)
         i->accept(*this);
   }
}

void CopyPropBackVisitor::visit(TexInstr *instr)
{
   (void)instr;
}

void CopyPropBackVisitor::visit(FetchInstr *instr)
{
   (void)instr;
}

void CopyPropBackVisitor::visit(Block *instr)
{
   for (auto i = instr->rbegin(); i != instr->rend(); ++i)
      if (!(*i)->is_dead())
         (*i)->accept(*this);
}

class SimplifySourceVecVisitor : public InstrVisitor {
public:
   SimplifySourceVecVisitor():progress(false) {}

   void visit(AluInstr *instr) override{(void)instr;}
   void visit(AluGroup *instr) override{(void)instr;}
   void visit(TexInstr *instr) override;
   void visit(ExportInstr *instr) override;
   void visit(FetchInstr *instr) override;
   void visit(Block *instr) override;
   void visit(ControlFlowInstr *instr) override;
   void visit(IfInstr *instr) override;
   void visit(ScratchIOInstr *instr) override;
   void visit(StreamOutInstr *instr) override;
   void visit(MemRingOutInstr *instr) override;
   void visit(EmitVertexInstr *instr) override {(void)instr;}
   void visit(GDSInstr *instr) override {(void)instr;};
   void visit(WriteTFInstr *instr) override {(void)instr;};
   void visit(LDSAtomicInstr *instr) override {(void)instr;};
   void visit(LDSReadInstr *instr) override {(void)instr;};
   void visit(RatInstr *instr) override {(void)instr;};

   void replace_src(Instr *instr, RegisterVec4& reg4);

   bool progress;
};

bool simplify_source_vectors(Shader& sh)
{
   SimplifySourceVecVisitor visitor;

   for (auto b: sh.func())
      b->accept(visitor);

   return visitor.progress;
}

void SimplifySourceVecVisitor::visit(TexInstr *instr)
{

   if (instr->opcode() != TexInstr::get_resinfo) {
      auto& src = instr->src();
      replace_src(instr, src);
      int nvals = 0;
      for (int i = 0; i < 4; ++i)
         if (src[i]->chan() < 4)
            ++nvals;
      if (nvals == 1) {
         for (int i = 0; i < 4; ++i)
            if (src[i]->chan() < 4) {
               if (src[i]->pin() == pin_group)
                  src[i]->set_pin(pin_free);
               else if (src[i]->pin() == pin_chgr)
                  src[i]->set_pin(pin_chan);
            }
      }
   }
   for (auto& prep : instr->prepare_instr()) {
      prep->accept(*this);
   }
}

void SimplifySourceVecVisitor::visit(ScratchIOInstr *instr)
{
   (void) instr;
}

class ReplaceConstSource : public AluInstrVisitor {
public:
   ReplaceConstSource(Instr *old_use_, RegisterVec4& vreg_, int i):
       old_use(old_use_), vreg(vreg_), index(i),success(false) {}

   using AluInstrVisitor::visit;

   void visit(AluInstr *alu) override;

   Instr *old_use;
   RegisterVec4& vreg;
   int index;
   bool success;
};

void SimplifySourceVecVisitor::visit(ExportInstr *instr)
{
   replace_src(instr, instr->value());
}

void SimplifySourceVecVisitor::replace_src(Instr *instr, RegisterVec4& reg4)
{
   for (int i = 0; i < 4; ++i) {
      auto s = reg4[i];

      if (s->chan() > 3)
         continue;

      if (!s->is_ssa())
         continue;

      /* Cayman trans ops have more then one parent for
       * one dest */
      if (s->parents().size() != 1)
         continue;

      auto& op = *s->parents().begin();

      ReplaceConstSource visitor(instr, reg4, i);

      op->accept(visitor);

      progress |= visitor.success;
   }
}

void SimplifySourceVecVisitor::visit(StreamOutInstr *instr)
{
   (void)instr;
}

void SimplifySourceVecVisitor::visit(MemRingOutInstr *instr)
{
   (void)instr;
}

void ReplaceConstSource::visit(AluInstr *alu)
{
   if (alu->opcode() != op1_mov)
      return;

   if (alu->has_alu_flag(alu_src0_abs) ||
       alu->has_alu_flag(alu_src0_neg))
      return;

   auto src = alu->psrc(0);
   assert(src);

   int override_chan = -1;

   auto ic = src->as_inline_const();
   if (ic) {
      if (ic->sel() == ALU_SRC_0)
         override_chan = 4;

      if (ic->sel() == ALU_SRC_1)
         override_chan = 5;
   }

   auto literal = src->as_literal();
   if (literal) {

      if (literal->value() == 0)
         override_chan = 4;

      if (literal->value() == 0x3F800000)
         override_chan = 5;
   }

   if (override_chan >= 0) {
      vreg[index]->del_use(old_use);
      auto reg = new Register(vreg.sel(), override_chan, vreg[index]->pin());
      vreg.set_value(index, reg);
      success = true;
   }
}

void SimplifySourceVecVisitor::visit(FetchInstr *instr)
{
   (void) instr;
}

void SimplifySourceVecVisitor::visit(Block *instr)
{
   for (auto i = instr->rbegin(); i != instr->rend(); ++i)
      if (!(*i)->is_dead())
         (*i)->accept(*this);
}

void SimplifySourceVecVisitor::visit(ControlFlowInstr *instr)
{
   (void) instr;
}

void SimplifySourceVecVisitor::visit(IfInstr *instr)
{
   (void) instr;
}



}
