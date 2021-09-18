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

#include <algorithm>
#include <map>
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
   std::vector<bool> empty_blocks;
   std::vector<bool> blocks_incoming_exec_used;
   Program* program;

   ssa_elimination_ctx(Program* program_)
       : logical_phi_info(program_->blocks.size()), linear_phi_info(program_->blocks.size()),
         empty_blocks(program_->blocks.size(), true),
         blocks_incoming_exec_used(program_->blocks.size(), true), program(program_)
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

            std::vector<unsigned>& preds =
               phi->opcode == aco_opcode::p_phi ? block.logical_preds : block.linear_preds;
            uint32_t pred_idx = preds[i];
            auto& info_vec = phi->opcode == aco_opcode::p_phi ? ctx.logical_phi_info[pred_idx]
                                                              : ctx.linear_phi_info[pred_idx];
            info_vec.push_back({phi->definitions[0], phi->operands[i]});
            ctx.empty_blocks[pred_idx] = false;
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
      aco_ptr<Pseudo_instruction> pc{
         create_instruction<Pseudo_instruction>(aco_opcode::p_parallelcopy, Format::PSEUDO,
                                                logical_phi_info.size(), logical_phi_info.size())};
      unsigned i = 0;
      for (auto& phi_info : logical_phi_info) {
         pc->definitions[i] = phi_info.def;
         pc->operands[i] = phi_info.op;
         i++;
      }
      /* this shouldn't be needed since we're only copying vgprs */
      pc->tmp_in_scc = false;
      block.instructions.insert(it, std::move(pc));
   }

   /* insert parallelcopies for the linear phis at the end of blocks just before the branch */
   for (unsigned block_idx = 0; block_idx < ctx.program->blocks.size(); ++block_idx) {
      auto& linear_phi_info = ctx.linear_phi_info[block_idx];
      if (linear_phi_info.empty())
         continue;

      Block& block = ctx.program->blocks[block_idx];
      std::vector<aco_ptr<Instruction>>::iterator it = block.instructions.end();
      --it;
      assert((*it)->isBranch());
      aco_ptr<Pseudo_instruction> pc{
         create_instruction<Pseudo_instruction>(aco_opcode::p_parallelcopy, Format::PSEUDO,
                                                linear_phi_info.size(), linear_phi_info.size())};
      unsigned i = 0;
      for (auto& phi_info : linear_phi_info) {
         pc->definitions[i] = phi_info.def;
         pc->operands[i] = phi_info.op;
         i++;
      }
      pc->tmp_in_scc = block.scc_live_out;
      pc->scratch_sgpr = block.scratch_sgpr;
      block.instructions.insert(it, std::move(pc));
   }
}

bool
is_empty_block(Block* block, bool ignore_exec_writes)
{
   /* check if this block is empty and the exec mask is not needed */
   for (aco_ptr<Instruction>& instr : block->instructions) {
      switch (instr->opcode) {
      case aco_opcode::p_linear_phi:
      case aco_opcode::p_phi:
      case aco_opcode::p_logical_start:
      case aco_opcode::p_logical_end:
      case aco_opcode::p_branch: break;
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
      default: return false;
      }
   }
   return true;
}

void
try_remove_merge_block(ssa_elimination_ctx& ctx, Block* block)
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

void
try_remove_invert_block(ssa_elimination_ctx& ctx, Block* block)
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
      Block* pred = &ctx.program->blocks[block->linear_preds[i]];
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

void
try_remove_simple_block(ssa_elimination_ctx& ctx, Block* block)
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

bool
instr_writes_exec(Instruction* instr)
{
   for (Definition& def : instr->definitions)
      if (def.physReg() == exec || def.physReg() == exec_hi)
         return true;

   return false;
}

void
eliminate_useless_exec_writes_in_block(ssa_elimination_ctx& ctx, Block& block)
{
   /* Check if any successor needs the outgoing exec mask from the current block. */

   bool exec_write_used;

   if (!ctx.logical_phi_info[block.index].empty()) {
      exec_write_used = true;
   } else {
      bool copy_to_exec = false;
      bool copy_from_exec = false;

      for (const auto& successor_phi_info : ctx.linear_phi_info[block.index]) {
         copy_to_exec |= successor_phi_info.def.physReg() == exec;
         copy_from_exec |= successor_phi_info.op.physReg() == exec;
      }

      if (copy_from_exec)
         exec_write_used = true;
      else if (copy_to_exec)
         exec_write_used = false;
      else
         /* blocks_incoming_exec_used is initialized to true, so this is correct even for loops. */
         exec_write_used =
            std::any_of(block.linear_succs.begin(), block.linear_succs.end(),
                        [&ctx](int succ_idx) { return ctx.blocks_incoming_exec_used[succ_idx]; });
   }

   /* Go through all instructions and eliminate useless exec writes. */

   for (int i = block.instructions.size() - 1; i >= 0; --i) {
      aco_ptr<Instruction>& instr = block.instructions[i];

      /* We already take information from phis into account before the loop, so let's just break on
       * phis. */
      if (instr->opcode == aco_opcode::p_linear_phi || instr->opcode == aco_opcode::p_phi)
         break;

      /* See if the current instruction needs or writes exec. */
      bool needs_exec = needs_exec_mask(instr.get());
      bool writes_exec = instr_writes_exec(instr.get());

      /* See if we found an unused exec write. */
      if (writes_exec && !exec_write_used) {
         instr.reset();
         continue;
      }

      /* For a newly encountered exec write, clear the used flag. */
      if (writes_exec)
         exec_write_used = false;

      /* If the current instruction needs exec, mark it as used. */
      exec_write_used |= needs_exec;
   }

   /* Remember if the current block needs an incoming exec mask from its predecessors. */
   ctx.blocks_incoming_exec_used[block.index] = exec_write_used;

   /* Cleanup: remove deleted instructions from the vector. */
   auto new_end = std::remove(block.instructions.begin(), block.instructions.end(), nullptr);
   block.instructions.resize(new_end - block.instructions.begin());
}

void
jump_threading(ssa_elimination_ctx& ctx)
{
   for (int i = ctx.program->blocks.size() - 1; i >= 0; i--) {
      Block* block = &ctx.program->blocks[i];
      eliminate_useless_exec_writes_in_block(ctx, *block);

      if (!ctx.empty_blocks[i])
         continue;

      if (block->kind & block_kind_invert) {
         try_remove_invert_block(ctx, block);
         continue;
      }

      if (block->linear_succs.size() > 1)
         continue;

      if (block->kind & block_kind_merge || block->kind & block_kind_loop_exit)
         try_remove_merge_block(ctx, block);

      if (block->linear_preds.size() == 1)
         try_remove_simple_block(ctx, block);
   }
}

} /* end namespace */

void
ssa_elimination(Program* program)
{
   ssa_elimination_ctx ctx(program);

   /* Collect information about every phi-instruction */
   collect_phi_info(ctx);

   /* eliminate empty blocks */
   jump_threading(ctx);

   /* insert parallelcopies from SSA elimination */
   insert_parallelcopies(ctx);
}
} // namespace aco
