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
 *    Connor Abbott (cwabbott0@gmail.com)
 *
 */

#include "nir.h"
#include "nir_builder.h"

static bool
convert_block(nir_block *block, nir_builder *b)
{
   bool progress = false;

   nir_foreach_instr_safe(instr, block) {
      if (instr->type != nir_instr_type_intrinsic)
         continue;

      nir_intrinsic_instr *load_var = nir_instr_as_intrinsic(instr);

      if (load_var->intrinsic != nir_intrinsic_load_var)
         continue;

      nir_variable *var = load_var->variables[0]->var;
      if (var->data.mode != nir_var_system_value)
         continue;

      b->cursor = nir_after_instr(&load_var->instr);

      nir_ssa_def *sysval = NULL;
      switch (var->data.location) {
      case SYSTEM_VALUE_GLOBAL_INVOCATION_ID: {
         /* From the GLSL man page for gl_GlobalInvocationID:
          *
          *    "The value of gl_GlobalInvocationID is equal to
          *    gl_WorkGroupID * gl_WorkGroupSize + gl_LocalInvocationID"
          */

         nir_const_value local_size;
         memset(&local_size, 0, sizeof(local_size));
         local_size.u32[0] = b->shader->info.cs.local_size[0];
         local_size.u32[1] = b->shader->info.cs.local_size[1];
         local_size.u32[2] = b->shader->info.cs.local_size[2];

         nir_ssa_def *group_id = nir_load_work_group_id(b);
         nir_ssa_def *local_id = nir_load_local_invocation_id(b);

         sysval = nir_iadd(b, nir_imul(b, group_id,
                                       nir_build_imm(b, 3, 32, local_size)),
                              local_id);
         break;
      }

      case SYSTEM_VALUE_LOCAL_INVOCATION_INDEX: {
         /* If lower_cs_local_index_from_id is true, then we derive the local
          * index from the local id.
          */
         if (!b->shader->options->lower_cs_local_index_from_id)
            break;

         /* From the GLSL man page for gl_LocalInvocationIndex:
          *
          *    "The value of gl_LocalInvocationIndex is equal to
          *    gl_LocalInvocationID.z * gl_WorkGroupSize.x *
          *    gl_WorkGroupSize.y + gl_LocalInvocationID.y *
          *    gl_WorkGroupSize.x + gl_LocalInvocationID.x"
          */
         nir_ssa_def *local_id = nir_load_local_invocation_id(b);

         nir_ssa_def *size_x =
            nir_imm_int(b, b->shader->info.cs.local_size[0]);
         nir_ssa_def *size_y =
            nir_imm_int(b, b->shader->info.cs.local_size[1]);

         sysval = nir_imul(b, nir_channel(b, local_id, 2),
                              nir_imul(b, size_x, size_y));
         sysval = nir_iadd(b, sysval,
                              nir_imul(b, nir_channel(b, local_id, 1), size_x));
         sysval = nir_iadd(b, sysval, nir_channel(b, local_id, 0));
         break;
      }

      case SYSTEM_VALUE_VERTEX_ID:
         if (b->shader->options->vertex_id_zero_based) {
            sysval = nir_iadd(b,
                              nir_load_vertex_id_zero_base(b),
                              nir_load_base_vertex(b));
         } else {
            sysval = nir_load_vertex_id(b);
         }
         break;

      case SYSTEM_VALUE_INSTANCE_INDEX:
         sysval = nir_iadd(b,
                           nir_load_instance_id(b),
                           nir_load_base_instance(b));
         break;

      case SYSTEM_VALUE_SUBGROUP_EQ_MASK:
      case SYSTEM_VALUE_SUBGROUP_GE_MASK:
      case SYSTEM_VALUE_SUBGROUP_GT_MASK:
      case SYSTEM_VALUE_SUBGROUP_LE_MASK:
      case SYSTEM_VALUE_SUBGROUP_LT_MASK: {
         nir_intrinsic_op op =
            nir_intrinsic_from_system_value(var->data.location);
         nir_intrinsic_instr *load = nir_intrinsic_instr_create(b->shader, op);
         nir_ssa_dest_init_for_type(&load->instr, &load->dest,
                                    var->type, NULL);
         load->num_components = load->dest.ssa.num_components;
         nir_builder_instr_insert(b, &load->instr);
         sysval = &load->dest.ssa;
         break;
      }

      case SYSTEM_VALUE_DEVICE_INDEX:
         if (b->shader->options->lower_device_index_to_zero)
            sysval = nir_imm_int(b, 0);
         break;

      default:
         break;
      }

      if (sysval == NULL) {
         nir_intrinsic_op sysval_op =
            nir_intrinsic_from_system_value(var->data.location);
         sysval = nir_load_system_value(b, sysval_op, 0);
      }

      nir_ssa_def_rewrite_uses(&load_var->dest.ssa, nir_src_for_ssa(sysval));
      nir_instr_remove(&load_var->instr);

      progress = true;
   }

   return progress;
}

static bool
convert_impl(nir_function_impl *impl)
{
   bool progress = false;
   nir_builder builder;
   nir_builder_init(&builder, impl);

   nir_foreach_block(block, impl) {
      progress |= convert_block(block, &builder);
   }

   nir_metadata_preserve(impl, nir_metadata_block_index |
                               nir_metadata_dominance);
   return progress;
}

bool
nir_lower_system_values(nir_shader *shader)
{
   bool progress = false;

   nir_foreach_function(function, shader) {
      if (function->impl)
         progress = convert_impl(function->impl) || progress;
   }

   exec_list_make_empty(&shader->system_values);

   return progress;
}
