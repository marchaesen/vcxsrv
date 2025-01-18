/*
 * Copyright © 2020 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "aco_ir.h"

#include "util/crc32.h"

#include <algorithm>
#include <deque>
#include <set>
#include <vector>

namespace aco {

namespace {

class BlockCycleEstimator {
public:
   enum resource {
      null = 0,
      scalar,
      branch_sendmsg,
      valu,
      valu_complex,
      lds,
      export_gds,
      vmem,
      resource_count,
   };

   BlockCycleEstimator(Program* program_) : program(program_) {}

   Program* program;

   int32_t cur_cycle = 0;
   int32_t res_available[(int)BlockCycleEstimator::resource_count] = {0};
   unsigned res_usage[(int)BlockCycleEstimator::resource_count] = {0};
   int32_t reg_available[512] = {0};
   std::deque<int32_t> mem_ops[wait_type_num];

   void add(aco_ptr<Instruction>& instr);
   void join(const BlockCycleEstimator& other);

private:
   unsigned get_waitcnt_cost(wait_imm imm);
   unsigned get_dependency_cost(aco_ptr<Instruction>& instr);

   void use_resources(aco_ptr<Instruction>& instr);
   int32_t cycles_until_res_available(aco_ptr<Instruction>& instr);
};

struct perf_info {
   int latency;

   BlockCycleEstimator::resource rsrc0;
   unsigned cost0;

   BlockCycleEstimator::resource rsrc1;
   unsigned cost1;
};

static bool
is_dual_issue_capable(const Program& program, const Instruction& instr)
{
   if (program.gfx_level < GFX11 || !instr.isVALU() || instr.isDPP())
      return false;

   switch (instr.opcode) {
   case aco_opcode::v_fma_f32:
   case aco_opcode::v_fmac_f32:
   case aco_opcode::v_fmaak_f32:
   case aco_opcode::v_fmamk_f32:
   case aco_opcode::v_mul_f32:
   case aco_opcode::v_add_f32:
   case aco_opcode::v_sub_f32:
   case aco_opcode::v_subrev_f32:
   case aco_opcode::v_mul_legacy_f32:
   case aco_opcode::v_fma_legacy_f32:
   case aco_opcode::v_fmac_legacy_f32:
   case aco_opcode::v_fma_f16:
   case aco_opcode::v_fmac_f16:
   case aco_opcode::v_fmaak_f16:
   case aco_opcode::v_fmamk_f16:
   case aco_opcode::v_mul_f16:
   case aco_opcode::v_add_f16:
   case aco_opcode::v_sub_f16:
   case aco_opcode::v_subrev_f16:
   case aco_opcode::v_mov_b32:
   case aco_opcode::v_movreld_b32:
   case aco_opcode::v_movrels_b32:
   case aco_opcode::v_movrelsd_b32:
   case aco_opcode::v_movrelsd_2_b32:
   case aco_opcode::v_cndmask_b32:
   case aco_opcode::v_writelane_b32_e64:
   case aco_opcode::v_mov_b16:
   case aco_opcode::v_cndmask_b16:
   case aco_opcode::v_max_f32:
   case aco_opcode::v_min_f32:
   case aco_opcode::v_max_f16:
   case aco_opcode::v_min_f16:
   case aco_opcode::v_max_i16_e64:
   case aco_opcode::v_min_i16_e64:
   case aco_opcode::v_max_u16_e64:
   case aco_opcode::v_min_u16_e64:
   case aco_opcode::v_add_i16:
   case aco_opcode::v_sub_i16:
   case aco_opcode::v_mad_i16:
   case aco_opcode::v_add_u16_e64:
   case aco_opcode::v_sub_u16_e64:
   case aco_opcode::v_mad_u16:
   case aco_opcode::v_mul_lo_u16_e64:
   case aco_opcode::v_not_b16:
   case aco_opcode::v_and_b16:
   case aco_opcode::v_or_b16:
   case aco_opcode::v_xor_b16:
   case aco_opcode::v_lshrrev_b16_e64:
   case aco_opcode::v_ashrrev_i16_e64:
   case aco_opcode::v_lshlrev_b16_e64:
   case aco_opcode::v_dot2_bf16_bf16:
   case aco_opcode::v_dot2_f32_bf16:
   case aco_opcode::v_dot2_f16_f16:
   case aco_opcode::v_dot2_f32_f16:
   case aco_opcode::v_dot2c_f32_f16: return true;
   case aco_opcode::v_fma_mix_f32:
   case aco_opcode::v_fma_mixlo_f16:
   case aco_opcode::v_fma_mixhi_f16: {
      /* dst and acc type must match */
      if (instr.valu().opsel_hi[2] == (instr.opcode == aco_opcode::v_fma_mix_f32))
         return false;

      /* If all operands are vgprs, two must be the same. */
      for (unsigned i = 0; i < 3; i++) {
         if (instr.operands[i].isConstant() || instr.operands[i].isOfType(RegType::sgpr))
            return true;
         for (unsigned j = 0; j < i; j++) {
            if (instr.operands[i].physReg() == instr.operands[j].physReg())
               return true;
         }
      }
      return false;
   }
   default: return false;
   }
}

