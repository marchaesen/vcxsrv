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

   branch_ctx(Program* program_) : program(program_) {}
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
      assert(!uniform_branch);
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

} /* end namespace */

void
lower_branches(Program* program)
{
   branch_ctx ctx(program);

   for (int i = program->blocks.size() - 1; i >= 0; i--) {
      Block& block = program->blocks[i];
      lower_branch_instruction(ctx, block);
   }
}

} // namespace aco
