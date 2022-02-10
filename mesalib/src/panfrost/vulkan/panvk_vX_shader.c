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

#include "genxml/gen_macros.h"

#include "panvk_private.h"

#include "nir_builder.h"
#include "nir_lower_blend.h"
#include "nir_conversion_builder.h"
#include "spirv/nir_spirv.h"
#include "util/mesa-sha1.h"

#include "pan_shader.h"
#include "util/pan_lower_framebuffer.h"

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

   const struct nir_lower_sysvals_to_varyings_options sysvals_to_varyings = {
      .frag_coord = PAN_ARCH <= 5,
      .point_coord = PAN_ARCH <= 5,
      .front_face = PAN_ARCH <= 5,
   };
   NIR_PASS_V(nir, nir_lower_sysvals_to_varyings, &sysvals_to_varyings);

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

static bool
panvk_inline_blend_constants(nir_builder *b, nir_instr *instr, void *data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   if (intr->intrinsic != nir_intrinsic_load_blend_const_color_rgba)
      return false;

   const nir_const_value *constants = data;

   b->cursor = nir_after_instr(instr);
   nir_ssa_def *constant = nir_build_imm(b, 4, 32, constants);
   nir_ssa_def_rewrite_uses(&intr->dest.ssa, constant);
   nir_instr_remove(instr);
   return true;
}

#if PAN_ARCH <= 5
struct panvk_lower_blend_type_conv {
   nir_variable *var;
   nir_alu_type newtype;
   nir_alu_type oldtype;
};

static bool
panvk_adjust_rt_type(nir_builder *b, nir_instr *instr, void *data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   if (intr->intrinsic != nir_intrinsic_store_deref &&
       intr->intrinsic != nir_intrinsic_load_deref)
      return false;

   nir_variable *var = nir_intrinsic_get_var(intr, 0);
   if (var->data.mode != nir_var_shader_out ||
       (var->data.location != FRAG_RESULT_COLOR &&
        var->data.location < FRAG_RESULT_DATA0))
      return false;

   /* Determine render target for per-RT blending */
   unsigned rt =
      (var->data.location == FRAG_RESULT_COLOR) ? 0 :
      (var->data.location - FRAG_RESULT_DATA0);

   const struct panvk_lower_blend_type_conv *typeconv = data;
   nir_alu_type newtype = typeconv[rt].newtype;
   nir_alu_type oldtype = typeconv[rt].oldtype;

   /* No conversion */
   if (newtype == nir_type_invalid || newtype == oldtype)
      return false;


   b->cursor = nir_before_instr(instr);

   nir_deref_instr *deref = nir_build_deref_var(b, typeconv[rt].var);
   nir_instr_rewrite_src(&intr->instr, &intr->src[0],
                         nir_src_for_ssa(&deref->dest.ssa));

   if (intr->intrinsic == nir_intrinsic_store_deref) {
      nir_ssa_def *val = nir_ssa_for_src(b, intr->src[1], 4);
      bool clamp = nir_alu_type_get_base_type(newtype) != nir_type_float;
      val = nir_convert_with_rounding(b, val, oldtype, newtype,
                                      nir_rounding_mode_undef, clamp);
      nir_store_var(b, typeconv[rt].var, val, nir_intrinsic_write_mask(intr));
   } else {
      bool clamp = nir_alu_type_get_base_type(oldtype) != nir_type_float;
      nir_ssa_def *val = nir_load_var(b, typeconv[rt].var);
      val = nir_convert_with_rounding(b, val, newtype, oldtype,
                                      nir_rounding_mode_undef, clamp);
      nir_ssa_def_rewrite_uses(&intr->dest.ssa, val);
   }

   nir_instr_remove(instr);

   return true;
}
#endif

