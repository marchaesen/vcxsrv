/*
 * Copyright © 2021 Collabora Ltd.
 *
 * Derived from tu_shader.c which is:
 * Copyright © 2019 Google LLC
 *
 * Also derived from anv_pipeline.c which is
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "genxml/gen_macros.h"

#include "panvk_private.h"

#include "nir_builder.h"
#include "nir_deref.h"
#include "nir_lower_blend.h"
#include "nir_conversion_builder.h"
#include "spirv/nir_spirv.h"
#include "util/mesa-sha1.h"
#include "vk_shader_module.h"

#include "pan_shader.h"
#include "util/pan_lower_framebuffer.h"

#include "vk_util.h"

static void
panvk_init_sysvals(struct panfrost_sysvals *sysvals,
                   gl_shader_stage stage)
{
   memset(sysvals, 0, sizeof(*sysvals));

#define SYSVAL_SLOT(name) \
   (assert(offsetof(struct panvk_sysvals, name) % 16 == 0), \
    offsetof(struct panvk_sysvals, name) / 16)

#define INIT_SYSVAL(name, SYSVAL) \
   sysvals->sysvals[SYSVAL_SLOT(name)] = PAN_SYSVAL_##SYSVAL

   if (gl_shader_stage_is_compute(stage)) {
      INIT_SYSVAL(num_work_groups, NUM_WORK_GROUPS);
      INIT_SYSVAL(local_group_size, LOCAL_GROUP_SIZE);
   } else {
      INIT_SYSVAL(viewport_scale, VIEWPORT_SCALE);
      INIT_SYSVAL(viewport_offset, VIEWPORT_OFFSET);
      INIT_SYSVAL(vertex_instance_offsets, VERTEX_INSTANCE_OFFSETS);
      INIT_SYSVAL(blend_constants, BLEND_CONSTANTS);
   }
   sysvals->sysval_count = SYSVAL_SLOT(dyn_ssbos);

#undef SYSVAL_SLOT
#undef INIT_SYSVAL
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

      inputs->bifrost.static_rt_conv = true;
      inputs->bifrost.rt_conv[rt] =
         GENX(pan_blend_get_internal_desc)(pdev, fmt, rt, 32, false) >> 32;
   }

   if (lower_blend) {
      NIR_PASS_V(nir, nir_lower_blend, &options);

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

static bool
panvk_lower_load_push_constant(nir_builder *b, nir_instr *instr, void *data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   if (intr->intrinsic != nir_intrinsic_load_push_constant)
      return false;

   b->cursor = nir_before_instr(instr);
   nir_ssa_def *ubo_load =
      nir_load_ubo(b, nir_dest_num_components(intr->dest),
                   nir_dest_bit_size(intr->dest),
                   nir_imm_int(b, PANVK_PUSH_CONST_UBO_INDEX),
                   intr->src[0].ssa,
                   .align_mul = nir_dest_bit_size(intr->dest) / 8,
                   .align_offset = 0,
                   .range_base = nir_intrinsic_base(intr),
                   .range = nir_intrinsic_range(intr));
   nir_ssa_def_rewrite_uses(&intr->dest.ssa, ubo_load);
   nir_instr_remove(instr);
   return true;
}

static void
shared_type_info(const struct glsl_type *type, unsigned *size, unsigned *align)
{
   assert(glsl_type_is_vector_or_scalar(type));

   uint32_t comp_size = glsl_type_is_boolean(type)
      ? 4 : glsl_get_bit_size(type) / 8;
   unsigned length = glsl_get_vector_elements(type);
   *size = comp_size * length,
   *align = comp_size * (length == 3 ? 4 : length);
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
   VK_FROM_HANDLE(vk_shader_module, module, stage_info->module);
   struct panfrost_device *pdev = &dev->physical_device->pdev;
   struct panvk_shader *shader;

   shader = vk_zalloc2(&dev->vk.alloc, alloc, sizeof(*shader), 8,
                       VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (!shader)
      return NULL;

   util_dynarray_init(&shader->binary, NULL);

   /* TODO these are made-up */
   const struct spirv_to_nir_options spirv_options = {
      .caps = {
         .variable_pointers = true,
      },
      .ubo_addr_format = nir_address_format_32bit_index_offset,
      .ssbo_addr_format = dev->vk.enabled_features.robustBufferAccess ?
                          nir_address_format_64bit_bounded_global :
                          nir_address_format_64bit_global_32bit_offset,
   };

   nir_shader *nir;
   VkResult result = vk_shader_module_to_nir(&dev->vk, module, stage,
                                             stage_info->pName,
                                             stage_info->pSpecializationInfo,
                                             &spirv_options,
                                             GENX(pan_shader_get_compiler_options)(),
                                             NULL, &nir);
   if (result != VK_SUCCESS) {
      vk_free2(&dev->vk.alloc, alloc, shader);
      return NULL;
   }

   NIR_PASS_V(nir, nir_lower_io_to_temporaries,
              nir_shader_get_entrypoint(nir), true, true);

   struct panfrost_sysvals fixed_sysvals;
   panvk_init_sysvals(&fixed_sysvals, stage);

   struct panfrost_compile_inputs inputs = {
      .gpu_id = pdev->gpu_id,
      .no_ubo_to_push = true,
      .no_idvs = true, /* TODO */
      .fixed_sysval_ubo = sysval_ubo,
      .fixed_sysval_layout = &fixed_sysvals,
   };

   NIR_PASS_V(nir, nir_lower_indirect_derefs,
              nir_var_shader_in | nir_var_shader_out,
              UINT32_MAX);

   NIR_PASS_V(nir, nir_opt_copy_prop_vars);
   NIR_PASS_V(nir, nir_opt_combine_stores, nir_var_all);
   NIR_PASS_V(nir, nir_opt_trivial_continues);

   /* Do texture lowering here.  Yes, it's a duplication of the texture
    * lowering in bifrost_compile.  However, we need to lower texture stuff
    * now, before we call panvk_per_arch(nir_lower_descriptors)() because some
    * of the texture lowering generates nir_texop_txs which we handle as part
    * of descriptor lowering.
    *
    * TODO: We really should be doing this in common code, not dpulicated in
    * panvk.  In order to do that, we need to rework the panfrost compile
    * flow to look more like the Intel flow:
    *
    *  1. Compile SPIR-V to NIR and maybe do a tiny bit of lowering that needs
    *     to be done really early.
    *
    *  2. bi_preprocess_nir: Does common lowering and runs the optimization
    *     loop.  Nothing here should be API-specific.
    *
    *  3. Do additional lowering in panvk
    *
    *  4. bi_postprocess_nir: Does final lowering and runs the optimization
    *     loop again.  This can happen as part of the final compile.
    *
    * This would give us a better place to do panvk-specific lowering.
    */
   nir_lower_tex_options lower_tex_options = {
      .lower_txs_lod = true,
      .lower_txp = ~0,
      .lower_tg4_broadcom_swizzle = true,
      .lower_txd = true,
      .lower_invalid_implicit_lod = true,
   };
   NIR_PASS_V(nir, nir_lower_tex, &lower_tex_options);

   NIR_PASS_V(nir, panvk_per_arch(nir_lower_descriptors),
              dev, layout, &shader->has_img_access);

   NIR_PASS_V(nir, nir_lower_explicit_io, nir_var_mem_ubo,
              nir_address_format_32bit_index_offset);
   NIR_PASS_V(nir, nir_lower_explicit_io, nir_var_mem_ssbo,
              spirv_options.ssbo_addr_format);
   NIR_PASS_V(nir, nir_lower_explicit_io,
              nir_var_mem_push_const,
              nir_address_format_32bit_offset);

   if (gl_shader_stage_uses_workgroup(stage)) {
      if (!nir->info.shared_memory_explicit_layout) {
         NIR_PASS_V(nir, nir_lower_vars_to_explicit_types,
                    nir_var_mem_shared,
                    shared_type_info);
      }

      NIR_PASS_V(nir, nir_lower_explicit_io,
                 nir_var_mem_shared,
                 nir_address_format_32bit_offset);
   }

   NIR_PASS_V(nir, nir_shader_instructions_pass,
              panvk_lower_load_push_constant,
              nir_metadata_block_index |
              nir_metadata_dominance,
              (void *)layout);

   NIR_PASS_V(nir, nir_lower_system_values);
   NIR_PASS_V(nir, nir_lower_compute_system_values, NULL);

   NIR_PASS_V(nir, nir_split_var_copies);
   NIR_PASS_V(nir, nir_lower_var_copies);

   /* We have to run nir_lower_blend() after we've gotten rid of copies (it
    * requires load/store) and before we assign output locations.
    */
   if (stage == MESA_SHADER_FRAGMENT) {
      /* This is required for nir_lower_blend */
      NIR_PASS_V(nir, nir_lower_io_arrays_to_elements_no_indirects, true);
      panvk_lower_blend(pdev, nir, &inputs, blend_state, static_blend_constants);
   }

   nir_assign_io_var_locations(nir, nir_var_shader_in, &nir->num_inputs, stage);
   nir_assign_io_var_locations(nir, nir_var_shader_out, &nir->num_outputs, stage);

   /* Needed to turn shader_temp into function_temp since the backend only
    * handles the latter for now.
    */
   NIR_PASS_V(nir, nir_lower_global_vars_to_local);

   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));
   if (unlikely(dev->physical_device->instance->debug_flags & PANVK_DEBUG_NIR)) {
      fprintf(stderr, "translated nir:\n");
      nir_print_shader(nir, stderr);
   }

   GENX(pan_shader_compile)(nir, &inputs, &shader->binary, &shader->info);

   /* System values shouldn't have changed */
   assert(memcmp(&shader->info.sysvals, &fixed_sysvals,
                 sizeof(fixed_sysvals)) == 0);

   /* Patch the descriptor count */
   shader->info.ubo_count = PANVK_NUM_BUILTIN_UBOS +
                            layout->num_ubos + layout->num_dyn_ubos;
   shader->info.sampler_count = layout->num_samplers;
   shader->info.texture_count = layout->num_textures;
   if (shader->has_img_access)
      shader->info.attribute_count += layout->num_imgs;

   shader->sysval_ubo = sysval_ubo;
   shader->local_size.x = nir->info.workgroup_size[0];
   shader->local_size.y = nir->info.workgroup_size[1];
   shader->local_size.z = nir->info.workgroup_size[2];

   ralloc_free(nir);

   return shader;
}
