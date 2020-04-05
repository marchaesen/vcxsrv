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
#include "aco_builder.h"
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

enum MoveResult {
   move_success,
   move_fail_ssa,
   move_fail_rar,
   move_fail_pressure,
};

struct MoveState {
   RegisterDemand max_registers;

   Block *block;
   Instruction *current;
   RegisterDemand *register_demand;
   bool improved_rar;

   std::vector<bool> depends_on;
   /* Two are needed because, for downwards VMEM scheduling, one needs to
    * exclude the instructions in the clause, since new instructions in the
    * clause are not moved past any other instructions in the clause. */
   std::vector<bool> RAR_dependencies;
   std::vector<bool> RAR_dependencies_clause;

   int source_idx;
   int insert_idx, insert_idx_clause;
   RegisterDemand total_demand, total_demand_clause;

   /* for moving instructions before the current instruction to after it */
   void downwards_init(int current_idx, bool improved_rar, bool may_form_clauses);
   MoveResult downwards_move(bool clause);
   void downwards_skip();

   /* for moving instructions after the first use of the current instruction upwards */
   void upwards_init(int source_idx, bool improved_rar);
   bool upwards_check_deps();
   void upwards_set_insert_idx(int before);
   MoveResult upwards_move();
   void upwards_skip();

private:
   void downwards_advance_helper();
};

