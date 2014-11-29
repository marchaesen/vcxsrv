/*
 * Copyright © 2014 Intel Corporation
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
 * \file lower_const_arrays_to_uniforms.cpp
 *
 * Lower constant arrays to uniform arrays.
 *
 * Some driver backends (such as i965 and nouveau) don't handle constant arrays
 * gracefully, instead treating them as ordinary writable temporary arrays.
 * Since arrays can be large, this often means spilling them to scratch memory,
 * which usually involves a large number of instructions.
 *
 * This must be called prior to link_set_uniform_initializers(); we need the
 * linker to process our new uniform's constant initializer.
 *
 * This should be called after optimizations, since those can result in
 * splitting and removing arrays that are indexed by constant expressions.
 */
#include "ir.h"
#include "ir_visitor.h"
#include "ir_rvalue_visitor.h"
#include "glsl_types.h"

namespace {
class lower_const_array_visitor : public ir_rvalue_visitor {
public:
   lower_const_array_visitor(exec_list *insts)
   {
      instructions = insts;
      progress = false;
      index = 0;
   }

   bool run()
   {
      visit_list_elements(this, instructions);
      return progress;
   }

   void handle_rvalue(ir_rvalue **rvalue);

private:
   exec_list *instructions;
   bool progress;
   unsigned index;
};

void
lower_const_array_visitor::handle_rvalue(ir_rvalue **rvalue)
{
   if (!*rvalue)
      return;

   ir_dereference_array *dra = (*rvalue)->as_dereference_array();
   if (!dra)
      return;

   ir_constant *con = dra->array->as_constant();
   if (!con || !con->type->is_array())
      return;

   void *mem_ctx = ralloc_parent(con);

   char *uniform_name = ralloc_asprintf(mem_ctx, "constarray__%d", index++);

   ir_variable *uni =
      new(mem_ctx) ir_variable(con->type, uniform_name, ir_var_uniform);
   uni->constant_initializer = con;
   uni->constant_value = con;
   uni->data.has_initializer = true;
   uni->data.how_declared = ir_var_hidden;
   uni->data.read_only = true;
   /* Assume the whole thing is accessed. */
   uni->data.max_array_access = uni->type->length - 1;
   instructions->push_head(uni);

   ir_dereference_variable *varref = new(mem_ctx) ir_dereference_variable(uni);
   *rvalue = new(mem_ctx) ir_dereference_array(varref, dra->array_index);

   progress = true;
}

} /* anonymous namespace */

bool
lower_const_arrays_to_uniforms(exec_list *instructions)
{
   lower_const_array_visitor v(instructions);
   return v.run();
}
