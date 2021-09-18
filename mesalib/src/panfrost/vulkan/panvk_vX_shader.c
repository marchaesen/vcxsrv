/*
 * Copyright © 2021 Collabora Ltd.
 *
 * Derived from tu_shader.c which is:
 * Copyright © 2019 Google LLC
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "gen_macros.h"

#include "panvk_private.h"

#include "nir_builder.h"
#include "nir_lower_blend.h"
#include "spirv/nir_spirv.h"
#include "util/mesa-sha1.h"

#include "panfrost-quirks.h"
#include "pan_shader.h"

#include "vk_util.h"

static nir_shader *
panvk_spirv_to_nir(const void *code,
                   size_t codesize,
                   gl_shader_stage stage,
                   const char *entry_point_name,
                   const VkSpecializationInfo *spec_info,
                   const nir_shader_compiler_options *nir_options)
{
   /* TODO these are made-up */
   const struct spirv_to_nir_options spirv_options = {
      .caps = { false },
      .ubo_addr_format = nir_address_format_32bit_index_offset,
      .ssbo_addr_format = nir_address_format_32bit_index_offset,
   };

   /* convert VkSpecializationInfo */
   uint32_t num_spec = 0;
   struct nir_spirv_specialization *spec =
      vk_spec_info_to_nir_spirv(spec_info, &num_spec);

   nir_shader *nir = spirv_to_nir(code, codesize / sizeof(uint32_t), spec,
                                  num_spec, stage, entry_point_name,
                                  &spirv_options, nir_options);

   free(spec);

   assert(nir->info.stage == stage);
   nir_validate_shader(nir, "after spirv_to_nir");

   return nir;
}

struct panvk_lower_misc_ctx {
   struct panvk_shader *shader;
   const struct panvk_pipeline_layout *layout;
};

static unsigned
get_fixed_sampler_index(nir_deref_instr *deref,
                        const struct panvk_lower_misc_ctx *ctx)
{
   nir_variable *var = nir_deref_instr_get_variable(deref);
   unsigned set = var->data.descriptor_set;
   unsigned binding = var->data.binding;
   const struct panvk_descriptor_set_binding_layout *bind_layout =
      &ctx->layout->sets[set].layout->bindings[binding];

   return bind_layout->sampler_idx + ctx->layout->sets[set].sampler_offset;
}

static unsigned
get_fixed_texture_index(nir_deref_instr *deref,
                        const struct panvk_lower_misc_ctx *ctx)
{
   nir_variable *var = nir_deref_instr_get_variable(deref);
   unsigned set = var->data.descriptor_set;
   unsigned binding = var->data.binding;
   const struct panvk_descriptor_set_binding_layout *bind_layout =
      &ctx->layout->sets[set].layout->bindings[binding];

   return bind_layout->tex_idx + ctx->layout->sets[set].tex_offset;
}

static bool
lower_tex(nir_builder *b, nir_tex_instr *tex,
          const struct panvk_lower_misc_ctx *ctx)
{
   bool progress = false;
   int sampler_src_idx = nir_tex_instr_src_index(tex, nir_tex_src_sampler_deref);

   b->cursor = nir_before_instr(&tex->instr);

   if (sampler_src_idx >= 0) {
      nir_deref_instr *deref = nir_src_as_deref(tex->src[sampler_src_idx].src);
      tex->sampler_index = get_fixed_sampler_index(deref, ctx);
      nir_tex_instr_remove_src(tex, sampler_src_idx);
      progress = true;
   }

   int tex_src_idx = nir_tex_instr_src_index(tex, nir_tex_src_texture_deref);
   if (tex_src_idx >= 0) {
      nir_deref_instr *deref = nir_src_as_deref(tex->src[tex_src_idx].src);
      tex->texture_index = get_fixed_texture_index(deref, ctx);
      nir_tex_instr_remove_src(tex, tex_src_idx);
      progress = true;
   }

   return progress;
}

static void
lower_vulkan_resource_index(nir_builder *b, nir_intrinsic_instr *intr,
                            const struct panvk_lower_misc_ctx *ctx)
{
   nir_ssa_def *vulkan_idx = intr->src[0].ssa;

   unsigned set = nir_intrinsic_desc_set(intr);
   unsigned binding = nir_intrinsic_binding(intr);
   struct panvk_descriptor_set_layout *set_layout = ctx->layout->sets[set].layout;
   struct panvk_descriptor_set_binding_layout *binding_layout =
      &set_layout->bindings[binding];
   unsigned base;

   switch (binding_layout->type) {
   case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
   case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
      base = binding_layout->ubo_idx + ctx->layout->sets[set].ubo_offset;
      break;
   case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
   case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
      base = binding_layout->ssbo_idx + ctx->layout->sets[set].ssbo_offset;
      break;
   default:
      unreachable("Invalid descriptor type");
      break;
   }

   b->cursor = nir_before_instr(&intr->instr);
   nir_ssa_def *idx = nir_iadd(b, nir_imm_int(b, base), vulkan_idx);
   nir_ssa_def_rewrite_uses(&intr->dest.ssa, idx);
   nir_instr_remove(&intr->instr);
}

