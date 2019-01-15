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
#include "util/set.h"
#include "util/hash_table.h"

/* This file contains various little helpers for doing simple linking in
 * NIR.  Eventually, we'll probably want a full-blown varying packing
 * implementation in here.  Right now, it just deletes unused things.
 */

/**
 * Returns the bits in the inputs_read, outputs_written, or
 * system_values_read bitfield corresponding to this variable.
 */
static uint64_t
get_variable_io_mask(nir_variable *var, gl_shader_stage stage)
{
   if (var->data.location < 0)
      return 0;

   unsigned location = var->data.patch ?
      var->data.location - VARYING_SLOT_PATCH0 : var->data.location;

   assert(var->data.mode == nir_var_shader_in ||
          var->data.mode == nir_var_shader_out ||
          var->data.mode == nir_var_system_value);
   assert(var->data.location >= 0);

   const struct glsl_type *type = var->type;
   if (nir_is_per_vertex_io(var, stage)) {
      assert(glsl_type_is_array(type));
      type = glsl_get_array_element(type);
   }

   unsigned slots = glsl_count_attribute_slots(type, false);
   return ((1ull << slots) - 1) << location;
}

static void
tcs_add_output_reads(nir_shader *shader, uint64_t *read, uint64_t *patches_read)
{
   nir_foreach_function(function, shader) {
      if (!function->impl)
         continue;

      nir_foreach_block(block, function->impl) {
         nir_foreach_instr(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
            if (intrin->intrinsic != nir_intrinsic_load_deref)
               continue;

            nir_deref_instr *deref = nir_src_as_deref(intrin->src[0]);
            if (deref->mode != nir_var_shader_out)
               continue;

            nir_variable *var = nir_deref_instr_get_variable(deref);
            if (var->data.patch) {
               patches_read[var->data.location_frac] |=
                  get_variable_io_mask(var, shader->info.stage);
            } else {
               read[var->data.location_frac] |=
                  get_variable_io_mask(var, shader->info.stage);
            }
         }
      }
   }
}

/**
 * Helper for removing unused shader I/O variables, by demoting them to global
 * variables (which may then by dead code eliminated).
 *
 * Example usage is:
 *
 * progress = nir_remove_unused_io_vars(producer,
 *                                      &producer->outputs,
 *                                      read, patches_read) ||
 *                                      progress;
 *
 * The "used" should be an array of 4 uint64_ts (probably of VARYING_BIT_*)
 * representing each .location_frac used.  Note that for vector variables,
 * only the first channel (.location_frac) is examined for deciding if the
 * variable is used!
 */
bool
nir_remove_unused_io_vars(nir_shader *shader, struct exec_list *var_list,
                          uint64_t *used_by_other_stage,
                          uint64_t *used_by_other_stage_patches)
{
   bool progress = false;
   uint64_t *used;

   nir_foreach_variable_safe(var, var_list) {
      if (var->data.patch)
         used = used_by_other_stage_patches;
      else
         used = used_by_other_stage;

      if (var->data.location < VARYING_SLOT_VAR0 && var->data.location >= 0)
         continue;

      if (var->data.always_active_io)
         continue;

      uint64_t other_stage = used[var->data.location_frac];

      if (!(other_stage & get_variable_io_mask(var, shader->info.stage))) {
         /* This one is invalid, make it a global variable instead */
         var->data.location = 0;
         var->data.mode = nir_var_private;

         exec_node_remove(&var->node);
         exec_list_push_tail(&shader->globals, &var->node);

         progress = true;
      }
   }

   if (progress)
      nir_fixup_deref_modes(shader);

   return progress;
}

