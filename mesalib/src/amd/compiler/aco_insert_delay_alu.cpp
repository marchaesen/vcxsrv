/*
 * Copyright © 2018 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "aco_builder.h"
#include "aco_ir.h"

#include <map>
#include <stack>
#include <vector>

namespace aco {

namespace {

/* On GFX11+ the SIMD frontend doesn't switch to issuing instructions from a different
 * wave if there is an ALU stall. Hence we have an instruction (s_delay_alu) to signal
 * that we should switch to a different wave and contains info on dependencies as to
 * when we can switch back.
 *
 * This seems to apply only for ALU->ALU dependencies as other instructions have better
 * integration with the frontend.
 *
 * Note that if we do not emit s_delay_alu things will still be correct, but the wave
 * will stall in the ALU (and the ALU will be doing nothing else). We'll use this as
 * I'm pretty sure our cycle info is wrong at times (necessarily so, e.g. wave64 VALU
 * instructions can take a different number of cycles based on the exec mask)
 */
struct alu_delay_info {
   /* These are the values directly above the max representable value, i.e. the wait
    * would turn into a no-op when we try to wait for something further back than
    * this.
    */
   static constexpr int8_t valu_nop = 5;
   static constexpr int8_t trans_nop = 4;

   /* How many VALU instructions ago this value was written */
   int8_t valu_instrs = valu_nop;
   /* Cycles until the writing VALU instruction is finished */
   int8_t valu_cycles = 0;

   /* How many Transcedent instructions ago this value was written */
   int8_t trans_instrs = trans_nop;
   /* Cycles until the writing Transcendent instruction is finished */
   int8_t trans_cycles = 0;

   /* Cycles until the writing SALU instruction is finished*/
   int8_t salu_cycles = 0;

   /* VALU wrote this as lane mask. */
   bool lane_mask_forwarding = true;

   bool combine(const alu_delay_info& other)
   {
      bool changed = other.valu_instrs < valu_instrs || other.trans_instrs < trans_instrs ||
                     other.salu_cycles > salu_cycles || other.valu_cycles > valu_cycles ||
                     other.trans_cycles > trans_cycles;
      valu_instrs = std::min(valu_instrs, other.valu_instrs);
      trans_instrs = std::min(trans_instrs, other.trans_instrs);
      salu_cycles = std::max(salu_cycles, other.salu_cycles);
      valu_cycles = std::max(valu_cycles, other.valu_cycles);
      trans_cycles = std::max(trans_cycles, other.trans_cycles);
      lane_mask_forwarding &= other.lane_mask_forwarding;
      return changed;
   }

   /* Needs to be called after any change to keep the data consistent. */
   bool fixup()
   {
      if (valu_instrs >= valu_nop || valu_cycles <= 0) {
         valu_instrs = valu_nop;
         valu_cycles = 0;
      }

      if (trans_instrs >= trans_nop || trans_cycles <= 0) {
         trans_instrs = trans_nop;
         trans_cycles = 0;
      }

      salu_cycles = std::max<int8_t>(salu_cycles, 0);

      return empty();
   }

   /* Returns true if a wait would be a no-op */
   bool empty() const
   {
      return valu_instrs == valu_nop && trans_instrs == trans_nop && salu_cycles == 0;
   }

   UNUSED void print(FILE* output) const
   {
      if (valu_instrs != valu_nop)
         fprintf(output, "valu_instrs: %u\n", valu_instrs);
      if (valu_cycles)
         fprintf(output, "valu_cycles: %u\n", valu_cycles);
      if (trans_instrs != trans_nop)
         fprintf(output, "trans_instrs: %u\n", trans_instrs);
      if (trans_cycles)
         fprintf(output, "trans_cycles: %u\n", trans_cycles);
      if (salu_cycles)
         fprintf(output, "salu_cycles: %u\n", salu_cycles);
   }
};

struct delay_ctx {
   Program* program;
   std::map<PhysReg, alu_delay_info> gpr_map;

   delay_ctx() {}
   delay_ctx(Program* program_) : program(program_) {}

   UNUSED void print(FILE* output) const
   {
      for (const auto& entry : gpr_map) {
         fprintf(output, "gpr_map[%c%u] = {\n", entry.first.reg() >= 256 ? 'v' : 's',
                 entry.first.reg() & 0xff);
         entry.second.print(output);
         fprintf(output, "}\n");
      }
   }
};

