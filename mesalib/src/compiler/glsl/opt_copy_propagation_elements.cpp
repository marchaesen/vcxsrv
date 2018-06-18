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
 * \file opt_copy_propagation_elements.cpp
 *
 * Replaces usage of recently-copied components of variables with the
 * previous copy of the variable.
 *
 * This pass can be compared with opt_copy_propagation, which operands
 * on arbitrary whole-variable copies.  However, in order to handle
 * the copy propagation of swizzled variables or writemasked writes,
 * we want to track things on a channel-wise basis.  I found that
 * trying to mix the swizzled/writemasked support here with the
 * whole-variable stuff in opt_copy_propagation.cpp just made a mess,
 * so this is separate despite the ACP handling being somewhat
 * similar.
 *
 * This should reduce the number of MOV instructions in the generated
 * programs unless copy propagation is also done on the LIR, and may
 * help anyway by triggering other optimizations that live in the HIR.
 */

#include "ir.h"
#include "ir_rvalue_visitor.h"
#include "ir_basic_block.h"
#include "ir_optimization.h"
#include "compiler/glsl_types.h"
#include "util/hash_table.h"

static bool debug = false;

namespace {

class acp_entry;

/* Class that refers to acp_entry in another exec_list. Used
 * when making removals based on rhs.
 */
class acp_ref : public exec_node
{
public:
   acp_ref(acp_entry *e)
   {
      entry = e;
   }
   acp_entry *entry;
};

class acp_entry : public exec_node
{
public:
   /* override operator new from exec_node */
   DECLARE_LINEAR_ZALLOC_CXX_OPERATORS(acp_entry)

   acp_entry(ir_variable *lhs, ir_variable *rhs, int write_mask, int swizzle[4])
      : rhs_node(this)
   {
      this->lhs = lhs;
      this->rhs = rhs;
      this->write_mask = write_mask;
      memcpy(this->swizzle, swizzle, sizeof(this->swizzle));
   }

   ir_variable *lhs;
   ir_variable *rhs;
   unsigned int write_mask;
   int swizzle[4];
   acp_ref rhs_node;
};


class kill_entry : public exec_node
{
public:
   /* override operator new from exec_node */
   DECLARE_LINEAR_ZALLOC_CXX_OPERATORS(kill_entry)

   kill_entry(ir_variable *var, int write_mask)
   {
      this->var = var;
      this->write_mask = write_mask;
   }

   ir_variable *var;
   unsigned int write_mask;
};

class ir_copy_propagation_elements_visitor : public ir_rvalue_visitor {
public:
   ir_copy_propagation_elements_visitor()
   {
      this->progress = false;
      this->killed_all = false;
      this->mem_ctx = ralloc_context(NULL);
      this->lin_ctx = linear_alloc_parent(this->mem_ctx, 0);
      this->shader_mem_ctx = NULL;
      this->kills = new(mem_ctx) exec_list;

      create_acp();
   }
   ~ir_copy_propagation_elements_visitor()
   {
      ralloc_free(mem_ctx);
   }

   void clone_acp(hash_table *lhs, hash_table *rhs)
   {
      lhs_ht = _mesa_hash_table_clone(lhs, mem_ctx);
      rhs_ht = _mesa_hash_table_clone(rhs, mem_ctx);
   }

   void create_acp()
   {
      lhs_ht = _mesa_hash_table_create(mem_ctx, _mesa_hash_pointer,
                                       _mesa_key_pointer_equal);
      rhs_ht = _mesa_hash_table_create(mem_ctx, _mesa_hash_pointer,
                                       _mesa_key_pointer_equal);
   }

   void destroy_acp()
   {
      _mesa_hash_table_destroy(lhs_ht, NULL);
      _mesa_hash_table_destroy(rhs_ht, NULL);
   }

   void handle_loop(ir_loop *, bool keep_acp);
   virtual ir_visitor_status visit_enter(class ir_loop *);
   virtual ir_visitor_status visit_enter(class ir_function_signature *);
   virtual ir_visitor_status visit_leave(class ir_assignment *);
   virtual ir_visitor_status visit_enter(class ir_call *);
   virtual ir_visitor_status visit_enter(class ir_if *);
   virtual ir_visitor_status visit_leave(class ir_swizzle *);

   void handle_rvalue(ir_rvalue **rvalue);

   void add_copy(ir_assignment *ir);
   void kill(kill_entry *k);
   void handle_if_block(exec_list *instructions);

   /** Hash of acp_entry: The available copies to propagate */
   hash_table *lhs_ht;
   hash_table *rhs_ht;