static perf_info
get_perf_info(const Program& program, const Instruction& instr)
{
   instr_class cls = instr_info.classes[(int)instr.opcode];

#define WAIT(res)          BlockCycleEstimator::res, 0
#define WAIT_USE(res, cnt) BlockCycleEstimator::res, cnt

   if (program.gfx_level >= GFX10) {
      /* fp64 might be incorrect */
      switch (cls) {
      case instr_class::valu32:
      case instr_class::valu_convert32:
      case instr_class::valu_fma: return {5, WAIT_USE(valu, 1)};
      case instr_class::valu64: return {6, WAIT_USE(valu, 2), WAIT_USE(valu_complex, 2)};
      case instr_class::valu_quarter_rate32:
         return {8, WAIT_USE(valu, 4), WAIT_USE(valu_complex, 4)};
      case instr_class::valu_transcendental32:
         return {10, WAIT_USE(valu, 1), WAIT_USE(valu_complex, 4)};
      case instr_class::valu_double: return {22, WAIT_USE(valu, 16), WAIT_USE(valu_complex, 16)};
      case instr_class::valu_double_add:
         return {22, WAIT_USE(valu, 16), WAIT_USE(valu_complex, 16)};
      case instr_class::valu_double_convert:
         return {22, WAIT_USE(valu, 16), WAIT_USE(valu_complex, 16)};
      case instr_class::valu_double_transcendental:
         return {24, WAIT_USE(valu, 16), WAIT_USE(valu_complex, 16)};
      case instr_class::salu: return {2, WAIT_USE(scalar, 1)};
      case instr_class::sfpu: return {4, WAIT_USE(scalar, 1)};
      case instr_class::valu_pseudo_scalar_trans:
         return {7, WAIT_USE(valu, 1), WAIT_USE(valu_complex, 1)};
      case instr_class::smem: return {0, WAIT_USE(scalar, 1)};
      case instr_class::branch:
      case instr_class::sendmsg: return {0, WAIT_USE(branch_sendmsg, 3)};
      case instr_class::ds:
         return instr.isDS() && instr.ds().gds ? perf_info{0, WAIT_USE(export_gds, 1)}
                                               : perf_info{0, WAIT_USE(lds, 1)};
      case instr_class::exp: return {0, WAIT_USE(export_gds, 1)};
      case instr_class::vmem: return {0, WAIT_USE(vmem, 1)};
      case instr_class::wmma: {
         /* int8 and (b)f16 have the same performance. */
         uint8_t cost = instr.opcode == aco_opcode::v_wmma_i32_16x16x16_iu4 ? 16 : 32;
         return {cost, WAIT_USE(valu, cost)};
      }
      case instr_class::barrier:
      case instr_class::waitcnt:
      case instr_class::other:
      default: return {0};
      }
   } else {
      switch (cls) {
      case instr_class::valu32: return {4, WAIT_USE(valu, 4)};
      case instr_class::valu_convert32: return {16, WAIT_USE(valu, 16)};
      case instr_class::valu64: return {8, WAIT_USE(valu, 8)};
      case instr_class::valu_quarter_rate32: return {16, WAIT_USE(valu, 16)};
      case instr_class::valu_fma:
         return program.dev.has_fast_fma32 ? perf_info{4, WAIT_USE(valu, 4)}
                                           : perf_info{16, WAIT_USE(valu, 16)};
      case instr_class::valu_transcendental32: return {16, WAIT_USE(valu, 16)};
      case instr_class::valu_double: return {64, WAIT_USE(valu, 64)};
      case instr_class::valu_double_add: return {32, WAIT_USE(valu, 32)};
      case instr_class::valu_double_convert: return {16, WAIT_USE(valu, 16)};
      case instr_class::valu_double_transcendental: return {64, WAIT_USE(valu, 64)};
      case instr_class::salu: return {4, WAIT_USE(scalar, 4)};
      case instr_class::smem: return {4, WAIT_USE(scalar, 4)};
      case instr_class::branch: return {4, WAIT_USE(branch_sendmsg, 4)};
      case instr_class::ds:
         return instr.isDS() && instr.ds().gds ? perf_info{4, WAIT_USE(export_gds, 4)}
                                               : perf_info{4, WAIT_USE(lds, 4)};
      case instr_class::exp: return {16, WAIT_USE(export_gds, 16)};
      case instr_class::vmem: return {4, WAIT_USE(vmem, 4)};
      case instr_class::barrier:
      case instr_class::waitcnt:
      case instr_class::other:
      default: return {4};
      }
   }

#undef WAIT_USE
#undef WAIT
}

