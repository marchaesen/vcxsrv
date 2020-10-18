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

#include "aco_ir.h"

#include <algorithm>

/*
 * Implements an analysis pass to determine the number of uses
 * for each SSA-definition.
 */

namespace aco {
namespace {

struct dce_ctx {
   int current_block;
   std::vector<uint16_t> uses;
   std::vector<std::vector<bool>> live;

   dce_ctx(Program* program) : current_block(program->blocks.size() - 1), uses(program->peekAllocationId())
   {
      live.reserve(program->blocks.size());
      for (Block& block : program->blocks)
         live.emplace_back(block.instructions.size());
   }
};

void process_block(dce_ctx& ctx, Block& block)
{
   std::vector<bool>& live = ctx.live[block.index];
   assert(live.size() == block.instructions.size());
   bool process_predecessors = false;
   for (int idx = block.instructions.size() - 1; idx >= 0; idx--) {
      if (live[idx])
         continue;

      aco_ptr<Instruction>& instr = block.instructions[idx];
      if (!is_dead(ctx.uses, instr.get())) {
         for (const Operand& op : instr->operands) {
            if (op.isTemp()) {
               if (ctx.uses[op.tempId()] == 0)
                  process_predecessors = true;
               ctx.uses[op.tempId()]++;
            }
         }
         live[idx] = true;
      }
   }

   if (process_predecessors) {
      for (unsigned pred_idx : block.linear_preds)
         ctx.current_block = std::max(ctx.current_block, (int) pred_idx);
   }
}

} /* end namespace */

bool is_dead(const std::vector<uint16_t>& uses, Instruction *instr)
{
   if (instr->definitions.empty() || instr->format == Format::PSEUDO_BRANCH)
      return false;
   if (std::any_of(instr->definitions.begin(), instr->definitions.end(),
          [&uses] (const Definition& def) { return uses[def.tempId()];}))
      return false;
   return !(get_sync_info(instr).semantics & (semantic_volatile | semantic_acqrel));
}

std::vector<uint16_t> dead_code_analysis(Program *program) {

   dce_ctx ctx(program);

   while (ctx.current_block >= 0) {
      unsigned next_block = ctx.current_block--;
      process_block(ctx, program->blocks[next_block]);
   }

   /* add one use to exec to prevent startpgm from being removed */
   aco_ptr<Instruction>& startpgm = program->blocks[0].instructions[0];
   assert(startpgm->opcode == aco_opcode::p_startpgm);
   ctx.uses[startpgm->definitions.back().tempId()]++;

   return ctx.uses;
}

}

