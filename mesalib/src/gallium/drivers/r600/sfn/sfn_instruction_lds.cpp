#include "sfn_instruction_lds.h"

namespace r600 {

void LDSReadInstruction::do_print(std::ostream& os) const
{
   os << "LDS Read  [";
   for (auto& v : m_dest_value)
      os << *v << " ";
   os << "], ";
   for (auto& a : m_address)
      os << *a << " ";
}

LDSReadInstruction::LDSReadInstruction(std::vector<PValue>& address, std::vector<PValue>& value):
   Instruction(lds_read),
   m_address(address),
   m_dest_value(value)
{
   assert(address.size() == value.size());

   for (unsigned i = 0; i < address.size(); ++i) {
      add_remappable_src_value(&m_address[i]);
      add_remappable_dst_value(&m_dest_value[i]);
   }
}

void LDSReadInstruction::replace_values(const ValueSet& candidates, PValue new_value)
{
   for (auto& c : candidates) {
      for (auto& d: m_dest_value) {
         if (*c == *d)
            d = new_value;
      }

      for (auto& a: m_address) {
         if (*c == *a)
            a = new_value;
      }
   }
}

bool LDSReadInstruction::is_equal_to(const Instruction& lhs) const
{
   auto& other = static_cast<const LDSReadInstruction&>(lhs);
   return m_address == other.m_address &&
         m_dest_value == other.m_dest_value;
}

LDSAtomicInstruction::LDSAtomicInstruction(PValue& dest, PValue& src0, PValue src1, PValue& address, unsigned op):
   Instruction(lds_atomic),
   m_address(address),
   m_dest_value(dest),
   m_src0_value(src0),
   m_src1_value(src1),
   m_opcode(op)
{
   add_remappable_src_value(&m_src0_value);
   add_remappable_src_value(&m_src1_value);
   add_remappable_src_value(&m_address);
   add_remappable_dst_value(&m_dest_value);
}

LDSAtomicInstruction::LDSAtomicInstruction(PValue& dest, PValue& src0, PValue& address, unsigned op):
   LDSAtomicInstruction(dest, src0, PValue(), address, op)
{

}


void LDSAtomicInstruction::do_print(std::ostream& os) const
{
   os << "LDS " << m_opcode << " " << *m_dest_value << " ";
   os << "[" << *m_address << "] " << *m_src0_value;
   if (m_src1_value)
      os << ", " << *m_src1_value;
}

bool LDSAtomicInstruction::is_equal_to(const Instruction& lhs) const
{
   auto& other = static_cast<const LDSAtomicInstruction&>(lhs);

   return m_opcode == other.m_opcode &&
         *m_dest_value == *other.m_dest_value &&
         *m_src0_value == *other.m_src0_value &&
         *m_address == *other.m_address &&
         ((m_src1_value && other.m_src1_value && (*m_src1_value == *other.m_src1_value)) ||
          (!m_src1_value && !other.m_src1_value));
}

LDSWriteInstruction::LDSWriteInstruction(PValue address, unsigned idx_offset, PValue value0):
   LDSWriteInstruction::LDSWriteInstruction(address, idx_offset, value0, PValue())

{
}

LDSWriteInstruction::LDSWriteInstruction(PValue address, unsigned idx_offset, PValue value0, PValue value1):
   Instruction(lds_write),
   m_address(address),
   m_value0(value0),
   m_value1(value1),
   m_idx_offset(idx_offset)
{
   add_remappable_src_value(&m_address);
   add_remappable_src_value(&m_value0);
   if (m_value1)
      add_remappable_src_value(&m_value1);
}


void LDSWriteInstruction::do_print(std::ostream& os) const
{
   os << "LDS Write" << num_components()
      << " " << address() << ", " << value0();
   if (num_components() > 1)
      os << ", " << value1();
}

void LDSWriteInstruction::replace_values(const ValueSet& candidates, PValue new_value)
{
   for (auto c: candidates) {
      if (*c == *m_address)
         m_address = new_value;

      if (*c == *m_value0)
         m_value0 = new_value;

      if (*c == *m_value1)
         m_value1 = new_value;
   }
}

bool LDSWriteInstruction::is_equal_to(const Instruction& lhs) const
{
   auto& other = static_cast<const LDSWriteInstruction&>(lhs);

   if (m_value1) {
      if (!other.m_value1)
         return false;
      if (*m_value1 != *other.m_value1)
         return false;
   } else {
      if (other.m_value1)
         return false;
   }

   return (m_value0 != other.m_value0 &&
           *m_address != *other.m_address);
}

} // namespace r600