struct sched_ctx {
   int16_t num_waves;
   int16_t last_SMEM_stall;
   int last_SMEM_dep_idx;
   MoveState mv;
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
void move_element(T begin_it, size_t idx, size_t before) {
    if (idx < before) {
        auto begin = std::next(begin_it, idx);
        auto end = std::next(begin_it, before);
        std::rotate(begin, begin + 1, end);
    } else if (idx > before) {
        auto begin = std::next(begin_it, before);
        auto end = std::next(begin_it, idx + 1);
        std::rotate(begin, end - 1, end);
    }
}

void MoveState::downwards_advance_helper()
{
   source_idx--;
   total_demand.update(register_demand[source_idx]);
}

void MoveState::downwards_init(int current_idx, bool improved_rar_, bool may_form_clauses)
{
   improved_rar = improved_rar_;
   source_idx = current_idx;

   insert_idx = current_idx + 1;
   insert_idx_clause = current_idx;

   total_demand = total_demand_clause = register_demand[current_idx];

   std::fill(depends_on.begin(), depends_on.end(), false);
   if (improved_rar) {
      std::fill(RAR_dependencies.begin(), RAR_dependencies.end(), false);
      if (may_form_clauses)
         std::fill(RAR_dependencies_clause.begin(), RAR_dependencies_clause.end(), false);
   }

   for (const Operand& op : current->operands) {
      if (op.isTemp()) {
         depends_on[op.tempId()] = true;
         if (improved_rar && op.isFirstKill())
            RAR_dependencies[op.tempId()] = true;
      }
   }

   /* update total_demand/source_idx */
   downwards_advance_helper();
}

MoveResult MoveState::downwards_move(bool clause)
{
   aco_ptr<Instruction>& instr = block->instructions[source_idx];

   for (const Definition& def : instr->definitions)
      if (def.isTemp() && depends_on[def.tempId()])
         return move_fail_ssa;

   /* check if one of candidate's operands is killed by depending instruction */
   std::vector<bool>& RAR_deps = improved_rar ? (clause ? RAR_dependencies_clause : RAR_dependencies) : depends_on;
   for (const Operand& op : instr->operands) {
      if (op.isTemp() && RAR_deps[op.tempId()]) {
         // FIXME: account for difference in register pressure
         return move_fail_rar;
      }
   }

   if (clause) {
      for (const Operand& op : instr->operands) {
         if (op.isTemp()) {
            depends_on[op.tempId()] = true;
            if (op.isFirstKill())
               RAR_dependencies[op.tempId()] = true;
         }
      }
   }

   int dest_insert_idx = clause ? insert_idx_clause : insert_idx;
   RegisterDemand register_pressure = clause ? total_demand_clause : total_demand;

   const RegisterDemand candidate_diff = get_live_changes(instr);
   const RegisterDemand temp = get_temp_registers(instr);
   if (RegisterDemand(register_pressure - candidate_diff).exceeds(max_registers))
      return move_fail_pressure;
   const RegisterDemand temp2 = get_temp_registers(block->instructions[dest_insert_idx - 1]);
   const RegisterDemand new_demand = register_demand[dest_insert_idx - 1] - temp2 + temp;
   if (new_demand.exceeds(max_registers))
      return move_fail_pressure;

   /* move the candidate below the memory load */
   move_element(block->instructions.begin(), source_idx, dest_insert_idx);

   /* update register pressure */
   move_element(register_demand, source_idx, dest_insert_idx);
   for (int i = source_idx; i < dest_insert_idx - 1; i++)
      register_demand[i] -= candidate_diff;
   register_demand[dest_insert_idx - 1] = new_demand;
   total_demand_clause -= candidate_diff;
   insert_idx_clause--;
   if (!clause) {
      total_demand -= candidate_diff;
      insert_idx--;
   }

   downwards_advance_helper();
   return move_success;
}

void MoveState::downwards_skip()
{
   aco_ptr<Instruction>& instr = block->instructions[source_idx];

   for (const Operand& op : instr->operands) {
      if (op.isTemp()) {
         depends_on[op.tempId()] = true;
         if (improved_rar && op.isFirstKill()) {
            RAR_dependencies[op.tempId()] = true;
            RAR_dependencies_clause[op.tempId()] = true;
         }
      }
   }
   total_demand_clause.update(register_demand[source_idx]);

   downwards_advance_helper();
}

void MoveState::upwards_init(int source_idx_, bool improved_rar_)
{
   source_idx = source_idx_;
   improved_rar = improved_rar_;

   insert_idx = -1;

   std::fill(depends_on.begin(), depends_on.end(), false);
   std::fill(RAR_dependencies.begin(), RAR_dependencies.end(), false);

   for (const Definition& def : current->definitions) {
      if (def.isTemp())
         depends_on[def.tempId()] = true;
   }
}

bool MoveState::upwards_check_deps()
{
   aco_ptr<Instruction>& instr = block->instructions[source_idx];
   for (const Operand& op : instr->operands) {
      if (op.isTemp() && depends_on[op.tempId()])
         return false;
   }
   return true;
}

void MoveState::upwards_set_insert_idx(int before)
{
   insert_idx = before;
   total_demand = register_demand[before - 1];
}

MoveResult MoveState::upwards_move()
{
   assert(insert_idx >= 0);

   aco_ptr<Instruction>& instr = block->instructions[source_idx];
   for (const Operand& op : instr->operands) {
      if (op.isTemp() && depends_on[op.tempId()])
         return move_fail_ssa;
   }

   /* check if candidate uses/kills an operand which is used by a dependency */
   for (const Operand& op : instr->operands) {
      if (op.isTemp() && (!improved_rar || op.isFirstKill()) && RAR_dependencies[op.tempId()])
         return move_fail_rar;
   }

   /* check if register pressure is low enough: the diff is negative if register pressure is decreased */
   const RegisterDemand candidate_diff = get_live_changes(instr);
   const RegisterDemand temp = get_temp_registers(instr);
   if (RegisterDemand(total_demand + candidate_diff).exceeds(max_registers))
      return move_fail_pressure;
   const RegisterDemand temp2 = get_temp_registers(block->instructions[insert_idx - 1]);
   const RegisterDemand new_demand = register_demand[insert_idx - 1] - temp2 + candidate_diff + temp;
   if (new_demand.exceeds(max_registers))
      return move_fail_pressure;

   /* move the candidate above the insert_idx */
   move_element(block->instructions.begin(), source_idx, insert_idx);

   /* update register pressure */
   move_element(register_demand, source_idx, insert_idx);
   for (int i = insert_idx + 1; i <= source_idx; i++)
      register_demand[i] += candidate_diff;
   register_demand[insert_idx] = new_demand;
   total_demand += candidate_diff;

   insert_idx++;

   total_demand.update(register_demand[source_idx]);
   source_idx++;

   return move_success;
}

void MoveState::upwards_skip()
{
   if (insert_idx >= 0) {
      aco_ptr<Instruction>& instr = block->instructions[source_idx];
      for (const Definition& def : instr->definitions) {
         if (def.isTemp())
            depends_on[def.tempId()] = true;
      }
      for (const Operand& op : instr->operands) {
         if (op.isTemp())
            RAR_dependencies[op.tempId()] = true;
      }
      total_demand.update(register_demand[source_idx]);
   }

   source_idx++;
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
      return static_cast<FLAT_instruction*>(candidate)->can_reorder;
   default:
      return true;
   }
}

