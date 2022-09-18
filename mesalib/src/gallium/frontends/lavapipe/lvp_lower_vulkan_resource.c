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
#include "nir.h"
#include "nir_builder.h"
#include "lvp_lower_vulkan_resource.h"

static bool
lower_vulkan_resource_index(const nir_instr *instr, const void *data_cb)
{
   if (instr->type == nir_instr_type_intrinsic) {
      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
      switch (intrin->intrinsic) {
      case nir_intrinsic_vulkan_resource_index:
      case nir_intrinsic_vulkan_resource_reindex:
      case nir_intrinsic_load_vulkan_descriptor:
      case nir_intrinsic_get_ssbo_size:
      case nir_intrinsic_image_deref_sparse_load:
      case nir_intrinsic_image_deref_load:
      case nir_intrinsic_image_deref_store:
      case nir_intrinsic_image_deref_atomic_add:
      case nir_intrinsic_image_deref_atomic_imin:
      case nir_intrinsic_image_deref_atomic_umin:
      case nir_intrinsic_image_deref_atomic_imax:
      case nir_intrinsic_image_deref_atomic_umax:
      case nir_intrinsic_image_deref_atomic_and:
      case nir_intrinsic_image_deref_atomic_or:
      case nir_intrinsic_image_deref_atomic_xor:
      case nir_intrinsic_image_deref_atomic_exchange:
      case nir_intrinsic_image_deref_atomic_comp_swap:
      case nir_intrinsic_image_deref_atomic_fadd:
      case nir_intrinsic_image_deref_size:
      case nir_intrinsic_image_deref_samples:
         return true;
      default:
         return false;
      }
   }
   if (instr->type == nir_instr_type_tex) {
      return true;
   }
   return false;
}

static bool
lower_uniform_block_access(const nir_instr *instr, const void *data_cb)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
   if (intrin->intrinsic != nir_intrinsic_load_deref)
      return false;
   nir_deref_instr *deref = nir_instr_as_deref(intrin->src[0].ssa->parent_instr);
   return deref->modes == nir_var_mem_ubo;
}

static nir_ssa_def *
lower_block_instr(nir_builder *b, nir_instr *instr, void *data_cb)
{
   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
   nir_binding nb = nir_chase_binding(intrin->src[0]);
   const struct lvp_pipeline_layout *layout = data_cb;
   const struct lvp_descriptor_set_binding_layout *binding =
      get_binding_layout(layout, nb.desc_set, nb.binding);
   if (binding->type != VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK)
      return NULL;
   if (!binding->array_size)
      return NIR_LOWER_INSTR_PROGRESS_REPLACE;

   assert(intrin->src[0].ssa->num_components == 2);
   unsigned value = 0;
   for (unsigned s = 0; s < nb.desc_set; s++)
      value += get_set_layout(layout, s)->stage[b->shader->info.stage].uniform_block_size;
   if (layout->push_constant_stages & BITFIELD_BIT(b->shader->info.stage))
      value += layout->push_constant_size;
   value += binding->stage[b->shader->info.stage].uniform_block_offset;

   b->cursor = nir_before_instr(instr);
   nir_ssa_def *offset = nir_imm_ivec2(b, 0, value);
   nir_ssa_def *added = nir_iadd(b, intrin->src[0].ssa, offset);
   nir_deref_instr *deref = nir_instr_as_deref(intrin->src[0].ssa->parent_instr);
   nir_deref_instr *cast = nir_build_deref_cast(b, added, deref->modes, deref->type, 0);
   nir_instr_rewrite_src_ssa(instr, &intrin->src[0], &cast->dest.ssa);
   return NIR_LOWER_INSTR_PROGRESS;
}

