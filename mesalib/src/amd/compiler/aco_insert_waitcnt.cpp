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

#include <algorithm>
#include <map>

#include "aco_ir.h"
#include "vulkan/radv_shader.h"

namespace aco {

namespace {

/**
 * The general idea of this pass is:
 * The CFG is traversed in reverse postorder (forward).
 * Per BB one wait_ctx is maintained.
 * The in-context is the joined out-contexts of the predecessors.
 * The context contains a map: gpr -> wait_entry
 * consisting of the information about the cnt values to be waited for.
 * Note: After merge-nodes, it might occur that for the same register
 *       multiple cnt values are to be waited for.
 *
 * The values are updated according to the encountered instructions:
 * - additional events increment the counter of waits of the same type
 * - or erase gprs with counters higher than to be waited for.
 */

// TODO: do a more clever insertion of wait_cnt (lgkm_cnt) when there is a load followed by a use of a previous load

/* Instructions of the same event will finish in-order except for smem
 * and maybe flat. Instructions of different events may not finish in-order. */
enum wait_event : uint16_t {
   event_smem = 1 << 0,
   event_lds = 1 << 1,
   event_gds = 1 << 2,
   event_vmem = 1 << 3,
   event_vmem_store = 1 << 4, /* GFX10+ */
   event_flat = 1 << 5,
   event_exp_pos = 1 << 6,
   event_exp_param = 1 << 7,
   event_exp_mrt_null = 1 << 8,
   event_gds_gpr_lock = 1 << 9,
   event_vmem_gpr_lock = 1 << 10,
};

enum counter_type : uint8_t {
   counter_exp = 1 << 0,
   counter_lgkm = 1 << 1,
   counter_vm = 1 << 2,
   counter_vs = 1 << 3,
};

static const uint16_t exp_events = event_exp_pos | event_exp_param | event_exp_mrt_null | event_gds_gpr_lock | event_vmem_gpr_lock;
static const uint16_t lgkm_events = event_smem | event_lds | event_gds | event_flat;
static const uint16_t vm_events = event_vmem | event_flat;
static const uint16_t vs_events = event_vmem_store;

uint8_t get_counters_for_event(wait_event ev)
{
   switch (ev) {
   case event_smem:
   case event_lds:
   case event_gds:
      return counter_lgkm;
   case event_vmem:
      return counter_vm;
   case event_vmem_store:
      return counter_vs;
   case event_flat:
      return counter_vm | counter_lgkm;
   case event_exp_pos:
   case event_exp_param:
   case event_exp_mrt_null:
   case event_gds_gpr_lock:
   case event_vmem_gpr_lock:
      return counter_exp;
   default:
      return 0;
   }
}

struct wait_imm {
   static const uint8_t unset_counter = 0xff;

   uint8_t vm;
   uint8_t exp;
   uint8_t lgkm;
   uint8_t vs;

   wait_imm() :
      vm(unset_counter), exp(unset_counter), lgkm(unset_counter), vs(unset_counter) {}
   wait_imm(uint16_t vm_, uint16_t exp_, uint16_t lgkm_, uint16_t vs_) :
      vm(vm_), exp(exp_), lgkm(lgkm_), vs(vs_) {}