void
BlockCycleEstimator::use_resources(aco_ptr<Instruction>& instr)
{
   perf_info perf = get_perf_info(*program, *instr);

   if (perf.rsrc0 != resource_count) {
      res_available[(int)perf.rsrc0] = cur_cycle + perf.cost0;
      res_usage[(int)perf.rsrc0] += perf.cost0;
   }

   if (perf.rsrc1 != resource_count) {
      res_available[(int)perf.rsrc1] = cur_cycle + perf.cost1;
      res_usage[(int)perf.rsrc1] += perf.cost1;
   }
}

int32_t
BlockCycleEstimator::cycles_until_res_available(aco_ptr<Instruction>& instr)
{
   perf_info perf = get_perf_info(*program, *instr);

   int32_t cost = 0;
   if (perf.rsrc0 != resource_count)
      cost = MAX2(cost, res_available[(int)perf.rsrc0] - cur_cycle);
   if (perf.rsrc1 != resource_count)
      cost = MAX2(cost, res_available[(int)perf.rsrc1] - cur_cycle);

   return cost;
}

static std::array<unsigned, wait_type_num>
get_wait_counter_info(amd_gfx_level gfx_level, aco_ptr<Instruction>& instr)
{
   /* These numbers are all a bit nonsense. LDS/VMEM/SMEM/EXP performance
    * depends a lot on the situation. */

   std::array<unsigned, wait_type_num> info{};

   if (instr->isEXP()) {
      info[wait_type_exp] = 16;
   } else if (instr->isLDSDIR()) {
      info[wait_type_exp] = 13;
   } else if (instr->isFlatLike()) {
      info[wait_type_lgkm] = instr->isFlat() ? 20 : 0;
      if (!instr->definitions.empty() || gfx_level < GFX10)
         info[wait_type_vm] = 320;
      else
         info[wait_type_vs] = 320;
   } else if (instr->isSMEM()) {
      wait_type type = gfx_level >= GFX12 ? wait_type_km : wait_type_lgkm;
      if (instr->definitions.empty()) {
         info[type] = 200;
      } else if (instr->operands.empty()) { /* s_memtime and s_memrealtime */
         info[type] = 1;
      } else {
         bool likely_desc_load = instr->operands[0].size() == 2;
         bool soe = instr->operands.size() >= (!instr->definitions.empty() ? 3 : 4);
         bool const_offset =
            instr->operands[1].isConstant() && (!soe || instr->operands.back().isConstant());

         if (likely_desc_load || const_offset)
            info[type] = 30; /* likely to hit L0 cache */
         else
            info[type] = 200;
      }
   } else if (instr->isDS()) {
      info[wait_type_lgkm] = 20;
   } else if (instr->isVMEM() && instr->definitions.empty() && gfx_level >= GFX10) {
      info[wait_type_vs] = 320;
   } else if (instr->isVMEM()) {
      uint8_t vm_type = get_vmem_type(gfx_level, instr.get());
      wait_type type = wait_type_vm;
      if (gfx_level >= GFX12 && vm_type == vmem_bvh)
         type = wait_type_bvh;
      else if (gfx_level >= GFX12 && vm_type == vmem_sampler)
         type = wait_type_sample;
      info[type] = 320;
   }

   return info;
}