   /**
    * List of kill_entry: The variables whose values were killed in this
    * block.
    */
   exec_list *kills;

   bool progress;

   bool killed_all;

   /* Context for our local data structures. */
   void *mem_ctx;
   void *lin_ctx;
   /* Context for allocating new shader nodes. */
   void *shader_mem_ctx;
};

} /* unnamed namespace */

ir_visitor_status
ir_copy_propagation_elements_visitor::visit_enter(ir_function_signature *ir)
{
   /* Treat entry into a function signature as a completely separate
    * block.  Any instructions at global scope will be shuffled into
    * main() at link time, so they're irrelevant to us.
    */
   exec_list *orig_kills = this->kills;
   bool orig_killed_all = this->killed_all;

   hash_table *orig_lhs_ht = lhs_ht;
   hash_table *orig_rhs_ht = rhs_ht;

   this->kills = new(mem_ctx) exec_list;
   this->killed_all = false;

   create_acp();

   visit_list_elements(this, &ir->body);

   ralloc_free(this->kills);

   destroy_acp();

   this->kills = orig_kills;
   this->killed_all = orig_killed_all;

   lhs_ht = orig_lhs_ht;
   rhs_ht = orig_rhs_ht;

   return visit_continue_with_parent;
}

ir_visitor_status
ir_copy_propagation_elements_visitor::visit_leave(ir_assignment *ir)
{
   ir_dereference_variable *lhs = ir->lhs->as_dereference_variable();
   ir_variable *var = ir->lhs->variable_referenced();

   if (var->type->is_scalar() || var->type->is_vector()) {
      kill_entry *k;

      if (lhs)
	 k = new(this->lin_ctx) kill_entry(var, ir->write_mask);
      else
	 k = new(this->lin_ctx) kill_entry(var, ~0);

      kill(k);
   }

   add_copy(ir);

   return visit_continue;
}

ir_visitor_status
ir_copy_propagation_elements_visitor::visit_leave(ir_swizzle *)
{
   /* Don't visit the values of swizzles since they are handled while
    * visiting the swizzle itself.
    */
   return visit_continue;
}

/**
 * Replaces dereferences of ACP RHS variables with ACP LHS variables.
 *
 * This is where the actual copy propagation occurs.  Note that the
 * rewriting of ir_dereference means that the ir_dereference instance
 * must not be shared by multiple IR operations!
 */
void
ir_copy_propagation_elements_visitor::handle_rvalue(ir_rvalue **ir)
{
   int swizzle_chan[4];
   ir_dereference_variable *deref_var;
   ir_variable *source[4] = {NULL, NULL, NULL, NULL};
   int source_chan[4] = {0, 0, 0, 0};
   int chans;
   bool noop_swizzle = true;

   if (!*ir)
      return;

   ir_swizzle *swizzle = (*ir)->as_swizzle();
   if (swizzle) {
      deref_var = swizzle->val->as_dereference_variable();
      if (!deref_var)
	 return;

      swizzle_chan[0] = swizzle->mask.x;
      swizzle_chan[1] = swizzle->mask.y;
      swizzle_chan[2] = swizzle->mask.z;
      swizzle_chan[3] = swizzle->mask.w;
      chans = swizzle->type->vector_elements;
   } else {
      deref_var = (*ir)->as_dereference_variable();
      if (!deref_var)
	 return;

      swizzle_chan[0] = 0;
      swizzle_chan[1] = 1;
      swizzle_chan[2] = 2;
      swizzle_chan[3] = 3;
      chans = deref_var->type->vector_elements;
   }

   if (this->in_assignee)
      return;

   ir_variable *var = deref_var->var;

   /* Try to find ACP entries covering swizzle_chan[], hoping they're
    * the same source variable.
    */
   hash_entry *ht_entry = _mesa_hash_table_search(lhs_ht, var);
   if (ht_entry) {
      exec_list *ht_list = (exec_list *) ht_entry->data;
      foreach_in_list(acp_entry, entry, ht_list) {
         for (int c = 0; c < chans; c++) {
            if (entry->write_mask & (1 << swizzle_chan[c])) {
               source[c] = entry->rhs;
               source_chan[c] = entry->swizzle[swizzle_chan[c]];

               if (source_chan[c] != swizzle_chan[c])
                  noop_swizzle = false;
            }
         }
      }
   }

   /* Make sure all channels are copying from the same source variable. */
   if (!source[0])
      return;
   for (int c = 1; c < chans; c++) {
      if (source[c] != source[0])
	 return;
   }

   if (!shader_mem_ctx)
      shader_mem_ctx = ralloc_parent(deref_var);

   /* Don't pointlessly replace the rvalue with itself (or a noop swizzle
    * of itself, which would just be deleted by opt_noop_swizzle).
    */
   if (source[0] == var && noop_swizzle)
      return;

   if (debug) {
      printf("Copy propagation from:\n");
      (*ir)->print();
   }

   deref_var = new(shader_mem_ctx) ir_dereference_variable(source[0]);
   *ir = new(shader_mem_ctx) ir_swizzle(deref_var,
					source_chan[0],
					source_chan[1],
					source_chan[2],
					source_chan[3],
					chans);
   progress = true;

   if (debug) {
      printf("to:\n");
      (*ir)->print();
      printf("\n");
   }
}