bool is_gs_or_done_sendmsg(Instruction *instr)
{
   if (instr->opcode == aco_opcode::s_sendmsg) {
      uint16_t imm = static_cast<SOPP_instruction*>(instr)->imm;
      return (imm & sendmsg_id_mask) == _sendmsg_gs ||
             (imm & sendmsg_id_mask) == _sendmsg_gs_done;
   }
   return false;
}

bool is_done_sendmsg(Instruction *instr)
{
   if (instr->opcode == aco_opcode::s_sendmsg) {
      uint16_t imm = static_cast<SOPP_instruction*>(instr)->imm;
      return (imm & sendmsg_id_mask) == _sendmsg_gs_done;
   }
   return false;
}

barrier_interaction get_barrier_interaction(Instruction* instr)
{
   switch (instr->format) {
   case Format::SMEM:
      return static_cast<SMEM_instruction*>(instr)->barrier;
   case Format::MUBUF:
      return static_cast<MUBUF_instruction*>(instr)->barrier;
   case Format::MIMG:
      return static_cast<MIMG_instruction*>(instr)->barrier;
   case Format::MTBUF:
      return static_cast<MTBUF_instruction*>(instr)->barrier;
   case Format::FLAT:
   case Format::GLOBAL:
   case Format::SCRATCH:
      return static_cast<FLAT_instruction*>(instr)->barrier;
   case Format::DS:
      return barrier_shared;
   case Format::SOPP:
      if (is_done_sendmsg(instr))
         return (barrier_interaction)(barrier_gs_data | barrier_gs_sendmsg);
      else if (is_gs_or_done_sendmsg(instr))
         return barrier_gs_sendmsg;
      else
         return barrier_none;
   case Format::PSEUDO_BARRIER:
      return barrier_barrier;
   default:
      return barrier_none;
   }
}

barrier_interaction parse_barrier(Instruction *instr)
{
   if (instr->format == Format::PSEUDO_BARRIER) {
      switch (instr->opcode) {
      case aco_opcode::p_memory_barrier_atomic:
         return barrier_atomic;
      /* For now, buffer and image barriers are treated the same. this is because of
       * dEQP-VK.memory_model.message_passing.core11.u32.coherent.fence_fence.atomicwrite.device.payload_nonlocal.buffer.guard_nonlocal.image.comp
       * which seems to use an image load to determine if the result of a buffer load is valid. So the ordering of the two loads is important.
       * I /think/ we should probably eventually expand the meaning of a buffer barrier so that all buffer operations before it, must stay before it
       * and that both image and buffer operations after it, must stay after it. We should also do the same for image barriers.
       * Or perhaps the problem is that we don't have a combined barrier instruction for both buffers and images, but the CTS test expects us to?
       * Either way, this solution should work. */
      case aco_opcode::p_memory_barrier_buffer:
      case aco_opcode::p_memory_barrier_image:
         return (barrier_interaction)(barrier_image | barrier_buffer);
      case aco_opcode::p_memory_barrier_shared:
         return barrier_shared;
      case aco_opcode::p_memory_barrier_common:
         return (barrier_interaction)(barrier_image | barrier_buffer | barrier_shared | barrier_atomic);
      case aco_opcode::p_memory_barrier_gs_data:
         return barrier_gs_data;
      case aco_opcode::p_memory_barrier_gs_sendmsg:
         return barrier_gs_sendmsg;
      default:
         break;
      }
   } else if (instr->opcode == aco_opcode::s_barrier) {
      return (barrier_interaction)(barrier_barrier | barrier_image | barrier_buffer | barrier_shared | barrier_atomic);
   }
   return barrier_none;
}