bool
nir_remove_unused_varyings(nir_shader *producer, nir_shader *consumer)
{
   assert(producer->info.stage != MESA_SHADER_FRAGMENT);
   assert(consumer->info.stage != MESA_SHADER_VERTEX);

   uint64_t read[4] = { 0 }, written[4] = { 0 };
   uint64_t patches_read[4] = { 0 }, patches_written[4] = { 0 };

   nir_foreach_variable(var, &producer->outputs) {
      if (var->data.patch) {
         patches_written[var->data.location_frac] |=
            get_variable_io_mask(var, producer->info.stage);
      } else {
         written[var->data.location_frac] |=
            get_variable_io_mask(var, producer->info.stage);
      }
   }

   nir_foreach_variable(var, &consumer->inputs) {
      if (var->data.patch) {
         patches_read[var->data.location_frac] |=
            get_variable_io_mask(var, consumer->info.stage);
      } else {
         read[var->data.location_frac] |=
            get_variable_io_mask(var, consumer->info.stage);
      }
   }

   /* Each TCS invocation can read data written by other TCS invocations,
    * so even if the outputs are not used by the TES we must also make
    * sure they are not read by the TCS before demoting them to globals.
    */
   if (producer->info.stage == MESA_SHADER_TESS_CTRL)
      tcs_add_output_reads(producer, read, patches_read);

   bool progress = false;
   progress = nir_remove_unused_io_vars(producer, &producer->outputs, read,
                                        patches_read);

   progress = nir_remove_unused_io_vars(consumer, &consumer->inputs, written,
                                        patches_written) || progress;

   return progress;
}

static uint8_t
get_interp_type(nir_variable *var, const struct glsl_type *type,
                bool default_to_smooth_interp)
{
   if (glsl_type_is_integer(type))
      return INTERP_MODE_FLAT;
   else if (var->data.interpolation != INTERP_MODE_NONE)
      return var->data.interpolation;
   else if (default_to_smooth_interp)
      return INTERP_MODE_SMOOTH;
   else
      return INTERP_MODE_NONE;
}

#define INTERPOLATE_LOC_SAMPLE 0
#define INTERPOLATE_LOC_CENTROID 1
#define INTERPOLATE_LOC_CENTER 2

static uint8_t
get_interp_loc(nir_variable *var)
{
   if (var->data.sample)
      return INTERPOLATE_LOC_SAMPLE;
   else if (var->data.centroid)
      return INTERPOLATE_LOC_CENTROID;
   else
      return INTERPOLATE_LOC_CENTER;
}

static void
get_slot_component_masks_and_interp_types(struct exec_list *var_list,
                                          uint8_t *comps,
                                          uint8_t *interp_type,
                                          uint8_t *interp_loc,
                                          gl_shader_stage stage,
                                          bool default_to_smooth_interp)
{
   nir_foreach_variable_safe(var, var_list) {
      assert(var->data.location >= 0);

      /* Only remap things that aren't built-ins.
       * TODO: add TES patch support.
       */
      if (var->data.location >= VARYING_SLOT_VAR0 &&
          var->data.location - VARYING_SLOT_VAR0 < 32) {

         const struct glsl_type *type = var->type;
         if (nir_is_per_vertex_io(var, stage)) {
            assert(glsl_type_is_array(type));
            type = glsl_get_array_element(type);
         }

         unsigned location = var->data.location - VARYING_SLOT_VAR0;
         unsigned elements =
            glsl_get_vector_elements(glsl_without_array(type));

         bool dual_slot = glsl_type_is_dual_slot(glsl_without_array(type));
         unsigned slots = glsl_count_attribute_slots(type, false);
         unsigned comps_slot2 = 0;
         for (unsigned i = 0; i < slots; i++) {
            interp_type[location + i] =
               get_interp_type(var, type, default_to_smooth_interp);
            interp_loc[location + i] = get_interp_loc(var);

            if (dual_slot) {
               if (i & 1) {
                  comps[location + i] |= ((1 << comps_slot2) - 1);
               } else {
                  unsigned num_comps = 4 - var->data.location_frac;
                  comps_slot2 = (elements * 2) - num_comps;

                  /* Assume ARB_enhanced_layouts packing rules for doubles */
                  assert(var->data.location_frac == 0 ||
                         var->data.location_frac == 2);
                  assert(comps_slot2 <= 4);

                  comps[location + i] |=
                     ((1 << num_comps) - 1) << var->data.location_frac;
               }
            } else {
               comps[location + i] |=
                  ((1 << elements) - 1) << var->data.location_frac;
            }
         }
      }
   }
}