ir_visitor_status
ir_copy_propagation_elements_visitor::visit_enter(ir_call *ir)
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
   _mesa_hash_table_clear(lhs_ht, NULL);
   _mesa_hash_table_clear(rhs_ht, NULL);

   this->killed_all = true;

   return visit_continue_with_parent;
}

void
ir_copy_propagation_elements_visitor::handle_if_block(exec_list *instructions)
{
   exec_list *orig_kills = this->kills;
   bool orig_killed_all = this->killed_all;

   hash_table *orig_lhs_ht = lhs_ht;
   hash_table *orig_rhs_ht = rhs_ht;

   this->kills = new(mem_ctx) exec_list;
   this->killed_all = false;

   /* Populate the initial acp with a copy of the original */
   clone_acp(orig_lhs_ht, orig_rhs_ht);

   visit_list_elements(this, instructions);

   if (this->killed_all) {
      _mesa_hash_table_clear(orig_lhs_ht, NULL);
      _mesa_hash_table_clear(orig_rhs_ht, NULL);
   }

   exec_list *new_kills = this->kills;
   this->kills = orig_kills;
   this->killed_all = this->killed_all || orig_killed_all;

   destroy_acp();

   lhs_ht = orig_lhs_ht;
   rhs_ht = orig_rhs_ht;

   /* Move the new kills into the parent block's list, removing them
    * from the parent's ACP list in the process.
    */
   foreach_in_list_safe(kill_entry, k, new_kills) {
      kill(k);
   }

   ralloc_free(new_kills);
}

ir_visitor_status
ir_copy_propagation_elements_visitor::visit_enter(ir_if *ir)
{
   ir->condition->accept(this);

   handle_if_block(&ir->then_instructions);
   handle_if_block(&ir->else_instructions);

   /* handle_if_block() already descended into the children. */
   return visit_continue_with_parent;
}

void
ir_copy_propagation_elements_visitor::handle_loop(ir_loop *ir, bool keep_acp)
{
   exec_list *orig_kills = this->kills;
   bool orig_killed_all = this->killed_all;

   hash_table *orig_lhs_ht = lhs_ht;
   hash_table *orig_rhs_ht = rhs_ht;

   /* FINISHME: For now, the initial acp for loops is totally empty.
    * We could go through once, then go through again with the acp
    * cloned minus the killed entries after the first run through.
    */
   this->kills = new(mem_ctx) exec_list;
   this->killed_all = false;

   if (keep_acp) {
      /* Populate the initial acp with a copy of the original */
      clone_acp(orig_lhs_ht, orig_rhs_ht);
   } else {
      create_acp();
   }

   visit_list_elements(this, &ir->body_instructions);

   if (this->killed_all) {
      _mesa_hash_table_clear(orig_lhs_ht, NULL);
      _mesa_hash_table_clear(orig_rhs_ht, NULL);
   }

   exec_list *new_kills = this->kills;
   this->kills = orig_kills;
   this->killed_all = this->killed_all || orig_killed_all;

   destroy_acp();

   lhs_ht = orig_lhs_ht;
   rhs_ht = orig_rhs_ht;

   foreach_in_list_safe(kill_entry, k, new_kills) {
      kill(k);
   }

   ralloc_free(new_kills);
}

ir_visitor_status
ir_copy_propagation_elements_visitor::visit_enter(ir_loop *ir)
{
   handle_loop(ir, false);
   handle_loop(ir, true);

   /* already descended into the children. */
   return visit_continue_with_parent;
}

