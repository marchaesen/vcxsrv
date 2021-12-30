/*
 * Copyright © 2021 Valve Corporation
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
#include <array>
#include <bitset>
#include <vector>

namespace aco {
namespace {

constexpr const size_t max_reg_cnt = 512;

struct Idx {
   bool operator==(const Idx& other) const { return block == other.block && instr == other.instr; }
   bool operator!=(const Idx& other) const { return !operator==(other); }

   bool found() const { return block != UINT32_MAX; }

   uint32_t block;
   uint32_t instr;
};

Idx not_written_in_block{UINT32_MAX, 0};
Idx clobbered{UINT32_MAX, 1};
Idx const_or_undef{UINT32_MAX, 2};
Idx written_by_multiple_instrs{UINT32_MAX, 3};

struct pr_opt_ctx {
   Program* program;
   Block* current_block;
   uint32_t current_instr_idx;
   std::vector<uint16_t> uses;
   std::vector<std::array<Idx, max_reg_cnt>> instr_idx_by_regs;

   void reset_block(Block* block)
   {
      current_block = block;
      current_instr_idx = 0;

      if ((block->kind & block_kind_loop_header) || block->linear_preds.empty()) {
         std::fill(instr_idx_by_regs[block->index].begin(), instr_idx_by_regs[block->index].end(),
                   not_written_in_block);
      } else {
         unsigned first_pred = block->linear_preds[0];
         for (unsigned i = 0; i < max_reg_cnt; i++) {
            bool all_same = std::all_of(
               std::next(block->linear_preds.begin()), block->linear_preds.end(),
               [&](unsigned pred)
               { return instr_idx_by_regs[pred][i] == instr_idx_by_regs[first_pred][i]; });

            if (all_same)
               instr_idx_by_regs[block->index][i] = instr_idx_by_regs[first_pred][i];
            else
               instr_idx_by_regs[block->index][i] = not_written_in_block;
         }
      }
   }

   Instruction* get(Idx idx) { return program->blocks[idx.block].instructions[idx.instr].get(); }
};

void
save_reg_writes(pr_opt_ctx& ctx, aco_ptr<Instruction>& instr)
{
   for (const Definition& def : instr->definitions) {
      assert(def.regClass().type() != RegType::sgpr || def.physReg().reg() <= 255);
      assert(def.regClass().type() != RegType::vgpr || def.physReg().reg() >= 256);

      unsigned dw_size = DIV_ROUND_UP(def.bytes(), 4u);
      unsigned r = def.physReg().reg();
      Idx idx{ctx.current_block->index, ctx.current_instr_idx};

      if (def.regClass().is_subdword())
         idx = clobbered;

      assert((r + dw_size) <= max_reg_cnt);
      assert(def.size() == dw_size || def.regClass().is_subdword());
      std::fill(ctx.instr_idx_by_regs[ctx.current_block->index].begin() + r,
                ctx.instr_idx_by_regs[ctx.current_block->index].begin() + r + dw_size, idx);
   }
}

Idx
last_writer_idx(pr_opt_ctx& ctx, PhysReg physReg, RegClass rc)
{
   /* Verify that all of the operand's registers are written by the same instruction. */
   assert(physReg.reg() < max_reg_cnt);
   Idx instr_idx = ctx.instr_idx_by_regs[ctx.current_block->index][physReg.reg()];
   unsigned dw_size = DIV_ROUND_UP(rc.bytes(), 4u);
   unsigned r = physReg.reg();
   bool all_same =
      std::all_of(ctx.instr_idx_by_regs[ctx.current_block->index].begin() + r,
                  ctx.instr_idx_by_regs[ctx.current_block->index].begin() + r + dw_size,
                  [instr_idx](Idx i) { return i == instr_idx; });

   return all_same ? instr_idx : written_by_multiple_instrs;
}

Idx
last_writer_idx(pr_opt_ctx& ctx, const Operand& op)
{
   if (op.isConstant() || op.isUndefined())
      return const_or_undef;

   assert(op.physReg().reg() < max_reg_cnt);
   Idx instr_idx = ctx.instr_idx_by_regs[ctx.current_block->index][op.physReg().reg()];

#ifndef NDEBUG
   /* Debug mode:  */
   instr_idx = last_writer_idx(ctx, op.physReg(), op.regClass());
   assert(instr_idx != written_by_multiple_instrs);
#endif

   return instr_idx;
}

