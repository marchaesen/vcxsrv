/*
 * Copyright © 2021 Intel Corporation
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
 */

#include "nir.h"
#include "nir_builder.h"

#include "util/set.h"
#include "util/macros.h"

/** @file nir_opt_ray_queries.c
 *
 * Remove ray queries that the shader is not using the result of.
 */

static void
mark_query_read(struct set *queries,
                nir_intrinsic_instr *intrin)
{
   nir_ssa_def *rq_def = intrin->src[0].ssa;

   nir_variable *query;
   if (rq_def->parent_instr->type == nir_instr_type_intrinsic) {
      nir_intrinsic_instr *load_deref =
         nir_instr_as_intrinsic(rq_def->parent_instr);
      assert(load_deref->intrinsic == nir_intrinsic_load_deref);

      query = nir_intrinsic_get_var(load_deref, 0);
   } else if (rq_def->parent_instr->type == nir_instr_type_deref) {
      query = nir_deref_instr_get_variable(
         nir_instr_as_deref(rq_def->parent_instr));
   } else {
      return;
   }
   assert(query);

   _mesa_set_add(queries, query);
}

static void
nir_find_ray_queries_read(struct set *queries,
                          nir_shader *shader)
{
   nir_foreach_function(function, shader) {
      nir_function_impl *impl = function->impl;

      if (!impl)
         continue;

      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
            switch (intrin->intrinsic) {
            case nir_intrinsic_rq_proceed:
               if (list_length(&intrin->dest.ssa.uses) > 0 ||
                   list_length(&intrin->dest.ssa.if_uses) > 0)
                  mark_query_read(queries, intrin);
               break;
            case nir_intrinsic_rq_load:
               mark_query_read(queries, intrin);
               break;
            default:
               break;
            }
         }
      }
   }
}

static bool
nir_replace_unread_queries_instr(nir_builder *b, nir_instr *instr, void *data)
{
   struct set *queries = data;

   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
   switch (intrin->intrinsic) {
   case nir_intrinsic_rq_initialize:
   case nir_intrinsic_rq_terminate:
   case nir_intrinsic_rq_generate_intersection:
   case nir_intrinsic_rq_confirm_intersection:
      break;
   case nir_intrinsic_rq_proceed:
      break;
   default:
      return false;
   }

   nir_variable *query = nir_intrinsic_get_var(intrin, 0);
   assert(query);

   struct set_entry *entry = _mesa_set_search(queries, query);
   if (entry)
      return false;

   if (intrin->intrinsic == nir_intrinsic_rq_load) {
      assert(list_is_empty(&intrin->dest.ssa.uses));
      assert(list_is_empty(&intrin->dest.ssa.if_uses));
   }

   nir_instr_remove(instr);

   return true;
}

bool
nir_opt_ray_queries(nir_shader *shader)
{
   struct set *read_queries = _mesa_pointer_set_create(NULL);
   nir_find_ray_queries_read(read_queries, shader);

   bool progress =
      nir_shader_instructions_pass(shader,
                                   nir_replace_unread_queries_instr,
                                   nir_metadata_block_index |
                                   nir_metadata_dominance,
                                   read_queries);

   /* Update the number of queries if some have been removed. */
   if (progress) {
      nir_remove_dead_derefs(shader);
      nir_remove_dead_variables(shader,
                                nir_var_shader_temp | nir_var_function_temp,
                                NULL);
      nir_shader_gather_info(shader, nir_shader_get_entrypoint(shader));
   }

   _mesa_set_destroy(read_queries, NULL);

   return progress;
}