struct hazard_query {
   bool contains_spill;
   int barriers;
   int barrier_interaction;
   bool can_reorder_vmem;
   bool can_reorder_smem;
};

void init_hazard_query(hazard_query *query) {
   query->contains_spill = false;
   query->barriers = 0;
   query->barrier_interaction = 0;
   query->can_reorder_vmem = true;
   query->can_reorder_smem = true;
}

void add_to_hazard_query(hazard_query *query, Instruction *instr)
{
   query->barriers |= parse_barrier(instr);
   query->barrier_interaction |= get_barrier_interaction(instr);
   if (instr->opcode == aco_opcode::p_spill || instr->opcode == aco_opcode::p_reload)
      query->contains_spill = true;

   bool can_reorder_instr = can_reorder(instr);
   query->can_reorder_smem &= instr->format != Format::SMEM || can_reorder_instr;
   query->can_reorder_vmem &= !(instr->isVMEM() || instr->isFlatOrGlobal()) || can_reorder_instr;
}

enum HazardResult {
   hazard_success,
   hazard_fail_reorder_vmem_smem,
   hazard_fail_reorder_ds,
   hazard_fail_reorder_sendmsg,
   hazard_fail_spill,
   hazard_fail_export,
   hazard_fail_barrier,
   /* Must stop at these failures. The hazard query code doesn't consider them
    * when added. */
   hazard_fail_exec,
   hazard_fail_memtime,
};

HazardResult perform_hazard_query(hazard_query *query, Instruction *instr)
{
   bool can_reorder_candidate = can_reorder(instr);

   if (instr->opcode == aco_opcode::p_exit_early_if)
      return hazard_fail_exec;
   for (const Definition& def : instr->definitions) {
      if (def.isFixed() && def.physReg() == exec)
         return hazard_fail_exec;
   }

   /* don't move exports so that they stay closer together */
   if (instr->format == Format::EXP)
      return hazard_fail_export;

   /* don't move s_memtime/s_memrealtime */
   if (instr->opcode == aco_opcode::s_memtime || instr->opcode == aco_opcode::s_memrealtime)
      return hazard_fail_memtime;

   if (query->barrier_interaction && (query->barrier_interaction & parse_barrier(instr)))
      return hazard_fail_barrier;
   if (query->barriers && (query->barriers & get_barrier_interaction(instr)))
      return hazard_fail_barrier;

   if (!query->can_reorder_smem && instr->format == Format::SMEM && !can_reorder_candidate)
      return hazard_fail_reorder_vmem_smem;
   if (!query->can_reorder_vmem && (instr->isVMEM() || instr->isFlatOrGlobal()) && !can_reorder_candidate)
      return hazard_fail_reorder_vmem_smem;
   if ((query->barrier_interaction & barrier_shared) && instr->format == Format::DS)
      return hazard_fail_reorder_ds;
   if (is_gs_or_done_sendmsg(instr) && (query->barrier_interaction & get_barrier_interaction(instr)))
      return hazard_fail_reorder_sendmsg;

   if ((instr->opcode == aco_opcode::p_spill || instr->opcode == aco_opcode::p_reload) &&
       query->contains_spill)
      return hazard_fail_spill;

   return hazard_success;
}