struct varying_loc
{
   uint8_t component;
   uint32_t location;
};

static void
remap_slots_and_components(struct exec_list *var_list, gl_shader_stage stage,
                           struct varying_loc (*remap)[4],
                           uint64_t *slots_used, uint64_t *out_slots_read)
 {
   uint64_t out_slots_read_tmp = 0;

   /* We don't touch builtins so just copy the bitmask */
   uint64_t slots_used_tmp =
      *slots_used & (((uint64_t)1 << (VARYING_SLOT_VAR0 - 1)) - 1);

   nir_foreach_variable(var, var_list) {
      assert(var->data.location >= 0);

      /* Only remap things that aren't built-ins */
      if (var->data.location >= VARYING_SLOT_VAR0 &&
          var->data.location - VARYING_SLOT_VAR0 < 32) {
         assert(var->data.location - VARYING_SLOT_VAR0 < 32);

         const struct glsl_type *type = var->type;
         if (nir_is_per_vertex_io(var, stage)) {
            assert(glsl_type_is_array(type));
            type = glsl_get_array_element(type);
         }

         unsigned num_slots = glsl_count_attribute_slots(type, false);
         bool used_across_stages = false;
         bool outputs_read = false;

         unsigned location = var->data.location - VARYING_SLOT_VAR0;
         struct varying_loc *new_loc = &remap[location][var->data.location_frac];

         uint64_t slots = (((uint64_t)1 << num_slots) - 1) << var->data.location;
         if (slots & *slots_used)
            used_across_stages = true;

         if (slots & *out_slots_read)
            outputs_read = true;

         if (new_loc->location) {
            var->data.location = new_loc->location;
            var->data.location_frac = new_loc->component;
         }

         if (var->data.always_active_io) {
            /* We can't apply link time optimisations (specifically array
             * splitting) to these so we need to copy the existing mask
             * otherwise we will mess up the mask for things like partially
             * marked arrays.
             */
            if (used_across_stages) {
               slots_used_tmp |=
                  *slots_used & (((uint64_t)1 << num_slots) - 1) << var->data.location;
            }

            if (outputs_read) {
               out_slots_read_tmp |=
                  *out_slots_read & (((uint64_t)1 << num_slots) - 1) << var->data.location;
            }

         } else {
            for (unsigned i = 0; i < num_slots; i++) {
               if (used_across_stages)
                  slots_used_tmp |= (uint64_t)1 << (var->data.location + i);

               if (outputs_read)
                  out_slots_read_tmp |= (uint64_t)1 << (var->data.location + i);
            }
         }
      }
   }

   *slots_used = slots_used_tmp;
   *out_slots_read = out_slots_read_tmp;
}

/* If there are empty components in the slot compact the remaining components
 * as close to component 0 as possible. This will make it easier to fill the
 * empty components with components from a different slot in a following pass.
 */
