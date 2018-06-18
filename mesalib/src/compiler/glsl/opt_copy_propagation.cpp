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
#include "util/set.h"

namespace {

class ir_copy_propagation_visitor : public ir_hierarchical_visitor {
public:
   ir_copy_propagation_visitor()
   {
      progress = false;
      mem_ctx = ralloc_context(0);
      lin_ctx = linear_alloc_parent(mem_ctx, 0);
      acp = _mesa_hash_table_create(mem_ctx, _mesa_hash_pointer,
                                    _mesa_key_pointer_equal);
      kills = _mesa_set_create(mem_ctx, _mesa_hash_pointer,
                               _mesa_key_pointer_equal);
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
    * Set of ir_variables: Whose values were killed in this block.
    */
   set *kills;

   bool progress;

   bool killed_all;

   void *mem_ctx;
   void *lin_ctx;
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
   set *orig_kills = this->kills;
   bool orig_killed_all = this->killed_all;

   acp = _mesa_hash_table_create(NULL, _mesa_hash_pointer,
                                 _mesa_key_pointer_equal);
   kills = _mesa_set_create(NULL, _mesa_hash_pointer,
                            _mesa_key_pointer_equal);
   this->killed_all = false;

   visit_list_elements(this, &ir->body);

   _mesa_hash_table_destroy(acp, NULL);
   _mesa_set_destroy(kills, NULL);

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

   /* Since this pass can run when unlinked, we don't (necessarily) know
    * the side effects of calls.  (When linked, most calls are inlined
    * anyway, so it doesn't matter much.)
    *
    * One place where this does matter is IR intrinsics.  They're never
    * inlined.  We also know what they do - while some have side effects
    * (such as image writes), none edit random global variables.  So we
    * can assume they're side-effect free (other than the return value
    * and out parameters).
    */
   if (!ir->callee->is_intrinsic()) {
      _mesa_hash_table_clear(acp, NULL);
      this->killed_all = true;
   } else {
      if (ir->return_deref)
         kill(ir->return_deref->var);

      foreach_two_lists(formal_node, &ir->callee->parameters,
                        actual_node, &ir->actual_parameters) {
         ir_variable *sig_param = (ir_variable *) formal_node;
         if (sig_param->data.mode == ir_var_function_out ||
             sig_param->data.mode == ir_var_function_inout) {
            ir_rvalue *ir = (ir_rvalue *) actual_node;
            ir_variable *var = ir->variable_referenced();
            kill(var);
         }
      }
   }

   return visit_continue_with_parent;
}

void
ir_copy_propagation_visitor::handle_if_block(exec_list *instructions)
{
   hash_table *orig_acp = this->acp;
   set *orig_kills = this->kills;
   bool orig_killed_all = this->killed_all;

   kills = _mesa_set_create(NULL, _mesa_hash_pointer,
                            _mesa_key_pointer_equal);
   this->killed_all = false;

   /* Populate the initial acp with a copy of the original */
   acp = _mesa_hash_table_clone(orig_acp, NULL);

   visit_list_elements(this, instructions);

   if (this->killed_all) {
      _mesa_hash_table_clear(orig_acp, NULL);
   }

   set *new_kills = this->kills;
   this->kills = orig_kills;
   _mesa_hash_table_destroy(acp, NULL);
   this->acp = orig_acp;
   this->killed_all = this->killed_all || orig_killed_all;

   struct set_entry *s_entry;
   set_foreach(new_kills, s_entry) {
      kill((ir_variable *) s_entry->key);
   }

   _mesa_set_destroy(new_kills, NULL);
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
   set *orig_kills = this->kills;
   bool orig_killed_all = this->killed_all;

   kills = _mesa_set_create(NULL, _mesa_hash_pointer,
                            _mesa_key_pointer_equal);
   this->killed_all = false;

   if (keep_acp) {
      acp = _mesa_hash_table_clone(orig_acp, NULL);
   } else {
      acp = _mesa_hash_table_create(NULL, _mesa_hash_pointer,
                                    _mesa_key_pointer_equal);
   }

   visit_list_elements(this, &ir->body_instructions);

   if (this->killed_all) {
      _mesa_hash_table_clear(orig_acp, NULL);
   }

   set *new_kills = this->kills;
   this->kills = orig_kills;
   _mesa_hash_table_destroy(acp, NULL);
   this->acp = orig_acp;
   this->killed_all = this->killed_all || orig_killed_all;

   struct set_entry *entry;
   set_foreach(new_kills, entry) {
      kill((ir_variable *) entry->key);
   }

   _mesa_set_destroy(new_kills, NULL);
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

   /* Add the LHS variable to the set of killed variables in this block. */
   _mesa_set_add(kills, var);
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

   /* Don't try to remove a dumb assignment of a variable to itself.  Removing
    * it now would mess up the loop iteration calling us.
    */
   if (lhs_var != NULL && rhs_var != NULL && lhs_var != rhs_var) {
      if (lhs_var->data.mode != ir_var_shader_storage &&
          lhs_var->data.mode != ir_var_shader_shared &&
          rhs_var->data.mode != ir_var_shader_storage &&
          rhs_var->data.mode != ir_var_shader_shared &&
          lhs_var->data.precise == rhs_var->data.precise) {
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
