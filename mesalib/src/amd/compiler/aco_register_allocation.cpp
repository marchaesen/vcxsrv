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
 * Authors:
 *    Daniel Schürmann (daniel.schuermann@campus.tu-berlin.de)
 *    Bas Nieuwenhuizen (bas@basnieuwenhuizen.nl)
 *
 */

#include <algorithm>
#include <array>
#include <map>
#include <unordered_map>
#include <functional>

#include "aco_ir.h"
#include "sid.h"
#include "util/u_math.h"

namespace aco {
namespace {

struct ra_ctx {
   std::bitset<512> war_hint;
   Program* program;
   std::unordered_map<unsigned, std::pair<PhysReg, RegClass>> assignments;
   std::map<unsigned, Temp> orig_names;
   unsigned max_used_sgpr = 0;
   unsigned max_used_vgpr = 0;
   std::bitset<64> defs_done; /* see MAX_ARGS in aco_instruction_selection_setup.cpp */

   ra_ctx(Program* program) : program(program) {}
};


/* helper function for debugging */
#if 0
void print_regs(ra_ctx& ctx, bool vgprs, std::array<uint32_t, 512>& reg_file)
{
   unsigned max = vgprs ? ctx.program->max_reg_demand.vgpr : ctx.program->max_reg_demand.sgpr;
   unsigned lb = vgprs ? 256 : 0;
   unsigned ub = lb + max;
   char reg_char = vgprs ? 'v' : 's';

   /* print markers */
   printf("       ");
   for (unsigned i = lb; i < ub; i += 3) {
      printf("%.2u ", i - lb);
   }
   printf("\n");

   /* print usage */
   printf("%cgprs: ", reg_char);
   unsigned free_regs = 0;
   unsigned prev = 0;
   bool char_select = false;
   for (unsigned i = lb; i < ub; i++) {
      if (reg_file[i] == 0xFFFF) {
         printf("~");
      } else if (reg_file[i]) {
         if (reg_file[i] != prev) {
            prev = reg_file[i];
            char_select = !char_select;
         }
         printf(char_select ? "#" : "@");
      } else {
         free_regs++;
         printf(".");
      }
   }
   printf("\n");

   printf("%u/%u used, %u/%u free\n", max - free_regs, max, free_regs, max);

   /* print assignments */
   prev = 0;
   unsigned size = 0;
   for (unsigned i = lb; i < ub; i++) {
      if (reg_file[i] != prev) {
         if (prev && size > 1)
            printf("-%d]\n", i - 1 - lb);
         else if (prev)
            printf("]\n");
         prev = reg_file[i];
         if (prev && prev != 0xFFFF) {
            if (ctx.orig_names.count(reg_file[i]) && ctx.orig_names[reg_file[i]].id() != reg_file[i])
               printf("%%%u (was %%%d) = %c[%d", reg_file[i], ctx.orig_names[reg_file[i]].id(), reg_char, i - lb);
            else
               printf("%%%u = %c[%d", reg_file[i], reg_char, i - lb);
         }
         size = 1;
      } else {
         size++;
      }
   }
   if (prev && size > 1)
      printf("-%d]\n", ub - lb - 1);
   else if (prev)
      printf("]\n");
}
#endif


void adjust_max_used_regs(ra_ctx& ctx, RegClass rc, unsigned reg)
{
   unsigned max_addressible_sgpr = ctx.program->sgpr_limit;
   unsigned size = rc.size();
   if (rc.type() == RegType::vgpr) {
      assert(reg >= 256);
      unsigned hi = reg - 256 + size - 1;
      ctx.max_used_vgpr = std::max(ctx.max_used_vgpr, hi);
   } else if (reg + rc.size() <= max_addressible_sgpr) {
      unsigned hi = reg + size - 1;
      ctx.max_used_sgpr = std::max(ctx.max_used_sgpr, std::min(hi, max_addressible_sgpr));
   }
}


void update_renames(ra_ctx& ctx, std::array<uint32_t, 512>& reg_file,
                    std::vector<std::pair<Operand, Definition>>& parallelcopies,
                    aco_ptr<Instruction>& instr)
{
   /* allocate id's and rename operands: this is done transparently here */
   for (std::pair<Operand, Definition>& copy : parallelcopies) {
      /* the definitions with id are not from this function and already handled */
      if (copy.second.isTemp())
         continue;

      // FIXME: if a definition got moved, change the target location and remove the parallelcopy
      copy.second.setTemp(Temp(ctx.program->allocateId(), copy.second.regClass()));
      ctx.assignments[copy.second.tempId()] = {copy.second.physReg(), copy.second.regClass()};
      for (unsigned i = copy.second.physReg().reg; i < copy.second.physReg() + copy.second.size(); i++)
         reg_file[i] = copy.second.tempId();
      /* check if we moved an operand */
      for (Operand& op : instr->operands) {
         if (!op.isTemp())
            continue;
         if (op.tempId() == copy.first.tempId()) {
            bool omit_renaming = instr->opcode == aco_opcode::p_create_vector && !op.isKill();
            for (std::pair<Operand, Definition>& pc : parallelcopies) {
               PhysReg def_reg = pc.second.physReg();
               omit_renaming &= def_reg > copy.first.physReg() ?
                                (copy.first.physReg() + copy.first.size() <= def_reg.reg) :
                                (def_reg + pc.second.size() <= copy.first.physReg().reg);
            }
            if (omit_renaming)
               continue;
            op.setTemp(copy.second.getTemp());
            op.setFixed(copy.second.physReg());
         }
      }
   }
}

std::pair<PhysReg, bool> get_reg_simple(ra_ctx& ctx,
                                        std::array<uint32_t, 512>& reg_file,
                                        uint32_t lb, uint32_t ub,
                                        uint32_t size, uint32_t stride,
                                        RegClass rc)
{
   /* best fit algorithm: find the smallest gap to fit in the variable */
   if (stride == 1) {
      unsigned best_pos = 0xFFFF;
      unsigned gap_size = 0xFFFF;
      unsigned next_pos = 0xFFFF;

      for (unsigned current_reg = lb; current_reg < ub; current_reg++) {
         if (reg_file[current_reg] != 0 || ctx.war_hint[current_reg]) {
            if (next_pos == 0xFFFF)
               continue;

            /* check if the variable fits */
            if (next_pos + size > current_reg) {
               next_pos = 0xFFFF;
               continue;
            }

            /* check if the tested gap is smaller */
            if (current_reg - next_pos < gap_size) {
               best_pos = next_pos;
               gap_size = current_reg - next_pos;
            }
            next_pos = 0xFFFF;
            continue;
         }

         if (next_pos == 0xFFFF)
            next_pos = current_reg;
      }

      /* final check */
      if (next_pos != 0xFFFF &&
          next_pos + size <= ub &&
          ub - next_pos < gap_size) {
         best_pos = next_pos;
         gap_size = ub - next_pos;
      }
      if (best_pos != 0xFFFF) {
         adjust_max_used_regs(ctx, rc, best_pos);
         return {PhysReg{best_pos}, true};
      }
      return {{}, false};
   }

   bool found = false;
   unsigned reg_lo = lb;
   unsigned reg_hi = lb + size - 1;
   while (!found && reg_lo + size <= ub) {
      if (reg_file[reg_lo] != 0) {
         reg_lo += stride;
         continue;
      }
      reg_hi = reg_lo + size - 1;
      found = true;
      for (unsigned reg = reg_lo + 1; found && reg <= reg_hi; reg++) {
         if (reg_file[reg] != 0 || ctx.war_hint[reg])
            found = false;
      }
      if (found) {
         adjust_max_used_regs(ctx, rc, reg_lo);
         return {PhysReg{reg_lo}, true};
      }

      reg_lo += stride;
   }

   return {{}, false};
}

bool get_regs_for_copies(ra_ctx& ctx,
                         std::array<uint32_t, 512>& reg_file,
                         std::vector<std::pair<Operand, Definition>>& parallelcopies,
                         std::set<std::pair<unsigned, unsigned>> vars,
                         uint32_t lb, uint32_t ub,
                         aco_ptr<Instruction>& instr,
                         uint32_t def_reg_lo,
                         uint32_t def_reg_hi)
{

   /* variables are sorted from small sized to large */
   /* NOTE: variables are also sorted by ID. this only affects a very small number of shaders slightly though. */
   for (std::set<std::pair<unsigned, unsigned>>::reverse_iterator it = vars.rbegin(); it != vars.rend(); ++it) {
      unsigned id = it->second;
      std::pair<PhysReg, RegClass> var = ctx.assignments[id];
      uint32_t size = it->first;
      uint32_t stride = 1;
      if (var.second.type() == RegType::sgpr) {
         if (size == 2)
            stride = 2;
         if (size > 3)
            stride = 4;
      }

      /* check if this is a dead operand, then we can re-use the space from the definition */
      bool is_dead_operand = false;
      for (unsigned i = 0; !is_phi(instr) && !is_dead_operand && i < instr->operands.size(); i++) {
         if (instr->operands[i].isTemp() && instr->operands[i].isKill() && instr->operands[i].tempId() == id)
            is_dead_operand = true;
      }

      std::pair<PhysReg, bool> res;
      if (is_dead_operand) {
         if (instr->opcode == aco_opcode::p_create_vector) {
            for (unsigned i = 0, offset = 0; i < instr->operands.size(); offset += instr->operands[i].size(), i++) {
               if (instr->operands[i].isTemp() && instr->operands[i].tempId() == id) {
                  for (unsigned j = 0; j < size; j++)
                     assert(reg_file[def_reg_lo + offset + j] == 0);
                  res = {PhysReg{def_reg_lo + offset}, true};
                  break;
               }
            }
         } else {
            res = get_reg_simple(ctx, reg_file, def_reg_lo, def_reg_hi + 1, size, stride, var.second);
         }
      } else {
         res = get_reg_simple(ctx, reg_file, lb, def_reg_lo, size, stride, var.second);
         if (!res.second) {
            unsigned lb = (def_reg_hi + stride) & ~(stride - 1);
            res = get_reg_simple(ctx, reg_file, lb, ub, size, stride, var.second);
         }
      }

      if (res.second) {
         /* mark the area as blocked */
         for (unsigned i = res.first.reg; i < res.first + size; i++)
            reg_file[i] = 0xFFFFFFFF;
         /* create parallelcopy pair (without definition id) */
         Temp tmp = Temp(id, var.second);
         Operand pc_op = Operand(tmp);
         pc_op.setFixed(var.first);
         Definition pc_def = Definition(res.first, pc_op.regClass());
         parallelcopies.emplace_back(pc_op, pc_def);
         continue;
      }

      unsigned best_pos = lb;
      unsigned num_moves = 0xFF;
      unsigned num_vars = 0;

      /* we use a sliding window to find potential positions */
      unsigned reg_lo = lb;
      unsigned reg_hi = lb + size - 1;
      for (reg_lo = lb, reg_hi = lb + size - 1; reg_hi < ub; reg_lo += stride, reg_hi += stride) {
         if (!is_dead_operand && ((reg_lo >= def_reg_lo && reg_lo <= def_reg_hi) ||
                                  (reg_hi >= def_reg_lo && reg_hi <= def_reg_hi)))
            continue;

         /* second, check that we have at most k=num_moves elements in the window
          * and no element is larger than the currently processed one */
         unsigned k = 0;
         unsigned n = 0;
         unsigned last_var = 0;
         bool found = true;
         for (unsigned j = reg_lo; found && j <= reg_hi; j++) {
            if (reg_file[j] == 0 || reg_file[j] == last_var)
               continue;

            /* 0xFFFF signals that this area is already blocked! */
            if (reg_file[j] == 0xFFFFFFFF || k > num_moves) {
               found = false;
               break;
            }
            /* we cannot split live ranges of linear vgprs */
            if (ctx.assignments[reg_file[j]].second & (1 << 6)) {
               found = false;
               break;
            }
            bool is_kill = false;
            for (const Operand& op : instr->operands) {
               if (op.isTemp() && op.isKill() && op.tempId() == reg_file[j]) {
                  is_kill = true;
                  break;
               }
            }
            if (!is_kill && ctx.assignments[reg_file[j]].second.size() >= size) {
               found = false;
               break;
            }

            k += ctx.assignments[reg_file[j]].second.size();
            last_var = reg_file[j];
            n++;
            if (k > num_moves || (k == num_moves && n <= num_vars)) {
               found = false;
               break;
            }
         }

         if (found) {
            best_pos = reg_lo;
            num_moves = k;
            num_vars = n;
         }
      }

      /* FIXME: we messed up and couldn't find space for the variables to be copied */
      if (num_moves == 0xFF)
         return false;

      reg_lo = best_pos;
      reg_hi = best_pos + size - 1;

      /* collect variables and block reg file */
      std::set<std::pair<unsigned, unsigned>> new_vars;
      for (unsigned j = reg_lo; j <= reg_hi; j++) {
         if (reg_file[j] != 0) {
            unsigned size = ctx.assignments[reg_file[j]].second.size();
            unsigned id = reg_file[j];
            new_vars.emplace(size, id);
            for (unsigned k = 0; k < size; k++)
               reg_file[ctx.assignments[id].first + k] = 0;
         }
      }

      /* mark the area as blocked */
      for (unsigned i = reg_lo; i <= reg_hi; i++)
         reg_file[i] = 0xFFFFFFFF;

      if (!get_regs_for_copies(ctx, reg_file, parallelcopies, new_vars, lb, ub, instr, def_reg_lo, def_reg_hi))
         return false;

      adjust_max_used_regs(ctx, var.second, reg_lo);

      /* create parallelcopy pair (without definition id) */
      Temp tmp = Temp(id, var.second);
      Operand pc_op = Operand(tmp);
      pc_op.setFixed(var.first);
      Definition pc_def = Definition(PhysReg{reg_lo}, pc_op.regClass());
      parallelcopies.emplace_back(pc_op, pc_def);
   }

   return true;
}


std::pair<PhysReg, bool> get_reg_impl(ra_ctx& ctx,
                                      std::array<uint32_t, 512>& reg_file,
                                      std::vector<std::pair<Operand, Definition>>& parallelcopies,
                                      uint32_t lb, uint32_t ub,
                                      uint32_t size, uint32_t stride,
                                      RegClass rc,
                                      aco_ptr<Instruction>& instr)
{
   unsigned regs_free = 0;
   /* check how many free regs we have */
   for (unsigned j = lb; j < ub; j++) {
      if (reg_file[j] == 0)
         regs_free++;
   }

   /* mark and count killed operands */
   unsigned killed_ops = 0;
   for (unsigned j = 0; !is_phi(instr) && j < instr->operands.size(); j++) {
      if (instr->operands[j].isTemp() &&
          instr->operands[j].isFirstKill() &&
          instr->operands[j].physReg() >= lb &&
          instr->operands[j].physReg() < ub) {
         assert(instr->operands[j].isFixed());
         assert(reg_file[instr->operands[j].physReg().reg] == 0);
         for (unsigned k = 0; k < instr->operands[j].size(); k++)
            reg_file[instr->operands[j].physReg() + k] = 0xFFFFFFFF;
         killed_ops += instr->operands[j].getTemp().size();
      }
   }

   assert(regs_free >= size);
   /* we might have to move dead operands to dst in order to make space */
   unsigned op_moves = 0;

   if (size > (regs_free - killed_ops))
      op_moves = size - (regs_free - killed_ops);

   /* find the best position to place the definition */
   unsigned best_pos = lb;
   unsigned num_moves = 0xFF;
   unsigned num_vars = 0;

   /* we use a sliding window to check potential positions */
   unsigned reg_lo = lb;
   unsigned reg_hi = lb + size - 1;
   for (reg_lo = lb, reg_hi = lb + size - 1; reg_hi < ub; reg_lo += stride, reg_hi += stride) {
      /* first check the edges: this is what we have to fix to allow for num_moves > size */
      if (reg_lo > lb && reg_file[reg_lo] != 0 && reg_file[reg_lo] == reg_file[reg_lo - 1])
         continue;
      if (reg_hi < ub - 1 && reg_file[reg_hi] != 0 && reg_file[reg_hi] == reg_file[reg_hi + 1])
         continue;

      /* second, check that we have at most k=num_moves elements in the window
       * and no element is larger than the currently processed one */
      unsigned k = op_moves;
      unsigned n = 0;
      unsigned remaining_op_moves = op_moves;
      unsigned last_var = 0;
      bool found = true;
      bool aligned = rc == RegClass::v4 && reg_lo % 4 == 0;
      for (unsigned j = reg_lo; found && j <= reg_hi; j++) {
         if (reg_file[j] == 0 || reg_file[j] == last_var)
            continue;

         /* dead operands effectively reduce the number of estimated moves */
         if (remaining_op_moves && reg_file[j] == 0xFFFFFFFF) {
            k--;
            remaining_op_moves--;
            continue;
         }

         if (ctx.assignments[reg_file[j]].second.size() >= size) {
            found = false;
            break;
         }


         /* we cannot split live ranges of linear vgprs */
         if (ctx.assignments[reg_file[j]].second & (1 << 6)) {
            found = false;
            break;
         }

         k += ctx.assignments[reg_file[j]].second.size();
         n++;
         last_var = reg_file[j];
      }

      if (!found || k > num_moves)
         continue;
      if (k == num_moves && n < num_vars)
         continue;
      if (!aligned && k == num_moves && n == num_vars)
         continue;

      if (found) {
         best_pos = reg_lo;
         num_moves = k;
         num_vars = n;
      }
   }

   if (num_moves == 0xFF) {
      /* remove killed operands from reg_file once again */
      for (unsigned i = 0; !is_phi(instr) && i < instr->operands.size(); i++) {
         if (instr->operands[i].isTemp() && instr->operands[i].isFirstKill()) {
            for (unsigned k = 0; k < instr->operands[i].getTemp().size(); k++)
               reg_file[instr->operands[i].physReg() + k] = 0;
         }
      }
      for (unsigned i = 0; i < instr->definitions.size(); i++) {
         Definition def = instr->definitions[i];
         if (def.isTemp() && def.isFixed() && ctx.defs_done.test(i)) {
            for (unsigned k = 0; k < def.getTemp().size(); k++)
               reg_file[def.physReg() + k] = def.tempId();
         }
      }
      return {{}, false};
   }

   std::array<uint32_t, 512> register_file = reg_file;

   /* now, we figured the placement for our definition */
   std::set<std::pair<unsigned, unsigned>> vars;
   for (unsigned j = best_pos; j < best_pos + size; j++) {
      if (reg_file[j] != 0xFFFFFFFF && reg_file[j] != 0)
         vars.emplace(ctx.assignments[reg_file[j]].second.size(), reg_file[j]);
      reg_file[j] = 0;
   }

   if (instr->opcode == aco_opcode::p_create_vector) {
      /* move killed operands which aren't yet at the correct position */
      for (unsigned i = 0, offset = 0; i < instr->operands.size(); offset += instr->operands[i].size(), i++) {
         if (instr->operands[i].isTemp() && instr->operands[i].isFirstKill() &&
             instr->operands[i].getTemp().type() == rc.type()) {

            if (instr->operands[i].physReg() != best_pos + offset) {
               vars.emplace(instr->operands[i].size(), instr->operands[i].tempId());
               for (unsigned j = 0; j < instr->operands[i].size(); j++)
                  reg_file[instr->operands[i].physReg() + j] = 0;
            } else {
               for (unsigned j = 0; j < instr->operands[i].size(); j++)
                  reg_file[instr->operands[i].physReg() + j] = instr->operands[i].tempId();
            }
         }
      }
   } else {
      /* re-enable the killed operands */
      for (unsigned j = 0; !is_phi(instr) && j < instr->operands.size(); j++) {
         if (instr->operands[j].isTemp() && instr->operands[j].isFirstKill()) {
            for (unsigned k = 0; k < instr->operands[j].getTemp().size(); k++)
               reg_file[instr->operands[j].physReg() + k] = instr->operands[j].tempId();
         }
      }
   }

   std::vector<std::pair<Operand, Definition>> pc;
   if (!get_regs_for_copies(ctx, reg_file, pc, vars, lb, ub, instr, best_pos, best_pos + size - 1)) {
      reg_file = std::move(register_file);
      /* remove killed operands from reg_file once again */
      if (!is_phi(instr)) {
         for (const Operand& op : instr->operands) {
            if (op.isTemp() && op.isFirstKill()) {
               for (unsigned k = 0; k < op.getTemp().size(); k++)
                  reg_file[op.physReg() + k] = 0;
            }
         }
      }
      for (unsigned i = 0; i < instr->definitions.size(); i++) {
         Definition& def = instr->definitions[i];
         if (def.isTemp() && def.isFixed() && ctx.defs_done.test(i)) {
            for (unsigned k = 0; k < def.getTemp().size(); k++)
               reg_file[def.physReg() + k] = def.tempId();
         }
      }
      return {{}, false};
   }

   parallelcopies.insert(parallelcopies.end(), pc.begin(), pc.end());

   /* we set the definition regs == 0. the actual caller is responsible for correct setting */
   for (unsigned i = 0; i < size; i++)
      reg_file[best_pos + i] = 0;

   update_renames(ctx, reg_file, parallelcopies, instr);

   /* remove killed operands from reg_file once again */
   for (unsigned i = 0; !is_phi(instr) && i < instr->operands.size(); i++) {
      if (!instr->operands[i].isTemp() || !instr->operands[i].isFixed())
         continue;
      assert(!instr->operands[i].isUndefined());
      if (instr->operands[i].isFirstKill()) {
         for (unsigned j = 0; j < instr->operands[i].getTemp().size(); j++)
            reg_file[instr->operands[i].physReg() + j] = 0;
      }
   }
   for (unsigned i = 0; i < instr->definitions.size(); i++) {
      Definition def = instr->definitions[i];
      if (def.isTemp() && def.isFixed() && ctx.defs_done.test(i)) {
         for (unsigned k = 0; k < def.getTemp().size(); k++)
            reg_file[def.physReg() + k] = def.tempId();
      }
   }

   adjust_max_used_regs(ctx, rc, best_pos);
   return {PhysReg{best_pos}, true};
}

PhysReg get_reg(ra_ctx& ctx,
                std::array<uint32_t, 512>& reg_file,
                RegClass rc,
                std::vector<std::pair<Operand, Definition>>& parallelcopies,
                aco_ptr<Instruction>& instr)
{
   uint32_t size = rc.size();
   uint32_t stride = 1;
   uint32_t lb, ub;
   if (rc.type() == RegType::vgpr) {
      lb = 256;
      ub = 256 + ctx.program->max_reg_demand.vgpr;
   } else {
      lb = 0;
      ub = ctx.program->max_reg_demand.sgpr;
      if (size == 2)
         stride = 2;
      else if (size >= 4)
         stride = 4;
   }

   std::pair<PhysReg, bool> res = {{}, false};
   /* try to find space without live-range splits */
   if (rc.type() == RegType::vgpr && (size == 4 || size == 8))
      res = get_reg_simple(ctx, reg_file, lb, ub, size, 4, rc);
   if (!res.second)
      res = get_reg_simple(ctx, reg_file, lb, ub, size, stride, rc);
   if (res.second)
      return res.first;

   /* try to find space with live-range splits */
   res = get_reg_impl(ctx, reg_file, parallelcopies, lb, ub, size, stride, rc, instr);

   if (res.second)
      return res.first;

   unsigned regs_free = 0;
   for (unsigned i = lb; i < ub; i++) {
      if (!reg_file[i])
         regs_free++;
   }

   /* We should only fail here because keeping under the limit would require
    * too many moves. */
   assert(regs_free >= size);

   /* try using more registers */
   uint16_t max_addressible_sgpr = ctx.program->sgpr_limit;
   uint16_t max_addressible_vgpr = ctx.program->vgpr_limit;
   if (rc.type() == RegType::vgpr && ctx.program->max_reg_demand.vgpr < max_addressible_vgpr) {
      update_vgpr_sgpr_demand(ctx.program, RegisterDemand(ctx.program->max_reg_demand.vgpr + 1, ctx.program->max_reg_demand.sgpr));
      return get_reg(ctx, reg_file, rc, parallelcopies, instr);
   } else if (rc.type() == RegType::sgpr && ctx.program->max_reg_demand.sgpr < max_addressible_sgpr) {
      update_vgpr_sgpr_demand(ctx.program,  RegisterDemand(ctx.program->max_reg_demand.vgpr, ctx.program->max_reg_demand.sgpr + 1));
      return get_reg(ctx, reg_file, rc, parallelcopies, instr);
   }

   //FIXME: if nothing helps, shift-rotate the registers to make space

   unreachable("did not find a register");
}


std::pair<PhysReg, bool> get_reg_vec(ra_ctx& ctx,
                                     std::array<uint32_t, 512>& reg_file,
                                     RegClass rc)
{
   uint32_t size = rc.size();
   uint32_t stride = 1;
   uint32_t lb, ub;
   if (rc.type() == RegType::vgpr) {
      lb = 256;
      ub = 256 + ctx.program->max_reg_demand.vgpr;
   } else {
      lb = 0;
      ub = ctx.program->max_reg_demand.sgpr;
      if (size == 2)
         stride = 2;
      else if (size >= 4)
         stride = 4;
   }
   return get_reg_simple(ctx, reg_file, lb, ub, size, stride, rc);
}


PhysReg get_reg_create_vector(ra_ctx& ctx,
                              std::array<uint32_t, 512>& reg_file,
                              RegClass rc,
                              std::vector<std::pair<Operand, Definition>>& parallelcopies,
                              aco_ptr<Instruction>& instr)
{
   /* create_vector instructions have different costs w.r.t. register coalescing */
   uint32_t size = rc.size();
   uint32_t stride = 1;
   uint32_t lb, ub;
   if (rc.type() == RegType::vgpr) {
      lb = 256;
      ub = 256 + ctx.program->max_reg_demand.vgpr;
   } else {
      lb = 0;
      ub = ctx.program->max_reg_demand.sgpr;
      if (size == 2)
         stride = 2;
      else if (size >= 4)
         stride = 4;
   }

   unsigned best_pos = -1;
   unsigned num_moves = 0xFF;
   bool best_war_hint = true;

   /* test for each operand which definition placement causes the least shuffle instructions */
   for (unsigned i = 0, offset = 0; i < instr->operands.size(); offset += instr->operands[i].size(), i++) {
      // TODO: think about, if we can alias live operands on the same register
      if (!instr->operands[i].isTemp() || !instr->operands[i].isKill() || instr->operands[i].getTemp().type() != rc.type())
         continue;

      if (offset > instr->operands[i].physReg())
         continue;

      unsigned reg_lo = instr->operands[i].physReg() - offset;
      unsigned reg_hi = reg_lo + size - 1;
      unsigned k = 0;

      /* no need to check multiple times */
      if (reg_lo == best_pos)
         continue;

      /* check borders */
      // TODO: this can be improved */
      if (reg_lo < lb || reg_hi >= ub || reg_lo % stride != 0)
         continue;
      if (reg_lo > lb && reg_file[reg_lo] != 0 && reg_file[reg_lo] == reg_file[reg_lo - 1])
         continue;
      if (reg_hi < ub - 1 && reg_file[reg_hi] != 0 && reg_file[reg_hi] == reg_file[reg_hi + 1])
         continue;

      /* count variables to be moved and check war_hint */
      bool war_hint = false;
      bool linear_vgpr = false;
      for (unsigned j = reg_lo; j <= reg_hi && !linear_vgpr; j++) {
         if (reg_file[j] != 0) {
            k++;
            /* we cannot split live ranges of linear vgprs */
            if (ctx.assignments[reg_file[j]].second & (1 << 6))
               linear_vgpr = true;
         }
         war_hint |= ctx.war_hint[j];
      }
      if (linear_vgpr || (war_hint && !best_war_hint))
         continue;

      /* count operands in wrong positions */
      for (unsigned j = 0, offset = 0; j < instr->operands.size(); offset += instr->operands[j].size(), j++) {
         if (j == i ||
             !instr->operands[j].isTemp() ||
             instr->operands[j].getTemp().type() != rc.type())
            continue;
         if (instr->operands[j].physReg() != reg_lo + offset)
            k += instr->operands[j].size();
      }
      bool aligned = rc == RegClass::v4 && reg_lo % 4 == 0;
      if (k > num_moves || (!aligned && k == num_moves))
         continue;

      best_pos = reg_lo;
      num_moves = k;
      best_war_hint = war_hint;
   }

   if (num_moves >= size)
      return get_reg(ctx, reg_file, rc, parallelcopies, instr);

   /* collect variables to be moved */
   std::set<std::pair<unsigned, unsigned>> vars;
   for (unsigned i = best_pos; i < best_pos + size; i++) {
      if (reg_file[i] != 0)
         vars.emplace(ctx.assignments[reg_file[i]].second.size(), reg_file[i]);
      reg_file[i] = 0;
   }

   /* move killed operands which aren't yet at the correct position */
   for (unsigned i = 0, offset = 0; i < instr->operands.size(); offset += instr->operands[i].size(), i++) {
      if (instr->operands[i].isTemp() && instr->operands[i].isFirstKill() && instr->operands[i].getTemp().type() == rc.type()) {
         if (instr->operands[i].physReg() != best_pos + offset) {
            vars.emplace(instr->operands[i].size(), instr->operands[i].tempId());
         } else {
            for (unsigned j = 0; j < instr->operands[i].size(); j++)
               reg_file[instr->operands[i].physReg() + j] = instr->operands[i].tempId();
         }
      }
   }

   ASSERTED bool success = false;
   success = get_regs_for_copies(ctx, reg_file, parallelcopies, vars, lb, ub, instr, best_pos, best_pos + size - 1);
   assert(success);

   update_renames(ctx, reg_file, parallelcopies, instr);
   adjust_max_used_regs(ctx, rc, best_pos);
   return PhysReg{best_pos};
}

bool get_reg_specified(ra_ctx& ctx,
                       std::array<uint32_t, 512>& reg_file,
                       RegClass rc,
                       std::vector<std::pair<Operand, Definition>>& parallelcopies,
                       aco_ptr<Instruction>& instr,
                       PhysReg reg)
{
   uint32_t size = rc.size();
   uint32_t stride = 1;
   uint32_t lb, ub;

   if (rc.type() == RegType::vgpr) {
      lb = 256;
      ub = 256 + ctx.program->max_reg_demand.vgpr;
   } else {
      if (size == 2)
         stride = 2;
      else if (size >= 4)
         stride = 4;
      if (reg % stride != 0)
         return false;
      lb = 0;
      ub = ctx.program->max_reg_demand.sgpr;
   }

   uint32_t reg_lo = reg.reg;
   uint32_t reg_hi = reg + (size - 1);

   if (reg_lo < lb || reg_hi >= ub || reg_lo > reg_hi)
      return false;

   for (unsigned i = reg_lo; i <= reg_hi; i++) {
      if (reg_file[i] != 0)
         return false;
   }
   adjust_max_used_regs(ctx, rc, reg_lo);
   return true;
}

void handle_pseudo(ra_ctx& ctx,
                   const std::array<uint32_t, 512>& reg_file,
                   Instruction* instr)
{
   if (instr->format != Format::PSEUDO)
      return;

   /* all instructions which use handle_operands() need this information */
   switch (instr->opcode) {
   case aco_opcode::p_extract_vector:
   case aco_opcode::p_create_vector:
   case aco_opcode::p_split_vector:
   case aco_opcode::p_parallelcopy:
   case aco_opcode::p_wqm:
      break;
   default:
      return;
   }

   /* if all definitions are vgpr, no need to care for SCC */
   bool writes_sgpr = false;
   for (Definition& def : instr->definitions) {
      if (def.getTemp().type() == RegType::sgpr) {
         writes_sgpr = true;
         break;
      }
   }
   /* if all operands are constant, no need to care either */
   bool reads_sgpr = false;
   for (Operand& op : instr->operands) {
      if (op.isTemp() && op.getTemp().type() == RegType::sgpr) {
         reads_sgpr = true;
         break;
      }
   }
   if (!(writes_sgpr && reads_sgpr))
      return;

   Pseudo_instruction *pi = (Pseudo_instruction *)instr;
   if (reg_file[scc.reg]) {
      pi->tmp_in_scc = true;

      int reg = ctx.max_used_sgpr;
      for (; reg >= 0 && reg_file[reg]; reg--)
         ;
      if (reg < 0) {
         reg = ctx.max_used_sgpr + 1;
         for (; reg < ctx.program->max_reg_demand.sgpr && reg_file[reg]; reg++)
            ;
         assert(reg < ctx.program->max_reg_demand.sgpr);
      }

      adjust_max_used_regs(ctx, s1, reg);
      pi->scratch_sgpr = PhysReg{(unsigned)reg};
   } else {
      pi->tmp_in_scc = false;
   }
}

bool operand_can_use_reg(aco_ptr<Instruction>& instr, unsigned idx, PhysReg reg)
{
   switch (instr->format) {
   case Format::SMEM:
      return reg != scc &&
             reg != exec &&
             (reg != m0 || idx == 1 || idx == 3) && /* offset can be m0 */
             (reg != vcc || (instr->definitions.empty() && idx == 2)); /* sdata can be vcc */
   default:
      // TODO: there are more instructions with restrictions on registers
      return true;
   }
}

} /* end namespace */


void register_allocation(Program *program, std::vector<std::set<Temp>> live_out_per_block)
{
   ra_ctx ctx(program);

   std::vector<std::unordered_map<unsigned, Temp>> renames(program->blocks.size());

   struct phi_info {
      Instruction* phi;
      unsigned block_idx;
      std::set<Instruction*> uses;
   };

   bool filled[program->blocks.size()];
   bool sealed[program->blocks.size()];
   memset(filled, 0, sizeof filled);
   memset(sealed, 0, sizeof sealed);
   std::vector<std::vector<Instruction*>> incomplete_phis(program->blocks.size());
   std::map<unsigned, phi_info> phi_map;
   std::map<unsigned, unsigned> affinities;
   std::function<Temp(Temp,unsigned)> read_variable;
   std::function<Temp(Temp,Block*)> handle_live_in;
   std::function<Temp(std::map<unsigned, phi_info>::iterator)> try_remove_trivial_phi;

   read_variable = [&](Temp val, unsigned block_idx) -> Temp {
      std::unordered_map<unsigned, Temp>::iterator it = renames[block_idx].find(val.id());
      assert(it != renames[block_idx].end());
      return it->second;
   };

   handle_live_in = [&](Temp val, Block *block) -> Temp {
      std::vector<unsigned>& preds = val.is_linear() ? block->linear_preds : block->logical_preds;
      if (preds.size() == 0 || val.regClass() == val.regClass().as_linear()) {
         renames[block->index][val.id()] = val;
         return val;
      }
      assert(preds.size() > 0);

      Temp new_val;
      if (!sealed[block->index]) {
         /* consider rename from already processed predecessor */
         Temp tmp = read_variable(val, preds[0]);

         /* if the block is not sealed yet, we create an incomplete phi (which might later get removed again) */
         new_val = Temp{program->allocateId(), val.regClass()};
         aco_opcode opcode = val.is_linear() ? aco_opcode::p_linear_phi : aco_opcode::p_phi;
         aco_ptr<Instruction> phi{create_instruction<Pseudo_instruction>(opcode, Format::PSEUDO, preds.size(), 1)};
         phi->definitions[0] = Definition(new_val);
         for (unsigned i = 0; i < preds.size(); i++)
            phi->operands[i] = Operand(val);
         if (tmp.regClass() == new_val.regClass())
            affinities[new_val.id()] = tmp.id();

         phi_map.emplace(new_val.id(), phi_info{phi.get(), block->index});
         incomplete_phis[block->index].emplace_back(phi.get());
         block->instructions.insert(block->instructions.begin(), std::move(phi));

      } else if (preds.size() == 1) {
         /* if the block has only one predecessor, just look there for the name */
         new_val = read_variable(val, preds[0]);
      } else {
         /* there are multiple predecessors and the block is sealed */
         Temp ops[preds.size()];

         /* we start assuming that the name is the same from all predecessors */
         renames[block->index][val.id()] = val;
         bool needs_phi = false;

         /* get the rename from each predecessor and check if they are the same */
         for (unsigned i = 0; i < preds.size(); i++) {
            ops[i] = read_variable(val, preds[i]);
            if (i == 0)
               new_val = ops[i];
            else
               needs_phi |= !(new_val == ops[i]);
         }

         if (needs_phi) {
            /* the variable has been renamed differently in the predecessors: we need to insert a phi */
            aco_opcode opcode = val.is_linear() ? aco_opcode::p_linear_phi : aco_opcode::p_phi;
            aco_ptr<Instruction> phi{create_instruction<Pseudo_instruction>(opcode, Format::PSEUDO, preds.size(), 1)};
            new_val = Temp{program->allocateId(), val.regClass()};
            phi->definitions[0] = Definition(new_val);
            for (unsigned i = 0; i < preds.size(); i++) {
               phi->operands[i] = Operand(ops[i]);
               phi->operands[i].setFixed(ctx.assignments[ops[i].id()].first);
               if (ops[i].regClass() == new_val.regClass())
                  affinities[new_val.id()] = ops[i].id();
            }
            phi_map.emplace(new_val.id(), phi_info{phi.get(), block->index});
            block->instructions.insert(block->instructions.begin(), std::move(phi));
         }
      }

      renames[block->index][val.id()] = new_val;
      renames[block->index][new_val.id()] = new_val;
      ctx.orig_names[new_val.id()] = val;
      return new_val;
   };

   try_remove_trivial_phi = [&] (std::map<unsigned, phi_info>::iterator info) -> Temp {
      assert(info->second.block_idx != 0);
      Instruction* phi = info->second.phi;
      Temp same = Temp();

      Definition def = phi->definitions[0];
      /* a phi node is trivial if all operands are the same as the definition of the phi */
      for (const Operand& op : phi->operands) {
         const Temp t = op.getTemp();
         if (t == same || t == def.getTemp())
            continue;
         if (!(same == Temp()) || !(op.physReg() == def.physReg())) {
            /* phi is not trivial */
            return def.getTemp();
         }
         same = t;
      }
      assert(!(same == Temp() || same == def.getTemp()));

      /* reroute all uses to same and remove phi */
      std::vector<std::map<unsigned, phi_info>::iterator> phi_users;
      std::map<unsigned, phi_info>::iterator same_phi_info = phi_map.find(same.id());
      for (Instruction* instr : info->second.uses) {
         assert(phi != instr);
         /* recursively try to remove trivial phis */
         if (is_phi(instr)) {
            /* ignore if the phi was already flagged trivial */
            if (instr->definitions.empty())
               continue;

            std::map<unsigned, phi_info>::iterator it = phi_map.find(instr->definitions[0].tempId());
            if (it != phi_map.end() && it != info)
               phi_users.emplace_back(it);
         }
         for (Operand& op : instr->operands) {
            if (op.isTemp() && op.tempId() == def.tempId()) {
               op.setTemp(same);
               if (same_phi_info != phi_map.end())
                  same_phi_info->second.uses.emplace(instr);
            }
         }
      }

      auto it = ctx.orig_names.find(same.id());
      unsigned orig_var = it != ctx.orig_names.end() ? it->second.id() : same.id();
      for (unsigned i = 0; i < program->blocks.size(); i++) {
         auto it = renames[i].find(orig_var);
         if (it != renames[i].end() && it->second == def.getTemp())
            renames[i][orig_var] = same;
      }

      unsigned block_idx = info->second.block_idx;
      phi->definitions.clear(); /* this indicates that the phi can be removed */
      phi_map.erase(info);
      for (auto it : phi_users) {
         if (sealed[it->second.block_idx])
            try_remove_trivial_phi(it);
      }

      /* due to the removal of other phis, the name might have changed once again! */
      return renames[block_idx][orig_var];
   };

   std::map<unsigned, Instruction*> vectors;
   std::vector<std::vector<Temp>> phi_ressources;
   std::map<unsigned, unsigned> temp_to_phi_ressources;

   for (std::vector<Block>::reverse_iterator it = program->blocks.rbegin(); it != program->blocks.rend(); it++) {
      Block& block = *it;

      /* first, compute the death points of all live vars within the block */
      std::set<Temp>& live = live_out_per_block[block.index];

      std::vector<aco_ptr<Instruction>>::reverse_iterator rit;
      for (rit = block.instructions.rbegin(); rit != block.instructions.rend(); ++rit) {
         aco_ptr<Instruction>& instr = *rit;
         if (!is_phi(instr)) {
            for (const Operand& op : instr->operands) {
               if (op.isTemp())
                  live.emplace(op.getTemp());
            }
            if (instr->opcode == aco_opcode::p_create_vector) {
               for (const Operand& op : instr->operands) {
                  if (op.isTemp() && op.getTemp().type() == instr->definitions[0].getTemp().type())
                     vectors[op.tempId()] = instr.get();
               }
            }
         } else if (!instr->definitions[0].isKill() && !instr->definitions[0].isFixed()) {
            /* collect information about affinity-related temporaries */
            std::vector<Temp> affinity_related;
            /* affinity_related[0] is the last seen affinity-related temp */
            affinity_related.emplace_back(instr->definitions[0].getTemp());
            affinity_related.emplace_back(instr->definitions[0].getTemp());
            for (const Operand& op : instr->operands) {
               if (op.isTemp() && op.regClass() == instr->definitions[0].regClass()) {
                  affinity_related.emplace_back(op.getTemp());
                  temp_to_phi_ressources[op.tempId()] = phi_ressources.size();
               }
            }
            phi_ressources.emplace_back(std::move(affinity_related));
         }

         /* erase from live */
         for (const Definition& def : instr->definitions) {
            if (def.isTemp()) {
               live.erase(def.getTemp());
               std::map<unsigned, unsigned>::iterator it = temp_to_phi_ressources.find(def.tempId());
               if (it != temp_to_phi_ressources.end() && def.regClass() == phi_ressources[it->second][0].regClass())
                  phi_ressources[it->second][0] = def.getTemp();
            }
         }
      }
   }
   /* create affinities */
   for (std::vector<Temp>& vec : phi_ressources) {
      assert(vec.size() > 1);
      for (unsigned i = 1; i < vec.size(); i++)
         if (vec[i].id() != vec[0].id())
            affinities[vec[i].id()] = vec[0].id();
   }

   /* state of register file after phis */
   std::vector<std::bitset<128>> sgpr_live_in(program->blocks.size());

   for (Block& block : program->blocks) {
      std::set<Temp>& live = live_out_per_block[block.index];
      /* initialize register file */
      assert(block.index != 0 || live.empty());
      std::array<uint32_t, 512> register_file = {0};
      ctx.war_hint.reset();

      for (Temp t : live) {
         Temp renamed = handle_live_in(t, &block);
         if (ctx.assignments.find(renamed.id()) != ctx.assignments.end()) {
            for (unsigned i = 0; i < t.size(); i++)
               register_file[ctx.assignments[renamed.id()].first + i] = renamed.id();
         }
      }

      std::vector<aco_ptr<Instruction>> instructions;
      std::vector<aco_ptr<Instruction>>::iterator it;

      /* this is a slight adjustment from the paper as we already have phi nodes:
       * We consider them incomplete phis and only handle the definition. */

      /* handle fixed phi definitions */
      for (it = block.instructions.begin(); it != block.instructions.end(); ++it) {
         aco_ptr<Instruction>& phi = *it;
         if (!is_phi(phi))
            break;
         Definition& definition = phi->definitions[0];
         if (!definition.isFixed())
            continue;

         /* check if a dead exec mask phi is needed */
         if (definition.isKill()) {
            for (Operand& op : phi->operands) {
               assert(op.isTemp());
               if (ctx.assignments.find(op.tempId()) == ctx.assignments.end() ||
                   ctx.assignments[op.tempId()].first != exec) {
                   definition.setKill(false);
                   break;
               }
            }
         }

         if (definition.isKill())
            continue;

         assert(definition.physReg() == exec);
         for (unsigned i = 0; i < definition.size(); i++) {
            assert(register_file[definition.physReg() + i] == 0);
            register_file[definition.physReg() + i] = definition.tempId();
         }
         ctx.assignments[definition.tempId()] = {definition.physReg(), definition.regClass()};
      }

      /* look up the affinities */
      for (it = block.instructions.begin(); it != block.instructions.end(); ++it) {
         aco_ptr<Instruction>& phi = *it;
         if (!is_phi(phi))
            break;
         Definition& definition = phi->definitions[0];
         if (definition.isKill() || definition.isFixed())
             continue;

         if (affinities.find(definition.tempId()) != affinities.end() &&
             ctx.assignments.find(affinities[definition.tempId()]) != ctx.assignments.end()) {
            assert(ctx.assignments[affinities[definition.tempId()]].second == definition.regClass());
            PhysReg reg = ctx.assignments[affinities[definition.tempId()]].first;
            bool try_use_special_reg = reg == scc || reg == exec;
            if (try_use_special_reg) {
               for (const Operand& op : phi->operands) {
                  if (!op.isTemp() ||
                      ctx.assignments.find(op.tempId()) == ctx.assignments.end() ||
                      !(ctx.assignments[op.tempId()].first == reg)) {
                     try_use_special_reg = false;
                     break;
                  }
               }
               if (!try_use_special_reg)
                  continue;
            }
            bool reg_free = true;
            for (unsigned i = reg.reg; reg_free && i < reg + definition.size(); i++) {
               if (register_file[i] != 0)
                  reg_free = false;
            }
            /* only assign if register is still free */
            if (reg_free) {
               definition.setFixed(reg);
               for (unsigned i = 0; i < definition.size(); i++)
                  register_file[definition.physReg() + i] = definition.tempId();
               ctx.assignments[definition.tempId()] = {definition.physReg(), definition.regClass()};
            }
         }
      }

      /* find registers for phis without affinity or where the register was blocked */
      for (it = block.instructions.begin();it != block.instructions.end(); ++it) {
         aco_ptr<Instruction>& phi = *it;
         if (!is_phi(phi))
            break;

         Definition& definition = phi->definitions[0];
         if (definition.isKill())
            continue;

         renames[block.index][definition.tempId()] = definition.getTemp();

         if (!definition.isFixed()) {
            std::vector<std::pair<Operand, Definition>> parallelcopy;
            /* try to find a register that is used by at least one operand */
            for (const Operand& op : phi->operands) {
               if (!op.isTemp() ||
                   ctx.assignments.find(op.tempId()) == ctx.assignments.end())
                  continue;
               PhysReg reg = ctx.assignments[op.tempId()].first;
               /* we tried this already on the previous loop */
               if (reg == scc || reg == exec)
                  continue;
               if (get_reg_specified(ctx, register_file, definition.regClass(), parallelcopy, phi, reg)) {
                  definition.setFixed(reg);
                  break;
               }
            }
            if (!definition.isFixed())
               definition.setFixed(get_reg(ctx, register_file, definition.regClass(), parallelcopy, phi));

            /* process parallelcopy */
            for (std::pair<Operand, Definition> pc : parallelcopy) {
               /* see if it's a copy from a different phi */
               //TODO: prefer moving some previous phis over live-ins
               //TODO: somehow prevent phis fixed before the RA from being updated (shouldn't be a problem in practice since they can only be fixed to exec)
               Instruction *prev_phi = NULL;
               std::vector<aco_ptr<Instruction>>::iterator phi_it;
               for (phi_it = instructions.begin(); phi_it != instructions.end(); ++phi_it) {
                  if ((*phi_it)->definitions[0].tempId() == pc.first.tempId())
                     prev_phi = phi_it->get();
               }
               phi_it = it;
               while (!prev_phi && is_phi(*++phi_it)) {
                  if ((*phi_it)->definitions[0].tempId() == pc.first.tempId())
                     prev_phi = phi_it->get();
               }
               if (prev_phi) {
                  /* if so, just update that phi's register */
                  prev_phi->definitions[0].setFixed(pc.second.physReg());
                  ctx.assignments[prev_phi->definitions[0].tempId()] = {pc.second.physReg(), pc.second.regClass()};
                  for (unsigned reg = pc.second.physReg(); reg < pc.second.physReg() + pc.second.size(); reg++)
                     register_file[reg] = prev_phi->definitions[0].tempId();
                  continue;
               }

               /* rename */
               std::map<unsigned, Temp>::iterator orig_it = ctx.orig_names.find(pc.first.tempId());
               Temp orig = pc.first.getTemp();
               if (orig_it != ctx.orig_names.end())
                  orig = orig_it->second;
               else
                  ctx.orig_names[pc.second.tempId()] = orig;
               renames[block.index][orig.id()] = pc.second.getTemp();
               renames[block.index][pc.second.tempId()] = pc.second.getTemp();

               /* otherwise, this is a live-in and we need to create a new phi
                * to move it in this block's predecessors */
               aco_opcode opcode = pc.first.getTemp().is_linear() ? aco_opcode::p_linear_phi : aco_opcode::p_phi;
               std::vector<unsigned>& preds = pc.first.getTemp().is_linear() ? block.linear_preds : block.logical_preds;
               aco_ptr<Instruction> new_phi{create_instruction<Pseudo_instruction>(opcode, Format::PSEUDO, preds.size(), 1)};
               new_phi->definitions[0] = pc.second;
               for (unsigned i = 0; i < preds.size(); i++)
                  new_phi->operands[i] = Operand(pc.first);
               instructions.emplace_back(std::move(new_phi));
            }

            for (unsigned i = 0; i < definition.size(); i++)
               register_file[definition.physReg() + i] = definition.tempId();
            ctx.assignments[definition.tempId()] = {definition.physReg(), definition.regClass()};
         }
         live.emplace(definition.getTemp());

         /* update phi affinities */
         for (const Operand& op : phi->operands) {
            if (op.isTemp() && op.regClass() == phi->definitions[0].regClass())
               affinities[op.tempId()] = definition.tempId();
         }

         instructions.emplace_back(std::move(*it));
      }

      /* fill in sgpr_live_in */
      for (unsigned i = 0; i < ctx.max_used_sgpr; i++)
         sgpr_live_in[block.index][i] = register_file[i];
      sgpr_live_in[block.index][127] = register_file[scc.reg];

      /* Handle all other instructions of the block */
      for (; it != block.instructions.end(); ++it) {
         aco_ptr<Instruction>& instr = *it;

         /* parallelcopies from p_phi are inserted here which means
          * live ranges of killed operands end here as well */
         if (instr->opcode == aco_opcode::p_logical_end) {
            /* no need to process this instruction any further */
            if (block.logical_succs.size() != 1) {
               instructions.emplace_back(std::move(instr));
               continue;
            }

            Block& succ = program->blocks[block.logical_succs[0]];
            unsigned idx = 0;
            for (; idx < succ.logical_preds.size(); idx++) {
               if (succ.logical_preds[idx] == block.index)
                  break;
            }
            for (aco_ptr<Instruction>& phi : succ.instructions) {
               if (phi->opcode == aco_opcode::p_phi) {
                  if (phi->operands[idx].isTemp() &&
                      phi->operands[idx].getTemp().type() == RegType::sgpr &&
                      phi->operands[idx].isFirstKill()) {
                     Temp phi_op = read_variable(phi->operands[idx].getTemp(), block.index);
                     PhysReg reg = ctx.assignments[phi_op.id()].first;
                     assert(register_file[reg] == phi_op.id());
                     register_file[reg] = 0;
                  }
               } else if (phi->opcode != aco_opcode::p_linear_phi) {
                  break;
               }
            }
            instructions.emplace_back(std::move(instr));
            continue;
         }

         std::vector<std::pair<Operand, Definition>> parallelcopy;

         assert(!is_phi(instr));

         /* handle operands */
         for (unsigned i = 0; i < instr->operands.size(); ++i) {
            auto& operand = instr->operands[i];
            if (!operand.isTemp())
               continue;

            /* rename operands */
            operand.setTemp(read_variable(operand.getTemp(), block.index));

            /* check if the operand is fixed */
            if (operand.isFixed()) {

               if (operand.physReg() == ctx.assignments[operand.tempId()].first) {
                  /* we are fine: the operand is already assigned the correct reg */

               } else {
                  /* check if target reg is blocked, and move away the blocking var */
                  if (register_file[operand.physReg().reg]) {
                     uint32_t blocking_id = register_file[operand.physReg().reg];
                     RegClass rc = ctx.assignments[blocking_id].second;
                     Operand pc_op = Operand(Temp{blocking_id, rc});
                     pc_op.setFixed(operand.physReg());
                     Definition pc_def = Definition(Temp{program->allocateId(), pc_op.regClass()});
                     /* find free reg */
                     PhysReg reg = get_reg(ctx, register_file, pc_op.regClass(), parallelcopy, instr);
                     pc_def.setFixed(reg);
                     ctx.assignments[pc_def.tempId()] = {reg, pc_def.regClass()};
                     for (unsigned i = 0; i < operand.size(); i++) {
                        register_file[pc_op.physReg() + i] = 0;
                        register_file[pc_def.physReg() + i] = pc_def.tempId();
                     }
                     parallelcopy.emplace_back(pc_op, pc_def);

                     /* handle renames of previous operands */
                     for (unsigned j = 0; j < i; j++) {
                        Operand& op = instr->operands[j];
                        if (op.isTemp() && op.tempId() == blocking_id) {
                           op.setTemp(pc_def.getTemp());
                           op.setFixed(reg);
                        }
                     }
                  }
                  /* move operand to fixed reg and create parallelcopy pair */
                  Operand pc_op = operand;
                  Temp tmp = Temp{program->allocateId(), operand.regClass()};
                  Definition pc_def = Definition(tmp);
                  pc_def.setFixed(operand.physReg());
                  pc_op.setFixed(ctx.assignments[operand.tempId()].first);
                  operand.setTemp(tmp);
                  ctx.assignments[tmp.id()] = {pc_def.physReg(), pc_def.regClass()};
                  operand.setFixed(pc_def.physReg());
                  for (unsigned i = 0; i < operand.size(); i++) {
                     register_file[pc_op.physReg() + i] = 0;
                     register_file[pc_def.physReg() + i] = tmp.id();
                  }
                  parallelcopy.emplace_back(pc_op, pc_def);
               }
            } else {
               assert(ctx.assignments.find(operand.tempId()) != ctx.assignments.end());
               PhysReg reg = ctx.assignments[operand.tempId()].first;

               if (operand_can_use_reg(instr, i, reg)) {
                  operand.setFixed(ctx.assignments[operand.tempId()].first);
               } else {
                  Operand pc_op = operand;
                  pc_op.setFixed(reg);
                  PhysReg new_reg = get_reg(ctx, register_file, operand.regClass(), parallelcopy, instr);
                  Definition pc_def = Definition(program->allocateId(), new_reg, pc_op.regClass());
                  ctx.assignments[pc_def.tempId()] = {reg, pc_def.regClass()};
                  for (unsigned i = 0; i < operand.size(); i++) {
                        register_file[pc_op.physReg() + i] = 0;
                        register_file[pc_def.physReg() + i] = pc_def.tempId();
                  }
                  parallelcopy.emplace_back(pc_op, pc_def);
                  operand.setFixed(new_reg);
               }

               if (instr->format == Format::EXP ||
                   (instr->isVMEM() && i == 3 && program->chip_class == GFX6) ||
                   (instr->format == Format::DS && static_cast<DS_instruction*>(instr.get())->gds)) {
                  for (unsigned j = 0; j < operand.size(); j++)
                     ctx.war_hint.set(operand.physReg().reg + j);
               }
            }
            std::map<unsigned, phi_info>::iterator phi = phi_map.find(operand.getTemp().id());
            if (phi != phi_map.end())
               phi->second.uses.emplace(instr.get());

         }
         /* remove dead vars from register file */
         for (const Operand& op : instr->operands) {
            if (op.isTemp() && op.isFirstKill())
               for (unsigned j = 0; j < op.size(); j++)
                  register_file[op.physReg() + j] = 0;
         }

         /* try to optimize v_mad_f32 -> v_mac_f32 */
         if (instr->opcode == aco_opcode::v_mad_f32 &&
             instr->operands[2].isTemp() &&
             instr->operands[2].isKill() &&
             instr->operands[2].getTemp().type() == RegType::vgpr &&
             instr->operands[1].isTemp() &&
             instr->operands[1].getTemp().type() == RegType::vgpr) { /* TODO: swap src0 and src1 in this case */
            VOP3A_instruction* vop3 = static_cast<VOP3A_instruction*>(instr.get());
            bool can_use_mac = !(vop3->abs[0] || vop3->abs[1] || vop3->abs[2] ||
                                 vop3->opsel[0] || vop3->opsel[1] || vop3->opsel[2] ||
                                 vop3->neg[0] || vop3->neg[1] || vop3->neg[2] ||
                                 vop3->clamp || vop3->omod);
            if (can_use_mac) {
               instr->format = Format::VOP2;
               instr->opcode = aco_opcode::v_mac_f32;
            }
         }

         /* handle definitions which must have the same register as an operand */
         if (instr->opcode == aco_opcode::v_interp_p2_f32 ||
             instr->opcode == aco_opcode::v_mac_f32 ||
             instr->opcode == aco_opcode::v_writelane_b32 ||
             instr->opcode == aco_opcode::v_writelane_b32_e64) {
            instr->definitions[0].setFixed(instr->operands[2].physReg());
         } else if (instr->opcode == aco_opcode::s_addk_i32 ||
                    instr->opcode == aco_opcode::s_mulk_i32) {
            instr->definitions[0].setFixed(instr->operands[0].physReg());
         } else if ((instr->format == Format::MUBUF ||
                   instr->format == Format::MIMG) &&
                  instr->definitions.size() == 1 &&
                  instr->operands.size() == 4) {
            instr->definitions[0].setFixed(instr->operands[3].physReg());
         }

         ctx.defs_done.reset();

         /* handle fixed definitions first */
         for (unsigned i = 0; i < instr->definitions.size(); ++i) {
            auto& definition = instr->definitions[i];
            if (!definition.isFixed())
               continue;

            adjust_max_used_regs(ctx, definition.regClass(), definition.physReg());
            /* check if the target register is blocked */
            if (register_file[definition.physReg().reg] != 0) {
               /* create parallelcopy pair to move blocking var */
               Temp tmp = {register_file[definition.physReg()], ctx.assignments[register_file[definition.physReg()]].second};
               Operand pc_op = Operand(tmp);
               pc_op.setFixed(ctx.assignments[register_file[definition.physReg().reg]].first);
               RegClass rc = pc_op.regClass();
               tmp = Temp{program->allocateId(), rc};
               Definition pc_def = Definition(tmp);

               /* re-enable the killed operands, so that we don't move the blocking var there */
               for (const Operand& op : instr->operands) {
                  if (op.isTemp() && op.isFirstKill())
                     for (unsigned j = 0; j < op.size(); j++)
                        register_file[op.physReg() + j] = 0xFFFF;
               }

               /* find a new register for the blocking variable */
               PhysReg reg = get_reg(ctx, register_file, rc, parallelcopy, instr);
               /* once again, disable killed operands */
               for (const Operand& op : instr->operands) {
                  if (op.isTemp() && op.isFirstKill())
                     for (unsigned j = 0; j < op.size(); j++)
                        register_file[op.physReg() + j] = 0;
               }
               for (unsigned k = 0; k < i; k++) {
                  if (instr->definitions[k].isTemp() && ctx.defs_done.test(k) && !instr->definitions[k].isKill())
                     for (unsigned j = 0; j < instr->definitions[k].size(); j++)
                        register_file[instr->definitions[k].physReg() + j] = instr->definitions[k].tempId();
               }
               pc_def.setFixed(reg);

               /* finish assignment of parallelcopy */
               ctx.assignments[pc_def.tempId()] = {reg, pc_def.regClass()};
               parallelcopy.emplace_back(pc_op, pc_def);

               /* add changes to reg_file */
               for (unsigned i = 0; i < pc_op.size(); i++) {
                  register_file[pc_op.physReg() + i] = 0x0;
                  register_file[pc_def.physReg() + i] = pc_def.tempId();
               }
            }
            ctx.defs_done.set(i);

            if (!definition.isTemp())
               continue;

            /* set live if it has a kill point */
            if (!definition.isKill())
               live.emplace(definition.getTemp());

            ctx.assignments[definition.tempId()] = {definition.physReg(), definition.regClass()};
            renames[block.index][definition.tempId()] = definition.getTemp();
            for (unsigned j = 0; j < definition.size(); j++)
               register_file[definition.physReg() + j] = definition.tempId();
         }

         /* handle all other definitions */
         for (unsigned i = 0; i < instr->definitions.size(); ++i) {
            auto& definition = instr->definitions[i];

            if (definition.isFixed() || !definition.isTemp())
               continue;

            /* find free reg */
            if (definition.hasHint() && register_file[definition.physReg().reg] == 0)
               definition.setFixed(definition.physReg());
            else if (instr->opcode == aco_opcode::p_split_vector) {
               PhysReg reg = PhysReg{instr->operands[0].physReg() + i * definition.size()};
               if (!get_reg_specified(ctx, register_file, definition.regClass(), parallelcopy, instr, reg))
                  reg = get_reg(ctx, register_file, definition.regClass(), parallelcopy, instr);
               definition.setFixed(reg);
            } else if (instr->opcode == aco_opcode::p_wqm) {
               PhysReg reg;
               if (instr->operands[0].isKill() && instr->operands[0].getTemp().type() == definition.getTemp().type()) {
                  reg = instr->operands[0].physReg();
                  assert(register_file[reg.reg] == 0);
               } else {
                  reg = get_reg(ctx, register_file, definition.regClass(), parallelcopy, instr);
               }
               definition.setFixed(reg);
            } else if (instr->opcode == aco_opcode::p_extract_vector) {
               PhysReg reg;
               if (instr->operands[0].isKill() &&
                   instr->operands[0].getTemp().type() == definition.getTemp().type()) {
                  reg = instr->operands[0].physReg();
                  reg.reg += definition.size() * instr->operands[1].constantValue();
                  assert(register_file[reg.reg] == 0);
               } else {
                  reg = get_reg(ctx, register_file, definition.regClass(), parallelcopy, instr);
               }
               definition.setFixed(reg);
            } else if (instr->opcode == aco_opcode::p_create_vector) {
               PhysReg reg = get_reg_create_vector(ctx, register_file, definition.regClass(),
                                                   parallelcopy, instr);
               definition.setFixed(reg);
            } else if (affinities.find(definition.tempId()) != affinities.end() &&
                       ctx.assignments.find(affinities[definition.tempId()]) != ctx.assignments.end()) {
               PhysReg reg = ctx.assignments[affinities[definition.tempId()]].first;
               if (get_reg_specified(ctx, register_file, definition.regClass(), parallelcopy, instr, reg))
                  definition.setFixed(reg);
               else
                  definition.setFixed(get_reg(ctx, register_file, definition.regClass(), parallelcopy, instr));

            } else if (vectors.find(definition.tempId()) != vectors.end()) {
               Instruction* vec = vectors[definition.tempId()];
               unsigned offset = 0;
               for (const Operand& op : vec->operands) {
                  if (op.isTemp() && op.tempId() == definition.tempId())
                     break;
                  else
                     offset += op.size();
               }
               unsigned k = 0;
               for (const Operand& op : vec->operands) {
                  if (op.isTemp() &&
                      op.tempId() != definition.tempId() &&
                      op.getTemp().type() == definition.getTemp().type() &&
                      ctx.assignments.find(op.tempId()) != ctx.assignments.end()) {
                     PhysReg reg = ctx.assignments[op.tempId()].first;
                     reg.reg = reg - k + offset;
                     if (get_reg_specified(ctx, register_file, definition.regClass(), parallelcopy, instr, reg)) {
                        definition.setFixed(reg);
                        break;
                     }
                  }
                  k += op.size();
               }
               if (!definition.isFixed()) {
                  std::pair<PhysReg, bool> res = get_reg_vec(ctx, register_file, vec->definitions[0].regClass());
                  PhysReg reg = res.first;
                  if (res.second) {
                     reg.reg += offset;
                  } else {
                     reg = get_reg(ctx, register_file, definition.regClass(), parallelcopy, instr);
                  }
                  definition.setFixed(reg);
               }
            } else
               definition.setFixed(get_reg(ctx, register_file, definition.regClass(), parallelcopy, instr));

            assert(definition.isFixed() && ((definition.getTemp().type() == RegType::vgpr && definition.physReg() >= 256) ||
                                            (definition.getTemp().type() != RegType::vgpr && definition.physReg() < 256)));
            ctx.defs_done.set(i);

            /* set live if it has a kill point */
            if (!definition.isKill())
               live.emplace(definition.getTemp());

            ctx.assignments[definition.tempId()] = {definition.physReg(), definition.regClass()};
            renames[block.index][definition.tempId()] = definition.getTemp();
            for (unsigned j = 0; j < definition.size(); j++)
               register_file[definition.physReg() + j] = definition.tempId();
         }

         handle_pseudo(ctx, register_file, instr.get());

         /* kill definitions */
         for (const Definition& def : instr->definitions) {
             if (def.isTemp() && def.isKill()) {
                for (unsigned j = 0; j < def.size(); j++) {
                   register_file[def.physReg() + j] = 0;
                }
             }
         }

         /* emit parallelcopy */
         if (!parallelcopy.empty()) {
            aco_ptr<Pseudo_instruction> pc;
            pc.reset(create_instruction<Pseudo_instruction>(aco_opcode::p_parallelcopy, Format::PSEUDO, parallelcopy.size(), parallelcopy.size()));
            bool temp_in_scc = register_file[scc.reg];
            bool sgpr_operands_alias_defs = false;
            uint64_t sgpr_operands[4] = {0, 0, 0, 0};
            for (unsigned i = 0; i < parallelcopy.size(); i++) {
               if (temp_in_scc && parallelcopy[i].first.isTemp() && parallelcopy[i].first.getTemp().type() == RegType::sgpr) {
                  if (!sgpr_operands_alias_defs) {
                     unsigned reg = parallelcopy[i].first.physReg().reg;
                     unsigned size = parallelcopy[i].first.getTemp().size();
                     sgpr_operands[reg / 64u] |= ((1u << size) - 1) << (reg % 64u);

                     reg = parallelcopy[i].second.physReg().reg;
                     size = parallelcopy[i].second.getTemp().size();
                     if (sgpr_operands[reg / 64u] & ((1u << size) - 1) << (reg % 64u))
                        sgpr_operands_alias_defs = true;
                  }
               }

               pc->operands[i] = parallelcopy[i].first;
               pc->definitions[i] = parallelcopy[i].second;
               assert(pc->operands[i].size() == pc->definitions[i].size());

               /* it might happen that the operand is already renamed. we have to restore the original name. */
               std::map<unsigned, Temp>::iterator it = ctx.orig_names.find(pc->operands[i].tempId());
               if (it != ctx.orig_names.end())
                  pc->operands[i].setTemp(it->second);
               unsigned orig_id = pc->operands[i].tempId();
               ctx.orig_names[pc->definitions[i].tempId()] = pc->operands[i].getTemp();

               pc->operands[i].setTemp(read_variable(pc->operands[i].getTemp(), block.index));
               renames[block.index][orig_id] = pc->definitions[i].getTemp();
               renames[block.index][pc->definitions[i].tempId()] = pc->definitions[i].getTemp();
               std::map<unsigned, phi_info>::iterator phi = phi_map.find(pc->operands[i].tempId());
               if (phi != phi_map.end())
                  phi->second.uses.emplace(pc.get());
            }

            if (temp_in_scc && sgpr_operands_alias_defs) {
               /* disable definitions and re-enable operands */
               for (const Definition& def : instr->definitions) {
                  if (def.isTemp() && !def.isKill()) {
                     for (unsigned j = 0; j < def.size(); j++) {
                        register_file[def.physReg() + j] = 0x0;
                     }
                  }
               }
               for (const Operand& op : instr->operands) {
                  if (op.isTemp() && op.isFirstKill()) {
                     for (unsigned j = 0; j < op.size(); j++)
                        register_file[op.physReg() + j] = 0xFFFF;
                  }
               }

               handle_pseudo(ctx, register_file, pc.get());

               /* re-enable live vars */
               for (const Operand& op : instr->operands) {
                  if (op.isTemp() && op.isFirstKill())
                     for (unsigned j = 0; j < op.size(); j++)
                        register_file[op.physReg() + j] = 0x0;
               }
               for (const Definition& def : instr->definitions) {
                  if (def.isTemp() && !def.isKill()) {
                     for (unsigned j = 0; j < def.size(); j++) {
                        register_file[def.physReg() + j] = def.tempId();
                     }
                  }
               }
            } else {
               pc->tmp_in_scc = false;
            }

            instructions.emplace_back(std::move(pc));
         }

         /* some instructions need VOP3 encoding if operand/definition is not assigned to VCC */
         bool instr_needs_vop3 = !instr->isVOP3() &&
                                 ((instr->format == Format::VOPC && !(instr->definitions[0].physReg() == vcc)) ||
                                  (instr->opcode == aco_opcode::v_cndmask_b32 && !(instr->operands[2].physReg() == vcc)) ||
                                  ((instr->opcode == aco_opcode::v_add_co_u32 ||
                                    instr->opcode == aco_opcode::v_addc_co_u32 ||
                                    instr->opcode == aco_opcode::v_sub_co_u32 ||
                                    instr->opcode == aco_opcode::v_subb_co_u32 ||
                                    instr->opcode == aco_opcode::v_subrev_co_u32 ||
                                    instr->opcode == aco_opcode::v_subbrev_co_u32) &&
                                   !(instr->definitions[1].physReg() == vcc)) ||
                                  ((instr->opcode == aco_opcode::v_addc_co_u32 ||
                                    instr->opcode == aco_opcode::v_subb_co_u32 ||
                                    instr->opcode == aco_opcode::v_subbrev_co_u32) &&
                                   !(instr->operands[2].physReg() == vcc)));
         if (instr_needs_vop3) {

            /* if the first operand is a literal, we have to move it to a reg */
            if (instr->operands.size() && instr->operands[0].isLiteral()) {
               bool can_sgpr = true;
               /* check, if we have to move to vgpr */
               for (const Operand& op : instr->operands) {
                  if (op.isTemp() && op.getTemp().type() == RegType::sgpr) {
                     can_sgpr = false;
                     break;
                  }
               }
               aco_ptr<Instruction> mov;
               if (can_sgpr)
                  mov.reset(create_instruction<SOP1_instruction>(aco_opcode::s_mov_b32, Format::SOP1, 1, 1));
               else
                  mov.reset(create_instruction<VOP1_instruction>(aco_opcode::v_mov_b32, Format::VOP1, 1, 1));
               mov->operands[0] = instr->operands[0];
               Temp tmp = {program->allocateId(), can_sgpr ? s1 : v1};
               mov->definitions[0] = Definition(tmp);
               /* disable definitions and re-enable operands */
               for (const Definition& def : instr->definitions) {
                  for (unsigned j = 0; j < def.size(); j++) {
                     register_file[def.physReg() + j] = 0x0;
                  }
               }
               for (const Operand& op : instr->operands) {
                  if (op.isTemp() && op.isFirstKill()) {
                     for (unsigned j = 0; j < op.size(); j++)
                        register_file[op.physReg() + j] = 0xFFFF;
                  }
               }
               mov->definitions[0].setFixed(get_reg(ctx, register_file, tmp.regClass(), parallelcopy, mov));
               instr->operands[0] = Operand(tmp);
               instr->operands[0].setFixed(mov->definitions[0].physReg());
               instructions.emplace_back(std::move(mov));
               /* re-enable live vars */
               for (const Operand& op : instr->operands) {
                  if (op.isTemp() && op.isFirstKill())
                     for (unsigned j = 0; j < op.size(); j++)
                        register_file[op.physReg() + j] = 0x0;
               }
               for (const Definition& def : instr->definitions) {
                  if (def.isTemp() && !def.isKill()) {
                     for (unsigned j = 0; j < def.size(); j++) {
                        register_file[def.physReg() + j] = def.tempId();
                     }
                  }
               }
            }

            /* change the instruction to VOP3 to enable an arbitrary register pair as dst */
            aco_ptr<Instruction> tmp = std::move(instr);
            Format format = asVOP3(tmp->format);
            instr.reset(create_instruction<VOP3A_instruction>(tmp->opcode, format, tmp->operands.size(), tmp->definitions.size()));
            for (unsigned i = 0; i < instr->operands.size(); i++) {
               Operand& operand = tmp->operands[i];
               instr->operands[i] = operand;
               /* keep phi_map up to date */
               if (operand.isTemp()) {
                  std::map<unsigned, phi_info>::iterator phi = phi_map.find(operand.tempId());
                  if (phi != phi_map.end()) {
                     phi->second.uses.erase(tmp.get());
                     phi->second.uses.emplace(instr.get());
                  }
               }
            }
            std::copy(tmp->definitions.begin(), tmp->definitions.end(), instr->definitions.begin());
         }
         instructions.emplace_back(std::move(*it));

      } /* end for Instr */

      block.instructions = std::move(instructions);

      filled[block.index] = true;
      for (unsigned succ_idx : block.linear_succs) {
         Block& succ = program->blocks[succ_idx];
         /* seal block if all predecessors are filled */
         bool all_filled = true;
         for (unsigned pred_idx : succ.linear_preds) {
            if (!filled[pred_idx]) {
               all_filled = false;
               break;
            }
         }
         if (all_filled) {
            /* finish incomplete phis and check if they became trivial */
            for (Instruction* phi : incomplete_phis[succ_idx]) {
               std::vector<unsigned> preds = phi->definitions[0].getTemp().is_linear() ? succ.linear_preds : succ.logical_preds;
               for (unsigned i = 0; i < phi->operands.size(); i++) {
                  phi->operands[i].setTemp(read_variable(phi->operands[i].getTemp(), preds[i]));
                  phi->operands[i].setFixed(ctx.assignments[phi->operands[i].tempId()].first);
               }
               try_remove_trivial_phi(phi_map.find(phi->definitions[0].tempId()));
            }
            /* complete the original phi nodes, but no need to check triviality */
            for (aco_ptr<Instruction>& instr : succ.instructions) {
               if (!is_phi(instr))
                  break;
               std::vector<unsigned> preds = instr->opcode == aco_opcode::p_phi ? succ.logical_preds : succ.linear_preds;

               for (unsigned i = 0; i < instr->operands.size(); i++) {
                  auto& operand = instr->operands[i];
                  if (!operand.isTemp())
                     continue;
                  operand.setTemp(read_variable(operand.getTemp(), preds[i]));
                  operand.setFixed(ctx.assignments[operand.tempId()].first);
                  std::map<unsigned, phi_info>::iterator phi = phi_map.find(operand.getTemp().id());
                  if (phi != phi_map.end())
                     phi->second.uses.emplace(instr.get());
               }
            }
            sealed[succ_idx] = true;
         }
      }
   } /* end for BB */

   /* remove trivial phis */
   for (Block& block : program->blocks) {
      auto end = std::find_if(block.instructions.begin(), block.instructions.end(),
                              [](aco_ptr<Instruction>& instr) { return !is_phi(instr);});
      auto middle = std::remove_if(block.instructions.begin(), end,
                                   [](const aco_ptr<Instruction>& instr) { return instr->definitions.empty();});
      block.instructions.erase(middle, end);
   }

   /* find scc spill registers which may be needed for parallelcopies created by phis */
   for (Block& block : program->blocks) {
      if (block.linear_preds.size() <= 1)
         continue;

      std::bitset<128> regs = sgpr_live_in[block.index];
      if (!regs[127])
         continue;

      /* choose a register */
      int16_t reg = 0;
      for (; reg < ctx.program->max_reg_demand.sgpr && regs[reg]; reg++)
         ;
      assert(reg < ctx.program->max_reg_demand.sgpr);
      adjust_max_used_regs(ctx, s1, reg);

      /* update predecessors */
      for (unsigned& pred_index : block.linear_preds) {
         Block& pred = program->blocks[pred_index];
         pred.scc_live_out = true;
         pred.scratch_sgpr = PhysReg{(uint16_t)reg};
      }
   }

   /* num_gpr = rnd_up(max_used_gpr + 1) */
   program->config->num_vgprs = align(ctx.max_used_vgpr + 1, 4);
   if (program->family == CHIP_TONGA || program->family == CHIP_ICELAND) /* workaround hardware bug */
      program->config->num_sgprs = get_sgpr_alloc(program, program->sgpr_limit);
   else
      program->config->num_sgprs = align(ctx.max_used_sgpr + 1 + get_extra_sgprs(program), 8);
}

}
