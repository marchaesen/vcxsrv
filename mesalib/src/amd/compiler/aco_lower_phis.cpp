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
#include <algorithm>


namespace aco {

struct ssa_state {
   bool checked_preds_for_uniform;
   bool all_preds_uniform;

   bool needs_init;
   uint64_t cur_undef_operands;

   unsigned phi_block_idx;
   unsigned loop_nest_depth;
   std::map<unsigned, unsigned> writes;
   std::vector<Operand> latest;
   std::vector<bool> visited;
};

Operand get_ssa(Program *program, unsigned block_idx, ssa_state *state, bool before_write)
{
   if (!before_write) {
      auto it = state->writes.find(block_idx);
      if (it != state->writes.end())
         return Operand(Temp(it->second, program->lane_mask));
      if (state->visited[block_idx])
         return state->latest[block_idx];
   }

   state->visited[block_idx] = true;

   Block& block = program->blocks[block_idx];
   size_t pred = block.linear_preds.size();
   if (pred == 0 || block.loop_nest_depth < state->loop_nest_depth) {
      return Operand(program->lane_mask);
   } else if (block.loop_nest_depth > state->loop_nest_depth) {
      Operand op = get_ssa(program, block_idx - 1, state, false);
      state->latest[block_idx] = op;
      return op;
   } else if (pred == 1 || block.kind & block_kind_loop_exit) {
      Operand op = get_ssa(program, block.linear_preds[0], state, false);
      state->latest[block_idx] = op;
      return op;
   } else if (block.kind & block_kind_loop_header &&
              !(program->blocks[state->phi_block_idx].kind & block_kind_loop_exit)) {
      return Operand(program->lane_mask);
   } else {
      Temp res = Temp(program->allocateTmp(program->lane_mask));
      state->latest[block_idx] = Operand(res);

      Operand *const ops = (Operand *)alloca(pred * sizeof(Operand));
      for (unsigned i = 0; i < pred; i++)
         ops[i] = get_ssa(program, block.linear_preds[i], state, false);

      bool all_undef = true;
      for (unsigned i = 0; i < pred; i++)
         all_undef = all_undef && ops[i].isUndefined();
      if (all_undef) {
         state->latest[block_idx] = ops[0];
         return ops[0];
      }

      aco_ptr<Pseudo_instruction> phi{create_instruction<Pseudo_instruction>(aco_opcode::p_linear_phi, Format::PSEUDO, pred, 1)};
      for (unsigned i = 0; i < pred; i++)
         phi->operands[i] = ops[i];
      phi->definitions[0] = Definition(res);
      block.instructions.emplace(block.instructions.begin(), std::move(phi));

      return Operand(res);
   }
}

void insert_before_logical_end(Block *block, aco_ptr<Instruction> instr)
{
   auto IsLogicalEnd = [] (const aco_ptr<Instruction>& inst) -> bool {
      return inst->opcode == aco_opcode::p_logical_end;
   };
   auto it = std::find_if(block->instructions.crbegin(), block->instructions.crend(), IsLogicalEnd);

   if (it == block->instructions.crend()) {
      assert(block->instructions.back()->isBranch());
      block->instructions.insert(std::prev(block->instructions.end()), std::move(instr));
   } else {
      block->instructions.insert(std::prev(it.base()), std::move(instr));
   }
}

void build_merge_code(Program *program, Block *block, Definition dst, Operand prev, Operand cur)
{
   Builder bld(program);

   auto IsLogicalEnd = [] (const aco_ptr<Instruction>& instr) -> bool {
      return instr->opcode == aco_opcode::p_logical_end;
   };
   auto it = std::find_if(block->instructions.rbegin(), block->instructions.rend(), IsLogicalEnd);
   assert(it != block->instructions.rend());
   bld.reset(&block->instructions, std::prev(it.base()));

   if (prev.isUndefined()) {
      bld.copy(dst, cur);
      return;
   }

   bool prev_is_constant = prev.isConstant() && prev.constantValue() + 1u < 2u;
   bool cur_is_constant = cur.isConstant() && cur.constantValue() + 1u < 2u;

   if (!prev_is_constant) {
      if (!cur_is_constant) {
         Temp tmp1 = bld.tmp(bld.lm), tmp2 = bld.tmp(bld.lm);
         bld.sop2(Builder::s_andn2, Definition(tmp1), bld.def(s1, scc), prev, Operand(exec, bld.lm));
         bld.sop2(Builder::s_and, Definition(tmp2), bld.def(s1, scc), cur, Operand(exec, bld.lm));
         bld.sop2(Builder::s_or, dst, bld.def(s1, scc), tmp1, tmp2);
      } else if (cur.constantValue()) {
         bld.sop2(Builder::s_or, dst, bld.def(s1, scc), prev, Operand(exec, bld.lm));
      } else {
         bld.sop2(Builder::s_andn2, dst, bld.def(s1, scc), prev, Operand(exec, bld.lm));
      }
   } else if (prev.constantValue()) {
      if (!cur_is_constant)
         bld.sop2(Builder::s_orn2, dst, bld.def(s1, scc), cur, Operand(exec, bld.lm));
      else if (cur.constantValue())
         bld.copy(dst, Operand(UINT32_MAX, bld.lm == s2));
      else
         bld.sop1(Builder::s_not, dst, bld.def(s1, scc), Operand(exec, bld.lm));
   } else {
      if (!cur_is_constant)
         bld.sop2(Builder::s_and, dst, bld.def(s1, scc), cur, Operand(exec, bld.lm));
      else if (cur.constantValue())
         bld.copy(dst, Operand(exec, bld.lm));
      else
         bld.copy(dst, Operand(0u, bld.lm == s2));
   }
}

void lower_divergent_bool_phi(Program *program, ssa_state *state, Block *block, aco_ptr<Instruction>& phi)
{
   Builder bld(program);

   if (!state->checked_preds_for_uniform) {
      state->all_preds_uniform = !(block->kind & block_kind_merge) &&
                                 block->linear_preds.size() == block->logical_preds.size();
      for (unsigned pred : block->logical_preds)
         state->all_preds_uniform = state->all_preds_uniform && (program->blocks[pred].kind & block_kind_uniform);
      state->checked_preds_for_uniform = true;
   }

   if (state->all_preds_uniform) {
      phi->opcode = aco_opcode::p_linear_phi;
      return;
   }

   state->latest.resize(program->blocks.size());
   state->visited.resize(program->blocks.size());

   uint64_t undef_operands = 0;
   for (unsigned i = 0; i < phi->operands.size(); i++)
      undef_operands |= phi->operands[i].isUndefined() << i;

   if (state->needs_init || undef_operands != state->cur_undef_operands ||
       block->logical_preds.size() > 64) {
      /* this only has to be done once per block unless the set of predecessors
       * which are undefined changes */
      state->cur_undef_operands = undef_operands;
      state->phi_block_idx = block->index;
      state->loop_nest_depth = block->loop_nest_depth;
      if (block->kind & block_kind_loop_exit) {
         state->loop_nest_depth += 1;
      }
      state->writes.clear();
      state->needs_init = false;
   }
   std::fill(state->latest.begin(), state->latest.end(), Operand(program->lane_mask));
   std::fill(state->visited.begin(), state->visited.end(), false);

   for (unsigned i = 0; i < phi->operands.size(); i++) {
      if (phi->operands[i].isUndefined())
         continue;

      state->writes[block->logical_preds[i]] = program->allocateId(program->lane_mask);
   }

   bool uniform_merge = block->kind & block_kind_loop_header;

   for (unsigned i = 0; i < phi->operands.size(); i++) {
      Block *pred = &program->blocks[block->logical_preds[i]];

      bool need_get_ssa = !uniform_merge;
      if (block->kind & block_kind_loop_header && !(pred->kind & block_kind_uniform))
         uniform_merge = false;

      if (phi->operands[i].isUndefined())
         continue;

      Operand cur(bld.lm);
      if (need_get_ssa)
         cur = get_ssa(program, pred->index, state, true);
      assert(cur.regClass() == bld.lm);

      Temp new_cur = {state->writes.at(pred->index), program->lane_mask};
      assert(new_cur.regClass() == bld.lm);

      if (i == 1 && (block->kind & block_kind_merge) && phi->operands[0].isConstant())
         cur = phi->operands[0];
      build_merge_code(program, pred, Definition(new_cur), cur, phi->operands[i]);
   }

   unsigned num_preds = block->linear_preds.size();
   if (phi->operands.size() != num_preds) {
      Pseudo_instruction* new_phi{create_instruction<Pseudo_instruction>(aco_opcode::p_linear_phi, Format::PSEUDO, num_preds, 1)};
      new_phi->definitions[0] = phi->definitions[0];
      phi.reset(new_phi);
   } else {
      phi->opcode = aco_opcode::p_linear_phi;
   }
   assert(phi->operands.size() == num_preds);

   for (unsigned i = 0; i < num_preds; i++)
      phi->operands[i] = get_ssa(program, block->linear_preds[i], state, false);

   return;
}

void lower_subdword_phis(Program *program, Block *block, aco_ptr<Instruction>& phi)
{
   Builder bld(program);
   for (unsigned i = 0; i < phi->operands.size(); i++) {
      if (phi->operands[i].isUndefined())
         continue;
      if (phi->operands[i].regClass() == phi->definitions[0].regClass())
         continue;

      assert(phi->operands[i].isTemp());
      Block *pred = &program->blocks[block->logical_preds[i]];
      Temp phi_src = phi->operands[i].getTemp();

      assert(phi_src.regClass().type() == RegType::sgpr);
      Temp tmp = bld.tmp(RegClass(RegType::vgpr, phi_src.size()));
      insert_before_logical_end(pred, bld.copy(Definition(tmp), phi_src).get_ptr());
      Temp new_phi_src = bld.tmp(phi->definitions[0].regClass());
      insert_before_logical_end(pred, bld.pseudo(aco_opcode::p_extract_vector, Definition(new_phi_src), tmp, Operand(0u)).get_ptr());

      phi->operands[i].setTemp(new_phi_src);
   }
   return;
}

void lower_phis(Program* program)
{
   ssa_state state;

   for (Block& block : program->blocks) {
      state.checked_preds_for_uniform = false;
      state.needs_init = true;
      for (aco_ptr<Instruction>& phi : block.instructions) {
         if (phi->opcode == aco_opcode::p_phi) {
            assert(program->wave_size == 64 ? phi->definitions[0].regClass() != s1 : phi->definitions[0].regClass() != s2);
            if (phi->definitions[0].regClass() == program->lane_mask)
               lower_divergent_bool_phi(program, &state, &block, phi);
            else if (phi->definitions[0].regClass().is_subdword())
               lower_subdword_phis(program, &block, phi);
         } else if (!is_phi(phi)) {
            break;
         }
      }
   }
}

}
