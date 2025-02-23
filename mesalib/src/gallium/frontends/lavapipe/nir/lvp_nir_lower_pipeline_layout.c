/*
 * Copyright © 2019 Red Hat.
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

#include "lvp_private.h"
#include "lvp_nir.h"

static nir_def *lower_vri_intrin_vri(struct nir_builder *b,
                                           nir_instr *instr, void *data_cb)
{
   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
   unsigned desc_set_idx = nir_intrinsic_desc_set(intrin);
   unsigned binding_idx = nir_intrinsic_binding(intrin);
   const struct lvp_descriptor_set_binding_layout *binding =
      get_binding_layout(data_cb, desc_set_idx, binding_idx);

   return nir_vec3(b, nir_imm_int(b, desc_set_idx + 1),
                   nir_iadd_imm(b, intrin->src[0].ssa, binding->descriptor_index),
                   nir_imm_int(b, 0));
}

static nir_def *lower_vri_intrin_vrri(struct nir_builder *b,
                                          nir_instr *instr, void *data_cb)
{
   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
   nir_def *old_index = intrin->src[0].ssa;
   nir_def *delta = intrin->src[1].ssa;
   return nir_vec3(b, nir_channel(b, old_index, 0),
                   nir_iadd(b, nir_channel(b, old_index, 1), delta),
                   nir_channel(b, old_index, 2));
}

static nir_def *lower_vri_intrin_lvd(struct nir_builder *b,
                                         nir_instr *instr, void *data_cb)
{
   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
   return intrin->src[0].ssa;
}

static void
lower_buffer(nir_builder *b, nir_intrinsic_instr *intr, uint32_t src_index)
{
   if (nir_src_num_components(intr->src[src_index]) == 1)
      return;

   nir_def *set = nir_channel(b, intr->src[src_index].ssa, 0);
   nir_def *binding = nir_channel(b, intr->src[src_index].ssa, 1);

   nir_def *base = nir_load_const_buf_base_addr_lvp(b, set);
   nir_def *offset = nir_imul_imm(b, binding, sizeof(struct lp_descriptor));
   nir_def *descriptor = nir_iadd(b, base, nir_u2u64(b, offset));
   nir_src_rewrite(&intr->src[src_index], descriptor);
}

static void
lower_accel_struct(nir_builder *b, nir_intrinsic_instr *intr, uint32_t src_index)
{
   if (nir_src_bit_size(intr->src[src_index]) == 64)
      return;

   nir_def *set = nir_channel(b, intr->src[src_index].ssa, 0);
   nir_def *binding = nir_channel(b, intr->src[src_index].ssa, 1);

   nir_def *offset = nir_imul_imm(b, binding, sizeof(struct lp_descriptor));
   nir_src_rewrite(&intr->src[src_index], nir_load_ubo(b, 1, 64, set, offset, .range = UINT32_MAX));
}

static nir_def *
vulkan_resource_from_deref(nir_builder *b, nir_deref_instr *deref, const struct lvp_pipeline_layout *layout,
                           unsigned plane)
{
   nir_def *index = nir_imm_int(b, 0);

   while (deref->deref_type != nir_deref_type_var) {
      assert(deref->deref_type == nir_deref_type_array);
      unsigned array_size = MAX2(glsl_get_aoa_size(deref->type), 1);

      index = nir_iadd(b, index, nir_imul_imm(b, deref->arr.index.ssa, array_size));

      deref = nir_deref_instr_parent(deref);
   }

   nir_variable *var = deref->var;

   const struct lvp_descriptor_set_binding_layout *binding = get_binding_layout(layout, var->data.descriptor_set, var->data.binding);
   uint32_t binding_base = binding->descriptor_index + plane;
   index = nir_iadd_imm(b, nir_imul_imm(b, index, binding->stride), binding_base);

   nir_def *set = nir_load_const_buf_base_addr_lvp(b, nir_imm_int(b, var->data.descriptor_set + 1));
   nir_def *offset = nir_imul_imm(b, index, sizeof(struct lp_descriptor));
   return nir_iadd(b, set, nir_u2u64(b, offset));
}

static void lower_vri_instr_tex(struct nir_builder *b,
                                nir_tex_instr *tex, void *data_cb)
{
   struct lvp_pipeline_layout *layout = data_cb;
   nir_def *plane_ssa = nir_steal_tex_src(tex, nir_tex_src_plane);
   const uint32_t plane =
      plane_ssa ? nir_src_as_uint(nir_src_for_ssa(plane_ssa)) : 0;

   for (unsigned i = 0; i < tex->num_srcs; i++) {
      nir_deref_instr *deref;
      switch (tex->src[i].src_type) {
      case nir_tex_src_texture_deref:
         tex->src[i].src_type = nir_tex_src_texture_handle;
         deref = nir_src_as_deref(tex->src[i].src);
         break;
      case nir_tex_src_sampler_deref:
         tex->src[i].src_type = nir_tex_src_sampler_handle;
         deref = nir_src_as_deref(tex->src[i].src);
         break;
      default:
         continue;
      }

      nir_def *resource = vulkan_resource_from_deref(b, deref, layout, plane);
      nir_src_rewrite(&tex->src[i].src, resource);
   }
}

static void
lower_image_intrinsic(nir_builder *b,
                      nir_intrinsic_instr *intrin,
                      void *data_cb)
{
   const struct lvp_pipeline_layout *layout = data_cb;

   nir_deref_instr *deref = nir_src_as_deref(intrin->src[0]);

   nir_def *resource = vulkan_resource_from_deref(b, deref, layout, 0);
   nir_rewrite_image_intrinsic(intrin, resource, true);
}

static bool
lower_load_ubo(nir_builder *b, nir_intrinsic_instr *intrin, void *data_cb)
{
   if (intrin->intrinsic != nir_intrinsic_load_ubo)
      return false;

   nir_binding binding = nir_chase_binding(intrin->src[0]);
   /* If binding.success=false, then this is a variable pointer, which we don't support with
    * VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK.
    */
   if (!binding.success)
      return false;

   const struct lvp_descriptor_set_binding_layout *bind_layout =
      get_binding_layout(data_cb, binding.desc_set, binding.binding);
   if (bind_layout->type != VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK)
      return false;

   b->cursor = nir_before_instr(&intrin->instr);

   nir_src_rewrite(&intrin->src[0], nir_imm_int(b, binding.desc_set + 1));

   nir_def *offset = nir_iadd_imm(b, intrin->src[1].ssa, bind_layout->uniform_block_offset);
   nir_src_rewrite(&intrin->src[1], offset);

   return true;
}

