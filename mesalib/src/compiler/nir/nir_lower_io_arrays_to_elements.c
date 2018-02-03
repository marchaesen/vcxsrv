/*
 * Copyright © 2017 Timothy Arceri
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

/** @file nir_lower_io_arrays_to_elements.c
 *
 * Split arrays/matrices with direct indexing into individual elements. This
 * will allow optimisation passes to better clean up unused elements.
 *
 */

static unsigned
get_io_offset(nir_builder *b, nir_deref_var *deref, nir_variable *var,
              unsigned *element_index)
{
   bool vs_in = (b->shader->info.stage == MESA_SHADER_VERTEX) &&
                (var->data.mode == nir_var_shader_in);

   nir_deref *tail = &deref->deref;

   /* For per-vertex input arrays (i.e. geometry shader inputs), skip the
    * outermost array index.  Process the rest normally.
    */
   if (nir_is_per_vertex_io(var, b->shader->info.stage)) {
      tail = tail->child;
   }

   unsigned offset = 0;
   while (tail->child != NULL) {
      tail = tail->child;

      if (tail->deref_type == nir_deref_type_array) {
         nir_deref_array *deref_array = nir_deref_as_array(tail);
         assert(deref_array->deref_array_type != nir_deref_array_type_indirect);

         unsigned size = glsl_count_attribute_slots(tail->type, vs_in);
         offset += size * deref_array->base_offset;

         unsigned num_elements = glsl_type_is_array(tail->type) ?
            glsl_get_aoa_size(tail->type) : 1;

         num_elements *= glsl_type_is_matrix(glsl_without_array(tail->type)) ?
            glsl_get_matrix_columns(glsl_without_array(tail->type)) : 1;

         *element_index += num_elements * deref_array->base_offset;
      } else if (tail->deref_type == nir_deref_type_struct) {
         /* TODO: we could also add struct splitting support to this pass */
         break;
      }
   }

   return offset;
}

static nir_variable **
get_array_elements(struct hash_table *ht, nir_variable *var,
                   gl_shader_stage stage)
{
   nir_variable **elements;
   struct hash_entry *entry = _mesa_hash_table_search(ht, var);
   if (!entry) {
      const struct glsl_type *type = var->type;
      if (nir_is_per_vertex_io(var, stage)) {
         assert(glsl_type_is_array(type));
         type = glsl_get_array_element(type);
      }

      unsigned num_elements = glsl_type_is_array(type) ?
         glsl_get_aoa_size(type) : 1;

      num_elements *= glsl_type_is_matrix(glsl_without_array(type)) ?
         glsl_get_matrix_columns(glsl_without_array(type)) : 1;

      elements = (nir_variable **) calloc(num_elements, sizeof(nir_variable *));
      _mesa_hash_table_insert(ht, var, elements);
   } else {
      elements = (nir_variable **) entry->data;
   }

   return elements;
}

static void
create_array_deref(nir_intrinsic_instr *arr_intr,
                   nir_intrinsic_instr *element_intr)
{
   assert(arr_intr->variables[0]->deref.child);

   nir_deref *parent = &element_intr->variables[0]->deref;
   nir_deref_array *darr =
            nir_deref_as_array(arr_intr->variables[0]->deref.child);
   nir_deref_array *ndarr = nir_deref_array_create(parent);

   ndarr->deref.type = glsl_get_array_element(parent->type);
   ndarr->deref_array_type = darr->deref_array_type;
   ndarr->base_offset = darr->base_offset;

   if (ndarr->deref_array_type == nir_deref_array_type_indirect)
      nir_src_copy(&ndarr->indirect, &darr->indirect, parent);

   element_intr->variables[0]->deref.child = &ndarr->deref;
}

static void
lower_array(nir_builder *b, nir_intrinsic_instr *intr, nir_variable *var,
            struct hash_table *varyings)
{
   b->cursor = nir_before_instr(&intr->instr);

   nir_variable **elements =
      get_array_elements(varyings, var, b->shader->info.stage);

   unsigned elements_index = 0;
   unsigned io_offset = get_io_offset(b, intr->variables[0], var,
                                      &elements_index);

   nir_variable *element = elements[elements_index];
   if (!element) {
         element = nir_variable_clone(var, b->shader);
         element->data.location =  var->data.location + io_offset;

         const struct glsl_type *type = glsl_without_array(element->type);

         /* This pass also splits matrices so we need give them a new type. */
         if (glsl_type_is_matrix(type)) {
            type = glsl_vector_type(glsl_get_base_type(type),
                                    glsl_get_vector_elements(type));
         }

         if (nir_is_per_vertex_io(var, b->shader->info.stage)) {
            type = glsl_get_array_instance(type,
                                           glsl_get_length(element->type));
         }

         element->type = type;
         elements[elements_index] = element;

         nir_shader_add_variable(b->shader, element);
   }