/* Remove any entries currently in the ACP for this kill. */
void
ir_copy_propagation_elements_visitor::kill(kill_entry *k)
{
   /* removal of lhs entries */
   hash_entry *ht_entry = _mesa_hash_table_search(lhs_ht, k->var);
   if (ht_entry) {
      exec_list *lhs_list = (exec_list *) ht_entry->data;
      foreach_in_list_safe(acp_entry, entry, lhs_list) {
         entry->write_mask = entry->write_mask & ~k->write_mask;
         if (entry->write_mask == 0) {
            entry->remove();
            continue;
         }
      }
   }

   /* removal of rhs entries */
   ht_entry = _mesa_hash_table_search(rhs_ht, k->var);
   if (ht_entry) {
      exec_list *rhs_list = (exec_list *) ht_entry->data;
      acp_ref *ref;

      while ((ref = (acp_ref *) rhs_list->pop_head()) != NULL) {
         acp_entry *entry = ref->entry;

         /* If entry is still in a list (not already removed by lhs entry
          * removal above), remove it.
          */
         if (entry->prev || entry->next)
            entry->remove();
      }
   }

   /* If we were on a list, remove ourselves before inserting */
   if (k->next)
      k->remove();

   this->kills->push_tail(k);
}

/**
 * Adds directly-copied channels between vector variables to the available
 * copy propagation list.
 */
void
ir_copy_propagation_elements_visitor::add_copy(ir_assignment *ir)
{
   acp_entry *entry;
   int orig_swizzle[4] = {0, 1, 2, 3};
   int swizzle[4];

   if (ir->condition)
      return;

   ir_dereference_variable *lhs = ir->lhs->as_dereference_variable();
   if (!lhs || !(lhs->type->is_scalar() || lhs->type->is_vector()))
      return;

   if (lhs->var->data.mode == ir_var_shader_storage ||
       lhs->var->data.mode == ir_var_shader_shared)
      return;

   ir_dereference_variable *rhs = ir->rhs->as_dereference_variable();
   if (!rhs) {
      ir_swizzle *swiz = ir->rhs->as_swizzle();
      if (!swiz)
	 return;

      rhs = swiz->val->as_dereference_variable();
      if (!rhs)
	 return;

      orig_swizzle[0] = swiz->mask.x;
      orig_swizzle[1] = swiz->mask.y;
      orig_swizzle[2] = swiz->mask.z;
      orig_swizzle[3] = swiz->mask.w;
   }

   if (rhs->var->data.mode == ir_var_shader_storage ||
       rhs->var->data.mode == ir_var_shader_shared)
      return;

   /* Move the swizzle channels out to the positions they match in the
    * destination.  We don't want to have to rewrite the swizzle[]
    * array every time we clear a bit of the write_mask.
    */
   int j = 0;
   for (int i = 0; i < 4; i++) {
      if (ir->write_mask & (1 << i))
	 swizzle[i] = orig_swizzle[j++];
   }

   int write_mask = ir->write_mask;
   if (lhs->var == rhs->var) {
      /* If this is a copy from the variable to itself, then we need
       * to be sure not to include the updated channels from this
       * instruction in the set of new source channels to be
       * copy-propagated from.
       */
      for (int i = 0; i < 4; i++) {
	 if (ir->write_mask & (1 << orig_swizzle[i]))
	    write_mask &= ~(1 << i);
      }
   }

   if (lhs->var->data.precise != rhs->var->data.precise)
      return;

   entry = new(this->lin_ctx) acp_entry(lhs->var, rhs->var, write_mask,
					swizzle);

   /* lhs hash, hash of lhs -> acp_entry lists */
   hash_entry *ht_entry = _mesa_hash_table_search(lhs_ht, lhs->var);
   if (ht_entry) {
      exec_list *lhs_list = (exec_list *) ht_entry->data;
      lhs_list->push_tail(entry);
   } else {
      exec_list *lhs_list = new(mem_ctx) exec_list;
      lhs_list->push_tail(entry);
      _mesa_hash_table_insert(lhs_ht, lhs->var, lhs_list);
   }

   /* rhs hash, hash of rhs -> acp_entry pointers to lhs lists */
   ht_entry = _mesa_hash_table_search(rhs_ht, rhs->var);
   if (ht_entry) {
      exec_list *rhs_list = (exec_list *) ht_entry->data;
      rhs_list->push_tail(&entry->rhs_node);
   } else {
      exec_list *rhs_list = new(mem_ctx) exec_list;
      rhs_list->push_tail(&entry->rhs_node);
      _mesa_hash_table_insert(rhs_ht, rhs->var, rhs_list);
   }
}

bool
do_copy_propagation_elements(exec_list *instructions)
{
   ir_copy_propagation_elements_visitor v;

   visit_list_elements(&v, instructions);

   return v.progress;
}
