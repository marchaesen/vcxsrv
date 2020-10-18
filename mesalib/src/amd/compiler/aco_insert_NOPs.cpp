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
#include <functional>

#include <time.h>

namespace aco {
namespace {

struct NOP_ctx_gfx6 {
   void join(const NOP_ctx_gfx6 &other) {
      set_vskip_mode_then_vector = MAX2(set_vskip_mode_then_vector, other.set_vskip_mode_then_vector);
      valu_wr_vcc_then_vccz = MAX2(valu_wr_vcc_then_vccz, other.valu_wr_vcc_then_vccz);
      valu_wr_exec_then_execz = MAX2(valu_wr_exec_then_execz, other.valu_wr_exec_then_execz);
      valu_wr_vcc_then_div_fmas = MAX2(valu_wr_vcc_then_div_fmas, other.valu_wr_vcc_then_div_fmas);
      salu_wr_m0_then_gds_msg_ttrace = MAX2(salu_wr_m0_then_gds_msg_ttrace, other.salu_wr_m0_then_gds_msg_ttrace);
      valu_wr_exec_then_dpp = MAX2(valu_wr_exec_then_dpp, other.valu_wr_exec_then_dpp);
      salu_wr_m0_then_lds = MAX2(salu_wr_m0_then_lds, other.salu_wr_m0_then_lds);
      salu_wr_m0_then_moverel = MAX2(salu_wr_m0_then_moverel, other.salu_wr_m0_then_moverel);
      setreg_then_getsetreg = MAX2(setreg_then_getsetreg, other.setreg_then_getsetreg);
      vmem_store_then_wr_data |= other.vmem_store_then_wr_data;
      smem_clause |= other.smem_clause;
      smem_write |= other.smem_write;
      for (unsigned i = 0; i < BITSET_WORDS(128); i++) {
         smem_clause_read_write[i] |= other.smem_clause_read_write[i];
         smem_clause_write[i] |= other.smem_clause_write[i];
      }
   }

   bool operator==(const NOP_ctx_gfx6 &other)
   {
      return
         set_vskip_mode_then_vector == other.set_vskip_mode_then_vector &&
         valu_wr_vcc_then_vccz == other.valu_wr_vcc_then_vccz &&
         valu_wr_exec_then_execz == other.valu_wr_exec_then_execz &&
         valu_wr_vcc_then_div_fmas == other.valu_wr_vcc_then_div_fmas &&
         vmem_store_then_wr_data == other.vmem_store_then_wr_data &&
         salu_wr_m0_then_gds_msg_ttrace == other.salu_wr_m0_then_gds_msg_ttrace &&
         valu_wr_exec_then_dpp == other.valu_wr_exec_then_dpp &&
         salu_wr_m0_then_lds == other.salu_wr_m0_then_lds &&
         salu_wr_m0_then_moverel == other.salu_wr_m0_then_moverel &&
         setreg_then_getsetreg == other.setreg_then_getsetreg &&
         smem_clause == other.smem_clause &&
         smem_write == other.smem_write &&
         BITSET_EQUAL(smem_clause_read_write, other.smem_clause_read_write) &&
         BITSET_EQUAL(smem_clause_write, other.smem_clause_write);
   }

   void add_wait_states(unsigned amount)
   {
      if ((set_vskip_mode_then_vector -= amount) < 0)
         set_vskip_mode_then_vector = 0;

      if ((valu_wr_vcc_then_vccz -= amount) < 0)
         valu_wr_vcc_then_vccz = 0;

      if ((valu_wr_exec_then_execz -= amount) < 0)
         valu_wr_exec_then_execz = 0;

      if ((valu_wr_vcc_then_div_fmas -= amount) < 0)
         valu_wr_vcc_then_div_fmas = 0;

      if ((salu_wr_m0_then_gds_msg_ttrace -= amount) < 0)
         salu_wr_m0_then_gds_msg_ttrace = 0;

      if ((valu_wr_exec_then_dpp -= amount) < 0)
         valu_wr_exec_then_dpp = 0;

      if ((salu_wr_m0_then_lds -= amount) < 0)
         salu_wr_m0_then_lds = 0;

      if ((salu_wr_m0_then_moverel -= amount) < 0)
         salu_wr_m0_then_moverel = 0;

      if ((setreg_then_getsetreg -= amount) < 0)
         setreg_then_getsetreg = 0;

      vmem_store_then_wr_data.reset();
   }