static void
panvk_lower_blend(struct panfrost_device *pdev,
                  nir_shader *nir,
                  struct panfrost_compile_inputs *inputs,
                  struct pan_blend_state *blend_state,
                  bool static_blend_constants)
{
   nir_lower_blend_options options = {
      .logicop_enable = blend_state->logicop_enable,
      .logicop_func = blend_state->logicop_func,
   };

#if PAN_ARCH <= 5
   struct panvk_lower_blend_type_conv typeconv[8] = { 0 };
#endif
   bool lower_blend = false;

   for (unsigned rt = 0; rt < blend_state->rt_count; rt++) {
      struct pan_blend_rt_state *rt_state = &blend_state->rts[rt];

      if (!panvk_per_arch(blend_needs_lowering)(pdev, blend_state, rt))
         continue;

      enum pipe_format fmt = rt_state->format;

      options.format[rt] = fmt;
      options.rt[rt].colormask = rt_state->equation.color_mask;

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

      /* Update the equation to force a color replacement */
      rt_state->equation.color_mask = 0xf;
      rt_state->equation.rgb_func = BLEND_FUNC_ADD;
      rt_state->equation.rgb_src_factor = BLEND_FACTOR_ZERO;
      rt_state->equation.rgb_invert_src_factor = true;
      rt_state->equation.rgb_dst_factor = BLEND_FACTOR_ZERO;
      rt_state->equation.rgb_invert_dst_factor = false;
      rt_state->equation.alpha_func = BLEND_FUNC_ADD;
      rt_state->equation.alpha_src_factor = BLEND_FACTOR_ZERO;
      rt_state->equation.alpha_invert_src_factor = true;
      rt_state->equation.alpha_dst_factor = BLEND_FACTOR_ZERO;
      rt_state->equation.alpha_invert_dst_factor = false;
      lower_blend = true;

#if PAN_ARCH >= 6
      inputs->bifrost.static_rt_conv = true;
      inputs->bifrost.rt_conv[rt] =
         GENX(pan_blend_get_internal_desc)(pdev, fmt, rt, 32, false) >> 32;
#else
      if (!panfrost_blendable_formats_v6[fmt].internal) {
         nir_variable *outvar =
            nir_find_variable_with_location(nir, nir_var_shader_out, FRAG_RESULT_DATA0 + rt);
         if (!outvar && !rt)
            outvar = nir_find_variable_with_location(nir, nir_var_shader_out, FRAG_RESULT_COLOR);

         assert(outvar);

         const struct util_format_description *format_desc =
            util_format_description(fmt);

         typeconv[rt].newtype = pan_unpacked_type_for_format(format_desc);
         typeconv[rt].oldtype = nir_get_nir_type_for_glsl_type(outvar->type);
         typeconv[rt].var =
            nir_variable_create(nir, nir_var_shader_out,
                                glsl_vector_type(nir_get_glsl_base_type_for_nir_type(typeconv[rt].newtype),
                                                 glsl_get_vector_elements(outvar->type)),
                                outvar->name);
         typeconv[rt].var->data.location = outvar->data.location;
         inputs->blend.nr_samples = rt_state->nr_samples;
         inputs->rt_formats[rt] = rt_state->format;
      }
#endif
   }

   if (lower_blend) {
#if PAN_ARCH <= 5
      NIR_PASS_V(nir, nir_shader_instructions_pass,
                 panvk_adjust_rt_type,
                 nir_metadata_block_index |
                 nir_metadata_dominance,
                 &typeconv);
      nir_remove_dead_derefs(nir);
      nir_remove_dead_variables(nir, nir_var_shader_out, NULL);
#endif

      NIR_PASS_V(nir, nir_lower_blend, options);

      if (static_blend_constants) {
         const nir_const_value constants[4] = {
            { .f32 = CLAMP(blend_state->constants[0], 0.0f, 1.0f) },
            { .f32 = CLAMP(blend_state->constants[1], 0.0f, 1.0f) },
            { .f32 = CLAMP(blend_state->constants[2], 0.0f, 1.0f) },
            { .f32 = CLAMP(blend_state->constants[3], 0.0f, 1.0f) },
         };
         NIR_PASS_V(nir, nir_shader_instructions_pass,
                    panvk_inline_blend_constants,
                    nir_metadata_block_index |
                    nir_metadata_dominance,
                    (void *)constants);
      }
   }
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
                                        GENX(pan_shader_get_compiler_options)());
   if (!nir) {
      vk_free2(&dev->vk.alloc, alloc, shader);
      return NULL;
   }

   struct panfrost_compile_inputs inputs = {
      .gpu_id = pdev->gpu_id,
      .no_ubo_to_push = true,
      .no_idvs = true, /* TODO */
      .sysval_ubo = sysval_ubo,
   };

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

   if (stage == MESA_SHADER_FRAGMENT)
      panvk_lower_blend(pdev, nir, &inputs, blend_state, static_blend_constants);

   NIR_PASS_V(nir, nir_lower_uniforms_to_ubo, true, false);
   NIR_PASS_V(nir, nir_lower_explicit_io,
              nir_var_mem_ubo | nir_var_mem_ssbo,
              nir_address_format_32bit_index_offset);

   nir_assign_io_var_locations(nir, nir_var_shader_in, &nir->num_inputs, stage);
   nir_assign_io_var_locations(nir, nir_var_shader_out, &nir->num_outputs, stage);

   NIR_PASS_V(nir, nir_lower_system_values);
   NIR_PASS_V(nir, nir_lower_compute_system_values, NULL);

   NIR_PASS_V(nir, nir_split_var_copies);
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

   GENX(pan_shader_compile)(nir, &inputs, &shader->binary, &shader->info);

   /* Patch the descriptor count */
   shader->info.ubo_count =
      shader->info.sysvals.sysval_count ? sysval_ubo + 1 : layout->num_ubos;
   shader->info.sampler_count = layout->num_samplers;
   shader->info.texture_count = layout->num_textures;

   shader->sysval_ubo = sysval_ubo;

   ralloc_free(nir);

   return shader;
}
