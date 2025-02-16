/*
 * Copyright © 2024 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "aco_builder.h"
#include "aco_ir.h"

namespace aco {
namespace {

struct branch_ctx {
   Program* program;
   std::vector<bool> blocks_incoming_exec_used;

   branch_ctx(Program* program_)
       : program(program_), blocks_incoming_exec_used(program_->blocks.size(), true)
   {}
};

void
remove_linear_successor(branch_ctx& ctx, Block& block, uint32_t succ_index)
{
   Block& succ = ctx.program->blocks[succ_index];
   ASSERTED auto it = std::remove(succ.linear_preds.begin(), succ.linear_preds.end(), block.index);
   assert(std::next(it) == succ.linear_preds.end());
   succ.linear_preds.pop_back();
   it = std::remove(block.linear_succs.begin(), block.linear_succs.end(), succ_index);
   assert(std::next(it) == block.linear_succs.end());
   block.linear_succs.pop_back();

   if (succ.linear_preds.empty()) {
      /* This block became unreachable - Recursively remove successors. */
      succ.instructions.clear();
      for (unsigned i : succ.linear_succs)
         remove_linear_successor(ctx, succ, i);
   }
}

void
try_remove_simple_block(branch_ctx& ctx, Block& block)
{
   if (!block.instructions.empty() && block.instructions.front()->opcode != aco_opcode::s_branch)
      return;

   /* Don't remove the preheader as it might be needed as convergence point
    * in order to insert code (e.g. for loop alignment, wait states, etc.).
    */
   if (block.kind & block_kind_loop_preheader)
      return;

   unsigned succ_idx = block.linear_succs[0];
   Block& succ = ctx.program->blocks[succ_idx];
   for (unsigned pred_idx : block.linear_preds) {
      Block& pred = ctx.program->blocks[pred_idx];
      assert(pred.index < block.index);
      assert(!pred.instructions.empty() && pred.instructions.back()->isBranch());
      Instruction* branch = pred.instructions.back().get();
      if (branch->opcode == aco_opcode::p_branch) {
         /* The predecessor unconditionally jumps to this block. Redirect to successor. */
         pred.linear_succs[0] = succ_idx;
         succ.linear_preds.push_back(pred_idx);
      } else if (pred.linear_succs[0] == succ_idx || pred.linear_succs[1] == succ_idx) {
         /* The predecessor's alternative target is this block's successor. */
         pred.linear_succs[0] = succ_idx;
         pred.linear_succs[1] = pred.linear_succs.back(); /* In case of discard */
         pred.linear_succs.pop_back();
         branch->opcode = aco_opcode::p_branch;
      } else if (pred.linear_succs[1] == block.index) {
         /* The predecessor jumps to this block. Redirect to successor. */
         pred.linear_succs[1] = succ_idx;
         succ.linear_preds.push_back(pred_idx);
      } else {
         /* This block is the fall-through target of the predecessor. */
         assert(pred_idx == block.index - 1);
         if (block.instructions.empty()) {
            /* If this block is empty, just fall-through to the successor. */
            pred.linear_succs[0] = succ_idx;
            succ.linear_preds.push_back(pred_idx);
            continue;
         }

         /* Otherwise, check if there is a fall-through path for the jump target. */
         if (block.index >= pred.linear_succs[1])
            return;
         for (unsigned j = block.index + 1; j < pred.linear_succs[1]; j++) {
            if (!ctx.program->blocks[j].instructions.empty())
               return;
         }
         pred.linear_succs[0] = pred.linear_succs[1];
         pred.linear_succs[1] = succ_idx;
         succ.linear_preds.push_back(pred_idx);

         /* Invert the condition. This branch now falls through to its original target.
          * However, we don't update the fall-through target since this instruction
          * gets lowered in the next step, anyway.
          */
         if (branch->opcode == aco_opcode::p_cbranch_nz)
            branch->opcode = aco_opcode::p_cbranch_z;
         else
            branch->opcode = aco_opcode::p_cbranch_nz;
      }

      /* Update the branch target. */
      branch->branch().target[0] = succ_idx;
   }

   /* If this block is part of the logical CFG, also connect pre- and successors. */
   if (!block.logical_succs.empty()) {
      assert(block.logical_succs.size() == 1);
      unsigned logical_succ_idx = block.logical_succs[0];
      Block& logical_succ = ctx.program->blocks[logical_succ_idx];
      ASSERTED auto it = std::remove(logical_succ.logical_preds.begin(),
                                     logical_succ.logical_preds.end(), block.index);
      assert(std::next(it) == logical_succ.logical_preds.end());
      logical_succ.logical_preds.pop_back();
      for (unsigned pred_idx : block.logical_preds) {
         Block& pred = ctx.program->blocks[pred_idx];
         std::replace(pred.logical_succs.begin(), pred.logical_succs.end(), block.index,
                      logical_succ_idx);

         if (pred.logical_succs.size() == 2 && pred.logical_succs[0] == pred.logical_succs[1])
            pred.logical_succs.pop_back(); /* This should have been optimized in NIR! */
         else
            logical_succ.logical_preds.push_back(pred_idx);
      }

      block.logical_succs.clear();
      block.logical_preds.clear();
   }

   remove_linear_successor(ctx, block, succ_idx);
   block.linear_preds.clear();
   block.instructions.clear();
}

