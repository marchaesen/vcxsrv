/*
 * Copyright © 2018 Valve Corporation
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


#include "aco_ir.h"

#include <map>

namespace aco {
namespace {

/* map: block-id -> pair (dest, src) to store phi information */
typedef std::map<uint32_t, std::vector<std::pair<Definition, Operand>>> phi_info;

struct ssa_elimination_ctx {
   phi_info logical_phi_info;
   phi_info linear_phi_info;
   std::vector<bool> empty_blocks;
   Program* program;

   ssa_elimination_ctx(Program* program_) : empty_blocks(program_->blocks.size(), true), program(program_) {}
};

void collect_phi_info(ssa_elimination_ctx& ctx)
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

            std::vector<unsigned>& preds = phi->opcode == aco_opcode::p_phi ? block.logical_preds : block.linear_preds;
            phi_info& info = phi->opcode == aco_opcode::p_phi ? ctx.logical_phi_info : ctx.linear_phi_info;
            const auto result = info.emplace(preds[i], std::vector<std::pair<Definition, Operand>>());
            assert(phi->definitions[0].size() == phi->operands[i].size());
            result.first->second.emplace_back(phi->definitions[0], phi->operands[i]);
            ctx.empty_blocks[preds[i]] = false;
         }
      }
   }
}

void insert_parallelcopies(ssa_elimination_ctx& ctx)
{
   /* insert the parallelcopies from logical phis before p_logical_end */
   for (auto&& entry : ctx.logical_phi_info) {
      Block& block = ctx.program->blocks[entry.first];
      unsigned idx = block.instructions.size() - 1;
      while (block.instructions[idx]->opcode != aco_opcode::p_logical_end) {
         assert(idx > 0);
         idx--;
      }

      std::vector<aco_ptr<Instruction>>::iterator it = std::next(block.instructions.begin(), idx);
      aco_ptr<Pseudo_instruction> pc{create_instruction<Pseudo_instruction>(aco_opcode::p_parallelcopy, Format::PSEUDO, entry.second.size(), entry.second.size())};
      unsigned i = 0;
      for (std::pair<Definition, Operand>& pair : entry.second)
      {
         pc->definitions[i] = pair.first;
         pc->operands[i] = pair.second;
         i++;
      }
      /* this shouldn't be needed since we're only copying vgprs */
      pc->tmp_in_scc = false;
      block.instructions.insert(it, std::move(pc));
   }

   /* insert parallelcopies for the linear phis at the end of blocks just before the branch */
   for (auto&& entry : ctx.linear_phi_info) {
      Block& block = ctx.program->blocks[entry.first];
      std::vector<aco_ptr<Instruction>>::iterator it = block.instructions.end();
      --it;
      assert((*it)->isBranch());
      aco_ptr<Pseudo_instruction> pc{create_instruction<Pseudo_instruction>(aco_opcode::p_parallelcopy, Format::PSEUDO, entry.second.size(), entry.second.size())};
      unsigned i = 0;
      for (std::pair<Definition, Operand>& pair : entry.second)
      {
         pc->definitions[i] = pair.first;
         pc->operands[i] = pair.second;
         i++;
      }
      pc->tmp_in_scc = block.scc_live_out;
      pc->scratch_sgpr = block.scratch_sgpr;
      block.instructions.insert(it, std::move(pc));
   }
}

bool is_empty_block(Block* block, bool ignore_exec_writes)
{
   /* check if this block is empty and the exec mask is not needed */
   for (aco_ptr<Instruction>& instr : block->instructions) {
      switch (instr->opcode) {
         case aco_opcode::p_linear_phi:
         case aco_opcode::p_phi:
         case aco_opcode::p_logical_start:
         case aco_opcode::p_logical_end:
         case aco_opcode::p_branch:
            break;
         case aco_opcode::p_parallelcopy:
            for (unsigned i = 0; i < instr->definitions.size(); i++) {
               if (ignore_exec_writes && instr->definitions[i].physReg() == exec)
                  continue;
               if (instr->definitions[i].physReg() != instr->operands[i].physReg())
                  return false;
            }
            break;
         case aco_opcode::s_andn2_b64:
         case aco_opcode::s_andn2_b32:
            if (ignore_exec_writes && instr->definitions[0].physReg() == exec)
               break;
            return false;
         default:
            return false;
      }
   }
   return true;
}

void try_remove_merge_block(ssa_elimination_ctx& ctx, Block* block)
{
   /* check if the successor is another merge block which restores exec */
   // TODO: divergent loops also restore exec
   if (block->linear_succs.size() != 1 ||
       !(ctx.program->blocks[block->linear_succs[0]].kind & block_kind_merge))
      return;

   /* check if this block is empty */
   if (!is_empty_block(block, true))
      return;

   /* keep the branch instruction and remove the rest */
   aco_ptr<Instruction> branch = std::move(block->instructions.back());
   block->instructions.clear();
   block->instructions.emplace_back(std::move(branch));
}

