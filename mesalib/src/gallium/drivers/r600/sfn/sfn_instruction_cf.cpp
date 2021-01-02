/* -*- mesa-c++  -*-
 *
 * Copyright (c) 2019 Collabora LTD
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

#include "sfn_instruction_cf.h"
#include "sfn_liverange.h"

namespace  r600 {

CFInstruction::CFInstruction(instr_type type):Instruction(type)
{

}

IfElseInstruction::IfElseInstruction(instr_type type):
   CFInstruction (type)
{

}

IfInstruction::IfInstruction(AluInstruction *pred):
   IfElseInstruction(cond_if),
   m_pred(pred)
{
   PValue *v = m_pred->psrc(0);
   add_remappable_src_value(v);
}

void IfInstruction::do_evalue_liveness(LiverangeEvaluator& eval) const
{
   eval.scope_if();
}

bool IfInstruction::is_equal_to(const Instruction& lhs) const
{
   assert(lhs.type() == cond_if);
   const IfInstruction& l = static_cast<const IfInstruction&>(lhs);
   return *l.m_pred == *m_pred;
}

void IfInstruction::do_print(std::ostream& os) const
{
   os << "PRED = " << *m_pred << "\n";
   os << "IF (PRED)";
}

ElseInstruction::ElseInstruction(IfInstruction *jump_src):
   IfElseInstruction(cond_else),
   m_jump_src(jump_src)
{
}

void ElseInstruction::do_evalue_liveness(LiverangeEvaluator& eval) const
{
   eval.scope_else();
}


bool ElseInstruction::is_equal_to(const Instruction& lhs) const
{
   if (lhs.type() != cond_else)
      return false;
   auto& l = static_cast<const ElseInstruction&>(lhs);
   return (*m_jump_src == *l.m_jump_src);
}

void ElseInstruction::do_print(std::ostream& os) const
{
   os << "ELSE";
}

IfElseEndInstruction::IfElseEndInstruction():
   IfElseInstruction(cond_endif)
{
}

void IfElseEndInstruction::do_evalue_liveness(LiverangeEvaluator& eval) const
{
   eval.scope_endif();
}

bool IfElseEndInstruction::is_equal_to(const Instruction& lhs) const
{
   if (lhs.type() != cond_endif)
      return false;
   return true;
}

void IfElseEndInstruction::do_print(std::ostream& os) const
{
   os << "ENDIF";
}

LoopBeginInstruction::LoopBeginInstruction():
   CFInstruction(loop_begin)
{
}

void LoopBeginInstruction::do_evalue_liveness(LiverangeEvaluator& eval) const
{
   eval.scope_loop_begin();
}

bool LoopBeginInstruction::is_equal_to(const Instruction& lhs) const
{
   assert(lhs.type() == loop_begin);
   return true;
}

void LoopBeginInstruction::do_print(std::ostream& os) const
{
   os << "BGNLOOP";
}

LoopEndInstruction::LoopEndInstruction(LoopBeginInstruction *start):
   CFInstruction (loop_end),
   m_start(start)
{
}

void LoopEndInstruction::do_evalue_liveness(LiverangeEvaluator& eval) const
{
   eval.scope_loop_end();
}

bool LoopEndInstruction::is_equal_to(const Instruction& lhs) const
{
   assert(lhs.type() == loop_end);
   const auto& other = static_cast<const LoopEndInstruction&>(lhs);
   return *m_start == *other.m_start;
}

void LoopEndInstruction::do_print(std::ostream& os) const
{
   os << "ENDLOOP";
}

LoopBreakInstruction::LoopBreakInstruction():
   CFInstruction (loop_break)
{
}

void LoopBreakInstruction::do_evalue_liveness(LiverangeEvaluator& eval) const
{
   eval.scope_loop_break();
}

bool LoopBreakInstruction::is_equal_to(UNUSED const Instruction& lhs) const
{
   return true;
}

void LoopBreakInstruction::do_print(std::ostream& os) const
{
   os << "BREAK";
}

LoopContInstruction::LoopContInstruction():
   CFInstruction (loop_continue)
{
}

bool LoopContInstruction::is_equal_to(UNUSED const Instruction& lhs) const
{
   return true;
}
void LoopContInstruction::do_print(std::ostream& os) const
{
   os << "CONTINUE";
}

}