static nir_ssa_def *lower_vri_intrin_vri(struct nir_builder *b,
                                           nir_instr *instr, void *data_cb)
{
   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
   unsigned desc_set_idx = nir_intrinsic_desc_set(intrin);
   unsigned binding_idx = nir_intrinsic_binding(intrin);
   const struct lvp_pipeline_layout *layout = data_cb;
   const struct lvp_descriptor_set_binding_layout *binding =
      get_binding_layout(data_cb, desc_set_idx, binding_idx);
   int value = 0;
   bool is_ubo = (binding->type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
                  binding->type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC);

   /* always load inline uniform blocks from ubo0 */
   if (binding->type == VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK)
      return nir_imm_ivec2(b, 0, 0);

   for (unsigned s = 0; s < desc_set_idx; s++) {
     if (!layout->vk.set_layouts[s])
        continue;
     if (is_ubo)
       value += get_set_layout(layout, s)->stage[b->shader->info.stage].const_buffer_count;
     else
       value += get_set_layout(layout, s)->stage[b->shader->info.stage].shader_buffer_count;
   }
   if (is_ubo)
     value += binding->stage[b->shader->info.stage].const_buffer_index + 1;
   else
     value += binding->stage[b->shader->info.stage].shader_buffer_index;

   /* The SSA size for indices is the same as for pointers.  We use
    * nir_addr_format_32bit_index_offset so we need a vec2.  We don't need all
    * that data so just stuff a 0 in the second component.
    */
   if (nir_src_is_const(intrin->src[0])) {
      value += nir_src_comp_as_int(intrin->src[0], 0);
      return nir_imm_ivec2(b, value, 0);
   } else
      return nir_vec2(b, nir_iadd_imm(b, intrin->src[0].ssa, value),
                         nir_imm_int(b, 0));
}

static nir_ssa_def *lower_vri_intrin_vrri(struct nir_builder *b,
                                          nir_instr *instr, void *data_cb)
{
   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
   nir_ssa_def *old_index = nir_ssa_for_src(b, intrin->src[0], 1);
   nir_ssa_def *delta = nir_ssa_for_src(b, intrin->src[1], 1);
   return nir_vec2(b, nir_iadd(b, old_index, delta),
                      nir_imm_int(b, 0));
}

static nir_ssa_def *lower_vri_intrin_lvd(struct nir_builder *b,
                                         nir_instr *instr, void *data_cb)
{
   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
   nir_ssa_def *index = nir_ssa_for_src(b, intrin->src[0], 1);
   return nir_vec2(b, index, nir_imm_int(b, 0));
}

/*
 * Return a bitset of the texture units or sampler units used by a
 * texture instruction.  Note that 'used' is expected to be already
 * initialized.  i.e. this function does not zero-out the bitset before
 * setting any bits.
 */
static void
lower_vri_instr_tex_deref(nir_tex_instr *tex,
                          nir_tex_src_type deref_src_type,
                          gl_shader_stage stage,
                          struct lvp_pipeline_layout *layout,
                          BITSET_WORD used[], // textures or samplers
                          size_t used_size)   // used[] size, in bits
{
   int deref_src_idx = nir_tex_instr_src_index(tex, deref_src_type);

   if (deref_src_idx < 0)
      return;

   nir_deref_instr *deref_instr = nir_src_as_deref(tex->src[deref_src_idx].src);
   nir_variable *var = nir_deref_instr_get_variable(deref_instr);
   unsigned desc_set_idx = var->data.descriptor_set;
   unsigned binding_idx = var->data.binding;
   int value = 0;

   const struct lvp_descriptor_set_binding_layout *binding =
      get_binding_layout(layout, desc_set_idx, binding_idx);
   nir_tex_instr_remove_src(tex, deref_src_idx);
   for (unsigned s = 0; s < desc_set_idx; s++) {
      if (!layout->vk.set_layouts[s])
         continue;
      if (deref_src_type == nir_tex_src_sampler_deref)
         value += get_set_layout(layout, s)->stage[stage].sampler_count;
      else
         value += get_set_layout(layout, s)->stage[stage].sampler_view_count;
   }
   if (deref_src_type == nir_tex_src_sampler_deref)
      value += binding->stage[stage].sampler_index;
   else
      value += binding->stage[stage].sampler_view_index;

   if (deref_instr->deref_type == nir_deref_type_array) {
      if (nir_src_is_const(deref_instr->arr.index))
         value += nir_src_as_uint(deref_instr->arr.index);
      else {
         if (deref_src_type == nir_tex_src_sampler_deref)
            nir_tex_instr_add_src(tex, nir_tex_src_sampler_offset, deref_instr->arr.index);
         else
            nir_tex_instr_add_src(tex, nir_tex_src_texture_offset, deref_instr->arr.index);
      }
   }
   if (deref_src_type == nir_tex_src_sampler_deref)
      tex->sampler_index = value;
   else
      tex->texture_index = value;

   if (deref_instr->deref_type == nir_deref_type_array) {
      assert(glsl_type_is_array(var->type));
      assert(value >= 0);
      assert(value < used_size);
      if (nir_src_is_const(deref_instr->arr.index)) {
         BITSET_SET(used, value);
      } else {
         unsigned size = glsl_get_aoa_size(var->type);
         assert(value + size <= used_size);
         BITSET_SET_RANGE(used, value, value+size);
      }
   } else {
      assert(value < used_size);
      BITSET_SET(used, value);
   }
}