void try_remove_invert_block(ssa_elimination_ctx& ctx, Block* block)
{
   assert(block->linear_succs.size() == 2);
   /* only remove this block if the successor got removed as well */
   if (block->linear_succs[0] != block->linear_succs[1])
      return;

   /* check if block is otherwise empty */
   if (!is_empty_block(block, true))
      return;

   unsigned succ_idx = block->linear_succs[0];
   assert(block->linear_preds.size() == 2);
   for (unsigned i = 0; i < 2; i++) {
      Block *pred = &ctx.program->blocks[block->linear_preds[i]];
      pred->linear_succs[0] = succ_idx;
      ctx.program->blocks[succ_idx].linear_preds[i] = pred->index;

      Pseudo_branch_instruction& branch = pred->instructions.back()->branch();
      assert(branch.isBranch());
      branch.target[0] = succ_idx;
      branch.target[1] = succ_idx;
   }

   block->instructions.clear();
   block->linear_preds.clear();
   block->linear_succs.clear();
}

void try_remove_simple_block(ssa_elimination_ctx& ctx, Block* block)
{
   if (!is_empty_block(block, false))
      return;

   Block& pred = ctx.program->blocks[block->linear_preds[0]];
   Block& succ = ctx.program->blocks[block->linear_succs[0]];
   Pseudo_branch_instruction& branch = pred.instructions.back()->branch();
   if (branch.opcode == aco_opcode::p_branch) {
      branch.target[0] = succ.index;
      branch.target[1] = succ.index;
   } else if (branch.target[0] == block->index) {
      branch.target[0] = succ.index;
   } else if (branch.target[0] == succ.index) {
      assert(branch.target[1] == block->index);
      branch.target[1] = succ.index;
      branch.opcode = aco_opcode::p_branch;
   } else if (branch.target[1] == block->index) {
      /* check if there is a fall-through path from block to succ */
      bool falls_through = block->index < succ.index;
      for (unsigned j = block->index + 1; falls_through && j < succ.index; j++) {
         assert(ctx.program->blocks[j].index == j);
         if (!ctx.program->blocks[j].instructions.empty())
            falls_through = false;
      }
      if (falls_through) {
         branch.target[1] = succ.index;
      } else {
         /* check if there is a fall-through path for the alternative target */
         if (block->index >= branch.target[0])
            return;
         for (unsigned j = block->index + 1; j < branch.target[0]; j++) {
            if (!ctx.program->blocks[j].instructions.empty())
               return;
         }

         /* This is a (uniform) break or continue block. The branch condition has to be inverted. */
         if (branch.opcode == aco_opcode::p_cbranch_z)
            branch.opcode = aco_opcode::p_cbranch_nz;
         else if (branch.opcode == aco_opcode::p_cbranch_nz)
            branch.opcode = aco_opcode::p_cbranch_z;
         else
            assert(false);
         /* also invert the linear successors */
         pred.linear_succs[0] = pred.linear_succs[1];
         pred.linear_succs[1] = succ.index;
         branch.target[1] = branch.target[0];
         branch.target[0] = succ.index;
      }
   } else {
      assert(false);
   }

   if (branch.target[0] == branch.target[1])
      branch.opcode = aco_opcode::p_branch;

   for (unsigned i = 0; i < pred.linear_succs.size(); i++)
      if (pred.linear_succs[i] == block->index)
         pred.linear_succs[i] = succ.index;

   for (unsigned i = 0; i < succ.linear_preds.size(); i++)
      if (succ.linear_preds[i] == block->index)
         succ.linear_preds[i] = pred.index;

   block->instructions.clear();
   block->linear_preds.clear();
   block->linear_succs.clear();
}

void jump_threading(ssa_elimination_ctx& ctx)
{
   for (int i = ctx.program->blocks.size() - 1; i >= 0; i--) {
      Block* block = &ctx.program->blocks[i];

      if (!ctx.empty_blocks[i])
         continue;

      if (block->kind & block_kind_invert) {
         try_remove_invert_block(ctx, block);
         continue;
      }

      if (block->linear_succs.size() > 1)
         continue;

      if (block->kind & block_kind_merge ||
          block->kind & block_kind_loop_exit)
         try_remove_merge_block(ctx, block);

      if (block->linear_preds.size() == 1)
         try_remove_simple_block(ctx, block);
   }
}

} /* end namespace */


void ssa_elimination(Program* program)
{
   ssa_elimination_ctx ctx(program);

   /* Collect information about every phi-instruction */
   collect_phi_info(ctx);

   /* eliminate empty blocks */
   jump_threading(ctx);

   /* insert parallelcopies from SSA elimination */
   insert_parallelcopies(ctx);

}
}
