#include "sfn_instruction_block.h"

namespace r600 {


InstructionBlock::InstructionBlock(unsigned nesting_depth, unsigned block_number):
   Instruction(block),
   m_block_number(block_number),
   m_nesting_depth(nesting_depth)
{
}

void InstructionBlock::emit(PInstruction instr)
{
   m_block.push_back(instr);
}

void InstructionBlock::remap_registers(ValueRemapper& map)
{
   for(auto& i: m_block)
      i->remap_registers(map);
}

void InstructionBlock::do_evalue_liveness(LiverangeEvaluator& eval) const
{
   for(auto& i: m_block)
      i->evalue_liveness(eval);
}

bool InstructionBlock::is_equal_to(const Instruction& lhs) const
{
   assert(lhs.type() == block);
   auto& l = static_cast<const InstructionBlock&>(lhs);

   if (m_block.size() != l.m_block.size())
      return false;

   if (m_block_number != l.m_block_number)
      return false;

   return std::equal(m_block.begin(), m_block.end(), l.m_block.begin(),
                     [](PInstruction ri, PInstruction li) {return *ri == *li;});
}

PInstruction InstructionBlock::last_instruction()
{
   return m_block.size() ? *m_block.rbegin() : nullptr;
}

void InstructionBlock::do_print(std::ostream& os) const
{
   std::string space(" ", 2 * m_nesting_depth);
   for(auto& i: m_block)
      os << space << *i << "\n";
}

}