static void
compact_components(nir_shader *producer, nir_shader *consumer, uint8_t *comps,
                   uint8_t *interp_type, uint8_t *interp_loc,
                   bool default_to_smooth_interp)
{
   struct exec_list *input_list = &consumer->inputs;
   struct exec_list *output_list = &producer->outputs;
   struct varying_loc remap[32][4] = {{{0}, {0}}};

   /* Create a cursor for each interpolation type */
   unsigned cursor[4] = {0};

   /* We only need to pass over one stage and we choose the consumer as it seems
    * to cause a larger reduction in instruction counts (tested on i965).
    */
   nir_foreach_variable(var, input_list) {

      /* Only remap things that aren't builtins.
       * TODO: add TES patch support.
       */
      if (var->data.location >= VARYING_SLOT_VAR0 &&
          var->data.location - VARYING_SLOT_VAR0 < 32) {

         /* We can't repack xfb varyings. */
         if (var->data.always_active_io)
            continue;

         const struct glsl_type *type = var->type;
         if (nir_is_per_vertex_io(var, consumer->info.stage)) {
            assert(glsl_type_is_array(type));
            type = glsl_get_array_element(type);
         }

         /* Skip types that require more complex packing handling.
          * TODO: add support for these types.
          */
         if (glsl_type_is_array(type) ||
             glsl_type_is_dual_slot(type) ||
             glsl_type_is_matrix(type) ||
             glsl_type_is_struct(type) ||
             glsl_type_is_64bit(type))
            continue;

         /* We ignore complex types above and all other vector types should
          * have been split into scalar variables by the lower_io_to_scalar
          * pass. The only exception should by OpenGL xfb varyings.
          */
         if (glsl_get_vector_elements(type) != 1)
            continue;

         unsigned location = var->data.location - VARYING_SLOT_VAR0;
         uint8_t used_comps = comps[location];

         /* If there are no empty components there is nothing more for us to do.
          */
         if (used_comps == 0xf)
            continue;

         bool found_new_offset = false;
         uint8_t interp = get_interp_type(var, type, default_to_smooth_interp);
         for (; cursor[interp] < 32; cursor[interp]++) {
            uint8_t cursor_used_comps = comps[cursor[interp]];

            /* We couldn't find anywhere to pack the varying continue on. */
            if (cursor[interp] == location &&
                (var->data.location_frac == 0 ||
                 cursor_used_comps & ((1 << (var->data.location_frac)) - 1)))
               break;

            /* We can only pack varyings with matching interpolation types */
            if (interp_type[cursor[interp]] != interp)
               continue;

            /* Interpolation loc must match also.
             * TODO: i965 can handle these if they don't match, but the
             * radeonsi nir backend handles everything as vec4s and so expects
             * this to be the same for all components. We could make this
             * check driver specfific or drop it if NIR ever become the only
             * radeonsi backend.
             */
            if (interp_loc[cursor[interp]] != get_interp_loc(var))
               continue;

            /* If the slot is empty just skip it for now, compact_var_list()
             * can be called after this function to remove empty slots for us.
             * TODO: finish implementing compact_var_list() requires array and
             * matrix splitting.
             */
            if (!cursor_used_comps)
               continue;

            uint8_t unused_comps = ~cursor_used_comps;

            for (unsigned i = 0; i < 4; i++) {
               uint8_t new_var_comps = 1 << i;
               if (unused_comps & new_var_comps) {
                  remap[location][var->data.location_frac].component = i;
                  remap[location][var->data.location_frac].location =
                     cursor[interp] + VARYING_SLOT_VAR0;

                  found_new_offset = true;

                  /* Turn off the mask for the component we are remapping */
                  if (comps[location] & 1 << var->data.location_frac) {
                     comps[location] ^= 1 << var->data.location_frac;
                     comps[cursor[interp]] |= new_var_comps;
                  }
                  break;
               }
            }

            if (found_new_offset)
               break;
         }
      }
   }

   uint64_t zero = 0;
   remap_slots_and_components(input_list, consumer->info.stage, remap,
                              &consumer->info.inputs_read, &zero);
   remap_slots_and_components(output_list, producer->info.stage, remap,
                              &producer->info.outputs_written,
                              &producer->info.outputs_read);
}

/* We assume that this has been called more-or-less directly after
 * remove_unused_varyings.  At this point, all of the varyings that we
 * aren't going to be using have been completely removed and the
 * inputs_read and outputs_written fields in nir_shader_info reflect
 * this.  Therefore, the total set of valid slots is the OR of the two
 * sets of varyings;  this accounts for varyings which one side may need
 * to read/write even if the other doesn't.  This can happen if, for
 * instance, an array is used indirectly from one side causing it to be
 * unsplittable but directly from the other.
 */