void
try_merge_break_with_continue(branch_ctx& ctx, Block& block)
{
   /* Look for this:
    * BB1:
    *    ...
    *    p_branch_z exec BB3, BB2
    * BB2:
    *    ...
    *    s[0:1], scc = s_andn2 s[0:1], exec
    *    s_cbranch_scc0 BB4
    * BB3:
    *    exec = s_mov_b64 s[0:1]
    *    s_branch BB1
    * BB4:
    *    ...
    *
    * And turn it into this:
    * BB1:
    *    ...
    *    p_branch_z exec BB3, BB2
    * BB2:
    *    ...
    * BB3:
    *    s[0:1], scc, exec = s_andn2_wrexec s[0:1], exec
    *    s_cbranch_scc1 BB1, BB4
    * BB4:
    *    ...
    */
   if (block.linear_succs.size() != 2 || block.instructions.size() < 2)
      return;

   Instruction* branch = block.instructions.back().get();
   if (branch->opcode != aco_opcode::s_cbranch_scc0)
      return;

   Block& merge = ctx.program->blocks[block.linear_succs[0]];
   Block& loopexit = ctx.program->blocks[block.linear_succs[1]];

   /* Just a jump to the loop header. */
   if (merge.linear_succs.size() != 1)
      return;

   for (unsigned merge_pred : merge.linear_preds) {
      if (merge_pred == block.index)
         continue;

      Block& pred = ctx.program->blocks[merge_pred];
      Instruction* pred_branch = pred.instructions.back().get();
      /* The branch needs to be exec zero only, otherwise we corrupt exec. */
      if (pred_branch->opcode != aco_opcode::p_cbranch_z ||
          pred_branch->operands[0].physReg() != exec)
         return;
   }

   /* merge block: copy to exec, branch */
   if (merge.instructions.size() != 2 || merge.instructions.back()->opcode != aco_opcode::s_branch)
      return;

   Builder bld(ctx.program);
   Instruction* execwrite = merge.instructions[0].get();
   if (execwrite->opcode != bld.w64or32(Builder::s_mov) || !execwrite->writes_exec())
      return;

   Instruction* execsrc = block.instructions[block.instructions.size() - 2].get();
   if (execsrc->opcode != bld.w64or32(Builder::s_andn2) ||
       execsrc->definitions[0].physReg() != execwrite->operands[0].physReg() ||
       execsrc->operands[0].physReg() != execwrite->operands[0].physReg() ||
       execsrc->operands[1].physReg() != exec)
      return;

   /* Use conditional branch in merge block. */
   merge.instructions.back()->opcode = aco_opcode::s_cbranch_scc1;
   block.linear_succs.pop_back();
   block.linear_succs[0] = merge.index;
   merge.linear_succs.push_back(loopexit.index);
   std::swap(merge.linear_succs[0], merge.linear_succs[1]);
   std::replace(loopexit.linear_preds.begin(), loopexit.linear_preds.end(), block.index,
                merge.index);

   /* Check if we can use the loopexit as the fallthrough block.
    * Otherwise, we'll need an extra branch instruction.
    */
   for (unsigned i = merge.index + 1; i < loopexit.index; i++) {
      if (!ctx.program->blocks[i].instructions.empty()) {
         branch->opcode = aco_opcode::s_branch;
         merge.instructions.emplace_back(std::move(block.instructions.back()));
         break;
      }
   }
   block.instructions.pop_back();

   if (ctx.program->gfx_level >= GFX9) {
      /* Combine s_andn2 and copy to exec to s_andn2_wrexec. */
      Instruction* wr_exec =
         bld.sop1(Builder::s_andn2_wrexec, execsrc->definitions[0], execsrc->definitions[1],
                  Definition(exec, bld.lm), execsrc->operands[0], execsrc->operands[1]);
      merge.instructions[0].reset(wr_exec);
   } else {
      /* Move s_andn2 to the merge block. */
      merge.instructions.emplace(merge.instructions.begin(), std::move(block.instructions.back()));
   }
   block.instructions.pop_back();
   ctx.blocks_incoming_exec_used[merge.index] = true;
}