static void lower_vri_instr_tex(struct nir_builder *b,
                                nir_tex_instr *tex, void *data_cb)
{
   struct lvp_pipeline_layout *layout = data_cb;
   lower_vri_instr_tex_deref(tex, nir_tex_src_sampler_deref,
                             b->shader->info.stage, layout,
                             b->shader->info.samplers_used,
                             BITSET_SIZE(b->shader->info.samplers_used));

   lower_vri_instr_tex_deref(tex, nir_tex_src_texture_deref,
                             b->shader->info.stage, layout,
                             b->shader->info.textures_used,
                             BITSET_SIZE(b->shader->info.textures_used));
}

static void
lower_image_intrinsic(nir_builder *b,
                      nir_intrinsic_instr *intrin,
                      void *data_cb)
{
   const struct lvp_pipeline_layout *layout = data_cb;
   gl_shader_stage stage = b->shader->info.stage;

   nir_deref_instr *deref = nir_src_as_deref(intrin->src[0]);
   nir_variable *var = nir_deref_instr_get_variable(deref);
   unsigned desc_set_idx = var->data.descriptor_set;
   unsigned binding_idx = var->data.binding;
   const struct lvp_descriptor_set_binding_layout *binding =
      get_binding_layout(layout, desc_set_idx, binding_idx);
   nir_ssa_def *index = NULL;

   int value = 0;
   for (unsigned s = 0; s < desc_set_idx; s++) {
      if (!layout->vk.set_layouts[s])
         continue;
      value += get_set_layout(layout, s)->stage[stage].image_count;
   }
   value += binding->stage[stage].image_index;

   b->cursor = nir_before_instr(&intrin->instr);
   if (deref->deref_type == nir_deref_type_array) {
      assert(glsl_type_is_array(var->type));
      assert(value >= 0);
      if (nir_src_is_const(deref->arr.index)) {
         value += nir_src_as_uint(deref->arr.index);
         BITSET_SET(b->shader->info.images_used, value);
         index = nir_imm_int(b, value);
      } else {
         unsigned size = glsl_get_aoa_size(var->type);
         BITSET_SET_RANGE(b->shader->info.images_used,
                          value, value + size - 1);
         index = nir_iadd_imm(b, deref->arr.index.ssa, value);
      }
   } else {
      BITSET_SET(b->shader->info.images_used, value);
      index = nir_imm_int(b, value);
   }

   nir_rewrite_image_intrinsic(intrin, index, false);
}

