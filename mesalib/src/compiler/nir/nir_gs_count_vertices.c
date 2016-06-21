/*
 * Copyright © 2015 Intel Corporation
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

static nir_intrinsic_instr *
as_intrinsic(nir_instr *instr, nir_intrinsic_op op)
{
   if (instr->type != nir_instr_type_intrinsic)
      return NULL;

   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
   if (intrin->intrinsic != op)
      return NULL;

   return intrin;
}

static nir_intrinsic_instr *
as_set_vertex_count(nir_instr *instr)
{
   return as_intrinsic(instr, nir_intrinsic_set_vertex_count);
}

/**
 * If a geometry shader emits a constant number of vertices, return the
 * number of vertices.  Otherwise, return -1 (unknown).
 *
 * This only works if you've used nir_lower_gs_intrinsics() to do vertex
 * counting at the NIR level.
 */
int
nir_gs_count_vertices(const nir_shader *shader)
{
   int count = -1;

   nir_foreach_function(function, shader) {
      if (!function->impl)
         continue;

      /* set_vertex_count intrinsics only appear in predecessors of the
       * end block.  So we don't need to walk all of them.
       */
      struct set_entry *entry;
      set_foreach(function->impl->end_block->predecessors, entry) {
         nir_block *block = (nir_block *) entry->key;

         nir_foreach_instr_reverse(instr, block) {
            nir_intrinsic_instr *intrin = as_set_vertex_count(instr);
            if (!intrin)
               continue;

            nir_const_value *val = nir_src_as_const_value(intrin->src[0]);
            /* We've found a non-constant value.  Bail. */
            if (!val)
               return -1;

            if (count == -1)
               count = val->i32[0];

            /* We've found contradictory set_vertex_count intrinsics.
             * This can happen if there are early-returns in main() and
             * different paths emit different numbers of vertices.
             */
            if (count != val->i32[0])
               return -1;
         }
      }
   }

   return count;
}
