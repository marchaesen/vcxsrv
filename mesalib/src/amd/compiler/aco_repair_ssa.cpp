/*
 * Copyright © 2024 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "aco_ir.h"

#include <functional>

namespace aco {

namespace {

/**
 * This is a limited SSA repair pass which inserts the phis necessary for the definition of a
 * linear temporary to dominate it's uses in the linear CFG. The definition must still dominate it's
 * uses in the logical CFG. If a path in which the temporary is defined is not taken, the value used
 * is undefined.
 *
 * aco::lower_phis() must be run after to lower the logical phis created by this pass.
 */

struct repair_state {
   Program* program;
   Block* block;

   std::vector<bool> block_needs_repair; /* uses in this block might need repair */
   std::vector<bool> dom_needs_repair;   /* whether a block dominates a block which needs repair */

   std::vector<bool> has_def_block;
   std::unique_ptr<uint32_t[]> def_blocks;
   std::unordered_map<uint64_t, Temp> renames;

   std::vector<aco_ptr<Instruction>> new_phis;

   std::vector<bool> visit_block;
   std::vector<Temp> temps;

   bool is_temp_defined_at_pred(uint32_t block_idx, uint32_t def_block) const
   {
      return block_idx >= def_block && visit_block[block_idx] && temps[block_idx].id();
   }
};

Temp
create_phis(repair_state* state, Temp tmp, uint32_t use_block, uint32_t def_block)
{
   Program* program = state->program;

   assert(program->blocks[def_block].logical_idom != -1);
   assert(program->blocks[use_block].logical_idom != -1);
   assert(use_block > def_block);

   std::fill(state->visit_block.begin() + def_block, state->visit_block.begin() + use_block + 1,
             false);

   for (int32_t i = use_block; i >= (int)def_block; i--) {
      bool block_needs_tmp = (uint32_t)i == use_block;
      for (uint32_t succ : program->blocks[i].logical_succs)
         block_needs_tmp |= succ > (uint32_t)i && state->visit_block[succ];
      state->visit_block[i] = block_needs_tmp;

      if (block_needs_tmp && (uint32_t)i != def_block) {
         uint64_t k = i | ((uint64_t)tmp.id() << 32);
         auto it = state->renames.find(k);
         if (it != state->renames.end())
            state->temps[i] = it->second;
         else
            state->temps[i] = Temp(0, tmp.regClass());
      }
   }

   state->temps[def_block] = tmp;
   for (uint32_t i = def_block + 1; i <= use_block; i++) {
      if (!state->visit_block[i] || state->temps[i].id())
         continue;

      Block& block = program->blocks[i];

      bool undef = true;
      for (unsigned pred : block.logical_preds)
         undef &= pred < i && !state->is_temp_defined_at_pred(pred, def_block);
      if (undef) {
         state->temps[i] = Temp(0, tmp.regClass());
         continue;
      }

      /* If a logical dominator has a temporary, we don't need to create a phi and can just use
       * that temporary instead. For linear temporaries, we also need to check if it dominates in
       * the linear CFG, because logical dominators do not necessarily dominate a block in the
       * linear CFG (for example, because of continue_or_break or empty exec skips). */
      uint32_t dom = block.index;
      bool skip_phis = false;
      do {
         dom = program->blocks[dom].logical_idom;
         if (state->is_temp_defined_at_pred(dom, def_block) &&
             dominates_linear(program->blocks[dom], block)) {
            skip_phis = true;
            break;
         }
      } while (dom != def_block);
      if (skip_phis) {
         state->temps[i] = state->temps[dom];
         continue;
      }

      /* This pass doesn't support creating loop header phis */
      assert(!(block.kind & block_kind_loop_header));

      Temp def = program->allocateTmp(tmp.regClass());
      aco_ptr<Instruction> phi{
         create_instruction(aco_opcode::p_phi, Format::PSEUDO, block.logical_preds.size(), 1)};
      for (unsigned j = 0; j < block.logical_preds.size(); j++) {
         uint32_t pred = block.logical_preds[j];
         assert(state->is_temp_defined_at_pred(pred, def_block));
         phi->operands[j] = Operand(state->temps[pred]);
      }
      phi->definitions[0] = Definition(def);

      if (&block == state->block)
         state->new_phis.emplace_back(std::move(phi));
      else
         block.instructions.emplace(block.instructions.begin(), std::move(phi));

      uint64_t k = i | ((uint64_t)tmp.id() << 32);
      state->renames.emplace(k, def);
      state->temps[i] = def;
   }

   return state->temps[use_block];
}