void schedule_SMEM(sched_ctx& ctx, Block* block,
                   std::vector<RegisterDemand>& register_demand,
                   Instruction* current, int idx)
{
   assert(idx != 0);
   int window_size = SMEM_WINDOW_SIZE;
   int max_moves = SMEM_MAX_MOVES;
   int16_t k = 0;

   /* don't move s_memtime/s_memrealtime */
   if (current->opcode == aco_opcode::s_memtime || current->opcode == aco_opcode::s_memrealtime)
      return;

   /* first, check if we have instructions before current to move down */
   hazard_query hq;
   init_hazard_query(&hq);
   add_to_hazard_query(&hq, current);

   ctx.mv.downwards_init(idx, false, false);

   for (int candidate_idx = idx - 1; k < max_moves && candidate_idx > (int) idx - window_size; candidate_idx--) {
      assert(candidate_idx >= 0);
      assert(candidate_idx == ctx.mv.source_idx);
      aco_ptr<Instruction>& candidate = block->instructions[candidate_idx];

      /* break if we'd make the previous SMEM instruction stall */
      bool can_stall_prev_smem = idx <= ctx.last_SMEM_dep_idx && candidate_idx < ctx.last_SMEM_dep_idx;
      if (can_stall_prev_smem && ctx.last_SMEM_stall >= 0)
         break;

      /* break when encountering another MEM instruction, logical_start or barriers */
      if (candidate->opcode == aco_opcode::p_logical_start)
         break;
      if (candidate->isVMEM())
         break;

      bool can_move_down = true;

      HazardResult haz = perform_hazard_query(&hq, candidate.get());
      if (haz == hazard_fail_reorder_ds || haz == hazard_fail_spill || haz == hazard_fail_reorder_sendmsg || haz == hazard_fail_barrier || haz == hazard_fail_export)
         can_move_down = false;
      else if (haz != hazard_success)
         break;

      /* don't use LDS/GDS instructions to hide latency since it can
       * significanly worsen LDS scheduling */
      if (candidate->format == Format::DS || !can_move_down) {
         add_to_hazard_query(&hq, candidate.get());
         ctx.mv.downwards_skip();
         continue;
      }

      MoveResult res = ctx.mv.downwards_move(false);
      if (res == move_fail_ssa || res == move_fail_rar) {
         add_to_hazard_query(&hq, candidate.get());
         ctx.mv.downwards_skip();
         continue;
      } else if (res == move_fail_pressure) {
         break;
      }

      if (candidate_idx < ctx.last_SMEM_dep_idx)
         ctx.last_SMEM_stall++;
      k++;
   }

   /* find the first instruction depending on current or find another MEM */
   ctx.mv.upwards_init(idx + 1, false);

   bool found_dependency = false;
   /* second, check if we have instructions after current to move up */
   for (int candidate_idx = idx + 1; k < max_moves && candidate_idx < (int) idx + window_size; candidate_idx++) {
      assert(candidate_idx == ctx.mv.source_idx);
      assert(candidate_idx < (int) block->instructions.size());
      aco_ptr<Instruction>& candidate = block->instructions[candidate_idx];

      if (candidate->opcode == aco_opcode::p_logical_end)
         break;

      /* check if candidate depends on current */
      bool is_dependency = !found_dependency && !ctx.mv.upwards_check_deps();
      /* no need to steal from following VMEM instructions */
      if (is_dependency && candidate->isVMEM())
         break;

      if (found_dependency) {
         HazardResult haz = perform_hazard_query(&hq, candidate.get());
         if (haz == hazard_fail_reorder_ds || haz == hazard_fail_spill ||
             haz == hazard_fail_reorder_sendmsg || haz == hazard_fail_barrier ||
             haz == hazard_fail_export)
            is_dependency = true;
         else if (haz != hazard_success)
            break;
      }

      if (is_dependency) {
         if (!found_dependency) {
            ctx.mv.upwards_set_insert_idx(candidate_idx);
            init_hazard_query(&hq);
            found_dependency = true;
         }
      }

      if (is_dependency || !found_dependency) {
         if (found_dependency)
            add_to_hazard_query(&hq, candidate.get());
         else
            k++;
         ctx.mv.upwards_skip();
         continue;
      }

      MoveResult res = ctx.mv.upwards_move();
      if (res == move_fail_ssa || res == move_fail_rar) {
         /* no need to steal from following VMEM instructions */
         if (res == move_fail_ssa && candidate->isVMEM())
            break;
         add_to_hazard_query(&hq, candidate.get());
         ctx.mv.upwards_skip();
         continue;
      } else if (res == move_fail_pressure) {
         break;
      }
      k++;
   }

   ctx.last_SMEM_dep_idx = found_dependency ? ctx.mv.insert_idx : 0;
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

   /* first, check if we have instructions before current to move down */
   hazard_query indep_hq;
   hazard_query clause_hq;
   init_hazard_query(&indep_hq);
   init_hazard_query(&clause_hq);
   add_to_hazard_query(&indep_hq, current);

   ctx.mv.downwards_init(idx, true, true);

   for (int candidate_idx = idx - 1; k < max_moves && candidate_idx > (int) idx - window_size; candidate_idx--) {
      assert(candidate_idx == ctx.mv.source_idx);
      assert(candidate_idx >= 0);
      aco_ptr<Instruction>& candidate = block->instructions[candidate_idx];
      bool is_vmem = candidate->isVMEM() || candidate->isFlatOrGlobal();

      /* break when encountering another VMEM instruction, logical_start or barriers */
      if (candidate->opcode == aco_opcode::p_logical_start)
         break;

      /* break if we'd make the previous SMEM instruction stall */
      bool can_stall_prev_smem = idx <= ctx.last_SMEM_dep_idx && candidate_idx < ctx.last_SMEM_dep_idx;
      if (can_stall_prev_smem && ctx.last_SMEM_stall >= 0)
         break;

      bool part_of_clause = false;
      if (current->isVMEM() == candidate->isVMEM()) {
         bool same_resource = true;
         if (current->isVMEM())
            same_resource = candidate->operands[0].tempId() == current->operands[0].tempId();
         int grab_dist = ctx.mv.insert_idx_clause - candidate_idx;
         /* We can't easily tell how much this will decrease the def-to-use
          * distances, so just use how far it will be moved as a heuristic. */
         part_of_clause = same_resource && grab_dist < clause_max_grab_dist;
      }

      /* if current depends on candidate, add additional dependencies and continue */
      bool can_move_down = !is_vmem || part_of_clause;

      HazardResult haz = perform_hazard_query(part_of_clause ? &clause_hq : &indep_hq, candidate.get());
      if (haz == hazard_fail_reorder_ds || haz == hazard_fail_spill ||
          haz == hazard_fail_reorder_sendmsg || haz == hazard_fail_barrier ||
          haz == hazard_fail_export)
         can_move_down = false;
      else if (haz != hazard_success)
         break;

      if (!can_move_down) {
         add_to_hazard_query(&indep_hq, candidate.get());
         add_to_hazard_query(&clause_hq, candidate.get());
         ctx.mv.downwards_skip();
         continue;
      }

      MoveResult res = ctx.mv.downwards_move(part_of_clause);
      if (res == move_fail_ssa || res == move_fail_rar) {
         add_to_hazard_query(&indep_hq, candidate.get());
         add_to_hazard_query(&clause_hq, candidate.get());
         ctx.mv.downwards_skip();
         continue;
      } else if (res == move_fail_pressure) {
         break;
      }
      k++;
      if (candidate_idx < ctx.last_SMEM_dep_idx)
         ctx.last_SMEM_stall++;
   }

   /* find the first instruction depending on current or find another VMEM */
   ctx.mv.upwards_init(idx + 1, true);

   bool found_dependency = false;
   /* second, check if we have instructions after current to move up */
   for (int candidate_idx = idx + 1; k < max_moves && candidate_idx < (int) idx + window_size; candidate_idx++) {
      assert(candidate_idx == ctx.mv.source_idx);
      assert(candidate_idx < (int) block->instructions.size());
      aco_ptr<Instruction>& candidate = block->instructions[candidate_idx];
      bool is_vmem = candidate->isVMEM() || candidate->isFlatOrGlobal();

      if (candidate->opcode == aco_opcode::p_logical_end)
         break;

      /* check if candidate depends on current */
      bool is_dependency = false;
      if (found_dependency) {
         HazardResult haz = perform_hazard_query(&indep_hq, candidate.get());
         if (haz == hazard_fail_reorder_ds || haz == hazard_fail_spill ||
             haz == hazard_fail_reorder_vmem_smem || haz == hazard_fail_reorder_sendmsg ||
             haz == hazard_fail_barrier || haz == hazard_fail_export)
            is_dependency = true;
         else if (haz != hazard_success)
            break;
      }

      is_dependency |= !found_dependency && !ctx.mv.upwards_check_deps();
      if (is_dependency) {
         if (!found_dependency) {
            ctx.mv.upwards_set_insert_idx(candidate_idx);
            init_hazard_query(&indep_hq);
            found_dependency = true;
         }
      } else if (is_vmem) {
         /* don't move up dependencies of other VMEM instructions */
         for (const Definition& def : candidate->definitions) {
            if (def.isTemp())
               ctx.mv.depends_on[def.tempId()] = true;
         }
      }

      if (is_dependency || !found_dependency) {
         if (found_dependency)
            add_to_hazard_query(&indep_hq, candidate.get());
         ctx.mv.upwards_skip();
         continue;
      }

      MoveResult res = ctx.mv.upwards_move();
      if (res == move_fail_ssa || res == move_fail_rar) {
         add_to_hazard_query(&indep_hq, candidate.get());
         ctx.mv.upwards_skip();
         continue;
      } else if (res == move_fail_pressure) {
         break;
      }
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

   ctx.mv.downwards_init(idx, true, false);

   hazard_query hq;
   init_hazard_query(&hq);
   add_to_hazard_query(&hq, current);

   for (int candidate_idx = idx - 1; k < max_moves && candidate_idx > (int) idx - window_size; candidate_idx--) {
      assert(candidate_idx >= 0);
      aco_ptr<Instruction>& candidate = block->instructions[candidate_idx];

      if (candidate->opcode == aco_opcode::p_logical_start)
         break;
      if (candidate->isVMEM() || candidate->format == Format::SMEM || candidate->isFlatOrGlobal())
         break;

      HazardResult haz = perform_hazard_query(&hq, candidate.get());
      if (haz == hazard_fail_exec || haz == hazard_fail_memtime)
         break;

      if (haz != hazard_success) {
         add_to_hazard_query(&hq, candidate.get());
         ctx.mv.downwards_skip();
         continue;
      }

      MoveResult res = ctx.mv.downwards_move(false);
      if (res == move_fail_ssa || res == move_fail_rar) {
         add_to_hazard_query(&hq, candidate.get());
         ctx.mv.downwards_skip();
         continue;
      } else if (res == move_fail_pressure) {
         break;
      }
      k++;
   }
}

void schedule_block(sched_ctx& ctx, Program *program, Block* block, live& live_vars)
{
   ctx.last_SMEM_dep_idx = 0;
   ctx.last_SMEM_stall = INT16_MIN;
   ctx.mv.block = block;
   ctx.mv.register_demand = live_vars.register_demand[block->index].data();

   /* go through all instructions and find memory loads */
   for (unsigned idx = 0; idx < block->instructions.size(); idx++) {
      Instruction* current = block->instructions[idx].get();

      if (current->definitions.empty())
         continue;

      if (current->isVMEM() || current->isFlatOrGlobal()) {
         ctx.mv.current = current;
         schedule_VMEM(ctx, block, live_vars.register_demand[block->index], current, idx);
      }

      if (current->format == Format::SMEM) {
         ctx.mv.current = current;
         schedule_SMEM(ctx, block, live_vars.register_demand[block->index], current, idx);
      }
   }

   if ((program->stage & hw_vs) && block->index == program->blocks.size() - 1) {
      /* Try to move position exports as far up as possible, to reduce register
       * usage and because ISA reference guides say so. */
      for (unsigned idx = 0; idx < block->instructions.size(); idx++) {
         Instruction* current = block->instructions[idx].get();

         if (current->format == Format::EXP) {
            unsigned target = static_cast<Export_instruction*>(current)->dest;
            if (target >= V_008DFC_SQ_EXP_POS && target < V_008DFC_SQ_EXP_PARAM) {
               ctx.mv.current = current;
               schedule_position_export(ctx, block, live_vars.register_demand[block->index], current, idx);
            }
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
   ctx.mv.depends_on.resize(program->peekAllocationId());
   ctx.mv.RAR_dependencies.resize(program->peekAllocationId());
   ctx.mv.RAR_dependencies_clause.resize(program->peekAllocationId());
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
   ctx.num_waves = std::max<uint16_t>(ctx.num_waves, program->min_waves);

   assert(ctx.num_waves > 0 && ctx.num_waves <= program->num_waves);
   ctx.mv.max_registers = { int16_t(get_addr_vgpr_from_waves(program, ctx.num_waves) - 2),
                            int16_t(get_addr_sgpr_from_waves(program, ctx.num_waves))};

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