static wait_imm
get_wait_imm(Program* program, aco_ptr<Instruction>& instr)
{
   wait_imm imm;
   if (instr->opcode == aco_opcode::s_endpgm) {
      for (unsigned i = 0; i < wait_type_num; i++)
         imm[i] = 0;
   } else if (imm.unpack(program->gfx_level, instr.get())) {
   } else if (instr->isVINTERP_INREG()) {
      imm.exp = instr->vinterp_inreg().wait_exp;
      if (imm.exp == 0x7)
         imm.exp = wait_imm::unset_counter;
   } else {
      /* If an instruction increases a counter, it waits for it to be below maximum first. */
      std::array<unsigned, wait_type_num> wait_info =
         get_wait_counter_info(program->gfx_level, instr);
      wait_imm max = wait_imm::max(program->gfx_level);
      for (unsigned i = 0; i < wait_type_num; i++) {
         if (wait_info[i])
            imm[i] = max[i] - 1;
      }
   }
   return imm;
}

unsigned
BlockCycleEstimator::get_dependency_cost(aco_ptr<Instruction>& instr)
{
   int deps_available = cur_cycle;

   wait_imm imm = get_wait_imm(program, instr);
   for (unsigned i = 0; i < wait_type_num; i++) {
      if (imm[i] == wait_imm::unset_counter)
         continue;
      for (int j = 0; j < (int)mem_ops[i].size() - imm[i]; j++)
         deps_available = MAX2(deps_available, mem_ops[i][j]);
   }

   if (instr->opcode == aco_opcode::s_endpgm) {
      for (unsigned i = 0; i < 512; i++)
         deps_available = MAX2(deps_available, reg_available[i]);
   } else if (program->gfx_level >= GFX10) {
      for (Operand& op : instr->operands) {
         if (op.isConstant() || op.isUndefined())
            continue;
         for (unsigned i = 0; i < op.size(); i++)
            deps_available = MAX2(deps_available, reg_available[op.physReg().reg() + i]);
      }
   }

   if (program->gfx_level < GFX10)
      deps_available = align(deps_available, 4);

   return deps_available - cur_cycle;
}

static bool
is_vector(aco_opcode op)
{
   switch (instr_info.classes[(int)op]) {
   case instr_class::valu32:
   case instr_class::valu_convert32:
   case instr_class::valu_fma:
   case instr_class::valu_double:
   case instr_class::valu_double_add:
   case instr_class::valu_double_convert:
   case instr_class::valu_double_transcendental:
   case instr_class::vmem:
   case instr_class::ds:
   case instr_class::exp:
   case instr_class::valu64:
   case instr_class::valu_quarter_rate32:
   case instr_class::valu_transcendental32: return true;
   default: return false;
   }
}

void
BlockCycleEstimator::add(aco_ptr<Instruction>& instr)
{
   perf_info perf = get_perf_info(*program, *instr);

   cur_cycle += get_dependency_cost(instr);

   unsigned start;
   bool dual_issue = program->gfx_level >= GFX10 && program->wave_size == 64 &&
                     is_vector(instr->opcode) && !is_dual_issue_capable(*program, *instr) &&
                     program->workgroup_size > 32;
   for (unsigned i = 0; i < (dual_issue ? 2 : 1); i++) {
      cur_cycle += cycles_until_res_available(instr);

      start = cur_cycle;
      use_resources(instr);

      /* GCN is in-order and doesn't begin the next instruction until the current one finishes */
      cur_cycle += program->gfx_level >= GFX10 ? 1 : perf.latency;
   }

   wait_imm imm = get_wait_imm(program, instr);
   for (unsigned i = 0; i < wait_type_num; i++) {
      while (mem_ops[i].size() > imm[i])
         mem_ops[i].pop_front();
   }

   std::array<unsigned, wait_type_num> wait_info = get_wait_counter_info(program->gfx_level, instr);
   for (unsigned i = 0; i < wait_type_num; i++) {
      if (wait_info[i])
         mem_ops[i].push_back(cur_cycle + wait_info[i]);
   }

   /* This is inaccurate but shouldn't affect anything after waitcnt insertion.
    * Before waitcnt insertion, this is necessary to consider memory operations.
    */
   unsigned latency = 0;
   for (unsigned i = 0; i < wait_type_num; i++)
      latency = MAX2(latency, i == wait_type_vs ? 0 : wait_info[i]);
   int32_t result_available = start + MAX2(perf.latency, (int32_t)latency);

   for (Definition& def : instr->definitions) {
      int32_t* available = &reg_available[def.physReg().reg()];
      for (unsigned i = 0; i < def.size(); i++)
         available[i] = MAX2(available[i], result_available);
   }
}