void
check_alu(delay_ctx& ctx, alu_delay_info& delay, Instruction* instr)
{
   for (unsigned i = 0; i < instr->operands.size(); i++) {
      const Operand op = instr->operands[i];
      alu_delay_info op_delay;
      if (op.isConstant() || op.isUndefined())
         continue;

      /* check consecutively read gprs */
      for (unsigned j = 0; j < op.size(); j++) {
         std::map<PhysReg, alu_delay_info>::iterator it =
            ctx.gpr_map.find(PhysReg{op.physReg() + j});
         if (it != ctx.gpr_map.end())
            op_delay.combine(it->second);
      }

      bool fast_forward = (instr->opcode == aco_opcode::v_cndmask_b32 ||
                           instr->opcode == aco_opcode::v_cndmask_b16 ||
                           instr->opcode == aco_opcode::v_dual_cndmask_b32) &&
                          i == 2;
      fast_forward |= instr->isVOPD() && instr->vopd().opy == aco_opcode::v_dual_cndmask_b32 &&
                      i + 1 == instr->operands.size();
      if (!op_delay.lane_mask_forwarding || !fast_forward)
         delay.combine(op_delay);
   }
}

void
update_alu(delay_ctx& ctx, bool is_valu, bool is_trans, int cycles)
{
   std::map<PhysReg, alu_delay_info>::iterator it = ctx.gpr_map.begin();
   while (it != ctx.gpr_map.end()) {
      alu_delay_info& entry = it->second;
      entry.valu_instrs += is_valu ? 1 : 0;
      entry.trans_instrs += is_trans ? 1 : 0;
      entry.salu_cycles -= cycles;
      entry.valu_cycles -= cycles;
      entry.trans_cycles -= cycles;
      it = it->second.fixup() ? ctx.gpr_map.erase(it) : std::next(it);
   }
}

void
kill_alu(alu_delay_info& delay, Instruction* instr, delay_ctx& ctx)
{
   /* Consider frontend waits first. These are automatically done by the hardware,
    * so we don't need to insert s_delay_alu.
    * They are also lower granularity, waiting for accesses of a counter instead
    * of only the real per register dependencies.
    */
   depctr_wait wait = parse_depctr_wait(instr);

   int8_t implict_cycles = 0;
   if (!wait.va_vdst || !wait.va_sdst || !wait.va_vcc || !wait.sa_sdst || !wait.sa_exec ||
       !wait.va_exec) {
      std::map<PhysReg, alu_delay_info>::iterator it = ctx.gpr_map.begin();
      while (it != ctx.gpr_map.end()) {
         alu_delay_info& entry = it->second;
         bool wait_valu = !wait.va_vdst || (it->first < vcc && !wait.va_sdst) ||
                          (it->first >= vcc && it->first <= vcc_hi && !wait.va_vcc) ||
                          (it->first >= exec && it->first <= exec_hi && !wait.va_exec);
         if (wait_valu) {
            implict_cycles = MAX3(implict_cycles, entry.valu_cycles, entry.trans_cycles);
            entry.valu_cycles = 0;
            entry.trans_cycles = 0;
         }
         bool wait_salu = ((it->first <= vcc_hi || it->first == scc) && !wait.sa_sdst) ||
                          (it->first >= exec && it->first <= exec_hi && !wait.sa_exec);
         if (wait_salu) {
            implict_cycles = MAX2(implict_cycles, entry.salu_cycles);
            entry.salu_cycles = 0;
         }
         it = it->second.fixup() ? ctx.gpr_map.erase(it) : std::next(it);
      }
   }

   /* Previous alu progresses as usual while the frontend waits. */
   if (implict_cycles != 0)
      update_alu(ctx, false, false, implict_cycles);

   if (instr->isVALU() || instr->isSALU())
      check_alu(ctx, delay, instr);

   if (!delay.empty()) {
      update_alu(ctx, false, false, MAX3(delay.salu_cycles, delay.valu_cycles, delay.trans_cycles));

      /* remove all gprs with higher counter from map */
      std::map<PhysReg, alu_delay_info>::iterator it = ctx.gpr_map.begin();
      while (it != ctx.gpr_map.end()) {
         if (delay.valu_instrs <= it->second.valu_instrs)
            it->second.valu_instrs = alu_delay_info::valu_nop;
         if (delay.trans_instrs <= it->second.trans_instrs)
            it->second.trans_instrs = alu_delay_info::trans_nop;
         it = it->second.fixup() ? ctx.gpr_map.erase(it) : std::next(it);
      }
   }
}

