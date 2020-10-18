/*
 * Copyright 2017 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * Remap load_uniform intrinsics to UBO accesses of UBO binding point 0.
 * Simultaneously, remap existing UBO accesses by increasing their binding
 * point by 1.
 *
 * Note that nir_intrinsic_load_uniform base/ranges can be set in different
 * units, and the multiplier argument caters to supporting these different
 * units.
 *
 * For example:
 * - st_glsl_to_nir for PIPE_CAP_PACKED_UNIFORMS uses dwords (4 bytes) so the
 *   multiplier should be 4
 * - st_glsl_to_nir for !PIPE_CAP_PACKED_UNIFORMS uses vec4s so the
 *   multiplier should be 16
 * - tgsi_to_nir uses vec4s, so the multiplier should be 16
 */

#include "nir.h"
#include "nir_builder.h"

static bool
lower_instr(nir_intrinsic_instr *instr, nir_builder *b, int multiplier)
{
   b->cursor = nir_before_instr(&instr->instr);

   /* Increase all UBO binding points by 1. */
   if (instr->intrinsic == nir_intrinsic_load_ubo &&
       !b->shader->info.first_ubo_is_default_ubo) {
      nir_ssa_def *old_idx = nir_ssa_for_src(b, instr->src[0], 1);
      nir_ssa_def *new_idx = nir_iadd(b, old_idx, nir_imm_int(b, 1));
      nir_instr_rewrite_src(&instr->instr, &instr->src[0],
                            nir_src_for_ssa(new_idx));
      return true;
   }

   if (instr->intrinsic == nir_intrinsic_load_uniform) {
      nir_ssa_def *ubo_idx = nir_imm_int(b, 0);
      nir_ssa_def *ubo_offset =
         nir_iadd(b, nir_imm_int(b, multiplier * nir_intrinsic_base(instr)),
                  nir_imul(b, nir_imm_int(b, multiplier),
                           nir_ssa_for_src(b, instr->src[0], 1)));

      nir_intrinsic_instr *load =
         nir_intrinsic_instr_create(b->shader, nir_intrinsic_load_ubo);
      load->num_components = instr->num_components;
      load->src[0] = nir_src_for_ssa(ubo_idx);
      load->src[1] = nir_src_for_ssa(ubo_offset);
      assert(instr->dest.ssa.bit_size >= 8);

      /* If it's const, set the alignment to our known constant offset.  If
       * not, set it to a pessimistic value based on the multiplier (or the
       * scalar size, for qword loads).
       *
       * We could potentially set up stricter alignments for indirects by
       * knowing what features are enabled in the APIs (see comment in
       * nir_lower_ubo_vec4.c)
       */
      if (nir_src_is_const(instr->src[0])) {
         nir_intrinsic_set_align(load, NIR_ALIGN_MUL_MAX,
                                 (nir_src_as_uint(instr->src[0]) +
                                  nir_intrinsic_base(instr) * multiplier) %
                                 NIR_ALIGN_MUL_MAX);
      } else {
         nir_intrinsic_set_align(load, MAX2(multiplier,
                                            instr->dest.ssa.bit_size / 8), 0);
      }
      nir_ssa_dest_init(&load->instr, &load->dest,
                        load->num_components, instr->dest.ssa.bit_size,
                        instr->dest.ssa.name);
      nir_builder_instr_insert(b, &load->instr);
      nir_ssa_def_rewrite_uses(&instr->dest.ssa, nir_src_for_ssa(&load->dest.ssa));

      nir_intrinsic_set_range_base(load, nir_intrinsic_base(instr) * multiplier);
      nir_intrinsic_set_range(load, nir_intrinsic_range(instr) * multiplier);

      nir_instr_remove(&instr->instr);
      return true;
   }

   return false;
}

bool
nir_lower_uniforms_to_ubo(nir_shader *shader, int multiplier)
{
   bool progress = false;

   nir_foreach_function(function, shader) {
      if (function->impl) {
         nir_builder builder;
         nir_builder_init(&builder, function->impl);
         nir_foreach_block(block, function->impl) {
            nir_foreach_instr_safe(instr, block) {
               if (instr->type == nir_instr_type_intrinsic)
                  progress |= lower_instr(nir_instr_as_intrinsic(instr),
                                          &builder,
                                          multiplier);
            }
         }

         nir_metadata_preserve(function->impl, nir_metadata_block_index |
                                               nir_metadata_dominance);
      }
   }

   if (progress) {
      if (!shader->info.first_ubo_is_default_ubo) {
         nir_foreach_variable_with_modes(var, shader, nir_var_mem_ubo) {
            var->data.binding++;
            /* only increment location for ubo arrays */
            if (glsl_without_array(var->type) == var->interface_type &&
                glsl_type_is_array(var->type))
               var->data.location++;
         }
      }
      shader->info.num_ubos++;

      if (shader->num_uniforms > 0) {
         const struct glsl_type *type = glsl_array_type(glsl_vec4_type(),
                                                        shader->num_uniforms, 0);
         nir_variable *ubo = nir_variable_create(shader, nir_var_mem_ubo, type,
                                                 "uniform_0");
         ubo->data.binding = 0;

         struct glsl_struct_field field = {
            .type = type,
            .name = "data",
            .location = -1,
         };
         ubo->interface_type =
               glsl_interface_type(&field, 1, GLSL_INTERFACE_PACKING_STD430,
                                   false, "__ubo0_interface");
      }
   }

   shader->info.first_ubo_is_default_ubo = true;
   return progress;
}
