/*
 * Copyright © 2010 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/**
 * \file opt_copy_propagation.cpp
 *
 * Moves usage of recently-copied variables to the previous copy of
 * the variable.
 *
 * This should reduce the number of MOV instructions in the generated
 * programs unless copy propagation is also done on the LIR, and may
 * help anyway by triggering other optimizations that live in the HIR.
 */

#include "ir.h"
#include "ir_visitor.h"
#include "ir_basic_block.h"
#include "ir_optimization.h"
#include "compiler/glsl_types.h"
#include "util/hash_table.h"

namespace {

class kill_entry : public exec_node
{
public:
   kill_entry(ir_variable *var)
   {
      assert(var);
      this->var = var;
   }

   ir_variable *var;
};

class ir_copy_propagation_visitor : public ir_hierarchical_visitor {
public:
   ir_copy_propagation_visitor()
   {
      progress = false;
      mem_ctx = ralloc_context(0);
      acp = _mesa_hash_table_create(mem_ctx, _mesa_hash_pointer,
                                    _mesa_key_pointer_equal);
      this->kills = new(mem_ctx) exec_list;
      killed_all = false;
   }
   ~ir_copy_propagation_visitor()
   {
      ralloc_free(mem_ctx);
   }

   virtual ir_visitor_status visit(class ir_dereference_variable *);
   void handle_loop(class ir_loop *, bool keep_acp);
   virtual ir_visitor_status visit_enter(class ir_loop *);
   virtual ir_visitor_status visit_enter(class ir_function_signature *);
   virtual ir_visitor_status visit_enter(class ir_function *);
   virtual ir_visitor_status visit_leave(class ir_assignment *);
   virtual ir_visitor_status visit_enter(class ir_call *);
   virtual ir_visitor_status visit_enter(class ir_if *);

   void add_copy(ir_assignment *ir);
   void kill(ir_variable *ir);
   void handle_if_block(exec_list *instructions);

   /** Hash of lhs->rhs: The available copies to propagate */
   hash_table *acp;
   /**
    * List of kill_entry: The variables whose values were killed in this
    * block.
    */
   exec_list *kills;

   bool progress;

   bool killed_all;

   void *mem_ctx;
};

} /* unnamed namespace */

ir_visitor_status
ir_copy_propagation_visitor::visit_enter(ir_function_signature *ir)
{
   /* Treat entry into a function signature as a completely separate
    * block.  Any instructions at global scope will be shuffled into
    * main() at link time, so they're irrelevant to us.
    */
   hash_table *orig_acp = this->acp;
   exec_list *orig_kills = this->kills;
   bool orig_killed_all = this->killed_all;

   acp = _mesa_hash_table_create(NULL, _mesa_hash_pointer,
                                 _mesa_key_pointer_equal);
   this->kills = new(mem_ctx) exec_list;
   this->killed_all = false;

   visit_list_elements(this, &ir->body);

   _mesa_hash_table_destroy(acp, NULL);
   ralloc_free(this->kills);

   this->kills = orig_kills;
   this->acp = orig_acp;
   this->killed_all = orig_killed_all;

   return visit_continue_with_parent;
}

ir_visitor_status
ir_copy_propagation_visitor::visit_leave(ir_assignment *ir)
{
   kill(ir->lhs->variable_referenced());

   add_copy(ir);

   return visit_continue;
}

ir_visitor_status
ir_copy_propagation_visitor::visit_enter(ir_function *ir)
{
   (void) ir;
   return visit_continue;
}

/**
 * Replaces dereferences of ACP RHS variables with ACP LHS variables.
 *
 * This is where the actual copy propagation occurs.  Note that the
 * rewriting of ir_dereference means that the ir_dereference instance
 * must not be shared by multiple IR operations!
 */
ir_visitor_status
ir_copy_propagation_visitor::visit(ir_dereference_variable *ir)
{
   if (this->in_assignee)
      return visit_continue;

   struct hash_entry *entry = _mesa_hash_table_search(acp, ir->var);
   if (entry) {
      ir->var = (ir_variable *) entry->data;
      progress = true;
   }

   return visit_continue;
}


ir_visitor_status
ir_copy_propagation_visitor::visit_enter(ir_call *ir)
{
   /* Do copy propagation on call parameters, but skip any out params */
   foreach_two_lists(formal_node, &ir->callee->parameters,
                     actual_node, &ir->actual_parameters) {
      ir_variable *sig_param = (ir_variable *) formal_node;
      ir_rvalue *ir = (ir_rvalue *) actual_node;
      if (sig_param->data.mode != ir_var_function_out
          && sig_param->data.mode != ir_var_function_inout) {
         ir->accept(this);
      }
   }

   /* Since we're unlinked, we don't (necessarily) know the side effects of
    * this call.  So kill all copies.
    */
   _mesa_hash_table_clear(acp, NULL);
   this->killed_all = true;

   return visit_continue_with_parent;
}

