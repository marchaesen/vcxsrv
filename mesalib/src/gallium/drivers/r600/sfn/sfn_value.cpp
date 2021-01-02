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

#include "sfn_value.h"
#include "util/macros.h"

#include <iostream>
#include <iomanip>
#include <cassert>

namespace r600 {

using std::unique_ptr;
using std::make_shared;

const char *Value::component_names = "xyzw01?_!";

Value::Value():
   m_type(gpr),
   m_chan(0)
{
}

Value::Value(Type type, uint32_t chan):
   m_type(type),
   m_chan(chan)
{

}



Value::Value(Type type):
   Value(type, 0)
{
}

Value::Type Value::type() const
{
   return m_type;
}

void Value::set_chan(uint32_t chan)
{
   m_chan = chan;
}

void Value::print(std::ostream& os) const
{
   do_print(os);
}

void Value::print(std::ostream& os, const PrintFlags& flags) const
{
   if (flags.flags & PrintFlags::has_neg) os << '-';
   if (flags.flags & PrintFlags::has_abs) os << '|';
   do_print(os, flags);
   if (flags.flags & PrintFlags::has_abs) os << '|';
}

void Value::do_print(std::ostream& os, const PrintFlags& flags) const
{
   (void)flags;
   do_print(os);
}

bool Value::operator < (const Value& lhs) const
{
   return sel() < lhs.sel() ||
         (sel() == lhs.sel() && chan() < lhs.chan());
}


LiteralValue::LiteralValue(float value, uint32_t chan):
   Value(Value::literal, chan)
{
   m_value.f=value;
}


LiteralValue::LiteralValue(uint32_t value, uint32_t chan):
   Value(Value::literal, chan)
{
   m_value.u=value;
}

LiteralValue::LiteralValue(int value, uint32_t chan):
   Value(Value::literal, chan)
{
   m_value.u=value;
}

uint32_t LiteralValue::sel() const
{
   return ALU_SRC_LITERAL;
}

uint32_t LiteralValue::value() const
{
   return m_value.u;
}

float LiteralValue::value_float() const
{
   return m_value.f;
}

void LiteralValue::do_print(std::ostream& os) const
{
   os << "[0x" << std::setbase(16) << m_value.u << " " << std::setbase(10)
      << m_value.f << "].";
   os << component_names[chan()];
}

void LiteralValue::do_print(std::ostream& os, UNUSED const PrintFlags& flags) const
{
   os << "[0x" << std::setbase(16) << m_value.u << " "
      << std::setbase(10);

   os << m_value.f << "f";

   os<< "]";
}

bool LiteralValue::is_equal_to(const Value& other) const
{
   assert(other.type() == Value::Type::literal);
   const auto& rhs = static_cast<const LiteralValue&>(other);
   return (sel() == rhs.sel() &&
           value() == rhs.value());
}

InlineConstValue::InlineConstValue(int value, int chan):
   Value(Value::cinline,  chan),
   m_value(static_cast<AluInlineConstants>(value))
{
}

uint32_t InlineConstValue::sel() const
{
   return m_value;
}

void InlineConstValue::do_print(std::ostream& os) const
{
   auto sv_info = alu_src_const.find(m_value);
   if (sv_info != alu_src_const.end()) {
      os << sv_info->second.descr;
      if (sv_info->second.use_chan)
         os << '.' << component_names[chan()];
      else if (chan() > 0)
         os << "." << component_names[chan()]
            << " (W: Channel ignored)";
   } else {
      if (m_value >= ALU_SRC_PARAM_BASE && m_value < ALU_SRC_PARAM_BASE + 32)
         os << " Param" << m_value - ALU_SRC_PARAM_BASE;
      else
         os << " E: unknown inline constant " << m_value;
   }
}

bool InlineConstValue::is_equal_to(const Value& other) const
{
   assert(other.type() == Value::Type::cinline);
   const auto& rhs = static_cast<const InlineConstValue&>(other);
   return sel() == rhs.sel();
}

PValue Value::zero(new InlineConstValue(ALU_SRC_0, 0));
PValue Value::one_f(new InlineConstValue(ALU_SRC_1, 0));
PValue Value::one_i(new InlineConstValue(ALU_SRC_1_INT, 0));
PValue Value::zero_dot_5(new InlineConstValue(ALU_SRC_0_5, 0));

UniformValue::UniformValue(uint32_t sel, uint32_t chan, uint32_t kcache_bank):
   Value(Value::kconst, chan)
{
   m_index = sel;
   m_kcache_bank = kcache_bank;
}

UniformValue::UniformValue(uint32_t sel, uint32_t chan, PValue addr):
   Value(Value::kconst, chan),
   m_index(sel),
   m_kcache_bank(1),
   m_addr(addr)
{

}

uint32_t UniformValue::sel() const
{
   const int bank_base[4] = {128, 160, 256, 288};
   return m_index < 512 ? m_index + bank_base[m_kcache_bank] : m_index;
}

uint32_t UniformValue::kcache_bank() const
{
   return m_kcache_bank;
}

bool UniformValue::is_equal_to(const Value& other) const
{
   const UniformValue& o = static_cast<const UniformValue&>(other);
   return sel()  == o.sel() &&
         m_kcache_bank == o.kcache_bank();
}

void UniformValue::do_print(std::ostream& os) const
{
   if (m_index < 512)
      os << "KC" << m_kcache_bank << "[" << m_index;
   else if (m_addr)
      os << "KC[" << *m_addr << "][" << m_index;
   else
      os << "KCx[" << m_index;
   os << "]." << component_names[chan()];
}

}
