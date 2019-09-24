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
 */

#include "aco_ir.h"
#include <map>
#include <stack>
#include "vulkan/radv_shader.h"


/*
 * Implements the spilling algorithm on SSA-form from
 * "Register Spilling and Live-Range Splitting for SSA-Form Programs"
 * by Matthias Braun and Sebastian Hack.
 */

namespace aco {

namespace {

struct remat_info {
   Instruction *instr;
};

struct spill_ctx {
   RegisterDemand target_pressure;
   Program* program;
   std::vector<std::vector<RegisterDemand>> register_demand;
   std::vector<std::map<Temp, Temp>> renames;
   std::vector<std::map<Temp, uint32_t>> spills_entry;
   std::vector<std::map<Temp, uint32_t>> spills_exit;
   std::vector<bool> processed;
   std::stack<Block*> loop_header;
   std::vector<std::map<Temp, std::pair<uint32_t, uint32_t>>> next_use_distances_start;
   std::vector<std::map<Temp, std::pair<uint32_t, uint32_t>>> next_use_distances_end;
   std::vector<std::pair<RegClass, std::set<uint32_t>>> interferences;
   std::vector<std::pair<uint32_t, uint32_t>> affinities;
   std::vector<bool> is_reloaded;
   std::map<Temp, remat_info> remat;
   std::map<Instruction *, bool> remat_used;

   spill_ctx(const RegisterDemand target_pressure, Program* program,
             std::vector<std::vector<RegisterDemand>> register_demand)
      : target_pressure(target_pressure), program(program),
        register_demand(register_demand), renames(program->blocks.size()),
        spills_entry(program->blocks.size()), spills_exit(program->blocks.size()),
        processed(program->blocks.size(), false) {}

   uint32_t allocate_spill_id(RegClass rc)
   {
      interferences.emplace_back(rc, std::set<uint32_t>());
      is_reloaded.push_back(false);
      return next_spill_id++;
   }

