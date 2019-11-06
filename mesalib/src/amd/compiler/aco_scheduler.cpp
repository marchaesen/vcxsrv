/*
 * Copyright © 2018 Valve Corporation
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
#include <unordered_set>
#include <algorithm>

#include "vulkan/radv_shader.h" // for radv_nir_compiler_options
#include "amdgfxregs.h"

#define SMEM_WINDOW_SIZE (350 - ctx.num_waves * 35)
#define VMEM_WINDOW_SIZE (1024 - ctx.num_waves * 64)
#define POS_EXP_WINDOW_SIZE 512
#define SMEM_MAX_MOVES (64 - ctx.num_waves * 4)
#define VMEM_MAX_MOVES (128 - ctx.num_waves * 8)
/* creating clauses decreases def-use distances, so make it less aggressive the lower num_waves is */
#define VMEM_CLAUSE_MAX_GRAB_DIST ((ctx.num_waves - 1) * 8)
#define POS_EXP_MAX_MOVES 512

namespace aco {

struct sched_ctx {
   std::vector<bool> depends_on;
   std::vector<bool> RAR_dependencies;
   /* For downwards VMEM scheduling, same as RAR_dependencies but excludes the
    * instructions in the clause, since new instructions in the clause are not
    * moved past any other instructions in the clause. */
   std::vector<bool> new_RAR_dependencies;