void
eliminate_useless_exec_writes_in_block(branch_ctx& ctx, Block& block)
{
   /* Check if any successor needs the outgoing exec mask from the current block. */
   bool exec_write_used;
   if (block.kind & block_kind_end_with_regs) {
      /* Last block of a program with succeed shader part should respect final exec write. */
      exec_write_used = true;
   } else if (block.linear_succs.empty() && !block.instructions.empty() &&
              block.instructions.back()->opcode == aco_opcode::s_setpc_b64) {
      /* This block ends in a long jump and exec might be needed for the next shader part. */
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

      /* See if the current instruction needs or writes exec. */
      bool needs_exec = needs_exec_mask(instr.get());
      bool writes_exec =
         instr->writes_exec() && instr->definitions[0].regClass() == ctx.program->lane_mask;

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

/**
 *  Check if the branch instruction can be removed:
 *  This is beneficial when executing the next block with an empty exec mask
 *  is faster than the branch instruction itself.
 *
 *  Override this judgement when:
 *  - The application prefers to remove control flow
 *  - The compiler stack knows that it's a divergent branch never taken
 */
bool
can_remove_branch(branch_ctx& ctx, Block& block, Pseudo_branch_instruction* branch)
{
   const uint32_t target = branch->target[0];
   const bool uniform_branch =
      !((branch->opcode == aco_opcode::p_cbranch_z || branch->opcode == aco_opcode::p_cbranch_nz) &&
        branch->operands[0].physReg() == exec);

   if (branch->never_taken) {
      assert(!uniform_branch || std::all_of(std::next(ctx.program->blocks.begin(), block.index + 1),
                                            std::next(ctx.program->blocks.begin(), target),
                                            [](Block& b) { return b.instructions.empty(); }));
      return true;
   }

   /* Cannot remove back-edges. */
   if (block.index >= target)
      return false;

   const bool prefer_remove = branch->rarely_taken;
   unsigned num_scalar = 0;
   unsigned num_vector = 0;

   /* Check the instructions between branch and target */
   for (unsigned i = block.index + 1; i < target; i++) {
      /* Uniform conditional branches must not be ignored if they
       * are about to jump over actual instructions */
      if (uniform_branch && !ctx.program->blocks[i].instructions.empty())
         return false;

      for (aco_ptr<Instruction>& instr : ctx.program->blocks[i].instructions) {
         if (instr->isSOPP()) {
            /* Discard early exits and loop breaks and continues should work fine with
             * an empty exec mask.
             */
            if (instr->opcode == aco_opcode::s_cbranch_scc0 ||
                instr->opcode == aco_opcode::s_cbranch_scc1 ||
                instr->opcode == aco_opcode::s_cbranch_execz ||
                instr->opcode == aco_opcode::s_cbranch_execnz) {
               bool is_break_continue =
                  ctx.program->blocks[i].kind & (block_kind_break | block_kind_continue);
               bool discard_early_exit =
                  ctx.program->blocks[instr->salu().imm].kind & block_kind_discard_early_exit;
               if (is_break_continue || discard_early_exit)
                  continue;
            }
            return false;
         } else if (instr->isSALU()) {
            num_scalar++;
         } else if (instr->isVALU() || instr->isVINTRP()) {
            if (instr->opcode == aco_opcode::v_writelane_b32 ||
                instr->opcode == aco_opcode::v_writelane_b32_e64) {
               /* writelane ignores exec, writing inactive lanes results in UB. */
               return false;
            }
            num_vector++;
            /* VALU which writes SGPRs are always executed on GFX10+ */
            if (ctx.program->gfx_level >= GFX10) {
               for (Definition& def : instr->definitions) {
                  if (def.regClass().type() == RegType::sgpr)
                     num_scalar++;
               }
            }
         } else if (instr->isEXP() || instr->isSMEM() || instr->isBarrier()) {
            /* Export instructions with exec=0 can hang some GFX10+ (unclear on old GPUs),
             * SMEM might be an invalid access, and barriers are probably expensive. */
            return false;
         } else if (instr->isVMEM() || instr->isFlatLike() || instr->isDS() || instr->isLDSDIR()) {
            // TODO: GFX6-9 can use vskip
            if (!prefer_remove)
               return false;
         } else if (instr->opcode != aco_opcode::p_debug_info) {
            assert(false && "Pseudo instructions should be lowered by this point.");
            return false;
         }

         if (!prefer_remove) {
            /* Under these conditions, we shouldn't remove the branch.
             * Don't care about the estimated cycles when the shader prefers flattening.
             */
            unsigned est_cycles;
            if (ctx.program->gfx_level >= GFX10)
               est_cycles = num_scalar * 2 + num_vector;
            else
               est_cycles = num_scalar * 4 + num_vector * 4;

            if (est_cycles > 16)
               return false;
         }
      }
   }

   return true;
}

void
lower_branch_instruction(branch_ctx& ctx, Block& block)
{
   if (block.instructions.empty() || !block.instructions.back()->isBranch())
      return;

   aco_ptr<Instruction> branch = std::move(block.instructions.back());
   const uint32_t target = branch->branch().target[0];
   block.instructions.pop_back();

   if (can_remove_branch(ctx, block, &branch->branch())) {
      if (branch->opcode != aco_opcode::p_branch)
         remove_linear_successor(ctx, block, target);
      return;
   }

   /* emit branch instruction */
   Builder bld(ctx.program, &block.instructions);
   switch (branch->opcode) {
   case aco_opcode::p_branch:
      assert(block.linear_succs[0] == target);
      bld.sopp(aco_opcode::s_branch, target);
      break;
   case aco_opcode::p_cbranch_nz:
      assert(block.linear_succs[1] == target);
      if (branch->operands[0].physReg() == exec)
         bld.sopp(aco_opcode::s_cbranch_execnz, target);
      else if (branch->operands[0].physReg() == vcc)
         bld.sopp(aco_opcode::s_cbranch_vccnz, target);
      else {
         assert(branch->operands[0].physReg() == scc);
         bld.sopp(aco_opcode::s_cbranch_scc1, target);
      }
      break;
   case aco_opcode::p_cbranch_z:
      assert(block.linear_succs[1] == target);
      if (branch->operands[0].physReg() == exec)
         bld.sopp(aco_opcode::s_cbranch_execz, target);
      else if (branch->operands[0].physReg() == vcc)
         bld.sopp(aco_opcode::s_cbranch_vccz, target);
      else {
         assert(branch->operands[0].physReg() == scc);
         bld.sopp(aco_opcode::s_cbranch_scc0, target);
      }
      break;
   default: unreachable("Unknown Pseudo branch instruction!");
   }
}

void
try_stitch_linear_block(branch_ctx& ctx, Block& block)
{
   /* Don't stitch blocks that are part of the logical CFG. */
   if (block.linear_preds.empty() || block.linear_succs.empty() || !block.logical_preds.empty())
      return;

   /* Try to stitch this block with the predecessor:
    * This block must have exactly one predecessor and
    * the predecessor must have exactly one successor.
    */
   Block& pred = ctx.program->blocks[block.linear_preds[0]];
   if (block.linear_preds.size() == 1 && pred.linear_succs.size() == 1 &&
       (pred.instructions.empty() || !pred.instructions.back()->isSOPP())) {
      /* Insert the instructions at the end of the predecessor and fixup edges. */
      pred.instructions.insert(pred.instructions.end(),
                               std::move_iterator(block.instructions.begin()),
                               std::move_iterator(block.instructions.end()));
      for (unsigned succ_idx : block.linear_succs) {
         Block& s = ctx.program->blocks[succ_idx];
         std::replace(s.linear_preds.begin(), s.linear_preds.end(), block.index, pred.index);
      }
      pred.linear_succs = std::move(block.linear_succs);

      block.instructions.clear();
      block.linear_preds.clear();
      block.linear_succs.clear();
      return;
   }

   /* Try to stitch this block with the successor:
    * This block must have exactly one successor and
    * the successor must have exactly one predecessor.
    */
   Block& succ = ctx.program->blocks[block.linear_succs[0]];
   if (block.linear_succs.size() == 1 && succ.linear_preds.size() == 1 &&
       (block.instructions.empty() || !block.instructions.back()->isSOPP())) {
      /* Insert the instructions at the beginning of the successor. */
      succ.instructions.insert(succ.instructions.begin(),
                               std::move_iterator(block.instructions.begin()),
                               std::move_iterator(block.instructions.end()));
      for (unsigned pred_idx : block.linear_preds) {
         Block& p = ctx.program->blocks[pred_idx];
         if (!p.instructions.empty() &&
             instr_info.classes[(int)p.instructions.back()->opcode] == instr_class::branch &&
             p.instructions.back()->salu().imm == block.index) {
            p.instructions.back()->salu().imm = succ.index;
         }
         std::replace(p.linear_succs.begin(), p.linear_succs.end(), block.index, succ.index);
      }
      succ.linear_preds = std::move(block.linear_preds);

      block.instructions.clear();
      block.linear_preds.clear();
      block.linear_succs.clear();
   }
}

} /* end namespace */

void
lower_branches(Program* program)
{
   branch_ctx ctx(program);

   for (int i = program->blocks.size() - 1; i >= 0; i--) {
      Block& block = program->blocks[i];
      lower_branch_instruction(ctx, block);
      eliminate_useless_exec_writes_in_block(ctx, block);

      if (block.kind & block_kind_break)
         try_merge_break_with_continue(ctx, block);

      if (block.linear_succs.size() == 1)
         try_remove_simple_block(ctx, block);
   }

   for (Block& block : program->blocks) {
      try_stitch_linear_block(ctx, block);
   }
}

} // namespace aco
