/*
 * Copyright © 2019 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "aco_builder.h"
#include "aco_ir.h"

#include <vector>

namespace aco {

namespace {

enum WQMState : uint8_t {
   Unspecified = 0,
   Exact,
   WQM, /* with control flow applied */
};

enum mask_type : uint8_t {
   mask_type_global = 1 << 0,
   mask_type_exact = 1 << 1,
   mask_type_wqm = 1 << 2,
   mask_type_loop = 1 << 3, /* active lanes of a loop */
};

struct loop_info {
   Block* loop_header;
   uint16_t num_exec_masks;
   bool has_divergent_break;
   bool has_divergent_continue;
   bool has_discard; /* has a discard or demote */
   loop_info(Block* b, uint16_t num, bool breaks, bool cont, bool discard)
       : loop_header(b), num_exec_masks(num), has_divergent_break(breaks),
         has_divergent_continue(cont), has_discard(discard)
   {}
};

struct exec_info {
   Operand op; /* Either a temporary, exec or const -1. */
   uint8_t type; /* enum mask_type */
   exec_info() = default;
   exec_info(const Operand& op_, const uint8_t& type_) : op(op_), type(type_) {}
};

struct block_info {
   std::vector<exec_info> exec;
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
      /* Require Exact for p_jump_to_epilog because if p_exit_early_if_not is
       * emitted inside the same block, the main FS will always jump to the PS
       * epilog without considering the exec mask.
       */
      return instr->isEXP() || instr->opcode == aco_opcode::p_jump_to_epilog ||
             instr->opcode == aco_opcode::p_dual_src_export_gfx11;
   }
}

WQMState
get_instr_needs(aco_ptr<Instruction>& instr)
{
   if (needs_exact(instr))
      return Exact;

   bool pred_by_exec = needs_exec_mask(instr.get()) || instr->opcode == aco_opcode::p_logical_end ||
                       instr->isBranch();

   return pred_by_exec ? WQM : Unspecified;
}

void
transition_to_WQM(exec_ctx& ctx, Builder bld, unsigned idx)
{
   if (ctx.info[idx].exec.back().type & mask_type_wqm)
      return;
   if (ctx.info[idx].exec.back().type & mask_type_global) {
      Operand exec_mask = ctx.info[idx].exec.back().op;
      if (exec_mask == Operand(exec, bld.lm))
         ctx.info[idx].exec.back().op = bld.copy(bld.def(bld.lm), exec_mask);

      bld.sop1(Builder::s_wqm, Definition(exec, bld.lm), bld.def(s1, scc), exec_mask);
      ctx.info[idx].exec.emplace_back(Operand(exec, bld.lm), mask_type_global | mask_type_wqm);
      return;
   }
   /* otherwise, the WQM mask should be one below the current mask */
   ctx.info[idx].exec.pop_back();
   assert(ctx.info[idx].exec.back().type & mask_type_wqm);
   assert(ctx.info[idx].exec.back().op.size() == bld.lm.size());
   assert(ctx.info[idx].exec.back().op.isTemp());
   bld.copy(Definition(exec, bld.lm), ctx.info[idx].exec.back().op);
}

void
transition_to_Exact(exec_ctx& ctx, Builder bld, unsigned idx)
{
   if (ctx.info[idx].exec.back().type & mask_type_exact)
      return;
   /* We can't remove the loop exec mask, because that can cause exec.size() to
    * be less than num_exec_masks. The loop exec mask also needs to be kept
    * around for various uses. */
   if ((ctx.info[idx].exec.back().type & mask_type_global) &&
       !(ctx.info[idx].exec.back().type & mask_type_loop)) {
      ctx.info[idx].exec.pop_back();
      assert(ctx.info[idx].exec.back().type & mask_type_exact);
      assert(ctx.info[idx].exec.back().op.size() == bld.lm.size());
      assert(ctx.info[idx].exec.back().op.isTemp());
      bld.copy(Definition(exec, bld.lm), ctx.info[idx].exec.back().op);
      return;
   }
   /* otherwise, we create an exact mask and push to the stack */
   Operand wqm = ctx.info[idx].exec.back().op;
   if (wqm == Operand(exec, bld.lm)) {
      wqm = bld.sop1(Builder::s_and_saveexec, bld.def(bld.lm), bld.def(s1, scc),
                     Definition(exec, bld.lm), ctx.info[idx].exec[0].op, Operand(exec, bld.lm));
   } else {
      bld.sop2(Builder::s_and, Definition(exec, bld.lm), bld.def(s1, scc), ctx.info[idx].exec[0].op,
               wqm);
   }
   ctx.info[idx].exec.back().op = Operand(wqm);
   ctx.info[idx].exec.emplace_back(Operand(exec, bld.lm), mask_type_exact);
}