void
BlockCycleEstimator::join(const BlockCycleEstimator& pred)
{
   assert(cur_cycle == 0);

   for (unsigned i = 0; i < (unsigned)resource_count; i++) {
      assert(res_usage[i] == 0);
      res_available[i] = MAX2(res_available[i], pred.res_available[i] - pred.cur_cycle);
   }

   for (unsigned i = 0; i < 512; i++)
      reg_available[i] = MAX2(reg_available[i], pred.reg_available[i] - pred.cur_cycle + cur_cycle);

   for (unsigned i = 0; i < wait_type_num; i++) {
      std::deque<int32_t>& ops = mem_ops[i];
      const std::deque<int32_t>& pred_ops = pred.mem_ops[i];
      for (unsigned j = 0; j < MIN2(ops.size(), pred_ops.size()); j++)
         ops.rbegin()[j] = MAX2(ops.rbegin()[j], pred_ops.rbegin()[j] - pred.cur_cycle);
      for (int j = pred_ops.size() - ops.size() - 1; j >= 0; j--)
         ops.push_front(pred_ops[j] - pred.cur_cycle);
   }
}

} /* end namespace */

/* sgpr_presched/vgpr_presched */
void
collect_presched_stats(Program* program)
{
   RegisterDemand presched_demand;
   for (Block& block : program->blocks)
      presched_demand.update(block.register_demand);
   program->statistics[aco_statistic_sgpr_presched] = presched_demand.sgpr;
   program->statistics[aco_statistic_vgpr_presched] = presched_demand.vgpr;
}

