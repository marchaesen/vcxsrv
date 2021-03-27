/* -*- mesa-c++  -*-
 *
 * Copyright (c) 2018 Collabora LTD
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

#include "sfn_instruction_alu.h"
#include "sfn_valuepool.h"

namespace r600  {

const AluModifiers AluInstruction::src_abs_flags[2] =
   {alu_src0_abs, alu_src1_abs};
const AluModifiers AluInstruction::src_neg_flags[3] =
   {alu_src0_neg, alu_src1_neg, alu_src2_neg};
const AluModifiers AluInstruction::src_rel_flags[3] =
   {alu_src0_rel, alu_src1_rel, alu_src2_rel};

AluInstruction::AluInstruction(EAluOp opcode):
   Instruction (Instruction::alu),
   m_opcode(opcode),
   m_src(alu_ops.at(opcode).nsrc),
   m_bank_swizzle(alu_vec_unknown),
   m_cf_type(cf_alu)
{
   if (alu_ops.at(opcode).nsrc == 3)
      m_flags.set(alu_op3);
}

AluInstruction::AluInstruction(EAluOp opcode, PValue dest,
                               std::vector<PValue> src,
                               const std::set<AluModifiers>& flags):
   Instruction (Instruction::alu),
   m_opcode(opcode),
   m_dest(dest),
   m_bank_swizzle(alu_vec_unknown),
   m_cf_type(cf_alu)
{
   assert(dest);
   m_src.swap(src);
   for (auto f : flags)
      m_flags.set(f);

   if (alu_ops.at(opcode).nsrc == 3)
      m_flags.set(alu_op3);

   for (auto &s: m_src)
      add_remappable_src_value(&s);

   add_remappable_dst_value(&m_dest);
}

AluInstruction::AluInstruction(EAluOp opcode, PValue dest, PValue src0,
                               const std::set<AluModifiers>& flags):
   AluInstruction(opcode, dest, std::vector<PValue>{src0}, flags)
{
}

AluInstruction::AluInstruction(EAluOp opcode, PValue dest,
                               PValue src0, PValue src1,
                               const std::set<AluModifiers> &m_flags):
   AluInstruction(opcode, dest, {src0, src1}, m_flags)
{
}

AluInstruction::AluInstruction(EAluOp opcode, PValue dest, PValue src0,
                               PValue src1, PValue src2,
                               const std::set<AluModifiers> &flags):
   AluInstruction(opcode, dest, {src0, src1, src2}, flags)
{
}

bool AluInstruction::is_equal_to(const Instruction& lhs) const
{
   assert(lhs.type() == alu);
   const auto& oth = static_cast<const AluInstruction&>(lhs);

   if (m_opcode != oth.m_opcode) {
      return false;
   }

   if (*m_dest != *oth.m_dest)
      return false;

   if (m_src.size() != oth.m_src.size())
      return false;

   for (unsigned i = 0; i < m_src.size(); ++i)
     if (*m_src[i] != *oth.m_src[i]) {
        return false;
     }
   return (m_flags == oth.m_flags && m_cf_type == oth.m_cf_type);
}

void AluInstruction::replace_values(const ValueSet& candidates, PValue new_value)
{
   for (auto c: candidates) {
      if (*c == *m_dest)
         m_dest = new_value;

      for (auto& s: m_src) {
         if (*c == *s)
            s = new_value;
      }
   }
}

PValue AluInstruction::remap_one_registers(PValue reg, std::vector<rename_reg_pair>& map,
                                           ValueMap &values)
{
   auto new_index = map[reg->sel()];
   if (new_index.valid)
      reg = values.get_or_inject(new_index.new_reg, reg->chan());
   map[reg->sel()].used = true;
   return reg;
}


void AluInstruction::set_flag(AluModifiers flag)
{
   m_flags.set(flag);
}

void AluInstruction::set_bank_swizzle(AluBankSwizzle bswz)
{
   m_bank_swizzle = bswz;
}

unsigned AluInstruction::n_sources() const
{
   return m_src.size();
}

void AluInstruction::do_print(std::ostream& os) const
{
   os << "ALU " << alu_ops.at(m_opcode).name;
   if (m_flags.test(alu_dst_clamp))
      os << "_CLAMP";
   if (m_dest)
      os << ' ' << *m_dest << " : "  ;

   for (unsigned i = 0; i < m_src.size(); ++i) {
      int pflags = 0;
      if (i)
         os << ' ';
      if (m_flags.test(src_neg_flags[i])) pflags |= Value::PrintFlags::has_neg;
      if (m_flags.test(src_rel_flags[i])) pflags |= Value::PrintFlags::is_rel;
      if (i < 2)
         if (m_flags.test(src_abs_flags[i])) pflags |= Value::PrintFlags::has_abs;
      m_src[i]->print(os, Value::PrintFlags(0, pflags));
   }
   os << " {";
   os << (m_flags.test(alu_write) ? 'W' : ' ');
   os << (m_flags.test(alu_last_instr) ? 'L' : ' ');
   os << (m_flags.test(alu_update_exec) ? 'E' : ' ');
   os << (m_flags.test(alu_update_pred) ? 'P' : ' ');
   os << "}";

   os <<  " BS:" << m_bank_swizzle;
   os <<  " CF:" << m_cf_type;
}

}