   nir_intrinsic_instr *element_intr =
      nir_intrinsic_instr_create(b->shader, intr->intrinsic);
   element_intr->num_components = intr->num_components;
   element_intr->variables[0] = nir_deref_var_create(element_intr, element);

   if (intr->intrinsic != nir_intrinsic_store_var) {
      nir_ssa_dest_init(&element_intr->instr, &element_intr->dest,
                        intr->num_components, intr->dest.ssa.bit_size, NULL);

      if (intr->intrinsic == nir_intrinsic_interp_var_at_offset ||
          intr->intrinsic == nir_intrinsic_interp_var_at_sample) {
         nir_src_copy(&element_intr->src[0], &intr->src[0],
                      &element_intr->instr);
      }

      nir_ssa_def_rewrite_uses(&intr->dest.ssa,
                               nir_src_for_ssa(&element_intr->dest.ssa));
   } else {
      nir_intrinsic_set_write_mask(element_intr,
                                   nir_intrinsic_write_mask(intr));
      nir_src_copy(&element_intr->src[0], &intr->src[0],
                   &element_intr->instr);
   }

   if (nir_is_per_vertex_io(var, b->shader->info.stage)) {
      create_array_deref(intr, element_intr);
   }

   nir_builder_instr_insert(b, &element_intr->instr);

   /* Remove the old load intrinsic */
   nir_instr_remove(&intr->instr);
}

static bool
deref_has_indirect(nir_builder *b, nir_variable *var, nir_deref_var *deref)
{
   nir_deref *tail = &deref->deref;

   if (nir_is_per_vertex_io(var, b->shader->info.stage)) {
      tail = tail->child;
   }

   for (tail = tail->child; tail; tail = tail->child) {
      if (tail->deref_type != nir_deref_type_array)
         continue;

      nir_deref_array *arr = nir_deref_as_array(tail);
      if (arr->deref_array_type == nir_deref_array_type_indirect)
         return true;
   }

   return false;
}

/* Creates a mask of locations that contains arrays that are indexed via
 * indirect indexing.
 */
static void
create_indirects_mask(nir_shader *shader, uint64_t *indirects,
                      uint64_t *patch_indirects, nir_variable_mode mode)
{
   nir_foreach_function(function, shader) {
      if (function->impl) {
         nir_builder b;
         nir_builder_init(&b, function->impl);

         nir_foreach_block(block, function->impl) {
            nir_foreach_instr_safe(instr, block) {

               if (instr->type != nir_instr_type_intrinsic)
                  continue;

               nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

               if (intr->intrinsic != nir_intrinsic_load_var &&
                   intr->intrinsic != nir_intrinsic_store_var &&
                   intr->intrinsic != nir_intrinsic_interp_var_at_centroid &&
                   intr->intrinsic != nir_intrinsic_interp_var_at_sample &&
                   intr->intrinsic != nir_intrinsic_interp_var_at_offset)
                  continue;

               nir_variable *var = intr->variables[0]->var;

               if (var->data.mode != mode)
                  continue;

               uint64_t loc_mask = ((uint64_t)1) << var->data.location;
               if (var->data.patch) {
                  if (deref_has_indirect(&b, var, intr->variables[0]))
                     patch_indirects[var->data.location_frac] |= loc_mask;
               } else {
                  if (deref_has_indirect(&b, var, intr->variables[0]))
                     indirects[var->data.location_frac] |= loc_mask;
               }
            }
         }
      }
   }
}

