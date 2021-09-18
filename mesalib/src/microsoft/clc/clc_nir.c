/*
 * Copyright © Microsoft Corporation
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "u_math.h"
#include "nir.h"
#include "glsl_types.h"
#include "nir_types.h"
#include "nir_builder.h"

#include "clc_nir.h"
#include "clc_compiler.h"
#include "../compiler/dxil_nir.h"

static bool
lower_load_base_global_invocation_id(nir_builder *b, nir_intrinsic_instr *intr,
                                    nir_variable *var)
{
   b->cursor = nir_after_instr(&intr->instr);

   nir_ssa_def *offset =
      build_load_ubo_dxil(b, nir_imm_int(b, var->data.binding),
                          nir_imm_int(b,
                                      offsetof(struct clc_work_properties_data,
                                               global_offset_x)),
                          nir_dest_num_components(intr->dest),
                          nir_dest_bit_size(intr->dest));
   nir_ssa_def_rewrite_uses(&intr->dest.ssa, offset);
   nir_instr_remove(&intr->instr);
   return true;
}

static bool
lower_load_work_dim(nir_builder *b, nir_intrinsic_instr *intr,
                    nir_variable *var)
{
   b->cursor = nir_after_instr(&intr->instr);

   nir_ssa_def *dim =
      build_load_ubo_dxil(b, nir_imm_int(b, var->data.binding),
                          nir_imm_int(b,
                                      offsetof(struct clc_work_properties_data,
                                               work_dim)),
                          nir_dest_num_components(intr->dest),
                          nir_dest_bit_size(intr->dest));
   nir_ssa_def_rewrite_uses(&intr->dest.ssa, dim);
   nir_instr_remove(&intr->instr);
   return true;
}

static bool
lower_load_local_group_size(nir_builder *b, nir_intrinsic_instr *intr)
{
   b->cursor = nir_after_instr(&intr->instr);

   nir_const_value v[3] = {
      nir_const_value_for_int(b->shader->info.workgroup_size[0], 32),
      nir_const_value_for_int(b->shader->info.workgroup_size[1], 32),
      nir_const_value_for_int(b->shader->info.workgroup_size[2], 32)
   };
   nir_ssa_def *size = nir_build_imm(b, 3, 32, v);
   nir_ssa_def_rewrite_uses(&intr->dest.ssa, size);
   nir_instr_remove(&intr->instr);
   return true;
}

static bool
lower_load_num_workgroups(nir_builder *b, nir_intrinsic_instr *intr,
                          nir_variable *var)
{
   b->cursor = nir_after_instr(&intr->instr);

   nir_ssa_def *count =
      build_load_ubo_dxil(b, nir_imm_int(b, var->data.binding),
                         nir_imm_int(b,
                                     offsetof(struct clc_work_properties_data,
                                              group_count_total_x)),
                         nir_dest_num_components(intr->dest),
                         nir_dest_bit_size(intr->dest));
   nir_ssa_def_rewrite_uses(&intr->dest.ssa, count);
   nir_instr_remove(&intr->instr);
   return true;
}

static bool
lower_load_base_workgroup_id(nir_builder *b, nir_intrinsic_instr *intr,
                             nir_variable *var)
{
   b->cursor = nir_after_instr(&intr->instr);

   nir_ssa_def *offset =
      build_load_ubo_dxil(b, nir_imm_int(b, var->data.binding),
                         nir_imm_int(b,
                                     offsetof(struct clc_work_properties_data,
                                              group_id_offset_x)),
                         nir_dest_num_components(intr->dest),
                         nir_dest_bit_size(intr->dest));
   nir_ssa_def_rewrite_uses(&intr->dest.ssa, offset);
   nir_instr_remove(&intr->instr);
   return true;
}

bool
clc_nir_lower_system_values(nir_shader *nir, nir_variable *var)
{
   bool progress = false;

   foreach_list_typed(nir_function, func, node, &nir->functions) {
      if (!func->is_entrypoint)
         continue;
      assert(func->impl);

      nir_builder b;
      nir_builder_init(&b, func->impl);

      nir_foreach_block(block, func->impl) {
         nir_foreach_instr_safe(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

            switch (intr->intrinsic) {
            case nir_intrinsic_load_base_global_invocation_id:
               progress |= lower_load_base_global_invocation_id(&b, intr, var);
               break;
            case nir_intrinsic_load_work_dim:
               progress |= lower_load_work_dim(&b, intr, var);
               break;
            case nir_intrinsic_load_workgroup_size:
               lower_load_local_group_size(&b, intr);
               break;
            case nir_intrinsic_load_num_workgroups:
               lower_load_num_workgroups(&b, intr, var);
               break;
            case nir_intrinsic_load_base_workgroup_id:
               lower_load_base_workgroup_id(&b, intr, var);
               break;
            default: break;
            }
         }
      }
   }

   return progress;
}

static bool
lower_load_kernel_input(nir_builder *b, nir_intrinsic_instr *intr,
                        nir_variable *var)
{
   b->cursor = nir_before_instr(&intr->instr);

   unsigned bit_size = nir_dest_bit_size(intr->dest);
   enum glsl_base_type base_type;

   switch (bit_size) {
   case 64:
      base_type = GLSL_TYPE_UINT64;
      break;
   case 32:
      base_type = GLSL_TYPE_UINT;
      break;
    case 16:
      base_type = GLSL_TYPE_UINT16;
      break;
    case 8:
      base_type = GLSL_TYPE_UINT8;
      break;
   }

   const struct glsl_type *type =
      glsl_vector_type(base_type, nir_dest_num_components(intr->dest));
   nir_ssa_def *ptr = nir_vec2(b, nir_imm_int(b, var->data.binding),
                                  nir_u2u(b, intr->src[0].ssa, 32));
   nir_deref_instr *deref = nir_build_deref_cast(b, ptr, nir_var_mem_ubo, type,
                                                    bit_size / 8);
   deref->cast.align_mul = nir_intrinsic_align_mul(intr);
   deref->cast.align_offset = nir_intrinsic_align_offset(intr);

   nir_ssa_def *result =
      nir_load_deref(b, deref);
   nir_ssa_def_rewrite_uses(&intr->dest.ssa, result);
   nir_instr_remove(&intr->instr);
   return true;
}

bool
clc_nir_lower_kernel_input_loads(nir_shader *nir, nir_variable *var)
{
   bool progress = false;

   foreach_list_typed(nir_function, func, node, &nir->functions) {
      if (!func->is_entrypoint)
         continue;
      assert(func->impl);

      nir_builder b;
      nir_builder_init(&b, func->impl);

      nir_foreach_block(block, func->impl) {
         nir_foreach_instr_safe(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

            if (intr->intrinsic == nir_intrinsic_load_kernel_input)
               progress |= lower_load_kernel_input(&b, intr, var);
         }
      }
   }

   return progress;
}


static nir_variable *
add_printf_var(struct nir_shader *nir, unsigned uav_id)
{
   /* This size is arbitrary. Minimum required per spec is 1MB */
   const unsigned max_printf_size = 1 * 1024 * 1024;
   const unsigned printf_array_size = max_printf_size / sizeof(unsigned);
   nir_variable *var =
      nir_variable_create(nir, nir_var_mem_ssbo,
                          glsl_array_type(glsl_uint_type(), printf_array_size, sizeof(unsigned)),
                          "printf");
   var->data.binding = uav_id;
   return var;
}

