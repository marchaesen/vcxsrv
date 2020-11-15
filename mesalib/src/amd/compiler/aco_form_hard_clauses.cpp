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
 */

#include "aco_ir.h"
#include "aco_builder.h"

namespace aco {
namespace {

/* there can also be LDS and VALU clauses, but I don't see how those are interesting */
enum clause_type
{
   clause_vmem,
   clause_flat,
   clause_smem,
   clause_other,
};

void emit_clause(Builder& bld, unsigned num_instrs, aco_ptr<Instruction> *instrs)
{
   unsigned start = 0;

   /* skip any stores at the start */
   for (; (start < num_instrs) && instrs[start]->definitions.empty(); start++)
      bld.insert(std::move(instrs[start]));

   unsigned end = start;
   for (; (end < num_instrs) && !instrs[end]->definitions.empty(); end++)
      ;
   unsigned clause_size = end - start;

   if (clause_size > 1)
      bld.sopp(aco_opcode::s_clause, -1, clause_size - 1);

   for (unsigned i = start; i < num_instrs; i++)
      bld.insert(std::move(instrs[i]));
}

} /* end namespace */

void form_hard_clauses(Program *program)
{
   for (Block& block : program->blocks) {
      unsigned num_instrs = 0;
      aco_ptr<Instruction> current_instrs[64];
      clause_type current_type = clause_other;
      unsigned current_resource = 0;

      std::vector<aco_ptr<Instruction>> new_instructions;
      new_instructions.reserve(block.instructions.size());
      Builder bld(program, &new_instructions);

      for (unsigned i = 0; i < block.instructions.size(); i++) {
         aco_ptr<Instruction>& instr = block.instructions[i];

         unsigned resource = 0;
         clause_type type = clause_other;
         if (instr->isVMEM() && !instr->operands.empty()) {
            resource = instr->operands[0].tempId();
            type = clause_vmem;
         } else if (instr->format == Format::SCRATCH || instr->format == Format::GLOBAL) {
            type = clause_vmem;
         } else if (instr->format == Format::FLAT) {
            type = clause_flat;
         } else if (instr->format == Format::SMEM && !instr->operands.empty()) {
            type = clause_smem;
            if (instr->operands[0].bytes() == 16)
               resource = instr->operands[0].tempId();
         }

         if (type != current_type || resource != current_resource || num_instrs == 64) {
            emit_clause(bld, num_instrs, current_instrs);
            num_instrs = 0;
            current_type = type;
            current_resource = resource;
         }

         if (type == clause_other) {
            bld.insert(std::move(instr));
            continue;
         }

         current_instrs[num_instrs++] = std::move(instr);
      }

      emit_clause(bld, num_instrs, current_instrs);

      block.instructions = std::move(new_instructions);
   }
}
}
