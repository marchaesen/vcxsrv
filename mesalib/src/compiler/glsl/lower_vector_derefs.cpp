/*
 * Copyright © 2013 Intel Corporation
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
#include "ir.h"
#include "ir_builder.h"
#include "ir_rvalue_visitor.h"
#include "ir_optimization.h"
#include "main/mtypes.h"

using namespace ir_builder;

namespace {

class vector_deref_visitor : public ir_rvalue_enter_visitor {
public:
   vector_deref_visitor()
      : progress(false)
   {
   }

   virtual ~vector_deref_visitor()
   {
   }

   virtual void handle_rvalue(ir_rvalue **rv);
   virtual ir_visitor_status visit_enter(ir_assignment *ir);

   bool progress;
};

} /* anonymous namespace */

ir_visitor_status
vector_deref_visitor::visit_enter(ir_assignment *ir)
{
   if (!ir->lhs || ir->lhs->ir_type != ir_type_dereference_array)
      return ir_rvalue_enter_visitor::visit_enter(ir);

   ir_dereference_array *const deref = (ir_dereference_array *) ir->lhs;
   if (!deref->array->type->is_vector())
      return ir_rvalue_enter_visitor::visit_enter(ir);

   ir_dereference *const new_lhs = (ir_dereference *) deref->array;
   ir->set_lhs(new_lhs);

   void *mem_ctx = ralloc_parent(ir);
   ir_constant *old_index_constant =
      deref->array_index->constant_expression_value(mem_ctx);
   if (!old_index_constant) {
      ir->rhs = new(mem_ctx) ir_expression(ir_triop_vector_insert,
                                           new_lhs->type,
                                           new_lhs->clone(mem_ctx, NULL),
                                           ir->rhs,
                                           deref->array_index);
      ir->write_mask = (1 << new_lhs->type->vector_elements) - 1;
   } else {
      ir->write_mask = 1 << old_index_constant->get_int_component(0);
   }

   return ir_rvalue_enter_visitor::visit_enter(ir);
}

void
vector_deref_visitor::handle_rvalue(ir_rvalue **rv)
{
   if (*rv == NULL || (*rv)->ir_type != ir_type_dereference_array)
      return;

   ir_dereference_array *const deref = (ir_dereference_array *) *rv;
   if (!deref->array->type->is_vector())
      return;

   void *mem_ctx = ralloc_parent(deref);
   *rv = new(mem_ctx) ir_expression(ir_binop_vector_extract,
                                    deref->array,
                                    deref->array_index);
}

bool
lower_vector_derefs(gl_linked_shader *shader)
{
   vector_deref_visitor v;

   visit_list_elements(&v, shader->ir);

   return v.progress;
}
