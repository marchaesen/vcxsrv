/*
 * Copyright © 2020 Valve Corporation
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
#include "util/crc32.h"

namespace aco {

/* sgpr_presched/vgpr_presched */
void collect_presched_stats(Program *program)
{
   RegisterDemand presched_demand;
   for (Block& block : program->blocks)
      presched_demand.update(block.register_demand);
   program->statistics[statistic_sgpr_presched] = presched_demand.sgpr;
   program->statistics[statistic_vgpr_presched] = presched_demand.vgpr;
}

/* instructions/branches/vmem_clauses/smem_clauses/cycles */
void collect_preasm_stats(Program *program)
{
   for (Block& block : program->blocks) {
      std::set<Temp> vmem_clause_res;
      std::set<Temp> smem_clause_res;

      program->statistics[statistic_instructions] += block.instructions.size();

      for (aco_ptr<Instruction>& instr : block.instructions) {
         if (instr->format == Format::SOPP && static_cast<SOPP_instruction*>(instr.get())->block != -1)
            program->statistics[statistic_branches]++;

         if (instr->opcode == aco_opcode::p_constaddr)
            program->statistics[statistic_instructions] += 2;

         if (instr->isVMEM() && !instr->operands.empty()) {
            vmem_clause_res.insert(instr->operands[0].getTemp());
         } else {
            program->statistics[statistic_vmem_clauses] += vmem_clause_res.size();
            vmem_clause_res.clear();
         }

         if (instr->format == Format::SMEM && !instr->operands.empty()) {
            if (instr->operands[0].size() == 2)
               smem_clause_res.insert(Temp(0, s2));
            else
               smem_clause_res.insert(instr->operands[0].getTemp());
         } else {
            program->statistics[statistic_smem_clauses] += smem_clause_res.size();
            smem_clause_res.clear();
          }

         /* TODO: this incorrectly assumes instructions always take 4 cycles */
         /* assume loops execute 4 times (TODO: it would be nice to be able to consider loop unrolling) */
         unsigned iter = 1 << (block.loop_nest_depth * 2);
         program->statistics[statistic_cycles] += 4 * iter;
      }

      program->statistics[statistic_vmem_clauses] += vmem_clause_res.size();
      program->statistics[statistic_smem_clauses] += smem_clause_res.size();
   }
}

void collect_postasm_stats(Program *program, const std::vector<uint32_t>& code)
{
   program->statistics[aco::statistic_hash] = util_hash_crc32(code.data(), code.size() * 4);
}

}
