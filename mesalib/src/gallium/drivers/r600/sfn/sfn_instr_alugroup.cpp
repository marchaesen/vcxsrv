/* -*- mesa-c++  -*-
 *
 * Copyright (c) 2022 Collabora LTD
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

#include "sfn_instr_alugroup.h"
#include "sfn_debug.h"
#include <algorithm>

namespace r600 {

AluGroup::AluGroup()
{
   std::fill(m_slots.begin(), m_slots.end(), nullptr);
}

bool AluGroup::add_instruction(AluInstr *instr)
{
   /* we can only schedule one op that accesses LDS or
     the LDS read queue */
   if (m_has_lds_op && instr->has_lds_access())
      return false;

   if (instr->has_alu_flag(alu_is_trans)) {
      auto opinfo = alu_ops.find(instr->opcode());
      assert(opinfo->second.can_channel(AluOp::t, s_chip_class));
      if (add_trans_instructions(instr))
         return true;
   }

   if (add_vec_instructions(instr) && !instr->has_alu_flag(alu_is_trans)) {
      instr->set_parent_group(this);
      return true;
   }

   auto opinfo = alu_ops.find(instr->opcode());
   assert(opinfo != alu_ops.end());

   if (s_max_slots > 4 &&
       opinfo->second.can_channel(AluOp::t, s_chip_class) &&
       add_trans_instructions(instr)) {
      instr->set_parent_group(this);
      return true;
   }

   return false;
}

bool AluGroup::add_trans_instructions(AluInstr *instr)
{
   if (m_slots[4] || s_max_slots < 5)
      return false;

   if (!update_indirect_access(instr))
      return false;

   /* LDS instructions have to be scheduled in X */
   if (instr->has_alu_flag(alu_is_lds))
      return false;

   auto opinfo = alu_ops.find(instr->opcode());
   assert(opinfo != alu_ops.end());

   if (!opinfo->second.can_channel(AluOp::t, s_chip_class))
      return false;

   /* if we schedule a non-trans instr into the trans slot, we have to make
    * sure that the corresponding vector slot is already occupied, otherwise
    * the hardware will schedule it as vector op and the bank-swizzle as
    * checked here (and in r600_asm.c) will not catch conflicts.
    */
   if (!instr->has_alu_flag(alu_is_trans) && !m_slots[instr->dest_chan()]) {
      if (instr->dest() && instr->dest()->pin() == pin_free) {
         int used_slot = 3;
         while (!m_slots[used_slot] && used_slot >= 0)
            --used_slot;

         // if we schedule a non-trans instr into the trans slot,
         // there should always be some slot that is already used
         assert(used_slot >= 0);
         instr->dest()->set_chan(used_slot);
      }
   }

   for (AluBankSwizzle i = sq_alu_scl_201; i != sq_alu_scl_unknown ; ++i) {
      AluReadportReservation readports_evaluator = m_readports_evaluator;
      if (readports_evaluator.schedule_trans_instruction(*instr, i)) {
         m_readports_evaluator = readports_evaluator;
         m_slots[4] = instr;
         instr->pin_sources_to_chan();
         sfn_log << SfnLog::schedule << "T: " << *instr << "\n";

         /* We added a vector op in the trans channel, so we have to
          * make sure the corresponding vector channel is used */
         if (!instr->has_alu_flag(alu_is_trans) && !m_slots[instr->dest_chan()])
            m_slots[instr->dest_chan()] =
                  new AluInstr(op0_nop, instr->dest_chan());
         return true;
      }
   }
   return false;
}

int AluGroup::free_slots() const
{
   int free_mask = 0;
   for(int i = 0; i < s_max_slots; ++i) {
      if (!m_slots[i])
         free_mask |= 1 << i;
   }
   return free_mask;
}

class AluAllowSlotSwitch : public AluInstrVisitor {
public:
   using AluInstrVisitor::visit;

   void visit(AluInstr *alu) {
      if (alu->alu_slots() != 1) {
         if (alu->has_alu_flag(alu_is_cayman_trans)) {
            free_mask &= (1 << alu->alu_slots()) - 1;
         } else {
            yes = false;
         }
      }
   }

   bool yes{true};
   uint8_t free_mask{0xf};

};

bool AluGroup::add_vec_instructions(AluInstr *instr)
{
   if (!update_indirect_access(instr))
      return false;

   int param_src = -1;
   for (auto& s : instr->sources()) {
      auto is = s->as_inline_const();
      if (is)
         param_src = is->sel() - ALU_SRC_PARAM_BASE;
   }

   if (param_src >= 0) {
      if (m_param_used < 0)
         m_param_used = param_src;
      else if (m_param_used != param_src)
         return false;
   }

   if (m_has_lds_op && instr->has_lds_access())
      return false;

   int preferred_chan = instr->dest_chan();
   if (!m_slots[preferred_chan]) {
      if (instr->bank_swizzle() != alu_vec_unknown) {
         if (try_readport(instr, instr->bank_swizzle()))
             return true;
      } else {
         for (AluBankSwizzle i = alu_vec_012; i != alu_vec_unknown; ++i) {
            if (try_readport(instr, i))
               return true;
         }
      }
   } else {

      auto dest = instr->dest();
      if (dest && dest->pin() == pin_free) {

         AluAllowSlotSwitch swich_allowed;
         for (auto u : dest->uses()) {
            u->accept(swich_allowed);
            if (!swich_allowed.yes)
               return false;
         }

         int free_chan = 0;
         while (free_chan < 4 && (m_slots[free_chan] ||
                !(swich_allowed.free_mask & (1 << free_chan))))
            free_chan++;

         if (free_chan < 4) {
            sfn_log << SfnLog::schedule << "V: Try force channel " << free_chan << "\n";
            dest->set_chan(free_chan);
            if (instr->bank_swizzle() != alu_vec_unknown) {
               if (try_readport(instr, instr->bank_swizzle()))
                  return true;
            } else {
               for (AluBankSwizzle i = alu_vec_012; i != alu_vec_unknown; ++i) {
                  if (try_readport(instr, i))
                     return true;
               }
            }
         }
      }
   }
   return false;
}

