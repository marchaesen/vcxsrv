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

#include <map>
#include "aco_ir.h"
#include "aco_builder.h"

/*
 * Implements an algorithm to lower to Concentional SSA Form (CSSA).
 * After "Revisiting Out-of-SSA Translation for Correctness, CodeQuality, and Efficiency"
 * by B. Boissinot, A. Darte, F. Rastello, B. Dupont de Dinechin, C. Guillon,
 *
 * By lowering the IR to CSSA, the insertion of parallelcopies is separated from
 * the register coalescing problem. Additionally, correctness is ensured w.r.t. spilling.
 * The algorithm tries to find beneficial insertion points by checking if a basic block
 * is empty and if the variable already has a new definition in a dominating block.
 */


namespace aco {
namespace {

typedef std::map<uint32_t, std::vector<std::pair<Definition, Operand>>> phi_info;

struct cssa_ctx {
   Program* program;
   live& live_vars;
   phi_info logical_phi_info;
   phi_info linear_phi_info;

   cssa_ctx(Program* program, live& live_vars) : program(program), live_vars(live_vars) {}
};

bool collect_phi_info(cssa_ctx& ctx)
{
   bool progress = false;
   for (Block& block : ctx.program->blocks) {
      for (aco_ptr<Instruction>& phi : block.instructions) {
         bool is_logical;
         if (phi->opcode == aco_opcode::p_phi)
            is_logical = true;
         else if (phi->opcode == aco_opcode::p_linear_phi)
            is_logical = false;
         else
            break;

         /* no CSSA for the exec mask as we don't spill it anyway */
         if (phi->definitions[0].isFixed() && phi->definitions[0].physReg() == exec)
            continue;
         std::vector<unsigned>& preds = is_logical ? block.logical_preds : block.linear_preds;

         /* collect definition's block per Operand */
         std::vector<unsigned> def_points(phi->operands.size());
         for (unsigned i = 0; i < phi->operands.size(); i++) {
            Operand& op = phi->operands[i];
            if (op.isUndefined()) {
               def_points[i] = preds[i];
            } else if (op.isConstant()) {
               /* in theory, we could insert the definition there... */
               def_points[i] = 0;
            } else {
               assert(op.isTemp());
               unsigned pred = preds[i];
               do {
                  def_points[i] = pred;
                  pred = is_logical ?
                         ctx.program->blocks[pred].logical_idom :
                         ctx.program->blocks[pred].linear_idom;
               } while (def_points[i] != pred &&
                        ctx.live_vars.live_out[pred].count(op.tempId()));
            }
         }

         /* check live-range intersections */
         for (unsigned i = 0; i < phi->operands.size(); i++) {
            Operand op = phi->operands[i];
            if (op.isUndefined())
               continue;
            /* check if the operand comes from the exec mask of a predecessor */
            if (op.isTemp() && op.getTemp() == ctx.program->blocks[preds[i]].live_out_exec)
               op.setFixed(exec);

            bool interferes = false;
            unsigned idom = is_logical ?
                            ctx.program->blocks[def_points[i]].logical_idom :
                            ctx.program->blocks[def_points[i]].linear_idom;
            /* live-through operands definitely interfere */
            if (op.isTemp() && !op.isKill()) {
               interferes = true;
            /* create copies for constants to ease spilling */
            } else if (op.isConstant()) {
               interferes = true;
            /* create copies for SGPR -> VGPR moves */
            } else if (op.regClass() != phi->definitions[0].regClass()) {
               interferes = true;
            /* operand might interfere with any phi-def*/
            } else if (def_points[i] == block.index) {
               interferes = true;
            /* operand might interfere with phi-def */
            } else if (ctx.live_vars.live_out[idom].count(phi->definitions[0].tempId())) {
               interferes = true;
            /* else check for interferences with other operands */
            } else {
               for (unsigned j = 0; !interferes && j < phi->operands.size(); j++) {
                  /* don't care about other register classes */
                  if (!phi->operands[j].isTemp() || phi->operands[j].regClass() != phi->definitions[0].regClass())
                     continue;
                  /* same operands cannot interfere */
                  if (op.getTemp() == phi->operands[j].getTemp())
                     continue;
                  /* if def_points[i] dominates any other def_point, assume they interfere.
                   * As live-through operands are checked above, only test up the current block. */
                  unsigned other_def_point = def_points[j];
                  while (def_points[i] < other_def_point && other_def_point != block.index)
                     other_def_point = is_logical ?
                                       ctx.program->blocks[other_def_point].logical_idom :
                                       ctx.program->blocks[other_def_point].linear_idom;
                  interferes = def_points[i] == other_def_point;
               }
            }

            if (!interferes)
               continue;

            progress = true;

            /* create new temporary and rename operands */
            Temp new_tmp = ctx.program->allocateTmp(phi->definitions[0].regClass());
            if (is_logical)
               ctx.logical_phi_info[preds[i]].emplace_back(Definition(new_tmp), op);
            else
               ctx.linear_phi_info[preds[i]].emplace_back(Definition(new_tmp), op);
            phi->operands[i] = Operand(new_tmp);
            phi->operands[i].setKill(true);
            def_points[i] = preds[i];
         }
      }
   }
   return progress;
}

void insert_parallelcopies(cssa_ctx& ctx)
{
   /* insert the parallelcopies from logical phis before p_logical_end */
   for (auto&& entry : ctx.logical_phi_info) {
      Block& block = ctx.program->blocks[entry.first];
      unsigned idx = block.instructions.size() - 1;
      while (block.instructions[idx]->opcode != aco_opcode::p_logical_end) {
         assert(idx > 0);
         idx--;
      }

      Builder bld(ctx.program);
      bld.reset(&block.instructions, std::next(block.instructions.begin(), idx));
      for (std::pair<Definition, Operand>& pair : entry.second)
         bld.pseudo(aco_opcode::p_parallelcopy, pair.first, pair.second);
   }

   /* insert parallelcopies for the linear phis at the end of blocks just before the branch */
   for (auto&& entry : ctx.linear_phi_info) {
      Block& block = ctx.program->blocks[entry.first];
      std::vector<aco_ptr<Instruction>>::iterator it = block.instructions.end();
      --it;
      assert((*it)->format == Format::PSEUDO_BRANCH);

      Builder bld(ctx.program);
      bld.reset(&block.instructions, it);
      for (std::pair<Definition, Operand>& pair : entry.second)
         bld.pseudo(aco_opcode::p_parallelcopy, pair.first, pair.second);
   }
}

} /* end namespace */


void lower_to_cssa(Program* program, live& live_vars)
{
   cssa_ctx ctx = {program, live_vars};
   /* collect information about all interfering phi operands */
   bool progress = collect_phi_info(ctx);

   if (!progress)
      return;

   insert_parallelcopies(ctx);

   /* update live variable information */
   live_vars = live_var_analysis(program);
}
}