void
nir_compact_varyings(nir_shader *producer, nir_shader *consumer,
                     bool default_to_smooth_interp)
{
   assert(producer->info.stage != MESA_SHADER_FRAGMENT);
   assert(consumer->info.stage != MESA_SHADER_VERTEX);

   uint8_t comps[32] = {0};
   uint8_t interp_type[32] = {0};
   uint8_t interp_loc[32] = {0};

   get_slot_component_masks_and_interp_types(&producer->outputs, comps,
                                             interp_type, interp_loc,
                                             producer->info.stage,
                                             default_to_smooth_interp);
   get_slot_component_masks_and_interp_types(&consumer->inputs, comps,
                                             interp_type, interp_loc,
                                             consumer->info.stage,
                                             default_to_smooth_interp);

   compact_components(producer, consumer, comps, interp_type, interp_loc,
                      default_to_smooth_interp);
}

/*
 * Mark XFB varyings as always_active_io in the consumer so the linking opts
 * don't touch them.
 */
void
nir_link_xfb_varyings(nir_shader *producer, nir_shader *consumer)
{
   nir_variable *input_vars[MAX_VARYING] = { 0 };

   nir_foreach_variable(var, &consumer->inputs) {
      if (var->data.location >= VARYING_SLOT_VAR0 &&
          var->data.location - VARYING_SLOT_VAR0 < MAX_VARYING) {

         unsigned location = var->data.location - VARYING_SLOT_VAR0;
         input_vars[location] = var;
      }
   }

   nir_foreach_variable(var, &producer->outputs) {
      if (var->data.location >= VARYING_SLOT_VAR0 &&
          var->data.location - VARYING_SLOT_VAR0 < MAX_VARYING) {

         if (!var->data.always_active_io)
            continue;

         unsigned location = var->data.location - VARYING_SLOT_VAR0;
         if (input_vars[location]) {
            input_vars[location]->data.always_active_io = true;
         }
      }
   }
}

static bool
does_varying_match(nir_variable *out_var, nir_variable *in_var)
{
   return in_var->data.location == out_var->data.location &&
          in_var->data.location_frac == out_var->data.location_frac;
}

static nir_variable *
get_matching_input_var(nir_shader *consumer, nir_variable *out_var)
{
   nir_foreach_variable(var, &consumer->inputs) {
      if (does_varying_match(out_var, var))
         return var;
   }

   return NULL;
}

static bool
can_replace_varying(nir_variable *out_var)
{
   /* Skip types that require more complex handling.
    * TODO: add support for these types.
    */
   if (glsl_type_is_array(out_var->type) ||
       glsl_type_is_dual_slot(out_var->type) ||
       glsl_type_is_matrix(out_var->type) ||
       glsl_type_is_struct(out_var->type))
      return false;

   /* Limit this pass to scalars for now to keep things simple. Most varyings
    * should have been lowered to scalars at this point anyway.
    */
   if (!glsl_type_is_scalar(out_var->type))
      return false;

   if (out_var->data.location < VARYING_SLOT_VAR0 ||
       out_var->data.location - VARYING_SLOT_VAR0 >= MAX_VARYING)
      return false;

   return true;
}

static bool
replace_constant_input(nir_shader *shader, nir_intrinsic_instr *store_intr)
{
   nir_function_impl *impl = nir_shader_get_entrypoint(shader);

   nir_builder b;
   nir_builder_init(&b, impl);

   nir_variable *out_var =
      nir_deref_instr_get_variable(nir_src_as_deref(store_intr->src[0]));

   bool progress = false;
   nir_foreach_block(block, impl) {
      nir_foreach_instr(instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
         if (intr->intrinsic != nir_intrinsic_load_deref)
            continue;

         nir_deref_instr *in_deref = nir_src_as_deref(intr->src[0]);
         if (in_deref->mode != nir_var_shader_in)
            continue;

         nir_variable *in_var = nir_deref_instr_get_variable(in_deref);

         if (!does_varying_match(out_var, in_var))
            continue;

         b.cursor = nir_before_instr(instr);

         nir_load_const_instr *out_const =
            nir_instr_as_load_const(store_intr->src[1].ssa->parent_instr);

         /* Add new const to replace the input */
         nir_ssa_def *nconst = nir_build_imm(&b, store_intr->num_components,
                                             intr->dest.ssa.bit_size,
                                             out_const->value);

         nir_ssa_def_rewrite_uses(&intr->dest.ssa, nir_src_for_ssa(nconst));

         progress = true;
      }
   }

   return progress;
}