static nir_ssa_def *lower_vri_instr(struct nir_builder *b,
                                    nir_instr *instr, void *data_cb)
{
   if (instr->type == nir_instr_type_intrinsic) {
      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
      switch (intrin->intrinsic) {
      case nir_intrinsic_vulkan_resource_index:
         return lower_vri_intrin_vri(b, instr, data_cb);

      case nir_intrinsic_vulkan_resource_reindex:
         return lower_vri_intrin_vrri(b, instr, data_cb);

      case nir_intrinsic_load_vulkan_descriptor:
         return lower_vri_intrin_lvd(b, instr, data_cb);

      case nir_intrinsic_get_ssbo_size: {
         /* The result of the load_vulkan_descriptor is a vec2(index, offset)
          * but we only want the index in get_ssbo_size.
          */
         b->cursor = nir_before_instr(&intrin->instr);
         nir_ssa_def *index = nir_ssa_for_src(b, intrin->src[0], 1);
         nir_instr_rewrite_src(&intrin->instr, &intrin->src[0],
                               nir_src_for_ssa(index));
         return NULL;
      }
      case nir_intrinsic_image_deref_sparse_load:
      case nir_intrinsic_image_deref_load:
      case nir_intrinsic_image_deref_store:
      case nir_intrinsic_image_deref_atomic_add:
      case nir_intrinsic_image_deref_atomic_imin:
      case nir_intrinsic_image_deref_atomic_umin:
      case nir_intrinsic_image_deref_atomic_imax:
      case nir_intrinsic_image_deref_atomic_umax:
      case nir_intrinsic_image_deref_atomic_and:
      case nir_intrinsic_image_deref_atomic_or:
      case nir_intrinsic_image_deref_atomic_xor:
      case nir_intrinsic_image_deref_atomic_exchange:
      case nir_intrinsic_image_deref_atomic_comp_swap:
      case nir_intrinsic_image_deref_atomic_fadd:
      case nir_intrinsic_image_deref_size:
      case nir_intrinsic_image_deref_samples:
         lower_image_intrinsic(b, intrin, data_cb);
         return NULL;

      default:
         return NULL;
      }
   }
   if (instr->type == nir_instr_type_tex)
      lower_vri_instr_tex(b, nir_instr_as_tex(instr), data_cb);
   return NULL;
}

void lvp_lower_pipeline_layout(const struct lvp_device *device,
                               struct lvp_pipeline_layout *layout,
                               nir_shader *shader)
{
   nir_shader_lower_instructions(shader, lower_uniform_block_access, lower_block_instr, layout);
   nir_shader_lower_instructions(shader, lower_vulkan_resource_index, lower_vri_instr, layout);
   nir_foreach_variable_with_modes(var, shader, nir_var_uniform |
                                                nir_var_image) {
      const struct glsl_type *type = var->type;
      enum glsl_base_type base_type =
         glsl_get_base_type(glsl_without_array(type));
      unsigned desc_set_idx = var->data.descriptor_set;
      unsigned binding_idx = var->data.binding;
      const struct lvp_descriptor_set_binding_layout *binding =
         get_binding_layout(layout, desc_set_idx, binding_idx);
      int value = 0;
      var->data.descriptor_set = 0;
      if (base_type == GLSL_TYPE_SAMPLER || base_type == GLSL_TYPE_TEXTURE) {
         if (binding->type == VK_DESCRIPTOR_TYPE_SAMPLER) {
            for (unsigned s = 0; s < desc_set_idx; s++) {
               if (!layout->vk.set_layouts[s])
                  continue;
               value += get_set_layout(layout, s)->stage[shader->info.stage].sampler_count;
            }
            value += binding->stage[shader->info.stage].sampler_index;
         } else {
            for (unsigned s = 0; s < desc_set_idx; s++) {
               if (!layout->vk.set_layouts[s])
                  continue;
               value += get_set_layout(layout, s)->stage[shader->info.stage].sampler_view_count;
            }
            value += binding->stage[shader->info.stage].sampler_view_index;
         }
         var->data.binding = value;
      }
      if (base_type == GLSL_TYPE_IMAGE) {
         var->data.descriptor_set = 0;
         for (unsigned s = 0; s < desc_set_idx; s++) {
           if (!layout->vk.set_layouts[s])
              continue;
           value += get_set_layout(layout, s)->stage[shader->info.stage].image_count;
         }
         value += binding->stage[shader->info.stage].image_index;
         var->data.binding = value;
      }
   }
}