template <bool LoopHeader>
bool
repair_block(repair_state* state, Block& block)
{
   bool needs_repair = state->block_needs_repair[block.index];
   bool dom_needs_repair = state->dom_needs_repair[block.index];
   bool progress = false;

   state->block = &block;
   for (aco_ptr<Instruction>& instr : block.instructions) {
      if (dom_needs_repair) {
         for (Definition def : instr->definitions) {
            if (def.isTemp()) {
               state->def_blocks[def.tempId()] = block.index;
               state->has_def_block[def.tempId()] = true;
            }
         }
      }

      bool phi = is_phi(instr) || instr->opcode == aco_opcode::p_boolean_phi;

      /* Skip the section below if we don't need to repair. If we don't need to update def_blocks
       * either, then we can just break. We always need to process phis because their actual uses
       * are in the predecessors, which might need repair. */
      if (!phi && !needs_repair) {
         if (!dom_needs_repair)
            break;
         continue;
      }

      unsigned start = 0;
      unsigned num_operands = instr->operands.size();
      if (phi && (block.kind & block_kind_loop_header)) {
         if (LoopHeader)
            start++;
         else
            num_operands = 1;
      } else if (LoopHeader) {
         break;
      }

      for (unsigned i = start; i < num_operands; i++) {
         Operand& op = instr->operands[i];
         if (!op.isTemp() || !op.getTemp().is_linear() || !state->has_def_block[op.tempId()])
            continue;

         uint32_t def_block = state->def_blocks[op.tempId()];
         uint32_t use_block = block.index;
         if (instr->opcode == aco_opcode::p_boolean_phi || instr->opcode == aco_opcode::p_phi) {
            use_block = block.logical_preds[i];
            if (!state->block_needs_repair[use_block])
               continue;
         } else if (instr->opcode == aco_opcode::p_linear_phi) {
            use_block = block.linear_preds[i];
            if (!state->block_needs_repair[use_block])
               continue;
         }

         if (!dominates_linear(state->program->blocks[def_block],
                               state->program->blocks[use_block])) {
            assert(dominates_logical(state->program->blocks[def_block],
                                     state->program->blocks[use_block]));
            op.setTemp(create_phis(state, op.getTemp(), use_block, def_block));
            progress = true;
         }
      }
   }

   /* These are inserted later to not invalidate any iterators. */
   block.instructions.insert(block.instructions.begin(),
                             std::move_iterator(state->new_phis.begin()),
                             std::move_iterator(state->new_phis.end()));
   state->new_phis.clear();

   return progress;
}

} /* end namespace */

bool
repair_ssa(Program* program)
{
   repair_state state;
   state.program = program;

   state.block_needs_repair.resize(program->blocks.size());
   state.dom_needs_repair.resize(program->blocks.size());

   state.has_def_block.resize(program->peekAllocationId());
   state.def_blocks.reset(new uint32_t[program->peekAllocationId()]);

   state.visit_block.resize(program->blocks.size());
   state.temps.resize(program->blocks.size());

   for (Block& block : program->blocks) {
      if (block.logical_idom == -1)
         continue;

      if (state.block_needs_repair[block.logical_idom] ||
          !dominates_linear(program->blocks[block.logical_idom], block)) {
         state.block_needs_repair[block.index] = true;

         /* Set dom_needs_repair=true for logical dominators. */
         uint32_t parent = block.logical_idom;
         while (!state.dom_needs_repair[parent]) {
            state.dom_needs_repair[parent] = true;
            parent = program->blocks[parent].logical_idom;
         }
      }
   }

   std::vector<unsigned> loop_header_indices;

   bool progress = false;
   for (Block& block : program->blocks) {
      if (block.kind & block_kind_loop_header)
         loop_header_indices.push_back(block.index);

      progress |= repair_block<false>(&state, block);

      if (block.kind & block_kind_loop_exit) {
         unsigned header = loop_header_indices.back();
         loop_header_indices.pop_back();

         progress |= repair_block<true>(&state, program->blocks[header]);
      }
   }

   return progress;
}

} // namespace aco
