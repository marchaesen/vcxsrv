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

#ifndef sfn_r600_instr_h
#define sfn_r600_instr_h

#include "sfn_value_gpr.h"
#include "sfn_defines.h"

#include "gallium/drivers/r600/r600_isa.h"
#include <iostream>
#include <memory>
#include <vector>
#include <set>

namespace r600 {


struct rename_reg_pair {
   bool valid;
   bool used;
   int new_reg;
};

class LiverangeEvaluator;
class ValueMap;


class ValueRemapper {
public:
   ValueRemapper(std::vector<rename_reg_pair>& m,
                 ValueMap& values);

   void remap(PValue& v);
   void remap(GPRVector& v);
private:
   PValue remap_one_registers(PValue& reg);

   std::vector<rename_reg_pair>& m_map;
   ValueMap& m_values;
};


using OutputRegisterMap = std::map<unsigned, const GPRVector *>;

class Instruction {
public:
   enum instr_type {
      alu,
      exprt,
      tex,
      vtx,
      wait_ack,
      cond_if,
      cond_else,
      cond_endif,
      lds_atomic,
      lds_read,
      lds_write,
      loop_begin,
      loop_end,
      loop_break,
      loop_continue,
      phi,
      streamout,
      ring,
      emit_vtx,
      mem_wr_scratch,
      gds,
      rat,
      tf_write,
      block,
      unknown
   };

   typedef std::shared_ptr<Instruction> Pointer;

   friend bool operator == (const Instruction& lhs, const Instruction& rhs);

   Instruction(instr_type t);

   virtual ~Instruction();

   instr_type type() const { return m_type;}

   void print(std::ostream& os) const;

   virtual void replace_values(const ValueSet& candidates, PValue new_value);

   void evalue_liveness(LiverangeEvaluator& eval) const;

   void remap_registers(ValueRemapper& map);

protected:

   void add_remappable_src_value(PValue *v);
   void add_remappable_src_value(GPRVector *v);
   void add_remappable_dst_value(PValue *v);
   void add_remappable_dst_value(GPRVector *v);

private:

   virtual void do_evalue_liveness(LiverangeEvaluator& eval) const;

   virtual bool is_equal_to(const Instruction& lhs) const = 0;

   instr_type m_type;

   virtual void do_print(std::ostream& os) const = 0;

   std::vector<PValue*> m_mappable_src_registers;
   std::vector<GPRVector*> m_mappable_src_vectors;
   std::vector<PValue*> m_mappable_dst_registers;
   std::vector<GPRVector*> m_mappable_dst_vectors;
};

using PInstruction=Instruction::Pointer;

inline std::ostream& operator << (std::ostream& os, const Instruction& instr)
{
   instr.print(os);
   return os;
}

bool operator == (const Instruction& lhs, const Instruction& rhs);

}

#endif