bool
clc_lower_printf_base(nir_shader *nir, unsigned uav_id)
{
   nir_variable *printf_var = NULL;
   nir_ssa_def *printf_deref = NULL;
   nir_foreach_function(func, nir) {
      nir_builder b;
      nir_builder_init(&b, func->impl);
      b.cursor = nir_before_instr(nir_block_first_instr(nir_start_block(func->impl)));
      bool progress = false;

      nir_foreach_block(block, func->impl) {
         nir_foreach_instr_safe(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;
            nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
            if (intrin->intrinsic != nir_intrinsic_load_printf_buffer_address)
               continue;

            if (!printf_var) {
               printf_var = add_printf_var(nir, uav_id);
               nir_deref_instr *deref = nir_build_deref_var(&b, printf_var);
               printf_deref = &deref->dest.ssa;
            }
            nir_ssa_def_rewrite_uses(&intrin->dest.ssa, printf_deref);
            progress = true;
         }
      }

      if (progress)
         nir_metadata_preserve(func->impl, nir_metadata_loop_analysis |
                                           nir_metadata_block_index |
                                           nir_metadata_dominance);
      else
         nir_metadata_preserve(func->impl, nir_metadata_all);
   }

   return printf_var != NULL;
}

static nir_variable *
find_identical_const_sampler(nir_shader *nir, nir_variable *sampler)
{
   nir_foreach_variable_with_modes(uniform, nir, nir_var_uniform) {
      if (!glsl_type_is_sampler(uniform->type) || !uniform->data.sampler.is_inline_sampler)
         continue;
      if (uniform->data.sampler.addressing_mode == sampler->data.sampler.addressing_mode &&
          uniform->data.sampler.normalized_coordinates == sampler->data.sampler.normalized_coordinates &&
          uniform->data.sampler.filter_mode == sampler->data.sampler.filter_mode)
         return uniform;
   }
   unreachable("Should have at least found the input sampler");
}

static bool
clc_nir_dedupe_const_samplers_instr(nir_builder *b,
                                    nir_instr *instr,
                                    void *cb_data)
{
   nir_shader *nir = cb_data;
   if (instr->type != nir_instr_type_tex)
      return false;

   nir_tex_instr *tex = nir_instr_as_tex(instr);
   int sampler_idx = nir_tex_instr_src_index(tex, nir_tex_src_sampler_deref);
   if (sampler_idx == -1)
      return false;

   nir_deref_instr *deref = nir_src_as_deref(tex->src[sampler_idx].src);
   nir_variable *sampler = nir_deref_instr_get_variable(deref);
   if (!sampler)
      return false;

   assert(sampler->data.mode == nir_var_uniform);

   if (!sampler->data.sampler.is_inline_sampler)
      return false;

   nir_variable *replacement = find_identical_const_sampler(nir, sampler);
   if (replacement == sampler)
      return false;

   b->cursor = nir_before_instr(&tex->instr);
   nir_deref_instr *replacement_deref = nir_build_deref_var(b, replacement);
   nir_instr_rewrite_src(&tex->instr, &tex->src[sampler_idx].src,
                         nir_src_for_ssa(&replacement_deref->dest.ssa));
   nir_deref_instr_remove_if_unused(deref);

   return true;
}

bool
clc_nir_dedupe_const_samplers(nir_shader *nir)
{
   return nir_shader_instructions_pass(nir,
                                       clc_nir_dedupe_const_samplers_instr,
                                       nir_metadata_block_index |
                                       nir_metadata_dominance,
                                       nir);
}
