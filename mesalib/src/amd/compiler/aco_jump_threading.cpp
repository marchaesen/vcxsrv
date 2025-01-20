/*
 * Copyright © 2024 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "aco_ir.h"

#include <algorithm>
#include <vector>

namespace aco {
namespace {

struct jump_threading_ctx {
   std::vector<bool> blocks_incoming_exec_used;
   Program* program;

   jump_threading_ctx(Program* program_)
       : blocks_incoming_exec_used(program_->blocks.size(), true), program(program_)
   {}
};

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
try_remove_merge_block(jump_threading_ctx& ctx, Block* block)
{
   if (block->linear_succs.size() != 1)
      return;

   unsigned succ_idx = block->linear_succs[0];

   /* Check if this block is empty, if the successor is an early block,
    * we didn't gather incoming_exec_used for it yet.
    */
   if (!is_empty_block(block, !ctx.blocks_incoming_exec_used[succ_idx] && block->index < succ_idx))
      return;

   /* keep the branch instruction and remove the rest */
   aco_ptr<Instruction> branch = std::move(block->instructions.back());
   block->instructions.clear();
   block->instructions.emplace_back(std::move(branch));
}

void
try_remove_invert_block(jump_threading_ctx& ctx, Block* block)
{
   assert(block->linear_succs.size() == 2);
   /* only remove this block if the successor got removed as well */
   if (block->linear_succs[0] != block->linear_succs[1])
      return;

   unsigned succ_idx = block->linear_succs[0];
   assert(block->index < succ_idx);

   /* check if block is otherwise empty */
   if (!is_empty_block(block, !ctx.blocks_incoming_exec_used[succ_idx]))
      return;

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
try_remove_simple_block(jump_threading_ctx& ctx, Block* block)
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
      branch.rarely_taken = branch.never_taken = false;
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

   if (branch.target[0] == branch.target[1]) {
      while (branch.operands.size())
         branch.operands.pop_back();

      branch.opcode = aco_opcode::p_branch;
      branch.rarely_taken = branch.never_taken = false;
   }

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
is_simple_copy(Instruction* instr)
{
   return instr->opcode == aco_opcode::p_parallelcopy && instr->definitions.size() == 1;
}

void
try_merge_break_with_continue(jump_threading_ctx& ctx, Block* block)
{
   /* Look for this:
    * BB1:
    *    ...
    *    p_branch_z exec BB3, BB2
    * BB2:
    *    ...
    *    s[0:1], scc = s_andn2 s[0:1], exec
    *    p_branch_z scc BB4, BB3
    * BB3:
    *    exec = p_parallelcopy s[0:1]
    *    p_branch BB1
    * BB4:
    *    ...
    *
    * And turn it into this:
    * BB1:
    *    ...
    *    p_branch_z exec BB3, BB2
    * BB2:
    *    ...
    *    p_branch BB3
    * BB3:
    *    s[0:1], scc, exec = s_andn2_wrexec s[0:1], exec
    *    p_branch_nz scc BB1, BB4
    * BB4:
    *    ...
    */
   if (block->linear_succs.size() != 2 || block->instructions.size() < 2)
      return;

   Pseudo_branch_instruction* branch = &block->instructions.back()->branch();
   if (branch->operands[0].physReg() != scc || branch->opcode != aco_opcode::p_cbranch_z)
      return;

   Block* merge = &ctx.program->blocks[branch->target[1]];
   Block* loopexit = &ctx.program->blocks[branch->target[0]];

   /* Just a jump to the loop header. */
   if (merge->linear_succs.size() != 1)
      return;

   /* We want to use the loopexit as the fallthrough block from merge,
    * so there shouldn't be a block inbetween.
    */
   for (unsigned i = merge->index + 1; i < loopexit->index; i++) {
      if (!ctx.program->blocks[i].instructions.empty())
         return;
   }

   for (unsigned merge_pred : merge->linear_preds) {
      Block* pred = &ctx.program->blocks[merge_pred];
      if (pred == block)
         continue;

      Instruction* pred_branch = pred->instructions.back().get();
      /* The branch needs to be exec zero only, otherwise we corrupt exec. */
      if (!pred_branch->isBranch() || pred_branch->opcode != aco_opcode::p_cbranch_z ||
          pred_branch->operands[0].physReg() != exec)
         return;
   }

   /* merge block: copy to exec, logical_start, logical_end, branch */
   if (merge->instructions.size() != 4 || !is_empty_block(merge, true))
      return;

   aco_ptr<Instruction>& execwrite = merge->instructions[0];
   if (!is_simple_copy(execwrite.get()) || execwrite->definitions[0].physReg() != exec)
      return;

   const aco_opcode andn2 =
      ctx.program->lane_mask == s2 ? aco_opcode::s_andn2_b64 : aco_opcode::s_andn2_b32;
   const aco_opcode andn2_wrexec = ctx.program->lane_mask == s2 ? aco_opcode::s_andn2_wrexec_b64
                                                                : aco_opcode::s_andn2_wrexec_b32;

   auto execsrc_it = block->instructions.end() - 2;
   if ((*execsrc_it)->opcode != andn2 ||
       (*execsrc_it)->definitions[0].physReg() != execwrite->operands[0].physReg() ||
       (*execsrc_it)->operands[0].physReg() != execwrite->operands[0].physReg() ||
       (*execsrc_it)->operands[1].physReg() != exec)
      return;

   /* Move s_andn2 to the merge block. */
   merge->instructions.insert(merge->instructions.begin(), std::move(*execsrc_it));
   block->instructions.erase(execsrc_it);

   branch->target[0] = merge->linear_succs[0];
   branch->target[1] = loopexit->index;
   branch->opcode = aco_opcode::p_cbranch_nz;

   merge->instructions.back()->branch().target[0] = merge->index;
   std::swap(merge->instructions.back(), block->instructions.back());

   block->linear_succs.clear();
   block->linear_succs.push_back(merge->index);
   merge->linear_succs.push_back(loopexit->index);
   std::swap(merge->linear_succs[0], merge->linear_succs[1]);
   ctx.blocks_incoming_exec_used[merge->index] = true;

   std::replace(loopexit->linear_preds.begin(), loopexit->linear_preds.end(), block->index,
                merge->index);

   if (ctx.program->gfx_level < GFX9)
      return;

   /* Combine s_andn2 and copy to exec to s_andn2_wrexec. */
   Instruction* r_exec = merge->instructions[0].get();
   Instruction* wr_exec = create_instruction(andn2_wrexec, Format::SOP1, 2, 3);
   wr_exec->operands[0] = r_exec->operands[0];
   wr_exec->operands[1] = r_exec->operands[1];
   wr_exec->definitions[0] = r_exec->definitions[0];
   wr_exec->definitions[1] = r_exec->definitions[1];
   wr_exec->definitions[2] = Definition(exec, ctx.program->lane_mask);

   merge->instructions.erase(merge->instructions.begin());
   merge->instructions[0].reset(wr_exec);
}

void
eliminate_useless_exec_writes_in_block(jump_threading_ctx& ctx, Block& block)
{
   /* Check if any successor needs the outgoing exec mask from the current block. */

   bool exec_write_used;
   if (block.kind & block_kind_end_with_regs) {
      /* Last block of a program with succeed shader part should respect final exec write. */
      exec_write_used = true;
   } else {
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
      bool writes_exec = instr->writes_exec();

      /* See if we found an unused exec write. */
      if (writes_exec && !exec_write_used) {
         /* Don't eliminate an instruction that writes registers other than exec and scc.
          * It is possible that this is eg. an s_and_saveexec and the saved value is
          * used by a later branch.
          */
         bool writes_other = std::any_of(instr->definitions.begin(), instr->definitions.end(),
                                         [](const Definition& def) -> bool
                                         { return def.physReg() != exec && def.physReg() != scc; });
         if (!writes_other) {
            instr.reset();
            continue;
         }
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

} /* end namespace */

void
jump_threading(Program* program)
{
   jump_threading_ctx ctx(program);

   for (int i = program->blocks.size() - 1; i >= 0; i--) {
      Block* block = &program->blocks[i];
      eliminate_useless_exec_writes_in_block(ctx, *block);

      if (block->kind & block_kind_break)
         try_merge_break_with_continue(ctx, block);

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
} // namespace aco