static void
lower_load_vulkan_descriptor(nir_builder *b, nir_intrinsic_instr *intrin)
{
   /* Loading the descriptor happens as part of the load/store instruction so
    * this is a no-op.
    */
   b->cursor = nir_before_instr(&intrin->instr);
   nir_ssa_def *val = nir_vec2(b, intrin->src[0].ssa, nir_imm_int(b, 0));
   nir_ssa_def_rewrite_uses(&intrin->dest.ssa, val);
   nir_instr_remove(&intrin->instr);
}

static bool
lower_intrinsic(nir_builder *b, nir_intrinsic_instr *intr,
                const struct panvk_lower_misc_ctx *ctx)
{
   switch (intr->intrinsic) {
   case nir_intrinsic_vulkan_resource_index:
      lower_vulkan_resource_index(b, intr, ctx);
      return true;
   case nir_intrinsic_load_vulkan_descriptor:
      lower_load_vulkan_descriptor(b, intr);
      return true;
   default:
      return false;
   }

}

static bool
panvk_lower_misc_instr(nir_builder *b,
                       nir_instr *instr,
                       void *data)
{
   const struct panvk_lower_misc_ctx *ctx = data;

   switch (instr->type) {
   case nir_instr_type_tex:
      return lower_tex(b, nir_instr_as_tex(instr), ctx);
   case nir_instr_type_intrinsic:
      return lower_intrinsic(b, nir_instr_as_intrinsic(instr), ctx);
   default:
      return false;
   }
}

static bool
panvk_lower_misc(nir_shader *nir, const struct panvk_lower_misc_ctx *ctx)
{
   return nir_shader_instructions_pass(nir, panvk_lower_misc_instr,
                                       nir_metadata_block_index |
                                       nir_metadata_dominance,
                                       (void *)ctx);
}

static void
panvk_lower_blend(struct panfrost_device *pdev,
                  nir_shader *nir,
                  struct pan_blend_state *blend_state,
                  bool static_blend_constants)
{
   nir_lower_blend_options options = {
      .logicop_enable = blend_state->logicop_enable,
      .logicop_func = blend_state->logicop_func,
   };

   bool lower_blend = false;
   for (unsigned rt = 0; rt < blend_state->rt_count; rt++) {
      if (!panvk_per_arch(blend_needs_lowering)(pdev, blend_state, rt))
         continue;

      const struct pan_blend_rt_state *rt_state = &blend_state->rts[rt];
      options.rt[rt].colormask = rt_state->equation.color_mask;
      options.format[rt] = rt_state->format;
      if (!rt_state->equation.blend_enable) {
         static const nir_lower_blend_channel replace = {
            .func = BLEND_FUNC_ADD,
            .src_factor = BLEND_FACTOR_ZERO,
            .invert_src_factor = true,
            .dst_factor = BLEND_FACTOR_ZERO,
            .invert_dst_factor = false,
         };

         options.rt[rt].rgb = replace;
         options.rt[rt].alpha = replace;
      } else {
         options.rt[rt].rgb.func = rt_state->equation.rgb_func;
         options.rt[rt].rgb.src_factor = rt_state->equation.rgb_src_factor;
         options.rt[rt].rgb.invert_src_factor = rt_state->equation.rgb_invert_src_factor;
         options.rt[rt].rgb.dst_factor = rt_state->equation.rgb_dst_factor;
         options.rt[rt].rgb.invert_dst_factor = rt_state->equation.rgb_invert_dst_factor;
         options.rt[rt].alpha.func = rt_state->equation.alpha_func;
         options.rt[rt].alpha.src_factor = rt_state->equation.alpha_src_factor;
         options.rt[rt].alpha.invert_src_factor = rt_state->equation.alpha_invert_src_factor;
         options.rt[rt].alpha.dst_factor = rt_state->equation.alpha_dst_factor;
         options.rt[rt].alpha.invert_dst_factor = rt_state->equation.alpha_invert_dst_factor;
      }

      lower_blend = true;
   }

   /* FIXME: currently untested */
   assert(!lower_blend);

   if (lower_blend)
      NIR_PASS_V(nir, nir_lower_blend, options);
}

