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

static nir_ssa_def*
build_local_group_size(nir_builder *b)
{
   nir_ssa_def *local_size;

   /*
    * If the local work group size is variable it can't be lowered at this
    * point, but its intrinsic can still be used.
    */
   if (b->shader->info.cs.local_size_variable) {
      local_size = nir_load_local_group_size(b);
   } else {
      nir_const_value local_size_const;
      memset(&local_size_const, 0, sizeof(local_size_const));
      local_size_const.u32[0] = b->shader->info.cs.local_size[0];
      local_size_const.u32[1] = b->shader->info.cs.local_size[1];
      local_size_const.u32[2] = b->shader->info.cs.local_size[2];
      local_size = nir_build_imm(b, 3, 32, local_size_const);
   }

   return local_size;
}

static nir_ssa_def *
build_local_invocation_id(nir_builder *b)
{
   if (b->shader->options->lower_cs_local_id_from_index) {
      /* We lower gl_LocalInvocationID from gl_LocalInvocationIndex based
       * on this formula:
       *
       *    gl_LocalInvocationID.x =
       *       gl_LocalInvocationIndex % gl_WorkGroupSize.x;
       *    gl_LocalInvocationID.y =
       *       (gl_LocalInvocationIndex / gl_WorkGroupSize.x) %
       *       gl_WorkGroupSize.y;
       *    gl_LocalInvocationID.z =
       *       (gl_LocalInvocationIndex /
       *        (gl_WorkGroupSize.x * gl_WorkGroupSize.y)) %
       *       gl_WorkGroupSize.z;
       *
       * However, the final % gl_WorkGroupSize.z does nothing unless we
       * accidentally end up with a gl_LocalInvocationIndex that is too
       * large so it can safely be omitted.
       */
      nir_ssa_def *local_index = nir_load_local_invocation_index(b);
      nir_ssa_def *local_size = build_local_group_size(b);

      nir_ssa_def *id_x, *id_y, *id_z;
      id_x = nir_umod(b, local_index,
                         nir_channel(b, local_size, 0));
      id_y = nir_umod(b, nir_udiv(b, local_index,
                                     nir_channel(b, local_size, 0)),
                         nir_channel(b, local_size, 1));
      id_z = nir_udiv(b, local_index,
                         nir_imul(b, nir_channel(b, local_size, 0),
                                     nir_channel(b, local_size, 1)));
      return nir_vec3(b, id_x, id_y, id_z);
   } else {
      return nir_load_local_invocation_id(b);
   }
}

static bool
convert_block(nir_block *block, nir_builder *b)
{
   bool progress = false;

   nir_foreach_instr_safe(instr, block) {
      if (instr->type != nir_instr_type_intrinsic)
         continue;

      nir_intrinsic_instr *load_deref = nir_instr_as_intrinsic(instr);
      if (load_deref->intrinsic != nir_intrinsic_load_deref)
         continue;

      nir_deref_instr *deref = nir_src_as_deref(load_deref->src[0]);
      if (deref->mode != nir_var_system_value)
         continue;

      if (deref->deref_type != nir_deref_type_var) {
         /* The only one system value that is an array and that is
          * gl_SampleMask which is always an array of one element.
          */
         assert(deref->deref_type == nir_deref_type_array);
         deref = nir_deref_instr_parent(deref);
         assert(deref->deref_type == nir_deref_type_var);
         assert(deref->var->data.location == SYSTEM_VALUE_SAMPLE_MASK_IN);
      }
      nir_variable *var = deref->var;

      b->cursor = nir_after_instr(&load_deref->instr);

      nir_ssa_def *sysval = NULL;
      switch (var->data.location) {
      case SYSTEM_VALUE_GLOBAL_INVOCATION_ID: {
         /* From the GLSL man page for gl_GlobalInvocationID:
          *
          *    "The value of gl_GlobalInvocationID is equal to
          *    gl_WorkGroupID * gl_WorkGroupSize + gl_LocalInvocationID"
          */
         nir_ssa_def *group_size = build_local_group_size(b);
         nir_ssa_def *group_id = nir_load_work_group_id(b);
         nir_ssa_def *local_id = build_local_invocation_id(b);

         sysval = nir_iadd(b, nir_imul(b, group_id, group_size), local_id);
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

      case SYSTEM_VALUE_LOCAL_INVOCATION_ID:
         /* If lower_cs_local_id_from_index is true, then we derive the local
          * index from the local id.
          */
         if (b->shader->options->lower_cs_local_id_from_index)
            sysval = build_local_invocation_id(b);
         break;

      case SYSTEM_VALUE_LOCAL_GROUP_SIZE: {
         sysval = build_local_group_size(b);
         break;
      }

      case SYSTEM_VALUE_VERTEX_ID:
         if (b->shader->options->vertex_id_zero_based) {
            sysval = nir_iadd(b,
                              nir_load_vertex_id_zero_base(b),
                              nir_load_first_vertex(b));
         } else {
            sysval = nir_load_vertex_id(b);
         }
         break;

      case SYSTEM_VALUE_BASE_VERTEX:
         /**
          * From the OpenGL 4.6 (11.1.3.9 Shader Inputs) specification:
          *
          * "gl_BaseVertex holds the integer value passed to the baseVertex
          * parameter to the command that resulted in the current shader
          * invocation. In the case where the command has no baseVertex
          * parameter, the value of gl_BaseVertex is zero."
          */
         if (b->shader->options->lower_base_vertex)
            sysval = nir_iand(b,
                              nir_load_is_indexed_draw(b),
                              nir_load_first_vertex(b));
         break;

      case SYSTEM_VALUE_HELPER_INVOCATION:
         if (b->shader->options->lower_helper_invocation) {
            nir_ssa_def *tmp;

            tmp = nir_ishl(b,
                           nir_imm_int(b, 1),
                           nir_load_sample_id_no_per_sample(b));

            tmp = nir_iand(b,
                           nir_load_sample_mask_in(b),
                           tmp);

            sysval = nir_inot(b, nir_i2b(b, tmp));
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

      case SYSTEM_VALUE_GLOBAL_GROUP_SIZE: {
         nir_ssa_def *group_size = build_local_group_size(b);
         nir_ssa_def *num_work_groups = nir_load_num_work_groups(b);
         sysval = nir_imul(b, group_size, num_work_groups);
         break;
      }

      default:
         break;
      }

      if (sysval == NULL) {
         nir_intrinsic_op sysval_op =
            nir_intrinsic_from_system_value(var->data.location);
         sysval = nir_load_system_value(b, sysval_op, 0);
         sysval->bit_size = load_deref->dest.ssa.bit_size;
      }

      nir_ssa_def_rewrite_uses(&load_deref->dest.ssa, nir_src_for_ssa(sysval));
      nir_instr_remove(&load_deref->instr);

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

   /* We're going to delete the variables so we need to clean up all those
    * derefs we left lying around.
    */
   nir_remove_dead_derefs(shader);

   exec_list_make_empty(&shader->system_values);

   return progress;
}