unsigned
add_coupling_code(exec_ctx& ctx, Block* block, std::vector<aco_ptr<Instruction>>& instructions)
{
   unsigned idx = block->index;
   Builder bld(ctx.program, &instructions);
   Block::edge_vec& preds = block->linear_preds;
   bool restore_exec = false;

   /* start block */
   if (preds.empty()) {
      aco_ptr<Instruction>& startpgm = block->instructions[0];
      assert(startpgm->opcode == aco_opcode::p_startpgm);
      bld.insert(std::move(startpgm));

      unsigned count = 1;
      while (block->instructions[count]->opcode == aco_opcode::p_init_scratch ||
             block->instructions[count]->opcode == aco_opcode::s_setprio) {
         bld.insert(std::move(block->instructions[count]));
         count++;
      }

      Operand start_exec(exec, bld.lm);

      /* exec seems to need to be manually initialized with combined shaders */
      if (ctx.program->stage.num_sw_stages() > 1 ||
          ctx.program->stage.hw == AC_HW_NEXT_GEN_GEOMETRY_SHADER ||
          (ctx.program->stage.sw == SWStage::VS &&
           (ctx.program->stage.hw == AC_HW_HULL_SHADER ||
            ctx.program->stage.hw == AC_HW_LEGACY_GEOMETRY_SHADER)) ||
          (ctx.program->stage.sw == SWStage::TES &&
           ctx.program->stage.hw == AC_HW_LEGACY_GEOMETRY_SHADER)) {
         start_exec = Operand::c32_or_c64(-1u, bld.lm == s2);
         bld.copy(Definition(exec, bld.lm), start_exec);
      }

      /* EXEC is automatically initialized by the HW for compute shaders.
       * We know for sure exec is initially -1 when the shader always has full subgroups.
       */
      if (ctx.program->stage == compute_cs && ctx.program->info.cs.uses_full_subgroups)
         start_exec = Operand::c32_or_c64(-1u, bld.lm == s2);

      if (ctx.handle_wqm) {
         ctx.info[idx].exec.emplace_back(start_exec, mask_type_global | mask_type_exact);
         /* Initialize WQM already */
         transition_to_WQM(ctx, bld, idx);
      } else {
         uint8_t mask = mask_type_global;
         if (ctx.program->needs_wqm) {
            bld.sop1(Builder::s_wqm, Definition(exec, bld.lm), bld.def(s1, scc),
                     Operand(exec, bld.lm));
            mask |= mask_type_wqm;
         } else {
            mask |= mask_type_exact;
         }
         ctx.info[idx].exec.emplace_back(start_exec, mask);
      }

      return count;
   }

   /* loop entry block */
   if (block->kind & block_kind_loop_header) {
      assert(preds[0] == idx - 1);
      ctx.info[idx].exec = ctx.info[idx - 1].exec;
      loop_info& info = ctx.loop.back();
      assert(ctx.info[idx].exec.size() == info.num_exec_masks);

      /* create ssa names for outer exec masks */
      if (info.has_discard && preds.size() > 1) {
         aco_ptr<Instruction> phi;
         for (int i = 0; i < info.num_exec_masks - 1; i++) {
            phi.reset(
               create_instruction(aco_opcode::p_linear_phi, Format::PSEUDO, preds.size(), 1));
            phi->definitions[0] = bld.def(bld.lm);
            phi->operands[0] = ctx.info[preds[0]].exec[i].op;
            ctx.info[idx].exec[i].op = bld.insert(std::move(phi));
         }
      }

      ctx.info[idx].exec.back().type |= mask_type_loop;

      if (info.has_divergent_continue) {
         /* create ssa name for loop active mask */
         aco_ptr<Instruction> phi{
            create_instruction(aco_opcode::p_linear_phi, Format::PSEUDO, preds.size(), 1)};
         phi->definitions[0] = bld.def(bld.lm);
         phi->operands[0] = ctx.info[preds[0]].exec.back().op;
         ctx.info[idx].exec.back().op = bld.insert(std::move(phi));

         restore_exec = true;
         uint8_t mask_type = ctx.info[idx].exec.back().type & (mask_type_wqm | mask_type_exact);
         ctx.info[idx].exec.emplace_back(ctx.info[idx].exec.back().op, mask_type);
      }

   } else if (block->kind & block_kind_loop_exit) {
      Block* header = ctx.loop.back().loop_header;
      loop_info& info = ctx.loop.back();

      for (ASSERTED unsigned pred : preds)
         assert(ctx.info[pred].exec.size() >= info.num_exec_masks);

      /* fill the loop header phis */
      Block::edge_vec& header_preds = header->linear_preds;
      int instr_idx = 0;
      if (info.has_discard && header_preds.size() > 1) {
         while (instr_idx < info.num_exec_masks - 1) {
            aco_ptr<Instruction>& phi = header->instructions[instr_idx];
            assert(phi->opcode == aco_opcode::p_linear_phi);
            for (unsigned i = 1; i < phi->operands.size(); i++)
               phi->operands[i] = ctx.info[header_preds[i]].exec[instr_idx].op;
            instr_idx++;
         }
      }

      if (info.has_divergent_continue) {
         aco_ptr<Instruction>& phi = header->instructions[instr_idx++];
         assert(phi->opcode == aco_opcode::p_linear_phi);
         for (unsigned i = 1; i < phi->operands.size(); i++)
            phi->operands[i] = ctx.info[header_preds[i]].exec[info.num_exec_masks - 1].op;
         restore_exec = true;
      }

      if (info.has_divergent_break) {
         restore_exec = true;
         /* Drop the loop active mask. */
         info.num_exec_masks--;
      }
      assert(!(block->kind & block_kind_top_level) || info.num_exec_masks <= 2);

      /* create the loop exit phis if not trivial */
      for (unsigned exec_idx = 0; exec_idx < info.num_exec_masks; exec_idx++) {
         Operand same = ctx.info[preds[0]].exec[exec_idx].op;
         uint8_t type = ctx.info[header_preds[0]].exec[exec_idx].type;
         bool trivial = true;

         for (unsigned i = 1; i < preds.size() && trivial; i++) {
            if (ctx.info[preds[i]].exec[exec_idx].op != same)
               trivial = false;
         }

         if (trivial) {
            ctx.info[idx].exec.emplace_back(same, type);
         } else {
            /* create phi for loop footer */
            aco_ptr<Instruction> phi{
               create_instruction(aco_opcode::p_linear_phi, Format::PSEUDO, preds.size(), 1)};
            phi->definitions[0] = bld.def(bld.lm);
            for (unsigned i = 0; i < phi->operands.size(); i++)
               phi->operands[i] = ctx.info[preds[i]].exec[exec_idx].op;
            ctx.info[idx].exec.emplace_back(bld.insert(std::move(phi)), type);
         }
      }

      assert(ctx.info[idx].exec.size() == info.num_exec_masks);
      ctx.loop.pop_back();

   } else if (preds.size() == 1) {
      ctx.info[idx].exec = ctx.info[preds[0]].exec;

      /* After continue and break blocks, we implicitly set exec to zero.
       * This is so that parallelcopies can be inserted before the branch
       * without being affected by the changed exec mask.
       */
      if (ctx.info[idx].exec.back().op.constantEquals(0)) {
         assert(block->logical_succs.empty());
         /* Check whether the successor block already restores exec. */
         uint16_t block_kind = ctx.program->blocks[block->linear_succs[0]].kind;
         if (!(block_kind & (block_kind_loop_header | block_kind_loop_exit | block_kind_invert |
                             block_kind_merge))) {
            /* The successor does not restore exec. */
            restore_exec = true;
         }
      }
   } else {
      assert(preds.size() == 2);
      assert(ctx.info[preds[0]].exec.size() == ctx.info[preds[1]].exec.size());

      unsigned last = ctx.info[preds[0]].exec.size() - 1;

      /* create phis for diverged temporary exec masks */
      for (unsigned i = 0; i < last; i++) {
         /* skip trivial phis */
         if (ctx.info[preds[0]].exec[i].op == ctx.info[preds[1]].exec[i].op) {
            Operand op = ctx.info[preds[0]].exec[i].op;
            /* discard/demote can change the state of the current exec mask */
            assert(!op.isTemp() ||
                   ctx.info[preds[0]].exec[i].type == ctx.info[preds[1]].exec[i].type);
            uint8_t mask = ctx.info[preds[0]].exec[i].type & ctx.info[preds[1]].exec[i].type;
            ctx.info[idx].exec.emplace_back(op, mask);
            continue;
         }

         Operand phi = bld.pseudo(aco_opcode::p_linear_phi, bld.def(bld.lm),
                                  ctx.info[preds[0]].exec[i].op, ctx.info[preds[1]].exec[i].op);
         uint8_t mask_type = ctx.info[preds[0]].exec[i].type & ctx.info[preds[1]].exec[i].type;
         ctx.info[idx].exec.emplace_back(phi, mask_type);
      }

      if (block->kind & block_kind_merge) {
         restore_exec = true;
      } else {
         /* The last mask is already in exec. */
         Operand current_exec = Operand(exec, bld.lm);
         if (ctx.info[preds[0]].exec[last].op == ctx.info[preds[1]].exec[last].op) {
            current_exec = ctx.info[preds[0]].exec[last].op;
         }
         uint8_t mask_type =
            ctx.info[preds[0]].exec[last].type & ctx.info[preds[1]].exec[last].type;
         ctx.info[idx].exec.emplace_back(current_exec, mask_type);
      }
   }

   unsigned i = 0;
   while (block->instructions[i]->opcode == aco_opcode::p_phi ||
          block->instructions[i]->opcode == aco_opcode::p_linear_phi) {
      bld.insert(std::move(block->instructions[i]));
      i++;
   }

   if (ctx.handle_wqm) {
      /* End WQM handling if not needed anymore */
      if (block->kind & block_kind_top_level && ctx.info[idx].exec.size() == 2) {
         if (block->instructions[i]->opcode == aco_opcode::p_end_wqm) {
            ctx.info[idx].exec.back().type |= mask_type_global;
            transition_to_Exact(ctx, bld, idx);
            ctx.handle_wqm = false;
            restore_exec = false;
            i++;
         }
      }
   }

   /* restore exec mask after divergent control flow */
   if (restore_exec) {
      Operand restore = ctx.info[idx].exec.back().op;
      assert(restore.size() == bld.lm.size());
      bld.copy(Definition(exec, bld.lm), restore);
   }

   return i;
}