void
ir_copy_propagation_visitor::handle_if_block(exec_list *instructions)
{
   hash_table *orig_acp = this->acp;
   exec_list *orig_kills = this->kills;
   bool orig_killed_all = this->killed_all;

   acp = _mesa_hash_table_create(NULL, _mesa_hash_pointer,
                                 _mesa_key_pointer_equal);
   this->kills = new(mem_ctx) exec_list;
   this->killed_all = false;

   /* Populate the initial acp with a copy of the original */
   struct hash_entry *entry;
   hash_table_foreach(orig_acp, entry) {
      _mesa_hash_table_insert(acp, entry->key, entry->data);
   }

   visit_list_elements(this, instructions);

   if (this->killed_all) {
      _mesa_hash_table_clear(orig_acp, NULL);
   }

   exec_list *new_kills = this->kills;
   this->kills = orig_kills;
   _mesa_hash_table_destroy(acp, NULL);
   this->acp = orig_acp;
   this->killed_all = this->killed_all || orig_killed_all;

   foreach_in_list(kill_entry, k, new_kills) {
      kill(k->var);
   }

   ralloc_free(new_kills);
}

ir_visitor_status
ir_copy_propagation_visitor::visit_enter(ir_if *ir)
{
   ir->condition->accept(this);

   handle_if_block(&ir->then_instructions);
   handle_if_block(&ir->else_instructions);

   /* handle_if_block() already descended into the children. */
   return visit_continue_with_parent;
}

void
ir_copy_propagation_visitor::handle_loop(ir_loop *ir, bool keep_acp)
{
   hash_table *orig_acp = this->acp;
   exec_list *orig_kills = this->kills;
   bool orig_killed_all = this->killed_all;

   acp = _mesa_hash_table_create(NULL, _mesa_hash_pointer,
                                 _mesa_key_pointer_equal);
   this->kills = new(mem_ctx) exec_list;
   this->killed_all = false;

   if (keep_acp) {
      struct hash_entry *entry;
      hash_table_foreach(orig_acp, entry) {
         _mesa_hash_table_insert(acp, entry->key, entry->data);
      }
   }

   visit_list_elements(this, &ir->body_instructions);

   if (this->killed_all) {
      _mesa_hash_table_clear(orig_acp, NULL);
   }

   exec_list *new_kills = this->kills;
   this->kills = orig_kills;
   _mesa_hash_table_destroy(acp, NULL);
   this->acp = orig_acp;
   this->killed_all = this->killed_all || orig_killed_all;

   foreach_in_list(kill_entry, k, new_kills) {
      kill(k->var);
   }

   ralloc_free(new_kills);
}

ir_visitor_status
ir_copy_propagation_visitor::visit_enter(ir_loop *ir)
{
   /* Make a conservative first pass over the loop with an empty ACP set.
    * This also removes any killed entries from the original ACP set.
    */
   handle_loop(ir, false);

   /* Then, run it again with the real ACP set, minus any killed entries.
    * This takes care of propagating values from before the loop into it.
    */
   handle_loop(ir, true);

   /* already descended into the children. */
   return visit_continue_with_parent;
}

void
ir_copy_propagation_visitor::kill(ir_variable *var)
{
   assert(var != NULL);

   /* Remove any entries currently in the ACP for this kill. */
   struct hash_entry *entry = _mesa_hash_table_search(acp, var);
   if (entry) {
      _mesa_hash_table_remove(acp, entry);
   }

   hash_table_foreach(acp, entry) {
      if (var == (ir_variable *) entry->data) {
         _mesa_hash_table_remove(acp, entry);
      }
   }

   /* Add the LHS variable to the list of killed variables in this block.
    */
   this->kills->push_tail(new(this->kills) kill_entry(var));
}

/**
 * Adds an entry to the available copy list if it's a plain assignment
 * of a variable to a variable.
 */
void
ir_copy_propagation_visitor::add_copy(ir_assignment *ir)
{
   if (ir->condition)
      return;

   ir_variable *lhs_var = ir->whole_variable_written();
   ir_variable *rhs_var = ir->rhs->whole_variable_referenced();

   if ((lhs_var != NULL) && (rhs_var != NULL)) {
      if (lhs_var == rhs_var) {
	 /* This is a dumb assignment, but we've conveniently noticed
	  * it here.  Removing it now would mess up the loop iteration
	  * calling us.  Just flag it to not execute, and someone else
	  * will clean up the mess.
	  */
	 ir->condition = new(ralloc_parent(ir)) ir_constant(false);
	 this->progress = true;
      } else if (lhs_var->data.mode != ir_var_shader_storage &&
                 lhs_var->data.mode != ir_var_shader_shared &&
                 lhs_var->data.precise == rhs_var->data.precise) {
         assert(lhs_var);
         assert(rhs_var);
         _mesa_hash_table_insert(acp, lhs_var, rhs_var);
      }
   }
}

/**
 * Does a copy propagation pass on the code present in the instruction stream.
 */
bool
do_copy_propagation(exec_list *instructions)
{
   ir_copy_propagation_visitor v;

   visit_list_elements(&v, instructions);

   return v.progress;
}
