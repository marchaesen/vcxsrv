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
 */

#include "aco_builder.h"
#include "aco_ir.h"

#include <algorithm>
#include <map>
#include <vector>

namespace aco {

enum class pred_defined : uint8_t {
   undef = 0,
   const_1 = 1,
   const_0 = 2,
   temp = 3,
   zero = 4, /* all disabled lanes are zero'd out */
};
MESA_DEFINE_CPP_ENUM_BITFIELD_OPERATORS(pred_defined);

struct ssa_state {
   bool checked_preds_for_uniform;
   bool all_preds_uniform;
   unsigned loop_nest_depth;

   std::vector<pred_defined> any_pred_defined;
   std::vector<bool> visited;
   std::vector<Operand> outputs; /* the output per block */
};

Operand
get_ssa(Program* program, unsigned block_idx, ssa_state* state, bool input)
{
   if (!input) {
      if (state->visited[block_idx])
         return state->outputs[block_idx];

      /* otherwise, output == input */
      Operand output = get_ssa(program, block_idx, state, true);
      state->visited[block_idx] = true;
      state->outputs[block_idx] = output;
      return output;
   }

   /* retrieve the Operand by checking the predecessors */
   if (state->any_pred_defined[block_idx] == pred_defined::undef)
      return Operand(program->lane_mask);

   Block& block = program->blocks[block_idx];
   size_t pred = block.linear_preds.size();
   Operand op;
   if (block.loop_nest_depth < state->loop_nest_depth) {
      /* loop-carried value for loop exit phis */
      op = Operand::zero(program->lane_mask.bytes());
   } else if (block.loop_nest_depth > state->loop_nest_depth || pred == 1 ||
              block.kind & block_kind_loop_exit) {
      op = get_ssa(program, block.linear_preds[0], state, false);
   } else {
      assert(pred > 1);
      bool previously_visited = state->visited[block_idx];
      /* potential recursion: anchor at loop header */
      if (block.kind & block_kind_loop_header) {
         assert(!previously_visited);
         previously_visited = true;
         state->visited[block_idx] = true;
         state->outputs[block_idx] = Operand(Temp(program->allocateTmp(program->lane_mask)));
      }

      /* collect predecessor output operands */
      std::vector<Operand> ops(pred);
      for (unsigned i = 0; i < pred; i++)
         ops[i] = get_ssa(program, block.linear_preds[i], state, false);

      /* check triviality */
      if (std::all_of(ops.begin() + 1, ops.end(), [&](Operand same) { return same == ops[0]; }))
         return ops[0];

      /* Return if this was handled in a recursive call by a loop header phi */
      if (!previously_visited && state->visited[block_idx])
         return state->outputs[block_idx];

      if (block.kind & block_kind_loop_header)
         op = state->outputs[block_idx];
      else
         op = Operand(Temp(program->allocateTmp(program->lane_mask)));

      /* create phi */
      aco_ptr<Pseudo_instruction> phi{
         create_instruction<Pseudo_instruction>(aco_opcode::p_linear_phi, Format::PSEUDO, pred, 1)};
      for (unsigned i = 0; i < pred; i++)
         phi->operands[i] = ops[i];
      phi->definitions[0] = Definition(op.getTemp());
      block.instructions.emplace(block.instructions.begin(), std::move(phi));
   }

   assert(op.size() == program->lane_mask.size());
   return op;
}

void
insert_before_logical_end(Block* block, aco_ptr<Instruction> instr)
{
   auto IsLogicalEnd = [](const aco_ptr<Instruction>& inst) -> bool
   { return inst->opcode == aco_opcode::p_logical_end; };
   auto it = std::find_if(block->instructions.crbegin(), block->instructions.crend(), IsLogicalEnd);

   if (it == block->instructions.crend()) {
      assert(block->instructions.back()->isBranch());
      block->instructions.insert(std::prev(block->instructions.end()), std::move(instr));
   } else {
      block->instructions.insert(std::prev(it.base()), std::move(instr));
   }
}

void
build_merge_code(Program* program, ssa_state* state, Block* block, Operand cur)
{
   unsigned block_idx = block->index;
   Definition dst = Definition(state->outputs[block_idx].getTemp());
   Operand prev = get_ssa(program, block_idx, state, true);
   if (cur.isUndefined())
      cur = Operand::zero(program->lane_mask.bytes());

   Builder bld(program);
   auto IsLogicalEnd = [](const aco_ptr<Instruction>& instr) -> bool
   { return instr->opcode == aco_opcode::p_logical_end; };
   auto it = std::find_if(block->instructions.rbegin(), block->instructions.rend(), IsLogicalEnd);
   assert(it != block->instructions.rend());
   bld.reset(&block->instructions, std::prev(it.base()));

   pred_defined defined = state->any_pred_defined[block_idx];
   if (defined == pred_defined::undef) {
      return;
   } else if (defined == pred_defined::const_0) {
      bld.sop2(Builder::s_and, dst, bld.def(s1, scc), cur, Operand(exec, bld.lm));
      return;
   } else if (defined == pred_defined::const_1) {
      bld.sop2(Builder::s_orn2, dst, bld.def(s1, scc), cur, Operand(exec, bld.lm));
      return;
   }

   assert(prev.isTemp());
   /* simpler sequence in case prev has only zeros in disabled lanes */
   if ((defined & pred_defined::zero) == pred_defined::zero) {
      if (cur.isConstant()) {
         if (!cur.constantValue()) {
            bld.copy(dst, prev);
            return;
         }
         cur = Operand(exec, bld.lm);
      } else {
         cur =
            bld.sop2(Builder::s_and, bld.def(bld.lm), bld.def(s1, scc), cur, Operand(exec, bld.lm));
      }
      bld.sop2(Builder::s_or, dst, bld.def(s1, scc), prev, cur);
      return;
   }

   if (cur.isConstant()) {
      if (cur.constantValue())
         bld.sop2(Builder::s_or, dst, bld.def(s1, scc), prev, Operand(exec, bld.lm));
      else
         bld.sop2(Builder::s_andn2, dst, bld.def(s1, scc), prev, Operand(exec, bld.lm));
      return;
   }
   prev =
      bld.sop2(Builder::s_andn2, bld.def(bld.lm), bld.def(s1, scc), prev, Operand(exec, bld.lm));
   cur = bld.sop2(Builder::s_and, bld.def(bld.lm), bld.def(s1, scc), cur, Operand(exec, bld.lm));
   bld.sop2(Builder::s_or, dst, bld.def(s1, scc), prev, cur);
   return;
}

void
init_any_pred_defined(Program* program, ssa_state* state, Block* block, aco_ptr<Instruction>& phi)
{
   std::fill(state->any_pred_defined.begin(), state->any_pred_defined.end(), pred_defined::undef);
   for (unsigned i = 0; i < block->logical_preds.size(); i++) {
      if (phi->operands[i].isUndefined())
         continue;
      pred_defined defined = pred_defined::temp;
      if (phi->operands[i].isConstant())
         defined = phi->operands[i].constantValue() ? pred_defined::const_1 : pred_defined::const_0;
      for (unsigned succ : program->blocks[block->logical_preds[i]].linear_succs)
         state->any_pred_defined[succ] |= defined;
   }

   unsigned start = block->logical_preds[0];
   unsigned end = block->index;

   /* for loop exit phis, start at the loop header */
   if (block->kind & block_kind_loop_exit) {
      while (program->blocks[start - 1].loop_nest_depth >= state->loop_nest_depth)
         start--;
      /* If the loop-header has a back-edge, we need to insert a phi.
       * This will contain a defined value */
      if (program->blocks[start].linear_preds.size() > 1)
         state->any_pred_defined[start] = pred_defined::temp;
   }
   /* for loop header phis, end at the loop exit */
   if (block->kind & block_kind_loop_header) {
      while (program->blocks[end].loop_nest_depth >= state->loop_nest_depth)
         end++;
      /* don't propagate the incoming value */
      state->any_pred_defined[block->index] = pred_defined::undef;
   }

   /* add dominating zero: this allows to emit simpler merge sequences
    * if we can ensure that all disabled lanes are always zero on incoming values */
   // TODO: find more occasions where pred_defined::zero is beneficial (e.g. with 2+ temp merges)
   if (block->kind & block_kind_loop_exit) {
      /* zero the loop-carried variable */
      if (program->blocks[start].linear_preds.size() > 1) {
         state->any_pred_defined[start] |= pred_defined::zero;
         // TODO: emit this zero explicitly
         state->any_pred_defined[start - 1] = pred_defined::const_0;
      }
   }

   for (unsigned j = start; j < end; j++) {
      if (state->any_pred_defined[j] == pred_defined::undef)
         continue;
      for (unsigned succ : program->blocks[j].linear_succs)
         state->any_pred_defined[succ] |= state->any_pred_defined[j];
   }

   state->any_pred_defined[block->index] = pred_defined::undef;
}

void
lower_divergent_bool_phi(Program* program, ssa_state* state, Block* block,
                         aco_ptr<Instruction>& phi)
{
   Builder bld(program);

   if (!state->checked_preds_for_uniform) {
      state->all_preds_uniform = !(block->kind & block_kind_merge) &&
                                 block->linear_preds.size() == block->logical_preds.size();
      for (unsigned pred : block->logical_preds)
         state->all_preds_uniform =
            state->all_preds_uniform && (program->blocks[pred].kind & block_kind_uniform);
      state->checked_preds_for_uniform = true;
   }

   if (state->all_preds_uniform) {
      phi->opcode = aco_opcode::p_linear_phi;
      return;
   }

   /* do this here to avoid resizing in case of no boolean phis */
   state->visited.resize(program->blocks.size());
   state->outputs.resize(program->blocks.size());
   state->any_pred_defined.resize(program->blocks.size());
   state->loop_nest_depth = block->loop_nest_depth;
   if (block->kind & block_kind_loop_exit)
      state->loop_nest_depth += 1;
   std::fill(state->visited.begin(), state->visited.end(), false);
   init_any_pred_defined(program, state, block, phi);

   for (unsigned i = 0; i < phi->operands.size(); i++) {
      unsigned pred = block->logical_preds[i];
      if (state->any_pred_defined[pred] != pred_defined::undef)
         state->outputs[pred] = Operand(bld.tmp(bld.lm));
      else
         state->outputs[pred] = phi->operands[i];
      assert(state->outputs[pred].size() == bld.lm.size());
      state->visited[pred] = true;
   }

   for (unsigned i = 0; i < phi->operands.size(); i++)
      build_merge_code(program, state, &program->blocks[block->logical_preds[i]], phi->operands[i]);

   unsigned num_preds = block->linear_preds.size();
   if (phi->operands.size() != num_preds) {
      Pseudo_instruction* new_phi{create_instruction<Pseudo_instruction>(
         aco_opcode::p_linear_phi, Format::PSEUDO, num_preds, 1)};
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

void
lower_subdword_phis(Program* program, Block* block, aco_ptr<Instruction>& phi)
{
   Builder bld(program);
   for (unsigned i = 0; i < phi->operands.size(); i++) {
      if (phi->operands[i].isUndefined())
         continue;
      if (phi->operands[i].regClass() == phi->definitions[0].regClass())
         continue;

      assert(phi->operands[i].isTemp());
      Block* pred = &program->blocks[block->logical_preds[i]];
      Temp phi_src = phi->operands[i].getTemp();

      assert(phi_src.regClass().type() == RegType::sgpr);
      Temp tmp = bld.tmp(RegClass(RegType::vgpr, phi_src.size()));
      insert_before_logical_end(pred, bld.copy(Definition(tmp), phi_src).get_ptr());
      Temp new_phi_src = bld.tmp(phi->definitions[0].regClass());
      insert_before_logical_end(pred, bld.pseudo(aco_opcode::p_extract_vector,
                                                 Definition(new_phi_src), tmp, Operand::zero())
                                         .get_ptr());

      phi->operands[i].setTemp(new_phi_src);
   }
   return;
}

void
lower_phis(Program* program)
{
   ssa_state state;

   for (Block& block : program->blocks) {
      state.checked_preds_for_uniform = false;
      for (aco_ptr<Instruction>& phi : block.instructions) {
         if (phi->opcode == aco_opcode::p_phi) {
            assert(program->wave_size == 64 ? phi->definitions[0].regClass() != s1
                                            : phi->definitions[0].regClass() != s2);
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

} // namespace aco
