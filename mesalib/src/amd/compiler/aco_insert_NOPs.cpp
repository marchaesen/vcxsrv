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

#include <algorithm>

#include "aco_ir.h"
#include <stack>

namespace aco {
namespace {

struct NOP_ctx_gfx8_9 {
   enum chip_class chip_class;
   unsigned vcc_physical;

   /* just initialize these with something less than max NOPs */
   int VALU_wrexec = -10;
   int VALU_wrvcc = -10;
   int VALU_wrsgpr = -10;

   NOP_ctx_gfx8_9(Program* program) : chip_class(program->chip_class) {
      vcc_physical = program->config->num_sgprs - 2;
   }
};

struct NOP_ctx_gfx10 {
   bool has_VOPC = false;
   bool has_nonVALU_exec_read = false;
   bool has_VMEM = false;
   bool has_branch_after_VMEM = false;
   bool has_DS = false;
   bool has_branch_after_DS = false;
   std::bitset<128> sgprs_read_by_VMEM;
   std::bitset<128> sgprs_read_by_SMEM;

   void join(const NOP_ctx_gfx10 &other) {
      has_VOPC |= other.has_VOPC;
      has_nonVALU_exec_read |= other.has_nonVALU_exec_read;
      has_VMEM |= other.has_VMEM;
      has_branch_after_VMEM |= other.has_branch_after_VMEM;
      has_DS |= other.has_DS;
      has_branch_after_DS |= other.has_branch_after_DS;
      sgprs_read_by_VMEM |= other.sgprs_read_by_VMEM;
      sgprs_read_by_SMEM |= other.sgprs_read_by_SMEM;
   }