   uint16_t pack(enum chip_class chip) const
   {
      uint16_t imm = 0;
      assert(exp == unset_counter || exp <= 0x7);
      switch (chip) {
      case GFX10:
         assert(lgkm == unset_counter || lgkm <= 0x3f);
         assert(vm == unset_counter || vm <= 0x3f);
         imm = ((vm & 0x30) << 10) | ((lgkm & 0x3f) << 8) | ((exp & 0x7) << 4) | (vm & 0xf);
         break;
      case GFX9:
         assert(lgkm == unset_counter || lgkm <= 0xf);
         assert(vm == unset_counter || vm <= 0x3f);
         imm = ((vm & 0x30) << 10) | ((lgkm & 0xf) << 8) | ((exp & 0x7) << 4) | (vm & 0xf);
         break;
      default:
         assert(lgkm == unset_counter || lgkm <= 0xf);
         assert(vm == unset_counter || vm <= 0xf);
         imm = ((lgkm & 0xf) << 8) | ((exp & 0x7) << 4) | (vm & 0xf);
         break;
      }
      if (chip < GFX9 && vm == wait_imm::unset_counter)
         imm |= 0xc000; /* should have no effect on pre-GFX9 and now we won't have to worry about the architecture when interpreting the immediate */
      if (chip < GFX10 && lgkm == wait_imm::unset_counter)
         imm |= 0x3000; /* should have no effect on pre-GFX10 and now we won't have to worry about the architecture when interpreting the immediate */
      return imm;
   }

   void combine(const wait_imm& other)
   {
      vm = std::min(vm, other.vm);
      exp = std::min(exp, other.exp);
      lgkm = std::min(lgkm, other.lgkm);
      vs = std::min(vs, other.vs);
   }

   bool empty() const
   {
      return vm == unset_counter && exp == unset_counter &&
             lgkm == unset_counter && vs == unset_counter;
   }
};

struct wait_entry {
   wait_imm imm;
   uint16_t events; /* use wait_event notion */
   uint8_t counters; /* use counter_type notion */
   bool wait_on_read:1;
   bool logical:1;

   wait_entry(wait_event event, wait_imm imm, bool logical, bool wait_on_read)
           : imm(imm), events(event), counters(get_counters_for_event(event)),
             wait_on_read(wait_on_read), logical(logical) {}

   void join(const wait_entry& other)
   {
      events |= other.events;
      counters |= other.counters;
      imm.combine(other.imm);
      wait_on_read = wait_on_read || other.wait_on_read;
      assert(logical == other.logical);
   }

   void remove_counter(counter_type counter)
   {
      counters &= ~counter;

      if (counter == counter_lgkm) {
         imm.lgkm = wait_imm::unset_counter;
         events &= ~(event_smem | event_lds | event_gds);
      }

      if (counter == counter_vm) {
         imm.vm = wait_imm::unset_counter;
         events &= ~event_vmem;
      }

      if (counter == counter_exp) {
         imm.exp = wait_imm::unset_counter;
         events &= ~(event_exp_pos | event_exp_param | event_exp_mrt_null | event_gds_gpr_lock | event_vmem_gpr_lock);
      }

      if (counter == counter_vs) {
         imm.vs = wait_imm::unset_counter;
         events &= ~event_vmem_store;
      }

      if (!(counters & counter_lgkm) && !(counters & counter_vm))
         events &= ~event_flat;
   }
};

struct wait_ctx {
   Program *program;
   enum chip_class chip_class;
   uint16_t max_vm_cnt;
   uint16_t max_exp_cnt;
   uint16_t max_lgkm_cnt;
   uint16_t max_vs_cnt;
   uint16_t unordered_events = event_smem | event_flat;

   uint8_t vm_cnt = 0;
   uint8_t exp_cnt = 0;
   uint8_t lgkm_cnt = 0;
   uint8_t vs_cnt = 0;
   bool pending_flat_lgkm = false;
   bool pending_flat_vm = false;
   bool pending_s_buffer_store = false; /* GFX10 workaround */

   wait_imm barrier_imm[barrier_count];

   std::map<PhysReg,wait_entry> gpr_map;

   wait_ctx() {}
   wait_ctx(Program *program_)
           : program(program_),
             chip_class(program_->chip_class),
             max_vm_cnt(program_->chip_class >= GFX9 ? 62 : 14),
             max_exp_cnt(6),
             max_lgkm_cnt(program_->chip_class >= GFX10 ? 62 : 14),
             max_vs_cnt(program_->chip_class >= GFX10 ? 62 : 0),
             unordered_events(event_smem | (program_->chip_class < GFX10 ? event_flat : 0)) {}