bool AluGroup::try_readport(AluInstr *instr, AluBankSwizzle cycle)
{
   int preferred_chan = instr->dest_chan();
   AluReadportReservation readports_evaluator = m_readports_evaluator;
   if (readports_evaluator.schedule_vec_instruction(*instr, cycle)) {
      m_readports_evaluator = readports_evaluator;
      m_slots[preferred_chan] = instr;
      m_has_lds_op |= instr->has_lds_access();
      sfn_log << SfnLog::schedule << "V: " << *instr << "\n";
      auto dest = instr->dest();
      if (dest && dest->pin() == pin_free)
         dest->set_pin(pin_chan);
      instr->pin_sources_to_chan();
      return true;
   }
   return false;
}

bool AluGroup::update_indirect_access(AluInstr *instr)
{
   auto [indirect_addr, for_src, is_index ] = instr->indirect_addr();

   if (indirect_addr) {
      if (!m_addr_used) {
         m_addr_used = indirect_addr;
         m_addr_for_src = for_src;
         m_addr_is_index = is_index;
      } else if (!indirect_addr->equal_to(*m_addr_used)) {
         return false;
      }
   }

   return true;
}

void AluGroup::accept(ConstInstrVisitor& visitor) const
{
   visitor.visit(*this);
}

void AluGroup::accept(InstrVisitor& visitor)
{
   visitor.visit(this);
}

void AluGroup::set_scheduled()
{
   for (int i = 0; i < s_max_slots; ++i) {
      if (m_slots[i])
         m_slots[i]->set_scheduled();
   }
}

void AluGroup::fix_last_flag()
{
   bool last_seen = false;
   for (int i = s_max_slots - 1; i >= 0; --i) {
      if (m_slots[i]) {
         if (!last_seen) {
            m_slots[i]->set_alu_flag(alu_last_instr);
            last_seen = true;
         } else {
            m_slots[i]->reset_alu_flag(alu_last_instr);
         }
      }
   }
}

bool AluGroup::is_equal_to(const AluGroup& other) const
{
   for (int i = 0; i < s_max_slots; ++i) {
      if (!other.m_slots[i]) {
         if (!m_slots[i])
            continue;
         else
            return false;
      }

      if (m_slots[i]) {
         if (!other.m_slots[i])
            return false;
         else if (!m_slots[i]->is_equal_to(*other.m_slots[i]))
            return false;
      }
   }
   return true;
}

bool AluGroup::has_lds_group_end() const
{
   for (int i = 0; i < s_max_slots; ++i) {
      if (m_slots[i] && m_slots[i]->has_alu_flag(alu_lds_group_end))
         return true;
   }
   return false;
}

bool AluGroup::do_ready() const
{
   for (int i = 0; i < s_max_slots; ++i) {
      if (m_slots[i] && !m_slots[i]->ready())
         return false;
   }
   return true;
}

void AluGroup::forward_set_blockid(int id, int index)
{
   for (int i = 0; i < s_max_slots; ++i) {
      if (m_slots[i]) {
         m_slots[i]->set_blockid(id, index);
      }
   }
}

uint32_t AluGroup::slots() const
{
   uint32_t result = (m_readports_evaluator.m_nliterals + 1) >> 1;
   for (int i = 0; i < s_max_slots; ++i) {
      if (m_slots[i])
         ++result;
   }
   if (m_addr_used) {
      ++result;
      if (m_addr_is_index)
         ++result;
   }

   return result;
}

void AluGroup::do_print(std::ostream& os) const
{
   const char slotname[] = "xyzwt";

   os << "ALU_GROUP_BEGIN\n";
   for (int i = 0; i < s_max_slots; ++i) {
      if (m_slots[i]) {
         for (int j = 0; j < 2 * m_nesting_depth + 4; ++j)
            os << ' ';
         os << slotname[i] << ": ";
         m_slots[i]->print(os);
         os << "\n";
      }
   }
   for (int i = 0; i < 2 * m_nesting_depth + 2; ++i)
      os << ' ';
   os << "ALU_GROUP_END";
}

AluInstr::SrcValues AluGroup::get_kconsts() const
{
   AluInstr::SrcValues result;

   for (int i = 0; i < s_max_slots; ++i) {
      if (m_slots[i]) {
         for (auto s : m_slots[i]->sources())
            if (s->as_uniform())
               result.push_back(s);
      }
   }
   return result;
}

void AluGroup::set_chipclass(r600_chip_class chip_class)
{
   s_chip_class = chip_class;
   s_max_slots  = chip_class == ISA_CC_CAYMAN ? 4 : 5;    
}

int AluGroup::s_max_slots = 5;
r600_chip_class AluGroup::s_chip_class = ISA_CC_EVERGREEN;
}
