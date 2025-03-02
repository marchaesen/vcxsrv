/*
 * Copyright © 2018 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "aco_ir.h"

#include <vector>

namespace aco {
namespace {

struct phi_info {
   std::vector<std::pair<Definition, Operand>> copies;
   PhysReg scratch_sgpr;
   bool needs_scratch_reg = false;
};

struct ssa_elimination_ctx {
   /* The outer vectors should be indexed by block index.
    * The inner vectors store phi information for each block.
    */
   std::vector<phi_info> phi_infos;
   Program* program;

   ssa_elimination_ctx(Program* program_) : phi_infos(program_->blocks.size()), program(program_) {}
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
            auto& info = ctx.phi_infos[preds[i]];
            info.copies.emplace_back(phi->definitions[0], phi->operands[i]);
            if (phi->pseudo().needs_scratch_reg) {
               info.needs_scratch_reg = true;
               info.scratch_sgpr = phi->pseudo().scratch_sgpr;
            }
         }
      }
   }
}

void
insert_parallelcopies(ssa_elimination_ctx& ctx)
{
   /* insert parallelcopies for the phis at the end of blocks just before the branch */
   for (unsigned block_idx = 0; block_idx < ctx.program->blocks.size(); ++block_idx) {
      auto& phi_info = ctx.phi_infos[block_idx];
      if (phi_info.copies.empty())
         continue;

      Block& block = ctx.program->blocks[block_idx];
      aco_ptr<Instruction> pc{create_instruction(aco_opcode::p_parallelcopy, Format::PSEUDO,
                                                 phi_info.copies.size(), phi_info.copies.size())};
      unsigned i = 0;
      for (auto& pair : phi_info.copies) {
         pc->definitions[i] = pair.first;
         pc->operands[i] = pair.second;
         i++;
      }
      pc->pseudo().scratch_sgpr = phi_info.scratch_sgpr;
      pc->pseudo().needs_scratch_reg = phi_info.needs_scratch_reg;
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