/* instructions/branches/vmem_clauses/smem_clauses/cycles */
void
collect_preasm_stats(Program* program)
{
   for (Block& block : program->blocks) {
      std::set<Instruction*> vmem_clause;
      std::set<Instruction*> smem_clause;

      program->statistics[aco_statistic_instructions] += block.instructions.size();

      for (aco_ptr<Instruction>& instr : block.instructions) {
         const bool is_branch =
            instr->isSOPP() && instr_info.classes[(int)instr->opcode] == instr_class::branch;
         if (is_branch)
            program->statistics[aco_statistic_branches]++;

         if (instr->isVALU() || instr->isVINTRP())
            program->statistics[aco_statistic_valu]++;
         if (instr->isSALU() && !instr->isSOPP() &&
             instr_info.classes[(int)instr->opcode] != instr_class::waitcnt)
            program->statistics[aco_statistic_salu]++;
         if (instr->isVOPD())
            program->statistics[aco_statistic_vopd]++;

         if ((instr->isVMEM() || instr->isScratch() || instr->isGlobal()) &&
             !instr->operands.empty()) {
            if (std::none_of(vmem_clause.begin(), vmem_clause.end(),
                             [&](Instruction* other)
                             { return should_form_clause(instr.get(), other); }))
               program->statistics[aco_statistic_vmem_clauses]++;
            vmem_clause.insert(instr.get());

            program->statistics[aco_statistic_vmem]++;
         } else {
            vmem_clause.clear();
         }

         if (instr->isSMEM() && !instr->operands.empty()) {
            if (std::none_of(smem_clause.begin(), smem_clause.end(),
                             [&](Instruction* other)
                             { return should_form_clause(instr.get(), other); }))
               program->statistics[aco_statistic_smem_clauses]++;
            smem_clause.insert(instr.get());

            program->statistics[aco_statistic_smem]++;
         } else {
            smem_clause.clear();
         }
      }
   }

   double latency = 0;
   double usage[(int)BlockCycleEstimator::resource_count] = {0};
   std::vector<BlockCycleEstimator> blocks(program->blocks.size(), program);

   constexpr const unsigned vmem_latency = 320;
   for (const Definition def : program->args_pending_vmem) {
      blocks[0].mem_ops[wait_type_vm].push_back(vmem_latency);
      for (unsigned i = 0; i < def.size(); i++)
         blocks[0].reg_available[def.physReg().reg() + i] = vmem_latency;
   }

   for (Block& block : program->blocks) {
      BlockCycleEstimator& block_est = blocks[block.index];
      for (unsigned pred : block.linear_preds)
         block_est.join(blocks[pred]);

      for (aco_ptr<Instruction>& instr : block.instructions) {
         unsigned before = block_est.cur_cycle;
         block_est.add(instr);
         instr->pass_flags = block_est.cur_cycle - before;
      }

      /* TODO: it would be nice to be able to consider estimated loop trip
       * counts used for loop unrolling.
       */

      /* TODO: estimate the trip_count of divergent loops (those which break
       * divergent) higher than of uniform loops
       */

      /* Assume loops execute 8-2 times, uniform branches are taken 50% the time,
       * and any lane in the wave takes a side of a divergent branch 75% of the
       * time.
       */
      double iter = 1.0;
      iter *= block.loop_nest_depth > 0 ? 8.0 : 1.0;
      iter *= block.loop_nest_depth > 1 ? 4.0 : 1.0;
      iter *= block.loop_nest_depth > 2 ? pow(2.0, block.loop_nest_depth - 2) : 1.0;
      iter *= pow(0.5, block.uniform_if_depth);
      iter *= pow(0.75, block.divergent_if_logical_depth);

      bool divergent_if_linear_else =
         block.logical_preds.empty() && block.linear_preds.size() == 1 &&
         block.linear_succs.size() == 1 &&
         program->blocks[block.linear_preds[0]].kind & (block_kind_branch | block_kind_invert);
      if (divergent_if_linear_else)
         iter *= 0.25;

      latency += block_est.cur_cycle * iter;
      for (unsigned i = 0; i < (unsigned)BlockCycleEstimator::resource_count; i++)
         usage[i] += block_est.res_usage[i] * iter;
   }

   /* This likely exaggerates the effectiveness of parallelism because it
    * ignores instruction ordering. It can assume there might be SALU/VALU/etc
    * work to from other waves while one is idle but that might not be the case
    * because those other waves have not reached such a point yet.
    */

   double parallelism = program->num_waves;
   for (unsigned i = 0; i < (unsigned)BlockCycleEstimator::resource_count; i++) {
      if (usage[i] > 0.0)
         parallelism = MIN2(parallelism, latency / usage[i]);
   }
   double waves_per_cycle = 1.0 / latency * parallelism;
   double wave64_per_cycle = waves_per_cycle * (program->wave_size / 64.0);

   double max_utilization = 1.0;
   if (program->workgroup_size != UINT_MAX)
      max_utilization =
         program->workgroup_size / (double)align(program->workgroup_size, program->wave_size);
   wave64_per_cycle *= max_utilization;

   program->statistics[aco_statistic_latency] = round(latency);
   program->statistics[aco_statistic_inv_throughput] = round(1.0 / wave64_per_cycle);

   if (debug_flags & DEBUG_PERF_INFO) {
      aco_print_program(program, stderr, print_no_ssa | print_perf_info);

      fprintf(stderr, "num_waves: %u\n", program->num_waves);
      fprintf(stderr, "salu_smem_usage: %f\n", usage[(int)BlockCycleEstimator::scalar]);
      fprintf(stderr, "branch_sendmsg_usage: %f\n",
              usage[(int)BlockCycleEstimator::branch_sendmsg]);
      fprintf(stderr, "valu_usage: %f\n", usage[(int)BlockCycleEstimator::valu]);
      fprintf(stderr, "valu_complex_usage: %f\n", usage[(int)BlockCycleEstimator::valu_complex]);
      fprintf(stderr, "lds_usage: %f\n", usage[(int)BlockCycleEstimator::lds]);
      fprintf(stderr, "export_gds_usage: %f\n", usage[(int)BlockCycleEstimator::export_gds]);
      fprintf(stderr, "vmem_usage: %f\n", usage[(int)BlockCycleEstimator::vmem]);
      fprintf(stderr, "latency: %f\n", latency);
      fprintf(stderr, "parallelism: %f\n", parallelism);
      fprintf(stderr, "max_utilization: %f\n", max_utilization);
      fprintf(stderr, "wave64_per_cycle: %f\n", wave64_per_cycle);
      fprintf(stderr, "\n");
   }
}

void
collect_postasm_stats(Program* program, const std::vector<uint32_t>& code)
{
   program->statistics[aco_statistic_hash] = util_hash_crc32(code.data(), code.size() * 4);
}

Instruction_cycle_info
get_cycle_info(const Program& program, const Instruction& instr)
{
   perf_info info = get_perf_info(program, instr);
   return Instruction_cycle_info{(unsigned)info.latency, std::max(info.cost0, info.cost1)};
}

} // namespace aco
