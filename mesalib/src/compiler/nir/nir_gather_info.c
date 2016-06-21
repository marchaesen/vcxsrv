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

static void
gather_intrinsic_info(nir_intrinsic_instr *instr, nir_shader *shader)
{
   switch (instr->intrinsic) {
   case nir_intrinsic_discard:
   case nir_intrinsic_discard_if:
      assert(shader->stage == MESA_SHADER_FRAGMENT);
      shader->info.fs.uses_discard = true;
      break;

   case nir_intrinsic_load_front_face:
   case nir_intrinsic_load_vertex_id:
   case nir_intrinsic_load_vertex_id_zero_base:
   case nir_intrinsic_load_base_vertex:
   case nir_intrinsic_load_instance_id:
   case nir_intrinsic_load_sample_id:
   case nir_intrinsic_load_sample_pos:
   case nir_intrinsic_load_sample_mask_in:
   case nir_intrinsic_load_primitive_id:
   case nir_intrinsic_load_invocation_id:
   case nir_intrinsic_load_local_invocation_id:
   case nir_intrinsic_load_local_invocation_index:
   case nir_intrinsic_load_work_group_id:
   case nir_intrinsic_load_num_work_groups:
      shader->info.system_values_read |=
         (1 << nir_system_value_from_intrinsic(instr->intrinsic));
      break;

   case nir_intrinsic_end_primitive:
   case nir_intrinsic_end_primitive_with_counter:
      assert(shader->stage == MESA_SHADER_GEOMETRY);
      shader->info.gs.uses_end_primitive = 1;
      break;

   default:
      break;
   }
}

static void
gather_tex_info(nir_tex_instr *instr, nir_shader *shader)
{
   if (instr->op == nir_texop_tg4)
      shader->info.uses_texture_gather = true;
}

static void
gather_info_block(nir_block *block, nir_shader *shader)
{
   nir_foreach_instr(instr, block) {
      switch (instr->type) {
      case nir_instr_type_intrinsic:
         gather_intrinsic_info(nir_instr_as_intrinsic(instr), shader);
         break;
      case nir_instr_type_tex:
         gather_tex_info(nir_instr_as_tex(instr), shader);
         break;
      case nir_instr_type_call:
         assert(!"nir_shader_gather_info only works if functions are inlined");
         break;
      default:
         break;
      }
   }
}

/**
 * Returns the bits in the inputs_read, outputs_written, or
 * system_values_read bitfield corresponding to this variable.
 */
static inline uint64_t
get_io_mask(nir_variable *var, gl_shader_stage stage)
{
   assert(var->data.mode == nir_var_shader_in ||
          var->data.mode == nir_var_shader_out ||
          var->data.mode == nir_var_system_value);
   assert(var->data.location >= 0);

   const struct glsl_type *var_type = var->type;
   if (stage == MESA_SHADER_GEOMETRY && var->data.mode == nir_var_shader_in) {
      /* Most geometry shader inputs are per-vertex arrays */
      if (var->data.location >= VARYING_SLOT_VAR0)
         assert(glsl_type_is_array(var_type));

      if (glsl_type_is_array(var_type))
         var_type = glsl_get_array_element(var_type);
   }

   bool is_vertex_input = (var->data.mode == nir_var_shader_in &&
                           stage == MESA_SHADER_VERTEX);
   unsigned slots = glsl_count_attribute_slots(var_type, is_vertex_input);
   return ((1ull << slots) - 1) << var->data.location;
}

void
nir_shader_gather_info(nir_shader *shader, nir_function_impl *entrypoint)
{
   /* This pass does not yet support tessellation shaders */
   assert(shader->stage == MESA_SHADER_VERTEX ||
          shader->stage == MESA_SHADER_GEOMETRY ||
          shader->stage == MESA_SHADER_FRAGMENT ||
          shader->stage == MESA_SHADER_COMPUTE);

   bool uses_sample_qualifier = false;
   shader->info.inputs_read = 0;
   foreach_list_typed(nir_variable, var, node, &shader->inputs) {
      shader->info.inputs_read |= get_io_mask(var, shader->stage);
      uses_sample_qualifier |= var->data.sample;
   }

   if (shader->stage == MESA_SHADER_FRAGMENT)
      shader->info.fs.uses_sample_qualifier = uses_sample_qualifier;

   /* TODO: Some day we may need to add stream support to NIR */
   shader->info.outputs_written = 0;
   foreach_list_typed(nir_variable, var, node, &shader->outputs)
      shader->info.outputs_written |= get_io_mask(var, shader->stage);

   shader->info.system_values_read = 0;
   foreach_list_typed(nir_variable, var, node, &shader->system_values)
      shader->info.system_values_read |= get_io_mask(var, shader->stage);

   shader->info.num_textures = 0;
   shader->info.num_images = 0;
   nir_foreach_variable(var, &shader->uniforms) {
      const struct glsl_type *type = var->type;
      unsigned count = 1;
      if (glsl_type_is_array(type)) {
         count = glsl_get_length(type);
         type = glsl_get_array_element(type);
      }

      if (glsl_type_is_image(type)) {
         shader->info.num_images += count;
      } else if (glsl_type_is_sampler(type)) {
         shader->info.num_textures += count;
      }
   }

   nir_foreach_block(block, entrypoint) {
      gather_info_block(block, shader);
   }
}