static void
lower_push_constant(nir_builder *b, nir_intrinsic_instr *intrin, void *data_cb)
{
   nir_def *load = nir_load_ubo(b, intrin->def.num_components, intrin->def.bit_size,
                                nir_imm_int(b, 0), intrin->src[0].ssa,
                                .range = nir_intrinsic_range(intrin));
   nir_def_rewrite_uses(&intrin->def, load);
   nir_instr_remove(&intrin->instr);
}

static bool
lower_vri_instr(struct nir_builder *b, nir_instr *instr, void *data_cb)
{
   b->cursor = nir_before_instr(instr);

   if (instr->type == nir_instr_type_intrinsic) {
      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
      switch (intrin->intrinsic) {
      case nir_intrinsic_vulkan_resource_index:
         nir_def_rewrite_uses(&intrin->def, lower_vri_intrin_vri(b, instr, data_cb));
         return true;

      case nir_intrinsic_vulkan_resource_reindex:
         nir_def_rewrite_uses(&intrin->def, lower_vri_intrin_vrri(b, instr, data_cb));
         return true;

      case nir_intrinsic_load_vulkan_descriptor:
         nir_def_rewrite_uses(&intrin->def, lower_vri_intrin_lvd(b, instr, data_cb));
         return true;


      case nir_intrinsic_load_ubo:
         lower_buffer(b, intrin, 0);
         return true;

      case nir_intrinsic_load_ssbo:
      case nir_intrinsic_ssbo_atomic:
      case nir_intrinsic_ssbo_atomic_swap:
      case nir_intrinsic_get_ssbo_size:
         lower_buffer(b, intrin, 0);
         return true;

      case nir_intrinsic_store_ssbo:
         lower_buffer(b, intrin, 1);
         return true;

      case nir_intrinsic_trace_ray:
         lower_accel_struct(b, intrin, 0);
         return true;

      case nir_intrinsic_rq_initialize:
         lower_accel_struct(b, intrin, 1);
         return true;

      case nir_intrinsic_image_deref_sparse_load:
      case nir_intrinsic_image_deref_load:
      case nir_intrinsic_image_deref_store:
      case nir_intrinsic_image_deref_atomic:
      case nir_intrinsic_image_deref_atomic_swap:
      case nir_intrinsic_image_deref_size:
      case nir_intrinsic_image_deref_samples:
         lower_image_intrinsic(b, intrin, data_cb);
         return true;
      
      case nir_intrinsic_load_push_constant:
         lower_push_constant(b, intrin, data_cb);
         return true;

      default:
         return false;
      }
   }

   if (instr->type == nir_instr_type_tex) {
      lower_vri_instr_tex(b, nir_instr_as_tex(instr), data_cb);
      return true;
   }

   return false;
}

void lvp_lower_pipeline_layout(const struct lvp_device *device,
                               struct lvp_pipeline_layout *layout,
                               nir_shader *shader)
{
   nir_shader_intrinsics_pass(shader, lower_load_ubo,
                              nir_metadata_control_flow,
                              layout);
   nir_shader_instructions_pass(shader, lower_vri_instr, nir_metadata_control_flow, layout);
}
