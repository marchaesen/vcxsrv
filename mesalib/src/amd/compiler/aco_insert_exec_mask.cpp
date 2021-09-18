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

#include "aco_builder.h"
#include "aco_ir.h"

#include "util/u_math.h"

#include <set>
#include <vector>

namespace aco {

namespace {

enum WQMState : uint8_t {
   Unspecified = 0,
   Exact = 1 << 0,
   WQM = 1 << 1, /* with control flow applied */
   Preserve_WQM = 1 << 2,
   Exact_Branch = 1 << 3,
};

enum mask_type : uint8_t {
   mask_type_global = 1 << 0,
   mask_type_exact = 1 << 1,
   mask_type_wqm = 1 << 2,
   mask_type_loop = 1 << 3, /* active lanes of a loop */
};

struct wqm_ctx {
   Program* program;
   /* state for WQM propagation */
   std::set<unsigned> worklist;
   std::vector<uint16_t> defined_in;
   std::vector<bool> needs_wqm;
   std::vector<bool> branch_wqm; /* true if the branch condition in this block should be in wqm */
   wqm_ctx(Program* program_)
       : program(program_), defined_in(program->peekAllocationId(), 0xFFFF),
         needs_wqm(program->peekAllocationId()), branch_wqm(program->blocks.size())
   {
      for (unsigned i = 0; i < program->blocks.size(); i++)
         worklist.insert(i);
   }
};

struct loop_info {
   Block* loop_header;
   uint16_t num_exec_masks;
   uint8_t needs;
   bool has_divergent_break;
   bool has_divergent_continue;
   bool has_discard; /* has a discard or demote */
   loop_info(Block* b, uint16_t num, uint8_t needs_, bool breaks, bool cont, bool discard)
       : loop_header(b), num_exec_masks(num), needs(needs_), has_divergent_break(breaks),
         has_divergent_continue(cont), has_discard(discard)
   {}
};

struct block_info {
   std::vector<std::pair<Operand, uint8_t>>
      exec; /* Vector of exec masks. Either a temporary or const -1. */
   std::vector<WQMState> instr_needs;
   uint8_t block_needs;
   uint8_t ever_again_needs;
   bool logical_end_wqm;
   /* more... */
};

struct exec_ctx {
   Program* program;
   std::vector<block_info> info;
   std::vector<loop_info> loop;
   bool handle_wqm = false;
   exec_ctx(Program* program_) : program(program_), info(program->blocks.size()) {}
};

bool
needs_exact(aco_ptr<Instruction>& instr)
{
   if (instr->isMUBUF()) {
      return instr->mubuf().disable_wqm;
   } else if (instr->isMTBUF()) {
      return instr->mtbuf().disable_wqm;
   } else if (instr->isMIMG()) {
      return instr->mimg().disable_wqm;
   } else if (instr->isFlatLike()) {
      return instr->flatlike().disable_wqm;
   } else {
      return instr->isEXP();
   }
}

void
set_needs_wqm(wqm_ctx& ctx, Temp tmp)
{
   if (!ctx.needs_wqm[tmp.id()]) {
      ctx.needs_wqm[tmp.id()] = true;
      if (ctx.defined_in[tmp.id()] != 0xFFFF)
         ctx.worklist.insert(ctx.defined_in[tmp.id()]);
   }
}

void
mark_block_wqm(wqm_ctx& ctx, unsigned block_idx)
{
   if (ctx.branch_wqm[block_idx])
      return;

   ctx.branch_wqm[block_idx] = true;
   ctx.worklist.insert(block_idx);

   Block& block = ctx.program->blocks[block_idx];

   /* TODO: this sets more branch conditions to WQM than it needs to
    * it should be enough to stop at the "exec mask top level" */
   if (block.kind & block_kind_top_level)
      return;

   for (unsigned pred_idx : block.logical_preds)
      mark_block_wqm(ctx, pred_idx);
}

void
get_block_needs(wqm_ctx& ctx, exec_ctx& exec_ctx, Block* block)
{
   block_info& info = exec_ctx.info[block->index];

   std::vector<WQMState> instr_needs(block->instructions.size());

   for (int i = block->instructions.size() - 1; i >= 0; --i) {
      aco_ptr<Instruction>& instr = block->instructions[i];

      WQMState needs = needs_exact(instr) ? Exact : Unspecified;
      bool propagate_wqm =
         instr->opcode == aco_opcode::p_wqm || instr->opcode == aco_opcode::p_as_uniform;
      bool preserve_wqm = instr->opcode == aco_opcode::p_discard_if;
      bool pred_by_exec = needs_exec_mask(instr.get());
      for (const Definition& definition : instr->definitions) {
         if (!definition.isTemp())
            continue;
         const unsigned def = definition.tempId();
         ctx.defined_in[def] = block->index;
         if (needs == Unspecified && ctx.needs_wqm[def]) {
            needs = pred_by_exec ? WQM : Unspecified;
            propagate_wqm = true;
         }
      }

      if (instr->isBranch() && ctx.branch_wqm[block->index]) {
         assert(!(info.block_needs & Exact_Branch));
         needs = WQM;
         propagate_wqm = true;
      }

      if (propagate_wqm) {
         for (const Operand& op : instr->operands) {
            if (op.isTemp()) {
               set_needs_wqm(ctx, op.getTemp());
            }
         }
      } else if (preserve_wqm && info.block_needs & WQM) {
         needs = Preserve_WQM;
      }

      /* ensure the condition controlling the control flow for this phi is in WQM */
      if (needs == WQM && instr->opcode == aco_opcode::p_phi) {
         for (unsigned pred_idx : block->logical_preds) {
            mark_block_wqm(ctx, pred_idx);
            exec_ctx.info[pred_idx].logical_end_wqm = true;
            ctx.worklist.insert(pred_idx);
         }
      }

      if ((instr->opcode == aco_opcode::p_logical_end && info.logical_end_wqm) ||
          instr->opcode == aco_opcode::p_wqm) {
         assert(needs != Exact);
         needs = WQM;
      }

      instr_needs[i] = needs;
      info.block_needs |= needs;
   }

   info.instr_needs = instr_needs;

   /* for "if (<cond>) <wqm code>" or "while (<cond>) <wqm code>",
    * <cond> should be computed in WQM */
   if (info.block_needs & WQM && !(block->kind & block_kind_top_level)) {
      for (unsigned pred_idx : block->logical_preds)
         mark_block_wqm(ctx, pred_idx);
   }
}

/* If an outer loop needs WQM but a nested loop does not, we have to ensure that
 * the nested loop is done in WQM so that the exec is not empty upon entering
 * the nested loop.
 *
 * TODO: This could be fixed with slightly better code (for loops with divergent
 * breaks, which might benefit from being in exact) by adding Exact_Branch to a
 * divergent branch surrounding the nested loop, if such a branch exists.
 */
void
handle_wqm_loops(wqm_ctx& ctx, exec_ctx& exec_ctx, unsigned preheader)
{
   for (unsigned idx = preheader + 1; idx < exec_ctx.program->blocks.size(); idx++) {
      Block& block = exec_ctx.program->blocks[idx];
      if (block.kind & block_kind_break)
         mark_block_wqm(ctx, idx);

      if ((block.kind & block_kind_loop_exit) && block.loop_nest_depth == 0)
         break;
   }
}

/* If an outer loop and it's nested loops does not need WQM,
 * add_branch_code() will ensure that it enters in Exact. We have to
 * ensure that the exact exec mask is not empty by adding Exact_Branch to
 * the outer divergent branch.
 */
void
handle_exact_loops(wqm_ctx& ctx, exec_ctx& exec_ctx, unsigned preheader)
{
   assert(exec_ctx.program->blocks[preheader + 1].kind & block_kind_loop_header);

   int parent_branch = preheader;
   unsigned rel_branch_depth = 0;
   for (; parent_branch >= 0; parent_branch--) {
      Block& branch = exec_ctx.program->blocks[parent_branch];
      if (branch.kind & block_kind_branch) {
         if (rel_branch_depth == 0)
            break;
         rel_branch_depth--;
      }

      /* top-level blocks should never have empty exact exec masks */
      if (branch.kind & block_kind_top_level)
         return;

      if (branch.kind & block_kind_merge)
         rel_branch_depth++;
   }
   assert(parent_branch >= 0);

   ASSERTED Block& branch = exec_ctx.program->blocks[parent_branch];
   assert(branch.kind & block_kind_branch);
   if (ctx.branch_wqm[parent_branch]) {
      /* The branch can't be done in Exact because some other blocks in it
       * are in WQM. So instead, ensure that the loop is done in WQM. */
      handle_wqm_loops(ctx, exec_ctx, preheader);
   } else {
      exec_ctx.info[parent_branch].block_needs |= Exact_Branch;
   }
}

void
calculate_wqm_needs(exec_ctx& exec_ctx)
{
   wqm_ctx ctx(exec_ctx.program);

   while (!ctx.worklist.empty()) {
      unsigned block_index = *std::prev(ctx.worklist.end());
      ctx.worklist.erase(std::prev(ctx.worklist.end()));

      Block& block = exec_ctx.program->blocks[block_index];
      get_block_needs(ctx, exec_ctx, &block);

      /* handle_exact_loops() needs information on outer branches, so don't
       * handle loops until a top-level block.
       */
      if (block.kind & block_kind_top_level && block.index != exec_ctx.program->blocks.size() - 1) {
         unsigned preheader = block.index;
         do {
            Block& preheader_block = exec_ctx.program->blocks[preheader];
            if ((preheader_block.kind & block_kind_loop_preheader) &&
                preheader_block.loop_nest_depth == 0) {
               /* If the loop or a nested loop needs WQM, branch_wqm will be true for the
                * preheader.
                */
               if (ctx.branch_wqm[preheader])
                  handle_wqm_loops(ctx, exec_ctx, preheader);
               else
                  handle_exact_loops(ctx, exec_ctx, preheader);
            }
            preheader++;
         } while (!(exec_ctx.program->blocks[preheader].kind & block_kind_top_level));
      }
   }

   uint8_t ever_again_needs = 0;
   for (int i = exec_ctx.program->blocks.size() - 1; i >= 0; i--) {
      exec_ctx.info[i].ever_again_needs = ever_again_needs;
      Block& block = exec_ctx.program->blocks[i];

      if (block.kind & block_kind_needs_lowering)
         exec_ctx.info[i].block_needs |= Exact;

      /* if discard is used somewhere in nested CF, we need to preserve the WQM mask */
      if ((block.kind & block_kind_discard || block.kind & block_kind_uses_discard_if) &&
          ever_again_needs & WQM)
         exec_ctx.info[i].block_needs |= Preserve_WQM;

      ever_again_needs |= exec_ctx.info[i].block_needs & ~Exact_Branch;
      if (block.kind & block_kind_discard || block.kind & block_kind_uses_discard_if ||
          block.kind & block_kind_uses_demote)
         ever_again_needs |= Exact;

      /* don't propagate WQM preservation further than the next top_level block */
      if (block.kind & block_kind_top_level)
         ever_again_needs &= ~Preserve_WQM;
      else
         exec_ctx.info[i].block_needs &= ~Preserve_WQM;
   }
   exec_ctx.handle_wqm = true;
}

Operand
get_exec_op(Operand t)
{
   if (t.isUndefined())
      return Operand(exec, t.regClass());
   else
      return t;
}

void
transition_to_WQM(exec_ctx& ctx, Builder bld, unsigned idx)
{
   if (ctx.info[idx].exec.back().second & mask_type_wqm)
      return;
   if (ctx.info[idx].exec.back().second & mask_type_global) {
      Operand exec_mask = ctx.info[idx].exec.back().first;
      if (exec_mask.isUndefined()) {
         exec_mask = bld.pseudo(aco_opcode::p_parallelcopy, bld.def(bld.lm), Operand(exec, bld.lm));
         ctx.info[idx].exec.back().first = exec_mask;
      }

      exec_mask = bld.sop1(Builder::s_wqm, Definition(exec, bld.lm), bld.def(s1, scc),
                           get_exec_op(exec_mask));
      ctx.info[idx].exec.emplace_back(exec_mask, mask_type_global | mask_type_wqm);
      return;
   }
   /* otherwise, the WQM mask should be one below the current mask */
   ctx.info[idx].exec.pop_back();
   assert(ctx.info[idx].exec.back().second & mask_type_wqm);
   assert(ctx.info[idx].exec.back().first.size() == bld.lm.size());
   assert(ctx.info[idx].exec.back().first.isTemp());
   ctx.info[idx].exec.back().first = bld.pseudo(
      aco_opcode::p_parallelcopy, Definition(exec, bld.lm), ctx.info[idx].exec.back().first);
}

void
transition_to_Exact(exec_ctx& ctx, Builder bld, unsigned idx)
{
   if (ctx.info[idx].exec.back().second & mask_type_exact)
      return;
   /* We can't remove the loop exec mask, because that can cause exec.size() to
    * be less than num_exec_masks. The loop exec mask also needs to be kept
    * around for various uses. */
   if ((ctx.info[idx].exec.back().second & mask_type_global) &&
       !(ctx.info[idx].exec.back().second & mask_type_loop)) {
      ctx.info[idx].exec.pop_back();
      assert(ctx.info[idx].exec.back().second & mask_type_exact);
      assert(ctx.info[idx].exec.back().first.size() == bld.lm.size());
      assert(ctx.info[idx].exec.back().first.isTemp());
      ctx.info[idx].exec.back().first = bld.pseudo(
         aco_opcode::p_parallelcopy, Definition(exec, bld.lm), ctx.info[idx].exec.back().first);
      return;
   }
   /* otherwise, we create an exact mask and push to the stack */
   Operand wqm = ctx.info[idx].exec.back().first;
   if (wqm.isUndefined()) {
      wqm = bld.sop1(Builder::s_and_saveexec, bld.def(bld.lm), bld.def(s1, scc),
                     Definition(exec, bld.lm), ctx.info[idx].exec[0].first, Operand(exec, bld.lm));
   } else {
      bld.sop2(Builder::s_and, Definition(exec, bld.lm), bld.def(s1, scc),
               ctx.info[idx].exec[0].first, wqm);
   }
   ctx.info[idx].exec.back().first = Operand(wqm);
   ctx.info[idx].exec.emplace_back(Operand(bld.lm), mask_type_exact);
}

unsigned
add_coupling_code(exec_ctx& ctx, Block* block, std::vector<aco_ptr<Instruction>>& instructions)
{
   unsigned idx = block->index;
   Builder bld(ctx.program, &instructions);
   std::vector<unsigned>& preds = block->linear_preds;

   /* start block */
   if (idx == 0) {
      aco_ptr<Instruction>& startpgm = block->instructions[0];
      assert(startpgm->opcode == aco_opcode::p_startpgm);
      bld.insert(std::move(startpgm));

      Operand start_exec(bld.lm);

      /* exec seems to need to be manually initialized with combined shaders */
      if (ctx.program->stage.num_sw_stages() > 1 || ctx.program->stage.hw == HWStage::NGG) {
         start_exec = Operand::c32_or_c64(-1u, bld.lm == s2);
         bld.copy(Definition(exec, bld.lm), start_exec);
      }

      if (ctx.handle_wqm) {
         ctx.info[0].exec.emplace_back(start_exec, mask_type_global | mask_type_exact);
         /* if this block only needs WQM, initialize already */
         if (ctx.info[0].block_needs == WQM)
            transition_to_WQM(ctx, bld, 0);
      } else {
         uint8_t mask = mask_type_global;
         if (ctx.program->needs_wqm) {
            bld.sop1(Builder::s_wqm, Definition(exec, bld.lm), bld.def(s1, scc),
                     Operand(exec, bld.lm));
            mask |= mask_type_wqm;
         } else {
            mask |= mask_type_exact;
         }
         ctx.info[0].exec.emplace_back(start_exec, mask);
      }

      return 1;
   }

   /* loop entry block */
   if (block->kind & block_kind_loop_header) {
      assert(preds[0] == idx - 1);
      ctx.info[idx].exec = ctx.info[idx - 1].exec;
      loop_info& info = ctx.loop.back();
      while (ctx.info[idx].exec.size() > info.num_exec_masks)
         ctx.info[idx].exec.pop_back();

      /* create ssa names for outer exec masks */
      if (info.has_discard) {
         aco_ptr<Pseudo_instruction> phi;
         for (int i = 0; i < info.num_exec_masks - 1; i++) {
            phi.reset(create_instruction<Pseudo_instruction>(aco_opcode::p_linear_phi,
                                                             Format::PSEUDO, preds.size(), 1));
            phi->definitions[0] = bld.def(bld.lm);
            phi->operands[0] = get_exec_op(ctx.info[preds[0]].exec[i].first);
            ctx.info[idx].exec[i].first = bld.insert(std::move(phi));
         }
      }

      /* create ssa name for restore mask */
      if (info.has_divergent_break) {
         /* this phi might be trivial but ensures a parallelcopy on the loop header */
         aco_ptr<Pseudo_instruction> phi{create_instruction<Pseudo_instruction>(
            aco_opcode::p_linear_phi, Format::PSEUDO, preds.size(), 1)};
         phi->definitions[0] = bld.def(bld.lm);
         phi->operands[0] = get_exec_op(ctx.info[preds[0]].exec[info.num_exec_masks - 1].first);
         ctx.info[idx].exec.back().first = bld.insert(std::move(phi));
      }

      /* create ssa name for loop active mask */
      aco_ptr<Pseudo_instruction> phi{create_instruction<Pseudo_instruction>(
         aco_opcode::p_linear_phi, Format::PSEUDO, preds.size(), 1)};
      if (info.has_divergent_continue)
         phi->definitions[0] = bld.def(bld.lm);
      else
         phi->definitions[0] = Definition(exec, bld.lm);
      phi->operands[0] = get_exec_op(ctx.info[preds[0]].exec.back().first);
      Temp loop_active = bld.insert(std::move(phi));

      if (info.has_divergent_break) {
         uint8_t mask_type =
            (ctx.info[idx].exec.back().second & (mask_type_wqm | mask_type_exact)) | mask_type_loop;
         ctx.info[idx].exec.emplace_back(loop_active, mask_type);
      } else {
         ctx.info[idx].exec.back().first = Operand(loop_active);
         ctx.info[idx].exec.back().second |= mask_type_loop;
      }

      /* create a parallelcopy to move the active mask to exec */
      unsigned i = 0;
      if (info.has_divergent_continue) {
         while (block->instructions[i]->opcode != aco_opcode::p_logical_start) {
            bld.insert(std::move(block->instructions[i]));
            i++;
         }
         uint8_t mask_type = ctx.info[idx].exec.back().second & (mask_type_wqm | mask_type_exact);
         assert(ctx.info[idx].exec.back().first.size() == bld.lm.size());
         ctx.info[idx].exec.emplace_back(
            bld.pseudo(aco_opcode::p_parallelcopy, Definition(exec, bld.lm),
                       ctx.info[idx].exec.back().first),
            mask_type);
      }

      return i;
   }

   /* loop exit block */
   if (block->kind & block_kind_loop_exit) {
      Block* header = ctx.loop.back().loop_header;
      loop_info& info = ctx.loop.back();

      for (ASSERTED unsigned pred : preds)
         assert(ctx.info[pred].exec.size() >= info.num_exec_masks);

      /* fill the loop header phis */
      std::vector<unsigned>& header_preds = header->linear_preds;
      int instr_idx = 0;
      if (info.has_discard) {
         while (instr_idx < info.num_exec_masks - 1) {
            aco_ptr<Instruction>& phi = header->instructions[instr_idx];
            assert(phi->opcode == aco_opcode::p_linear_phi);
            for (unsigned i = 1; i < phi->operands.size(); i++)
               phi->operands[i] = get_exec_op(ctx.info[header_preds[i]].exec[instr_idx].first);
            instr_idx++;
         }
      }

      {
         aco_ptr<Instruction>& phi = header->instructions[instr_idx++];
         assert(phi->opcode == aco_opcode::p_linear_phi);
         for (unsigned i = 1; i < phi->operands.size(); i++)
            phi->operands[i] =
               get_exec_op(ctx.info[header_preds[i]].exec[info.num_exec_masks - 1].first);
      }

      if (info.has_divergent_break) {
         aco_ptr<Instruction>& phi = header->instructions[instr_idx];
         assert(phi->opcode == aco_opcode::p_linear_phi);
         for (unsigned i = 1; i < phi->operands.size(); i++)
            phi->operands[i] =
               get_exec_op(ctx.info[header_preds[i]].exec[info.num_exec_masks].first);
      }

      assert(!(block->kind & block_kind_top_level) || info.num_exec_masks <= 2);

      /* create the loop exit phis if not trivial */
      for (unsigned exec_idx = 0; exec_idx < info.num_exec_masks; exec_idx++) {
         Operand same = ctx.info[preds[0]].exec[exec_idx].first;
         uint8_t type = ctx.info[header_preds[0]].exec[exec_idx].second;
         bool trivial = true;

         for (unsigned i = 1; i < preds.size() && trivial; i++) {
            if (ctx.info[preds[i]].exec[exec_idx].first != same)
               trivial = false;
         }

         if (trivial) {
            ctx.info[idx].exec.emplace_back(same, type);
         } else {
            /* create phi for loop footer */
            aco_ptr<Pseudo_instruction> phi{create_instruction<Pseudo_instruction>(
               aco_opcode::p_linear_phi, Format::PSEUDO, preds.size(), 1)};
            phi->definitions[0] = bld.def(bld.lm);
            if (exec_idx == info.num_exec_masks - 1u) {
               phi->definitions[0] = Definition(exec, bld.lm);
            }
            for (unsigned i = 0; i < phi->operands.size(); i++)
               phi->operands[i] = get_exec_op(ctx.info[preds[i]].exec[exec_idx].first);
            ctx.info[idx].exec.emplace_back(bld.insert(std::move(phi)), type);
         }
      }
      assert(ctx.info[idx].exec.size() == info.num_exec_masks);

      /* create a parallelcopy to move the live mask to exec */
      unsigned i = 0;
      while (block->instructions[i]->opcode != aco_opcode::p_logical_start) {
         bld.insert(std::move(block->instructions[i]));
         i++;
      }

      if (ctx.handle_wqm) {
         if (block->kind & block_kind_top_level && ctx.info[idx].exec.size() == 2) {
            if ((ctx.info[idx].block_needs | ctx.info[idx].ever_again_needs) == 0 ||
                (ctx.info[idx].block_needs | ctx.info[idx].ever_again_needs) == Exact) {
               ctx.info[idx].exec.back().second |= mask_type_global;
               transition_to_Exact(ctx, bld, idx);
               ctx.handle_wqm = false;
            }
         }
         if (ctx.info[idx].block_needs == WQM)
            transition_to_WQM(ctx, bld, idx);
         else if (ctx.info[idx].block_needs == Exact)
            transition_to_Exact(ctx, bld, idx);
      }

      assert(ctx.info[idx].exec.back().first.size() == bld.lm.size());
      if (get_exec_op(ctx.info[idx].exec.back().first).isTemp()) {
         /* move current exec mask into exec register */
         ctx.info[idx].exec.back().first = bld.pseudo(
            aco_opcode::p_parallelcopy, Definition(exec, bld.lm), ctx.info[idx].exec.back().first);
      }

      ctx.loop.pop_back();
      return i;
   }

   if (preds.size() == 1) {
      ctx.info[idx].exec = ctx.info[preds[0]].exec;
   } else {
      assert(preds.size() == 2);
      /* if one of the predecessors ends in exact mask, we pop it from stack */
      unsigned num_exec_masks =
         std::min(ctx.info[preds[0]].exec.size(), ctx.info[preds[1]].exec.size());

      if (block->kind & block_kind_merge)
         num_exec_masks--;
      if (block->kind & block_kind_top_level)
         num_exec_masks = std::min(num_exec_masks, 2u);

      /* create phis for diverged exec masks */
      for (unsigned i = 0; i < num_exec_masks; i++) {
         /* skip trivial phis */
         if (ctx.info[preds[0]].exec[i].first == ctx.info[preds[1]].exec[i].first) {
            Operand t = ctx.info[preds[0]].exec[i].first;
            /* discard/demote can change the state of the current exec mask */
            assert(!t.isTemp() ||
                   ctx.info[preds[0]].exec[i].second == ctx.info[preds[1]].exec[i].second);
            uint8_t mask = ctx.info[preds[0]].exec[i].second & ctx.info[preds[1]].exec[i].second;
            ctx.info[idx].exec.emplace_back(t, mask);
            continue;
         }

         bool in_exec = i == num_exec_masks - 1 && !(block->kind & block_kind_merge);
         Temp phi = bld.pseudo(aco_opcode::p_linear_phi,
                               in_exec ? Definition(exec, bld.lm) : bld.def(bld.lm),
                               get_exec_op(ctx.info[preds[0]].exec[i].first),
                               get_exec_op(ctx.info[preds[1]].exec[i].first));
         uint8_t mask_type = ctx.info[preds[0]].exec[i].second & ctx.info[preds[1]].exec[i].second;
         ctx.info[idx].exec.emplace_back(phi, mask_type);
      }
   }

   unsigned i = 0;
   while (block->instructions[i]->opcode == aco_opcode::p_phi ||
          block->instructions[i]->opcode == aco_opcode::p_linear_phi) {
      bld.insert(std::move(block->instructions[i]));
      i++;
   }

   /* try to satisfy the block's needs */
   if (ctx.handle_wqm) {
      if (block->kind & block_kind_top_level && ctx.info[idx].exec.size() == 2) {
         if ((ctx.info[idx].block_needs | ctx.info[idx].ever_again_needs) == 0 ||
             (ctx.info[idx].block_needs | ctx.info[idx].ever_again_needs) == Exact) {
            ctx.info[idx].exec.back().second |= mask_type_global;
            transition_to_Exact(ctx, bld, idx);
            ctx.handle_wqm = false;
         }
      }
      if (ctx.info[idx].block_needs == WQM)
         transition_to_WQM(ctx, bld, idx);
      else if (ctx.info[idx].block_needs == Exact)
         transition_to_Exact(ctx, bld, idx);
   }

   if (block->kind & block_kind_merge && !ctx.info[idx].exec.back().first.isUndefined()) {
      Operand restore = ctx.info[idx].exec.back().first;
      assert(restore.size() == bld.lm.size());
      bld.pseudo(aco_opcode::p_parallelcopy, Definition(exec, bld.lm), restore);
      if (!restore.isConstant())
         ctx.info[idx].exec.back().first = Operand(bld.lm);
   }

   return i;
}

void
process_instructions(exec_ctx& ctx, Block* block, std::vector<aco_ptr<Instruction>>& instructions,
                     unsigned idx)
{
   WQMState state;
   if (ctx.info[block->index].exec.back().second & mask_type_wqm)
      state = WQM;
   else {
      assert(!ctx.handle_wqm || ctx.info[block->index].exec.back().second & mask_type_exact);
      state = Exact;
   }

   /* if the block doesn't need both, WQM and Exact, we can skip processing the instructions */
   bool process = (ctx.handle_wqm && (ctx.info[block->index].block_needs & state) !=
                                        (ctx.info[block->index].block_needs & (WQM | Exact))) ||
                  block->kind & block_kind_uses_discard_if ||
                  block->kind & block_kind_uses_demote || block->kind & block_kind_needs_lowering;
   if (!process) {
      std::vector<aco_ptr<Instruction>>::iterator it = std::next(block->instructions.begin(), idx);
      instructions.insert(instructions.end(),
                          std::move_iterator<std::vector<aco_ptr<Instruction>>::iterator>(it),
                          std::move_iterator<std::vector<aco_ptr<Instruction>>::iterator>(
                             block->instructions.end()));
      return;
   }

   Builder bld(ctx.program, &instructions);

   for (; idx < block->instructions.size(); idx++) {
      aco_ptr<Instruction> instr = std::move(block->instructions[idx]);

      WQMState needs = ctx.handle_wqm ? ctx.info[block->index].instr_needs[idx] : Unspecified;

      if (instr->opcode == aco_opcode::p_discard_if) {
         if (ctx.info[block->index].block_needs & Preserve_WQM) {
            assert(block->kind & block_kind_top_level);
            transition_to_WQM(ctx, bld, block->index);
            ctx.info[block->index].exec.back().second &= ~mask_type_global;
         }
         int num = ctx.info[block->index].exec.size();
         assert(num);

         /* discard from current exec */
         const Operand cond = instr->operands[0];
         Temp exit_cond = bld.sop2(Builder::s_andn2, Definition(exec, bld.lm), bld.def(s1, scc),
                                   Operand(exec, bld.lm), cond)
                             .def(1)
                             .getTemp();

         /* discard from inner to outer exec mask on stack */
         for (int i = num - 2; i >= 0; i--) {
            Instruction* andn2 = bld.sop2(Builder::s_andn2, bld.def(bld.lm), bld.def(s1, scc),
                                          ctx.info[block->index].exec[i].first, cond);
            ctx.info[block->index].exec[i].first = Operand(andn2->definitions[0].getTemp());
            exit_cond = andn2->definitions[1].getTemp();
         }

         instr->opcode = aco_opcode::p_exit_early_if;
         instr->operands[0] = bld.scc(exit_cond);
         assert(!ctx.handle_wqm || (ctx.info[block->index].exec[0].second & mask_type_wqm) == 0);

      } else if (needs == WQM && state != WQM) {
         transition_to_WQM(ctx, bld, block->index);
         state = WQM;
      } else if (needs == Exact && state != Exact) {
         transition_to_Exact(ctx, bld, block->index);
         state = Exact;
      }

      if (instr->opcode == aco_opcode::p_is_helper) {
         Definition dst = instr->definitions[0];
         assert(dst.size() == bld.lm.size());
         if (state == Exact) {
            instr.reset(create_instruction<SOP1_instruction>(bld.w64or32(Builder::s_mov),
                                                             Format::SOP1, 1, 1));
            instr->operands[0] = Operand::zero();
            instr->definitions[0] = dst;
         } else {
            std::pair<Operand, uint8_t>& exact_mask = ctx.info[block->index].exec[0];
            assert(exact_mask.second & mask_type_exact);

            instr.reset(create_instruction<SOP2_instruction>(bld.w64or32(Builder::s_andn2),
                                                             Format::SOP2, 2, 2));
            instr->operands[0] = Operand(exec, bld.lm); /* current exec */
            instr->operands[1] = Operand(exact_mask.first);
            instr->definitions[0] = dst;
            instr->definitions[1] = bld.def(s1, scc);
         }
      } else if (instr->opcode == aco_opcode::p_demote_to_helper) {
         /* turn demote into discard_if with only exact masks */
         assert((ctx.info[block->index].exec[0].second & (mask_type_exact | mask_type_global)) ==
                (mask_type_exact | mask_type_global));

         int num;
         Temp cond, exit_cond;
         if (instr->operands[0].isConstant()) {
            assert(instr->operands[0].constantValue() == -1u);
            /* transition to exact and set exec to zero */
            exit_cond = bld.tmp(s1);
            cond =
               bld.sop1(Builder::s_and_saveexec, bld.def(bld.lm), bld.scc(Definition(exit_cond)),
                        Definition(exec, bld.lm), Operand::zero(), Operand(exec, bld.lm));

            num = ctx.info[block->index].exec.size() - 2;
            if (!(ctx.info[block->index].exec.back().second & mask_type_exact)) {
               ctx.info[block->index].exec.back().first = Operand(cond);
               ctx.info[block->index].exec.emplace_back(Operand(bld.lm), mask_type_exact);
            }
         } else {
            /* demote_if: transition to exact */
            transition_to_Exact(ctx, bld, block->index);
            assert(instr->operands[0].isTemp());
            cond = instr->operands[0].getTemp();
            num = ctx.info[block->index].exec.size() - 1;
         }

         for (int i = num; i >= 0; i--) {
            if (ctx.info[block->index].exec[i].second & mask_type_exact) {
               Instruction* andn2 = bld.sop2(Builder::s_andn2, bld.def(bld.lm), bld.def(s1, scc),
                                             ctx.info[block->index].exec[i].first, cond);
               if (i == (int)ctx.info[block->index].exec.size() - 1) {
                  andn2->operands[0] = Operand(exec, bld.lm);
                  andn2->definitions[0] = Definition(exec, bld.lm);
               }

               ctx.info[block->index].exec[i].first = Operand(andn2->definitions[0].getTemp());
               exit_cond = andn2->definitions[1].getTemp();
            } else {
               assert(i != 0);
            }
         }
         instr->opcode = aco_opcode::p_exit_early_if;
         instr->operands[0] = bld.scc(exit_cond);
         state = Exact;

      } else if (instr->opcode == aco_opcode::p_elect) {
         bool all_lanes_enabled = ctx.info[block->index].exec.back().first.constantEquals(-1u);
         Definition dst = instr->definitions[0];

         if (all_lanes_enabled) {
            bld.copy(Definition(dst), Operand::c32_or_c64(1u, dst.size() == 2));
         } else {
            Temp first_lane_idx = bld.sop1(Builder::s_ff1_i32, bld.def(s1), Operand(exec, bld.lm));
            bld.sop2(Builder::s_lshl, Definition(dst), bld.def(s1, scc),
                     Operand::c32_or_c64(1u, dst.size() == 2), Operand(first_lane_idx));
         }
         instr.reset();
         continue;
      }

      bld.insert(std::move(instr));
   }
}

void
add_branch_code(exec_ctx& ctx, Block* block)
{
   unsigned idx = block->index;
   Builder bld(ctx.program, block);

   if (idx == ctx.program->blocks.size() - 1)
      return;

   /* try to disable wqm handling */
   if (ctx.handle_wqm && block->kind & block_kind_top_level) {
      if (ctx.info[idx].exec.size() == 3) {
         assert(ctx.info[idx].exec[1].second == mask_type_wqm);
         ctx.info[idx].exec.pop_back();
      }
      assert(ctx.info[idx].exec.size() <= 2);

      if (ctx.info[idx].ever_again_needs == 0 || ctx.info[idx].ever_again_needs == Exact) {
         /* transition to Exact */
         aco_ptr<Instruction> branch = std::move(block->instructions.back());
         block->instructions.pop_back();
         ctx.info[idx].exec.back().second |= mask_type_global;
         transition_to_Exact(ctx, bld, idx);
         bld.insert(std::move(branch));
         ctx.handle_wqm = false;

      } else if (ctx.info[idx].block_needs & Preserve_WQM) {
         /* transition to WQM and remove global flag */
         aco_ptr<Instruction> branch = std::move(block->instructions.back());
         block->instructions.pop_back();
         transition_to_WQM(ctx, bld, idx);
         ctx.info[idx].exec.back().second &= ~mask_type_global;
         bld.insert(std::move(branch));
      }
   }

   if (block->kind & block_kind_loop_preheader) {
      /* collect information about the succeeding loop */
      bool has_divergent_break = false;
      bool has_divergent_continue = false;
      bool has_discard = false;
      uint8_t needs = 0;
      unsigned loop_nest_depth = ctx.program->blocks[idx + 1].loop_nest_depth;

      for (unsigned i = idx + 1; ctx.program->blocks[i].loop_nest_depth >= loop_nest_depth; i++) {
         Block& loop_block = ctx.program->blocks[i];
         needs |= ctx.info[i].block_needs;

         if (loop_block.kind & block_kind_uses_discard_if || loop_block.kind & block_kind_discard ||
             loop_block.kind & block_kind_uses_demote)
            has_discard = true;
         if (loop_block.loop_nest_depth != loop_nest_depth)
            continue;

         if (loop_block.kind & block_kind_uniform)
            continue;
         else if (loop_block.kind & block_kind_break)
            has_divergent_break = true;
         else if (loop_block.kind & block_kind_continue)
            has_divergent_continue = true;
      }

      if (ctx.handle_wqm) {
         if (needs & WQM) {
            aco_ptr<Instruction> branch = std::move(block->instructions.back());
            block->instructions.pop_back();
            transition_to_WQM(ctx, bld, idx);
            bld.insert(std::move(branch));
         } else {
            aco_ptr<Instruction> branch = std::move(block->instructions.back());
            block->instructions.pop_back();
            transition_to_Exact(ctx, bld, idx);
            bld.insert(std::move(branch));
         }
      }

      unsigned num_exec_masks = ctx.info[idx].exec.size();
      if (block->kind & block_kind_top_level)
         num_exec_masks = std::min(num_exec_masks, 2u);

      ctx.loop.emplace_back(&ctx.program->blocks[block->linear_succs[0]], num_exec_masks, needs,
                            has_divergent_break, has_divergent_continue, has_discard);
   }

   /* For normal breaks, this is the exec mask. For discard+break, it's the
    * old exec mask before it was zero'd.
    */
   Operand break_cond = Operand(exec, bld.lm);

   if (block->kind & block_kind_discard) {

      assert(block->instructions.back()->isBranch());
      aco_ptr<Instruction> branch = std::move(block->instructions.back());
      block->instructions.pop_back();

      /* create a discard_if() instruction with the exec mask as condition */
      unsigned num = 0;
      if (ctx.loop.size()) {
         /* if we're in a loop, only discard from the outer exec masks */
         num = ctx.loop.back().num_exec_masks;
      } else {
         num = ctx.info[idx].exec.size() - 1;
      }

      Temp cond = bld.sop1(Builder::s_and_saveexec, bld.def(bld.lm), bld.def(s1, scc),
                           Definition(exec, bld.lm), Operand::zero(), Operand(exec, bld.lm));

      for (int i = num - 1; i >= 0; i--) {
         Instruction* andn2 = bld.sop2(Builder::s_andn2, bld.def(bld.lm), bld.def(s1, scc),
                                       get_exec_op(ctx.info[block->index].exec[i].first), cond);
         if (i == (int)ctx.info[idx].exec.size() - 1)
            andn2->definitions[0] = Definition(exec, bld.lm);
         if (i == 0)
            bld.pseudo(aco_opcode::p_exit_early_if, bld.scc(andn2->definitions[1].getTemp()));
         ctx.info[block->index].exec[i].first = Operand(andn2->definitions[0].getTemp());
      }
      assert(!ctx.handle_wqm || (ctx.info[block->index].exec[0].second & mask_type_wqm) == 0);

      break_cond = Operand(cond);
      bld.insert(std::move(branch));
      /* no return here as it can be followed by a divergent break */
   }

   if (block->kind & block_kind_continue_or_break) {
      assert(ctx.program->blocks[ctx.program->blocks[block->linear_succs[1]].linear_succs[0]].kind &
             block_kind_loop_header);
      assert(ctx.program->blocks[ctx.program->blocks[block->linear_succs[0]].linear_succs[0]].kind &
             block_kind_loop_exit);
      assert(block->instructions.back()->opcode == aco_opcode::p_branch);
      block->instructions.pop_back();

      bool need_parallelcopy = false;
      while (!(ctx.info[idx].exec.back().second & mask_type_loop)) {
         ctx.info[idx].exec.pop_back();
         need_parallelcopy = true;
      }

      if (need_parallelcopy)
         ctx.info[idx].exec.back().first = bld.pseudo(
            aco_opcode::p_parallelcopy, Definition(exec, bld.lm), ctx.info[idx].exec.back().first);
      bld.branch(aco_opcode::p_cbranch_nz, bld.hint_vcc(bld.def(s2)), Operand(exec, bld.lm),
                 block->linear_succs[1], block->linear_succs[0]);
      return;
   }

   if (block->kind & block_kind_uniform) {
      Pseudo_branch_instruction& branch = block->instructions.back()->branch();
      if (branch.opcode == aco_opcode::p_branch) {
         branch.target[0] = block->linear_succs[0];
      } else {
         branch.target[0] = block->linear_succs[1];
         branch.target[1] = block->linear_succs[0];
      }
      return;
   }

   if (block->kind & block_kind_branch) {

      if (ctx.handle_wqm && ctx.info[idx].exec.size() >= 2 &&
          ctx.info[idx].exec.back().second == mask_type_exact &&
          !(ctx.info[idx].block_needs & Exact_Branch) &&
          ctx.info[idx].exec[ctx.info[idx].exec.size() - 2].second & mask_type_wqm) {
         /* return to wqm before branching */
         ctx.info[idx].exec.pop_back();
      }

      // orig = s_and_saveexec_b64
      assert(block->linear_succs.size() == 2);
      assert(block->instructions.back()->opcode == aco_opcode::p_cbranch_z);
      Temp cond = block->instructions.back()->operands[0].getTemp();
      block->instructions.pop_back();

      if (ctx.info[idx].block_needs & Exact_Branch)
         transition_to_Exact(ctx, bld, idx);

      uint8_t mask_type = ctx.info[idx].exec.back().second & (mask_type_wqm | mask_type_exact);
      if (ctx.info[idx].exec.back().first.constantEquals(-1u)) {
         bld.pseudo(aco_opcode::p_parallelcopy, Definition(exec, bld.lm), cond);
      } else {
         Temp old_exec = bld.sop1(Builder::s_and_saveexec, bld.def(bld.lm), bld.def(s1, scc),
                                  Definition(exec, bld.lm), cond, Operand(exec, bld.lm));

         ctx.info[idx].exec.back().first = Operand(old_exec);
      }

      /* add next current exec to the stack */
      ctx.info[idx].exec.emplace_back(Operand(bld.lm), mask_type);

      bld.branch(aco_opcode::p_cbranch_z, bld.hint_vcc(bld.def(s2)), Operand(exec, bld.lm),
                 block->linear_succs[1], block->linear_succs[0]);
      return;
   }

   if (block->kind & block_kind_invert) {
      // exec = s_andn2_b64 (original_exec, exec)
      assert(block->instructions.back()->opcode == aco_opcode::p_branch);
      block->instructions.pop_back();
      assert(ctx.info[idx].exec.size() >= 2);
      Operand orig_exec = ctx.info[idx].exec[ctx.info[idx].exec.size() - 2].first;
      bld.sop2(Builder::s_andn2, Definition(exec, bld.lm), bld.def(s1, scc), orig_exec,
               Operand(exec, bld.lm));

      bld.branch(aco_opcode::p_cbranch_z, bld.hint_vcc(bld.def(s2)), Operand(exec, bld.lm),
                 block->linear_succs[1], block->linear_succs[0]);
      return;
   }

   if (block->kind & block_kind_break) {
      // loop_mask = s_andn2_b64 (loop_mask, exec)
      assert(block->instructions.back()->opcode == aco_opcode::p_branch);
      block->instructions.pop_back();

      Temp cond = Temp();
      for (int exec_idx = ctx.info[idx].exec.size() - 2; exec_idx >= 0; exec_idx--) {
         cond = bld.tmp(s1);
         Operand exec_mask = ctx.info[idx].exec[exec_idx].first;
         exec_mask = bld.sop2(Builder::s_andn2, bld.def(bld.lm), bld.scc(Definition(cond)),
                              exec_mask, break_cond);
         ctx.info[idx].exec[exec_idx].first = exec_mask;
         if (ctx.info[idx].exec[exec_idx].second & mask_type_loop)
            break;
      }

      /* check if the successor is the merge block, otherwise set exec to 0 */
      // TODO: this could be done better by directly branching to the merge block
      unsigned succ_idx = ctx.program->blocks[block->linear_succs[1]].linear_succs[0];
      Block& succ = ctx.program->blocks[succ_idx];
      if (!(succ.kind & block_kind_invert || succ.kind & block_kind_merge)) {
         bld.copy(Definition(exec, bld.lm), Operand::zero(bld.lm.bytes()));
      }

      bld.branch(aco_opcode::p_cbranch_nz, bld.hint_vcc(bld.def(s2)), bld.scc(cond),
                 block->linear_succs[1], block->linear_succs[0]);
      return;
   }

   if (block->kind & block_kind_continue) {
      assert(block->instructions.back()->opcode == aco_opcode::p_branch);
      block->instructions.pop_back();

      Temp cond = Temp();
      for (int exec_idx = ctx.info[idx].exec.size() - 2; exec_idx >= 0; exec_idx--) {
         if (ctx.info[idx].exec[exec_idx].second & mask_type_loop)
            break;
         cond = bld.tmp(s1);
         Operand exec_mask = ctx.info[idx].exec[exec_idx].first;
         exec_mask = bld.sop2(Builder::s_andn2, bld.def(bld.lm), bld.scc(Definition(cond)),
                              exec_mask, Operand(exec, bld.lm));
         ctx.info[idx].exec[exec_idx].first = exec_mask;
      }
      assert(cond != Temp());

      /* check if the successor is the merge block, otherwise set exec to 0 */
      // TODO: this could be done better by directly branching to the merge block
      unsigned succ_idx = ctx.program->blocks[block->linear_succs[1]].linear_succs[0];
      Block& succ = ctx.program->blocks[succ_idx];
      if (!(succ.kind & block_kind_invert || succ.kind & block_kind_merge)) {
         bld.copy(Definition(exec, bld.lm), Operand::zero(bld.lm.bytes()));
      }

      bld.branch(aco_opcode::p_cbranch_nz, bld.hint_vcc(bld.def(s2)), bld.scc(cond),
                 block->linear_succs[1], block->linear_succs[0]);
      return;
   }
}

void
process_block(exec_ctx& ctx, Block* block)
{
   std::vector<aco_ptr<Instruction>> instructions;
   instructions.reserve(block->instructions.size());

   unsigned idx = add_coupling_code(ctx, block, instructions);

   assert(block->index != ctx.program->blocks.size() - 1 ||
          ctx.info[block->index].exec.size() <= 2);

   process_instructions(ctx, block, instructions, idx);

   block->instructions = std::move(instructions);

   add_branch_code(ctx, block);
}

} /* end namespace */

void
insert_exec_mask(Program* program)
{
   exec_ctx ctx(program);

   if (program->needs_wqm && program->needs_exact)
      calculate_wqm_needs(ctx);

   for (Block& block : program->blocks)
      process_block(ctx, &block);
}

} // namespace aco
