/*
 * Copyright (c) 2017-2019 Gert Wollny
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "sfn_liverange.h"
#include "sfn_debug.h"
#include "sfn_value.h"
#include "sfn_value_gpr.h"

#include "program/prog_instruction.h"
#include "util/bitscan.h"
#include "util/u_math.h"

#include <limits>
#include <cstdlib>
#include <iomanip>

/* std::sort is significantly faster than qsort */
#include <algorithm>

/* If <windows.h> is included this is defined and clashes with
 * std::numeric_limits<>::max()
 */
#ifdef max
#undef max
#endif


namespace r600 {

using std::numeric_limits;
using std::unique_ptr;
using std::setw;

prog_scope_storage::prog_scope_storage(int n):
   current_slot(0),
   storage(n)
{
}

prog_scope_storage::~prog_scope_storage()
{
}

prog_scope*
prog_scope_storage::create(prog_scope *p, prog_scope_type type, int id,
                           int lvl, int s_begin)
{
   storage[current_slot] = prog_scope(p, type, id, lvl, s_begin);
   return &storage[current_slot++];
}

prog_scope::prog_scope(prog_scope *parent, prog_scope_type type, int id,
                       int depth, int scope_begin):
   scope_type(type),
   scope_id(id),
   scope_nesting_depth(depth),
   scope_begin(scope_begin),
   scope_end(-1),
   break_loop_line(numeric_limits<int>::max()),
   parent_scope(parent)
{
}

prog_scope::prog_scope():
   prog_scope(nullptr, undefined_scope, -1, -1, -1)
{
}

prog_scope_type prog_scope::type() const
{
   return scope_type;
}

prog_scope *prog_scope::parent() const
{
   return parent_scope;
}

int prog_scope::nesting_depth() const
{
   return scope_nesting_depth;
}

bool prog_scope::is_loop() const
{
   return (scope_type == loop_body);
}

bool prog_scope::is_in_loop() const
{
   if (scope_type == loop_body)
      return true;

   if (parent_scope)
      return parent_scope->is_in_loop();

   return false;
}

const prog_scope *prog_scope::innermost_loop() const
{
   if (scope_type == loop_body)
      return this;

   if (parent_scope)
      return parent_scope->innermost_loop();

   return nullptr;
}

const prog_scope *prog_scope::outermost_loop() const
{
   const prog_scope *loop = nullptr;
   const prog_scope *p = this;

   do {
      if (p->type() == loop_body)
         loop = p;
      p = p->parent();
   } while (p);

   return loop;
}

bool prog_scope::is_child_of_ifelse_id_sibling(const prog_scope *scope) const
{
   const prog_scope *my_parent = in_parent_ifelse_scope();
   while (my_parent) {
      /* is a direct child? */
      if (my_parent == scope)
         return false;
      /* is a child of the conditions sibling? */
      if (my_parent->id() == scope->id())
         return true;
      my_parent = my_parent->in_parent_ifelse_scope();
   }
   return false;
}

bool prog_scope::is_child_of(const prog_scope *scope) const
{
   const prog_scope *my_parent = parent();
   while (my_parent) {
      if (my_parent == scope)
         return true;
      my_parent = my_parent->parent();
   }
   return false;
}

const prog_scope *prog_scope::enclosing_conditional() const
{
   if (is_conditional())
      return this;

   if (parent_scope)
      return parent_scope->enclosing_conditional();

   return nullptr;
}

bool prog_scope::contains_range_of(const prog_scope& other) const
{
   return (begin() <= other.begin()) && (end() >= other.end());
}

bool prog_scope::is_conditional() const
{
   return scope_type == if_branch ||
         scope_type == else_branch ||
         scope_type == switch_case_branch ||
         scope_type == switch_default_branch;
}

const prog_scope *prog_scope::in_else_scope() const
{
   if (scope_type == else_branch)
      return this;

   if (parent_scope)
      return parent_scope->in_else_scope();

   return nullptr;
}

const prog_scope *prog_scope::in_parent_ifelse_scope() const
{
        if (parent_scope)
                return parent_scope->in_ifelse_scope();
        else
                return nullptr;
}

const prog_scope *prog_scope::in_ifelse_scope() const
{
   if (scope_type == if_branch ||
       scope_type == else_branch)
      return this;

   if (parent_scope)
      return parent_scope->in_ifelse_scope();

   return nullptr;
}

bool prog_scope::is_switchcase_scope_in_loop() const
{
   return (scope_type == switch_case_branch ||
           scope_type == switch_default_branch) &&
         is_in_loop();
}

bool prog_scope::break_is_for_switchcase() const
{
   if (scope_type == loop_body)
      return false;

   if (scope_type == switch_case_branch ||
       scope_type == switch_default_branch ||
       scope_type == switch_body)
      return true;

   if (parent_scope)
      return parent_scope->break_is_for_switchcase();

   return false;
}

int prog_scope::id() const
{
   return scope_id;
}

int prog_scope::begin() const
{
   return scope_begin;
}

int prog_scope::end() const
{
   return scope_end;
}

void prog_scope::set_end(int end)
{
   if (scope_end == -1)
      scope_end = end;
}

void prog_scope::set_loop_break_line(int line)
{
   if (scope_type == loop_body) {
      break_loop_line = MIN2(break_loop_line, line);
   } else {
      if (parent_scope)
         parent()->set_loop_break_line(line);
   }
}

int prog_scope::loop_break_line() const
{
   return break_loop_line;
}

temp_access::temp_access():
   access_mask(0),
   needs_component_tracking(false),
   is_array_element(false)
{
}

void temp_access::update_access_mask(int mask)
{
   if (access_mask && access_mask != mask)
      needs_component_tracking = true;
   access_mask |= mask;
}

void temp_access::record_write(int line, prog_scope *scope, int writemask, bool is_array_elm)
{


   update_access_mask(writemask);
   is_array_element |= is_array_elm;

   if (writemask & WRITEMASK_X)
      comp[0].record_write(line, scope);
   if (writemask & WRITEMASK_Y)
      comp[1].record_write(line, scope);
   if (writemask & WRITEMASK_Z)
      comp[2].record_write(line, scope);
   if (writemask & WRITEMASK_W)
      comp[3].record_write(line, scope);
}

void temp_access::record_read(int line, prog_scope *scope, int readmask, bool is_array_elm)
{
   update_access_mask(readmask);
   is_array_element |= is_array_elm;

   if (readmask & WRITEMASK_X)
      comp[0].record_read(line, scope);
   if (readmask & WRITEMASK_Y)
      comp[1].record_read(line, scope);
   if (readmask & WRITEMASK_Z)
      comp[2].record_read(line, scope);
   if (readmask & WRITEMASK_W)
      comp[3].record_read(line, scope);
}

inline static register_live_range make_live_range(int b, int e)
{
   register_live_range lt;
   lt.begin = b;
   lt.end = e;
   lt.is_array_elm = false;
   return lt;
}

register_live_range temp_access::get_required_live_range()
{
   register_live_range result = make_live_range(-1, -1);

   unsigned mask = access_mask;
   while (mask) {
      unsigned chan = u_bit_scan(&mask);
      register_live_range lt = comp[chan].get_required_live_range();

      if (lt.begin >= 0) {
         if ((result.begin < 0) || (result.begin > lt.begin))
            result.begin = lt.begin;
      }

      if (lt.end > result.end)
         result.end = lt.end;

      if (!needs_component_tracking)
         break;
   }
   result.is_array_elm = is_array_element;

   return result;
}

const int
temp_comp_access::conditionality_untouched = std::numeric_limits<int>::max();

const int
temp_comp_access::write_is_unconditional = std::numeric_limits<int>::max() - 1;


temp_comp_access::temp_comp_access():
   last_read_scope(nullptr),
   first_read_scope(nullptr),
   first_write_scope(nullptr),
   first_write(-1),
   last_read(-1),
   last_write(-1),
   first_read(numeric_limits<int>::max()),
   conditionality_in_loop_id(conditionality_untouched),
   if_scope_write_flags(0),
   next_ifelse_nesting_depth(0),
   current_unpaired_if_write_scope(nullptr),
   was_written_in_current_else_scope(false)
{
}

void temp_comp_access::record_read(int line, prog_scope *scope)
{
   last_read_scope = scope;
   if (last_read < line)
      last_read = line;

   if (first_read > line) {
      first_read = line;
      first_read_scope = scope;
   }

   /* If the conditionality of the first write is already resolved then
    * no further checks are required.
    */
   if (conditionality_in_loop_id == write_is_unconditional ||
       conditionality_in_loop_id == write_is_conditional)
      return;

   /* Check whether we are in a condition within a loop */
   const prog_scope *ifelse_scope = scope->in_ifelse_scope();
   const prog_scope *enclosing_loop;
   if (ifelse_scope && (enclosing_loop = ifelse_scope->innermost_loop())) {

      /* If we have either not yet written to this register nor writes are
       * resolved as unconditional in the enclosing loop then check whether
       * we read before write in an IF/ELSE branch.
       */
      if ((conditionality_in_loop_id != write_is_conditional) &&
          (conditionality_in_loop_id != enclosing_loop->id())) {

         if (current_unpaired_if_write_scope)  {

            /* Has been written in this or a parent scope? - this makes the temporary
             * unconditionally set at this point.
             */
            if (scope->is_child_of(current_unpaired_if_write_scope))
               return;

            /* Has been written in the same scope before it was read? */
            if (ifelse_scope->type() == if_branch) {
               if (current_unpaired_if_write_scope->id() == scope->id())
                  return;
            } else {
               if (was_written_in_current_else_scope)
                  return;
            }
         }

         /* The temporary was read (conditionally) before it is written, hence
          * it should survive a loop. This can be signaled like if it were
          * conditionally written.
          */
         conditionality_in_loop_id = write_is_conditional;
      }
   }
}

void temp_comp_access::record_write(int line, prog_scope *scope)
{
   last_write = line;

   if (first_write < 0) {
      first_write = line;
      first_write_scope = scope;

      /* If the first write we encounter is not in a conditional branch, or
       * the conditional write is not within a loop, then this is to be
       * considered an unconditional dominant write.
       */
      const prog_scope *conditional = scope->enclosing_conditional();
      if (!conditional || !conditional->innermost_loop()) {
         conditionality_in_loop_id = write_is_unconditional;
      }
   }

   /* The conditionality of the first write is already resolved. */
   if (conditionality_in_loop_id == write_is_unconditional ||
       conditionality_in_loop_id == write_is_conditional)
      return;

   /* If the nesting depth is larger than the supported level,
    * then we assume conditional writes.
    */
   if (next_ifelse_nesting_depth >= supported_ifelse_nesting_depth) {
      conditionality_in_loop_id = write_is_conditional;
      return;
   }

   /* If we are in an IF/ELSE scope within a loop and the loop has not
    * been resolved already, then record this write.
    */
   const prog_scope *ifelse_scope = scope->in_ifelse_scope();
   if (ifelse_scope && ifelse_scope->innermost_loop() &&
       ifelse_scope->innermost_loop()->id()  != conditionality_in_loop_id)
      record_ifelse_write(*ifelse_scope);
}

void temp_comp_access::record_ifelse_write(const prog_scope& scope)
{
   if (scope.type() == if_branch) {
      /* The first write in an IF branch within a loop implies unresolved
       * conditionality (if it was untouched or unconditional before).
       */
      conditionality_in_loop_id = conditionality_unresolved;
      was_written_in_current_else_scope = false;
      record_if_write(scope);
   } else {
      was_written_in_current_else_scope = true;
      record_else_write(scope);
   }
}

void temp_comp_access::record_if_write(const prog_scope& scope)
{
   /* Don't record write if this IF scope if it ...
    * - is not the first write in this IF scope,
    * - has already been written in a parent IF scope.
    * In both cases this write is a secondary write that doesn't contribute
    * to resolve conditionality.
    *
    * Record the write if it
    * - is the first one (obviously),
    * - happens in an IF branch that is a child of the ELSE branch of the
    *   last active IF/ELSE pair. In this case recording this write is used to
    *   established whether the write is (un-)conditional in the scope enclosing
    *   this outer IF/ELSE pair.
    */
   if (!current_unpaired_if_write_scope ||
       (current_unpaired_if_write_scope->id() != scope.id() &&
        scope.is_child_of_ifelse_id_sibling(current_unpaired_if_write_scope)))  {
      if_scope_write_flags |= 1 << next_ifelse_nesting_depth;
      current_unpaired_if_write_scope = &scope;
      next_ifelse_nesting_depth++;
   }
}

void temp_comp_access::record_else_write(const prog_scope& scope)
{
   int mask = 1 << (next_ifelse_nesting_depth - 1);

   /* If the temporary was written in an IF branch on the same scope level
    * and this branch is the sibling of this ELSE branch, then we have a
    * pair of writes that makes write access to this temporary unconditional
    * in the enclosing scope.
    */

   if ((if_scope_write_flags & mask) &&
       (scope.id() == current_unpaired_if_write_scope->id())) {
          --next_ifelse_nesting_depth;
         if_scope_write_flags &= ~mask;

         /* The following code deals with propagating unconditionality from
          * inner levels of nested IF/ELSE to the outer levels like in
          *
          * 1: var t;
          * 2: if (a) {        <- start scope A
          * 3:    if (b)
          * 4:         t = ...
          * 5:    else
          * 6:         t = ...
          * 7: } else {        <- start scope B
          * 8:    if (c)
          * 9:         t = ...
          * A:    else         <- start scope C
          * B:         t = ...
          * C: }
          *
          */

         const prog_scope *parent_ifelse = scope.parent()->in_ifelse_scope();

         if (1 << (next_ifelse_nesting_depth - 1) & if_scope_write_flags) {
            /* We are at the end of scope C and already recorded a write
             * within an IF scope (A), the sibling of the parent ELSE scope B,
             * and it is not yet resolved. Mark that as the last relevant
             * IF scope. Below the write will be resolved for the A/B
             * scope pair.
             */
            current_unpaired_if_write_scope = parent_ifelse;
         } else {
            current_unpaired_if_write_scope = nullptr;
         }
	 /* Promote the first write scope to the enclosing scope because
	  * the current IF/ELSE pair is now irrelevant for the analysis.
	  * This is also required to evaluate the minimum life time for t in
	  * {
	  *    var t;
	  *    if (a)
	  *      t = ...
	  *    else
	  *      t = ...
	  *    x = t;
	  *    ...
	  * }
	  */
	 first_write_scope = scope.parent();

         /* If some parent is IF/ELSE and in a loop then propagate the
          * write to that scope. Otherwise the write is unconditional
          * because it happens in both corresponding IF/ELSE branches
          * in this loop, and hence, record the loop id to signal the
          * resolution.
          */
         if (parent_ifelse && parent_ifelse->is_in_loop()) {
            record_ifelse_write(*parent_ifelse);
         } else {
            conditionality_in_loop_id = scope.innermost_loop()->id();
         }
   } else {
     /* The temporary was not written in the IF branch corresponding
      * to this ELSE branch, hence the write is conditional.
      */
      conditionality_in_loop_id = write_is_conditional;
   }
}

bool temp_comp_access::conditional_ifelse_write_in_loop() const
{
   return conditionality_in_loop_id <= conditionality_unresolved;
}

void temp_comp_access::propagate_live_range_to_dominant_write_scope()
{
   first_write = first_write_scope->begin();
   int lr = first_write_scope->end();

   if (last_read < lr)
      last_read = lr;
}

register_live_range temp_comp_access::get_required_live_range()
{
   bool keep_for_full_loop = false;

   /* This register component is not used at all, or only read,
    * mark it as unused and ignore it when renaming.
    * glsl_to_tgsi_visitor::renumber_registers will take care of
    * eliminating registers that are not written to.
    */
   if (last_write < 0)
      return make_live_range(-1, -1);

   assert(first_write_scope);

   /* Only written to, just make sure the register component is not
    * reused in the range it is used to write to
    */
   if (!last_read_scope)
      return make_live_range(first_write, last_write + 1);

   const prog_scope *enclosing_scope_first_read = first_read_scope;
   const prog_scope *enclosing_scope_first_write = first_write_scope;

   /* We read before writing in a loop
    * hence the value must survive the loops
    */
   if ((first_read <= first_write) &&
       first_read_scope->is_in_loop()) {
      keep_for_full_loop = true;
      enclosing_scope_first_read = first_read_scope->outermost_loop();
   }

   /* A conditional write within a (nested) loop must survive the outermost
    * loop if the last read was not within the same scope.
    */
   const prog_scope *conditional = enclosing_scope_first_write->enclosing_conditional();
   if (conditional && !conditional->contains_range_of(*last_read_scope) &&
       (conditional->is_switchcase_scope_in_loop() ||
        conditional_ifelse_write_in_loop())) {
         keep_for_full_loop = true;
         enclosing_scope_first_write = conditional->outermost_loop();
   }

   /* Evaluate the scope that is shared by all: required first write scope,
    * required first read before write scope, and last read scope.
    */
   const prog_scope *enclosing_scope = enclosing_scope_first_read;
   if (enclosing_scope_first_write->contains_range_of(*enclosing_scope))
      enclosing_scope = enclosing_scope_first_write;

   if (last_read_scope->contains_range_of(*enclosing_scope))
      enclosing_scope = last_read_scope;

   while (!enclosing_scope->contains_range_of(*enclosing_scope_first_write) ||
          !enclosing_scope->contains_range_of(*last_read_scope)) {
      enclosing_scope = enclosing_scope->parent();
      assert(enclosing_scope);
   }

   /* Propagate the last read scope to the target scope */
   while (enclosing_scope->nesting_depth() < last_read_scope->nesting_depth()) {
      /* If the read is in a loop and we have to move up the scope we need to
       * extend the live range to the end of this current loop because at this
       * point we don't know whether the component was written before
       * un-conditionally in the same loop.
       */
      if (last_read_scope->is_loop())
         last_read = last_read_scope->end();

      last_read_scope = last_read_scope->parent();
   }

   /* If the variable has to be kept for the whole loop, and we
    * are currently in a loop, then propagate the live range.
    */
   if (keep_for_full_loop && first_write_scope->is_loop())
      propagate_live_range_to_dominant_write_scope();

   /* Propagate the first_dominant_write scope to the target scope */
   while (enclosing_scope->nesting_depth() < first_write_scope->nesting_depth()) {
      /* Propagate live_range if there was a break in a loop and the write was
       * after the break inside that loop. Note, that this is only needed if
       * we move up in the scopes.
       */
      if (first_write_scope->loop_break_line() < first_write) {
         keep_for_full_loop = true;
	 propagate_live_range_to_dominant_write_scope();
      }

      first_write_scope = first_write_scope->parent();

      /* Propagate live_range if we are now in a loop */
      if (keep_for_full_loop && first_write_scope->is_loop())
	  propagate_live_range_to_dominant_write_scope();
   }

   /* The last write past the last read is dead code, but we have to
    * ensure that the component is not reused too early, hence extend the
    * live_range past the last write.
    */
   if (last_write >= last_read)
      last_read = last_write + 1;

   /* Here we are at the same scope, all is resolved */
   return make_live_range(first_write, last_read);
}

/* Helper class for sorting and searching the registers based
 * on live ranges. */
class register_merge_record {
public:
   int begin;
   int end;
   int reg;
   bool erase;
   bool is_array_elm;