bool
is_clobbered_since(pr_opt_ctx& ctx, PhysReg reg, RegClass rc, const Idx& idx)
{
   /* If we didn't find an instruction, assume that the register is clobbered. */
   if (!idx.found())
      return true;

   /* TODO: We currently can't keep track of subdword registers. */
   if (rc.is_subdword())
      return true;

   unsigned begin_reg = reg.reg();
   unsigned end_reg = begin_reg + rc.size();
   unsigned current_block_idx = ctx.current_block->index;

   for (unsigned r = begin_reg; r < end_reg; ++r) {
      Idx& i = ctx.instr_idx_by_regs[current_block_idx][r];
      if (i == clobbered || i == written_by_multiple_instrs)
         return true;
      else if (i == not_written_in_block)
         continue;

      assert(i.found());

      if (i.block > idx.block || (i.block == idx.block && i.instr > idx.instr))
         return true;
   }

   return false;
}

template <typename T>
bool
is_clobbered_since(pr_opt_ctx& ctx, const T& t, const Idx& idx)
{
   return is_clobbered_since(ctx, t.physReg(), t.regClass(), idx);
}

void
try_apply_branch_vcc(pr_opt_ctx& ctx, aco_ptr<Instruction>& instr)
{
   /* We are looking for the following pattern:
    *
    * vcc = ...                      ; last_vcc_wr
    * sX, scc = s_and_bXX vcc, exec  ; op0_instr
    * (...vcc and exec must not be clobbered inbetween...)
    * s_cbranch_XX scc               ; instr
    *
    * If possible, the above is optimized into:
    *
    * vcc = ...                      ; last_vcc_wr
    * s_cbranch_XX vcc               ; instr modified to use vcc
    */

   /* Don't try to optimize this on GFX6-7 because SMEM may corrupt the vccz bit. */
   if (ctx.program->chip_class < GFX8)
      return;

   if (instr->format != Format::PSEUDO_BRANCH || instr->operands.size() == 0 ||
       instr->operands[0].physReg() != scc)
      return;

   Idx op0_instr_idx = last_writer_idx(ctx, instr->operands[0]);
   Idx last_vcc_wr_idx = last_writer_idx(ctx, vcc, ctx.program->lane_mask);

   /* We need to make sure:
    * - the instructions that wrote the operand register and VCC are both found
    * - the operand register used by the branch, and VCC were both written in the current block
    * - EXEC hasn't been clobbered since the last VCC write
    * - VCC hasn't been clobbered since the operand register was written
    *   (ie. the last VCC writer precedes the op0 writer)
    */
   if (!op0_instr_idx.found() || !last_vcc_wr_idx.found() ||
       op0_instr_idx.block != ctx.current_block->index ||
       last_vcc_wr_idx.block != ctx.current_block->index ||
       is_clobbered_since(ctx, exec, ctx.program->lane_mask, last_vcc_wr_idx) ||
       is_clobbered_since(ctx, vcc, ctx.program->lane_mask, op0_instr_idx))
      return;

   Instruction* op0_instr = ctx.get(op0_instr_idx);
   Instruction* last_vcc_wr = ctx.get(last_vcc_wr_idx);

   if ((op0_instr->opcode != aco_opcode::s_and_b64 /* wave64 */ &&
        op0_instr->opcode != aco_opcode::s_and_b32 /* wave32 */) ||
       op0_instr->operands[0].physReg() != vcc || op0_instr->operands[1].physReg() != exec ||
       !last_vcc_wr->isVOPC())
      return;

   assert(last_vcc_wr->definitions[0].tempId() == op0_instr->operands[0].tempId());

   /* Reduce the uses of the SCC def */
   ctx.uses[instr->operands[0].tempId()]--;
   /* Use VCC instead of SCC in the branch */
   instr->operands[0] = op0_instr->operands[0];
}