struct panvk_shader *
panvk_per_arch(shader_create)(struct panvk_device *dev,
                              gl_shader_stage stage,
                              const VkPipelineShaderStageCreateInfo *stage_info,
                              const struct panvk_pipeline_layout *layout,
                              unsigned sysval_ubo,
                              struct pan_blend_state *blend_state,
                              bool static_blend_constants,
                              const VkAllocationCallbacks *alloc)
{
   const struct panvk_shader_module *module = panvk_shader_module_from_handle(stage_info->module);
   struct panfrost_device *pdev = &dev->physical_device->pdev;
   struct panvk_shader *shader;

   shader = vk_zalloc2(&dev->vk.alloc, alloc, sizeof(*shader), 8,
                       VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (!shader)
      return NULL;

   util_dynarray_init(&shader->binary, NULL);

   /* translate SPIR-V to NIR */
   assert(module->code_size % 4 == 0);
   nir_shader *nir = panvk_spirv_to_nir(module->code,
                                        module->code_size,
                                        stage, stage_info->pName,
                                        stage_info->pSpecializationInfo,
                                        pan_shader_get_compiler_options(pdev));
   if (!nir) {
      vk_free2(&dev->vk.alloc, alloc, shader);
      return NULL;
   }

   if (stage == MESA_SHADER_FRAGMENT)
      panvk_lower_blend(pdev, nir, blend_state, static_blend_constants);

   /* multi step inlining procedure */
   NIR_PASS_V(nir, nir_lower_variable_initializers, nir_var_function_temp);
   NIR_PASS_V(nir, nir_lower_returns);
   NIR_PASS_V(nir, nir_inline_functions);
   NIR_PASS_V(nir, nir_copy_prop);
   NIR_PASS_V(nir, nir_opt_deref);
   foreach_list_typed_safe(nir_function, func, node, &nir->functions) {
      if (!func->is_entrypoint)
         exec_node_remove(&func->node);
   }
   assert(exec_list_length(&nir->functions) == 1);
   NIR_PASS_V(nir, nir_lower_variable_initializers, ~nir_var_function_temp);

   /* Split member structs.  We do this before lower_io_to_temporaries so that
    * it doesn't lower system values to temporaries by accident.
    */
   NIR_PASS_V(nir, nir_split_var_copies);
   NIR_PASS_V(nir, nir_split_per_member_structs);

   NIR_PASS_V(nir, nir_remove_dead_variables,
              nir_var_shader_in | nir_var_shader_out |
              nir_var_system_value | nir_var_mem_shared,
              NULL);

   NIR_PASS_V(nir, nir_lower_io_to_temporaries,
              nir_shader_get_entrypoint(nir), true, true);

   NIR_PASS_V(nir, nir_lower_indirect_derefs,
              nir_var_shader_in | nir_var_shader_out,
              UINT32_MAX);

   NIR_PASS_V(nir, nir_opt_copy_prop_vars);
   NIR_PASS_V(nir, nir_opt_combine_stores, nir_var_all);

   NIR_PASS_V(nir, nir_lower_uniforms_to_ubo, true, false);
   NIR_PASS_V(nir, nir_lower_explicit_io,
              nir_var_mem_ubo | nir_var_mem_ssbo,
              nir_address_format_32bit_index_offset);

   nir_assign_io_var_locations(nir, nir_var_shader_in, &nir->num_inputs, stage);
   nir_assign_io_var_locations(nir, nir_var_shader_out, &nir->num_outputs, stage);

   NIR_PASS_V(nir, nir_lower_system_values);
   NIR_PASS_V(nir, nir_lower_compute_system_values, NULL);

   NIR_PASS_V(nir, nir_lower_var_copies);

   struct panvk_lower_misc_ctx ctx = {
      .shader = shader,
      .layout = layout,
   }; 
   NIR_PASS_V(nir, panvk_lower_misc, &ctx);

   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));
   if (unlikely(dev->physical_device->instance->debug_flags & PANVK_DEBUG_NIR)) {
      fprintf(stderr, "translated nir:\n");
      nir_print_shader(nir, stderr);
   }

   struct panfrost_compile_inputs inputs = {
      .gpu_id = pdev->gpu_id,
      .no_ubo_to_push = true,
      .sysval_ubo = sysval_ubo,
   };

   pan_shader_compile(pdev, nir, &inputs, &shader->binary, &shader->info);

   /* Patch the descriptor count */
   shader->info.ubo_count =
      shader->info.sysvals.sysval_count ? sysval_ubo + 1 : layout->num_ubos;
   shader->info.sampler_count = layout->num_samplers;
   shader->info.texture_count = layout->num_textures;

   shader->sysval_ubo = sysval_ubo;

   ralloc_free(nir);

   return shader;
}
