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
   if (nir_is_per_vertex_io(var, nir->info.stage))
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
   const struct glsl_type *type = glsl_array_type(glsl_float_type(), length, 0);

   if (nir_is_per_vertex_io(var, stage))
      type = glsl_array_type(type, glsl_get_length(var->type), 0);

   var->type = type;
}

static void
rewrite_clip_cull_deref(nir_builder *b,
                        nir_deref_instr *deref,
                        const struct glsl_type *type,
                        unsigned tail_offset)
{
   deref->type = type;

   if (glsl_type_is_array(type)) {
      const struct glsl_type *child_type = glsl_get_array_element(type);
      nir_foreach_use(src, &deref->dest.ssa) {
         rewrite_clip_cull_deref(b, nir_instr_as_deref(src->parent_instr),
                                 child_type, tail_offset);
      }
   } else {
      assert(glsl_type_is_scalar(type));

      /* This is the end of the line.  Add the tail offset if needed */
      if (tail_offset > 0) {
         b->cursor = nir_before_instr(&deref->instr);
         assert(deref->deref_type == nir_deref_type_array);
         nir_ssa_def *index = nir_iadd(b, deref->arr.index.ssa,
                                          nir_imm_int(b, tail_offset));
         nir_instr_rewrite_src(&deref->instr, &deref->arr.index,
                               nir_src_for_ssa(index));
      }
   }
}

static void
rewrite_references(nir_builder *b,
                   nir_instr *instr,
                   nir_variable *combined,
                   unsigned cull_offset)
{
   if (instr->type != nir_instr_type_deref)
      return;

   nir_deref_instr *deref = nir_instr_as_deref(instr);
   if (deref->deref_type != nir_deref_type_var)
      return;

   if (deref->var->data.mode != combined->data.mode)
      return;

   const unsigned location = deref->var->data.location;
   if (location != VARYING_SLOT_CLIP_DIST0 &&
       location != VARYING_SLOT_CULL_DIST0)
      return;

   deref->var = combined;
   if (location == VARYING_SLOT_CULL_DIST0)
      rewrite_clip_cull_deref(b, deref, combined->type, cull_offset);
   else
      rewrite_clip_cull_deref(b, deref, combined->type, 0);
}

static bool
combine_clip_cull(nir_shader *nir,
                  struct exec_list *vars,
                  bool store_info)
{
   nir_variable *cull = NULL;
   nir_variable *clip = NULL;
   bool progress = false;

   nir_foreach_variable(var, vars) {
      if (var->data.location == VARYING_SLOT_CLIP_DIST0)
         clip = var;

      if (var->data.location == VARYING_SLOT_CULL_DIST0)
         cull = var;
   }

   /* if the GLSL lowering pass has already run, don't bother repeating */
   if (!cull && clip) {
      if (!glsl_type_is_array(clip->type))
         return false;
   }

   const unsigned clip_array_size = get_unwrapped_array_length(nir, clip);
   const unsigned cull_array_size = get_unwrapped_array_length(nir, cull);

   if (store_info) {
      nir->info.clip_distance_array_size = clip_array_size;
      nir->info.cull_distance_array_size = cull_array_size;
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
         update_type(clip, nir->info.stage, clip_array_size + cull_array_size);

         /* Rewrite CullDistance to reference the combined array */
         nir_foreach_function(function, nir) {
            if (function->impl) {
               nir_builder b;
               nir_builder_init(&b, function->impl);

               nir_foreach_block(block, function->impl) {
                  nir_foreach_instr(instr, block) {
                     rewrite_references(&b, instr, clip, clip_array_size);
                  }
               }
            }
         }

         /* Delete the old CullDistance variable */
         exec_node_remove(&cull->node);
         ralloc_free(cull);
      }

      nir_foreach_function(function, nir) {
         if (function->impl) {
            nir_metadata_preserve(function->impl,
                                  nir_metadata_block_index |
                                  nir_metadata_dominance);
         }
      }
      progress = true;
   }

   return progress;
}

bool
nir_lower_clip_cull_distance_arrays(nir_shader *nir)
{
   bool progress = false;

   if (nir->info.stage <= MESA_SHADER_GEOMETRY)
      progress |= combine_clip_cull(nir, &nir->outputs, true);

   if (nir->info.stage > MESA_SHADER_VERTEX)
      progress |= combine_clip_cull(nir, &nir->inputs, false);

   return progress;
}