   uint32_t next_spill_id = 0;
};

int32_t get_dominator(int idx_a, int idx_b, Program* program, bool is_linear)
{

   if (idx_a == -1)
      return idx_b;
   if (idx_b == -1)
      return idx_a;
   if (is_linear) {
      while (idx_a != idx_b) {
         if (idx_a > idx_b)
            idx_a = program->blocks[idx_a].linear_idom;
         else
            idx_b = program->blocks[idx_b].linear_idom;
      }
   } else {
      while (idx_a != idx_b) {
         if (idx_a > idx_b)
            idx_a = program->blocks[idx_a].logical_idom;
         else
            idx_b = program->blocks[idx_b].logical_idom;
      }
   }
   assert(idx_a != -1);
   return idx_a;
}

void next_uses_per_block(spill_ctx& ctx, unsigned block_idx, std::set<uint32_t>& worklist)
{
   Block* block = &ctx.program->blocks[block_idx];
   std::map<Temp, std::pair<uint32_t, uint32_t>> next_uses = ctx.next_use_distances_end[block_idx];

   /* to compute the next use distance at the beginning of the block, we have to add the block's size */
   for (std::map<Temp, std::pair<uint32_t, uint32_t>>::iterator it = next_uses.begin(); it != next_uses.end();) {
      it->second.second = it->second.second + block->instructions.size();

      /* remove the live out exec mask as we really don't want to spill it */
      if (it->first == block->live_out_exec)
         it = next_uses.erase(it);
      else
         ++it;
   }

   int idx = block->instructions.size() - 1;
   while (idx >= 0) {
      aco_ptr<Instruction>& instr = block->instructions[idx];

      if (instr->opcode == aco_opcode::p_linear_phi ||
          instr->opcode == aco_opcode::p_phi)
         break;

      for (const Definition& def : instr->definitions) {
         if (def.isTemp())
            next_uses.erase(def.getTemp());
      }

      for (const Operand& op : instr->operands) {
         /* omit exec mask */
         if (op.isFixed() && op.physReg() == exec)
            continue;
         if (op.isTemp())
            next_uses[op.getTemp()] = {block_idx, idx};
      }
      idx--;
   }

   assert(block_idx != 0 || next_uses.empty());
   ctx.next_use_distances_start[block_idx] = next_uses;
   while (idx >= 0) {
      aco_ptr<Instruction>& instr = block->instructions[idx];
      assert(instr->opcode == aco_opcode::p_linear_phi || instr->opcode == aco_opcode::p_phi);

      for (unsigned i = 0; i < instr->operands.size(); i++) {
         unsigned pred_idx = instr->opcode == aco_opcode::p_phi ?
                             block->logical_preds[i] :
                             block->linear_preds[i];
         if (instr->operands[i].isTemp()) {
            if (ctx.next_use_distances_end[pred_idx].find(instr->operands[i].getTemp()) == ctx.next_use_distances_end[pred_idx].end() ||
                ctx.next_use_distances_end[pred_idx][instr->operands[i].getTemp()] != std::pair<uint32_t, uint32_t>{block_idx, 0})
               worklist.insert(pred_idx);
            ctx.next_use_distances_end[pred_idx][instr->operands[i].getTemp()] = {block_idx, 0};
         }
      }
      next_uses.erase(instr->definitions[0].getTemp());
      idx--;
   }

   /* all remaining live vars must be live-out at the predecessors */
   for (std::pair<Temp, std::pair<uint32_t, uint32_t>> pair : next_uses) {
      Temp temp = pair.first;
      uint32_t distance = pair.second.second;
      uint32_t dom = pair.second.first;
      std::vector<unsigned>& preds = temp.is_linear() ? block->linear_preds : block->logical_preds;
      for (unsigned pred_idx : preds) {
         if (ctx.program->blocks[pred_idx].loop_nest_depth > block->loop_nest_depth)
            distance += 0xFFFF;
         if (ctx.next_use_distances_end[pred_idx].find(temp) != ctx.next_use_distances_end[pred_idx].end()) {
            dom = get_dominator(dom, ctx.next_use_distances_end[pred_idx][temp].first, ctx.program, temp.is_linear());
            distance = std::min(ctx.next_use_distances_end[pred_idx][temp].second, distance);
         }
         if (ctx.next_use_distances_end[pred_idx][temp] != std::pair<uint32_t, uint32_t>{dom, distance})
            worklist.insert(pred_idx);
         ctx.next_use_distances_end[pred_idx][temp] = {dom, distance};
      }
   }

}

void compute_global_next_uses(spill_ctx& ctx, std::vector<std::set<Temp>>& live_out)
{
   ctx.next_use_distances_start.resize(ctx.program->blocks.size());
   ctx.next_use_distances_end.resize(ctx.program->blocks.size());
   std::set<uint32_t> worklist;
   for (Block& block : ctx.program->blocks)
      worklist.insert(block.index);

   while (!worklist.empty()) {
      std::set<unsigned>::reverse_iterator b_it = worklist.rbegin();
      unsigned block_idx = *b_it;
      worklist.erase(block_idx);
      next_uses_per_block(ctx, block_idx, worklist);
   }
}

bool should_rematerialize(aco_ptr<Instruction>& instr)
{
   /* TODO: rematerialization is only supported for VOP1, SOP1 and PSEUDO */
   if (instr->format != Format::VOP1 && instr->format != Format::SOP1 && instr->format != Format::PSEUDO)
      return false;
   /* TODO: pseudo-instruction rematerialization is only supported for p_create_vector */
   if (instr->format == Format::PSEUDO && instr->opcode != aco_opcode::p_create_vector)
      return false;

   for (const Operand& op : instr->operands) {
      /* TODO: rematerialization using temporaries isn't yet supported */
      if (op.isTemp())
         return false;
   }

   /* TODO: rematerialization with multiple definitions isn't yet supported */
   if (instr->definitions.size() > 1)
      return false;

   return true;
}

aco_ptr<Instruction> do_reload(spill_ctx& ctx, Temp tmp, Temp new_name, uint32_t spill_id)
{
   std::map<Temp, remat_info>::iterator remat = ctx.remat.find(tmp);
   if (remat != ctx.remat.end()) {
      Instruction *instr = remat->second.instr;
      assert((instr->format == Format::VOP1 || instr->format == Format::SOP1 || instr->format == Format::PSEUDO) && "unsupported");
      assert((instr->format != Format::PSEUDO || instr->opcode == aco_opcode::p_create_vector) && "unsupported");
      assert(instr->definitions.size() == 1 && "unsupported");

      aco_ptr<Instruction> res;
      if (instr->format == Format::VOP1) {
         res.reset(create_instruction<VOP1_instruction>(instr->opcode, instr->format, instr->operands.size(), instr->definitions.size()));
      } else if (instr->format == Format::SOP1) {
         res.reset(create_instruction<SOP1_instruction>(instr->opcode, instr->format, instr->operands.size(), instr->definitions.size()));
      } else if (instr->format == Format::PSEUDO) {
         res.reset(create_instruction<Instruction>(instr->opcode, instr->format, instr->operands.size(), instr->definitions.size()));
      }
      for (unsigned i = 0; i < instr->operands.size(); i++) {
         res->operands[i] = instr->operands[i];
         if (instr->operands[i].isTemp()) {
            assert(false && "unsupported");
            if (ctx.remat.count(instr->operands[i].getTemp()))
               ctx.remat_used[ctx.remat[instr->operands[i].getTemp()].instr] = true;
         }
      }
      res->definitions[0] = Definition(new_name);
      return res;
   } else {
      aco_ptr<Pseudo_instruction> reload{create_instruction<Pseudo_instruction>(aco_opcode::p_reload, Format::PSEUDO, 1, 1)};
      reload->operands[0] = Operand(spill_id);
      reload->definitions[0] = Definition(new_name);
      ctx.is_reloaded[spill_id] = true;
      return reload;
   }
}

void get_rematerialize_info(spill_ctx& ctx)
{
   for (Block& block : ctx.program->blocks) {
      bool logical = false;
      for (aco_ptr<Instruction>& instr : block.instructions) {
         if (instr->opcode == aco_opcode::p_logical_start)
            logical = true;
         else if (instr->opcode == aco_opcode::p_logical_end)
            logical = false;
         if (logical && should_rematerialize(instr)) {
            for (const Definition& def : instr->definitions) {
               if (def.isTemp()) {
                  ctx.remat[def.getTemp()] = (remat_info){instr.get()};
                  ctx.remat_used[instr.get()] = false;
               }
            }
         }
      }
   }
}

std::vector<std::map<Temp, uint32_t>> local_next_uses(spill_ctx& ctx, Block* block)
{
   std::vector<std::map<Temp, uint32_t>> local_next_uses(block->instructions.size());

   std::map<Temp, uint32_t> next_uses;
   for (std::pair<Temp, std::pair<uint32_t, uint32_t>> pair : ctx.next_use_distances_end[block->index]) {
      /* omit live out exec mask */
      if (pair.first == block->live_out_exec)
         continue;

      next_uses[pair.first] = pair.second.second + block->instructions.size();
   }

   for (int idx = block->instructions.size() - 1; idx >= 0; idx--) {
      aco_ptr<Instruction>& instr = block->instructions[idx];
      if (!instr)
         break;
      if (instr->opcode == aco_opcode::p_phi || instr->opcode == aco_opcode::p_linear_phi)
         break;

      for (const Operand& op : instr->operands) {
         if (op.isFixed() && op.physReg() == exec)
            continue;
         if (op.isTemp())
            next_uses[op.getTemp()] = idx;
      }
      for (const Definition& def : instr->definitions) {
         if (def.isTemp())
            next_uses.erase(def.getTemp());
      }
      local_next_uses[idx] = next_uses;
   }
   return local_next_uses;
}


RegisterDemand init_live_in_vars(spill_ctx& ctx, Block* block, unsigned block_idx)
{
   RegisterDemand spilled_registers;

   /* first block, nothing was spilled before */
   if (block_idx == 0)
      return {0, 0};

   /* loop header block */
   if (block->loop_nest_depth > ctx.program->blocks[block_idx - 1].loop_nest_depth) {
      assert(block->linear_preds[0] == block_idx - 1);
      assert(block->logical_preds[0] == block_idx - 1);

      /* create new loop_info */
      ctx.loop_header.emplace(block);

      /* check how many live-through variables should be spilled */
      RegisterDemand new_demand;
      unsigned i = block_idx;
      while (ctx.program->blocks[i].loop_nest_depth >= block->loop_nest_depth) {
         assert(ctx.program->blocks.size() > i);
         new_demand.update(ctx.program->blocks[i].register_demand);
         i++;
      }
      unsigned loop_end = i;

      /* select live-through vgpr variables */
      while (new_demand.vgpr - spilled_registers.vgpr > ctx.target_pressure.vgpr) {
         unsigned distance = 0;
         Temp to_spill;
         for (std::pair<Temp, std::pair<uint32_t, uint32_t>> pair : ctx.next_use_distances_end[block_idx - 1]) {
            if (pair.first.type() == RegType::vgpr &&
                pair.second.first >= loop_end &&
                pair.second.second > distance &&
                ctx.spills_entry[block_idx].find(pair.first) == ctx.spills_entry[block_idx].end()) {
               to_spill = pair.first;
               distance = pair.second.second;
            }
         }
         if (distance == 0)
            break;

         uint32_t spill_id;
         if (ctx.spills_exit[block_idx - 1].find(to_spill) == ctx.spills_exit[block_idx - 1].end()) {
            spill_id = ctx.allocate_spill_id(to_spill.regClass());
         } else {
            spill_id = ctx.spills_exit[block_idx - 1][to_spill];
         }

         ctx.spills_entry[block_idx][to_spill] = spill_id;
         spilled_registers.vgpr += to_spill.size();
      }

      /* select live-through sgpr variables */
      while (new_demand.sgpr - spilled_registers.sgpr > ctx.target_pressure.sgpr) {
         unsigned distance = 0;
         Temp to_spill;
         for (std::pair<Temp, std::pair<uint32_t, uint32_t>> pair : ctx.next_use_distances_end[block_idx - 1]) {
            if (pair.first.type() == RegType::sgpr &&
                pair.second.first >= loop_end &&
                pair.second.second > distance &&
                ctx.spills_entry[block_idx].find(pair.first) == ctx.spills_entry[block_idx].end()) {
               to_spill = pair.first;
               distance = pair.second.second;
            }
         }
         if (distance == 0)
            break;

         uint32_t spill_id;
         if (ctx.spills_exit[block_idx - 1].find(to_spill) == ctx.spills_exit[block_idx - 1].end()) {
            spill_id = ctx.allocate_spill_id(to_spill.regClass());
         } else {
            spill_id = ctx.spills_exit[block_idx - 1][to_spill];
         }

         ctx.spills_entry[block_idx][to_spill] = spill_id;
         spilled_registers.sgpr += to_spill.size();
      }



      /* shortcut */
      if (!RegisterDemand(new_demand - spilled_registers).exceeds(ctx.target_pressure))
         return spilled_registers;

      /* if reg pressure is too high at beginning of loop, add variables with furthest use */
      unsigned idx = 0;
      while (block->instructions[idx]->opcode == aco_opcode::p_phi || block->instructions[idx]->opcode == aco_opcode::p_linear_phi)
         idx++;

      assert(idx != 0 && "loop without phis: TODO");
      idx--;
      RegisterDemand reg_pressure = ctx.register_demand[block_idx][idx] - spilled_registers;
      while (reg_pressure.sgpr > ctx.target_pressure.sgpr) {
         unsigned distance = 0;
         Temp to_spill;
         for (std::pair<Temp, std::pair<uint32_t, uint32_t>> pair : ctx.next_use_distances_start[block_idx]) {
            if (pair.first.type() == RegType::sgpr &&
                pair.second.second > distance &&
                ctx.spills_entry[block_idx].find(pair.first) == ctx.spills_entry[block_idx].end()) {
               to_spill = pair.first;
               distance = pair.second.second;
            }
         }
         assert(distance != 0);

         ctx.spills_entry[block_idx][to_spill] = ctx.allocate_spill_id(to_spill.regClass());
         spilled_registers.sgpr += to_spill.size();
         reg_pressure.sgpr -= to_spill.size();
      }
      while (reg_pressure.vgpr > ctx.target_pressure.vgpr) {
         unsigned distance = 0;
         Temp to_spill;
         for (std::pair<Temp, std::pair<uint32_t, uint32_t>> pair : ctx.next_use_distances_start[block_idx]) {
            if (pair.first.type() == RegType::vgpr &&
                pair.second.second > distance &&
                ctx.spills_entry[block_idx].find(pair.first) == ctx.spills_entry[block_idx].end()) {
               to_spill = pair.first;
               distance = pair.second.second;
            }
         }
         assert(distance != 0);
         ctx.spills_entry[block_idx][to_spill] = ctx.allocate_spill_id(to_spill.regClass());
         spilled_registers.vgpr += to_spill.size();
         reg_pressure.vgpr -= to_spill.size();
      }

      return spilled_registers;
   }

   /* branch block */
   if (block->linear_preds.size() == 1) {
      /* keep variables spilled if they are alive and not used in the current block */
      unsigned pred_idx = block->linear_preds[0];
      for (std::pair<Temp, uint32_t> pair : ctx.spills_exit[pred_idx]) {
         if (pair.first.type() == RegType::sgpr &&
             ctx.next_use_distances_start[block_idx].find(pair.first) != ctx.next_use_distances_start[block_idx].end() &&
             ctx.next_use_distances_start[block_idx][pair.first].second > block_idx) {
            ctx.spills_entry[block_idx].insert(pair);
            spilled_registers.sgpr += pair.first.size();
         }
      }
      if (block->logical_preds.size() == 1) {
         pred_idx = block->logical_preds[0];
         for (std::pair<Temp, uint32_t> pair : ctx.spills_exit[pred_idx]) {
            if (pair.first.type() == RegType::vgpr &&
                ctx.next_use_distances_start[block_idx].find(pair.first) != ctx.next_use_distances_start[block_idx].end() &&
                ctx.next_use_distances_end[pred_idx][pair.first].second > block_idx) {
               ctx.spills_entry[block_idx].insert(pair);
               spilled_registers.vgpr += pair.first.size();
            }
         }
      }

      /* if register demand is still too high, we just keep all spilled live vars and process the block */
      if (block->register_demand.sgpr - spilled_registers.sgpr > ctx.target_pressure.sgpr) {
         pred_idx = block->linear_preds[0];
         for (std::pair<Temp, uint32_t> pair : ctx.spills_exit[pred_idx]) {
            if (pair.first.type() == RegType::sgpr &&
                ctx.next_use_distances_start[block_idx].find(pair.first) != ctx.next_use_distances_start[block_idx].end() &&
                ctx.spills_entry[block_idx].insert(pair).second) {
               spilled_registers.sgpr += pair.first.size();
            }
         }
      }
      if (block->register_demand.vgpr - spilled_registers.vgpr > ctx.target_pressure.vgpr && block->logical_preds.size() == 1) {
         pred_idx = block->logical_preds[0];
         for (std::pair<Temp, uint32_t> pair : ctx.spills_exit[pred_idx]) {
            if (pair.first.type() == RegType::vgpr &&
                ctx.next_use_distances_start[block_idx].find(pair.first) != ctx.next_use_distances_start[block_idx].end() &&
                ctx.spills_entry[block_idx].insert(pair).second) {
               spilled_registers.vgpr += pair.first.size();
            }
         }
      }

      return spilled_registers;
   }

   /* else: merge block */
   std::set<Temp> partial_spills;

   /* keep variables spilled on all incoming paths */
   for (std::pair<Temp, std::pair<uint32_t, uint32_t>> pair : ctx.next_use_distances_start[block_idx]) {
      std::vector<unsigned>& preds = pair.first.type() == RegType::vgpr ? block->logical_preds : block->linear_preds;
      /* If it can be rematerialized, keep the variable spilled if all predecessors do not reload it.
       * Otherwise, if any predecessor reloads it, ensure it's reloaded on all other predecessors.
       * The idea is that it's better in practice to rematerialize redundantly than to create lots of phis. */
      /* TODO: test this idea with more than Dawn of War III shaders (the current pipeline-db doesn't seem to exercise this path much) */
      bool remat = ctx.remat.count(pair.first);
      bool spill = !remat;
      uint32_t spill_id = 0;
      for (unsigned pred_idx : preds) {
         /* variable is not even live at the predecessor: probably from a phi */
         if (ctx.next_use_distances_end[pred_idx].find(pair.first) == ctx.next_use_distances_end[pred_idx].end()) {
            spill = false;
            break;
         }
         if (ctx.spills_exit[pred_idx].find(pair.first) == ctx.spills_exit[pred_idx].end()) {
            if (!remat)
               spill = false;
         } else {
            partial_spills.insert(pair.first);
            /* it might be that on one incoming path, the variable has a different spill_id, but add_couple_code() will take care of that. */
            spill_id = ctx.spills_exit[pred_idx][pair.first];
            if (remat)
               spill = true;
         }
      }
      if (spill) {
         ctx.spills_entry[block_idx][pair.first] = spill_id;
         partial_spills.erase(pair.first);
         spilled_registers += pair.first;
      }
   }

   /* same for phis */
   unsigned idx = 0;
   while (block->instructions[idx]->opcode == aco_opcode::p_linear_phi ||
          block->instructions[idx]->opcode == aco_opcode::p_phi) {
      aco_ptr<Instruction>& phi = block->instructions[idx];
      std::vector<unsigned>& preds = phi->opcode == aco_opcode::p_phi ? block->logical_preds : block->linear_preds;
      bool spill = true;

      for (unsigned i = 0; i < phi->operands.size(); i++) {
         if (!phi->operands[i].isTemp())
            spill = false;
         else if (ctx.spills_exit[preds[i]].find(phi->operands[i].getTemp()) == ctx.spills_exit[preds[i]].end())
            spill = false;
         else
            partial_spills.insert(phi->definitions[0].getTemp());
      }
      if (spill) {
         ctx.spills_entry[block_idx][phi->definitions[0].getTemp()] = ctx.allocate_spill_id(phi->definitions[0].regClass());
         partial_spills.erase(phi->definitions[0].getTemp());
         spilled_registers += phi->definitions[0].getTemp();
      }

      idx++;
   }

   /* if reg pressure at first instruction is still too high, add partially spilled variables */
   RegisterDemand reg_pressure;
   if (idx == 0) {
      for (const Definition& def : block->instructions[idx]->definitions) {
         if (def.isTemp()) {
            reg_pressure -= def.getTemp();
         }
      }
      for (const Operand& op : block->instructions[idx]->operands) {
         if (op.isTemp() && op.isFirstKill()) {
            reg_pressure += op.getTemp();
         }
      }
   } else {
      idx--;
   }
   reg_pressure += ctx.register_demand[block_idx][idx] - spilled_registers;

   while (reg_pressure.sgpr > ctx.target_pressure.sgpr) {
      assert(!partial_spills.empty());

      std::set<Temp>::iterator it = partial_spills.begin();
      Temp to_spill = *it;
      unsigned distance = ctx.next_use_distances_start[block_idx][*it].second;
      while (it != partial_spills.end()) {
         assert(ctx.spills_entry[block_idx].find(*it) == ctx.spills_entry[block_idx].end());

         if (it->type() == RegType::sgpr && ctx.next_use_distances_start[block_idx][*it].second > distance) {
            distance = ctx.next_use_distances_start[block_idx][*it].second;
            to_spill = *it;
         }
         ++it;
      }
      assert(distance != 0);

      ctx.spills_entry[block_idx][to_spill] = ctx.allocate_spill_id(to_spill.regClass());
      partial_spills.erase(to_spill);
      spilled_registers.sgpr += to_spill.size();
      reg_pressure.sgpr -= to_spill.size();
   }

   while (reg_pressure.vgpr > ctx.target_pressure.vgpr) {
      assert(!partial_spills.empty());

      std::set<Temp>::iterator it = partial_spills.begin();
      Temp to_spill = *it;
      unsigned distance = ctx.next_use_distances_start[block_idx][*it].second;
      while (it != partial_spills.end()) {
         assert(ctx.spills_entry[block_idx].find(*it) == ctx.spills_entry[block_idx].end());

         if (it->type() == RegType::vgpr && ctx.next_use_distances_start[block_idx][*it].second > distance) {
            distance = ctx.next_use_distances_start[block_idx][*it].second;
            to_spill = *it;
         }
         ++it;
      }
      assert(distance != 0);

      ctx.spills_entry[block_idx][to_spill] = ctx.allocate_spill_id(to_spill.regClass());
      partial_spills.erase(to_spill);
      spilled_registers.vgpr += to_spill.size();
      reg_pressure.vgpr -= to_spill.size();
   }

   return spilled_registers;
}


void add_coupling_code(spill_ctx& ctx, Block* block, unsigned block_idx)
{
   /* no coupling code necessary */
   if (block->linear_preds.size() == 0)
      return;

   std::vector<aco_ptr<Instruction>> instructions;
   /* branch block: TODO take other branch into consideration */
   if (block->linear_preds.size() == 1) {
      assert(ctx.processed[block->linear_preds[0]]);

      if (block->logical_preds.size() == 1) {
         unsigned pred_idx = block->logical_preds[0];
         for (std::pair<Temp, std::pair<uint32_t, uint32_t>> live : ctx.next_use_distances_start[block_idx]) {
            if (live.first.type() == RegType::sgpr)
               continue;
            /* still spilled */
            if (ctx.spills_entry[block_idx].find(live.first) != ctx.spills_entry[block_idx].end())
               continue;

            /* in register at end of predecessor */
            if (ctx.spills_exit[pred_idx].find(live.first) == ctx.spills_exit[pred_idx].end()) {
               std::map<Temp, Temp>::iterator it = ctx.renames[pred_idx].find(live.first);
               if (it != ctx.renames[pred_idx].end())
                  ctx.renames[block_idx].insert(*it);
               continue;
            }

            /* variable is spilled at predecessor and live at current block: create reload instruction */
            Temp new_name = {ctx.program->allocateId(), live.first.regClass()};
            aco_ptr<Instruction> reload = do_reload(ctx, live.first, new_name, ctx.spills_exit[pred_idx][live.first]);
            instructions.emplace_back(std::move(reload));
            ctx.renames[block_idx][live.first] = new_name;
         }
      }

      unsigned pred_idx = block->linear_preds[0];
      for (std::pair<Temp, std::pair<uint32_t, uint32_t>> live : ctx.next_use_distances_start[block_idx]) {
         if (live.first.type() == RegType::vgpr)
            continue;
         /* still spilled */
         if (ctx.spills_entry[block_idx].find(live.first) != ctx.spills_entry[block_idx].end())
            continue;

         /* in register at end of predecessor */
         if (ctx.spills_exit[pred_idx].find(live.first) == ctx.spills_exit[pred_idx].end()) {
            std::map<Temp, Temp>::iterator it = ctx.renames[pred_idx].find(live.first);
            if (it != ctx.renames[pred_idx].end())
               ctx.renames[block_idx].insert(*it);
            continue;
         }

         /* variable is spilled at predecessor and live at current block: create reload instruction */
         Temp new_name = {ctx.program->allocateId(), live.first.regClass()};
         aco_ptr<Instruction> reload = do_reload(ctx, live.first, new_name, ctx.spills_exit[pred_idx][live.first]);
         instructions.emplace_back(std::move(reload));
         ctx.renames[block_idx][live.first] = new_name;
      }

      /* combine new reload instructions with original block */
      if (!instructions.empty()) {
         unsigned insert_idx = 0;
         while (block->instructions[insert_idx]->opcode == aco_opcode::p_phi ||
                block->instructions[insert_idx]->opcode == aco_opcode::p_linear_phi) {
            insert_idx++;
         }
         ctx.register_demand[block->index].insert(std::next(ctx.register_demand[block->index].begin(), insert_idx),
                                                  instructions.size(), RegisterDemand());
         block->instructions.insert(std::next(block->instructions.begin(), insert_idx),
                                    std::move_iterator<std::vector<aco_ptr<Instruction>>::iterator>(instructions.begin()),
                                    std::move_iterator<std::vector<aco_ptr<Instruction>>::iterator>(instructions.end()));
      }
      return;
   }

   /* loop header and merge blocks: check if all (linear) predecessors have been processed */
   for (ASSERTED unsigned pred : block->linear_preds)
      assert(ctx.processed[pred]);

   /* iterate the phi nodes for which operands to spill at the predecessor */
   for (aco_ptr<Instruction>& phi : block->instructions) {
      if (phi->opcode != aco_opcode::p_phi &&
          phi->opcode != aco_opcode::p_linear_phi)
         break;

      /* if the phi is not spilled, add to instructions */
      if (ctx.spills_entry[block_idx].find(phi->definitions[0].getTemp()) == ctx.spills_entry[block_idx].end()) {
         instructions.emplace_back(std::move(phi));
         continue;
      }

      std::vector<unsigned>& preds = phi->opcode == aco_opcode::p_phi ? block->logical_preds : block->linear_preds;
      uint32_t def_spill_id = ctx.spills_entry[block_idx][phi->definitions[0].getTemp()];

      for (unsigned i = 0; i < phi->operands.size(); i++) {
         unsigned pred_idx = preds[i];

         /* we have to spill constants to the same memory address */
         if (phi->operands[i].isConstant()) {
            uint32_t spill_id = ctx.allocate_spill_id(phi->definitions[0].regClass());
            for (std::pair<Temp, uint32_t> pair : ctx.spills_exit[pred_idx]) {
               ctx.interferences[def_spill_id].second.emplace(pair.second);
               ctx.interferences[pair.second].second.emplace(def_spill_id);
            }
            ctx.affinities.emplace_back(std::pair<uint32_t, uint32_t>{def_spill_id, spill_id});

            aco_ptr<Pseudo_instruction> spill{create_instruction<Pseudo_instruction>(aco_opcode::p_spill, Format::PSEUDO, 2, 0)};
            spill->operands[0] = phi->operands[i];
            spill->operands[1] = Operand(spill_id);
            Block& pred = ctx.program->blocks[pred_idx];
            unsigned idx = pred.instructions.size();
            do {
               assert(idx != 0);
               idx--;
            } while (phi->opcode == aco_opcode::p_phi && pred.instructions[idx]->opcode != aco_opcode::p_logical_end);
            std::vector<aco_ptr<Instruction>>::iterator it = std::next(pred.instructions.begin(), idx);
            pred.instructions.insert(it, std::move(spill));
            continue;
         }
         if (!phi->operands[i].isTemp())
            continue;

         /* build interferences between the phi def and all spilled variables at the predecessor blocks */
         for (std::pair<Temp, uint32_t> pair : ctx.spills_exit[pred_idx]) {
            if (phi->operands[i].getTemp() == pair.first)
               continue;
            ctx.interferences[def_spill_id].second.emplace(pair.second);
            ctx.interferences[pair.second].second.emplace(def_spill_id);
         }

         /* variable is already spilled at predecessor */
         std::map<Temp, uint32_t>::iterator spilled = ctx.spills_exit[pred_idx].find(phi->operands[i].getTemp());
         if (spilled != ctx.spills_exit[pred_idx].end()) {
            if (spilled->second != def_spill_id)
               ctx.affinities.emplace_back(std::pair<uint32_t, uint32_t>{def_spill_id, spilled->second});
            continue;
         }

         /* rename if necessary */
         Temp var = phi->operands[i].getTemp();
         std::map<Temp, Temp>::iterator rename_it = ctx.renames[pred_idx].find(var);
         if (rename_it != ctx.renames[pred_idx].end()) {
            var = rename_it->second;
            ctx.renames[pred_idx].erase(rename_it);
         }

         uint32_t spill_id = ctx.allocate_spill_id(phi->definitions[0].regClass());
         ctx.affinities.emplace_back(std::pair<uint32_t, uint32_t>{def_spill_id, spill_id});
         aco_ptr<Pseudo_instruction> spill{create_instruction<Pseudo_instruction>(aco_opcode::p_spill, Format::PSEUDO, 2, 0)};
         spill->operands[0] = Operand(var);
         spill->operands[1] = Operand(spill_id);
         Block& pred = ctx.program->blocks[pred_idx];
         unsigned idx = pred.instructions.size();
         do {
            assert(idx != 0);
            idx--;
         } while (phi->opcode == aco_opcode::p_phi && pred.instructions[idx]->opcode != aco_opcode::p_logical_end);
         std::vector<aco_ptr<Instruction>>::iterator it = std::next(pred.instructions.begin(), idx);
         pred.instructions.insert(it, std::move(spill));
         ctx.spills_exit[pred_idx][phi->operands[i].getTemp()] = spill_id;
      }

      /* remove phi from instructions */
      phi.reset();
   }

   /* iterate all (other) spilled variables for which to spill at the predecessor */
   // TODO: would be better to have them sorted: first vgprs and first with longest distance
   for (std::pair<Temp, uint32_t> pair : ctx.spills_entry[block_idx]) {
      std::vector<unsigned> preds = pair.first.type() == RegType::vgpr ? block->logical_preds : block->linear_preds;

      for (unsigned pred_idx : preds) {
         /* add interferences between spilled variable and predecessors exit spills */
         for (std::pair<Temp, uint32_t> exit_spill : ctx.spills_exit[pred_idx]) {
            if (exit_spill.first == pair.first)
               continue;
            ctx.interferences[exit_spill.second].second.emplace(pair.second);
            ctx.interferences[pair.second].second.emplace(exit_spill.second);
         }

         /* variable is already spilled at predecessor */
         std::map<Temp, uint32_t>::iterator spilled = ctx.spills_exit[pred_idx].find(pair.first);
         if (spilled != ctx.spills_exit[pred_idx].end()) {
            if (spilled->second != pair.second)
               ctx.affinities.emplace_back(std::pair<uint32_t, uint32_t>{pair.second, spilled->second});
            continue;
         }

         /* variable is dead at predecessor, it must be from a phi: this works because of CSSA form */ // FIXME: lower_to_cssa()
         if (ctx.next_use_distances_end[pred_idx].find(pair.first) == ctx.next_use_distances_end[pred_idx].end())
            continue;

         /* variable is in register at predecessor and has to be spilled */
         /* rename if necessary */
         Temp var = pair.first;
         std::map<Temp, Temp>::iterator rename_it = ctx.renames[pred_idx].find(var);
         if (rename_it != ctx.renames[pred_idx].end()) {
            var = rename_it->second;
            ctx.renames[pred_idx].erase(rename_it);
         }

         aco_ptr<Pseudo_instruction> spill{create_instruction<Pseudo_instruction>(aco_opcode::p_spill, Format::PSEUDO, 2, 0)};
         spill->operands[0] = Operand(var);
         spill->operands[1] = Operand(pair.second);
         Block& pred = ctx.program->blocks[pred_idx];
         unsigned idx = pred.instructions.size();
         do {
            assert(idx != 0);
            idx--;
         } while (pair.first.type() == RegType::vgpr && pred.instructions[idx]->opcode != aco_opcode::p_logical_end);
         std::vector<aco_ptr<Instruction>>::iterator it = std::next(pred.instructions.begin(), idx);
         pred.instructions.insert(it, std::move(spill));
         ctx.spills_exit[pred.index][pair.first] = pair.second;
      }
   }

   /* iterate phis for which operands to reload */
   for (aco_ptr<Instruction>& phi : instructions) {
      assert(phi->opcode == aco_opcode::p_phi || phi->opcode == aco_opcode::p_linear_phi);
      assert(ctx.spills_entry[block_idx].find(phi->definitions[0].getTemp()) == ctx.spills_entry[block_idx].end());

      std::vector<unsigned>& preds = phi->opcode == aco_opcode::p_phi ? block->logical_preds : block->linear_preds;
      for (unsigned i = 0; i < phi->operands.size(); i++) {
         if (!phi->operands[i].isTemp())
            continue;
         unsigned pred_idx = preds[i];

         /* rename operand */
         if (ctx.spills_exit[pred_idx].find(phi->operands[i].getTemp()) == ctx.spills_exit[pred_idx].end()) {
            std::map<Temp, Temp>::iterator it = ctx.renames[pred_idx].find(phi->operands[i].getTemp());
            if (it != ctx.renames[pred_idx].end())
               phi->operands[i].setTemp(it->second);
            continue;
         }

         Temp tmp = phi->operands[i].getTemp();

         /* reload phi operand at end of predecessor block */
         Temp new_name = {ctx.program->allocateId(), tmp.regClass()};
         Block& pred = ctx.program->blocks[pred_idx];
         unsigned idx = pred.instructions.size();
         do {
            assert(idx != 0);
            idx--;
         } while (phi->opcode == aco_opcode::p_phi && pred.instructions[idx]->opcode != aco_opcode::p_logical_end);
         std::vector<aco_ptr<Instruction>>::iterator it = std::next(pred.instructions.begin(), idx);

         aco_ptr<Instruction> reload = do_reload(ctx, tmp, new_name, ctx.spills_exit[pred_idx][tmp]);
         pred.instructions.insert(it, std::move(reload));

         ctx.spills_exit[pred_idx].erase(tmp);
         ctx.renames[pred_idx][tmp] = new_name;
         phi->operands[i].setTemp(new_name);
      }
   }

   /* iterate live variables for which to reload */
   // TODO: reload at current block if variable is spilled on all predecessors
   for (std::pair<Temp, std::pair<uint32_t, uint32_t>> pair : ctx.next_use_distances_start[block_idx]) {
      /* skip spilled variables */
      if (ctx.spills_entry[block_idx].find(pair.first) != ctx.spills_entry[block_idx].end())
         continue;
      std::vector<unsigned> preds = pair.first.type() == RegType::vgpr ? block->logical_preds : block->linear_preds;

      /* variable is dead at predecessor, it must be from a phi */
      bool is_dead = false;
      for (unsigned pred_idx : preds) {
         if (ctx.next_use_distances_end[pred_idx].find(pair.first) == ctx.next_use_distances_end[pred_idx].end())
            is_dead = true;
      }
      if (is_dead)
         continue;
      for (unsigned pred_idx : preds) {
         /* the variable is not spilled at the predecessor */
         if (ctx.spills_exit[pred_idx].find(pair.first) == ctx.spills_exit[pred_idx].end())
            continue;

         /* variable is spilled at predecessor and has to be reloaded */
         Temp new_name = {ctx.program->allocateId(), pair.first.regClass()};
         Block& pred = ctx.program->blocks[pred_idx];
         unsigned idx = pred.instructions.size();
         do {
            assert(idx != 0);
            idx--;
         } while (pair.first.type() == RegType::vgpr && pred.instructions[idx]->opcode != aco_opcode::p_logical_end);
         std::vector<aco_ptr<Instruction>>::iterator it = std::next(pred.instructions.begin(), idx);

         aco_ptr<Instruction> reload = do_reload(ctx, pair.first, new_name, ctx.spills_exit[pred.index][pair.first]);
         pred.instructions.insert(it, std::move(reload));

         ctx.spills_exit[pred.index].erase(pair.first);
         ctx.renames[pred.index][pair.first] = new_name;
      }

      /* check if we have to create a new phi for this variable */
      Temp rename = Temp();
      bool is_same = true;
      for (unsigned pred_idx : preds) {
         if (ctx.renames[pred_idx].find(pair.first) == ctx.renames[pred_idx].end()) {
            if (rename == Temp())
               rename = pair.first;
            else
               is_same = rename == pair.first;
         } else {
            if (rename == Temp())
               rename = ctx.renames[pred_idx][pair.first];
            else
               is_same = rename == ctx.renames[pred_idx][pair.first];
         }

         if (!is_same)
            break;
      }

      if (!is_same) {
         /* the variable was renamed differently in the predecessors: we have to create a phi */
         aco_opcode opcode = pair.first.type() == RegType::vgpr ? aco_opcode::p_phi : aco_opcode::p_linear_phi;
         aco_ptr<Pseudo_instruction> phi{create_instruction<Pseudo_instruction>(opcode, Format::PSEUDO, preds.size(), 1)};
         rename = {ctx.program->allocateId(), pair.first.regClass()};
         for (unsigned i = 0; i < phi->operands.size(); i++) {
            Temp tmp;
            if (ctx.renames[preds[i]].find(pair.first) != ctx.renames[preds[i]].end())
               tmp = ctx.renames[preds[i]][pair.first];
            else if (preds[i] >= block_idx)
               tmp = rename;
            else
               tmp = pair.first;
            phi->operands[i] = Operand(tmp);
         }
         phi->definitions[0] = Definition(rename);
         instructions.emplace_back(std::move(phi));
      }

      /* the variable was renamed: add new name to renames */
      if (!(rename == Temp() || rename == pair.first))
         ctx.renames[block_idx][pair.first] = rename;
   }

   /* combine phis with instructions */
   unsigned idx = 0;
   while (!block->instructions[idx]) {
      idx++;
   }

   ctx.register_demand[block->index].erase(ctx.register_demand[block->index].begin(), ctx.register_demand[block->index].begin() + idx);
   ctx.register_demand[block->index].insert(ctx.register_demand[block->index].begin(), instructions.size(), RegisterDemand());

   std::vector<aco_ptr<Instruction>>::iterator start = std::next(block->instructions.begin(), idx);
   instructions.insert(instructions.end(), std::move_iterator<std::vector<aco_ptr<Instruction>>::iterator>(start),
               std::move_iterator<std::vector<aco_ptr<Instruction>>::iterator>(block->instructions.end()));
   block->instructions = std::move(instructions);
}

void process_block(spill_ctx& ctx, unsigned block_idx, Block* block,
                   std::map<Temp, uint32_t> &current_spills, RegisterDemand spilled_registers)
{
   std::vector<std::map<Temp, uint32_t>> local_next_use_distance;
   std::vector<aco_ptr<Instruction>> instructions;
   unsigned idx = 0;

   /* phis are handled separetely */
   while (block->instructions[idx]->opcode == aco_opcode::p_phi ||
          block->instructions[idx]->opcode == aco_opcode::p_linear_phi) {
      aco_ptr<Instruction>& instr = block->instructions[idx];
      for (const Operand& op : instr->operands) {
         /* prevent it's definining instruction from being DCE'd if it could be rematerialized */
         if (op.isTemp() && ctx.remat.count(op.getTemp()))
            ctx.remat_used[ctx.remat[op.getTemp()].instr] = true;
      }
      instructions.emplace_back(std::move(instr));
      idx++;
   }

   if (block->register_demand.exceeds(ctx.target_pressure))
      local_next_use_distance = local_next_uses(ctx, block);

   while (idx < block->instructions.size()) {
      aco_ptr<Instruction>& instr = block->instructions[idx];

      std::map<Temp, std::pair<Temp, uint32_t>> reloads;
      std::map<Temp, uint32_t> spills;
      /* rename and reload operands */
      for (Operand& op : instr->operands) {
         if (!op.isTemp())
            continue;
         if (current_spills.find(op.getTemp()) == current_spills.end()) {
            /* the Operand is in register: check if it was renamed */
            if (ctx.renames[block_idx].find(op.getTemp()) != ctx.renames[block_idx].end())
               op.setTemp(ctx.renames[block_idx][op.getTemp()]);
            /* prevent it's definining instruction from being DCE'd if it could be rematerialized */
            if (ctx.remat.count(op.getTemp()))
               ctx.remat_used[ctx.remat[op.getTemp()].instr] = true;
            continue;
         }
         /* the Operand is spilled: add it to reloads */
         Temp new_tmp = {ctx.program->allocateId(), op.regClass()};
         ctx.renames[block_idx][op.getTemp()] = new_tmp;
         reloads[new_tmp] = std::make_pair(op.getTemp(), current_spills[op.getTemp()]);
         current_spills.erase(op.getTemp());
         op.setTemp(new_tmp);
         spilled_registers -= new_tmp;
      }

      /* check if register demand is low enough before and after the current instruction */
      if (block->register_demand.exceeds(ctx.target_pressure)) {

         RegisterDemand new_demand = ctx.register_demand[block_idx][idx];
         if (idx == 0) {
            for (const Definition& def : instr->definitions) {
               if (!def.isTemp())
                  continue;
               new_demand += def.getTemp();
            }
         } else {
            new_demand.update(ctx.register_demand[block_idx][idx - 1]);
         }

         assert(!local_next_use_distance.empty());

         /* if reg pressure is too high, spill variable with furthest next use */
         while (RegisterDemand(new_demand - spilled_registers).exceeds(ctx.target_pressure)) {
            unsigned distance = 0;
            Temp to_spill;
            bool do_rematerialize = false;
            if (new_demand.vgpr - spilled_registers.vgpr > ctx.target_pressure.vgpr) {
               for (std::pair<Temp, uint32_t> pair : local_next_use_distance[idx]) {
                  bool can_rematerialize = ctx.remat.count(pair.first);
                  if (pair.first.type() == RegType::vgpr &&
                      ((pair.second > distance && can_rematerialize == do_rematerialize) ||
                       (can_rematerialize && !do_rematerialize && pair.second > idx)) &&
                      current_spills.find(pair.first) == current_spills.end() &&
                      ctx.spills_exit[block_idx].find(pair.first) == ctx.spills_exit[block_idx].end()) {
                     to_spill = pair.first;
                     distance = pair.second;
                     do_rematerialize = can_rematerialize;
                  }
               }
            } else {
               for (std::pair<Temp, uint32_t> pair : local_next_use_distance[idx]) {
                  bool can_rematerialize = ctx.remat.count(pair.first);
                  if (pair.first.type() == RegType::sgpr &&
                      ((pair.second > distance && can_rematerialize == do_rematerialize) ||
                       (can_rematerialize && !do_rematerialize && pair.second > idx)) &&
                      current_spills.find(pair.first) == current_spills.end() &&
                      ctx.spills_exit[block_idx].find(pair.first) == ctx.spills_exit[block_idx].end()) {
                     to_spill = pair.first;
                     distance = pair.second;
                     do_rematerialize = can_rematerialize;
                  }
               }
            }

            assert(distance != 0 && distance > idx);
            uint32_t spill_id = ctx.allocate_spill_id(to_spill.regClass());

            /* add interferences with currently spilled variables */
            for (std::pair<Temp, uint32_t> pair : current_spills) {
               ctx.interferences[spill_id].second.emplace(pair.second);
               ctx.interferences[pair.second].second.emplace(spill_id);
            }
            for (std::pair<Temp, std::pair<Temp, uint32_t>> pair : reloads) {
               ctx.interferences[spill_id].second.emplace(pair.second.second);
               ctx.interferences[pair.second.second].second.emplace(spill_id);
            }

            current_spills[to_spill] = spill_id;
            spilled_registers += to_spill;

            /* rename if necessary */
            if (ctx.renames[block_idx].find(to_spill) != ctx.renames[block_idx].end()) {
               to_spill = ctx.renames[block_idx][to_spill];
            }

            /* add spill to new instructions */
            aco_ptr<Pseudo_instruction> spill{create_instruction<Pseudo_instruction>(aco_opcode::p_spill, Format::PSEUDO, 2, 0)};
            spill->operands[0] = Operand(to_spill);
            spill->operands[1] = Operand(spill_id);
            instructions.emplace_back(std::move(spill));
         }
      }

      /* add reloads and instruction to new instructions */
      for (std::pair<Temp, std::pair<Temp, uint32_t>> pair : reloads) {
         aco_ptr<Instruction> reload = do_reload(ctx, pair.second.first, pair.first, pair.second.second);
         instructions.emplace_back(std::move(reload));
      }
      instructions.emplace_back(std::move(instr));
      idx++;
   }

   block->instructions = std::move(instructions);
   ctx.spills_exit[block_idx].insert(current_spills.begin(), current_spills.end());
}

void spill_block(spill_ctx& ctx, unsigned block_idx)
{
   Block* block = &ctx.program->blocks[block_idx];
   ctx.processed[block_idx] = true;

   /* determine set of variables which are spilled at the beginning of the block */
   RegisterDemand spilled_registers = init_live_in_vars(ctx, block, block_idx);

   /* add interferences for spilled variables */
   for (std::pair<Temp, uint32_t> x : ctx.spills_entry[block_idx]) {
      for (std::pair<Temp, uint32_t> y : ctx.spills_entry[block_idx])
         if (x.second != y.second)
            ctx.interferences[x.second].second.emplace(y.second);
   }

   bool is_loop_header = block->loop_nest_depth && ctx.loop_header.top()->index == block_idx;
   if (!is_loop_header) {
      /* add spill/reload code on incoming control flow edges */
      add_coupling_code(ctx, block, block_idx);
   }

   std::map<Temp, uint32_t> current_spills = ctx.spills_entry[block_idx];

   /* check conditions to process this block */
   bool process = RegisterDemand(block->register_demand - spilled_registers).exceeds(ctx.target_pressure) ||
                  !ctx.renames[block_idx].empty() ||
                  ctx.remat_used.size();

   std::map<Temp, uint32_t>::iterator it = current_spills.begin();
   while (!process && it != current_spills.end()) {
      if (ctx.next_use_distances_start[block_idx][it->first].first == block_idx)
         process = true;
      ++it;
   }

   if (process)
      process_block(ctx, block_idx, block, current_spills, spilled_registers);
   else
      ctx.spills_exit[block_idx].insert(current_spills.begin(), current_spills.end());

   /* check if the next block leaves the current loop */
   if (block->loop_nest_depth == 0 || ctx.program->blocks[block_idx + 1].loop_nest_depth >= block->loop_nest_depth)
      return;

   Block* loop_header = ctx.loop_header.top();

   /* preserve original renames at end of loop header block */
   std::map<Temp, Temp> renames = std::move(ctx.renames[loop_header->index]);

   /* add coupling code to all loop header predecessors */
   add_coupling_code(ctx, loop_header, loop_header->index);

   /* update remat_used for phis added in add_coupling_code() */
   for (aco_ptr<Instruction>& instr : loop_header->instructions) {
      if (!is_phi(instr))
         break;
      for (const Operand& op : instr->operands) {
         if (op.isTemp() && ctx.remat.count(op.getTemp()))
            ctx.remat_used[ctx.remat[op.getTemp()].instr] = true;
      }
   }

   /* propagate new renames through loop: i.e. repair the SSA */
   renames.swap(ctx.renames[loop_header->index]);
   for (std::pair<Temp, Temp> rename : renames) {
      for (unsigned idx = loop_header->index; idx <= block_idx; idx++) {
         Block& current = ctx.program->blocks[idx];
         std::vector<aco_ptr<Instruction>>::iterator instr_it = current.instructions.begin();

         /* first rename phis */
         while (instr_it != current.instructions.end()) {
            aco_ptr<Instruction>& phi = *instr_it;
            if (phi->opcode != aco_opcode::p_phi && phi->opcode != aco_opcode::p_linear_phi)
               break;
            /* no need to rename the loop header phis once again. this happened in add_coupling_code() */
            if (idx == loop_header->index) {
               instr_it++;
               continue;
            }

            for (Operand& op : phi->operands) {
               if (!op.isTemp())
                  continue;
               if (op.getTemp() == rename.first)
                  op.setTemp(rename.second);
            }
            instr_it++;
         }

         std::map<Temp, std::pair<uint32_t, uint32_t>>::iterator it = ctx.next_use_distances_start[idx].find(rename.first);

         /* variable is not live at beginning of this block */
         if (it == ctx.next_use_distances_start[idx].end())
            continue;

         /* if the variable is live at the block's exit, add rename */
         if (ctx.next_use_distances_end[idx].find(rename.first) != ctx.next_use_distances_end[idx].end())
            ctx.renames[idx].insert(rename);

         /* rename all uses in this block */
         bool renamed = false;
         while (!renamed && instr_it != current.instructions.end()) {
            aco_ptr<Instruction>& instr = *instr_it;
            for (Operand& op : instr->operands) {
               if (!op.isTemp())
                  continue;
               if (op.getTemp() == rename.first) {
                  op.setTemp(rename.second);
                  /* we can stop with this block as soon as the variable is spilled */
                  if (instr->opcode == aco_opcode::p_spill)
                    renamed = true;
               }
            }
            instr_it++;
         }
      }
   }

   /* remove loop header info from stack */
   ctx.loop_header.pop();
}

void assign_spill_slots(spill_ctx& ctx, unsigned spills_to_vgpr) {
   std::map<uint32_t, uint32_t> sgpr_slot;
   std::map<uint32_t, uint32_t> vgpr_slot;
   std::vector<bool> is_assigned(ctx.interferences.size());

   /* first, handle affinities: just merge all interferences into both spill ids */
   for (std::pair<uint32_t, uint32_t> pair : ctx.affinities) {
      assert(pair.first != pair.second);
      for (uint32_t id : ctx.interferences[pair.first].second)
         ctx.interferences[id].second.insert(pair.second);
      for (uint32_t id : ctx.interferences[pair.second].second)
         ctx.interferences[id].second.insert(pair.first);
      ctx.interferences[pair.first].second.insert(ctx.interferences[pair.second].second.begin(), ctx.interferences[pair.second].second.end());
      ctx.interferences[pair.second].second.insert(ctx.interferences[pair.first].second.begin(), ctx.interferences[pair.first].second.end());

      bool reloaded = ctx.is_reloaded[pair.first] || ctx.is_reloaded[pair.second];
      ctx.is_reloaded[pair.first] = ctx.is_reloaded[pair.second] = reloaded;
   }
   for (ASSERTED uint32_t i = 0; i < ctx.interferences.size(); i++)
      for (ASSERTED uint32_t id : ctx.interferences[i].second)
         assert(i != id);

   /* for each spill slot, assign as many spill ids as possible */
   std::vector<std::set<uint32_t>> spill_slot_interferences;
   unsigned slot_idx = 0;
   bool done = false;

   /* assign sgpr spill slots */
   while (!done) {
      done = true;
      for (unsigned id = 0; id < ctx.interferences.size(); id++) {
         if (is_assigned[id] || !ctx.is_reloaded[id])
            continue;
         if (ctx.interferences[id].first.type() != RegType::sgpr)
            continue;

         /* check interferences */
         bool interferes = false;
         for (unsigned i = slot_idx; i < slot_idx + ctx.interferences[id].first.size(); i++) {
            if (i == spill_slot_interferences.size())
               spill_slot_interferences.emplace_back(std::set<uint32_t>());
            if (spill_slot_interferences[i].find(id) != spill_slot_interferences[i].end() || i / 64 != slot_idx / 64) {
               interferes = true;
               break;
            }
         }
         if (interferes) {
            done = false;
            continue;
         }

         /* we found a spill id which can be assigned to current spill slot */
         sgpr_slot[id] = slot_idx;
         is_assigned[id] = true;
         for (unsigned i = slot_idx; i < slot_idx + ctx.interferences[id].first.size(); i++)
            spill_slot_interferences[i].insert(ctx.interferences[id].second.begin(), ctx.interferences[id].second.end());
      }
      slot_idx++;
   }

   slot_idx = 0;
   done = false;

   /* assign vgpr spill slots */
   while (!done) {
      done = true;
      for (unsigned id = 0; id < ctx.interferences.size(); id++) {
         if (is_assigned[id] || !ctx.is_reloaded[id])
            continue;
         if (ctx.interferences[id].first.type() != RegType::vgpr)
            continue;

         /* check interferences */
         bool interferes = false;
         for (unsigned i = slot_idx; i < slot_idx + ctx.interferences[id].first.size(); i++) {
            if (i == spill_slot_interferences.size())
               spill_slot_interferences.emplace_back(std::set<uint32_t>());
            /* check for interference and ensure that vector regs are stored next to each other */
            if (spill_slot_interferences[i].find(id) != spill_slot_interferences[i].end() || i / 64 != slot_idx / 64) {
               interferes = true;
               break;
            }
         }
         if (interferes) {
            done = false;
            continue;
         }

         /* we found a spill id which can be assigned to current spill slot */
         vgpr_slot[id] = slot_idx;
         is_assigned[id] = true;
         for (unsigned i = slot_idx; i < slot_idx + ctx.interferences[id].first.size(); i++)
            spill_slot_interferences[i].insert(ctx.interferences[id].second.begin(), ctx.interferences[id].second.end());
      }
      slot_idx++;
   }

   for (unsigned id = 0; id < is_assigned.size(); id++)
      assert(is_assigned[id] || !ctx.is_reloaded[id]);

   for (std::pair<uint32_t, uint32_t> pair : ctx.affinities) {
      assert(is_assigned[pair.first] == is_assigned[pair.second]);
      if (!is_assigned[pair.first])
         continue;
      assert(ctx.is_reloaded[pair.first] == ctx.is_reloaded[pair.second]);
      assert(ctx.interferences[pair.first].first.type() == ctx.interferences[pair.second].first.type());
      if (ctx.interferences[pair.first].first.type() == RegType::sgpr)
         assert(sgpr_slot[pair.first] == sgpr_slot[pair.second]);
      else
         assert(vgpr_slot[pair.first] == vgpr_slot[pair.second]);
   }

   /* hope, we didn't mess up */
   std::vector<Temp> vgpr_spill_temps((spill_slot_interferences.size() + 63) / 64);
   assert(vgpr_spill_temps.size() <= spills_to_vgpr);

   /* replace pseudo instructions with actual hardware instructions */
   unsigned last_top_level_block_idx = 0;
   std::vector<bool> reload_in_loop(vgpr_spill_temps.size());
   for (Block& block : ctx.program->blocks) {

      /* after loops, we insert a user if there was a reload inside the loop */
      if (block.loop_nest_depth == 0) {
         int end_vgprs = 0;
         for (unsigned i = 0; i < vgpr_spill_temps.size(); i++) {
            if (reload_in_loop[i])
               end_vgprs++;
         }

         if (end_vgprs > 0) {
            aco_ptr<Instruction> destr{create_instruction<Pseudo_instruction>(aco_opcode::p_end_linear_vgpr, Format::PSEUDO, end_vgprs, 0)};
            int k = 0;
            for (unsigned i = 0; i < vgpr_spill_temps.size(); i++) {
               if (reload_in_loop[i])
                  destr->operands[k++] = Operand(vgpr_spill_temps[i]);
               reload_in_loop[i] = false;
            }
            /* find insertion point */
            std::vector<aco_ptr<Instruction>>::iterator it = block.instructions.begin();
            while ((*it)->opcode == aco_opcode::p_linear_phi || (*it)->opcode == aco_opcode::p_phi)
               ++it;
            block.instructions.insert(it, std::move(destr));
         }
      }

      if (block.kind & block_kind_top_level && !block.linear_preds.empty()) {
         last_top_level_block_idx = block.index;

         /* check if any spilled variables use a created linear vgpr, otherwise destroy them */
         for (unsigned i = 0; i < vgpr_spill_temps.size(); i++) {
            if (vgpr_spill_temps[i] == Temp())
               continue;

            bool can_destroy = true;
            for (std::pair<Temp, uint32_t> pair : ctx.spills_exit[block.linear_preds[0]]) {

               if (sgpr_slot.find(pair.second) != sgpr_slot.end() &&
                   sgpr_slot[pair.second] / 64 == i) {
                  can_destroy = false;
                  break;
               }
            }
            if (can_destroy)
               vgpr_spill_temps[i] = Temp();
         }
      }

      std::vector<aco_ptr<Instruction>>::iterator it;
      std::vector<aco_ptr<Instruction>> instructions;
      instructions.reserve(block.instructions.size());
      for (it = block.instructions.begin(); it != block.instructions.end(); ++it) {

         if ((*it)->opcode == aco_opcode::p_spill) {
            uint32_t spill_id = (*it)->operands[1].constantValue();

            if (!ctx.is_reloaded[spill_id]) {
               /* never reloaded, so don't spill */
            } else if (vgpr_slot.find(spill_id) != vgpr_slot.end()) {
               /* spill vgpr */
               ctx.program->config->spilled_vgprs += (*it)->operands[0].size();

               assert(false && "vgpr spilling not yet implemented.");
            } else if (sgpr_slot.find(spill_id) != sgpr_slot.end()) {
               ctx.program->config->spilled_sgprs += (*it)->operands[0].size();

               uint32_t spill_slot = sgpr_slot[spill_id];

               /* check if the linear vgpr already exists */
               if (vgpr_spill_temps[spill_slot / 64] == Temp()) {
                  Temp linear_vgpr = {ctx.program->allocateId(), v1.as_linear()};
                  vgpr_spill_temps[spill_slot / 64] = linear_vgpr;
                  aco_ptr<Pseudo_instruction> create{create_instruction<Pseudo_instruction>(aco_opcode::p_start_linear_vgpr, Format::PSEUDO, 0, 1)};
                  create->definitions[0] = Definition(linear_vgpr);
                  /* find the right place to insert this definition */
                  if (last_top_level_block_idx == block.index) {
                     /* insert right before the current instruction */
                     instructions.emplace_back(std::move(create));
                  } else {
                     assert(last_top_level_block_idx < block.index);
                     /* insert before the branch at last top level block */
                     std::vector<aco_ptr<Instruction>>& instructions = ctx.program->blocks[last_top_level_block_idx].instructions;
                     instructions.insert(std::next(instructions.begin(), instructions.size() - 1), std::move(create));
                  }
               }

               /* spill sgpr: just add the vgpr temp to operands */
               Pseudo_instruction* spill = create_instruction<Pseudo_instruction>(aco_opcode::p_spill, Format::PSEUDO, 3, 0);
               spill->operands[0] = Operand(vgpr_spill_temps[spill_slot / 64]);
               spill->operands[1] = Operand(spill_slot % 64);
               spill->operands[2] = (*it)->operands[0];
               instructions.emplace_back(aco_ptr<Instruction>(spill));
            } else {
               unreachable("No spill slot assigned for spill id");
            }

         } else if ((*it)->opcode == aco_opcode::p_reload) {
            uint32_t spill_id = (*it)->operands[0].constantValue();
            assert(ctx.is_reloaded[spill_id]);

            if (vgpr_slot.find(spill_id) != vgpr_slot.end()) {
               /* reload vgpr */
               assert(false && "vgpr spilling not yet implemented.");

            } else if (sgpr_slot.find(spill_id) != sgpr_slot.end()) {
               uint32_t spill_slot = sgpr_slot[spill_id];
               reload_in_loop[spill_slot / 64] = block.loop_nest_depth > 0;

               /* check if the linear vgpr already exists */
               if (vgpr_spill_temps[spill_slot / 64] == Temp()) {
                  Temp linear_vgpr = {ctx.program->allocateId(), v1.as_linear()};
                  vgpr_spill_temps[spill_slot / 64] = linear_vgpr;
                  aco_ptr<Pseudo_instruction> create{create_instruction<Pseudo_instruction>(aco_opcode::p_start_linear_vgpr, Format::PSEUDO, 0, 1)};
                  create->definitions[0] = Definition(linear_vgpr);
                  /* find the right place to insert this definition */
                  if (last_top_level_block_idx == block.index) {
                     /* insert right before the current instruction */
                     instructions.emplace_back(std::move(create));
                  } else {
                     assert(last_top_level_block_idx < block.index);
                     /* insert before the branch at last top level block */
                     std::vector<aco_ptr<Instruction>>& instructions = ctx.program->blocks[last_top_level_block_idx].instructions;
                     instructions.insert(std::next(instructions.begin(), instructions.size() - 1), std::move(create));
                  }
               }

               /* reload sgpr: just add the vgpr temp to operands */
               Pseudo_instruction* reload = create_instruction<Pseudo_instruction>(aco_opcode::p_reload, Format::PSEUDO, 2, 1);
               reload->operands[0] = Operand(vgpr_spill_temps[spill_slot / 64]);
               reload->operands[1] = Operand(spill_slot % 64);
               reload->definitions[0] = (*it)->definitions[0];
               instructions.emplace_back(aco_ptr<Instruction>(reload));
            } else {
               unreachable("No spill slot assigned for spill id");
            }
         } else if (!ctx.remat_used.count(it->get()) || ctx.remat_used[it->get()]) {
            instructions.emplace_back(std::move(*it));
         }

      }
      block.instructions = std::move(instructions);
   }

   /* SSA elimination inserts copies for logical phis right before p_logical_end
    * So if a linear vgpr is used between that p_logical_end and the branch,
    * we need to ensure logical phis don't choose a definition which aliases
    * the linear vgpr.
    * TODO: Moving the spills and reloads to before p_logical_end might produce
    *       slightly better code. */
   for (Block& block : ctx.program->blocks) {
      /* loops exits are already handled */
      if (block.logical_preds.size() <= 1)
         continue;

      bool has_logical_phis = false;
      for (aco_ptr<Instruction>& instr : block.instructions) {
         if (instr->opcode == aco_opcode::p_phi) {
            has_logical_phis = true;
            break;
         } else if (instr->opcode != aco_opcode::p_linear_phi) {
            break;
         }
      }
      if (!has_logical_phis)
         continue;

      std::set<Temp> vgprs;
      for (unsigned pred_idx : block.logical_preds) {
         Block& pred = ctx.program->blocks[pred_idx];
         for (int i = pred.instructions.size() - 1; i >= 0; i--) {
            aco_ptr<Instruction>& pred_instr = pred.instructions[i];
            if (pred_instr->opcode == aco_opcode::p_logical_end) {
               break;
            } else if (pred_instr->opcode == aco_opcode::p_spill ||
                       pred_instr->opcode == aco_opcode::p_reload) {
               vgprs.insert(pred_instr->operands[0].getTemp());
            }
         }
      }
      if (!vgprs.size())
         continue;

      aco_ptr<Instruction> destr{create_instruction<Pseudo_instruction>(aco_opcode::p_end_linear_vgpr, Format::PSEUDO, vgprs.size(), 0)};
      int k = 0;
      for (Temp tmp : vgprs) {
         destr->operands[k++] = Operand(tmp);
      }
      /* find insertion point */
      std::vector<aco_ptr<Instruction>>::iterator it = block.instructions.begin();
      while ((*it)->opcode == aco_opcode::p_linear_phi || (*it)->opcode == aco_opcode::p_phi)
         ++it;
      block.instructions.insert(it, std::move(destr));
   }
}

} /* end namespace */


void spill(Program* program, live& live_vars, const struct radv_nir_compiler_options *options)
{
   program->config->spilled_vgprs = 0;
   program->config->spilled_sgprs = 0;

   /* no spilling when wave count is already high */
   if (program->num_waves >= 6)
      return;

   /* else, we check if we can improve things a bit */
   uint16_t total_sgpr_regs = options->chip_class >= GFX8 ? 800 : 512;
   uint16_t max_addressible_sgpr = program->sgpr_limit;

   /* calculate target register demand */
   RegisterDemand max_reg_demand;
   for (Block& block : program->blocks) {
      max_reg_demand.update(block.register_demand);
   }

   RegisterDemand target_pressure = {256, int16_t(max_addressible_sgpr)};
   unsigned num_waves = 1;
   int spills_to_vgpr = (max_reg_demand.sgpr - max_addressible_sgpr + 63) / 64;

   /* test if it possible to increase occupancy with little spilling */
   for (unsigned num_waves_next = 2; num_waves_next <= 8; num_waves_next++) {
      RegisterDemand target_pressure_next = {int16_t((256 / num_waves_next) & ~3),
                                             int16_t(std::min<uint16_t>(((total_sgpr_regs / num_waves_next) & ~7) - 2, max_addressible_sgpr))};

      /* Currently no vgpr spilling supported.
       * Spill as many sgprs as necessary to not hinder occupancy */
      if (max_reg_demand.vgpr > target_pressure_next.vgpr)
         break;
      /* check that we have enough free vgprs to spill sgprs to */
      if (max_reg_demand.sgpr > target_pressure_next.sgpr) {
         /* add some buffer in case graph coloring is not perfect ... */
         const int spills_to_vgpr_next = (max_reg_demand.sgpr - target_pressure_next.sgpr + 63 + 32) / 64;
         if (spills_to_vgpr_next + max_reg_demand.vgpr > target_pressure_next.vgpr)
            break;
         spills_to_vgpr = spills_to_vgpr_next;
      }

      target_pressure = target_pressure_next;
      num_waves = num_waves_next;
   }

   assert(max_reg_demand.vgpr <= target_pressure.vgpr && "VGPR spilling not yet supported.");
   /* nothing to do */
   if (num_waves == program->num_waves)
      return;

   /* initialize ctx */
   spill_ctx ctx(target_pressure, program, live_vars.register_demand);
   compute_global_next_uses(ctx, live_vars.live_out);
   get_rematerialize_info(ctx);

   /* create spills and reloads */
   for (unsigned i = 0; i < program->blocks.size(); i++)
      spill_block(ctx, i);

   /* assign spill slots and DCE rematerialized code */
   assign_spill_slots(ctx, spills_to_vgpr);

   /* update live variable information */
   live_vars = live_var_analysis(program, options);

   assert(program->num_waves >= num_waves);
}

}

