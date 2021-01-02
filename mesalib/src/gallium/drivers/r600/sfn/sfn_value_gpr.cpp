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

#include "sfn_value_gpr.h"
#include "sfn_valuepool.h"
#include "sfn_debug.h"
#include "sfn_liverange.h"

namespace r600 {

using std::vector;
using std::array;

GPRValue::GPRValue(uint32_t sel, uint32_t chan, int base_offset):
   Value(Value::gpr, chan),
   m_sel(sel),
   m_base_offset(base_offset),
   m_input(false),
   m_pin_to_channel(false),
   m_keep_alive(false)
{
}

GPRValue::GPRValue(uint32_t sel, uint32_t chan):
   Value(Value::gpr, chan),
   m_sel(sel),
   m_base_offset(0),
   m_input(false),
   m_pin_to_channel(false),
   m_keep_alive(false)
{
}

uint32_t GPRValue::sel() const
{
   return m_sel;
}

void GPRValue::do_print(std::ostream& os) const
{
   os << 'R';
   os << m_sel;
   os << '.' << component_names[chan()];
}

bool GPRValue::is_equal_to(const Value& other) const
{
   assert(other.type() == Value::Type::gpr);
   const auto& rhs = static_cast<const GPRValue&>(other);
   return (sel() == rhs.sel() &&
           chan() == rhs.chan());
}

void GPRValue::do_print(std::ostream& os, UNUSED const PrintFlags& flags) const
{
   os << 'R';
   os << m_sel;
   os << '.' << component_names[chan()];
}

GPRVector::GPRVector(const GPRVector& orig):
   Value(gpr_vector),
   m_elms(orig.m_elms),
   m_valid(orig.m_valid)
{
}

GPRVector::GPRVector(std::array<PValue,4> elms):
   Value(gpr_vector),
   m_elms(elms),
   m_valid(false)
{
   for (unsigned i = 0; i < 4; ++i)
      if (!m_elms[i] || (m_elms[i]->type() != Value::gpr)) {
         assert(0 && "GPR vector not valid because element missing or nit a GPR");
         return;
      }
   unsigned sel = m_elms[0]->sel();
   for (unsigned i = 1; i < 4; ++i)
      if (m_elms[i]->sel() != sel) {
         assert(0 && "GPR vector not valid because sel is not equal for all elements");
         return;
      }
   m_valid = true;
}

GPRVector::GPRVector(uint32_t sel, std::array<uint32_t,4> swizzle):
   Value (gpr_vector),
   m_valid(true)
{
   for (int i = 0; i < 4; ++i)
      m_elms[i] = PValue(new GPRValue(sel, swizzle[i]));
}

GPRVector::GPRVector(const GPRVector& orig, const std::array<uint8_t,4>& swizzle)
{
      for (int i = 0; i < 4; ++i)
         m_elms[i] = orig.reg_i(swizzle[i]);
      m_valid = orig.m_valid;
}

void GPRVector::validate() const
{
   assert(m_elms[0]);
   uint32_t sel = m_elms[0]->sel();
   if (sel >= 124)
      return;

   for (unsigned i = 1; i < 4; ++i) {
      assert(m_elms[i]);
      if (sel != m_elms[i]->sel())
         return;
   }

   m_valid = true;
}

uint32_t GPRVector::sel() const
{
   validate();
   assert(m_valid);
   return m_elms[0] ? m_elms[0]->sel() : 999;
}

void GPRVector::set_reg_i(int i, PValue reg)
{
   m_elms[i] = reg;
}

void GPRVector::pin_to_channel(int i)
{
   auto& v = static_cast<GPRValue&>(*m_elms[i]);
   v.set_pin_to_channel();
}

void GPRVector::pin_all_to_channel()
{
   for (auto& v: m_elms) {
      auto& c = static_cast<GPRValue&>(*v);
      c.set_pin_to_channel();
   }
}

void GPRVector::do_print(std::ostream& os) const
{
   os << "R" << sel() << ".";
   for (int i = 0; i < 4; ++i)
      os << (m_elms[i] ? component_names[m_elms[i]->chan() < 8 ? m_elms[i]->chan() : 8] : '?');
}

void GPRVector::swizzle(const Swizzle& swz)
{
   Values v(m_elms);
   for (uint32_t i = 0; i < 4; ++i)
      if (i != swz[i]) {
         assert(swz[i] < 4);
         m_elms[i] = v[swz[i]];
      }
}

bool GPRVector::is_equal_to(const Value& other) const
{
   if (other.type() != gpr_vector) {
      std::cerr << "t";
      return false;
   }

   const GPRVector& o = static_cast<const GPRVector&>(other);

   for (int i = 0; i < 4; ++i) {
      if (*m_elms[i] != *o.m_elms[i]) {
         std::cerr << "elm" << i;
         return false;
      }
   }
   return true;
}


GPRArrayValue::GPRArrayValue(PValue value, PValue addr, GPRArray *array):
   Value(gpr_array_value, value->chan()),
   m_value(value),
   m_addr(addr),
   m_array(array)
{
}

GPRArrayValue::GPRArrayValue(PValue value, GPRArray *array):
   Value(gpr_array_value, value->chan()),
   m_value(value),
   m_array(array)
{
}

static const char *swz_char = "xyzw01_";

void GPRArrayValue::do_print(std::ostream& os) const
{
   assert(m_array);
   os << "R"  << m_value->sel();
   if (m_addr) {
      os <<  "[" << *m_addr  << "] ";
   }
   os << swz_char[m_value->chan()];

   os << "(" << *m_array << ")";
}

bool GPRArrayValue::is_equal_to(const Value& other) const
{
   const GPRArrayValue& v = static_cast<const GPRArrayValue&>(other);

   return *m_value == *v.m_value &&
         *m_array == *v.m_array;
}

void GPRArrayValue::record_read(LiverangeEvaluator& ev) const
{
   if (m_addr) {
      ev.record_read(*m_addr);
      unsigned chan = m_value->chan();
      assert(m_array);
      m_array->record_read(ev, chan);
   } else
      ev.record_read(*m_value);
}

void GPRArrayValue::record_write(LiverangeEvaluator& ev) const
{
   if (m_addr) {
      ev.record_read(*m_addr);
      unsigned chan = m_value->chan();
      assert(m_array);
      m_array->record_write(ev, chan);
   } else
      ev.record_write(*m_value);
}

void GPRArrayValue::reset_value(PValue new_value)
{
   m_value = new_value;
}

void GPRArrayValue::reset_addr(PValue new_addr)
{
   m_addr = new_addr;
}


GPRArray::GPRArray(int base, int size, int mask, int frac):
   Value (gpr_vector),
   m_base_index(base),
   m_component_mask(mask),
   m_frac(frac)
{
   m_values.resize(size);
   for (int i = 0; i < size; ++i) {
      for (int j = 0; j < 4; ++j) {
         if (mask & (1 << j))
            m_values[i].set_reg_i(j, PValue(new GPRValue(base + i, j)));
      }
   }
}

uint32_t GPRArray::sel() const
{
   return m_base_index;
}

static const char *compchar = "xyzw";
void GPRArray::do_print(std::ostream& os) const
{
   os << "ARRAY[R" << sel() << "..R" << sel() + m_values.size()  - 1 << "].";
   for (int j = 0; j < 4; ++j) {
      if (m_component_mask & (1 << j))
         os << compchar[j];
   }
}

bool GPRArray::is_equal_to(const Value& other) const
{
   const GPRArray& o = static_cast<const GPRArray&>(other);
   return o.sel() == sel() &&
         o.m_values.size() == m_values.size() &&
         o.m_component_mask == m_component_mask;
}

uint32_t GPRArrayValue::sel() const
{
   return m_value->sel();
}

PValue GPRArray::get_indirect(unsigned index, PValue indirect, unsigned component)
{
   assert(index < m_values.size());
   assert(m_component_mask & (1 << (component + m_frac)));

   sfn_log << SfnLog::reg << "Create indirect register from " << *this;

   PValue v = m_values[index].reg_i(component + m_frac);
   assert(v);

   sfn_log << SfnLog::reg << " ->  " << *v;

   if (indirect) {
      sfn_log << SfnLog::reg << "["  << *indirect << "]";
      switch (indirect->type()) {
      case Value::literal: {
         const LiteralValue& lv = static_cast<const LiteralValue&>(*indirect);
         v = m_values[lv.value()].reg_i(component + m_frac);
         break;
      }
      case Value::gpr:  {
         v = PValue(new GPRArrayValue(v, indirect, this));
         sfn_log << SfnLog::reg << "(" << *v << ")";
         break;
      }
      default:
         assert(0 && !"Indirect addressing must be literal value or GPR");
      }
   }
   sfn_log << SfnLog::reg <<"  -> " << *v << "\n";
   return v;
}

void GPRArray::record_read(LiverangeEvaluator& ev, int chan) const
{
   for (auto& v: m_values)
      ev.record_read(*v.reg_i(chan), true);
}

void GPRArray::record_write(LiverangeEvaluator& ev, int chan) const
{
   for (auto& v: m_values)
      ev.record_write(*v.reg_i(chan), true);
}

void GPRArray::collect_registers(ValueMap& output) const
{
   for (auto& v: m_values) {
      for (int i = 0; i < 4; ++i) {
         auto vv = v.reg_i(i);
         if (vv)
            output.insert(vv);
      }
   }
}

}
