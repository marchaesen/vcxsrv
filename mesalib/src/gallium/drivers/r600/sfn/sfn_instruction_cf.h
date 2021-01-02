/* -*- mesa-c++  -*-
 *
 * Copyright (c) 2018-2019 Collabora LTD
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

#ifndef SFN_IFELSEINSTRUCTION_H
#define SFN_IFELSEINSTRUCTION_H

#include "sfn_instruction_alu.h"

namespace r600  {

class CFInstruction : public Instruction {
protected:
   CFInstruction(instr_type type);
};

class IfElseInstruction : public CFInstruction {
public:
   IfElseInstruction(instr_type type);
};

class IfInstruction : public IfElseInstruction {
public:
   IfInstruction(AluInstruction *pred);
   const AluInstruction& pred() const {return *m_pred;}
private:
   void do_evalue_liveness(LiverangeEvaluator& eval) const override;
   bool is_equal_to(const Instruction& lhs) const override;
   void do_print(std::ostream& os) const override;
   std::shared_ptr<AluInstruction> m_pred;
};

class ElseInstruction : public IfElseInstruction {
public:
   ElseInstruction(IfInstruction *jump_src);
private:
   void do_evalue_liveness(LiverangeEvaluator& eval) const override;
   bool is_equal_to(const Instruction& lhs) const override;
   void do_print(std::ostream& os) const override;

   IfElseInstruction *m_jump_src;
};

class IfElseEndInstruction : public IfElseInstruction {
public:
   IfElseEndInstruction();
private:
   void do_evalue_liveness(LiverangeEvaluator& eval) const override;
   bool is_equal_to(const Instruction& lhs) const override;
   void do_print(std::ostream& os) const override;
};

class LoopBeginInstruction: public CFInstruction {
public:
   LoopBeginInstruction();
private:
   void do_evalue_liveness(LiverangeEvaluator& eval) const override;
   bool is_equal_to(const Instruction& lhs) const override;
   void do_print(std::ostream& os) const override;
};

class LoopEndInstruction: public CFInstruction {
public:
   LoopEndInstruction(LoopBeginInstruction *start);
private:
   void do_evalue_liveness(LiverangeEvaluator& eval) const override;
   bool is_equal_to(const Instruction& lhs) const override;
   void do_print(std::ostream& os) const override;
   LoopBeginInstruction *m_start;
};

class LoopBreakInstruction: public CFInstruction {
public:
   LoopBreakInstruction();
private:
   void do_evalue_liveness(LiverangeEvaluator& eval) const override;
   bool is_equal_to(const Instruction& lhs) const override;
   void do_print(std::ostream& os) const override;
};

class LoopContInstruction: public CFInstruction {
public:
   LoopContInstruction();
private:
   bool is_equal_to(const Instruction& lhs) const override;
   void do_print(std::ostream& os) const override;
};

}

#endif // SFN_IFELSEINSTRUCTION_H
