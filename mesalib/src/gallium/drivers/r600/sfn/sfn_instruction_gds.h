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

#ifndef SFN_GDSINSTR_H
#define SFN_GDSINSTR_H

#include "sfn_instruction_base.h"

#include <bitset>

namespace r600 {

class GDSInstr : public Instruction
{
public:
   GDSInstr(ESDOp op, const GPRVector& dest,  const PValue& value,
            const PValue &uav_id, int uav_base);
   GDSInstr(ESDOp op, const GPRVector& dest,  const PValue& value,
            const PValue& value2, const PValue &uav_id, int uav_base);
   GDSInstr(ESDOp op, const GPRVector& dest,  const PValue &uav_id, int uav_base);

   ESDOp op() const {return m_op;}

   int src_sel() const {
      if (!m_src)
         return 0;

      assert(m_src->type() == Value::gpr);
      return m_src->sel();
   }

   int src2_chan() const {
      if (!m_src2)
         return 0;

      assert(m_src->type() == Value::gpr);
      return m_src->chan();
   }

   int src_swizzle(int idx) const {assert(idx < 3); return m_src_swizzle[idx];}

   int dest_sel() const {
      return m_dest.sel();
   }

   int dest_swizzle(int i) const {
      if (i < 4)
         return m_dest_swizzle[i];
      return 7;
   }

   void set_dest_swizzle(const std::array<int,4>& swz) {
      m_dest_swizzle = swz;
   }

   PValue uav_id() const {return m_uav_id;}
   int uav_base() const {return m_uav_base;}

private:

   bool is_equal_to(const Instruction& lhs) const override;
   void do_print(std::ostream& os) const override;

   ESDOp m_op;

   PValue m_src;
   PValue m_src2;
   GPRVector m_dest;
   std::array <int, 4> m_dest_swizzle;
   std::array <int, 3> m_src_swizzle;

   EBufferIndexMode m_buffer_index_mode;
   PValue m_uav_id;
   int m_uav_base;
   std::bitset<8> m_flags;

};

class RatInstruction : public Instruction {

public:
   enum ERatOp {
      NOP,
      STORE_TYPED,
      STORE_RAW,
      STORE_RAW_FDENORM,
      CMPXCHG_INT,
      CMPXCHG_FLT,
      CMPXCHG_FDENORM,
      ADD,
      SUB,
      RSUB,
      MIN_INT,
      MIN_UINT,
      MAX_INT,
      MAX_UINT,
      AND,
      OR,
      XOR,
      MSKOR,
      INC_UINT,
      DEC_UINT,
      NOP_RTN = 32,
      XCHG_RTN = 34,
      XCHG_FDENORM_RTN,
      CMPXCHG_INT_RTN,
      CMPXCHG_FLT_RTN,
      CMPXCHG_FDENORM_RTN,
      ADD_RTN,
      SUB_RTN,
      RSUB_RTN,
      MIN_INT_RTN,
      MIN_UINT_RTN,
      MAX_INT_RTN,
      MAX_UINT_RTN,
      AND_RTN,
      OR_RTN,
      XOR_RTN,
      MSKOR_RTN,
      UINT_RTN,
      UNSUPPORTED
   };

   RatInstruction(ECFOpCode cf_opcode, ERatOp rat_op,
                  const GPRVector& data, const GPRVector& index,
                  int rat_id, const PValue& rat_id_offset,
                  int burst_count, int comp_mask, int element_size,
                  bool ack);

   PValue rat_id_offset() const { return m_rat_id_offset;}
   int  rat_id() const { return m_rat_id;}

   ERatOp rat_op() const {return m_rat_op;}

   int data_gpr() const {return m_data.sel();}
   int index_gpr() const {return m_index.sel();}
   int elm_size() const {return m_element_size;}

   int comp_mask() const {return m_comp_mask;}

   bool need_ack() const {return m_need_ack;}
   int burst_count() const {return m_burst_count;}

   static ERatOp opcode(nir_intrinsic_op opcode);

   int data_swz(int chan) const {return m_data.chan_i(chan);}

   ECFOpCode cf_opcode() const { return m_cf_opcode;}

   void set_ack() {m_need_ack = true; }

private:

   bool is_equal_to(const Instruction& lhs) const override;
   void do_print(std::ostream& os) const override;

   ECFOpCode m_cf_opcode;
   ERatOp m_rat_op;

   GPRVector m_data;
   GPRVector m_index;

   int m_rat_id;
   PValue m_rat_id_offset;
   int m_burst_count;
   int m_comp_mask;
   int m_element_size;

   std::bitset<8> m_flags;

   bool m_need_ack;

};

class GDSStoreTessFactor : public Instruction {
public:
      GDSStoreTessFactor(GPRVector& value);
      int sel() const {return m_value.sel();}
      int chan(int i ) const {return m_value.chan_i(i);}

      void replace_values(const ValueSet& candidates, PValue new_value) override;
private:
      bool is_equal_to(const Instruction& lhs) const override;
      void do_print(std::ostream& os) const override;

      GPRVector m_value;
};

}

#endif // SFN_GDSINSTR_H
