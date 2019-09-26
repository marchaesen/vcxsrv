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

#include <set>
#include <vector>

#include "vulkan/radv_shader.h"

namespace aco {
namespace {

void process_live_temps_per_block(Program *program, live& lives, Block* block,
                                  std::set<unsigned>& worklist, std::vector<uint16_t>& phi_sgpr_ops)
{
   std::vector<RegisterDemand>& register_demand = lives.register_demand[block->index];
   RegisterDemand new_demand;

   register_demand.resize(block->instructions.size());
   block->register_demand = RegisterDemand();

   std::set<Temp> live_sgprs;
   std::set<Temp> live_vgprs;

   /* add the live_out_exec to live */
   bool exec_live = false;
   if (block->live_out_exec != Temp()) {
      live_sgprs.insert(block->live_out_exec);
      new_demand.sgpr += 2;
      exec_live = true;
   }

   /* split the live-outs from this block into the temporary sets */
   std::vector<std::set<Temp>>& live_temps = lives.live_out;
   for (const Temp temp : live_temps[block->index]) {
      const bool inserted = temp.is_linear()
                          ? live_sgprs.insert(temp).second
                          : live_vgprs.insert(temp).second;
      if (inserted) {
         new_demand += temp;
      }
   }
   new_demand.sgpr -= phi_sgpr_ops[block->index];

   /* traverse the instructions backwards */
   for (int idx = block->instructions.size() -1; idx >= 0; idx--)
   {
      /* substract the 2 sgprs from exec */
      if (exec_live)
         assert(new_demand.sgpr >= 2);
      register_demand[idx] = RegisterDemand(new_demand.vgpr, new_demand.sgpr - (exec_live ? 2 : 0));

      Instruction *insn = block->instructions[idx].get();
      /* KILL */
      for (Definition& definition : insn->definitions) {
         if (!definition.isTemp()) {
            continue;
         }

         const Temp temp = definition.getTemp();
         size_t n = 0;
         if (temp.is_linear())
            n = live_sgprs.erase(temp);
         else
            n = live_vgprs.erase(temp);

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
      if (insn->opcode == aco_opcode::p_phi ||
          insn->opcode == aco_opcode::p_linear_phi) {
         /* directly insert into the predecessors live-out set */
         std::vector<unsigned>& preds = insn->opcode == aco_opcode::p_phi
                                      ? block->logical_preds
                                      : block->linear_preds;
         for (unsigned i = 0; i < preds.size(); ++i)
         {
            Operand &operand = insn->operands[i];
            if (!operand.isTemp()) {
               continue;
            }
            /* check if we changed an already processed block */
            const bool inserted = live_temps[preds[i]].insert(operand.getTemp()).second;
            if (inserted) {
               operand.setFirstKill(true);
               worklist.insert(preds[i]);
               if (insn->opcode == aco_opcode::p_phi && operand.getTemp().type() == RegType::sgpr)
                  phi_sgpr_ops[preds[i]] += operand.size();
            }
         }
      } else if (insn->opcode == aco_opcode::p_logical_end) {
         new_demand.sgpr += phi_sgpr_ops[block->index];
      } else {
         for (unsigned i = 0; i < insn->operands.size(); ++i)
         {
            Operand& operand = insn->operands[i];
            if (!operand.isTemp()) {
               continue;
            }
            const Temp temp = operand.getTemp();
            const bool inserted = temp.is_linear()
                                ? live_sgprs.insert(temp).second
                                : live_vgprs.insert(temp).second;
            if (inserted) {
               operand.setFirstKill(true);
               for (unsigned j = i + 1; j < insn->operands.size(); ++j) {
                  if (insn->operands[j].isTemp() && insn->operands[j].tempId() == operand.tempId()) {
                     insn->operands[j].setFirstKill(false);
                     insn->operands[j].setKill(true);
                  }
               }
               new_demand += temp;
            } else {
               operand.setKill(false);
            }

            if (operand.isFixed() && operand.physReg() == exec)
               exec_live = true;
         }
      }

      block->register_demand.update(register_demand[idx]);
   }

   /* now, we have the live-in sets and need to merge them into the live-out sets */
   for (unsigned pred_idx : block->logical_preds) {
      for (Temp vgpr : live_vgprs) {
         auto it = live_temps[pred_idx].insert(vgpr);
         if (it.second)
            worklist.insert(pred_idx);
      }
   }

   for (unsigned pred_idx : block->linear_preds) {
      for (Temp sgpr : live_sgprs) {
         auto it = live_temps[pred_idx].insert(sgpr);
         if (it.second)
            worklist.insert(pred_idx);
      }
   }

   if (!(block->index != 0 || (live_vgprs.empty() && live_sgprs.empty()))) {
      aco_print_program(program, stderr);
      fprintf(stderr, "These temporaries are never defined or are defined after use:\n");
      for (Temp vgpr : live_vgprs)
         fprintf(stderr, "%%%d\n", vgpr.id());
      for (Temp sgpr : live_sgprs)
         fprintf(stderr, "%%%d\n", sgpr.id());
      abort();
   }

   assert(block->index != 0 || new_demand == RegisterDemand());
}
} /* end namespace */

void update_vgpr_sgpr_demand(Program* program, const RegisterDemand new_demand)
{
   // TODO: also take shared mem into account
   const int16_t total_sgpr_regs = program->chip_class >= GFX8 ? 800 : 512;
   const int16_t max_addressible_sgpr = program->sgpr_limit;
   /* VGPRs are allocated in chunks of 4 */
   const int16_t rounded_vgpr_demand = std::max<int16_t>(4, (new_demand.vgpr + 3) & ~3);
   /* SGPRs are allocated in chunks of 16 between 8 and 104. VCC occupies the last 2 registers */
   const int16_t rounded_sgpr_demand = std::min(std::max<int16_t>(8, (new_demand.sgpr + 2 + 7) & ~7), max_addressible_sgpr);
   /* this won't compile, register pressure reduction necessary */
   if (new_demand.vgpr > 256 || new_demand.sgpr > max_addressible_sgpr) {
      program->num_waves = 0;
      program->max_reg_demand = new_demand;
   } else {
      program->num_waves = std::min<uint16_t>(10,
                                              std::min<uint16_t>(256 / rounded_vgpr_demand,
                                                                 total_sgpr_regs / rounded_sgpr_demand));

      program->max_reg_demand = {  int16_t((256 / program->num_waves) & ~3), std::min<int16_t>(((total_sgpr_regs / program->num_waves) & ~7) - 2, max_addressible_sgpr)};
   }
}

live live_var_analysis(Program* program,
                       const struct radv_nir_compiler_options *options)
{
   live result;
   result.live_out.resize(program->blocks.size());
   result.register_demand.resize(program->blocks.size());
   std::set<unsigned> worklist;
   std::vector<uint16_t> phi_sgpr_ops(program->blocks.size());
   RegisterDemand new_demand;

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