static void
lower_io_arrays_to_elements(nir_shader *shader, nir_variable_mode mask,
                            uint64_t *indirects, uint64_t *patch_indirects,
                            struct hash_table *varyings,
                            bool after_cross_stage_opts)
{
   nir_foreach_function(function, shader) {
      if (function->impl) {
         nir_builder b;
         nir_builder_init(&b, function->impl);

         nir_foreach_block(block, function->impl) {
            nir_foreach_instr_safe(instr, block) {
               if (instr->type != nir_instr_type_intrinsic)
                  continue;

               nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

               if (intr->intrinsic != nir_intrinsic_load_var &&
                   intr->intrinsic != nir_intrinsic_store_var &&
                   intr->intrinsic != nir_intrinsic_interp_var_at_centroid &&
                   intr->intrinsic != nir_intrinsic_interp_var_at_sample &&
                   intr->intrinsic != nir_intrinsic_interp_var_at_offset)
                  continue;

               nir_variable *var = intr->variables[0]->var;

               /* Skip indirects */
               uint64_t loc_mask = ((uint64_t)1) << var->data.location;
               if (var->data.patch) {
                  if (patch_indirects[var->data.location_frac] & loc_mask)
                     continue;
               } else {
                  if (indirects[var->data.location_frac] & loc_mask)
                     continue;
               }

               nir_variable_mode mode = var->data.mode;

               const struct glsl_type *type = var->type;
               if (nir_is_per_vertex_io(var, b.shader->info.stage)) {
                  assert(glsl_type_is_array(type));
                  type = glsl_get_array_element(type);
               }

               /* Skip types we cannot split.
                *
                * TODO: Add support for struct splitting.
                */
               if ((!glsl_type_is_array(type) && !glsl_type_is_matrix(type))||
                   glsl_type_is_struct(glsl_without_array(type)))
                  continue;

               /* Skip builtins */
               if (!after_cross_stage_opts &&
                   var->data.location < VARYING_SLOT_VAR0 &&
                   var->data.location >= 0)
                  continue;

               /* Don't bother splitting if we can't opt away any unused
                * elements.
                */
               if (!after_cross_stage_opts && var->data.always_active_io)
                  continue;

               switch (intr->intrinsic) {
               case nir_intrinsic_interp_var_at_centroid:
               case nir_intrinsic_interp_var_at_sample:
               case nir_intrinsic_interp_var_at_offset:
               case nir_intrinsic_load_var:
               case nir_intrinsic_store_var:
                  if ((mask & nir_var_shader_in && mode == nir_var_shader_in) ||
                      (mask & nir_var_shader_out && mode == nir_var_shader_out))
                     lower_array(&b, intr, var, varyings);
                  break;
               default:
                  break;
               }
            }
         }
      }
   }
}

void
nir_lower_io_arrays_to_elements_no_indirects(nir_shader *shader,
                                             bool outputs_only)
{
   struct hash_table *split_inputs =
      _mesa_hash_table_create(NULL, _mesa_hash_pointer,
                              _mesa_key_pointer_equal);
   struct hash_table *split_outputs =
      _mesa_hash_table_create(NULL, _mesa_hash_pointer,
                              _mesa_key_pointer_equal);

   uint64_t indirects[4] = {0}, patch_indirects[4] = {0};

   lower_io_arrays_to_elements(shader, nir_var_shader_out, indirects,
                               patch_indirects, split_outputs, true);

   if (!outputs_only) {
      lower_io_arrays_to_elements(shader, nir_var_shader_in, indirects,
                                  patch_indirects, split_inputs, true);

      /* Remove old input from the shaders inputs list */
      struct hash_entry *entry;
      hash_table_foreach(split_inputs, entry) {
         nir_variable *var = (nir_variable *) entry->key;
         exec_node_remove(&var->node);

         free(entry->data);
      }
   }

   /* Remove old output from the shaders outputs list */
   struct hash_entry *entry;
   hash_table_foreach(split_outputs, entry) {
      nir_variable *var = (nir_variable *) entry->key;
      exec_node_remove(&var->node);

      free(entry->data);
   }

   _mesa_hash_table_destroy(split_inputs, NULL);
   _mesa_hash_table_destroy(split_outputs, NULL);
}

void
nir_lower_io_arrays_to_elements(nir_shader *producer, nir_shader *consumer)
{
   struct hash_table *split_inputs =
      _mesa_hash_table_create(NULL, _mesa_hash_pointer,
                              _mesa_key_pointer_equal);
   struct hash_table *split_outputs =
      _mesa_hash_table_create(NULL, _mesa_hash_pointer,
                              _mesa_key_pointer_equal);

   uint64_t indirects[4] = {0}, patch_indirects[4] = {0};
   create_indirects_mask(producer, indirects, patch_indirects,
                         nir_var_shader_out);
   create_indirects_mask(consumer, indirects, patch_indirects,
                         nir_var_shader_in);

   lower_io_arrays_to_elements(producer, nir_var_shader_out, indirects,
                               patch_indirects, split_outputs, false);

   lower_io_arrays_to_elements(consumer, nir_var_shader_in, indirects,
                               patch_indirects, split_inputs, false);

   /* Remove old input from the shaders inputs list */
   struct hash_entry *entry;
   hash_table_foreach(split_inputs, entry) {
      nir_variable *var = (nir_variable *) entry->key;
      exec_node_remove(&var->node);

      free(entry->data);
   }

   /* Remove old output from the shaders outputs list */
   hash_table_foreach(split_outputs, entry) {
      nir_variable *var = (nir_variable *) entry->key;
      exec_node_remove(&var->node);

      free(entry->data);
   }

   _mesa_hash_table_destroy(split_inputs, NULL);
   _mesa_hash_table_destroy(split_outputs, NULL);
}
