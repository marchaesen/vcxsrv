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


#include "sfn_instruction_export.h"
#include "sfn_liverange.h"
#include "sfn_valuepool.h"

namespace r600 {

WriteoutInstruction::WriteoutInstruction(instr_type t, const GPRVector& value):
   Instruction(t),
   m_value(value)
{
   add_remappable_src_value(&m_value);
}

void WriteoutInstruction::replace_values(const ValueSet& candidates, PValue new_value)
{
   // I wonder whether we can actually end up here ...
   for (auto c: candidates) {
      if (*c == *m_value.reg_i(c->chan()))
         m_value.set_reg_i(c->chan(), new_value);
   }

   replace_values_child(candidates, new_value);
}

void WriteoutInstruction::replace_values_child(UNUSED const ValueSet& candidates,
                                               UNUSED PValue new_value)
{
}

void WriteoutInstruction::remap_registers_child(UNUSED std::vector<rename_reg_pair>& map,
                                                UNUSED ValueMap& values)
{
}

ExportInstruction::ExportInstruction(unsigned loc, const GPRVector &value, ExportType type):
   WriteoutInstruction(Instruction::exprt, value),
   m_type(type),
   m_loc(loc),
   m_is_last(false)
{
}


bool ExportInstruction::is_equal_to(const Instruction& lhs) const
{
   assert(lhs.type() == exprt);
   const auto& oth = static_cast<const ExportInstruction&>(lhs);

   return (gpr() == oth.gpr()) &&
         (m_type == oth.m_type) &&
         (m_loc == oth.m_loc) &&
         (m_is_last == oth.m_is_last);
}

void ExportInstruction::do_print(std::ostream& os) const
{
   os << (m_is_last ? "EXPORT_DONE ":"EXPORT ");
   switch (m_type) {
   case et_pixel: os << "PIXEL "; break;
   case et_pos: os << "POS "; break;
   case et_param: os << "PARAM "; break;
   }
   os << m_loc << " " << gpr();
}

void ExportInstruction::update_output_map(OutputRegisterMap& map) const
{
   map[m_loc] = gpr_ptr();
}

void ExportInstruction::set_last()
{
   m_is_last = true;
}

WriteScratchInstruction::WriteScratchInstruction(unsigned loc, const GPRVector& value,
                                                 int align, int align_offset, int writemask):
   WriteoutInstruction (Instruction::mem_wr_scratch, value),
   m_loc(loc),
   m_align(align),
   m_align_offset(align_offset),
   m_writemask(writemask),
   m_array_size(0)
{
}

WriteScratchInstruction::WriteScratchInstruction(const PValue& address, const GPRVector& value,
                                                 int align, int align_offset, int writemask, int array_size):
   WriteoutInstruction (Instruction::mem_wr_scratch, value),
   m_loc(0),
   m_address(address),
   m_align(align),
   m_align_offset(align_offset),
   m_writemask(writemask),
   m_array_size(array_size - 1)
{
   add_remappable_src_value(&m_address);
}

bool WriteScratchInstruction::is_equal_to(const Instruction& lhs) const
{
   if (lhs.type() != Instruction::mem_wr_scratch)
      return false;
   const auto& other = static_cast<const WriteScratchInstruction&>(lhs);

   if (m_address) {
      if (!other.m_address)
         return false;
      if (*m_address != *other.m_address)
         return false;
   } else {
      if (other.m_address)
         return false;
   }

   return gpr() == other.gpr() &&
         m_loc == other.m_loc &&
         m_align == other.m_align &&
         m_align_offset == other.m_align_offset &&
         m_writemask == other.m_writemask;
}

static char *writemask_to_swizzle(int writemask, char *buf)
{
   const char *swz = "xyzw";
   for (int i = 0; i < 4; ++i) {
      buf[i] = (writemask & (1 << i)) ? swz[i] : '_';
   }
   return buf;
}

void WriteScratchInstruction::do_print(std::ostream& os) const
{
   char buf[5];

   os << "MEM_SCRATCH_WRITE ";
   if (m_address)
      os << "@" << *m_address << "+";

   os << m_loc  << "." << writemask_to_swizzle(m_writemask, buf)
      << " " <<  gpr()  << " AL:" << m_align << " ALO:" << m_align_offset;
}

void WriteScratchInstruction::replace_values_child(const ValueSet& candidates, PValue new_value)
{
   if (!m_address)
      return;

   for (auto c: candidates) {
      if (*c == *m_address)
         m_address = new_value;
   }
}

void WriteScratchInstruction::remap_registers_child(std::vector<rename_reg_pair>& map,
                           ValueMap& values)
{
   if (!m_address)
      return;
   sfn_log << SfnLog::merge << "Remap " << *m_address <<  " of type " << m_address->type() << "\n";
   assert(m_address->type() == Value::gpr);
   auto new_index = map[m_address->sel()];
   if (new_index.valid)
      m_address = values.get_or_inject(new_index.new_reg, m_address->chan());
   map[m_address->sel()].used = true;
}

StreamOutIntruction::StreamOutIntruction(const GPRVector& value, int num_components,
                                         int array_base, int comp_mask, int out_buffer,
                                         int stream):
   WriteoutInstruction(Instruction::streamout, value),
   m_element_size(num_components == 3 ? 3 : num_components - 1),
   m_burst_count(1),
   m_array_base(array_base),
   m_array_size(0xfff),
   m_writemask(comp_mask),
   m_output_buffer(out_buffer),
   m_stream(stream)
{
}

unsigned StreamOutIntruction::op() const
{
   int op = 0;
   switch (m_output_buffer) {
   case 0: op = CF_OP_MEM_STREAM0_BUF0; break;
   case 1: op = CF_OP_MEM_STREAM0_BUF1; break;
   case 2: op = CF_OP_MEM_STREAM0_BUF2; break;
   case 3: op = CF_OP_MEM_STREAM0_BUF3; break;
   }
   return 4 * m_stream + op;
}

bool StreamOutIntruction::is_equal_to(const Instruction& lhs) const
{
   assert(lhs.type() == streamout);
   const auto& oth = static_cast<const StreamOutIntruction&>(lhs);

   return gpr() == oth.gpr() &&
         m_element_size == oth.m_element_size &&
         m_burst_count == oth.m_burst_count &&
         m_array_base == oth.m_array_base &&
         m_array_size == oth.m_array_size &&
         m_writemask == oth.m_writemask &&
         m_output_buffer == oth.m_output_buffer &&
         m_stream == oth.m_stream;
}

void StreamOutIntruction::do_print(std::ostream& os) const
{
   os << "WRITE STREAM(" << m_stream << ") "  << gpr()
      << " ES:" << m_element_size
      << " BC:" << m_burst_count
      << " BUF:" << m_output_buffer
      << " ARRAY:" <<  m_array_base;
   if (m_array_size != 0xfff)
      os << "+" << m_array_size;
}

MemRingOutIntruction::MemRingOutIntruction(ECFOpCode ring, EMemWriteType type,
                                           const GPRVector& value,
                                           unsigned base_addr, unsigned ncomp,
                                           PValue index):
   WriteoutInstruction(Instruction::ring, value),
   m_ring_op(ring),
   m_type(type),
   m_base_address(base_addr),
   m_num_comp(ncomp),
   m_index(index)
{
   add_remappable_src_value(&m_index);

   assert(m_ring_op  == cf_mem_ring || m_ring_op  == cf_mem_ring1||
          m_ring_op  == cf_mem_ring2 || m_ring_op  == cf_mem_ring3);
   assert(m_num_comp <= 4);
}

unsigned MemRingOutIntruction::ncomp() const
{
   switch (m_num_comp) {
   case 1: return 0;
   case 2: return 1;
   case 3:
   case 4: return 3;
   default:
      assert(0);
   }
   return 3;
}

bool MemRingOutIntruction::is_equal_to(const Instruction& lhs) const
{
   assert(lhs.type() == streamout);
   const auto& oth = static_cast<const MemRingOutIntruction&>(lhs);

   bool equal = gpr() == oth.gpr() &&
                m_ring_op == oth.m_ring_op &&
                m_type == oth.m_type &&
                m_num_comp == oth.m_num_comp &&
                m_base_address == oth.m_base_address;

   if (m_type == mem_write_ind || m_type == mem_write_ind_ack)
      equal &= (*m_index == *oth.m_index);
   return equal;

}

static const char *write_type_str[4] = {"WRITE", "WRITE_IDX", "WRITE_ACK", "WRITE_IDX_ACK" };
void MemRingOutIntruction::do_print(std::ostream& os) const
{
   os << "MEM_RING " << m_ring_op;
   os << " " << write_type_str[m_type] << " " << m_base_address;
   os << " " << gpr();
   if (m_type == mem_write_ind || m_type == mem_write_ind_ack)
      os << " @" << *m_index;
   os << " ES:" << m_num_comp;
}


void MemRingOutIntruction::replace_values_child(const ValueSet& candidates,
                                                PValue new_value)
{
   if (!m_index)
      return;

   for (auto c: candidates) {
      if (*c == *m_index)
         m_index = new_value;
   }
}

void MemRingOutIntruction::remap_registers_child(std::vector<rename_reg_pair>& map,
                                                 ValueMap& values)
{
   if (!m_index)
      return;

   assert(m_index->type() == Value::gpr);
   auto new_index = map[m_index->sel()];
   if (new_index.valid)
      m_index = values.get_or_inject(new_index.new_reg, m_index->chan());
   map[m_index->sel()].used = true;
}

void MemRingOutIntruction::patch_ring(int stream, PValue index)
{
   const ECFOpCode ring_op[4] = {cf_mem_ring, cf_mem_ring1, cf_mem_ring2, cf_mem_ring3};

   assert(stream < 4);
   m_ring_op = ring_op[stream];
   m_index = index;
}

}
