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


#include <algorithm>
#include <cassert>

#include "sfn_instruction_base.h"
#include "sfn_liverange.h"
#include "sfn_valuepool.h"

namespace r600  {

ValueRemapper::ValueRemapper(std::vector<rename_reg_pair>& m,
                             ValueMap& values):
   m_map(m),
   m_values(values)
{
}

void ValueRemapper::remap(PValue& v)
{
   if (!v)
      return;
   if (v->type() == Value::gpr) {
      v = remap_one_registers(v);
   } else if (v->type() == Value::gpr_array_value) {
      GPRArrayValue& val = static_cast<GPRArrayValue&>(*v);
      auto value = val.value();
      auto addr = val.indirect();
      val.reset_value(remap_one_registers(value));
      if (addr) {
         if (addr->type() == Value::gpr)
            val.reset_addr(remap_one_registers(addr));
      }
      size_t range_start = val.sel();
      size_t range_end = range_start + val.array_size();
      while (range_start < range_end)
         m_map[range_start++].used = true;
   } else if (v->type() == Value::kconst) {
      auto& val = static_cast<UniformValue&>(*v);
      auto addr = val.addr();
      if (addr && addr->type() == Value::gpr)
            val.reset_addr(remap_one_registers(addr));
   }

}

void ValueRemapper::remap(GPRVector& v)
{
   for (int i = 0; i < 4; ++i) {
      if (v.reg_i(i)) {
         auto& ns_idx = m_map[v.reg_i(i)->sel()];
         if (ns_idx.valid)
            v.set_reg_i(i,m_values.get_or_inject(ns_idx.new_reg, v.reg_i(i)->chan()));
         m_map[v.reg_i(i)->sel()].used = true;
      }
   }
}

PValue ValueRemapper::remap_one_registers(PValue& reg)
{
   auto new_index = m_map[reg->sel()];
   if (new_index.valid)
      reg = m_values.get_or_inject(new_index.new_reg, reg->chan());
   m_map[reg->sel()].used = true;
   return reg;
}


Instruction::Instruction(instr_type t):
   m_type(t)
{
}

Instruction::~Instruction()
{
}

void Instruction::print(std::ostream& os) const
{
   os << "OP:";
   do_print(os);
}


void Instruction::remap_registers(ValueRemapper& map)
{
   sfn_log << SfnLog::merge << "REMAP " << *this << "\n";
   for (auto& v: m_mappable_src_registers)
      map.remap(*v);

   for (auto& v: m_mappable_src_vectors)
      map.remap(*v);

   for (auto& v: m_mappable_dst_registers)
      map.remap(*v);

   for (auto& v: m_mappable_dst_vectors)
      map.remap(*v);
   sfn_log << SfnLog::merge << "TO    " << *this << "\n\n";
}

void Instruction::add_remappable_src_value(PValue *v)
{
   if (*v)
      m_mappable_src_registers.push_back(v);
}

void Instruction::add_remappable_src_value(GPRVector *v)
{
   m_mappable_src_vectors.push_back(v);
}

void Instruction::add_remappable_dst_value(PValue *v)
{
   if (v)
      m_mappable_dst_registers.push_back(v);
}

void Instruction::add_remappable_dst_value(GPRVector *v)
{
   m_mappable_dst_vectors.push_back(v);
}

void Instruction::replace_values(UNUSED const ValueSet& candiates, UNUSED PValue new_value)
{

}

void Instruction::evalue_liveness(LiverangeEvaluator& eval) const
{
   sfn_log << SfnLog::merge << "Scan " << *this << "\n";
   for (const auto& s: m_mappable_src_registers)
      if (*s)
         eval.record_read(**s);

   for (const auto& s: m_mappable_src_vectors)
      eval.record_read(*s);

   for (const auto& s: m_mappable_dst_registers)
      if (*s)
         eval.record_write(**s);

   for (const auto& s: m_mappable_dst_vectors)
      eval.record_write(*s);

   do_evalue_liveness(eval);
}

void Instruction::do_evalue_liveness(UNUSED LiverangeEvaluator& eval) const
{

}

bool operator == (const Instruction& lhs, const Instruction& rhs)
{
   if (rhs.m_type != lhs.m_type)
      return false;

   return lhs.is_equal_to(rhs);
}

}
