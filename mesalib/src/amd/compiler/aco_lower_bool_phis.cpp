/*
 * Copyright © 2019 Valve Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Rhys Perry (pendingchaos02@gmail.com)
 *
 */

#include <map>

#include "aco_ir.h"
#include "aco_builder.h"


namespace aco {

struct phi_use {
   Block *block;
   unsigned phi_def;

   bool operator<(const phi_use& other) const {
      return std::make_tuple(block, phi_def) <
             std::make_tuple(other.block, other.phi_def);
   }
};

struct ssa_state {
   std::map<unsigned, unsigned> latest;
   std::map<unsigned, std::map<phi_use, uint64_t>> phis;
};

Operand get_ssa(Program *program, unsigned block_idx, ssa_state *state)
{
   while (true) {
      auto pos = state->latest.find(block_idx);
      if (pos != state->latest.end())
         return Operand({pos->second, s2});

      Block& block = program->blocks[block_idx];
      size_t pred = block.linear_preds.size();
      if (pred == 0) {
         return Operand(s2);
      } else if (pred == 1) {
         block_idx = block.linear_preds[0];
         continue;
      } else {
         unsigned res = program->allocateId();
         state->latest[block_idx] = res;

         aco_ptr<Pseudo_instruction> phi{create_instruction<Pseudo_instruction>(aco_opcode::p_linear_phi, Format::PSEUDO, pred, 1)};
         for (unsigned i = 0; i < pred; i++) {
            phi->operands[i] = get_ssa(program, block.linear_preds[i], state);
            if (phi->operands[i].isTemp()) {
               assert(i < 64);
               state->phis[phi->operands[i].tempId()][(phi_use){&block, res}] |= (uint64_t)1 << i;
            }
         }
         phi->definitions[0] = Definition(Temp{res, s2});
         block.instructions.emplace(block.instructions.begin(), std::move(phi));

         return Operand({res, s2});
      }
   }
}

void update_phi(Program *program, ssa_state *state, Block *block, unsigned phi_def, uint64_t operand_mask) {
   for (auto& phi : block->instructions) {
      if (phi->opcode != aco_opcode::p_phi && phi->opcode != aco_opcode::p_linear_phi)
         break;
      if (phi->opcode != aco_opcode::p_linear_phi)
         continue;
      if (phi->definitions[0].tempId() != phi_def)
         continue;
      assert(ffsll(operand_mask) <= phi->operands.size());

      uint64_t operands = operand_mask;
      while (operands) {
         unsigned operand = u_bit_scan64(&operands);
         Operand new_operand = get_ssa(program, block->linear_preds[operand], state);
         phi->operands[operand] = new_operand;
         if (!new_operand.isUndefined())
            state->phis[new_operand.tempId()][(phi_use){block, phi_def}] |= (uint64_t)1 << operand;
      }
      return;
   }
   assert(false);
}

Temp write_ssa(Program *program, Block *block, ssa_state *state, unsigned previous) {
   unsigned id = program->allocateId();
   state->latest[block->index] = id;

   /* update phis */
   if (previous) {
      std::map<phi_use, uint64_t> phis;
      phis.swap(state->phis[previous]);
      for (auto& phi : phis)
         update_phi(program, state, phi.first.block, phi.first.phi_def, phi.second);
   }

   return {id, s2};
}

void insert_before_branch(Block *block, aco_ptr<Instruction> instr)
{
   int end = block->instructions.size() - 1;
   if (block->instructions[end]->format == Format::PSEUDO_BRANCH)
      block->instructions.emplace(std::prev(block->instructions.end()), std::move(instr));
   else
      block->instructions.emplace_back(std::move(instr));
}

void insert_before_logical_end(Block *block, aco_ptr<Instruction> instr)
{
   for (int i = block->instructions.size() - 1; i >= 0; --i) {
      if (block->instructions[i]->opcode == aco_opcode::p_logical_end) {
         block->instructions.emplace(std::next(block->instructions.begin(), i), std::move(instr));
         return;
      }
   }
   insert_before_branch(block, std::move(instr));
}

aco_ptr<Instruction> lower_divergent_bool_phi(Program *program, Block *block, aco_ptr<Instruction>& phi)
{
   Builder bld(program);

   ssa_state state;
   for (unsigned i = 0; i < phi->operands.size(); i++) {
      Block *pred = &program->blocks[block->logical_preds[i]];

      if (phi->operands[i].isUndefined())
         continue;

      assert(phi->operands[i].isTemp());
      Temp phi_src = phi->operands[i].getTemp();
      if (phi_src.regClass() == s1) {
         Temp new_phi_src = bld.tmp(s2);
         insert_before_logical_end(pred,
            bld.sop2(aco_opcode::s_cselect_b64, Definition(new_phi_src),
                     Operand((uint32_t)-1), Operand(0u), bld.scc(phi_src)).get_ptr());
         phi_src = new_phi_src;
      }
      assert(phi_src.regClass() == s2);

      Operand cur = get_ssa(program, pred->index, &state);
      Temp new_cur = write_ssa(program, pred, &state, cur.isTemp() ? cur.tempId() : 0);

      if (cur.isUndefined()) {
         insert_before_logical_end(pred, bld.sop1(aco_opcode::s_mov_b64, Definition(new_cur), phi_src).get_ptr());
      } else {
         Temp tmp1 = bld.tmp(s2), tmp2 = bld.tmp(s2);
         insert_before_logical_end(pred,
            bld.sop2(aco_opcode::s_andn2_b64, Definition(tmp1), bld.def(s1, scc),
                     cur, Operand(exec, s2)).get_ptr());
         insert_before_logical_end(pred,
            bld.sop2(aco_opcode::s_and_b64, Definition(tmp2), bld.def(s1, scc),
                     phi_src, Operand(exec, s2)).get_ptr());
         insert_before_logical_end(pred,
            bld.sop2(aco_opcode::s_or_b64, Definition(new_cur), bld.def(s1, scc),
                     tmp1, tmp2).get_ptr());
      }
   }

   return bld.sop1(aco_opcode::s_mov_b64, phi->definitions[0], get_ssa(program, block->index, &state)).get_ptr();
}

void lower_linear_bool_phi(Program *program, Block *block, aco_ptr<Instruction>& phi)
{
   Builder bld(program);

   for (unsigned i = 0; i < phi->operands.size(); i++) {
      if (!phi->operands[i].isTemp())
         continue;

      Temp phi_src = phi->operands[i].getTemp();
      if (phi_src.regClass() == s2) {
         Temp new_phi_src = bld.tmp(s1);
         insert_before_logical_end(&program->blocks[block->linear_preds[i]],
            bld.sopc(aco_opcode::s_cmp_lg_u64, bld.scc(Definition(new_phi_src)),
                     Operand(0u), phi_src).get_ptr());
         phi->operands[i].setTemp(new_phi_src);
      }
   }
}

void lower_bool_phis(Program* program)
{
   for (Block& block : program->blocks) {
      std::vector<aco_ptr<Instruction>> instructions;
      std::vector<aco_ptr<Instruction>> non_phi;
      instructions.swap(block.instructions);
      block.instructions.reserve(instructions.size());
      unsigned i = 0;
      for (; i < instructions.size(); i++)
      {
         aco_ptr<Instruction>& phi = instructions[i];
         if (phi->opcode != aco_opcode::p_phi && phi->opcode != aco_opcode::p_linear_phi)
            break;
         if (phi->opcode == aco_opcode::p_phi && phi->definitions[0].regClass() == s2) {
            non_phi.emplace_back(std::move(lower_divergent_bool_phi(program, &block, phi)));
         } else if (phi->opcode == aco_opcode::p_linear_phi && phi->definitions[0].regClass() == s1) {
            /* if it's a valid non-boolean phi, this should be a no-op */
            lower_linear_bool_phi(program, &block, phi);
            block.instructions.emplace_back(std::move(phi));
         } else {
            block.instructions.emplace_back(std::move(phi));
         }
      }
      for (auto&& instr : non_phi) {
         assert(instr->opcode != aco_opcode::p_phi && instr->opcode != aco_opcode::p_linear_phi);
         block.instructions.emplace_back(std::move(instr));
      }
      for (; i < instructions.size(); i++) {
         aco_ptr<Instruction> instr = std::move(instructions[i]);
         assert(instr->opcode != aco_opcode::p_phi && instr->opcode != aco_opcode::p_linear_phi);
         block.instructions.emplace_back(std::move(instr));
      }
   }
}

}