/* Avoid live-range splits in Exact mode:
 * Because the data register of atomic VMEM instructions
 * is shared between src and dst, it might be necessary
 * to create live-range splits during RA.
 * Make the live-range splits explicit in WQM mode.
 */
void
handle_atomic_data(exec_ctx& ctx, Builder& bld, unsigned block_idx, aco_ptr<Instruction>& instr)
{
   /* check if this is an atomic VMEM instruction */
   int idx = -1;
   if (!instr->isVMEM() || instr->definitions.empty())
      return;
   else if (instr->isMIMG())
      idx = instr->operands[2].isTemp() ? 2 : -1;
   else if (instr->operands.size() == 4)
      idx = 3;

   if (idx != -1) {
      /* insert explicit copy of atomic data in WQM-mode */
      transition_to_WQM(ctx, bld, block_idx);
      Temp data = instr->operands[idx].getTemp();
      data = bld.copy(bld.def(data.regClass()), data);
      instr->operands[idx].setTemp(data);
   }
}

void
process_instructions(exec_ctx& ctx, Block* block, std::vector<aco_ptr<Instruction>>& instructions,
                     unsigned idx)
{
   block_info& info = ctx.info[block->index];
   WQMState state;
   if (info.exec.back().type & mask_type_wqm) {
      state = WQM;
   } else {
      assert(!ctx.handle_wqm || info.exec.back().type & mask_type_exact);
      state = Exact;
   }

   Builder bld(ctx.program, &instructions);

   for (; idx < block->instructions.size(); idx++) {
      aco_ptr<Instruction> instr = std::move(block->instructions[idx]);

      WQMState needs = ctx.handle_wqm ? get_instr_needs(instr) : Unspecified;

      if (needs == WQM && state != WQM) {
         transition_to_WQM(ctx, bld, block->index);
         state = WQM;
      } else if (needs == Exact) {
         if (ctx.handle_wqm)
            handle_atomic_data(ctx, bld, block->index, instr);
         transition_to_Exact(ctx, bld, block->index);
         state = Exact;
      }

      if (instr->opcode == aco_opcode::p_discard_if) {
         Operand current_exec = Operand(exec, bld.lm);

         if (block->instructions[idx + 1]->opcode == aco_opcode::p_end_wqm) {
            /* Transition to Exact without extra instruction. */
            info.exec.resize(1);
            assert(info.exec[0].type == (mask_type_exact | mask_type_global));
            current_exec = info.exec[0].op;
            info.exec[0].op = Operand(exec, bld.lm);
            state = Exact;
         } else if (info.exec.size() >= 2 && ctx.handle_wqm) {
            /* Preserve the WQM mask */
            info.exec[1].type &= ~mask_type_global;
         }

         Temp cond;
         if (instr->operands[0].isConstant()) {
            assert(instr->operands[0].constantValue() == -1u);
            /* save condition and set exec to zero */
            cond = bld.sop1(Builder::s_and_saveexec, bld.def(bld.lm), bld.def(s1, scc),
                            Definition(exec, bld.lm), Operand::zero(), Operand(exec, bld.lm));
         } else {
            cond = instr->operands[0].getTemp();
            /* discard from current exec */
            bld.sop2(Builder::s_andn2, Definition(exec, bld.lm), bld.def(s1, scc), current_exec,
                     cond);
         }

         if (info.exec.size() == 1) {
            instr->operands[0] = Operand(exec, bld.lm);
         } else {
            /* discard from inner to outer exec mask on stack */
            int num = info.exec.size() - 2;
            Temp exit_cond;
            for (int i = num; i >= 0; i--) {
               Instruction* andn2 = bld.sop2(Builder::s_andn2, bld.def(bld.lm), bld.def(s1, scc),
                                             info.exec[i].op, cond);
               info.exec[i].op = Operand(andn2->definitions[0].getTemp());
               exit_cond = andn2->definitions[1].getTemp();
            }
            instr->operands[0] = bld.scc(exit_cond);
         }

         info.exec.back().op = Operand(exec, bld.lm);
         instr->opcode = aco_opcode::p_exit_early_if_not;
         assert(!ctx.handle_wqm || (info.exec[0].type & mask_type_wqm) == 0);
      } else if (instr->opcode == aco_opcode::p_is_helper) {
         Definition dst = instr->definitions[0];
         assert(dst.size() == bld.lm.size());
         if (state == Exact) {
            instr.reset(create_instruction(bld.w64or32(Builder::s_mov), Format::SOP1, 1, 1));
            instr->operands[0] = Operand::zero();
            instr->definitions[0] = dst;
         } else {
            exec_info& exact_mask = info.exec[0];
            assert(exact_mask.type & mask_type_exact);

            instr.reset(create_instruction(bld.w64or32(Builder::s_andn2), Format::SOP2, 2, 2));
            instr->operands[0] = Operand(exec, bld.lm); /* current exec */
            instr->operands[1] = Operand(exact_mask.op);
            instr->definitions[0] = dst;
            instr->definitions[1] = bld.def(s1, scc);
         }
      } else if (instr->opcode == aco_opcode::p_demote_to_helper) {
         assert((info.exec[0].type & mask_type_exact) && (info.exec[0].type & mask_type_global));

         const bool nested_cf = !(info.exec.back().type & mask_type_global);
         if (ctx.handle_wqm && state == Exact && nested_cf) {
            /* Transition back to WQM without extra instruction. */
            info.exec.pop_back();
            state = WQM;
         } else if (block->instructions[idx + 1]->opcode == aco_opcode::p_end_wqm) {
            /* Transition to Exact without extra instruction. */
            info.exec.resize(1);
            state = Exact;
         } else if (nested_cf) {
            /* Save curent exec temporarily. */
            info.exec.back().op = bld.copy(bld.def(bld.lm), Operand(exec, bld.lm));
         } else {
            info.exec.back().op = Operand(exec, bld.lm);
         }

         /* Remove invocations from global exact mask. */
         Definition def = state == Exact ? Definition(exec, bld.lm) : bld.def(bld.lm);
         Operand src = instr->operands[0].isConstant() ? Operand(exec, bld.lm) : instr->operands[0];

         bld.sop2(Builder::s_andn2, def, bld.def(s1, scc), info.exec[0].op, src);
         info.exec[0].op = def.isTemp() ? Operand(def.getTemp()) : Operand(exec, bld.lm);

         /* Update global WQM mask and store in exec. */
         if (state == WQM) {
            assert(info.exec.size() > 1);
            bld.sop1(Builder::s_wqm, Definition(exec, bld.lm), bld.def(s1, scc), def.getTemp());
         }

         /* End shader if global mask is zero. */
         instr->opcode = aco_opcode::p_exit_early_if_not;
         instr->operands[0] = Operand(exec, bld.lm);
         bld.insert(std::move(instr));

         /* Update all other exec masks. */
         if (nested_cf) {
            const unsigned global_idx = state == WQM ? 1 : 0;
            for (unsigned i = global_idx + 1; i < info.exec.size() - 1; i++) {
               info.exec[i].op = bld.sop2(Builder::s_and, bld.def(bld.lm), bld.def(s1, scc),
                                          info.exec[i].op, Operand(exec, bld.lm));
            }
            /* Update current exec and save WQM mask. */
            info.exec[global_idx].op =
               bld.sop1(Builder::s_and_saveexec, bld.def(bld.lm), bld.def(s1, scc),
                        Definition(exec, bld.lm), info.exec.back().op, Operand(exec, bld.lm));
            info.exec.back().op = Operand(exec, bld.lm);
         }
         continue;

      } else if (instr->opcode == aco_opcode::p_elect) {
         bool all_lanes_enabled = info.exec.back().op.constantEquals(-1u);
         Definition dst = instr->definitions[0];

         if (all_lanes_enabled) {
            bld.copy(Definition(dst), Operand::c32_or_c64(1u, dst.size() == 2));
         } else {
            Temp first_lane_idx = bld.sop1(Builder::s_ff1_i32, bld.def(s1), Operand(exec, bld.lm));
            bld.sop2(Builder::s_lshl, Definition(dst), bld.def(s1, scc),
                     Operand::c32_or_c64(1u, dst.size() == 2), Operand(first_lane_idx));
         }
         continue;
      } else if (instr->opcode == aco_opcode::p_end_wqm) {
         assert(block->kind & block_kind_top_level);
         assert(info.exec.size() <= 2);
         /* This instruction indicates the end of WQM mode. */
         info.exec.back().type |= mask_type_global;
         transition_to_Exact(ctx, bld, block->index);
         state = Exact;
         ctx.handle_wqm = false;
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

   if (block->linear_succs.empty())
      return;

   if (block->kind & block_kind_loop_preheader) {
      /* collect information about the succeeding loop */
      bool has_divergent_break = false;
      bool has_divergent_continue = false;
      bool has_discard = false;
      unsigned loop_nest_depth = ctx.program->blocks[idx + 1].loop_nest_depth;

      for (unsigned i = idx + 1; ctx.program->blocks[i].loop_nest_depth >= loop_nest_depth; i++) {
         Block& loop_block = ctx.program->blocks[i];

         if (loop_block.kind & block_kind_uses_discard)
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

      if (has_divergent_break) {
         /* save restore exec mask */
         const Operand& current_exec = ctx.info[idx].exec.back().op;
         if (!current_exec.isTemp() && !current_exec.isConstant()) {
            bld.reset(bld.instructions, std::prev(bld.instructions->end()));
            Operand restore = bld.copy(bld.def(bld.lm), Operand(exec, bld.lm));
            ctx.info[idx].exec.back().op = restore;
            bld.reset(bld.instructions);
         }
         uint8_t mask = ctx.info[idx].exec.back().type & (mask_type_wqm | mask_type_exact);
         ctx.info[idx].exec.emplace_back(Operand(exec, bld.lm), mask);
      }
      unsigned num_exec_masks = ctx.info[idx].exec.size();

      ctx.loop.emplace_back(&ctx.program->blocks[block->linear_succs[0]], num_exec_masks,
                            has_divergent_break, has_divergent_continue, has_discard);

      Pseudo_branch_instruction& branch = block->instructions.back()->branch();
      branch.target[0] = block->linear_succs[0];
   } else if (block->kind & block_kind_continue_or_break) {
      assert(ctx.program->blocks[ctx.program->blocks[block->linear_succs[1]].linear_succs[0]].kind &
             block_kind_loop_header);
      assert(ctx.program->blocks[ctx.program->blocks[block->linear_succs[0]].linear_succs[0]].kind &
             block_kind_loop_exit);
      assert(block->instructions.back()->opcode == aco_opcode::p_branch);
      block->instructions.pop_back();

      while (!(ctx.info[idx].exec.back().type & mask_type_loop))
         ctx.info[idx].exec.pop_back();

      Temp cond = bld.sop2(Builder::s_or, bld.def(bld.lm), bld.def(s1, scc),
                           ctx.info[idx].exec.back().op, Operand::zero(bld.lm.bytes()))
                     .def(1)
                     .getTemp();
      bld.branch(aco_opcode::p_cbranch_nz, Operand(cond, scc), block->linear_succs[1],
                 block->linear_succs[0]);
   } else if (block->kind & block_kind_uniform) {
      Pseudo_branch_instruction& branch = block->instructions.back()->branch();
      if (branch.opcode == aco_opcode::p_branch) {
         branch.target[0] = block->linear_succs[0];
      } else {
         branch.target[0] = block->linear_succs[1];
         branch.target[1] = block->linear_succs[0];
      }
   } else if (block->kind & block_kind_branch) {
      // orig = s_and_saveexec_b64
      assert(block->linear_succs.size() == 2);
      assert(block->instructions.back()->opcode == aco_opcode::p_cbranch_z);
      Temp cond = block->instructions.back()->operands[0].getTemp();
      aco_ptr<Instruction> branch = std::move(block->instructions.back());
      block->instructions.pop_back();

      uint8_t mask_type = ctx.info[idx].exec.back().type & (mask_type_wqm | mask_type_exact);
      if (ctx.info[idx].exec.back().op.constantEquals(-1u)) {
         bld.copy(Definition(exec, bld.lm), cond);
      } else if (ctx.info[idx].exec.back().op.isTemp()) {
         bld.sop2(Builder::s_and, Definition(exec, bld.lm), bld.def(s1, scc), cond,
                  Operand(exec, bld.lm));
      } else {
         Temp old_exec = bld.sop1(Builder::s_and_saveexec, bld.def(bld.lm), bld.def(s1, scc),
                                  Definition(exec, bld.lm), cond, Operand(exec, bld.lm));

         ctx.info[idx].exec.back().op = Operand(old_exec);
      }

      /* add next current exec to the stack */
      ctx.info[idx].exec.emplace_back(Operand(exec, bld.lm), mask_type);

      Builder::Result r = bld.branch(aco_opcode::p_cbranch_z, Operand(exec, bld.lm),
                                     block->linear_succs[1], block->linear_succs[0]);
      r->branch().rarely_taken = branch->branch().rarely_taken;
      r->branch().never_taken = branch->branch().never_taken;
   } else if (block->kind & block_kind_invert) {
      // exec = s_andn2_b64 (original_exec, exec)
      assert(block->instructions.back()->opcode == aco_opcode::p_branch);
      aco_ptr<Instruction> branch = std::move(block->instructions.back());
      block->instructions.pop_back();
      assert(ctx.info[idx].exec.size() >= 2);
      Operand orig_exec = ctx.info[idx].exec[ctx.info[idx].exec.size() - 2].op;
      bld.sop2(Builder::s_andn2, Definition(exec, bld.lm), bld.def(s1, scc), orig_exec,
               Operand(exec, bld.lm));

      Builder::Result r = bld.branch(aco_opcode::p_cbranch_z, Operand(exec, bld.lm),
                                     block->linear_succs[1], block->linear_succs[0]);
      r->branch().rarely_taken = branch->branch().rarely_taken;
      r->branch().never_taken = branch->branch().never_taken;
   } else if (block->kind & block_kind_break) {
      // loop_mask = s_andn2_b64 (loop_mask, exec)
      assert(block->instructions.back()->opcode == aco_opcode::p_branch);
      block->instructions.pop_back();

      Temp cond = Temp();
      for (int exec_idx = ctx.info[idx].exec.size() - 2; exec_idx >= 0; exec_idx--) {
         cond = bld.tmp(s1);
         Operand exec_mask = ctx.info[idx].exec[exec_idx].op;
         exec_mask = bld.sop2(Builder::s_andn2, bld.def(bld.lm), bld.scc(Definition(cond)),
                              exec_mask, Operand(exec, bld.lm));
         ctx.info[idx].exec[exec_idx].op = exec_mask;
         if (ctx.info[idx].exec[exec_idx].type & mask_type_loop)
            break;
      }

      /* Implicitly set exec to zero and branch. */
      ctx.info[idx].exec.back().op = Operand::zero(bld.lm.bytes());
      bld.branch(aco_opcode::p_cbranch_nz, bld.scc(cond), block->linear_succs[1],
                 block->linear_succs[0]);
   } else if (block->kind & block_kind_continue) {
      assert(block->instructions.back()->opcode == aco_opcode::p_branch);
      block->instructions.pop_back();

      Temp cond = Temp();
      for (int exec_idx = ctx.info[idx].exec.size() - 2; exec_idx >= 0; exec_idx--) {
         if (ctx.info[idx].exec[exec_idx].type & mask_type_loop)
            break;
         cond = bld.tmp(s1);
         Operand exec_mask = ctx.info[idx].exec[exec_idx].op;
         exec_mask = bld.sop2(Builder::s_andn2, bld.def(bld.lm), bld.scc(Definition(cond)),
                              exec_mask, Operand(exec, bld.lm));
         ctx.info[idx].exec[exec_idx].op = exec_mask;
      }
      assert(cond != Temp());

      /* Implicitly set exec to zero and branch. */
      ctx.info[idx].exec.back().op = Operand::zero(bld.lm.bytes());
      bld.branch(aco_opcode::p_cbranch_nz, bld.scc(cond), block->linear_succs[1],
                 block->linear_succs[0]);
   } else {
      unreachable("unknown/invalid block type");
   }
}

void
process_block(exec_ctx& ctx, Block* block)
{
   std::vector<aco_ptr<Instruction>> instructions;
   instructions.reserve(block->instructions.size());

   unsigned idx = add_coupling_code(ctx, block, instructions);

   assert(!block->linear_succs.empty() || ctx.info[block->index].exec.size() <= 2);

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
      ctx.handle_wqm = true;

   for (Block& block : program->blocks)
      process_block(ctx, &block);
}

} // namespace aco
