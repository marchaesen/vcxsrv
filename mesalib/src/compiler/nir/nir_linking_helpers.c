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
      if (function->impl) {
         nir_foreach_block(block, function->impl) {
            nir_foreach_instr(instr, block) {
               if (instr->type != nir_instr_type_intrinsic)
                  continue;

               nir_intrinsic_instr *intrin_instr =
                  nir_instr_as_intrinsic(instr);
               if (intrin_instr->intrinsic == nir_intrinsic_load_var &&
                   intrin_instr->variables[0]->var->data.mode ==
                   nir_var_shader_out) {

                  nir_variable *var = intrin_instr->variables[0]->var;
                  if (var->data.patch) {
                     patches_read[var->data.location_frac] |=
                        get_variable_io_mask(intrin_instr->variables[0]->var,
                                             shader->info.stage);
                  } else {
                     read[var->data.location_frac] |=
                        get_variable_io_mask(intrin_instr->variables[0]->var,
                                             shader->info.stage);
                  }
               }
            }
         }
      }
   }
}

static bool
remove_unused_io_vars(nir_shader *shader, struct exec_list *var_list,
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
         var->data.mode = nir_var_global;

         exec_node_remove(&var->node);
         exec_list_push_tail(&shader->globals, &var->node);

         progress = true;
      }
   }

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
   progress = remove_unused_io_vars(producer, &producer->outputs, read,
                                    patches_read);

   progress = remove_unused_io_vars(consumer, &consumer->inputs, written,
                                    patches_written) || progress;

   return progress;
}