static bool
replace_duplicate_input(nir_shader *shader, nir_variable *input_var,
                         nir_intrinsic_instr *dup_store_intr)
{
   assert(input_var);

   nir_function_impl *impl = nir_shader_get_entrypoint(shader);

   nir_builder b;
   nir_builder_init(&b, impl);

   nir_variable *dup_out_var =
      nir_deref_instr_get_variable(nir_src_as_deref(dup_store_intr->src[0]));

   bool progress = false;
   nir_foreach_block(block, impl) {
      nir_foreach_instr(instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
         if (intr->intrinsic != nir_intrinsic_load_deref)
            continue;

         nir_deref_instr *in_deref = nir_src_as_deref(intr->src[0]);
         if (in_deref->mode != nir_var_shader_in)
            continue;

         nir_variable *in_var = nir_deref_instr_get_variable(in_deref);

         if (!does_varying_match(dup_out_var, in_var) ||
             in_var->data.interpolation != input_var->data.interpolation ||
             get_interp_loc(in_var) != get_interp_loc(input_var))
            continue;

         b.cursor = nir_before_instr(instr);

         nir_ssa_def *load = nir_load_var(&b, input_var);
         nir_ssa_def_rewrite_uses(&intr->dest.ssa, nir_src_for_ssa(load));

         progress = true;
      }
   }

   return progress;
}

bool
nir_link_opt_varyings(nir_shader *producer, nir_shader *consumer)
{
   /* TODO: Add support for more shader stage combinations */
   if (consumer->info.stage != MESA_SHADER_FRAGMENT ||
       (producer->info.stage != MESA_SHADER_VERTEX &&
        producer->info.stage != MESA_SHADER_TESS_EVAL))
      return false;

   bool progress = false;

   nir_function_impl *impl = nir_shader_get_entrypoint(producer);

   struct hash_table *varying_values = _mesa_pointer_hash_table_create(NULL);

   /* If we find a store in the last block of the producer we can be sure this
    * is the only possible value for this output.
    */
   nir_block *last_block = nir_impl_last_block(impl);
   nir_foreach_instr_reverse(instr, last_block) {
      if (instr->type != nir_instr_type_intrinsic)
         continue;

      nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

      if (intr->intrinsic != nir_intrinsic_store_deref)
         continue;

      nir_deref_instr *out_deref = nir_src_as_deref(intr->src[0]);
      if (out_deref->mode != nir_var_shader_out)
         continue;

      nir_variable *out_var = nir_deref_instr_get_variable(out_deref);
      if (!can_replace_varying(out_var))
         continue;

      if (intr->src[1].ssa->parent_instr->type == nir_instr_type_load_const) {
         progress |= replace_constant_input(consumer, intr);
      } else {
         struct hash_entry *entry =
               _mesa_hash_table_search(varying_values, intr->src[1].ssa);
         if (entry) {
            progress |= replace_duplicate_input(consumer,
                                                (nir_variable *) entry->data,
                                                intr);
         } else {
            nir_variable *in_var = get_matching_input_var(consumer, out_var);
            if (in_var) {
               _mesa_hash_table_insert(varying_values, intr->src[1].ssa,
                                       in_var);
            }
         }
      }
   }

   _mesa_hash_table_destroy(varying_values, NULL);

   return progress;
}
