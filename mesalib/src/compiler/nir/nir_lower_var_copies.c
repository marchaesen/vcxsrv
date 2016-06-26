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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Jason Ekstrand (jason@jlekstrand.net)
 *
 */

#include "nir.h"
#include "compiler/nir_types.h"

/*
 * Lowers all copy intrinsics to sequences of load/store intrinsics.
 */

/* Walks down the deref chain and returns the next deref in the chain whose
 * child is a wildcard.  In other words, given the chain  a[1].foo[*].bar,
 * this function will return the deref to foo.  Calling it a second time
 * with the [*].bar, it will return NULL.
 */
static nir_deref *
deref_next_wildcard_parent(nir_deref *deref)
{
   for (nir_deref *tail = deref; tail->child; tail = tail->child) {
      if (tail->child->deref_type != nir_deref_type_array)
         continue;

      nir_deref_array *arr = nir_deref_as_array(tail->child);

      if (arr->deref_array_type == nir_deref_array_type_wildcard)
         return tail;
   }

   return NULL;
}

/* This function recursively walks the given deref chain and replaces the
 * given copy instruction with an equivalent sequence load/store
 * operations.
 *
 * @copy_instr    The copy instruction to replace; new instructions will be
 *                inserted before this one
 *
 * @dest_head     The head of the destination variable deref chain
 *
 * @src_head      The head of the source variable deref chain
 *
 * @dest_tail     The current tail of the destination variable deref chain;
 *                this is used for recursion and external callers of this
 *                function should call it with tail == head
 *
 * @src_tail      The current tail of the source variable deref chain;
 *                this is used for recursion and external callers of this
 *                function should call it with tail == head
 *
 * @state         The current variable lowering state
 */
static void
emit_copy_load_store(nir_intrinsic_instr *copy_instr,
                     nir_deref_var *dest_head, nir_deref_var *src_head,
                     nir_deref *dest_tail, nir_deref *src_tail, void *mem_ctx)
{
   /* Find the next pair of wildcards */
   nir_deref *src_arr_parent = deref_next_wildcard_parent(src_tail);
   nir_deref *dest_arr_parent = deref_next_wildcard_parent(dest_tail);

   if (src_arr_parent || dest_arr_parent) {
      /* Wildcards had better come in matched pairs */
      assert(src_arr_parent && dest_arr_parent);

      nir_deref_array *src_arr = nir_deref_as_array(src_arr_parent->child);
      nir_deref_array *dest_arr = nir_deref_as_array(dest_arr_parent->child);

      unsigned length = glsl_get_length(src_arr_parent->type);
      /* The wildcards should represent the same number of elements */
      assert(length == glsl_get_length(dest_arr_parent->type));
      assert(length > 0);

      /* Walk over all of the elements that this wildcard refers to and
       * call emit_copy_load_store on each one of them */
      src_arr->deref_array_type = nir_deref_array_type_direct;
      dest_arr->deref_array_type = nir_deref_array_type_direct;
      for (unsigned i = 0; i < length; i++) {
         src_arr->base_offset = i;
         dest_arr->base_offset = i;
         emit_copy_load_store(copy_instr, dest_head, src_head,
                              &dest_arr->deref, &src_arr->deref, mem_ctx);
      }
      src_arr->deref_array_type = nir_deref_array_type_wildcard;
      dest_arr->deref_array_type = nir_deref_array_type_wildcard;
   } else {
      /* In this case, we have no wildcards anymore, so all we have to do
       * is just emit the load and store operations. */
      src_tail = nir_deref_tail(src_tail);
      dest_tail = nir_deref_tail(dest_tail);

      assert(src_tail->type == dest_tail->type);

      unsigned num_components = glsl_get_vector_elements(src_tail->type);
      unsigned bit_size = glsl_get_bit_size(src_tail->type);

      nir_intrinsic_instr *load =
         nir_intrinsic_instr_create(mem_ctx, nir_intrinsic_load_var);
      load->num_components = num_components;
      load->variables[0] = nir_deref_as_var(nir_copy_deref(load, &src_head->deref));
      nir_ssa_dest_init(&load->instr, &load->dest, num_components, bit_size,
                        NULL);

      nir_instr_insert_before(&copy_instr->instr, &load->instr);

      nir_intrinsic_instr *store =
         nir_intrinsic_instr_create(mem_ctx, nir_intrinsic_store_var);
      store->num_components = num_components;
      nir_intrinsic_set_write_mask(store, (1 << num_components) - 1);
      store->variables[0] = nir_deref_as_var(nir_copy_deref(store, &dest_head->deref));

      store->src[0].is_ssa = true;
      store->src[0].ssa = &load->dest.ssa;

      nir_instr_insert_before(&copy_instr->instr, &store->instr);
   }
}

/* Lowers a copy instruction to a sequence of load/store instructions
 *
 * The new instructions are placed before the copy instruction in the IR.
 */
void
nir_lower_var_copy_instr(nir_intrinsic_instr *copy, void *mem_ctx)
{
   assert(copy->intrinsic == nir_intrinsic_copy_var);
   emit_copy_load_store(copy, copy->variables[0], copy->variables[1],
                        &copy->variables[0]->deref,
                        &copy->variables[1]->deref, mem_ctx);
}

static void
lower_var_copies_impl(nir_function_impl *impl)
{
   void *mem_ctx = ralloc_parent(impl);

   nir_foreach_block(block, impl) {
      nir_foreach_instr_safe(instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *copy = nir_instr_as_intrinsic(instr);
         if (copy->intrinsic != nir_intrinsic_copy_var)
            continue;

         nir_lower_var_copy_instr(copy, mem_ctx);

         nir_instr_remove(&copy->instr);
         ralloc_free(copy);
      }
   }
}

/* Lowers every copy_var instruction in the program to a sequence of
 * load/store instructions.
 */
void
nir_lower_var_copies(nir_shader *shader)
{
   nir_foreach_function(function, shader) {
      if (function->impl)
         lower_var_copies_impl(function->impl);
   }
}
