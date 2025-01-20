/*
 * Copyright © 2018 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "aco_ir.h"

#include <vector>

namespace aco {
namespace {

struct phi_info_item {
   Definition def;
   Operand op;
};

struct ssa_elimination_ctx {
   /* The outer vectors should be indexed by block index. The inner vectors store phi information
    * for each block. */
   std::vector<std::vector<phi_info_item>> logical_phi_info;
   std::vector<std::vector<phi_info_item>> linear_phi_info;
   Program* program;

   ssa_elimination_ctx(Program* program_)
       : logical_phi_info(program_->blocks.size()), linear_phi_info(program_->blocks.size()),
         program(program_)
   {}
};

void
collect_phi_info(ssa_elimination_ctx& ctx)
{
   for (Block& block : ctx.program->blocks) {
      for (aco_ptr<Instruction>& phi : block.instructions) {
         if (phi->opcode != aco_opcode::p_phi && phi->opcode != aco_opcode::p_linear_phi)
            break;

         for (unsigned i = 0; i < phi->operands.size(); i++) {
            if (phi->operands[i].isUndefined())
               continue;
            if (phi->operands[i].physReg() == phi->definitions[0].physReg())
               continue;

            assert(phi->definitions[0].size() == phi->operands[i].size());

            Block::edge_vec& preds =
               phi->opcode == aco_opcode::p_phi ? block.logical_preds : block.linear_preds;
            uint32_t pred_idx = preds[i];
            auto& info_vec = phi->opcode == aco_opcode::p_phi ? ctx.logical_phi_info[pred_idx]
                                                              : ctx.linear_phi_info[pred_idx];
            info_vec.push_back({phi->definitions[0], phi->operands[i]});
         }
      }
   }
}

void
insert_parallelcopies(ssa_elimination_ctx& ctx)
{
   /* insert the parallelcopies from logical phis before p_logical_end */
   for (unsigned block_idx = 0; block_idx < ctx.program->blocks.size(); ++block_idx) {
      auto& logical_phi_info = ctx.logical_phi_info[block_idx];
      if (logical_phi_info.empty())
         continue;

      Block& block = ctx.program->blocks[block_idx];
      unsigned idx = block.instructions.size() - 1;
      while (block.instructions[idx]->opcode != aco_opcode::p_logical_end) {
         assert(idx > 0);
         idx--;
      }

      std::vector<aco_ptr<Instruction>>::iterator it = std::next(block.instructions.begin(), idx);
      aco_ptr<Instruction> pc{create_instruction(aco_opcode::p_parallelcopy, Format::PSEUDO,
                                                 logical_phi_info.size(), logical_phi_info.size())};
      unsigned i = 0;
      for (auto& phi_info : logical_phi_info) {
         pc->definitions[i] = phi_info.def;
         pc->operands[i] = phi_info.op;
         i++;
      }
      pc->pseudo().needs_scratch_reg = false;
      block.instructions.insert(it, std::move(pc));
   }

   /* insert parallelcopies for the linear phis at the end of blocks just before the branch */
   for (unsigned block_idx = 0; block_idx < ctx.program->blocks.size(); ++block_idx) {
      auto& linear_phi_info = ctx.linear_phi_info[block_idx];
      if (linear_phi_info.empty())
         continue;

      Block& block = ctx.program->blocks[block_idx];
      Block& succ = ctx.program->blocks[block.linear_succs[0]];
      aco_ptr<Instruction> pc{create_instruction(aco_opcode::p_parallelcopy, Format::PSEUDO,
                                                 linear_phi_info.size(), linear_phi_info.size())};
      unsigned i = 0;
      for (auto& phi_info : linear_phi_info) {
         pc->definitions[i] = phi_info.def;
         pc->operands[i] = phi_info.op;
         i++;
      }
      pc->pseudo().scratch_sgpr = succ.instructions[0]->pseudo().scratch_sgpr;
      pc->pseudo().needs_scratch_reg = succ.instructions[0]->pseudo().needs_scratch_reg;
      auto it = std::prev(block.instructions.end());
      block.instructions.insert(it, std::move(pc));
   }
}

} /* end namespace */

void
ssa_elimination(Program* program)
{
   ssa_elimination_ctx ctx(program);

   /* Collect information about every phi-instruction */
   collect_phi_info(ctx);

   /* insert parallelcopies from SSA elimination */
   insert_parallelcopies(ctx);
}
} // namespace aco