   /* setting MODE.vskip and then any vector op requires 2 wait states */
   int8_t set_vskip_mode_then_vector = 0;

   /* VALU writing VCC/EXEC and then a VALU reading VCCZ/EXECZ requires 5 wait states */
   int8_t valu_wr_vcc_then_vccz = 0;
   int8_t valu_wr_exec_then_execz = 0;

   /* VALU writing VCC followed by v_div_fmas require 4 wait states */
   int8_t valu_wr_vcc_then_div_fmas = 0;

   /* SALU writing M0 followed by GDS, s_sendmsg or s_ttrace_data requires 1 wait state */
   int8_t salu_wr_m0_then_gds_msg_ttrace = 0;

   /* VALU writing EXEC followed by DPP requires 5 wait states */
   int8_t valu_wr_exec_then_dpp = 0;

   /* SALU writing M0 followed by some LDS instructions requires 1 wait state on GFX10 */
   int8_t salu_wr_m0_then_lds = 0;

   /* SALU writing M0 followed by s_moverel requires 1 wait state on GFX9 */
   int8_t salu_wr_m0_then_moverel = 0;

   /* s_setreg followed by a s_getreg/s_setreg of the same register needs 2 wait states
    * currently we don't look at the actual register */
   int8_t setreg_then_getsetreg = 0;

   /* some memory instructions writing >64bit followed by a instructions
    * writing the VGPRs holding the writedata requires 1 wait state */
   std::bitset<256> vmem_store_then_wr_data;

