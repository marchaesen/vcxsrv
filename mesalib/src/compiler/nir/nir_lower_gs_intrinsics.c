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

/**
 * \file nir_lower_gs_intrinsics.c
 *
 * Geometry Shaders can call EmitVertex()/EmitStreamVertex() to output an
 * arbitrary number of vertices.  However, the shader must declare the maximum
 * number of vertices that it will ever output - further attempts to emit
 * vertices result in undefined behavior according to the GLSL specification.
 *
 * Drivers might use this maximum number of vertices to allocate enough space
 * to hold the geometry shader's output.  Some drivers (such as i965) need to
 * implement "safety checks" which ensure that the shader hasn't emitted too
 * many vertices, to avoid overflowing that space and trashing other memory.
 *
 * The count of emitted vertices can also be useful in buffer offset
 * calculations, so drivers know where to write the GS output.
 *
 * However, for simple geometry shaders that emit a statically determinable
 * number of vertices, this extra bookkeeping is unnecessary and inefficient.
 * By tracking the vertex count in NIR, we allow constant folding/propagation
 * and dead control flow optimizations to eliminate most of it where possible.
 *
 * This pass introduces a new global variable which stores the current vertex
 * count (initialized to 0), and converts emit_vertex/end_primitive intrinsics
 * to their *_with_counter variants.  emit_vertex is also wrapped in a safety
 * check to avoid buffer overflows.  Finally, it adds a set_vertex_count
 * intrinsic at the end of the program, informing the driver of the final
 * vertex count.
 */

struct state {
   nir_builder *builder;
   nir_variable *vertex_count_var;
   bool progress;
};

/**
 * Replace emit_vertex intrinsics with:
 *
 * if (vertex_count < max_vertices) {
 *    emit_vertex_with_counter vertex_count ...
 *    vertex_count += 1
 * }
 */
static void
rewrite_emit_vertex(nir_intrinsic_instr *intrin, struct state *state)
{
   nir_builder *b = state->builder;

   /* Load the vertex count */
   b->cursor = nir_before_instr(&intrin->instr);
   nir_ssa_def *count = nir_load_var(b, state->vertex_count_var);

   nir_ssa_def *max_vertices =
      nir_imm_int(b, b->shader->info->gs.vertices_out);

   /* Create: if (vertex_count < max_vertices) and insert it.
    *
    * The new if statement needs to be hooked up to the control flow graph
    * before we start inserting instructions into it.
    */
   nir_if *if_stmt = nir_if_create(b->shader);
   if_stmt->condition = nir_src_for_ssa(nir_ilt(b, count, max_vertices));
   nir_builder_cf_insert(b, &if_stmt->cf_node);

   /* Fill out the new then-block */
   b->cursor = nir_after_cf_list(&if_stmt->then_list);

   nir_intrinsic_instr *lowered =
      nir_intrinsic_instr_create(b->shader,
                                 nir_intrinsic_emit_vertex_with_counter);
   nir_intrinsic_set_stream_id(lowered, nir_intrinsic_stream_id(intrin));
   lowered->src[0] = nir_src_for_ssa(count);
   nir_builder_instr_insert(b, &lowered->instr);

   /* Increment the vertex count by 1 */
   nir_store_var(b, state->vertex_count_var,
                 nir_iadd(b, count, nir_imm_int(b, 1)),
                 0x1); /* .x */

   nir_instr_remove(&intrin->instr);

   state->progress = true;
}

/**
 * Replace end_primitive with end_primitive_with_counter.
 */
static void
rewrite_end_primitive(nir_intrinsic_instr *intrin, struct state *state)
{
   nir_builder *b = state->builder;

   b->cursor = nir_before_instr(&intrin->instr);
   nir_ssa_def *count = nir_load_var(b, state->vertex_count_var);

   nir_intrinsic_instr *lowered =
      nir_intrinsic_instr_create(b->shader,
                                 nir_intrinsic_end_primitive_with_counter);
   nir_intrinsic_set_stream_id(lowered, nir_intrinsic_stream_id(intrin));
   lowered->src[0] = nir_src_for_ssa(count);
   nir_builder_instr_insert(b, &lowered->instr);

   nir_instr_remove(&intrin->instr);

   state->progress = true;
}

static bool
rewrite_intrinsics(nir_block *block, struct state *state)
{
   nir_foreach_instr_safe(instr, block) {
      if (instr->type != nir_instr_type_intrinsic)
         continue;

      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
      switch (intrin->intrinsic) {
      case nir_intrinsic_emit_vertex:
         rewrite_emit_vertex(intrin, state);
         break;
      case nir_intrinsic_end_primitive:
         rewrite_end_primitive(intrin, state);
         break;
      default:
         /* not interesting; skip this */
         break;
      }
   }

   return true;
}

/**
 * Add a set_vertex_count intrinsic at the end of the program
 * (representing the final vertex count).
 */
static void
append_set_vertex_count(nir_block *end_block, struct state *state)
{
   nir_builder *b = state->builder;
   nir_shader *shader = state->builder->shader;

   /* Insert the new intrinsic in all of the predecessors of the end block,
    * but before any jump instructions (return).
    */
   struct set_entry *entry;
   set_foreach(end_block->predecessors, entry) {
      nir_block *pred = (nir_block *) entry->key;
      b->cursor = nir_after_block_before_jump(pred);

      nir_ssa_def *count = nir_load_var(b, state->vertex_count_var);

      nir_intrinsic_instr *set_vertex_count =
         nir_intrinsic_instr_create(shader, nir_intrinsic_set_vertex_count);
      set_vertex_count->src[0] = nir_src_for_ssa(count);

      nir_builder_instr_insert(b, &set_vertex_count->instr);
   }
}

bool
nir_lower_gs_intrinsics(nir_shader *shader)
{
   struct state state;
   state.progress = false;

   nir_function_impl *impl = nir_shader_get_entrypoint(shader);
   assert(impl);

   nir_builder b;
   nir_builder_init(&b, impl);
   state.builder = &b;

   /* Create the counter variable */
   state.vertex_count_var =
      nir_local_variable_create(impl, glsl_uint_type(), "vertex_count");
   /* initialize to 0 */
   b.cursor = nir_before_cf_list(&impl->body);
   nir_store_var(&b, state.vertex_count_var, nir_imm_int(&b, 0), 0x1);

   nir_foreach_block_safe(block, impl)
      rewrite_intrinsics(block, &state);

   /* This only works because we have a single main() function. */
   append_set_vertex_count(impl->end_block, &state);

   nir_metadata_preserve(impl, 0);

   return state.progress;
}
