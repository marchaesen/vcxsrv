/*
 * Copyright © 2018 Valve Corporation
 * Copyright © 2018 Google
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
 * Authors:
 *    Daniel Schürmann (daniel.schuermann@campus.tu-berlin.de)
 *    Bas Nieuwenhuizen (bas@basnieuwenhuizen.nl)
 *
 */

#include "aco_ir.h"
#include "util/u_math.h"

#include <set>
#include <vector>

namespace aco {
RegisterDemand get_live_changes(aco_ptr<Instruction>& instr)
{
   RegisterDemand changes;
   for (const Definition& def : instr->definitions) {
      if (!def.isTemp() || def.isKill())
         continue;
      changes += def.getTemp();
   }

   for (const Operand& op : instr->operands) {
      if (!op.isTemp() || !op.isFirstKill())
         continue;
      changes -= op.getTemp();
   }

   return changes;
}

RegisterDemand get_temp_registers(aco_ptr<Instruction>& instr)
{
   RegisterDemand temp_registers;

   for (Definition def : instr->definitions) {
      if (!def.isTemp())
         continue;
      if (def.isKill())
         temp_registers += def.getTemp();
   }

   for (Operand op : instr->operands) {
      if (op.isTemp() && op.isLateKill() && op.isFirstKill())
         temp_registers += op.getTemp();
   }

   return temp_registers;
}

RegisterDemand get_demand_before(RegisterDemand demand, aco_ptr<Instruction>& instr, aco_ptr<Instruction>& instr_before)
{
   demand -= get_live_changes(instr);
   demand -= get_temp_registers(instr);
   if (instr_before)
      demand += get_temp_registers(instr_before);
   return demand;
}

namespace {
void process_live_temps_per_block(Program *program, live& lives, Block* block,
                                  std::set<unsigned>& worklist, std::vector<uint16_t>& phi_sgpr_ops)
{
   std::vector<RegisterDemand>& register_demand = lives.register_demand[block->index];
   RegisterDemand new_demand;

   register_demand.resize(block->instructions.size());
   block->register_demand = RegisterDemand();
   IDSet live = lives.live_out[block->index];

   /* add the live_out_exec to live */
   bool exec_live = false;
   if (block->live_out_exec != Temp()) {
      live.insert(block->live_out_exec.id());
      exec_live = true;
   }

   /* initialize register demand */
   for (unsigned t : live)
      new_demand += Temp(t, program->temp_rc[t]);
   new_demand.sgpr -= phi_sgpr_ops[block->index];

   /* traverse the instructions backwards */
   int idx;
   for (idx = block->instructions.size() -1; idx >= 0; idx--) {
      Instruction *insn = block->instructions[idx].get();
      if (is_phi(insn))
         break;

      /* substract the 1 or 2 sgprs from exec */
      if (exec_live)
         assert(new_demand.sgpr >= (int16_t) program->lane_mask.size());
      register_demand[idx] = RegisterDemand(new_demand.vgpr, new_demand.sgpr - (exec_live ? program->lane_mask.size() : 0));

      /* KILL */
      for (Definition& definition : insn->definitions) {
         if (!definition.isTemp()) {
            continue;
         }
         if ((definition.isFixed() || definition.hasHint()) && definition.physReg() == vcc)
            program->needs_vcc = true;

         const Temp temp = definition.getTemp();
         const size_t n = live.erase(temp.id());

         if (n) {
            new_demand -= temp;
            definition.setKill(false);
         } else {
            register_demand[idx] += temp;
            definition.setKill(true);
         }

         if (definition.isFixed() && definition.physReg() == exec)
            exec_live = false;
      }

      /* GEN */
      if (insn->opcode == aco_opcode::p_logical_end) {
         new_demand.sgpr += phi_sgpr_ops[block->index];
      } else {
         /* we need to do this in a separate loop because the next one can
          * setKill() for several operands at once and we don't want to
          * overwrite that in a later iteration */
         for (Operand& op : insn->operands)
            op.setKill(false);

         for (unsigned i = 0; i < insn->operands.size(); ++i)
         {
            Operand& operand = insn->operands[i];
            if (!operand.isTemp())
               continue;
            if (operand.isFixed() && operand.physReg() == vcc)
               program->needs_vcc = true;
            const Temp temp = operand.getTemp();
            const bool inserted = live.insert(temp.id()).second;
            if (inserted) {
               operand.setFirstKill(true);
               for (unsigned j = i + 1; j < insn->operands.size(); ++j) {
                  if (insn->operands[j].isTemp() && insn->operands[j].tempId() == operand.tempId()) {
                     insn->operands[j].setFirstKill(false);
                     insn->operands[j].setKill(true);
                  }
               }
               if (operand.isLateKill())
                  register_demand[idx] += temp;
               new_demand += temp;
            }

            if (operand.isFixed() && operand.physReg() == exec)
               exec_live = true;
         }
      }

      block->register_demand.update(register_demand[idx]);
   }

   /* update block's register demand for a last time */
   if (exec_live)
      assert(new_demand.sgpr >= (int16_t) program->lane_mask.size());
   new_demand.sgpr -= exec_live ? program->lane_mask.size() : 0;
   block->register_demand.update(new_demand);

   /* handle phi definitions */
   int phi_idx = idx;
   while (phi_idx >= 0) {
      register_demand[phi_idx] = new_demand;
      Instruction *insn = block->instructions[phi_idx].get();

      assert(is_phi(insn));
      assert(insn->definitions.size() == 1 && insn->definitions[0].isTemp());
      Definition& definition = insn->definitions[0];
      if ((definition.isFixed() || definition.hasHint()) && definition.physReg() == vcc)
         program->needs_vcc = true;
      const Temp temp = definition.getTemp();
      const size_t n = live.erase(temp.id());

      if (n)
         definition.setKill(false);
      else
         definition.setKill(true);

      phi_idx--;
   }

   /* now, we need to merge the live-ins into the live-out sets */
   for (unsigned t : live) {
      RegClass rc = program->temp_rc[t];
      std::vector<unsigned>& preds = rc.is_linear() ? block->linear_preds : block->logical_preds;

#ifndef NDEBUG
      if (preds.empty())
         aco_err(program, "Temporary never defined or are defined after use: %%%d in BB%d", t, block->index);
#endif

      for (unsigned pred_idx : preds) {
         auto it = lives.live_out[pred_idx].insert(t);
         if (it.second)
            worklist.insert(pred_idx);
      }
   }

   /* handle phi operands */
   phi_idx = idx;
   while (phi_idx >= 0) {
      Instruction *insn = block->instructions[phi_idx].get();
      assert(is_phi(insn));
      /* directly insert into the predecessors live-out set */
      std::vector<unsigned>& preds = insn->opcode == aco_opcode::p_phi
                                   ? block->logical_preds
                                   : block->linear_preds;
      for (unsigned i = 0; i < preds.size(); ++i) {
         Operand &operand = insn->operands[i];
         if (!operand.isTemp())
            continue;
         if (operand.isFixed() && operand.physReg() == vcc)
            program->needs_vcc = true;
         /* check if we changed an already processed block */
         const bool inserted = lives.live_out[preds[i]].insert(operand.tempId()).second;
         if (inserted) {
            operand.setKill(true);
            worklist.insert(preds[i]);
            if (insn->opcode == aco_opcode::p_phi && operand.getTemp().type() == RegType::sgpr)
               phi_sgpr_ops[preds[i]] += operand.size();
         }
      }
      phi_idx--;
   }

   assert(block->index != 0 || (new_demand == RegisterDemand() && live.empty()));
}

unsigned calc_waves_per_workgroup(Program *program)
{
   /* When workgroup size is not known, just go with wave_size */
   unsigned workgroup_size = program->workgroup_size == UINT_MAX
                             ? program->wave_size
                             : program->workgroup_size;

   return align(workgroup_size, program->wave_size) / program->wave_size;
}
} /* end namespace */

uint16_t get_extra_sgprs(Program *program)
{
   if (program->chip_class >= GFX10) {
      assert(!program->needs_flat_scr);
      assert(!program->xnack_enabled);
      return 2;
   } else if (program->chip_class >= GFX8) {
      if (program->needs_flat_scr)
         return 6;
      else if (program->xnack_enabled)
         return 4;
      else if (program->needs_vcc)
         return 2;
      else
         return 0;
   } else {
      assert(!program->xnack_enabled);
      if (program->needs_flat_scr)
         return 4;
      else if (program->needs_vcc)
         return 2;
      else
         return 0;
   }
}

uint16_t get_sgpr_alloc(Program *program, uint16_t addressable_sgprs)
{
   assert(addressable_sgprs <= program->sgpr_limit);
   uint16_t sgprs = addressable_sgprs + get_extra_sgprs(program);
   uint16_t granule = program->sgpr_alloc_granule + 1;
   return align(std::max(sgprs, granule), granule);
}

uint16_t get_vgpr_alloc(Program *program, uint16_t addressable_vgprs)
{
   assert(addressable_vgprs <= program->vgpr_limit);
   uint16_t granule = program->vgpr_alloc_granule + 1;
   return align(std::max(addressable_vgprs, granule), granule);
}

uint16_t get_addr_sgpr_from_waves(Program *program, uint16_t max_waves)
{
    uint16_t sgprs = program->physical_sgprs / max_waves & ~program->sgpr_alloc_granule;
    sgprs -= get_extra_sgprs(program);
    return std::min(sgprs, program->sgpr_limit);
}

uint16_t get_addr_vgpr_from_waves(Program *program, uint16_t max_waves)
{
    uint16_t vgprs = 256 / max_waves & ~program->vgpr_alloc_granule;
    return std::min(vgprs, program->vgpr_limit);
}

void calc_min_waves(Program* program)
{
   unsigned waves_per_workgroup = calc_waves_per_workgroup(program);
   /* currently min_waves is in wave64 waves */
   if (program->wave_size == 32)
      waves_per_workgroup = DIV_ROUND_UP(waves_per_workgroup, 2);

   unsigned simd_per_cu = 4; /* TODO: different on Navi */
   bool wgp = program->chip_class >= GFX10; /* assume WGP is used on Navi */
   unsigned simd_per_cu_wgp = wgp ? simd_per_cu * 2 : simd_per_cu;

   program->min_waves = DIV_ROUND_UP(waves_per_workgroup, simd_per_cu_wgp);
}

void update_vgpr_sgpr_demand(Program* program, const RegisterDemand new_demand)
{
   /* TODO: max_waves_per_simd, simd_per_cu and the number of physical vgprs for Navi */
   unsigned max_waves_per_simd = 10;
   if ((program->family >= CHIP_POLARIS10 && program->family <= CHIP_VEGAM) || program->chip_class >= GFX10_3)
      max_waves_per_simd = 8;
   unsigned simd_per_cu = 4;

   bool wgp = program->chip_class >= GFX10; /* assume WGP is used on Navi */
   unsigned simd_per_cu_wgp = wgp ? simd_per_cu * 2 : simd_per_cu;
   unsigned lds_limit = wgp ? program->lds_limit * 2 : program->lds_limit;

   /* this won't compile, register pressure reduction necessary */
   if (new_demand.vgpr > program->vgpr_limit || new_demand.sgpr > program->sgpr_limit) {
      program->num_waves = 0;
      program->max_reg_demand = new_demand;
   } else {
      program->num_waves = program->physical_sgprs / get_sgpr_alloc(program, new_demand.sgpr);
      program->num_waves = std::min<uint16_t>(program->num_waves, 256 / get_vgpr_alloc(program, new_demand.vgpr));
      program->max_waves = max_waves_per_simd;

      /* adjust max_waves for workgroup and LDS limits */
      unsigned waves_per_workgroup = calc_waves_per_workgroup(program);
      unsigned workgroups_per_cu_wgp = max_waves_per_simd * simd_per_cu_wgp / waves_per_workgroup;
      if (program->config->lds_size) {
         unsigned lds = program->config->lds_size * program->lds_alloc_granule;
         workgroups_per_cu_wgp = std::min(workgroups_per_cu_wgp, lds_limit / lds);
      }
      if (waves_per_workgroup > 1 && program->chip_class < GFX10)
         workgroups_per_cu_wgp = std::min(workgroups_per_cu_wgp, 16u); /* TODO: is this a SI-only limit? what about Navi? */

      /* in cases like waves_per_workgroup=3 or lds=65536 and
       * waves_per_workgroup=1, we want the maximum possible number of waves per
       * SIMD and not the minimum. so DIV_ROUND_UP is used */
      program->max_waves = std::min<uint16_t>(program->max_waves, DIV_ROUND_UP(workgroups_per_cu_wgp * waves_per_workgroup, simd_per_cu_wgp));

      /* incorporate max_waves and calculate max_reg_demand */
      program->num_waves = std::min<uint16_t>(program->num_waves, program->max_waves);
      program->max_reg_demand.vgpr = get_addr_vgpr_from_waves(program, program->num_waves);
      program->max_reg_demand.sgpr = get_addr_sgpr_from_waves(program, program->num_waves);
   }
}

live live_var_analysis(Program* program)
{
   live result;
   result.live_out.resize(program->blocks.size());
   result.register_demand.resize(program->blocks.size());
   std::set<unsigned> worklist;
   std::vector<uint16_t> phi_sgpr_ops(program->blocks.size());
   RegisterDemand new_demand;

   program->needs_vcc = false;

   /* this implementation assumes that the block idx corresponds to the block's position in program->blocks vector */
   for (Block& block : program->blocks)
      worklist.insert(block.index);
   while (!worklist.empty()) {
      std::set<unsigned>::reverse_iterator b_it = worklist.rbegin();
      unsigned block_idx = *b_it;
      worklist.erase(block_idx);
      process_live_temps_per_block(program, result, &program->blocks[block_idx], worklist, phi_sgpr_ops);
      new_demand.update(program->blocks[block_idx].register_demand);
   }

   /* calculate the program's register demand and number of waves */
   update_vgpr_sgpr_demand(program, new_demand);

   return result;
}

}

