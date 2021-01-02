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

#ifndef SFN_VALUE_H
#define SFN_VALUE_H

#include "sfn_alu_defines.h"
#include "nir.h"

#include <memory>
#include <set>
#include <bitset>
#include <iostream>

namespace r600 {

class Value {
public:
   using Pointer=std::shared_ptr<Value>;

   struct PrintFlags {
      PrintFlags():index_mode(0),
         flags(0)
      {
      }
      PrintFlags(int im, int f):index_mode(im),
         flags(f)
      {
      }
      int index_mode;
      int flags;
      static const int is_rel = 1;
      static const int has_abs = 2;
      static const int has_neg = 4;
      static const int literal_is_float = 8;
      static const int index_ar = 16;
      static const int index_loopidx = 32;
   };

   enum Type {
      gpr,
      kconst,
      literal,
      cinline,
      lds_direct,
      gpr_vector,
      gpr_array_value,
      unknown
   };

   static const char *component_names;

   using LiteralFlags=std::bitset<4>;

   Value();

   Value(Type type);

   virtual ~Value(){}

   Type type() const;
   virtual uint32_t sel() const = 0;
   uint32_t chan() const {return m_chan;}

   void set_chan(uint32_t chan);
   virtual void set_pin_to_channel() { assert(0 && "Only GPRs can be pinned to a channel ");}
   void print(std::ostream& os, const PrintFlags& flags) const;

   void print(std::ostream& os) const;

   bool operator < (const Value& lhs) const;

   static Value::Pointer zero;
   static Value::Pointer one_f;
   static Value::Pointer zero_dot_5;
   static Value::Pointer one_i;

protected:
   Value(Type type, uint32_t chan);

private:
   virtual void do_print(std::ostream& os) const = 0;
   virtual void do_print(std::ostream& os, const PrintFlags& flags) const;

   virtual bool is_equal_to(const Value& other) const = 0;

   Type m_type;
   uint32_t m_chan;

   friend bool operator == (const Value& lhs, const Value& rhs);
};


inline std::ostream& operator << (std::ostream& os, const Value& v)
{
   v.print(os);
   return os;
}


inline bool operator == (const Value& lhs, const Value& rhs)
{
   if (lhs.type() == rhs.type())
      return lhs.is_equal_to(rhs);
   return false;
}

inline bool operator != (const Value& lhs, const Value& rhs)
{
   return !(lhs == rhs);
}

using PValue=Value::Pointer;

struct value_less {
   inline bool operator () (PValue lhs, PValue rhs) const {
      return *lhs < *rhs;
   }
};

using ValueSet = std::set<PValue, value_less>;


class LiteralValue: public Value {
public:
   LiteralValue(float value, uint32_t chan= 0);
   LiteralValue(uint32_t value, uint32_t chan= 0);
   LiteralValue(int value, uint32_t chan= 0);
   uint32_t sel() const override final;
   uint32_t value() const;
   float value_float() const;
private:
   void do_print(std::ostream& os) const override;
   void do_print(std::ostream& os, const PrintFlags& flags) const override;
   bool is_equal_to(const Value& other) const override;
   union {
      uint32_t u;
      float f;
   } m_value;
};

class InlineConstValue: public Value {
public:
   InlineConstValue(int value, int chan);
   uint32_t sel() const override final;
private:
   void do_print(std::ostream& os) const override;
   bool is_equal_to(const Value& other) const override;
   AluInlineConstants m_value;
};

class UniformValue: public Value {
public:
   UniformValue(uint32_t sel, uint32_t chan, uint32_t kcache_bank = 0);
   UniformValue(uint32_t sel, uint32_t chan, PValue addr);
   uint32_t sel() const override;
   uint32_t kcache_bank() const;
   PValue addr() const {return m_addr;}
   void reset_addr(PValue v) {m_addr = v;}
private:
   void do_print(std::ostream& os) const override;
   bool is_equal_to(const Value& other) const override;

   uint32_t m_index;
   uint32_t m_kcache_bank;
   PValue m_addr;
};

} // end ns r600

#endif