void
try_optimize_scc_nocompare(pr_opt_ctx& ctx, aco_ptr<Instruction>& instr)
{
   /* We are looking for the following pattern:
    *
    * s_bfe_u32 s0, s3, 0x40018  ; outputs SGPR and SCC if the SGPR != 0
    * s_cmp_eq_i32 s0, 0         ; comparison between the SGPR and 0
    * s_cbranch_scc0 BB3         ; use the result of the comparison, eg. branch or cselect
    *
    * If possible, the above is optimized into:
    *
    * s_bfe_u32 s0, s3, 0x40018  ; original instruction
    * s_cbranch_scc1 BB3         ; modified to use SCC directly rather than the SGPR with comparison
    *
    */

   if (!instr->isSALU() && !instr->isBranch())
      return;

   if (instr->isSOPC() &&
       (instr->opcode == aco_opcode::s_cmp_eq_u32 || instr->opcode == aco_opcode::s_cmp_eq_i32 ||
        instr->opcode == aco_opcode::s_cmp_lg_u32 || instr->opcode == aco_opcode::s_cmp_lg_i32 ||
        instr->opcode == aco_opcode::s_cmp_eq_u64 || instr->opcode == aco_opcode::s_cmp_lg_u64) &&
       (instr->operands[0].constantEquals(0) || instr->operands[1].constantEquals(0)) &&
       (instr->operands[0].isTemp() || instr->operands[1].isTemp())) {
      /* Make sure the constant is always in operand 1 */
      if (instr->operands[0].isConstant())
         std::swap(instr->operands[0], instr->operands[1]);

      if (ctx.uses[instr->operands[0].tempId()] > 1)
         return;

      /* Make sure both SCC and Operand 0 are written by the same instruction. */
      Idx wr_idx = last_writer_idx(ctx, instr->operands[0]);
      Idx sccwr_idx = last_writer_idx(ctx, scc, s1);
      if (!wr_idx.found() || wr_idx != sccwr_idx)
         return;

      Instruction* wr_instr = ctx.get(wr_idx);
      if (!wr_instr->isSALU() || wr_instr->definitions.size() < 2 ||
          wr_instr->definitions[1].physReg() != scc)
         return;

      /* Look for instructions which set SCC := (D != 0) */
      switch (wr_instr->opcode) {
      case aco_opcode::s_bfe_i32:
      case aco_opcode::s_bfe_i64:
      case aco_opcode::s_bfe_u32:
      case aco_opcode::s_bfe_u64:
      case aco_opcode::s_and_b32:
      case aco_opcode::s_and_b64:
      case aco_opcode::s_andn2_b32:
      case aco_opcode::s_andn2_b64:
      case aco_opcode::s_or_b32:
      case aco_opcode::s_or_b64:
      case aco_opcode::s_orn2_b32:
      case aco_opcode::s_orn2_b64:
      case aco_opcode::s_xor_b32:
      case aco_opcode::s_xor_b64:
      case aco_opcode::s_not_b32:
      case aco_opcode::s_not_b64:
      case aco_opcode::s_nor_b32:
      case aco_opcode::s_nor_b64:
      case aco_opcode::s_xnor_b32:
      case aco_opcode::s_xnor_b64:
      case aco_opcode::s_nand_b32:
      case aco_opcode::s_nand_b64:
      case aco_opcode::s_lshl_b32:
      case aco_opcode::s_lshl_b64:
      case aco_opcode::s_lshr_b32:
      case aco_opcode::s_lshr_b64:
      case aco_opcode::s_ashr_i32:
      case aco_opcode::s_ashr_i64:
      case aco_opcode::s_abs_i32:
      case aco_opcode::s_absdiff_i32: break;
      default: return;
      }

      /* Use the SCC def from wr_instr */
      ctx.uses[instr->operands[0].tempId()]--;
      instr->operands[0] = Operand(wr_instr->definitions[1].getTemp(), scc);
      ctx.uses[instr->operands[0].tempId()]++;

      /* Set the opcode and operand to 32-bit */
      instr->operands[1] = Operand::zero();
      instr->opcode =
         (instr->opcode == aco_opcode::s_cmp_eq_u32 || instr->opcode == aco_opcode::s_cmp_eq_i32 ||
          instr->opcode == aco_opcode::s_cmp_eq_u64)
            ? aco_opcode::s_cmp_eq_u32
            : aco_opcode::s_cmp_lg_u32;
   } else if ((instr->format == Format::PSEUDO_BRANCH && instr->operands.size() == 1 &&
               instr->operands[0].physReg() == scc) ||
              instr->opcode == aco_opcode::s_cselect_b32) {

      /* For cselect, operand 2 is the SCC condition */
      unsigned scc_op_idx = 0;
      if (instr->opcode == aco_opcode::s_cselect_b32) {
         scc_op_idx = 2;
      }

      Idx wr_idx = last_writer_idx(ctx, instr->operands[scc_op_idx]);
      if (!wr_idx.found())
         return;

      Instruction* wr_instr = ctx.get(wr_idx);

      /* Check if we found the pattern above. */
      if (wr_instr->opcode != aco_opcode::s_cmp_eq_u32 &&
          wr_instr->opcode != aco_opcode::s_cmp_lg_u32)
         return;
      if (wr_instr->operands[0].physReg() != scc)
         return;
      if (!wr_instr->operands[1].constantEquals(0))
         return;

      /* The optimization can be unsafe when there are other users. */
      if (ctx.uses[instr->operands[scc_op_idx].tempId()] > 1)
         return;

      if (wr_instr->opcode == aco_opcode::s_cmp_eq_u32) {
         /* Flip the meaning of the instruction to correctly use the SCC. */
         if (instr->format == Format::PSEUDO_BRANCH)
            instr->opcode = instr->opcode == aco_opcode::p_cbranch_z ? aco_opcode::p_cbranch_nz
                                                                     : aco_opcode::p_cbranch_z;
         else if (instr->opcode == aco_opcode::s_cselect_b32)
            std::swap(instr->operands[0], instr->operands[1]);
         else
            unreachable(
               "scc_nocompare optimization is only implemented for p_cbranch and s_cselect");
      }

      /* Use the SCC def from the original instruction, not the comparison */
      ctx.uses[instr->operands[scc_op_idx].tempId()]--;
      instr->operands[scc_op_idx] = wr_instr->operands[0];
   }
}

