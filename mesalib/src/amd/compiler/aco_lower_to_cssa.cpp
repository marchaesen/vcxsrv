/*
 * Copyright © 2019 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "aco_builder.h"
#include "aco_ir.h"

#include <algorithm>
#include <map>
#include <unordered_map>
#include <vector>

/*
 * Implements an algorithm to lower to Conventional SSA Form (CSSA).
 * After "Revisiting Out-of-SSA Translation for Correctness, CodeQuality, and Efficiency"
 * by B. Boissinot, A. Darte, F. Rastello, B. Dupont de Dinechin, C. Guillon,
 *
 * By lowering the IR to CSSA, the insertion of parallelcopies is separated from
 * the register coalescing problem. Additionally, correctness is ensured w.r.t. spilling.
 * The algorithm coalesces non-interfering phi-resources while taking value-equality
 * into account. Re-indexes the SSA-defs.
 */

namespace aco {
namespace {

typedef std::vector<Temp> merge_set;

struct copy {
   Definition def;
   Operand op;
};

struct merge_node {
   Operand value = Operand(); /* original value: can be an SSA-def or constant value */
   uint32_t index = -1u;      /* index into the vector of merge sets */
   uint32_t defined_at = -1u; /* defining block */

   /* We also remember two closest equal intersecting ancestors. Because they intersect with this
    * merge node, they must dominate it (intersection isn't possible otherwise) and have the same
    * value (or else they would not be allowed to be in the same merge set).
    */
   Temp equal_anc_in = Temp();  /* within the same merge set */
   Temp equal_anc_out = Temp(); /* from the other set we're currently trying to merge with */
};

struct cssa_ctx {
   Program* program;
   std::vector<std::vector<copy>> parallelcopies; /* copies per block */
   std::vector<merge_set> merge_sets;             /* each vector is one (ordered) merge set */
   std::unordered_map<uint32_t, merge_node> merge_node_table; /* tempid -> merge node */
};

/* create (virtual) parallelcopies for each phi instruction and
 * already merge copy-definitions with phi-defs into merge sets */
void
collect_parallelcopies(cssa_ctx& ctx)
{
   ctx.parallelcopies.resize(ctx.program->blocks.size());
   Builder bld(ctx.program);
   for (Block& block : ctx.program->blocks) {
      for (aco_ptr<Instruction>& phi : block.instructions) {
         if (phi->opcode != aco_opcode::p_phi && phi->opcode != aco_opcode::p_linear_phi)
            break;

         const Definition& def = phi->definitions[0];

         /* if the definition is not temp, it is the exec mask.
          * We can reload the exec mask directly from the spill slot.
          */
         if (!def.isTemp() || def.isKill())
            continue;

         Block::edge_vec& preds =
            phi->opcode == aco_opcode::p_phi ? block.logical_preds : block.linear_preds;
         uint32_t index = ctx.merge_sets.size();
         merge_set set;

         bool has_preheader_copy = false;
         for (unsigned i = 0; i < phi->operands.size(); i++) {
            Operand op = phi->operands[i];
            if (op.isUndefined())
               continue;

            if (def.regClass().type() == RegType::sgpr && !op.isTemp()) {
               /* SGPR inline constants and literals on GFX10+ can be spilled
                * and reloaded directly (without intermediate register) */
               if (op.isConstant()) {
                  if (ctx.program->gfx_level >= GFX10)
                     continue;
                  if (op.size() == 1 && !op.isLiteral())
                     continue;
               } else {
                  assert(op.isFixed() && op.physReg() == exec);
                  continue;
               }
            }

            /* create new temporary and rename operands */
            Temp tmp = bld.tmp(def.regClass());
            ctx.parallelcopies[preds[i]].emplace_back(copy{Definition(tmp), op});
            phi->operands[i] = Operand(tmp);
            phi->operands[i].setKill(true);

            /* place the new operands in the same merge set */
            set.emplace_back(tmp);
            ctx.merge_node_table[tmp.id()] = {op, index, preds[i]};

            has_preheader_copy |= i == 0 && block.kind & block_kind_loop_header;
         }

         if (set.empty())
            continue;

         /* place the definition in dominance-order */
         if (def.isTemp()) {
            if (has_preheader_copy)
               set.emplace(std::next(set.begin()), def.getTemp());
            else if (block.kind & block_kind_loop_header)
               set.emplace(set.begin(), def.getTemp());
            else
               set.emplace_back(def.getTemp());
            ctx.merge_node_table[def.tempId()] = {Operand(def.getTemp()), index, block.index};
         }
         ctx.merge_sets.emplace_back(set);
      }
   }
}

/* check whether the definition of a comes after b. */
inline bool
defined_after(cssa_ctx& ctx, Temp a, Temp b)
{
   merge_node& node_a = ctx.merge_node_table[a.id()];
   merge_node& node_b = ctx.merge_node_table[b.id()];
   if (node_a.defined_at == node_b.defined_at)
      return a.id() > b.id();

   return node_a.defined_at > node_b.defined_at;
}

/* check whether a dominates b where b is defined after a */
inline bool
dominates(cssa_ctx& ctx, Temp a, Temp b)
{
   assert(defined_after(ctx, b, a));
   Block& parent = ctx.program->blocks[ctx.merge_node_table[a.id()].defined_at];
   Block& child = ctx.program->blocks[ctx.merge_node_table[b.id()].defined_at];
   if (b.regClass().type() == RegType::vgpr)
      return dominates_logical(parent, child);
   else
      return dominates_linear(parent, child);
}

/* Checks whether some variable is live-out, not considering any phi-uses. */
inline bool
is_live_out(cssa_ctx& ctx, Temp var, uint32_t block_idx)
{
   Block::edge_vec& succs = var.is_linear() ? ctx.program->blocks[block_idx].linear_succs
                                            : ctx.program->blocks[block_idx].logical_succs;

   return std::any_of(succs.begin(), succs.end(), [&](unsigned succ)
                      { return ctx.program->live.live_in[succ].count(var.id()); });
}

/* check intersection between var and parent:
 * We already know that parent dominates var. */
inline bool
intersects(cssa_ctx& ctx, Temp var, Temp parent)
{
   merge_node& node_var = ctx.merge_node_table[var.id()];
   merge_node& node_parent = ctx.merge_node_table[parent.id()];
   assert(node_var.index != node_parent.index);
   uint32_t block_idx = node_var.defined_at;

   /* if parent is defined in a different block than var */
   if (node_parent.defined_at < node_var.defined_at) {
      /* if the parent is not live-in, they don't interfere */
      if (!ctx.program->live.live_in[block_idx].count(parent.id()))
         return false;
   }

   /* if the parent is live-out at the definition block of var, they intersect */
   bool parent_live = is_live_out(ctx, parent, block_idx);
   if (parent_live)
      return true;

   for (const copy& cp : ctx.parallelcopies[block_idx]) {
      /* if var is defined at the edge, they don't intersect */
      if (cp.def.getTemp() == var)
         return false;
      if (cp.op.isTemp() && cp.op.getTemp() == parent)
         parent_live = true;
   }
   /* if the parent is live at the edge, they intersect */
   if (parent_live)
      return true;

   /* both, parent and var, are present in the same block */
   const Block& block = ctx.program->blocks[block_idx];
   for (auto it = block.instructions.crbegin(); it != block.instructions.crend(); ++it) {
      /* if the parent was not encountered yet, it can only be used by a phi */
      if (is_phi(it->get()))
         break;

      for (const Definition& def : (*it)->definitions) {
         if (!def.isTemp())
            continue;
         /* if parent was not found yet, they don't intersect */
         if (def.getTemp() == var)
            return false;
      }

      for (const Operand& op : (*it)->operands) {
         if (!op.isTemp())
            continue;
         /* if the var was defined before this point, they intersect */
         if (op.getTemp() == parent)
            return true;
      }
   }

   return false;
}

/* check interference between var and parent:
 * i.e. they have different values and intersect.
 * If parent and var intersect and share the same value, also updates the equal ancestor. */
inline bool
interference(cssa_ctx& ctx, Temp var, Temp parent)
{
   assert(var != parent);
   merge_node& node_var = ctx.merge_node_table[var.id()];
   node_var.equal_anc_out = Temp();

   if (node_var.index == ctx.merge_node_table[parent.id()].index) {
      /* Check/update in other set. equal_anc_out is only present if it intersects with 'parent',
       * but that's fine since it has to for it to intersect with 'var'. */
      parent = ctx.merge_node_table[parent.id()].equal_anc_out;
   }

   Temp tmp = parent;
   /* Check if 'var' intersects with 'parent' or any ancestors which might intersect too. */
   while (tmp != Temp() && !intersects(ctx, var, tmp)) {
      merge_node& node_tmp = ctx.merge_node_table[tmp.id()];
      tmp = node_tmp.equal_anc_in;
   }

   /* no intersection found */
   if (tmp == Temp())
      return false;

   /* var and parent, same value and intersect, but in different sets */
   if (node_var.value == ctx.merge_node_table[parent.id()].value) {
      node_var.equal_anc_out = tmp;
      return false;
   }

   /* var and parent, different values and intersect */
   return true;
}

/* tries to merge set_b into set_a of given temporary and
 * drops that temporary as it is being coalesced */
bool
try_merge_merge_set(cssa_ctx& ctx, Temp dst, merge_set& set_b)
{
   auto def_node_it = ctx.merge_node_table.find(dst.id());
   uint32_t index = def_node_it->second.index;
   merge_set& set_a = ctx.merge_sets[index];
   std::vector<Temp> dom; /* stack of the traversal */
   merge_set union_set;   /* the new merged merge-set */
   uint32_t i_a = 0;
   uint32_t i_b = 0;

   while (i_a < set_a.size() || i_b < set_b.size()) {
      Temp current;
      if (i_a == set_a.size())
         current = set_b[i_b++];
      else if (i_b == set_b.size())
         current = set_a[i_a++];
      /* else pick the one defined first */
      else if (defined_after(ctx, set_a[i_a], set_b[i_b]))
         current = set_b[i_b++];
      else
         current = set_a[i_a++];

      while (!dom.empty() && !dominates(ctx, dom.back(), current))
         dom.pop_back(); /* not the desired parent, remove */

      if (!dom.empty() && interference(ctx, current, dom.back())) {
         for (Temp t : union_set)
            ctx.merge_node_table[t.id()].equal_anc_out = Temp();
         return false; /* intersection detected */
      }

      dom.emplace_back(current); /* otherwise, keep checking */
      if (current != dst)
         union_set.emplace_back(current); /* maintain the new merge-set sorted */
   }

   /* update hashmap */
   for (Temp t : union_set) {
      merge_node& node = ctx.merge_node_table[t.id()];
      /* update the equal ancestors:
       * i.e. the 'closest' dominating def which intersects */
      Temp in = node.equal_anc_in;
      Temp out = node.equal_anc_out;
      if (in == Temp() || (out != Temp() && defined_after(ctx, out, in)))
         node.equal_anc_in = out;
      node.equal_anc_out = Temp();
      /* update merge-set index */
      node.index = index;
   }
   set_b = merge_set(); /* free the old set_b */
   ctx.merge_sets[index] = union_set;
   ctx.merge_node_table.erase(dst.id()); /* remove the temporary */

   return true;
}

/* returns true if the copy can safely be omitted */
bool
try_coalesce_copy(cssa_ctx& ctx, copy copy, uint32_t block_idx)
{
   /* we can only coalesce temporaries */
   if (!copy.op.isTemp() || !copy.op.isKill())
      return false;

   /* we can only coalesce copies of the same register class */
   if (copy.op.regClass() != copy.def.regClass())
      return false;

   /* try emplace a merge_node for the copy operand */
   merge_node& op_node = ctx.merge_node_table[copy.op.tempId()];
   if (op_node.defined_at == -1u) {
      /* find defining block of operand */
      while (ctx.program->live.live_in[block_idx].count(copy.op.tempId()))
         block_idx = copy.op.regClass().type() == RegType::vgpr
                        ? ctx.program->blocks[block_idx].logical_idom
                        : ctx.program->blocks[block_idx].linear_idom;
      op_node.defined_at = block_idx;
      op_node.value = copy.op;
   }

   /* check if this operand has not yet been coalesced */
   if (op_node.index == -1u) {
      merge_set op_set = merge_set{copy.op.getTemp()};
      return try_merge_merge_set(ctx, copy.def.getTemp(), op_set);
   }

   /* check if this operand has been coalesced into the same set */
   assert(ctx.merge_node_table.count(copy.def.tempId()));
   if (op_node.index == ctx.merge_node_table[copy.def.tempId()].index)
      return true;

   /* otherwise, try to coalesce both merge sets */
   return try_merge_merge_set(ctx, copy.def.getTemp(), ctx.merge_sets[op_node.index]);
}

/* node in the location-transfer-graph */
struct ltg_node {
   copy* cp;
   uint32_t read_idx;
   uint32_t num_uses = 0;
};

/* emit the copies in an order that does not
 * create interferences within a merge-set */
void
emit_copies_block(Builder& bld, std::map<uint32_t, ltg_node>& ltg, RegType type)
{
   RegisterDemand live_changes;
   RegisterDemand reg_demand = bld.it->get()->register_demand - get_temp_registers(bld.it->get()) -
                               get_live_changes(bld.it->get());
   auto&& it = ltg.begin();
   while (it != ltg.end()) {
      copy& cp = *it->second.cp;

      /* wrong regclass or still needed as operand */
      if (cp.def.regClass().type() != type || it->second.num_uses > 0) {
         ++it;
         continue;
      }

      /* update the location transfer graph */
      if (it->second.read_idx != -1u) {
         auto&& other = ltg.find(it->second.read_idx);
         if (other != ltg.end())
            other->second.num_uses--;
      }
      ltg.erase(it);

      /* Remove the kill flag if we still need this operand for other copies. */
      if (cp.op.isKill() && std::any_of(ltg.begin(), ltg.end(),
                                        [&](auto& other) { return other.second.cp->op == cp.op; }))
         cp.op.setKill(false);

      /* emit the copy */
      Instruction* instr = bld.copy(cp.def, cp.op);
      live_changes += get_live_changes(instr);
      RegisterDemand temps = get_temp_registers(instr);
      instr->register_demand = reg_demand + live_changes + temps;

      it = ltg.begin();
   }

   /* count the number of remaining circular dependencies */
   unsigned num = std::count_if(
      ltg.begin(), ltg.end(), [&](auto& n) { return n.second.cp->def.regClass().type() == type; });

   /* if there are circular dependencies, we just emit them as single parallelcopy */
   if (num) {
      // TODO: this should be restricted to a feasible number of registers
      // and otherwise use a temporary to avoid having to reload more (spilled)
      // variables than we have registers.
      aco_ptr<Instruction> copy{
         create_instruction(aco_opcode::p_parallelcopy, Format::PSEUDO, num, num)};
      it = ltg.begin();
      for (unsigned i = 0; i < num; i++) {
         while (it->second.cp->def.regClass().type() != type)
            ++it;

         copy->definitions[i] = it->second.cp->def;
         copy->operands[i] = it->second.cp->op;
         it = ltg.erase(it);
      }
      live_changes += get_live_changes(copy.get());
      RegisterDemand temps = get_temp_registers(copy.get());
      copy->register_demand = reg_demand + live_changes + temps;
      bld.insert(std::move(copy));
   }

   /* Update RegisterDemand after inserted copies */
   for (auto instr_it = bld.it; instr_it != bld.instructions->end(); ++instr_it) {
      instr_it->get()->register_demand += live_changes;
   }
}

/* either emits or coalesces all parallelcopies and
 * renames the phi-operands accordingly. */
void
emit_parallelcopies(cssa_ctx& ctx)
{
   std::unordered_map<uint32_t, Operand> renames;

   /* we iterate backwards to prioritize coalescing in else-blocks */
   for (int i = ctx.program->blocks.size() - 1; i >= 0; i--) {
      if (ctx.parallelcopies[i].empty())
         continue;

      std::map<uint32_t, ltg_node> ltg;
      bool has_vgpr_copy = false;
      bool has_sgpr_copy = false;

      /* first, try to coalesce all parallelcopies */
      for (copy& cp : ctx.parallelcopies[i]) {
         if (try_coalesce_copy(ctx, cp, i)) {
            assert(cp.op.isTemp() && cp.op.isKill());
            /* As this temp will be used as phi operand and becomes live-out,
             * remove the kill flag from any other copy of this same temp.
             */
            for (copy& other : ctx.parallelcopies[i]) {
               if (&other != &cp && other.op.isTemp() && other.op.getTemp() == cp.op.getTemp())
                  other.op.setKill(false);
            }
            renames.emplace(cp.def.tempId(), cp.op);
         } else {
            uint32_t read_idx = -1u;
            if (cp.op.isTemp()) {
               read_idx = ctx.merge_node_table[cp.op.tempId()].index;
               /* In case the original phi-operand was killed, it might still be live-out
                * if the logical successor is not the same as linear successors.
                * Thus, re-check whether the temp is live-out.
                */
               cp.op.setKill(cp.op.isKill() && !is_live_out(ctx, cp.op.getTemp(), i));
               cp.op.setFirstKill(cp.op.isKill());
            }
            uint32_t write_idx = ctx.merge_node_table[cp.def.tempId()].index;
            assert(write_idx != -1u);
            ltg[write_idx] = {&cp, read_idx};

            bool is_vgpr = cp.def.regClass().type() == RegType::vgpr;
            has_vgpr_copy |= is_vgpr;
            has_sgpr_copy |= !is_vgpr;
         }
      }

      /* build location-transfer-graph */
      for (auto& pair : ltg) {
         if (pair.second.read_idx == -1u)
            continue;
         auto&& it = ltg.find(pair.second.read_idx);
         if (it != ltg.end())
            it->second.num_uses++;
      }

      /* emit parallelcopies ordered */
      Builder bld(ctx.program);
      Block& block = ctx.program->blocks[i];

      if (has_vgpr_copy) {
         /* emit VGPR copies */
         auto IsLogicalEnd = [](const aco_ptr<Instruction>& inst) -> bool
         { return inst->opcode == aco_opcode::p_logical_end; };
         auto it =
            std::find_if(block.instructions.rbegin(), block.instructions.rend(), IsLogicalEnd);
         bld.reset(&block.instructions, std::prev(it.base()));
         emit_copies_block(bld, ltg, RegType::vgpr);
      }

      if (has_sgpr_copy) {
         /* emit SGPR copies */
         bld.reset(&block.instructions, std::prev(block.instructions.end()));
         emit_copies_block(bld, ltg, RegType::sgpr);
      }
   }

   RegisterDemand new_demand;
   for (Block& block : ctx.program->blocks) {
      /* Finally, rename coalesced phi operands */
      for (aco_ptr<Instruction>& phi : block.instructions) {
         if (phi->opcode != aco_opcode::p_phi && phi->opcode != aco_opcode::p_linear_phi)
            break;

         for (Operand& op : phi->operands) {
            if (!op.isTemp())
               continue;
            auto&& it = renames.find(op.tempId());
            if (it != renames.end()) {
               op = it->second;
               renames.erase(it);
            }
         }
      }

      /* Resummarize the block's register demand */
      block.register_demand = block.live_in_demand;
      for (const aco_ptr<Instruction>& instr : block.instructions)
         block.register_demand.update(instr->register_demand);
      new_demand.update(block.register_demand);
   }

   /* Update max_reg_demand and num_waves */
   update_vgpr_sgpr_demand(ctx.program, new_demand);

   assert(renames.empty());
}

} /* end namespace */

void
lower_to_cssa(Program* program)
{
   reindex_ssa(program);
   cssa_ctx ctx = {program};
   collect_parallelcopies(ctx);
   emit_parallelcopies(ctx);

   /* Validate live variable information */
   if (!validate_live_vars(program))
      abort();
}
} // namespace aco