   /* we break up SMEM clauses that contain stores or overwrite an
    * operand/definition of another instruction in the clause */
   bool smem_clause = false;
   bool smem_write = false;
   BITSET_DECLARE(smem_clause_read_write, 128) = {0};
   BITSET_DECLARE(smem_clause_write, 128) = {0};
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

int get_wait_states(aco_ptr<Instruction>& instr)
{
   if (instr->opcode == aco_opcode::s_nop)
      return static_cast<SOPP_instruction*>(instr.get())->imm + 1;
   else if (instr->opcode == aco_opcode::p_constaddr)
      return 3; /* lowered to 3 instructions in the assembler */
   else
      return 1;
}

bool regs_intersect(PhysReg a_reg, unsigned a_size, PhysReg b_reg, unsigned b_size)
{
   return a_reg > b_reg ?
          (a_reg - b_reg < b_size) :
          (b_reg - a_reg < a_size);
}

template <bool Valu, bool Vintrp, bool Salu>
int handle_raw_hazard_internal(Program *program, Block *block,
                               int nops_needed, PhysReg reg, uint32_t mask)
{
   unsigned mask_size = util_last_bit(mask);
   for (int pred_idx = block->instructions.size() - 1; pred_idx >= 0; pred_idx--) {
      aco_ptr<Instruction>& pred = block->instructions[pred_idx];

      uint32_t writemask = 0;
      for (Definition& def : pred->definitions) {
         if (regs_intersect(reg, mask_size, def.physReg(), def.size())) {
            unsigned start = def.physReg() > reg ? def.physReg() - reg : 0;
            unsigned end = MIN2(mask_size, start + def.size());
            writemask |= u_bit_consecutive(start, end - start);
         }
      }

      bool is_hazard = writemask != 0 &&
                       ((pred->isVALU() && Valu) ||
                        (pred->format == Format::VINTRP && Vintrp) ||
                        (pred->isSALU() && Salu));
      if (is_hazard)
         return nops_needed;

      mask &= ~writemask;
      nops_needed -= get_wait_states(pred);

      if (nops_needed <= 0 || mask == 0)
         return 0;
   }

   int res = 0;

   /* Loops require branch instructions, which count towards the wait
    * states. So even with loops this should finish unless nops_needed is some
    * huge value. */
   for (unsigned lin_pred : block->linear_preds) {
      res = std::max(res, handle_raw_hazard_internal<Valu, Vintrp, Salu>(
         program, &program->blocks[lin_pred], nops_needed, reg, mask));
   }
   return res;
}

template <bool Valu, bool Vintrp, bool Salu>
void handle_raw_hazard(Program *program, Block *cur_block, int *NOPs, int min_states, Operand op)
{
   if (*NOPs >= min_states)
      return;
   int res = handle_raw_hazard_internal<Valu, Vintrp, Salu>(program, cur_block, min_states, op.physReg(), u_bit_consecutive(0, op.size()));
   *NOPs = MAX2(*NOPs, res);
}

static auto handle_valu_then_read_hazard = handle_raw_hazard<true, true, false>;
static auto handle_vintrp_then_read_hazard = handle_raw_hazard<false, true, false>;
static auto handle_valu_salu_then_read_hazard = handle_raw_hazard<true, true, true>;

void set_bitset_range(BITSET_WORD *words, unsigned start, unsigned size) {
   unsigned end = start + size - 1;
   unsigned start_mod = start % BITSET_WORDBITS;
   if (start_mod + size <= BITSET_WORDBITS) {
      BITSET_SET_RANGE(words, start, end);
   } else {
      unsigned first_size = BITSET_WORDBITS - start_mod;
      set_bitset_range(words, start, BITSET_WORDBITS - start_mod);
      set_bitset_range(words, start + first_size, size - first_size);
   }
}

bool test_bitset_range(BITSET_WORD *words, unsigned start, unsigned size) {
   unsigned end = start + size - 1;
   unsigned start_mod = start % BITSET_WORDBITS;
   if (start_mod + size <= BITSET_WORDBITS) {
      return BITSET_TEST_RANGE(words, start, end);
   } else {
      unsigned first_size = BITSET_WORDBITS - start_mod;
      return test_bitset_range(words, start, BITSET_WORDBITS - start_mod) ||
             test_bitset_range(words, start + first_size, size - first_size);
   }
}

/* A SMEM clause is any group of consecutive SMEM instructions. The
 * instructions in this group may return out of order and/or may be replayed.
 *
 * To fix this potential hazard correctly, we have to make sure that when a
 * clause has more than one instruction, no instruction in the clause writes
 * to a register that is read by another instruction in the clause (including
 * itself). In this case, we have to break the SMEM clause by inserting non
 * SMEM instructions.
 *
 * SMEM clauses are only present on GFX8+, and only matter when XNACK is set.
 */
void handle_smem_clause_hazards(Program *program, NOP_ctx_gfx6 &ctx,
                                aco_ptr<Instruction>& instr, int *NOPs)
{
   /* break off from previous SMEM clause if needed */
   if (!*NOPs & (ctx.smem_clause || ctx.smem_write)) {
      /* Don't allow clauses with store instructions since the clause's
       * instructions may use the same address. */
      if (ctx.smem_write || instr->definitions.empty() || instr_info.is_atomic[(unsigned)instr->opcode]) {
         *NOPs = 1;
      } else if (program->xnack_enabled) {
         for (Operand op : instr->operands) {
            if (!op.isConstant() && test_bitset_range(ctx.smem_clause_write, op.physReg(), op.size())) {
               *NOPs = 1;
               break;
            }
         }

         Definition def = instr->definitions[0];
         if (!*NOPs && test_bitset_range(ctx.smem_clause_read_write, def.physReg(), def.size()))
            *NOPs = 1;
      }
   }
}

/* TODO: we don't handle accessing VCC using the actual SGPR instead of using the alias */
void handle_instruction_gfx6(Program *program, Block *cur_block, NOP_ctx_gfx6 &ctx,
                             aco_ptr<Instruction>& instr, std::vector<aco_ptr<Instruction>>& new_instructions)
{
   /* check hazards */
   int NOPs = 0;

   if (instr->format == Format::SMEM) {
      if (program->chip_class == GFX6) {
         /* A read of an SGPR by SMRD instruction requires 4 wait states
          * when the SGPR was written by a VALU instruction. According to LLVM,
          * there is also an undocumented hardware behavior when the buffer
          * descriptor is written by a SALU instruction */
         for (unsigned i = 0; i < instr->operands.size(); i++) {
            Operand op = instr->operands[i];
            if (op.isConstant())
               continue;

            bool is_buffer_desc = i == 0 && op.size() > 2;
            if (is_buffer_desc)
               handle_valu_salu_then_read_hazard(program, cur_block, &NOPs, 4, op);
            else
               handle_valu_then_read_hazard(program, cur_block, &NOPs, 4, op);
         }
      }

      handle_smem_clause_hazards(program, ctx, instr, &NOPs);
   } else if (instr->isSALU()) {
      if (instr->opcode == aco_opcode::s_setreg_b32 || instr->opcode == aco_opcode::s_setreg_imm32_b32 ||
          instr->opcode == aco_opcode::s_getreg_b32) {
         NOPs = MAX2(NOPs, ctx.setreg_then_getsetreg);
      }

      if (program->chip_class == GFX9) {
         if (instr->opcode == aco_opcode::s_movrels_b32 || instr->opcode == aco_opcode::s_movrels_b64 ||
             instr->opcode == aco_opcode::s_movreld_b32 || instr->opcode == aco_opcode::s_movreld_b64) {
            NOPs = MAX2(NOPs, ctx.salu_wr_m0_then_moverel);
         }
      }

      if (instr->opcode == aco_opcode::s_sendmsg || instr->opcode == aco_opcode::s_ttracedata)
         NOPs = MAX2(NOPs, ctx.salu_wr_m0_then_gds_msg_ttrace);
   } else if (instr->format == Format::DS && static_cast<DS_instruction *>(instr.get())->gds) {
      NOPs = MAX2(NOPs, ctx.salu_wr_m0_then_gds_msg_ttrace);
   } else if (instr->isVALU() || instr->format == Format::VINTRP) {
      for (Operand op : instr->operands) {
         if (op.physReg() == vccz)
            NOPs = MAX2(NOPs, ctx.valu_wr_vcc_then_vccz);
         if (op.physReg() == execz)
            NOPs = MAX2(NOPs, ctx.valu_wr_exec_then_execz);
      }

      if (instr->isDPP()) {
         NOPs = MAX2(NOPs, ctx.valu_wr_exec_then_dpp);
         handle_valu_then_read_hazard(program, cur_block, &NOPs, 2, instr->operands[0]);
      }

      for (Definition def : instr->definitions) {
         if (def.regClass().type() != RegType::sgpr) {
            for (unsigned i = 0; i < def.size(); i++)
               NOPs = MAX2(NOPs, ctx.vmem_store_then_wr_data[(def.physReg() & 0xff) + i]);
         }
      }

      if ((instr->opcode == aco_opcode::v_readlane_b32 ||
           instr->opcode == aco_opcode::v_readlane_b32_e64 ||
           instr->opcode == aco_opcode::v_writelane_b32 ||
           instr->opcode == aco_opcode::v_writelane_b32_e64) &&
          !instr->operands[1].isConstant()) {
         handle_valu_then_read_hazard(program, cur_block, &NOPs, 4, instr->operands[1]);
      }

      /* It's required to insert 1 wait state if the dst VGPR of any v_interp_*
       * is followed by a read with v_readfirstlane or v_readlane to fix GPU
       * hangs on GFX6. Note that v_writelane_* is apparently not affected.
       * This hazard isn't documented anywhere but AMD confirmed that hazard.
       */
      if (program->chip_class == GFX6 &&
          (instr->opcode == aco_opcode::v_readlane_b32 || /* GFX6 doesn't have v_readlane_b32_e64 */
           instr->opcode == aco_opcode::v_readfirstlane_b32)) {
         handle_vintrp_then_read_hazard(program, cur_block, &NOPs, 1, instr->operands[0]);
      }

      if (instr->opcode == aco_opcode::v_div_fmas_f32 || instr->opcode == aco_opcode::v_div_fmas_f64)
         NOPs = MAX2(NOPs, ctx.valu_wr_vcc_then_div_fmas);
   } else if (instr->isVMEM() || instr->isFlatOrGlobal() || instr->format == Format::SCRATCH) {
      /* If the VALU writes the SGPR that is used by a VMEM, the user must add five wait states. */
      for (Operand op : instr->operands) {
         if (!op.isConstant() && !op.isUndefined() && op.regClass().type() == RegType::sgpr)
            handle_valu_then_read_hazard(program, cur_block, &NOPs, 5, op);
      }
   }

   if (!instr->isSALU() && instr->format != Format::SMEM)
      NOPs = MAX2(NOPs, ctx.set_vskip_mode_then_vector);

   if (program->chip_class == GFX9) {
      bool lds_scratch_global = (instr->format == Format::SCRATCH || instr->format == Format::GLOBAL) &&
                                static_cast<FLAT_instruction *>(instr.get())->lds;
      if (instr->format == Format::VINTRP ||
          instr->opcode == aco_opcode::ds_read_addtid_b32 ||
          instr->opcode == aco_opcode::ds_write_addtid_b32 ||
          instr->opcode == aco_opcode::buffer_store_lds_dword ||
          lds_scratch_global) {
         NOPs = MAX2(NOPs, ctx.salu_wr_m0_then_lds);
      }
   }

   ctx.add_wait_states(NOPs + get_wait_states(instr));

   // TODO: try to schedule the NOP-causing instruction up to reduce the number of stall cycles
   if (NOPs) {
      /* create NOP */
      aco_ptr<SOPP_instruction> nop{create_instruction<SOPP_instruction>(aco_opcode::s_nop, Format::SOPP, 0, 0)};
      nop->imm = NOPs - 1;
      nop->block = -1;
      new_instructions.emplace_back(std::move(nop));
   }

   /* update information to check for later hazards */
   if ((ctx.smem_clause || ctx.smem_write) && (NOPs || instr->format != Format::SMEM)) {
      ctx.smem_clause = false;
      ctx.smem_write = false;

      if (program->xnack_enabled) {
         BITSET_ZERO(ctx.smem_clause_read_write);
         BITSET_ZERO(ctx.smem_clause_write);
      }
   }

   if (instr->format == Format::SMEM) {
      if (instr->definitions.empty() || instr_info.is_atomic[(unsigned)instr->opcode]) {
         ctx.smem_write = true;
      } else {
         ctx.smem_clause = true;

         if (program->xnack_enabled) {
            for (Operand op : instr->operands) {
               if (!op.isConstant()) {
                  set_bitset_range(ctx.smem_clause_read_write, op.physReg(), op.size());
               }
            }

            Definition def = instr->definitions[0];
            set_bitset_range(ctx.smem_clause_read_write, def.physReg(), def.size());
            set_bitset_range(ctx.smem_clause_write, def.physReg(), def.size());
         }
      }
   } else if (instr->isVALU()) {
      for (Definition def : instr->definitions) {
         if (def.regClass().type() == RegType::sgpr) {
            if (def.physReg() == vcc || def.physReg() == vcc_hi) {
               ctx.valu_wr_vcc_then_vccz = 5;
               ctx.valu_wr_vcc_then_div_fmas = 4;
            }
            if (def.physReg() == exec || def.physReg() == exec_hi) {
               ctx.valu_wr_exec_then_execz = 5;
               ctx.valu_wr_exec_then_dpp = 5;
            }
         }
      }
   } else if (instr->isSALU() && !instr->definitions.empty()) {
      if (!instr->definitions.empty()) {
         /* all other definitions should be SCC */
         Definition def = instr->definitions[0];
         if (def.physReg() == m0) {
            ctx.salu_wr_m0_then_gds_msg_ttrace = 1;
            ctx.salu_wr_m0_then_lds = 1;
            ctx.salu_wr_m0_then_moverel = 1;
         }
      } else if (instr->opcode == aco_opcode::s_setreg_b32 || instr->opcode == aco_opcode::s_setreg_imm32_b32) {
         SOPK_instruction *sopk = static_cast<SOPK_instruction *>(instr.get());
         unsigned offset = (sopk->imm >> 6) & 0x1f;
         unsigned size = ((sopk->imm >> 11) & 0x1f) + 1;
         unsigned reg = sopk->imm & 0x3f;
         ctx.setreg_then_getsetreg = 2;

         if (reg == 1 && offset >= 28 && size > (28 - offset))
            ctx.set_vskip_mode_then_vector = 2;
      }
   } else if (instr->isVMEM() || instr->isFlatOrGlobal() || instr->format == Format::SCRATCH) {
      /* >64-bit MUBUF/MTBUF store with a constant in SOFFSET */
      bool consider_buf = (instr->format == Format::MUBUF || instr->format == Format::MTBUF) &&
                          instr->operands.size() == 4 &&
                          instr->operands[3].size() > 2 &&
                          instr->operands[2].physReg() >= 128;
      /* MIMG store with a 128-bit T# with more than two bits set in dmask (making it a >64-bit store) */
      bool consider_mimg = instr->format == Format::MIMG &&
                           instr->operands[1].regClass().type() == RegType::vgpr &&
                           instr->operands[1].size() > 2 &&
                           instr->operands[0].size() == 4;
      /* FLAT/GLOBAL/SCRATCH store with >64-bit data */
      bool consider_flat = (instr->isFlatOrGlobal() || instr->format == Format::SCRATCH) &&
                            instr->operands.size() == 3 &&
                            instr->operands[2].size() > 2;
      if (consider_buf || consider_mimg || consider_flat) {
         PhysReg wrdata = instr->operands[consider_flat ? 2 : 3].physReg();
         unsigned size = instr->operands[consider_flat ? 2 : 3].size();
         for (unsigned i = 0; i < size; i++)
            ctx.vmem_store_then_wr_data[(wrdata & 0xff) + i] = 1;
      }
   }
}

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

void handle_instruction_gfx10(Program *program, Block *cur_block, NOP_ctx_gfx10 &ctx,
                              aco_ptr<Instruction>& instr, std::vector<aco_ptr<Instruction>>& new_instructions)
{
   //TODO: s_dcache_inv needs to be in it's own group on GFX10

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
      if (instr->opcode == aco_opcode::s_waitcnt) {
         /* Hazard is mitigated by "s_waitcnt vmcnt(0)" */
         uint16_t imm = static_cast<SOPP_instruction*>(instr.get())->imm;
         unsigned vmcnt = (imm & 0xF) | ((imm & (0x3 << 14)) >> 10);
         if (vmcnt == 0)
            ctx.sgprs_read_by_VMEM.reset();
      } else if (instr->opcode == aco_opcode::s_waitcnt_depctr) {
         /* Hazard is mitigated by a s_waitcnt_depctr with a magic imm */
         const SOPP_instruction *sopp = static_cast<const SOPP_instruction *>(instr.get());
         if (sopp->imm == 0xffe3)
            ctx.sgprs_read_by_VMEM.reset();
      }

      /* Check if SALU writes an SGPR that was previously read by the VALU */
      if (check_written_regs(instr, ctx.sgprs_read_by_VMEM)) {
         ctx.sgprs_read_by_VMEM.reset();

         /* Insert s_waitcnt_depctr instruction with magic imm to mitigate the problem */
         aco_ptr<SOPP_instruction> depctr{create_instruction<SOPP_instruction>(aco_opcode::s_waitcnt_depctr, Format::SOPP, 0, 0)};
         depctr->imm = 0xffe3;
         depctr->block = -1;
         new_instructions.emplace_back(std::move(depctr));
      }
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
         aco_ptr<SOPP_instruction> depctr{create_instruction<SOPP_instruction>(aco_opcode::s_waitcnt_depctr, Format::SOPP, 0, 0)};
         depctr->imm = 0xfffe;
         depctr->block = -1;
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

template <typename Ctx>
using HandleInstr = void (*)(Program *, Block *block, Ctx&, aco_ptr<Instruction>&,
                             std::vector<aco_ptr<Instruction>>&);

template <typename Ctx, HandleInstr<Ctx> Handle>
void handle_block(Program *program, Ctx& ctx, Block& block)
{
   if (block.instructions.empty())
      return;

   std::vector<aco_ptr<Instruction>> old_instructions = std::move(block.instructions);

   block.instructions.reserve(block.instructions.size());

   for (aco_ptr<Instruction>& instr : old_instructions) {
      Handle(program, &block, ctx, instr, block.instructions);
      block.instructions.emplace_back(std::move(instr));
   }
}

template <typename Ctx, HandleInstr<Ctx> Handle>
void mitigate_hazards(Program *program)
{
   std::vector<Ctx> all_ctx(program->blocks.size());
   std::stack<unsigned> loop_header_indices;

   for (unsigned i = 0; i < program->blocks.size(); i++) {
      Block& block = program->blocks[i];
      Ctx &ctx = all_ctx[i];

      if (block.kind & block_kind_loop_header) {
         loop_header_indices.push(i);
      } else if (block.kind & block_kind_loop_exit) {
         /* Go through the whole loop again */
         for (unsigned idx = loop_header_indices.top(); idx < i; idx++) {
            Ctx loop_block_ctx;
            for (unsigned b : program->blocks[idx].linear_preds)
               loop_block_ctx.join(all_ctx[b]);

            handle_block<Ctx, Handle>(program, loop_block_ctx, program->blocks[idx]);

            /* We only need to continue if the loop header context changed */
            if (idx == loop_header_indices.top() && loop_block_ctx == all_ctx[idx])
               break;

            all_ctx[idx] = loop_block_ctx;
         }

         loop_header_indices.pop();
      }

      for (unsigned b : block.linear_preds)
         ctx.join(all_ctx[b]);

      handle_block<Ctx, Handle>(program, ctx, block);
   }
}

} /* end namespace */

void insert_NOPs(Program* program)
{
   if (program->chip_class >= GFX10_3)
      ; /* no hazards/bugs to mitigate */
   else if (program->chip_class >= GFX10)
      mitigate_hazards<NOP_ctx_gfx10, handle_instruction_gfx10>(program);
   else
      mitigate_hazards<NOP_ctx_gfx6, handle_instruction_gfx6>(program);
}

}