   void join(const wait_ctx* other, bool logical)
   {
      exp_cnt = std::max(exp_cnt, other->exp_cnt);
      vm_cnt = std::max(vm_cnt, other->vm_cnt);
      lgkm_cnt = std::max(lgkm_cnt, other->lgkm_cnt);
      vs_cnt = std::max(vs_cnt, other->vs_cnt);
      pending_flat_lgkm |= other->pending_flat_lgkm;
      pending_flat_vm |= other->pending_flat_vm;
      pending_s_buffer_store |= other->pending_s_buffer_store;

      for (std::pair<PhysReg,wait_entry> entry : other->gpr_map)
      {
         std::map<PhysReg,wait_entry>::iterator it = gpr_map.find(entry.first);
         if (entry.second.logical != logical)
            continue;

         if (it != gpr_map.end())
            it->second.join(entry.second);
         else
            gpr_map.insert(entry);
      }

      for (unsigned i = 0; i < barrier_count; i++)
         barrier_imm[i].combine(other->barrier_imm[i]);
   }
};

wait_imm check_instr(Instruction* instr, wait_ctx& ctx)
{
   wait_imm wait;

   for (const Operand op : instr->operands) {
      if (op.isConstant() || op.isUndefined())
         continue;

      /* check consecutively read gprs */
      for (unsigned j = 0; j < op.size(); j++) {
         PhysReg reg{op.physReg() + j};
         std::map<PhysReg,wait_entry>::iterator it = ctx.gpr_map.find(reg);
         if (it == ctx.gpr_map.end() || !it->second.wait_on_read)
            continue;

         wait.combine(it->second.imm);
      }
   }

   for (const Definition& def : instr->definitions) {
      /* check consecutively written gprs */
      for (unsigned j = 0; j < def.getTemp().size(); j++)
      {
         PhysReg reg{def.physReg() + j};

         std::map<PhysReg,wait_entry>::iterator it = ctx.gpr_map.find(reg);
         if (it == ctx.gpr_map.end())
            continue;

         /* Vector Memory reads and writes return in the order they were issued */
         if (instr->isVMEM() && ((it->second.events & vm_events) == event_vmem)) {
            it->second.remove_counter(counter_vm);
            if (!it->second.counters)
               it = ctx.gpr_map.erase(it);
            continue;
         }

         /* LDS reads and writes return in the order they were issued. same for GDS */
         if (instr->format == Format::DS) {
            bool gds = static_cast<DS_instruction*>(instr)->gds;
            if ((it->second.events & lgkm_events) == (gds ? event_gds : event_lds)) {
               it->second.remove_counter(counter_lgkm);
               if (!it->second.counters)
                  it = ctx.gpr_map.erase(it);
               continue;
            }
         }

         wait.combine(it->second.imm);
      }
   }

   return wait;
}

wait_imm kill(Instruction* instr, wait_ctx& ctx)
{
   wait_imm imm;
   if (ctx.exp_cnt || ctx.vm_cnt || ctx.lgkm_cnt)
      imm.combine(check_instr(instr, ctx));

   if (ctx.chip_class >= GFX10) {
      /* Seems to be required on GFX10 to achieve correct behaviour.
       * It shouldn't cost anything anyways since we're about to do s_endpgm.
       */
      if (ctx.lgkm_cnt && instr->opcode == aco_opcode::s_dcache_wb)
         imm.lgkm = 0;

      /* GFX10: A store followed by a load at the same address causes a problem because
       * the load doesn't load the correct values unless we wait for the store first.
       * This is NOT mitigated by an s_nop.
       *
       * TODO: Refine this when we have proper alias analysis.
       */
      SMEM_instruction *smem = static_cast<SMEM_instruction *>(instr);
      if (ctx.pending_s_buffer_store &&
          !smem->definitions.empty() &&
          !smem->can_reorder && smem->barrier == barrier_buffer) {
         imm.lgkm = 0;
      }
   }

   if (instr->format == Format::PSEUDO_BARRIER) {
      unsigned* bsize = ctx.program->info->cs.block_size;
      unsigned workgroup_size = bsize[0] * bsize[1] * bsize[2];
      switch (instr->opcode) {
      case aco_opcode::p_memory_barrier_all:
         for (unsigned i = 0; i < barrier_count; i++) {
            if ((1 << i) == barrier_shared && workgroup_size <= 64)
               continue;
            imm.combine(ctx.barrier_imm[i]);
         }
         break;
      case aco_opcode::p_memory_barrier_atomic:
         imm.combine(ctx.barrier_imm[ffs(barrier_atomic) - 1]);
         break;
      /* see comment in aco_scheduler.cpp's can_move_instr() on why these barriers are merged */
      case aco_opcode::p_memory_barrier_buffer:
      case aco_opcode::p_memory_barrier_image:
         imm.combine(ctx.barrier_imm[ffs(barrier_buffer) - 1]);
         imm.combine(ctx.barrier_imm[ffs(barrier_image) - 1]);
         break;
      case aco_opcode::p_memory_barrier_shared:
         if (workgroup_size > 64)
            imm.combine(ctx.barrier_imm[ffs(barrier_shared) - 1]);
         break;
      default:
         assert(false);
         break;
      }
   }

   if (!imm.empty()) {
      if (ctx.pending_flat_vm && imm.vm != wait_imm::unset_counter)
         imm.vm = 0;
      if (ctx.pending_flat_lgkm && imm.lgkm != wait_imm::unset_counter)
         imm.lgkm = 0;

      /* reset counters */
      ctx.exp_cnt = std::min(ctx.exp_cnt, imm.exp);
      ctx.vm_cnt = std::min(ctx.vm_cnt, imm.vm);
      ctx.lgkm_cnt = std::min(ctx.lgkm_cnt, imm.lgkm);
      ctx.vs_cnt = std::min(ctx.vs_cnt, imm.vs);

      /* update barrier wait imms */
      for (unsigned i = 0; i < barrier_count; i++) {
         wait_imm& bar = ctx.barrier_imm[i];
         if (bar.exp != wait_imm::unset_counter && imm.exp <= bar.exp)
            bar.exp = wait_imm::unset_counter;
         if (bar.vm != wait_imm::unset_counter && imm.vm <= bar.vm)
            bar.vm = wait_imm::unset_counter;
         if (bar.lgkm != wait_imm::unset_counter && imm.lgkm <= bar.lgkm)
            bar.lgkm = wait_imm::unset_counter;
         if (bar.vs != wait_imm::unset_counter && imm.vs <= bar.vs)
            bar.vs = wait_imm::unset_counter;
      }

      /* remove all gprs with higher counter from map */
      std::map<PhysReg,wait_entry>::iterator it = ctx.gpr_map.begin();
      while (it != ctx.gpr_map.end())
      {
         if (imm.exp != wait_imm::unset_counter && imm.exp <= it->second.imm.exp)
            it->second.remove_counter(counter_exp);
         if (imm.vm != wait_imm::unset_counter && imm.vm <= it->second.imm.vm)
            it->second.remove_counter(counter_vm);
         if (imm.lgkm != wait_imm::unset_counter && imm.lgkm <= it->second.imm.lgkm)
            it->second.remove_counter(counter_lgkm);
         if (imm.lgkm != wait_imm::unset_counter && imm.vs <= it->second.imm.vs)
            it->second.remove_counter(counter_vs);
         if (!it->second.counters)
            it = ctx.gpr_map.erase(it);
         else
            it++;
      }
   }

   if (imm.vm == 0)
      ctx.pending_flat_vm = false;
   if (imm.lgkm == 0) {
      ctx.pending_flat_lgkm = false;
      ctx.pending_s_buffer_store = false;
   }

   return imm;
}

void update_barrier_imm(wait_ctx& ctx, uint8_t counters, barrier_interaction barrier)
{
   unsigned barrier_index = ffs(barrier) - 1;
   for (unsigned i = 0; i < barrier_count; i++) {
      wait_imm& bar = ctx.barrier_imm[i];
      if (i == barrier_index) {
         if (counters & counter_lgkm)
            bar.lgkm = 0;
         if (counters & counter_vm)
            bar.vm = 0;
         if (counters & counter_exp)
            bar.exp = 0;
         if (counters & counter_vs)
            bar.vs = 0;
      } else {
         if (counters & counter_lgkm && bar.lgkm != wait_imm::unset_counter && bar.lgkm < ctx.max_lgkm_cnt)
            bar.lgkm++;
         if (counters & counter_vm && bar.vm != wait_imm::unset_counter && bar.vm < ctx.max_vm_cnt)
            bar.vm++;
         if (counters & counter_exp && bar.exp != wait_imm::unset_counter && bar.exp < ctx.max_exp_cnt)
            bar.exp++;
         if (counters & counter_vs && bar.vs != wait_imm::unset_counter && bar.vs < ctx.max_vs_cnt)
            bar.vs++;
      }
   }
}

void update_counters(wait_ctx& ctx, wait_event event, barrier_interaction barrier=barrier_none)
{
   uint8_t counters = get_counters_for_event(event);

   if (counters & counter_lgkm && ctx.lgkm_cnt <= ctx.max_lgkm_cnt)
      ctx.lgkm_cnt++;
   if (counters & counter_vm && ctx.vm_cnt <= ctx.max_vm_cnt)
      ctx.vm_cnt++;
   if (counters & counter_exp && ctx.exp_cnt <= ctx.max_exp_cnt)
      ctx.exp_cnt++;
   if (counters & counter_vs && ctx.vs_cnt <= ctx.max_vs_cnt)
      ctx.vs_cnt++;

   update_barrier_imm(ctx, counters, barrier);

   if (ctx.unordered_events & event)
      return;

   if (ctx.pending_flat_lgkm)
      counters &= ~counter_lgkm;
   if (ctx.pending_flat_vm)
      counters &= ~counter_vm;

   for (std::pair<const PhysReg,wait_entry>& e : ctx.gpr_map) {
      wait_entry& entry = e.second;

      if (entry.events & ctx.unordered_events)
         continue;

      assert(entry.events);

      if ((counters & counter_exp) && (entry.events & exp_events) == event && entry.imm.exp < ctx.max_exp_cnt)
         entry.imm.exp++;
      if ((counters & counter_lgkm) && (entry.events & lgkm_events) == event && entry.imm.lgkm < ctx.max_lgkm_cnt)
         entry.imm.lgkm++;
      if ((counters & counter_vm) && (entry.events & vm_events) == event && entry.imm.vm < ctx.max_vm_cnt)
         entry.imm.vm++;
      if ((counters & counter_vs) && (entry.events & vs_events) == event && entry.imm.vs < ctx.max_vs_cnt)
         entry.imm.vs++;
   }
}

void update_counters_for_flat_load(wait_ctx& ctx, barrier_interaction barrier=barrier_none)
{
   assert(ctx.chip_class < GFX10);

   if (ctx.lgkm_cnt <= ctx.max_lgkm_cnt)
      ctx.lgkm_cnt++;
   if (ctx.lgkm_cnt <= ctx.max_vm_cnt)
   ctx.vm_cnt++;

   update_barrier_imm(ctx, counter_vm | counter_lgkm, barrier);

   for (std::pair<PhysReg,wait_entry> e : ctx.gpr_map)
   {
      if (e.second.counters & counter_vm)
         e.second.imm.vm = 0;
      if (e.second.counters & counter_lgkm)
         e.second.imm.lgkm = 0;
   }
   ctx.pending_flat_lgkm = true;
   ctx.pending_flat_vm = true;
}

void insert_wait_entry(wait_ctx& ctx, PhysReg reg, RegClass rc, wait_event event, bool wait_on_read)
{
   uint16_t counters = get_counters_for_event(event);
   wait_imm imm;
   if (counters & counter_lgkm)
      imm.lgkm = 0;
   if (counters & counter_vm)
      imm.vm = 0;
   if (counters & counter_exp)
      imm.exp = 0;
   if (counters & counter_vs)
      imm.vs = 0;

   wait_entry new_entry(event, imm, !rc.is_linear(), wait_on_read);

   for (unsigned i = 0; i < rc.size(); i++) {
      auto it = ctx.gpr_map.emplace(PhysReg{reg.reg+i}, new_entry);
      if (!it.second)
         it.first->second.join(new_entry);
   }
}

void insert_wait_entry(wait_ctx& ctx, Operand op, wait_event event)
{
   if (!op.isConstant() && !op.isUndefined())
      insert_wait_entry(ctx, op.physReg(), op.regClass(), event, false);
}

void insert_wait_entry(wait_ctx& ctx, Definition def, wait_event event)
{
   insert_wait_entry(ctx, def.physReg(), def.regClass(), event, true);
}

void gen(Instruction* instr, wait_ctx& ctx)
{
   switch (instr->format) {
   case Format::EXP: {
      Export_instruction* exp_instr = static_cast<Export_instruction*>(instr);

      wait_event ev;
      if (exp_instr->dest <= 9)
         ev = event_exp_mrt_null;
      else if (exp_instr->dest <= 15)
         ev = event_exp_pos;
      else
         ev = event_exp_param;
      update_counters(ctx, ev);

      /* insert new entries for exported vgprs */
      for (unsigned i = 0; i < 4; i++)
      {
         if (exp_instr->enabled_mask & (1 << i)) {
            unsigned idx = exp_instr->compressed ? i >> 1 : i;
            assert(idx < exp_instr->operands.size());
            insert_wait_entry(ctx, exp_instr->operands[idx], ev);

         }
      }
      insert_wait_entry(ctx, exec, s2, ev, false);
      break;
   }
   case Format::FLAT: {
      if (ctx.chip_class < GFX10 && !instr->definitions.empty())
         update_counters_for_flat_load(ctx, barrier_buffer);
      else
         update_counters(ctx, event_flat, barrier_buffer);

      if (!instr->definitions.empty())
         insert_wait_entry(ctx, instr->definitions[0], event_flat);
      break;
   }
   case Format::SMEM: {
      SMEM_instruction *smem = static_cast<SMEM_instruction*>(instr);
      update_counters(ctx, event_smem, static_cast<SMEM_instruction*>(instr)->barrier);

      if (!instr->definitions.empty())
         insert_wait_entry(ctx, instr->definitions[0], event_smem);
      else if (ctx.chip_class >= GFX10 &&
               !smem->can_reorder &&
               smem->barrier == barrier_buffer)
         ctx.pending_s_buffer_store = true;

      break;
   }
   case Format::DS: {
      bool gds = static_cast<DS_instruction*>(instr)->gds;
      update_counters(ctx, gds ? event_gds : event_lds, gds ? barrier_none : barrier_shared);
      if (gds)
         update_counters(ctx, event_gds_gpr_lock);

      if (!instr->definitions.empty())
         insert_wait_entry(ctx, instr->definitions[0], gds ? event_gds : event_lds);

      if (gds) {
         for (const Operand& op : instr->operands)
            insert_wait_entry(ctx, op, event_gds_gpr_lock);
         insert_wait_entry(ctx, exec, s2, event_gds_gpr_lock, false);
      }
      break;
   }
   case Format::MUBUF:
   case Format::MTBUF:
   case Format::MIMG:
   case Format::GLOBAL: {
      wait_event ev = !instr->definitions.empty() || ctx.chip_class < GFX10 ? event_vmem : event_vmem_store;
      update_counters(ctx, ev, get_barrier_interaction(instr));

      if (!instr->definitions.empty())
         insert_wait_entry(ctx, instr->definitions[0], ev);

      if (instr->operands.size() == 4 && ctx.chip_class == GFX6) {
         ctx.exp_cnt++;
         update_counters(ctx, event_vmem_gpr_lock);
         insert_wait_entry(ctx, instr->operands[3], event_vmem_gpr_lock);
      }
      break;
   }
   default:
      break;
   }
}

void emit_waitcnt(wait_ctx& ctx, std::vector<aco_ptr<Instruction>>& instructions, wait_imm imm)
{
   if (imm.vs != wait_imm::unset_counter) {
      assert(ctx.chip_class >= GFX10);
      SOPK_instruction* waitcnt_vs = create_instruction<SOPK_instruction>(aco_opcode::s_waitcnt_vscnt, Format::SOPK, 0, 1);
      waitcnt_vs->definitions[0] = Definition(sgpr_null, s1);
      waitcnt_vs->imm = imm.vs;
      instructions.emplace_back(waitcnt_vs);
      imm.vs = wait_imm::unset_counter;
   }
   if (!imm.empty()) {
      SOPP_instruction* waitcnt = create_instruction<SOPP_instruction>(aco_opcode::s_waitcnt, Format::SOPP, 0, 0);
      waitcnt->imm = imm.pack(ctx.chip_class);
      waitcnt->block = -1;
      instructions.emplace_back(waitcnt);
   }
}

void handle_block(Program *program, Block& block, wait_ctx& ctx)
{
   std::vector<aco_ptr<Instruction>> new_instructions;

   for (aco_ptr<Instruction>& instr : block.instructions) {
      wait_imm imm = kill(instr.get(), ctx);

      if (!imm.empty())
         emit_waitcnt(ctx, new_instructions, imm);

      gen(instr.get(), ctx);

      if (instr->format != Format::PSEUDO_BARRIER)
         new_instructions.emplace_back(std::move(instr));
   }

   /* check if this block is at the end of a loop */
   for (unsigned succ_idx : block.linear_succs) {
      /* eliminate any remaining counters */
      if (succ_idx <= block.index && (ctx.vm_cnt || ctx.exp_cnt || ctx.lgkm_cnt || ctx.vs_cnt)) {
         // TODO: we could do better if we only wait if the regs between the block and other predecessors differ

         aco_ptr<Instruction> branch = std::move(new_instructions.back());
         new_instructions.pop_back();

         wait_imm imm(ctx.vm_cnt ? 0 : wait_imm::unset_counter,
                      ctx.exp_cnt ? 0 : wait_imm::unset_counter,
                      ctx.lgkm_cnt ? 0 : wait_imm::unset_counter,
                      ctx.vs_cnt ? 0 : wait_imm::unset_counter);
         emit_waitcnt(ctx, new_instructions, imm);

         new_instructions.push_back(std::move(branch));

         ctx = wait_ctx(program);
         break;
      }
   }
   block.instructions.swap(new_instructions);
}

} /* end namespace */

void insert_wait_states(Program* program)
{
   wait_ctx out_ctx[program->blocks.size()]; /* per BB ctx */
   for (unsigned i = 0; i < program->blocks.size(); i++)
      out_ctx[i] = wait_ctx(program);

   for (unsigned i = 0; i < program->blocks.size(); i++) {
      Block& current = program->blocks[i];
      wait_ctx& in = out_ctx[current.index];

      for (unsigned b : current.linear_preds)
         in.join(&out_ctx[b], false);
      for (unsigned b : current.logical_preds)
         in.join(&out_ctx[b], true);

      if (current.instructions.empty())
         continue;

      handle_block(program, current, in);
   }
}

}