void
gen_alu(Instruction* instr, delay_ctx& ctx)
{
   Instruction_cycle_info cycle_info = get_cycle_info(*ctx.program, *instr);
   bool is_valu = instr->isVALU();
   bool is_trans = instr->isTrans();

   if (is_trans || is_valu || instr->isSALU()) {
      alu_delay_info delay;
      delay.lane_mask_forwarding = false;
      if (is_trans) {
         delay.trans_instrs = 0;
         delay.trans_cycles = cycle_info.latency;
      } else if (is_valu) {
         delay.valu_instrs = 0;
         delay.valu_cycles = cycle_info.latency;
      } else if (instr->isSALU()) {
         delay.salu_cycles = cycle_info.latency;
      }

      for (Definition& def : instr->definitions) {
         if (is_valu && def.regClass() == ctx.program->lane_mask) {
            delay.lane_mask_forwarding = instr->opcode != aco_opcode::v_readlane_b32_e64 &&
                                         instr->opcode != aco_opcode::v_readfirstlane_b32;
         }

         for (unsigned j = 0; j < def.size(); j++) {
            auto it = ctx.gpr_map.emplace(PhysReg{def.physReg().reg() + j}, delay);
            if (!it.second)
               it.first->second.combine(delay);
         }
      }
   }

   update_alu(ctx, is_valu && instr_info.classes[(int)instr->opcode] != instr_class::wmma, is_trans,
              cycle_info.issue_cycles);
}

void
emit_delay_alu(delay_ctx& ctx, std::vector<aco_ptr<Instruction>>& instructions,
               alu_delay_info& delay)
{
   uint32_t imm = 0;
   if (delay.trans_instrs != delay.trans_nop) {
      imm |= (uint32_t)alu_delay_wait::TRANS32_DEP_1 + delay.trans_instrs - 1;
   }

   if (delay.valu_instrs != delay.valu_nop) {
      imm |= ((uint32_t)alu_delay_wait::VALU_DEP_1 + delay.valu_instrs - 1) << (imm ? 7 : 0);
   }

   /* Note that we can only put 2 wait conditions in the instruction, so if we have all 3 we just
    * drop the SALU one. Here we use that this doesn't really affect correctness so occasionally
    * getting this wrong isn't an issue. */
   if (delay.salu_cycles && imm <= 0xf) {
      unsigned cycles = std::min<uint8_t>(3, delay.salu_cycles);
      imm |= ((uint32_t)alu_delay_wait::SALU_CYCLE_1 + cycles - 1) << (imm ? 7 : 0);
   }

   Instruction* inst = create_instruction(aco_opcode::s_delay_alu, Format::SOPP, 0, 0);
   inst->salu().imm = imm;
   instructions.emplace_back(inst);
   delay = alu_delay_info();
}

void
handle_block(Program* program, Block& block, delay_ctx& ctx)
{
   std::vector<aco_ptr<Instruction>> new_instructions;
   alu_delay_info queued_delay;

   for (size_t i = 0; i < block.instructions.size(); i++) {
      aco_ptr<Instruction>& instr = block.instructions[i];
      assert(instr->opcode != aco_opcode::s_delay_alu);

      kill_alu(queued_delay, instr.get(), ctx);
      gen_alu(instr.get(), ctx);

      if (!queued_delay.empty())
         emit_delay_alu(ctx, new_instructions, queued_delay);
      new_instructions.emplace_back(std::move(instr));
   }

   block.instructions.swap(new_instructions);
}

} /* end namespace */

void
insert_delay_alu(Program* program)
{
   /* per BB ctx */
   delay_ctx ctx(program);

   for (unsigned i = 0; i < program->blocks.size(); i++) {
      Block& current = program->blocks[i];

      if (current.instructions.empty())
         continue;

      handle_block(program, current, ctx);

      /* Reset ctx if there is a jump, assuming ALU will be done
       * because branch latency is pretty high.
       */
      if (current.linear_succs.empty() ||
          current.instructions.back()->opcode == aco_opcode::s_branch) {
         ctx = delay_ctx(program);
      }
   }
}

void
combine_delay_alu(Program* program)
{
   /* Combine s_delay_alu using the skip field. */
   for (Block& block : program->blocks) {
      int i = 0;
      int prev_delay_alu = -1;
      for (aco_ptr<Instruction>& instr : block.instructions) {
         if (instr->opcode != aco_opcode::s_delay_alu) {
            block.instructions[i++] = std::move(instr);
            continue;
         }

         uint16_t imm = instr->salu().imm;
         int skip = i - prev_delay_alu - 1;
         if (imm >> 7 || prev_delay_alu < 0 || skip >= 6) {
            if (imm >> 7 == 0)
               prev_delay_alu = i;
            block.instructions[i++] = std::move(instr);
            continue;
         }

         block.instructions[prev_delay_alu]->salu().imm |= (skip << 4) | (imm << 7);
         prev_delay_alu = -1;
      }
      block.instructions.resize(i);
   }
}

} // namespace aco