   bool operator < (const register_merge_record& rhs) const {
      return begin < rhs.begin;
   }
};

LiverangeEvaluator::LiverangeEvaluator():
   line(0),
   loop_id(1),
   if_id(1),
   switch_id(0),
   is_at_end(false),
   n_scopes(1),
   cur_scope(nullptr)
{
}

void LiverangeEvaluator::run(const Shader& shader,
                             std::vector<register_live_range>& register_live_ranges)
{
   temp_acc.resize(register_live_ranges.size());
   fill(temp_acc.begin(), temp_acc.end(), temp_access());

   sfn_log << SfnLog::merge << "have " << temp_acc.size() << " temps\n";

   for (const auto& block: shader.m_ir) {
      for (const auto& ir: block) {
         switch (ir->type()) {
         case Instruction::cond_if:
         case Instruction::cond_else:
         case Instruction::loop_begin:
            ++n_scopes;
         default:
            ;
         }
      }
   }

   scopes.reset(new prog_scope_storage(n_scopes));

   cur_scope = scopes->create(nullptr, outer_scope, 0, 0, line);

   line = 0;

   for (auto& v: shader.m_temp) {
      if (v.second->type() == Value::gpr) {
         sfn_log << SfnLog::merge << "Record " << *v.second << "\n";
         const auto& g = static_cast<const GPRValue&>(*v.second);
         if (g.is_input()) {
            sfn_log << SfnLog::merge << "Record INPUT write for "
                    << g << " in " << temp_acc.size() << " temps\n";
            temp_acc[g.sel()].record_write(line, cur_scope, 1 << g.chan(), false);
            temp_acc[g.sel()].record_read(line, cur_scope, 1 << g.chan(), false);
         }
         if (g.keep_alive()) {
            sfn_log << SfnLog::merge << "Record KEEP ALIVE for "
                    << g << " in " << temp_acc.size() << " temps\n";
            temp_acc[g.sel()].record_read(0x7fffff, cur_scope, 1 << g.chan(), false);
         }
      }
   }

   for (const auto& block: shader.m_ir)
      for (const auto& ir: block)  {
         ir->evalue_liveness(*this);
         if (ir->type() != Instruction::alu ||
             static_cast<const AluInstruction&>(*ir).flag(alu_last_instr))
            ++line;
      }

   assert(cur_scope->type() == outer_scope);
   cur_scope->set_end(line);
   is_at_end = true;

   get_required_live_ranges(register_live_ranges);
}


void LiverangeEvaluator::record_read(const Value& src, bool is_array_elm)
{
   sfn_log << SfnLog::merge << "Record read l:" << line << " reg:" << src << "\n";
   if (src.type() == Value::gpr) {
      const GPRValue& v = static_cast<const GPRValue&>(src);
      if (v.chan() < 4)
         temp_acc[v.sel()].record_read(v.keep_alive() ? 0x7fffff: line, cur_scope, 1 << v.chan(), is_array_elm);
      return;
   } else if (src.type() == Value::gpr_array_value) {
      const GPRArrayValue& v = static_cast<const GPRArrayValue&>(src);
      v.record_read(*this);
   } else if (src.type() == Value::kconst) {
      const UniformValue& v = static_cast<const UniformValue&>(src);
      if (v.addr())
         record_read(*v.addr(),is_array_elm);
   }
}

void LiverangeEvaluator::record_write(const Value& src, bool is_array_elm)
{
   sfn_log << SfnLog::merge << "Record write for "
           << src << " in " << temp_acc.size() << " temps\n";

   if (src.type() == Value::gpr) {
      const GPRValue& v = static_cast<const GPRValue&>(src);
      assert(v.sel() < temp_acc.size());
      if (v.chan() < 4)
         temp_acc[v.sel()].record_write(line, cur_scope, 1 << v.chan(), is_array_elm);
      return;
   } else if (src.type() == Value::gpr_array_value) {
      const GPRArrayValue& v = static_cast<const GPRArrayValue&>(src);
      v.record_write(*this);
   } else if (src.type() == Value::kconst) {
      const UniformValue& v = static_cast<const UniformValue&>(src);
      if (v.addr())
         record_write(*v.addr(),is_array_elm);
   }
}

void LiverangeEvaluator::record_read(const GPRVector& src)
{
   for (int i = 0; i < 4; ++i)
      if (src.reg_i(i))
         record_read(*src.reg_i(i));
}

void LiverangeEvaluator::record_write(const GPRVector& dst)
{
   for (int i = 0; i < 4; ++i)
      if (dst.reg_i(i))
         record_write(*dst.reg_i(i));
}

void LiverangeEvaluator::get_required_live_ranges(std::vector<register_live_range>& register_live_ranges)
{
   sfn_log << SfnLog::merge << "== register live ranges ==========\n";
   for(unsigned i = 0; i < register_live_ranges.size(); ++i) {
      sfn_log << SfnLog::merge << setw(4) << i;
      register_live_ranges[i] = temp_acc[i].get_required_live_range();
      sfn_log << SfnLog::merge << ": [" << register_live_ranges[i].begin << ", "
		   << register_live_ranges[i].end << "]\n";
   }
   sfn_log << SfnLog::merge << "==================================\n\n";
}

void LiverangeEvaluator::scope_if()
{
   cur_scope = scopes->create(cur_scope, if_branch, if_id++,
                              cur_scope->nesting_depth() + 1, line + 1);
}

void LiverangeEvaluator::scope_else()
{
   assert(cur_scope->type() == if_branch);
   cur_scope->set_end(line - 1);
   cur_scope = scopes->create(cur_scope->parent(), else_branch,
                              cur_scope->id(), cur_scope->nesting_depth(),
                             line + 1);
}

void LiverangeEvaluator::scope_endif()
{
   cur_scope->set_end(line - 1);
   cur_scope = cur_scope->parent();
   assert(cur_scope);
}

void LiverangeEvaluator::scope_loop_begin()
{
   cur_scope = scopes->create(cur_scope, loop_body, loop_id++,
                              cur_scope->nesting_depth() + 1, line);
}

void LiverangeEvaluator::scope_loop_end()
{
   assert(cur_scope->type() == loop_body);
   cur_scope->set_end(line);
   cur_scope = cur_scope->parent();
   assert(cur_scope);
}

void LiverangeEvaluator::scope_loop_break()
{
   cur_scope->set_loop_break_line(line);
}

/* This functions evaluates the register merges by using a binary
 * search to find suitable merge candidates. */

std::vector<rename_reg_pair>
get_temp_registers_remapping(const std::vector<register_live_range>& live_ranges)
{

   std::vector<rename_reg_pair> result(live_ranges.size(), rename_reg_pair{false, false, 0});
   std::vector<register_merge_record> reg_access;

   for (unsigned i = 0; i < live_ranges.size(); ++i) {
      if (live_ranges[i].begin >= 0) {
         register_merge_record r;
         r.begin = live_ranges[i].begin;
         r.end = live_ranges[i].end;
         r.is_array_elm = live_ranges[i].is_array_elm;
         r.reg = i;
         r.erase = false;
         reg_access.push_back(r);
      }
   }

   std::sort(reg_access.begin(), reg_access.end());

   for (auto& r : reg_access)
      sfn_log << SfnLog::merge << "Use Range " <<r.reg << " ["
              << r.begin << ", "  << r.end << "]\n";

   auto trgt = reg_access.begin();
   auto reg_access_end = reg_access.end();
   auto first_erase = reg_access_end;
   auto search_start = trgt + 1;

   while (trgt != reg_access_end) {
      /* Find the next register that has a live-range starting past the
       * search start and that is not an array element. Array elements can't
       * be moved (Moving the whole array could be an option to be implemented later)*/

      sfn_log << SfnLog::merge << "Next target is "
              << trgt->reg << "[" << trgt->begin << ", "  << trgt->end << "]\n";


      auto src = upper_bound(search_start, reg_access_end, trgt->end,
                             [](int bound, const register_merge_record& m){
                                    return bound < m.begin && !m.is_array_elm;}
                             );

      if (src != reg_access_end) {
         result[src->reg].new_reg = trgt->reg;
         result[src->reg].valid = true;

         sfn_log << SfnLog::merge << "Map "
                 << src->reg << "[" << src->begin << ", "  << src->end << "] to  "
                 << trgt->reg << "[" << trgt->begin << ", "  << trgt->end << ":";
         trgt->end = src->end;
         sfn_log << SfnLog::merge << trgt->end  << "]\n";

         /* Since we only search forward, don't remove the renamed
          * register just now, only mark it. */
         src->erase = true;

         if (first_erase == reg_access_end)
            first_erase = src;

         search_start = src + 1;
      } else {
         /* Moving to the next target register it is time to remove
          * the already merged registers from the search range */
         if (first_erase != reg_access_end) {
	    auto outp = first_erase;
	    auto inp = first_erase + 1;

            while (inp != reg_access_end) {
               if (!inp->erase)
                  *outp++ = *inp;
               ++inp;
            }

            reg_access_end = outp;
            first_erase = reg_access_end;
         }
         ++trgt;
         search_start = trgt + 1;
      }
   }
   return result;
}

} // end ns r600