void
try_combine_dpp(pr_opt_ctx& ctx, aco_ptr<Instruction>& instr)
{
   /* We are looking for the following pattern:
    *
    * v_mov_dpp vA, vB, ...      ; move instruction with DPP
    * v_xxx vC, vA, ...          ; current instr that uses the result from the move
    *
    * If possible, the above is optimized into:
    *
    * v_xxx_dpp vC, vB, ...      ; current instr modified to use DPP directly
    *
    */

   if (!instr->isVALU() || instr->isDPP() || !can_use_DPP(instr, false))
      return;

   for (unsigned i = 0; i < MIN2(2, instr->operands.size()); i++) {
      Idx op_instr_idx = last_writer_idx(ctx, instr->operands[i]);
      if (!op_instr_idx.found())
         continue;

      Instruction* mov = ctx.get(op_instr_idx);
      if (mov->opcode != aco_opcode::v_mov_b32 || !mov->isDPP())
         continue;

      /* If we aren't going to remove the v_mov_b32, we have to ensure that it doesn't overwrite
       * it's own operand before we use it.
       */
      if (mov->definitions[0].physReg() == mov->operands[0].physReg() &&
          (!mov->definitions[0].tempId() || ctx.uses[mov->definitions[0].tempId()] > 1))
         continue;

      /* Don't propagate DPP if the source register is overwritten since the move. */
      if (is_clobbered_since(ctx, mov->operands[0], op_instr_idx))
         continue;

      if (i && !can_swap_operands(instr, &instr->opcode))
         continue;

      /* anything else doesn't make sense in SSA */
      assert(mov->dpp().row_mask == 0xf && mov->dpp().bank_mask == 0xf);

      if (--ctx.uses[mov->definitions[0].tempId()])
         ctx.uses[mov->operands[0].tempId()]++;

      convert_to_DPP(instr);

      DPP_instruction* dpp = &instr->dpp();
      if (i) {
         std::swap(dpp->operands[0], dpp->operands[1]);
         std::swap(dpp->neg[0], dpp->neg[1]);
         std::swap(dpp->abs[0], dpp->abs[1]);
      }
      dpp->operands[0] = mov->operands[0];
      dpp->dpp_ctrl = mov->dpp().dpp_ctrl;
      dpp->bound_ctrl = true;
      dpp->neg[0] ^= mov->dpp().neg[0] && !dpp->abs[0];
      dpp->abs[0] |= mov->dpp().abs[0];
      return;
   }
}

void
process_instruction(pr_opt_ctx& ctx, aco_ptr<Instruction>& instr)
{
   try_apply_branch_vcc(ctx, instr);

   try_optimize_scc_nocompare(ctx, instr);

   try_combine_dpp(ctx, instr);

   if (instr)
      save_reg_writes(ctx, instr);

   ctx.current_instr_idx++;
}

} // namespace

void
optimize_postRA(Program* program)
{
   pr_opt_ctx ctx;
   ctx.program = program;
   ctx.uses = dead_code_analysis(program);
   ctx.instr_idx_by_regs.resize(program->blocks.size());

   /* Forward pass
    * Goes through each instruction exactly once, and can transform
    * instructions or adjust the use counts of temps.
    */
   for (auto& block : program->blocks) {
      ctx.reset_block(&block);

      for (aco_ptr<Instruction>& instr : block.instructions)
         process_instruction(ctx, instr);
   }

   /* Cleanup pass
    * Gets rid of instructions which are manually deleted or
    * no longer have any uses.
    */
   for (auto& block : program->blocks) {
      auto new_end = std::remove_if(block.instructions.begin(), block.instructions.end(),
                                    [&ctx](const aco_ptr<Instruction>& instr)
                                    { return !instr || is_dead(ctx.uses, instr.get()); });
      block.instructions.resize(new_end - block.instructions.begin());
   }
}

} // namespace aco
