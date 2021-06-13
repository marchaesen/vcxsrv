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

#include "sfn_instruction_gds.h"
#include "sfn_liverange.h"

namespace  r600 {

GDSInstr::GDSInstr(ESDOp op, const GPRVector& dest,  const PValue& value,
                   const PValue& value2, const PValue& uav_id, int uav_base):
   Instruction(gds),
   m_op(op),
   m_src(value),
   m_src2(value2),
   m_dest(dest),
   m_dest_swizzle({PIPE_SWIZZLE_X,7,7,7}),
   m_src_swizzle({PIPE_SWIZZLE_0, PIPE_SWIZZLE_X, PIPE_SWIZZLE_0}),
   m_buffer_index_mode(bim_none),
   m_uav_id(uav_id),
   m_uav_base(uav_base),
   m_flags(0)
{
   add_remappable_src_value(&m_src);
   add_remappable_src_value(&m_src2);
   add_remappable_src_value(&m_uav_id);
   add_remappable_dst_value(&m_dest);
   m_dest_swizzle[0] = m_dest.chan_i(0);
}

GDSInstr::GDSInstr(ESDOp op, const GPRVector& dest,  const PValue& value,
                   const PValue& uav_id, int uav_base):
   GDSInstr(op, dest,  value, PValue(), uav_id, uav_base)
{
      assert(value);
      m_src_swizzle[1] = value->chan();
      m_src_swizzle[2] = PIPE_SWIZZLE_0;
}

GDSInstr::GDSInstr(ESDOp op, const GPRVector& dest,
                   const PValue& uav_id, int uav_base):
   GDSInstr(op, dest,  PValue(), PValue(), uav_id, uav_base)
{
   m_src_swizzle[1] = PIPE_SWIZZLE_0;
}

bool GDSInstr::is_equal_to(UNUSED const Instruction& lhs) const
{
   return false;
}

void GDSInstr::do_print(std::ostream& os) const
{
   const char *swz = "xyzw01?_";
   os << lds_ops.at(m_op).name << " R" << m_dest.sel() << ".";
   for (int i = 0; i < 4; ++i) {
      os << swz[m_dest_swizzle[i]];
   }
   if (m_src)
      os << " " << *m_src;

   os << " UAV:" << *m_uav_id;
}

RatInstruction::RatInstruction(ECFOpCode cf_opcode, ERatOp rat_op,
                               const GPRVector& data, const GPRVector& index,
                               int rat_id, const PValue& rat_id_offset,
                               int burst_count, int comp_mask, int element_size, bool ack):
   Instruction(rat),
   m_cf_opcode(cf_opcode),
   m_rat_op(rat_op),
   m_data(data),
   m_index(index),
   m_rat_id(rat_id),
   m_rat_id_offset(rat_id_offset),
   m_burst_count(burst_count),
   m_comp_mask(comp_mask),
   m_element_size(element_size),
   m_need_ack(ack)
{
   add_remappable_src_value(&m_data);
   add_remappable_src_value(&m_rat_id_offset);
   add_remappable_src_value(&m_index);
}

bool RatInstruction::is_equal_to(UNUSED const Instruction& lhs) const
{
   return false;
}

void RatInstruction::do_print(std::ostream& os) const
{
   os << "MEM_RAT RAT(" << m_rat_id;
   if (m_rat_id_offset)
      os << "+" << *m_rat_id_offset;
   os << ") @" << m_index;
   os << " OP:" << m_rat_op << " " << m_data;
   os << " BC:" << m_burst_count
      << " MASK:" << m_comp_mask
      << " ES:" << m_element_size;
   if (m_need_ack)
      os << " ACK";
}

RatInstruction::ERatOp RatInstruction::opcode(nir_intrinsic_op opcode)
{
   switch (opcode) {
   case nir_intrinsic_ssbo_atomic_add:
      return ADD_RTN;
   case nir_intrinsic_ssbo_atomic_and:
      return AND_RTN;
   case nir_intrinsic_ssbo_atomic_exchange:
      return XCHG_RTN;
   case nir_intrinsic_ssbo_atomic_umax:
      return MAX_UINT_RTN;
   case nir_intrinsic_ssbo_atomic_umin:
      return MIN_UINT_RTN;
   case nir_intrinsic_ssbo_atomic_imax:
      return MAX_INT_RTN;
   case nir_intrinsic_ssbo_atomic_imin:
      return MIN_INT_RTN;
   case nir_intrinsic_ssbo_atomic_xor:
      return XOR_RTN;
   default:
      return UNSUPPORTED;
   }
}

GDSStoreTessFactor::GDSStoreTessFactor(GPRVector& value):
   Instruction(tf_write),
   m_value(value)
{
   add_remappable_src_value(&m_value);
}

void GDSStoreTessFactor::replace_values(const ValueSet& candidates, PValue new_value)
{
   for (auto& c: candidates) {
      for (int i = 0; i < 4; ++i) {
         if (*c == *m_value[i])
            m_value[i] = new_value;
      }
   }
}


bool GDSStoreTessFactor::is_equal_to(const Instruction& lhs) const
{
   auto& other = static_cast<const GDSStoreTessFactor&>(lhs);
   return m_value == other.m_value;
}

void GDSStoreTessFactor::do_print(std::ostream& os) const
{
   os << "TF_WRITE " << m_value;
}

}