   RegisterDemand max_registers;
   int16_t num_waves;
   int16_t last_SMEM_stall;
   int last_SMEM_dep_idx;
};

/* This scheduler is a simple bottom-up pass based on ideas from
 * "A Novel Lightweight Instruction Scheduling Algorithm for Just-In-Time Compiler"
 * from Xiaohua Shi and Peng Guo.
 * The basic approach is to iterate over all instructions. When a memory instruction
 * is encountered it tries to move independent instructions from above and below
 * between the memory instruction and it's first user.
 * The novelty is that this scheduler cares for the current register pressure:
 * Instructions will only be moved if the register pressure won't exceed a certain bound.
 */

template <typename T>
void move_element(T& list, size_t idx, size_t before) {
    if (idx < before) {
        auto begin = std::next(list.begin(), idx);
        auto end = std::next(list.begin(), before);
        std::rotate(begin, begin + 1, end);
    } else if (idx > before) {
        auto begin = std::next(list.begin(), before);
        auto end = std::next(list.begin(), idx + 1);
        std::rotate(begin, end - 1, end);
    }
}

static RegisterDemand getLiveChanges(aco_ptr<Instruction>& instr)
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

static RegisterDemand getTempRegisters(aco_ptr<Instruction>& instr)
{
   RegisterDemand temp_registers;
   for (const Definition& def : instr->definitions) {
      if (!def.isTemp() || !def.isKill())
         continue;
      temp_registers += def.getTemp();
   }
   return temp_registers;
}

static bool is_spill_reload(aco_ptr<Instruction>& instr)
{
   return instr->opcode == aco_opcode::p_spill || instr->opcode == aco_opcode::p_reload;
}

bool can_move_instr(aco_ptr<Instruction>& instr, Instruction* current, int moving_interaction)
{
   /* don't move exports so that they stay closer together */
   if (instr->format == Format::EXP)
      return false;

   /* don't move s_memtime/s_memrealtime */
   if (instr->opcode == aco_opcode::s_memtime || instr->opcode == aco_opcode::s_memrealtime)
      return false;

   /* handle barriers */

   /* TODO: instead of stopping, maybe try to move the barriers and any
    * instructions interacting with them instead? */
   if (instr->format != Format::PSEUDO_BARRIER) {
      if (instr->opcode == aco_opcode::s_barrier) {
         bool can_reorder = false;
         switch (current->format) {
         case Format::SMEM:
            can_reorder = static_cast<SMEM_instruction*>(current)->can_reorder;
            break;
         case Format::MUBUF:
            can_reorder = static_cast<MUBUF_instruction*>(current)->can_reorder;
            break;
         case Format::MIMG:
            can_reorder = static_cast<MIMG_instruction*>(current)->can_reorder;
            break;
         default:
            break;
         }
         return can_reorder && moving_interaction == barrier_none;
      } else {
         return true;
      }
   }

   int interaction = get_barrier_interaction(current);
   interaction |= moving_interaction;

   switch (instr->opcode) {
   case aco_opcode::p_memory_barrier_atomic:
      return !(interaction & barrier_atomic);
   /* For now, buffer and image barriers are treated the same. this is because of
    * dEQP-VK.memory_model.message_passing.core11.u32.coherent.fence_fence.atomicwrite.device.payload_nonlocal.buffer.guard_nonlocal.image.comp
    * which seems to use an image load to determine if the result of a buffer load is valid. So the ordering of the two loads is important.
    * I /think/ we should probably eventually expand the meaning of a buffer barrier so that all buffer operations before it, must stay before it
    * and that both image and buffer operations after it, must stay after it. We should also do the same for image barriers.
    * Or perhaps the problem is that we don't have a combined barrier instruction for both buffers and images, but the CTS test expects us to?
    * Either way, this solution should work. */
   case aco_opcode::p_memory_barrier_buffer:
   case aco_opcode::p_memory_barrier_image:
      return !(interaction & (barrier_image | barrier_buffer));
   case aco_opcode::p_memory_barrier_shared:
      return !(interaction & barrier_shared);
   case aco_opcode::p_memory_barrier_all:
      return interaction == barrier_none;
   default:
      return false;
   }
}

bool can_reorder(Instruction* candidate)
{
   switch (candidate->format) {
   case Format::SMEM:
      return static_cast<SMEM_instruction*>(candidate)->can_reorder;
   case Format::MUBUF:
      return static_cast<MUBUF_instruction*>(candidate)->can_reorder;
   case Format::MIMG:
      return static_cast<MIMG_instruction*>(candidate)->can_reorder;
   case Format::MTBUF:
      return static_cast<MTBUF_instruction*>(candidate)->can_reorder;
   case Format::FLAT:
   case Format::GLOBAL:
   case Format::SCRATCH:
      return false;
   default:
      return true;
   }
}

void schedule_SMEM(sched_ctx& ctx, Block* block,
                   std::vector<RegisterDemand>& register_demand,
                   Instruction* current, int idx)
{
   assert(idx != 0);
   int window_size = SMEM_WINDOW_SIZE;
   int max_moves = SMEM_MAX_MOVES;
   int16_t k = 0;
   bool can_reorder_cur = can_reorder(current);

   /* don't move s_memtime/s_memrealtime */
   if (current->opcode == aco_opcode::s_memtime || current->opcode == aco_opcode::s_memrealtime)
      return;

   /* create the initial set of values which current depends on */
   std::fill(ctx.depends_on.begin(), ctx.depends_on.end(), false);
   for (const Operand& op : current->operands) {
      if (op.isTemp())
         ctx.depends_on[op.tempId()] = true;
   }

   /* maintain how many registers remain free when moving instructions */
   RegisterDemand register_pressure = register_demand[idx];

   /* first, check if we have instructions before current to move down */
   int insert_idx = idx + 1;
   int moving_interaction = barrier_none;
   bool moving_spill = false;

   for (int candidate_idx = idx - 1; k < max_moves && candidate_idx > (int) idx - window_size; candidate_idx--) {
      assert(candidate_idx >= 0);
      aco_ptr<Instruction>& candidate = block->instructions[candidate_idx];
      bool can_reorder_candidate = can_reorder(candidate.get());

      /* break if we'd make the previous SMEM instruction stall */
      bool can_stall_prev_smem = idx <= ctx.last_SMEM_dep_idx && candidate_idx < ctx.last_SMEM_dep_idx;
      if (can_stall_prev_smem && ctx.last_SMEM_stall >= 0)
         break;

      /* break when encountering another MEM instruction, logical_start or barriers */
      if (!can_reorder_candidate && !can_reorder_cur)
         break;
      if (candidate->opcode == aco_opcode::p_logical_start)
         break;
      if (candidate->opcode == aco_opcode::p_exit_early_if)
         break;
      if (!can_move_instr(candidate, current, moving_interaction))
         break;
      if (candidate->isVMEM())
         break;
      register_pressure.update(register_demand[candidate_idx]);

      /* if current depends on candidate, add additional dependencies and continue */
      bool can_move_down = true;
      bool writes_exec = false;
      for (const Definition& def : candidate->definitions) {
         if (def.isTemp() && ctx.depends_on[def.tempId()])
            can_move_down = false;
         if (def.isFixed() && def.physReg() == exec)
            writes_exec = true;
      }
      if (writes_exec)
         break;

      if (moving_spill && is_spill_reload(candidate))
         can_move_down = false;
      if ((moving_interaction & barrier_shared) && candidate->format == Format::DS)
         can_move_down = false;
      moving_interaction |= get_barrier_interaction(candidate.get());
      moving_spill |= is_spill_reload(candidate);
      if (!can_move_down) {
         for (const Operand& op : candidate->operands) {
            if (op.isTemp())
               ctx.depends_on[op.tempId()] = true;
         }
         can_reorder_cur &= can_reorder_candidate;
         continue;
      }

      bool register_pressure_unknown = false;
      /* check if one of candidate's operands is killed by depending instruction */
      for (const Operand& op : candidate->operands) {
         if (op.isTemp() && ctx.depends_on[op.tempId()]) {
            // FIXME: account for difference in register pressure
            register_pressure_unknown = true;
         }
      }
      if (register_pressure_unknown) {
         for (const Operand& op : candidate->operands) {
            if (op.isTemp())
               ctx.depends_on[op.tempId()] = true;
         }
         can_reorder_cur &= can_reorder_candidate;
         continue;
      }

      /* check if register pressure is low enough: the diff is negative if register pressure is increased */
      const RegisterDemand candidate_diff = getLiveChanges(candidate);
      const RegisterDemand tempDemand = getTempRegisters(candidate);
      if (RegisterDemand(register_pressure - candidate_diff).exceeds(ctx.max_registers))
         break;
      const RegisterDemand tempDemand2 = getTempRegisters(block->instructions[insert_idx - 1]);
      const RegisterDemand new_demand  = register_demand[insert_idx - 1] - tempDemand2 + tempDemand;
      if (new_demand.exceeds(ctx.max_registers))
         break;
      // TODO: we might want to look further to find a sequence of instructions to move down which doesn't exceed reg pressure

      /* move the candidate below the memory load */
      move_element(block->instructions, candidate_idx, insert_idx);

      /* update register pressure */
      move_element(register_demand, candidate_idx, insert_idx);
      for (int i = candidate_idx; i < insert_idx - 1; i++) {
         register_demand[i] -= candidate_diff;
      }
      register_demand[insert_idx - 1] = new_demand;
      register_pressure -= candidate_diff;

      if (candidate_idx < ctx.last_SMEM_dep_idx)
         ctx.last_SMEM_stall++;
      insert_idx--;
      k++;
   }

   /* create the initial set of values which depend on current */
   std::fill(ctx.depends_on.begin(), ctx.depends_on.end(), false);
   std::fill(ctx.RAR_dependencies.begin(), ctx.RAR_dependencies.end(), false);
   for (const Definition& def : current->definitions) {
      if (def.isTemp())
         ctx.depends_on[def.tempId()] = true;
   }

   /* find the first instruction depending on current or find another MEM */
   insert_idx = idx + 1;
   moving_interaction = barrier_none;
   moving_spill = false;
   can_reorder_cur = true;

   bool found_dependency = false;
   /* second, check if we have instructions after current to move up */
   for (int candidate_idx = idx + 1; k < max_moves && candidate_idx < (int) idx + window_size; candidate_idx++) {
      assert(candidate_idx < (int) block->instructions.size());
      aco_ptr<Instruction>& candidate = block->instructions[candidate_idx];
      bool can_reorder_candidate = can_reorder(candidate.get());

      if (candidate->opcode == aco_opcode::p_logical_end)
         break;
      if (!can_move_instr(candidate, current, moving_interaction))
         break;

      const bool writes_exec = std::any_of(candidate->definitions.begin(), candidate->definitions.end(),
                                           [](const Definition& def) { return def.isFixed() && def.physReg() == exec;});
      if (writes_exec)
         break;

      /* check if candidate depends on current */
      bool is_dependency = std::any_of(candidate->operands.begin(), candidate->operands.end(),
                                       [&ctx](const Operand& op) { return op.isTemp() && ctx.depends_on[op.tempId()];});
      /* no need to steal from following VMEM instructions */
      if (is_dependency && candidate->isVMEM())
         break;
      if (moving_spill && is_spill_reload(candidate))
         is_dependency = true;
      if ((moving_interaction & barrier_shared) && candidate->format == Format::DS)
         is_dependency = true;
      moving_interaction |= get_barrier_interaction(candidate.get());
      moving_spill |= is_spill_reload(candidate);
      if (is_dependency) {
         for (const Definition& def : candidate->definitions) {
            if (def.isTemp())
               ctx.depends_on[def.tempId()] = true;
         }
         for (const Operand& op : candidate->operands) {
            if (op.isTemp())
               ctx.RAR_dependencies[op.tempId()] = true;
         }
         if (!found_dependency) {
            insert_idx = candidate_idx;
            found_dependency = true;
            /* init register pressure */
            register_pressure = register_demand[insert_idx - 1];
         }
      }

      if (!can_reorder_candidate && !can_reorder_cur)
         break;

      if (!found_dependency) {
         k++;
         continue;
      }

      /* update register pressure */
      register_pressure.update(register_demand[candidate_idx - 1]);

      if (is_dependency) {
         can_reorder_cur &= can_reorder_candidate;
         continue;
      }
      assert(insert_idx != idx);

      // TODO: correctly calculate register pressure for this case
      bool register_pressure_unknown = false;
      /* check if candidate uses/kills an operand which is used by a dependency */
      for (const Operand& op : candidate->operands) {
         if (op.isTemp() && ctx.RAR_dependencies[op.tempId()])
            register_pressure_unknown = true;
      }
      if (register_pressure_unknown) {
         if (candidate->isVMEM())
            break;
         for (const Definition& def : candidate->definitions) {
            if (def.isTemp())
               ctx.RAR_dependencies[def.tempId()] = true;
         }
         for (const Operand& op : candidate->operands) {
            if (op.isTemp())
               ctx.RAR_dependencies[op.tempId()] = true;
         }
         can_reorder_cur &= can_reorder_candidate;
         continue;
      }

      /* check if register pressure is low enough: the diff is negative if register pressure is decreased */
      const RegisterDemand candidate_diff = getLiveChanges(candidate);
      const RegisterDemand temp = getTempRegisters(candidate);
      if (RegisterDemand(register_pressure + candidate_diff).exceeds(ctx.max_registers))
         break;
      const RegisterDemand temp2 = getTempRegisters(block->instructions[insert_idx - 1]);
      const RegisterDemand new_demand = register_demand[insert_idx - 1] - temp2 + candidate_diff + temp;
      if (new_demand.exceeds(ctx.max_registers))
         break;

      /* move the candidate above the insert_idx */
      move_element(block->instructions, candidate_idx, insert_idx);

      /* update register pressure */
      move_element(register_demand, candidate_idx, insert_idx);
      for (int i = insert_idx + 1; i <= candidate_idx; i++) {
         register_demand[i] += candidate_diff;
      }
      register_demand[insert_idx] = new_demand;
      register_pressure += candidate_diff;
      insert_idx++;
      k++;
   }

   ctx.last_SMEM_dep_idx = found_dependency ? insert_idx : 0;
   ctx.last_SMEM_stall = 10 - ctx.num_waves - k;
}

void schedule_VMEM(sched_ctx& ctx, Block* block,
                   std::vector<RegisterDemand>& register_demand,
                   Instruction* current, int idx)
{
   assert(idx != 0);
   int window_size = VMEM_WINDOW_SIZE;
   int max_moves = VMEM_MAX_MOVES;
   int clause_max_grab_dist = VMEM_CLAUSE_MAX_GRAB_DIST;
   int16_t k = 0;
   /* initially true as we don't pull other VMEM instructions
    * through the current instruction */
   bool can_reorder_vmem = true;
   bool can_reorder_smem = true;

   /* create the initial set of values which current depends on */
   std::fill(ctx.depends_on.begin(), ctx.depends_on.end(), false);
   std::fill(ctx.RAR_dependencies.begin(), ctx.RAR_dependencies.end(), false);
   std::fill(ctx.new_RAR_dependencies.begin(), ctx.new_RAR_dependencies.end(), false);
   for (const Operand& op : current->operands) {
      if (op.isTemp()) {
         ctx.depends_on[op.tempId()] = true;
         if (op.isFirstKill())
            ctx.RAR_dependencies[op.tempId()] = true;
      }
   }

   /* maintain how many registers remain free when moving instructions */
   RegisterDemand register_pressure_indep = register_demand[idx];
   RegisterDemand register_pressure_clause = register_demand[idx];

   /* first, check if we have instructions before current to move down */
   int indep_insert_idx = idx + 1;
   int clause_insert_idx = idx;
   int moving_interaction = barrier_none;
   bool moving_spill = false;

   for (int candidate_idx = idx - 1; k < max_moves && candidate_idx > (int) idx - window_size; candidate_idx--) {
      assert(candidate_idx >= 0);
      aco_ptr<Instruction>& candidate = block->instructions[candidate_idx];
      bool can_reorder_candidate = can_reorder(candidate.get());

      /* break when encountering another VMEM instruction, logical_start or barriers */
      if (!can_reorder_smem && candidate->format == Format::SMEM && !can_reorder_candidate)
         break;
      if (candidate->opcode == aco_opcode::p_logical_start)
         break;
      if (candidate->opcode == aco_opcode::p_exit_early_if)
         break;
      if (!can_move_instr(candidate, current, moving_interaction))
         break;

      /* break if we'd make the previous SMEM instruction stall */
      bool can_stall_prev_smem = idx <= ctx.last_SMEM_dep_idx && candidate_idx < ctx.last_SMEM_dep_idx;
      if (can_stall_prev_smem && ctx.last_SMEM_stall >= 0)
         break;
      register_pressure_indep.update(register_demand[candidate_idx]);

      bool part_of_clause = false;
      if (candidate->isVMEM()) {
         bool same_resource = candidate->operands[1].tempId() == current->operands[1].tempId();
         bool can_reorder = can_reorder_vmem || can_reorder_candidate;
         int grab_dist = clause_insert_idx - candidate_idx;
         /* We can't easily tell how much this will decrease the def-to-use
          * distances, so just use how far it will be moved as a heuristic. */
         part_of_clause = can_reorder && same_resource && grab_dist < clause_max_grab_dist;
      }

      /* if current depends on candidate, add additional dependencies and continue */
      bool can_move_down = !candidate->isVMEM() || part_of_clause;
      bool writes_exec = false;
      for (const Definition& def : candidate->definitions) {
         if (def.isTemp() && ctx.depends_on[def.tempId()])
            can_move_down = false;
         if (def.isFixed() && def.physReg() == exec)
            writes_exec = true;
      }
      if (writes_exec)
         break;

      if (moving_spill && is_spill_reload(candidate))
         can_move_down = false;
      if ((moving_interaction & barrier_shared) && candidate->format == Format::DS)
         can_move_down = false;
      moving_interaction |= get_barrier_interaction(candidate.get());
      moving_spill |= is_spill_reload(candidate);
      if (!can_move_down) {
         for (const Operand& op : candidate->operands) {
            if (op.isTemp()) {
               ctx.depends_on[op.tempId()] = true;
               if (op.isFirstKill()) {
                  ctx.RAR_dependencies[op.tempId()] = true;
                  ctx.new_RAR_dependencies[op.tempId()] = true;
               }
            }
         }
         register_pressure_clause.update(register_demand[candidate_idx]);
         can_reorder_smem &= candidate->format != Format::SMEM || can_reorder_candidate;
         can_reorder_vmem &= !candidate->isVMEM() || can_reorder_candidate;
         continue;
      }

      if (part_of_clause) {
         for (const Operand& op : candidate->operands) {
            if (op.isTemp()) {
               ctx.depends_on[op.tempId()] = true;
               if (op.isFirstKill())
                  ctx.RAR_dependencies[op.tempId()] = true;
            }
         }
      }

      bool register_pressure_unknown = false;
      std::vector<bool>& RAR_deps = part_of_clause ? ctx.new_RAR_dependencies : ctx.RAR_dependencies;
      /* check if one of candidate's operands is killed by depending instruction */
      for (const Operand& op : candidate->operands) {
         if (op.isTemp() && RAR_deps[op.tempId()]) {
            // FIXME: account for difference in register pressure
            register_pressure_unknown = true;
         }
      }
      if (register_pressure_unknown) {
         for (const Operand& op : candidate->operands) {
            if (op.isTemp()) {
               ctx.depends_on[op.tempId()] = true;
               if (op.isFirstKill()) {
                  ctx.RAR_dependencies[op.tempId()] = true;
                  ctx.new_RAR_dependencies[op.tempId()] = true;
               }
            }
         }
         register_pressure_clause.update(register_demand[candidate_idx]);
         can_reorder_smem &= candidate->format != Format::SMEM || can_reorder_candidate;
         can_reorder_vmem &= !candidate->isVMEM() || can_reorder_candidate;
         continue;
      }

      int insert_idx = part_of_clause ? clause_insert_idx : indep_insert_idx;
      RegisterDemand register_pressure = part_of_clause ? register_pressure_clause : register_pressure_indep;

      /* check if register pressure is low enough: the diff is negative if register pressure is increased */
      const RegisterDemand candidate_diff = getLiveChanges(candidate);
      const RegisterDemand temp = getTempRegisters(candidate);;
      if (RegisterDemand(register_pressure - candidate_diff).exceeds(ctx.max_registers))
         break;
      const RegisterDemand temp2 = getTempRegisters(block->instructions[insert_idx - 1]);
      const RegisterDemand new_demand = register_demand[insert_idx - 1] - temp2 + temp;
      if (new_demand.exceeds(ctx.max_registers))
         break;
      // TODO: we might want to look further to find a sequence of instructions to move down which doesn't exceed reg pressure

      /* move the candidate below the memory load */
      move_element(block->instructions, candidate_idx, insert_idx);

      /* update register pressure */
      move_element(register_demand, candidate_idx, insert_idx);
      for (int i = candidate_idx; i < insert_idx - 1; i++) {
         register_demand[i] -= candidate_diff;
      }
      register_demand[insert_idx - 1] = new_demand;
      register_pressure_clause -= candidate_diff;
      clause_insert_idx--;
      if (!part_of_clause) {
         register_pressure_indep -= candidate_diff;
         indep_insert_idx--;
      }
      k++;
      if (candidate_idx < ctx.last_SMEM_dep_idx)
         ctx.last_SMEM_stall++;
   }

   /* create the initial set of values which depend on current */
   std::fill(ctx.depends_on.begin(), ctx.depends_on.end(), false);
   std::fill(ctx.RAR_dependencies.begin(), ctx.RAR_dependencies.end(), false);
   for (const Definition& def : current->definitions) {
      if (def.isTemp())
         ctx.depends_on[def.tempId()] = true;
   }

   /* find the first instruction depending on current or find another VMEM */
   RegisterDemand register_pressure;
   int insert_idx = idx;
   moving_interaction = barrier_none;
   moving_spill = false;
   // TODO: differentiate between loads and stores (load-load can always reorder)
   can_reorder_vmem = true;
   can_reorder_smem = true;

   bool found_dependency = false;
   /* second, check if we have instructions after current to move up */
   for (int candidate_idx = idx + 1; k < max_moves && candidate_idx < (int) idx + window_size; candidate_idx++) {
      assert(candidate_idx < (int) block->instructions.size());
      aco_ptr<Instruction>& candidate = block->instructions[candidate_idx];
      bool can_reorder_candidate = can_reorder(candidate.get());

      if (candidate->opcode == aco_opcode::p_logical_end)
         break;
      if (!can_move_instr(candidate, current, moving_interaction))
         break;

      const bool writes_exec = std::any_of(candidate->definitions.begin(), candidate->definitions.end(),
                                           [](const Definition& def) {return def.isFixed() && def.physReg() == exec; });
      if (writes_exec)
         break;

      /* check if candidate depends on current */
      bool is_dependency = false;
      if (candidate->format == Format::SMEM)
         is_dependency = !can_reorder_smem && !can_reorder_candidate;
      if (candidate->isVMEM())
         is_dependency = !can_reorder_vmem && !can_reorder_candidate;
      for (const Operand& op : candidate->operands) {
         if (op.isTemp() && ctx.depends_on[op.tempId()]) {
            is_dependency = true;
            break;
         }
      }
      if (moving_spill && is_spill_reload(candidate))
         is_dependency = true;
      if ((moving_interaction & barrier_shared) && candidate->format == Format::DS)
         is_dependency = true;
      moving_interaction |= get_barrier_interaction(candidate.get());
      moving_spill |= is_spill_reload(candidate);
      if (is_dependency) {
         for (const Definition& def : candidate->definitions) {
            if (def.isTemp())
               ctx.depends_on[def.tempId()] = true;
         }
         for (const Operand& op : candidate->operands) {
            if (op.isTemp())
               ctx.RAR_dependencies[op.tempId()] = true;
         }
         /* update flag whether we can reorder other memory instructions */
         can_reorder_smem &= candidate->format != Format::SMEM || can_reorder_candidate;
         can_reorder_vmem &= !candidate->isVMEM() || can_reorder_candidate;

         if (!found_dependency) {
            insert_idx = candidate_idx;
            found_dependency = true;
            /* init register pressure */
            register_pressure = register_demand[insert_idx - 1];
            continue;
         }

      } else if (candidate->isVMEM()) {
         /* don't move up dependencies of other VMEM instructions */
         for (const Definition& def : candidate->definitions) {
            if (def.isTemp())
               ctx.depends_on[def.tempId()] = true;
         }
      }

      /* update register pressure */
      register_pressure.update(register_demand[candidate_idx - 1]);

      if (is_dependency || !found_dependency)
         continue;
      assert(insert_idx != idx);

      bool register_pressure_unknown = false;
      /* check if candidate uses/kills an operand which is used by a dependency */
      for (const Operand& op : candidate->operands) {
         if (op.isTemp() && op.isFirstKill() && ctx.RAR_dependencies[op.tempId()])
            register_pressure_unknown = true;
      }
      if (register_pressure_unknown) {
         for (const Definition& def : candidate->definitions) {
            if (def.isTemp())
               ctx.depends_on[def.tempId()] = true;
         }
         for (const Operand& op : candidate->operands) {
            if (op.isTemp())
               ctx.RAR_dependencies[op.tempId()] = true;
         }
         can_reorder_smem &= candidate->format != Format::SMEM || can_reorder_candidate;
         can_reorder_vmem &= !candidate->isVMEM() || can_reorder_candidate;
         continue;
      }

      /* check if register pressure is low enough: the diff is negative if register pressure is decreased */
      const RegisterDemand candidate_diff = getLiveChanges(candidate);
      const RegisterDemand temp = getTempRegisters(candidate);
      if (RegisterDemand(register_pressure + candidate_diff).exceeds(ctx.max_registers))
         break;
      const RegisterDemand temp2 = getTempRegisters(block->instructions[insert_idx - 1]);
      const RegisterDemand new_demand = register_demand[insert_idx - 1] - temp2 + candidate_diff + temp;
      if (new_demand.exceeds(ctx.max_registers))
         break;

      /* move the candidate above the insert_idx */
      move_element(block->instructions, candidate_idx, insert_idx);

      /* update register pressure */
      move_element(register_demand, candidate_idx, insert_idx);
      for (int i = insert_idx + 1; i <= candidate_idx; i++) {
         register_demand[i] += candidate_diff;
      }
      register_demand[insert_idx] = new_demand;
      register_pressure += candidate_diff;
      insert_idx++;
      k++;
   }
}

void schedule_position_export(sched_ctx& ctx, Block* block,
                              std::vector<RegisterDemand>& register_demand,
                              Instruction* current, int idx)
{
   assert(idx != 0);
   int window_size = POS_EXP_WINDOW_SIZE;
   int max_moves = POS_EXP_MAX_MOVES;
   int16_t k = 0;

   /* create the initial set of values which current depends on */
   std::fill(ctx.depends_on.begin(), ctx.depends_on.end(), false);
   std::fill(ctx.RAR_dependencies.begin(), ctx.RAR_dependencies.end(), false);
   for (const Operand& op : current->operands) {
      if (op.isTemp()) {
         ctx.depends_on[op.tempId()] = true;
         if (op.isFirstKill())
            ctx.RAR_dependencies[op.tempId()] = true;
      }
   }

   /* maintain how many registers remain free when moving instructions */
   RegisterDemand register_pressure = register_demand[idx];

   /* first, check if we have instructions before current to move down */
   int insert_idx = idx + 1;
   int moving_interaction = barrier_none;
   bool moving_spill = false;

   for (int candidate_idx = idx - 1; k < max_moves && candidate_idx > (int) idx - window_size; candidate_idx--) {
      assert(candidate_idx >= 0);
      aco_ptr<Instruction>& candidate = block->instructions[candidate_idx];

      /* break when encountering logical_start or barriers */
      if (candidate->opcode == aco_opcode::p_logical_start)
         break;
      if (candidate->opcode == aco_opcode::p_exit_early_if)
         break;
      if (candidate->isVMEM() || candidate->format == Format::SMEM)
         break;
      if (!can_move_instr(candidate, current, moving_interaction))
         break;

      register_pressure.update(register_demand[candidate_idx]);

      /* if current depends on candidate, add additional dependencies and continue */
      bool can_move_down = true;
      bool writes_exec = false;
      for (unsigned i = 0; i < candidate->definitions.size(); i++) {
         if (candidate->definitions[i].isTemp() && ctx.depends_on[candidate->definitions[i].tempId()])
            can_move_down = false;
         if (candidate->definitions[i].isFixed() && candidate->definitions[i].physReg() == exec)
            writes_exec = true;
      }
      if (writes_exec)
         break;

      if (moving_spill && is_spill_reload(candidate))
         can_move_down = false;
      if ((moving_interaction & barrier_shared) && candidate->format == Format::DS)
         can_move_down = false;
      moving_interaction |= get_barrier_interaction(candidate.get());
      moving_spill |= is_spill_reload(candidate);
      if (!can_move_down) {
         for (const Operand& op : candidate->operands) {
            if (op.isTemp()) {
               ctx.depends_on[op.tempId()] = true;
               if (op.isFirstKill())
                  ctx.RAR_dependencies[op.tempId()] = true;
            }
         }
         continue;
      }

      bool register_pressure_unknown = false;
      /* check if one of candidate's operands is killed by depending instruction */
      for (const Operand& op : candidate->operands) {
         if (op.isTemp() && ctx.RAR_dependencies[op.tempId()]) {
            // FIXME: account for difference in register pressure
            register_pressure_unknown = true;
         }
      }
      if (register_pressure_unknown) {
         for (const Operand& op : candidate->operands) {
            if (op.isTemp()) {
               ctx.depends_on[op.tempId()] = true;
               if (op.isFirstKill())
                  ctx.RAR_dependencies[op.tempId()] = true;
            }
         }
         continue;
      }

      /* check if register pressure is low enough: the diff is negative if register pressure is increased */
      const RegisterDemand candidate_diff = getLiveChanges(candidate);
      const RegisterDemand temp = getTempRegisters(candidate);;
      if (RegisterDemand(register_pressure - candidate_diff).exceeds(ctx.max_registers))
         break;
      const RegisterDemand temp2 = getTempRegisters(block->instructions[insert_idx - 1]);
      const RegisterDemand new_demand = register_demand[insert_idx - 1] - temp2 + temp;
      if (new_demand.exceeds(ctx.max_registers))
         break;
      // TODO: we might want to look further to find a sequence of instructions to move down which doesn't exceed reg pressure

      /* move the candidate below the export */
      move_element(block->instructions, candidate_idx, insert_idx);

      /* update register pressure */
      move_element(register_demand, candidate_idx, insert_idx);
      for (int i = candidate_idx; i < insert_idx - 1; i++) {
         register_demand[i] -= candidate_diff;
      }
      register_demand[insert_idx - 1] = new_demand;
      register_pressure -=  candidate_diff;
      insert_idx--;
      k++;
   }
}

void schedule_block(sched_ctx& ctx, Program *program, Block* block, live& live_vars)
{
   ctx.last_SMEM_dep_idx = 0;
   ctx.last_SMEM_stall = INT16_MIN;

   /* go through all instructions and find memory loads */
   for (unsigned idx = 0; idx < block->instructions.size(); idx++) {
      Instruction* current = block->instructions[idx].get();

      if (current->definitions.empty())
         continue;

      if (current->isVMEM())
         schedule_VMEM(ctx, block, live_vars.register_demand[block->index], current, idx);
      if (current->format == Format::SMEM)
         schedule_SMEM(ctx, block, live_vars.register_demand[block->index], current, idx);
   }

   if ((program->stage & hw_vs) && block->index == program->blocks.size() - 1) {
      /* Try to move position exports as far up as possible, to reduce register
       * usage and because ISA reference guides say so. */
      for (unsigned idx = 0; idx < block->instructions.size(); idx++) {
         Instruction* current = block->instructions[idx].get();

         if (current->format == Format::EXP) {
            unsigned target = static_cast<Export_instruction*>(current)->dest;
            if (target >= V_008DFC_SQ_EXP_POS && target < V_008DFC_SQ_EXP_PARAM)
               schedule_position_export(ctx, block, live_vars.register_demand[block->index], current, idx);
         }
      }
   }

   /* resummarize the block's register demand */
   block->register_demand = RegisterDemand();
   for (unsigned idx = 0; idx < block->instructions.size(); idx++) {
      block->register_demand.update(live_vars.register_demand[block->index][idx]);
   }
}


void schedule_program(Program *program, live& live_vars)
{
   sched_ctx ctx;
   ctx.depends_on.resize(program->peekAllocationId());
   ctx.RAR_dependencies.resize(program->peekAllocationId());
   ctx.new_RAR_dependencies.resize(program->peekAllocationId());
   /* Allowing the scheduler to reduce the number of waves to as low as 5
    * improves performance of Thrones of Britannia significantly and doesn't
    * seem to hurt anything else. */
   if (program->num_waves <= 5)
      ctx.num_waves = program->num_waves;
   else if (program->max_reg_demand.vgpr >= 32)
      ctx.num_waves = 5;
   else if (program->max_reg_demand.vgpr >= 28)
      ctx.num_waves = 6;
   else if (program->max_reg_demand.vgpr >= 24)
      ctx.num_waves = 7;
   else
      ctx.num_waves = 8;

   assert(ctx.num_waves > 0 && ctx.num_waves <= program->num_waves);
   ctx.max_registers = { int16_t(((256 / ctx.num_waves) & ~3) - 2), int16_t(get_addr_sgpr_from_waves(program, ctx.num_waves))};

   for (Block& block : program->blocks)
      schedule_block(ctx, program, &block, live_vars);

   /* update max_reg_demand and num_waves */
   RegisterDemand new_demand;
   for (Block& block : program->blocks) {
      new_demand.update(block.register_demand);
   }
   update_vgpr_sgpr_demand(program, new_demand);

   /* if enabled, this code asserts that register_demand is updated correctly */
   #if 0
   int prev_num_waves = program->num_waves;
   const RegisterDemand prev_max_demand = program->max_reg_demand;

   std::vector<RegisterDemand> demands(program->blocks.size());
   for (unsigned j = 0; j < program->blocks.size(); j++) {
      demands[j] = program->blocks[j].register_demand;
   }

   struct radv_nir_compiler_options options;
   options.chip_class = program->chip_class;
   live live_vars2 = aco::live_var_analysis(program, &options);

   for (unsigned j = 0; j < program->blocks.size(); j++) {
      Block &b = program->blocks[j];
      for (unsigned i = 0; i < b.instructions.size(); i++)
         assert(live_vars.register_demand[b.index][i] == live_vars2.register_demand[b.index][i]);
      assert(b.register_demand == demands[j]);
   }

   assert(program->max_reg_demand == prev_max_demand);
   assert(program->num_waves == prev_num_waves);
   #endif
}

}
