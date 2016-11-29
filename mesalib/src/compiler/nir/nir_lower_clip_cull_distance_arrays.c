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
 * @file
 *
 * This pass combines separate clip and cull distance arrays into a
 * single array that contains both.  Clip distances come first, then
 * cull distances.  It also populates nir_shader_info with the size
 * of the original arrays so the driver knows which are which.
 */

/**
 * Get the length of the clip/cull distance array, looking past
 * any interface block arrays.
 */
static unsigned
get_unwrapped_array_length(nir_shader *nir, nir_variable *var)
{
   if (!var)
      return 0;

   /* Unwrap GS input and TCS input/output interfaces.  We want the
    * underlying clip/cull distance array length, not the per-vertex
    * array length.
    */
   const struct glsl_type *type = var->type;
   if (nir_is_per_vertex_io(var, nir->stage))
      type = glsl_get_array_element(type);

   assert(glsl_type_is_array(type));

   return glsl_get_length(type);
}

/**
 * Update the type of the combined array (including interface block nesting).
 */
static void
update_type(nir_variable *var, gl_shader_stage stage, unsigned length)
{
   const struct glsl_type *type = glsl_array_type(glsl_float_type(), length);

   if (nir_is_per_vertex_io(var, stage))
      type = glsl_array_type(type, glsl_get_length(var->type));

   var->type = type;
}

/**
 * Rewrite any clip/cull distances to refer to the new combined array.
 */
static void
rewrite_references(nir_instr *instr,
                   nir_variable *combined,
                   unsigned cull_offset)
{
   if (instr->type != nir_instr_type_intrinsic)
      return;

   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

   /* copy_var needs to be lowered to load/store before calling this pass */
   assert(intrin->intrinsic != nir_intrinsic_copy_var);

   if (intrin->intrinsic != nir_intrinsic_load_var &&
       intrin->intrinsic != nir_intrinsic_store_var)
      return;

   nir_deref_var *var_ref = intrin->variables[0];
   if (var_ref->var->data.mode != combined->data.mode)
      return;

   if (var_ref->var->data.location != VARYING_SLOT_CLIP_DIST0 &&
       var_ref->var->data.location != VARYING_SLOT_CULL_DIST0)
      return;

   /* Update types along the deref chain */
   const struct glsl_type *type = combined->type;
   nir_deref *deref = &var_ref->deref;
   while (deref) {
      deref->type = type;
      deref = deref->child;
      type = glsl_get_array_element(type);
   }

   /* For cull distances, add an offset to the array index */
   if (var_ref->var->data.location == VARYING_SLOT_CULL_DIST0) {
      nir_deref *tail = nir_deref_tail(&intrin->variables[0]->deref);
      nir_deref_array *array_ref = nir_deref_as_array(tail);

      array_ref->base_offset += cull_offset;
   }

   /* Point the deref at the combined array */
   var_ref->var = combined;

   /* There's no need to update writemasks; it's a scalar array. */
}

static void
combine_clip_cull(nir_shader *nir,
                  struct exec_list *vars,
                  bool store_info)
{
   nir_variable *cull = NULL;
   nir_variable *clip = NULL;

   nir_foreach_variable(var, vars) {
      if (var->data.location == VARYING_SLOT_CLIP_DIST0)
         clip = var;

      if (var->data.location == VARYING_SLOT_CULL_DIST0)
         cull = var;
   }

   const unsigned clip_array_size = get_unwrapped_array_length(nir, clip);
   const unsigned cull_array_size = get_unwrapped_array_length(nir, cull);

   if (store_info) {
      nir->info->clip_distance_array_size = clip_array_size;
      nir->info->cull_distance_array_size = cull_array_size;
   }

   if (clip)
      clip->data.compact = true;

   if (cull)
      cull->data.compact = true;

   if (cull_array_size > 0) {
      if (clip_array_size == 0) {
         /* No clip distances, just change the cull distance location */
         cull->data.location = VARYING_SLOT_CLIP_DIST0;
      } else {
         /* Turn the ClipDistance array into a combined one */
         update_type(clip, nir->stage, clip_array_size + cull_array_size);

         /* Rewrite CullDistance to reference the combined array */
         nir_foreach_function(function, nir) {
            if (function->impl) {
               nir_foreach_block(block, function->impl) {
                  nir_foreach_instr(instr, block) {
                     rewrite_references(instr, clip, clip_array_size);
                  }
               }
            }
         }

         /* Delete the old CullDistance variable */
         exec_node_remove(&cull->node);
         ralloc_free(cull);
      }
   }
}

void
nir_lower_clip_cull_distance_arrays(nir_shader *nir)
{
   if (nir->stage <= MESA_SHADER_GEOMETRY)
      combine_clip_cull(nir, &nir->outputs, true);

   if (nir->stage > MESA_SHADER_VERTEX)
      combine_clip_cull(nir, &nir->inputs, false);
}
