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

#ifndef SFN_GPRARRAY_H
#define SFN_GPRARRAY_H

#include "sfn_value.h"
#include <vector>
#include <array>

namespace r600 {

class ValuePool;
class ValueMap;
class LiverangeEvaluator;

class GPRValue : public Value {
public:
   GPRValue() = default;
   GPRValue(GPRValue&& orig) = default;
   GPRValue(const GPRValue& orig) = default;

   GPRValue(uint32_t sel, uint32_t chan, int base_offset);

   GPRValue(uint32_t sel, uint32_t chan);

   GPRValue& operator = (const GPRValue& orig) = default;
   GPRValue& operator = (GPRValue&& orig) = default;

   uint32_t sel() const override final;

   void set_as_input(){ m_input = true; }
   bool is_input() const {return  m_input; }
   void set_keep_alive() { m_keep_alive = true; }
   bool keep_alive() const {return  m_keep_alive; }
   void set_pin_to_channel() override { m_pin_to_channel = true;}
   bool pin_to_channel()  const { return m_pin_to_channel;}

private:
   void do_print(std::ostream& os) const override;
   void do_print(std::ostream& os, const PrintFlags& flags) const override;
   bool is_equal_to(const Value& other) const override;
   uint32_t m_sel;
   bool m_base_offset;
   bool m_input;
   bool m_pin_to_channel;
   bool m_keep_alive;
};

using PGPRValue = std::shared_ptr<GPRValue>;

class GPRVector : public Value {
public:
   using Swizzle = std::array<uint32_t,4>;
   using Values = std::array<PValue,4>;
   GPRVector() = default;
   GPRVector(GPRVector&& orig) = default;
   GPRVector(const GPRVector& orig);

   GPRVector(const GPRVector& orig, const std::array<uint8_t, 4>& swizzle);
   GPRVector(std::array<PValue,4> elms);
   GPRVector(uint32_t sel, std::array<uint32_t,4> swizzle);

   GPRVector& operator = (const GPRVector& orig) = default;
   GPRVector& operator = (GPRVector&& orig) = default;

   void swizzle(const Swizzle& swz);

   uint32_t sel() const override final;

   void set_reg_i(int i, PValue reg);

   unsigned chan_i(int i) const {return m_elms[i]->chan();}
   PValue reg_i(int i) const {return m_elms[i];}
   PValue operator [] (int i) const {return m_elms[i];}
   PValue& operator [] (int i) {return m_elms[i];}

   void pin_to_channel(int i);
   void pin_all_to_channel();

   PValue x() const {return m_elms[0];}
   PValue y() const {return m_elms[1];}
   PValue z() const {return m_elms[2];}
   PValue w() const {return m_elms[3];}

   Values& values() { return m_elms;}

private:
   void do_print(std::ostream& os) const override;
   bool is_equal_to(const Value& other) const override;
   void validate() const;

   Values m_elms;
   mutable bool m_valid;
};


class GPRArray : public Value
{
public:
   using Pointer = std::shared_ptr<GPRArray>;

   GPRArray(int base, int size, int comp_mask, int frac);

   uint32_t sel() const override;

   size_t size() const {return m_values.size();}

   PValue get_indirect(unsigned index, PValue indirect, unsigned component);

   void record_read(LiverangeEvaluator& ev, int chan)const;
   void record_write(LiverangeEvaluator& ev, int chan)const;

   void collect_registers(ValueMap& output) const;

private:
   void do_print(std::ostream& os) const override;

   bool is_equal_to(const Value& other) const override;

   int m_base_index;
   int m_component_mask;
   int m_frac;

   std::vector<GPRVector> m_values;
};

using PGPRArray = GPRArray::Pointer;

class GPRArrayValue :public Value {
public:
   GPRArrayValue(PValue value, GPRArray *array);
   GPRArrayValue(PValue value, PValue index, GPRArray *array);

   void record_read(LiverangeEvaluator& ev) const;
   void record_write(LiverangeEvaluator& ev) const;

   size_t array_size() const;
   uint32_t sel() const override;

   PValue value() {return m_value;}

   void reset_value(PValue new_value);
   void reset_addr(PValue new_addr);

   Value::Pointer indirect() const {return m_addr;}

private:

   void do_print(std::ostream& os) const override;

   bool is_equal_to(const Value& other) const override;

   PValue m_value;
   PValue m_addr;
   GPRArray *m_array;
};

inline size_t GPRArrayValue::array_size() const
{
   return m_array->size();
}

inline GPRVector::Swizzle swizzle_from_comps(unsigned ncomp)
{
   GPRVector::Swizzle swz = {0,1,2,3};
   for (int i = ncomp; i < 4; ++i)
      swz[i] = 7;
   return swz;
}

inline GPRVector::Swizzle swizzle_from_mask(unsigned mask)
{
   GPRVector::Swizzle swz;
   for (int i = 0; i < 4; ++i)
      swz[i] =  ((1 << i) & mask) ? i : 7;
   return swz;
}


}

#endif // SFN_GPRARRAY_H