   bool operator==(const NOP_ctx_gfx10 &other)
   {
      return
         has_VOPC == other.has_VOPC &&
         has_nonVALU_exec_read == other.has_nonVALU_exec_read &&
         has_VMEM == other.has_VMEM &&
         has_branch_after_VMEM == other.has_branch_after_VMEM &&
         has_DS == other.has_DS &&
         has_branch_after_DS == other.has_branch_after_DS &&
         sgprs_read_by_VMEM == other.sgprs_read_by_VMEM &&
         sgprs_read_by_SMEM == other.sgprs_read_by_SMEM;
   }
};

template <std::size_t N>
bool check_written_regs(const aco_ptr<Instruction> &instr, const std::bitset<N> &check_regs)
{
   return std::any_of(instr->definitions.begin(), instr->definitions.end(), [&check_regs](const Definition &def) -> bool {
      bool writes_any = false;
      for (unsigned i = 0; i < def.size(); i++) {
         unsigned def_reg = def.physReg() + i;
         writes_any |= def_reg < check_regs.size() && check_regs[def_reg];
      }
      return writes_any;
   });
}

template <std::size_t N>
void mark_read_regs(const aco_ptr<Instruction> &instr, std::bitset<N> &reg_reads)
{
   for (const Operand &op : instr->operands) {
      for (unsigned i = 0; i < op.size(); i++) {
         unsigned reg = op.physReg() + i;
         if (reg < reg_reads.size())
            reg_reads.set(reg);
      }
   }
}

bool VALU_writes_sgpr(aco_ptr<Instruction>& instr)
{
   if ((uint32_t) instr->format & (uint32_t) Format::VOPC)
      return true;
   if (instr->isVOP3() && instr->definitions.size() == 2)
      return true;
   if (instr->opcode == aco_opcode::v_readfirstlane_b32 ||
       instr->opcode == aco_opcode::v_readlane_b32 ||
       instr->opcode == aco_opcode::v_readlane_b32_e64)
      return true;
   return false;
}

bool instr_writes_exec(const aco_ptr<Instruction>& instr)
{
   return std::any_of(instr->definitions.begin(), instr->definitions.end(), [](const Definition &def) -> bool {
      return def.physReg() == exec_lo || def.physReg() == exec_hi;
   });
}

bool instr_writes_sgpr(const aco_ptr<Instruction>& instr)
{
   return std::any_of(instr->definitions.begin(), instr->definitions.end(), [](const Definition &def) -> bool {
      return def.getTemp().type() == RegType::sgpr;
   });
}

inline bool instr_is_branch(const aco_ptr<Instruction>& instr)
{
   return instr->opcode == aco_opcode::s_branch ||
          instr->opcode == aco_opcode::s_cbranch_scc0 ||
          instr->opcode == aco_opcode::s_cbranch_scc1 ||
          instr->opcode == aco_opcode::s_cbranch_vccz ||
          instr->opcode == aco_opcode::s_cbranch_vccnz ||
          instr->opcode == aco_opcode::s_cbranch_execz ||
          instr->opcode == aco_opcode::s_cbranch_execnz ||
          instr->opcode == aco_opcode::s_cbranch_cdbgsys ||
          instr->opcode == aco_opcode::s_cbranch_cdbguser ||
          instr->opcode == aco_opcode::s_cbranch_cdbgsys_or_user ||
          instr->opcode == aco_opcode::s_cbranch_cdbgsys_and_user ||
          instr->opcode == aco_opcode::s_subvector_loop_begin ||
          instr->opcode == aco_opcode::s_subvector_loop_end ||
          instr->opcode == aco_opcode::s_setpc_b64 ||
          instr->opcode == aco_opcode::s_swappc_b64 ||
          instr->opcode == aco_opcode::s_getpc_b64 ||
          instr->opcode == aco_opcode::s_call_b64;
}

bool regs_intersect(PhysReg a_reg, unsigned a_size, PhysReg b_reg, unsigned b_size)
{
   return a_reg > b_reg ?
          (a_reg - b_reg < b_size) :
          (b_reg - a_reg < a_size);
}

unsigned handle_SMEM_clause(aco_ptr<Instruction>& instr, int new_idx,
                            std::vector<aco_ptr<Instruction>>& new_instructions)
{
   //TODO: s_dcache_inv needs to be in it's own group on GFX10 (and previous versions?)
   const bool is_store = instr->definitions.empty();
   for (int pred_idx = new_idx - 1; pred_idx >= 0; pred_idx--) {
      aco_ptr<Instruction>& pred = new_instructions[pred_idx];
      if (pred->format != Format::SMEM)
         break;

      /* Don't allow clauses with store instructions since the clause's
       * instructions may use the same address. */
      if (is_store || pred->definitions.empty())
         return 1;

      Definition& instr_def = instr->definitions[0];
      Definition& pred_def = pred->definitions[0];

      /* ISA reference doesn't say anything about this, but best to be safe */
      if (regs_intersect(instr_def.physReg(), instr_def.size(), pred_def.physReg(), pred_def.size()))
         return 1;

      for (const Operand& op : pred->operands) {
         if (op.isConstant() || !op.isFixed())
            continue;
         if (regs_intersect(instr_def.physReg(), instr_def.size(), op.physReg(), op.size()))
            return 1;
      }
      for (const Operand& op : instr->operands) {
         if (op.isConstant() || !op.isFixed())
            continue;
         if (regs_intersect(pred_def.physReg(), pred_def.size(), op.physReg(), op.size()))
            return 1;
      }
   }

   return 0;
}

int handle_instruction_gfx8_9(NOP_ctx_gfx8_9& ctx, aco_ptr<Instruction>& instr,
                              std::vector<aco_ptr<Instruction>>& old_instructions,
                              std::vector<aco_ptr<Instruction>>& new_instructions)
{
   int new_idx = new_instructions.size();

   // TODO: setreg / getreg / m0 writes
   // TODO: try to schedule the NOP-causing instruction up to reduce the number of stall cycles


   if (instr->format == Format::SMEM) {
      if (ctx.chip_class == GFX6) {
         bool is_buffer_load = instr->operands.size() && instr->operands[0].size() > 2;
         for (int pred_idx = new_idx - 1; pred_idx >= 0 && pred_idx >= new_idx - 4; pred_idx--) {
            aco_ptr<Instruction>& pred = new_instructions[pred_idx];
            /* A read of an SGPR by SMRD instruction requires 4 wait states
             * when the SGPR was written by a VALU instruction. */
            if (VALU_writes_sgpr(pred)) {
               Definition pred_def = pred->definitions[pred->definitions.size() - 1];
               for (const Operand& op : instr->operands) {
                  if (regs_intersect(pred_def.physReg(), pred_def.size(), op.physReg(), op.size()))
                     return 4 + pred_idx - new_idx + 1;
               }
            }
            /* According to LLVM, this is an undocumented hardware behavior */
            if (is_buffer_load && pred->isSALU() && pred->definitions.size()) {
               Definition pred_def = pred->definitions[0];
               Operand& op = instr->operands[0];
               if (regs_intersect(pred_def.physReg(), pred_def.size(), op.physReg(), op.size()))
                  return 4 + pred_idx - new_idx + 1;
            }
         }
      }

      /* break off from prevous SMEM clause if needed */
      return handle_SMEM_clause(instr, new_idx, new_instructions);

   } else if (instr->isVALU() || instr->format == Format::VINTRP) {
      int NOPs = 0;

      if (instr->isDPP()) {
         /* VALU does not forward EXEC to DPP. */
         if (ctx.VALU_wrexec + 5 >= new_idx)
            NOPs = 5 + ctx.VALU_wrexec - new_idx + 1;

         /* VALU DPP reads VGPR written by VALU */
         for (int pred_idx = new_idx - 1; pred_idx >= 0 && pred_idx >= new_idx - 2; pred_idx--) {
            aco_ptr<Instruction>& pred = new_instructions[pred_idx];
            if ((pred->isVALU() || pred->format == Format::VINTRP) &&
                !pred->definitions.empty() &&
                pred->definitions[0].physReg() == instr->operands[0].physReg()) {
               NOPs = std::max(NOPs, 2 + pred_idx - new_idx + 1);
               break;
            }
         }
      }

      /* SALU writes M0 */
      if (instr->format == Format::VINTRP && new_idx > 0 && ctx.chip_class >= GFX9) {
         aco_ptr<Instruction>& pred = new_instructions.back();
         if (pred->isSALU() &&
             !pred->definitions.empty() &&
             pred->definitions[0].physReg() == m0)
            NOPs = std::max(NOPs, 1);
      }

      for (const Operand& op : instr->operands) {
         /* VALU which uses VCCZ */
         if (op.physReg() == PhysReg{251} &&
             ctx.VALU_wrvcc + 5 >= new_idx)
            NOPs = std::max(NOPs, 5 + ctx.VALU_wrvcc - new_idx + 1);

         /* VALU which uses EXECZ */
         if (op.physReg() == PhysReg{252} &&
             ctx.VALU_wrexec + 5 >= new_idx)
            NOPs = std::max(NOPs, 5 + ctx.VALU_wrexec - new_idx + 1);

         /* VALU which reads VCC as a constant */
         if (ctx.VALU_wrvcc + 1 >= new_idx) {
            for (unsigned k = 0; k < op.size(); k++) {
               unsigned reg = op.physReg() + k;
               if (reg == ctx.vcc_physical || reg == ctx.vcc_physical + 1)
                  NOPs = std::max(NOPs, 1);
            }
         }
      }

      switch (instr->opcode) {
         case aco_opcode::v_readlane_b32:
         case aco_opcode::v_readlane_b32_e64:
         case aco_opcode::v_writelane_b32:
         case aco_opcode::v_writelane_b32_e64: {
            if (ctx.VALU_wrsgpr + 4 < new_idx)
               break;
            PhysReg reg = instr->operands[1].physReg();
            for (int pred_idx = new_idx - 1; pred_idx >= 0 && pred_idx >= new_idx - 4; pred_idx--) {
               aco_ptr<Instruction>& pred = new_instructions[pred_idx];
               if (!pred->isVALU() || !VALU_writes_sgpr(pred))
                  continue;
               for (const Definition& def : pred->definitions) {
                  if (def.physReg() == reg)
                     NOPs = std::max(NOPs, 4 + pred_idx - new_idx + 1);
               }
            }
            break;
         }
         case aco_opcode::v_div_fmas_f32:
         case aco_opcode::v_div_fmas_f64: {
            if (ctx.VALU_wrvcc + 4 >= new_idx)
               NOPs = std::max(NOPs, 4 + ctx.VALU_wrvcc - new_idx + 1);
            break;
         }
         default:
            break;
      }

      /* Write VGPRs holding writedata > 64 bit from MIMG/MUBUF instructions */
      // FIXME: handle case if the last instruction of a block without branch is such store
      if (new_idx > 0) {
         aco_ptr<Instruction>& pred = new_instructions.back();
         /* >64-bit MUBUF/MTBUF store with a constant in SOFFSET */
         bool consider_buf = (pred->format == Format::MUBUF || pred->format == Format::MTBUF) &&
                             pred->operands.size() == 4 &&
                             pred->operands[3].size() > 2 &&
                             pred->operands[2].physReg() >= 128;
         /* MIMG store with a 128-bit T# with more than two bits set in dmask (making it a >64-bit store) */
         bool consider_mimg = pred->format == Format::MIMG &&
                              pred->operands.size() == 4 &&
                              pred->operands[3].size() > 2 &&
                              pred->operands[1].size() != 8;
         /* FLAT/GLOBAL/SCRATCH store with >64-bit data */
         bool consider_flat = (pred->isFlatOrGlobal() || pred->format == Format::SCRATCH) &&
                              pred->operands.size() == 3 &&
                              pred->operands[2].size() > 2;
         if (consider_buf || consider_mimg || consider_flat) {
            PhysReg wrdata = pred->operands[3].physReg();
            unsigned size = pred->operands[3].size();
            assert(wrdata >= 256);
            for (const Definition& def : instr->definitions) {
               if (regs_intersect(def.physReg(), def.size(), wrdata, size))
                  NOPs = std::max(NOPs, 1);
            }
         }
      }

      if (VALU_writes_sgpr(instr)) {
         for (const Definition& def : instr->definitions) {
            if (def.physReg() == vcc)
               ctx.VALU_wrvcc = NOPs ? new_idx : new_idx + 1;
            else if (def.physReg() == exec)
               ctx.VALU_wrexec = NOPs ? new_idx : new_idx + 1;
            else if (def.physReg() <= 102)
               ctx.VALU_wrsgpr = NOPs ? new_idx : new_idx + 1;
         }
      }
      return NOPs;
   } else if (instr->isVMEM() && ctx.VALU_wrsgpr + 5 >= new_idx) {
      /* If the VALU writes the SGPR that is used by a VMEM, the user must add five wait states. */
      for (int pred_idx = new_idx - 1; pred_idx >= 0 && pred_idx >= new_idx - 5; pred_idx--) {
         aco_ptr<Instruction>& pred = new_instructions[pred_idx];
         if (!(pred->isVALU() && VALU_writes_sgpr(pred)))
            continue;

         for (const Definition& def : pred->definitions) {
            if (def.physReg() > 102)
               continue;

            if (instr->operands.size() > 1 &&
                regs_intersect(instr->operands[1].physReg(), instr->operands[1].size(),
                               def.physReg(), def.size())) {
                  return 5 + pred_idx - new_idx + 1;
            }

            if (instr->operands.size() > 2 &&
                regs_intersect(instr->operands[2].physReg(), instr->operands[2].size(),
                               def.physReg(), def.size())) {
                  return 5 + pred_idx - new_idx + 1;
            }
         }
      }
   }

   return 0;
}

void handle_block_gfx8_9(NOP_ctx_gfx8_9& ctx, Block& block)
{
   std::vector<aco_ptr<Instruction>> instructions;
   instructions.reserve(block.instructions.size());
   for (unsigned i = 0; i < block.instructions.size(); i++) {
      aco_ptr<Instruction>& instr = block.instructions[i];
      unsigned NOPs = handle_instruction_gfx8_9(ctx, instr, block.instructions, instructions);
      if (NOPs) {
         // TODO: try to move the instruction down
         /* create NOP */
         aco_ptr<SOPP_instruction> nop{create_instruction<SOPP_instruction>(aco_opcode::s_nop, Format::SOPP, 0, 0)};
         nop->imm = NOPs - 1;
         nop->block = -1;
         instructions.emplace_back(std::move(nop));
      }

      instructions.emplace_back(std::move(instr));
   }

   ctx.VALU_wrvcc -= instructions.size();
   ctx.VALU_wrexec -= instructions.size();
   ctx.VALU_wrsgpr -= instructions.size();
   block.instructions = std::move(instructions);
}

void insert_NOPs_gfx8_9(Program* program)
{
   NOP_ctx_gfx8_9 ctx(program);

   for (Block& block : program->blocks) {
      if (block.instructions.empty())
         continue;

      handle_block_gfx8_9(ctx, block);
   }
}

void handle_instruction_gfx10(Program *program, NOP_ctx_gfx10 &ctx, aco_ptr<Instruction>& instr,
                              std::vector<aco_ptr<Instruction>>& old_instructions,
                              std::vector<aco_ptr<Instruction>>& new_instructions)
{
   /* VMEMtoScalarWriteHazard
    * Handle EXEC/M0/SGPR write following a VMEM instruction without a VALU or "waitcnt vmcnt(0)" in-between.
    */
   if (instr->isVMEM() || instr->format == Format::FLAT || instr->format == Format::GLOBAL ||
       instr->format == Format::SCRATCH || instr->format == Format::DS) {
      /* Remember all SGPRs that are read by the VMEM instruction */
      mark_read_regs(instr, ctx.sgprs_read_by_VMEM);
      ctx.sgprs_read_by_VMEM.set(exec);
      if (program->wave_size == 64)
         ctx.sgprs_read_by_VMEM.set(exec_hi);
   } else if (instr->isSALU() || instr->format == Format::SMEM) {
      /* Check if SALU writes an SGPR that was previously read by the VALU */
      if (check_written_regs(instr, ctx.sgprs_read_by_VMEM)) {
         ctx.sgprs_read_by_VMEM.reset();

         /* Insert v_nop to mitigate the problem */
         aco_ptr<VOP1_instruction> nop{create_instruction<VOP1_instruction>(aco_opcode::v_nop, Format::VOP1, 0, 0)};
         new_instructions.emplace_back(std::move(nop));
      }
   } else if (instr->opcode == aco_opcode::s_waitcnt) {
      /* Hazard is mitigated by "s_waitcnt vmcnt(0)" */
      uint16_t imm = static_cast<SOPP_instruction*>(instr.get())->imm;
      unsigned vmcnt = (imm & 0xF) | ((imm & (0x3 << 14)) >> 10);
      if (vmcnt == 0)
         ctx.sgprs_read_by_VMEM.reset();
   } else if (instr->isVALU()) {
      /* Hazard is mitigated by any VALU instruction */
      ctx.sgprs_read_by_VMEM.reset();
   }

   /* VcmpxPermlaneHazard
    * Handle any permlane following a VOPC instruction, insert v_mov between them.
    */
   if (instr->format == Format::VOPC) {
      ctx.has_VOPC = true;
   } else if (ctx.has_VOPC &&
              (instr->opcode == aco_opcode::v_permlane16_b32 ||
               instr->opcode == aco_opcode::v_permlanex16_b32)) {
      ctx.has_VOPC = false;

      /* v_nop would be discarded by SQ, so use v_mov with the first operand of the permlane */
      aco_ptr<VOP1_instruction> v_mov{create_instruction<VOP1_instruction>(aco_opcode::v_mov_b32, Format::VOP1, 1, 1)};
      v_mov->definitions[0] = Definition(instr->operands[0].physReg(), v1);
      v_mov->operands[0] = Operand(instr->operands[0].physReg(), v1);
      new_instructions.emplace_back(std::move(v_mov));
   } else if (instr->isVALU() && instr->opcode != aco_opcode::v_nop) {
      ctx.has_VOPC = false;
   }

   /* VcmpxExecWARHazard
    * Handle any VALU instruction writing the exec mask after it was read by a non-VALU instruction.
    */
   if (!instr->isVALU() && instr->reads_exec()) {
      ctx.has_nonVALU_exec_read = true;
   } else if (instr->isVALU()) {
      if (instr_writes_exec(instr)) {
         ctx.has_nonVALU_exec_read = false;

         /* Insert s_waitcnt_depctr instruction with magic imm to mitigate the problem */
         aco_ptr<SOPP_instruction> depctr{create_instruction<SOPP_instruction>(aco_opcode::s_waitcnt_depctr, Format::SOPP, 0, 1)};
         depctr->imm = 0xfffe;
         depctr->definitions[0] = Definition(sgpr_null, s1);
         new_instructions.emplace_back(std::move(depctr));
      } else if (instr_writes_sgpr(instr)) {
         /* Any VALU instruction that writes an SGPR mitigates the problem */
         ctx.has_nonVALU_exec_read = false;
      }
   } else if (instr->opcode == aco_opcode::s_waitcnt_depctr) {
      /* s_waitcnt_depctr can mitigate the problem if it has a magic imm */
      const SOPP_instruction *sopp = static_cast<const SOPP_instruction *>(instr.get());
      if ((sopp->imm & 0xfffe) == 0xfffe)
         ctx.has_nonVALU_exec_read = false;
   }

   /* SMEMtoVectorWriteHazard
    * Handle any VALU instruction writing an SGPR after an SMEM reads it.
    */
   if (instr->format == Format::SMEM) {
      /* Remember all SGPRs that are read by the SMEM instruction */
      mark_read_regs(instr, ctx.sgprs_read_by_SMEM);
   } else if (VALU_writes_sgpr(instr)) {
      /* Check if VALU writes an SGPR that was previously read by SMEM */
      if (check_written_regs(instr, ctx.sgprs_read_by_SMEM)) {
         ctx.sgprs_read_by_SMEM.reset();

         /* Insert s_mov to mitigate the problem */
         aco_ptr<SOP1_instruction> s_mov{create_instruction<SOP1_instruction>(aco_opcode::s_mov_b32, Format::SOP1, 1, 1)};
         s_mov->definitions[0] = Definition(sgpr_null, s1);
         s_mov->operands[0] = Operand(0u);
         new_instructions.emplace_back(std::move(s_mov));
      }
   } else if (instr->isSALU()) {
      if (instr->format != Format::SOPP) {
         /* SALU can mitigate the hazard */
         ctx.sgprs_read_by_SMEM.reset();
      } else {
         /* Reducing lgkmcnt count to 0 always mitigates the hazard. */
         const SOPP_instruction *sopp = static_cast<const SOPP_instruction *>(instr.get());
         if (sopp->opcode == aco_opcode::s_waitcnt_lgkmcnt) {
            if (sopp->imm == 0 && sopp->definitions[0].physReg() == sgpr_null)
               ctx.sgprs_read_by_SMEM.reset();
         } else if (sopp->opcode == aco_opcode::s_waitcnt) {
            unsigned lgkm = (sopp->imm >> 8) & 0x3f;
            if (lgkm == 0)
               ctx.sgprs_read_by_SMEM.reset();
         }
      }
   }

   /* LdsBranchVmemWARHazard
    * Handle VMEM/GLOBAL/SCRATCH->branch->DS and DS->branch->VMEM/GLOBAL/SCRATCH patterns.
    */
   if (instr->isVMEM() || instr->format == Format::GLOBAL || instr->format == Format::SCRATCH) {
      ctx.has_VMEM = true;
      ctx.has_branch_after_VMEM = false;
      /* Mitigation for DS is needed only if there was already a branch after */
      ctx.has_DS = ctx.has_branch_after_DS;
   } else if (instr->format == Format::DS) {
      ctx.has_DS = true;
      ctx.has_branch_after_DS = false;
      /* Mitigation for VMEM is needed only if there was already a branch after */
      ctx.has_VMEM = ctx.has_branch_after_VMEM;
   } else if (instr_is_branch(instr)) {
      ctx.has_branch_after_VMEM = ctx.has_VMEM;
      ctx.has_branch_after_DS = ctx.has_DS;
   } else if (instr->opcode == aco_opcode::s_waitcnt_vscnt) {
      /* Only s_waitcnt_vscnt can mitigate the hazard */
      const SOPK_instruction *sopk = static_cast<const SOPK_instruction *>(instr.get());
      if (sopk->definitions[0].physReg() == sgpr_null && sopk->imm == 0)
         ctx.has_VMEM = ctx.has_branch_after_VMEM = ctx.has_DS = ctx.has_branch_after_DS = false;
   }
   if ((ctx.has_VMEM && ctx.has_branch_after_DS) || (ctx.has_DS && ctx.has_branch_after_VMEM)) {
      ctx.has_VMEM = ctx.has_branch_after_VMEM = ctx.has_DS = ctx.has_branch_after_DS = false;

      /* Insert s_waitcnt_vscnt to mitigate the problem */
      aco_ptr<SOPK_instruction> wait{create_instruction<SOPK_instruction>(aco_opcode::s_waitcnt_vscnt, Format::SOPK, 0, 1)};
      wait->definitions[0] = Definition(sgpr_null, s1);
      wait->imm = 0;
      new_instructions.emplace_back(std::move(wait));
   }
}

void handle_block_gfx10(Program *program, NOP_ctx_gfx10& ctx, Block& block)
{
   if (block.instructions.empty())
      return;

   std::vector<aco_ptr<Instruction>> instructions;
   instructions.reserve(block.instructions.size());

   for (aco_ptr<Instruction>& instr : block.instructions) {
      handle_instruction_gfx10(program, ctx, instr, block.instructions, instructions);
      instructions.emplace_back(std::move(instr));
   }

   block.instructions = std::move(instructions);
}

void mitigate_hazards_gfx10(Program *program)
{
   NOP_ctx_gfx10 all_ctx[program->blocks.size()];
   std::stack<unsigned> loop_header_indices;

   for (unsigned i = 0; i < program->blocks.size(); i++) {
      Block& block = program->blocks[i];
      NOP_ctx_gfx10 &ctx = all_ctx[i];

      if (block.kind & block_kind_loop_header) {
         loop_header_indices.push(i);
      } else if (block.kind & block_kind_loop_exit) {
         /* Go through the whole loop again */
         for (unsigned idx = loop_header_indices.top(); idx < i; idx++) {
            NOP_ctx_gfx10 loop_block_ctx;
            for (unsigned b : program->blocks[idx].linear_preds)
               loop_block_ctx.join(all_ctx[b]);

            handle_block_gfx10(program, loop_block_ctx, program->blocks[idx]);

            /* We only need to continue if the loop header context changed */
            if (idx == loop_header_indices.top() && loop_block_ctx == all_ctx[idx])
               break;

            all_ctx[idx] = loop_block_ctx;
         }

         loop_header_indices.pop();
      }

      for (unsigned b : block.linear_preds)
         ctx.join(all_ctx[b]);

      handle_block_gfx10(program, ctx, block);
   }
}

} /* end namespace */

void insert_NOPs(Program* program)
{
   if (program->chip_class >= GFX10)
      mitigate_hazards_gfx10(program);
   else
      insert_NOPs_gfx8_9(program);
}

}
