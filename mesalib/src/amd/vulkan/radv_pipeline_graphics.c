/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "meta/radv_meta.h"
#include "nir/nir.h"
#include "nir/nir_builder.h"
#include "nir/nir_serialize.h"
#include "nir/nir_xfb_info.h"
#include "nir/radv_nir.h"
#include "spirv/nir_spirv.h"
#include "util/disk_cache.h"
#include "util/mesa-sha1.h"
#include "util/os_time.h"
#include "util/u_atomic.h"
#include "radv_cs.h"
#include "radv_debug.h"
#include "radv_entrypoints.h"
#include "radv_formats.h"
#include "radv_physical_device.h"
#include "radv_pipeline_binary.h"
#include "radv_pipeline_cache.h"
#include "radv_rmv.h"
#include "radv_shader.h"
#include "radv_shader_args.h"
#include "vk_nir_convert_ycbcr.h"
#include "vk_pipeline.h"
#include "vk_render_pass.h"
#include "vk_util.h"

#include "util/u_debug.h"
#include "ac_binary.h"
#include "ac_formats.h"
#include "ac_nir.h"
#include "ac_shader_util.h"
#include "aco_interface.h"
#include "sid.h"

static bool
radv_is_static_vrs_enabled(const struct vk_graphics_pipeline_state *state)
{
   if (!state->fsr)
      return false;

   return state->fsr->fragment_size.width != 1 || state->fsr->fragment_size.height != 1 ||
          state->fsr->combiner_ops[0] != VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR ||
          state->fsr->combiner_ops[1] != VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR;
}

static bool
radv_is_vrs_enabled(const struct vk_graphics_pipeline_state *state)
{
   return radv_is_static_vrs_enabled(state) || BITSET_TEST(state->dynamic, MESA_VK_DYNAMIC_FSR);
}

static bool
radv_pipeline_has_ds_attachments(const struct vk_render_pass_state *rp)
{
   return rp->depth_attachment_format != VK_FORMAT_UNDEFINED || rp->stencil_attachment_format != VK_FORMAT_UNDEFINED;
}

static bool
radv_pipeline_has_color_attachments(const struct vk_render_pass_state *rp)
{
   for (uint32_t i = 0; i < rp->color_attachment_count; ++i) {
      if (rp->color_attachment_formats[i] != VK_FORMAT_UNDEFINED)
         return true;
   }

   return false;
}

/**
 * Get rid of DST in the blend factors by commuting the operands:
 *    func(src * DST, dst * 0) ---> func(src * 0, dst * SRC)
 */
void
radv_blend_remove_dst(VkBlendOp *func, VkBlendFactor *src_factor, VkBlendFactor *dst_factor, VkBlendFactor expected_dst,
                      VkBlendFactor replacement_src)
{
   if (*src_factor == expected_dst && *dst_factor == VK_BLEND_FACTOR_ZERO) {
      *src_factor = VK_BLEND_FACTOR_ZERO;
      *dst_factor = replacement_src;

      /* Commuting the operands requires reversing subtractions. */
      if (*func == VK_BLEND_OP_SUBTRACT)
         *func = VK_BLEND_OP_REVERSE_SUBTRACT;
      else if (*func == VK_BLEND_OP_REVERSE_SUBTRACT)
         *func = VK_BLEND_OP_SUBTRACT;
   }
}

static unsigned
radv_choose_spi_color_format(const struct radv_device *device, VkFormat vk_format, bool blend_enable,
                             bool blend_need_alpha)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   const struct util_format_description *desc = vk_format_description(vk_format);
   bool use_rbplus = pdev->info.rbplus_allowed;
   struct ac_spi_color_formats formats = {0};
   unsigned format, ntype, swap;

   format = ac_get_cb_format(pdev->info.gfx_level, desc->format);
   ntype = ac_get_cb_number_type(desc->format);
   swap = ac_translate_colorswap(pdev->info.gfx_level, desc->format, false);

   ac_choose_spi_color_formats(format, swap, ntype, false, use_rbplus, &formats);

   if (blend_enable && blend_need_alpha)
      return formats.blend_alpha;
   else if (blend_need_alpha)
      return formats.alpha;
   else if (blend_enable)
      return formats.blend;
   else
      return formats.normal;
}

static bool
format_is_int8(VkFormat format)
{
   const struct util_format_description *desc = vk_format_description(format);
   int channel = vk_format_get_first_non_void_channel(format);

   return channel >= 0 && desc->channel[channel].pure_integer && desc->channel[channel].size == 8;
}

static bool
format_is_int10(VkFormat format)
{
   const struct util_format_description *desc = vk_format_description(format);

   if (desc->nr_channels != 4)
      return false;
   for (unsigned i = 0; i < 4; i++) {
      if (desc->channel[i].pure_integer && desc->channel[i].size == 10)
         return true;
   }
   return false;
}

static bool
format_is_float32(VkFormat format)
{
   const struct util_format_description *desc = vk_format_description(format);
   int channel = vk_format_get_first_non_void_channel(format);

   return channel >= 0 && desc->channel[channel].type == UTIL_FORMAT_TYPE_FLOAT && desc->channel[channel].size == 32;
}

/*
 * Ordered so that for each i,
 * radv_format_meta_fs_key(radv_fs_key_format_exemplars[i]) == i.
 */
const VkFormat radv_fs_key_format_exemplars[NUM_META_FS_KEYS] = {
   VK_FORMAT_R32_SFLOAT,         VK_FORMAT_R32G32_SFLOAT,           VK_FORMAT_R8G8B8A8_UNORM,
   VK_FORMAT_R16G16B16A16_UNORM, VK_FORMAT_R16G16B16A16_SNORM,      VK_FORMAT_R16G16B16A16_UINT,
   VK_FORMAT_R16G16B16A16_SINT,  VK_FORMAT_R32G32B32A32_SFLOAT,     VK_FORMAT_R8G8B8A8_UINT,
   VK_FORMAT_R8G8B8A8_SINT,      VK_FORMAT_A2R10G10B10_UINT_PACK32, VK_FORMAT_A2R10G10B10_SINT_PACK32,
};

unsigned
radv_format_meta_fs_key(struct radv_device *device, VkFormat format)
{
   unsigned col_format = radv_choose_spi_color_format(device, format, false, false);
   assert(col_format != V_028714_SPI_SHADER_32_AR);

   bool is_int8 = format_is_int8(format);
   bool is_int10 = format_is_int10(format);

   if (col_format == V_028714_SPI_SHADER_UINT16_ABGR && is_int8)
      return 8;
   else if (col_format == V_028714_SPI_SHADER_SINT16_ABGR && is_int8)
      return 9;
   else if (col_format == V_028714_SPI_SHADER_UINT16_ABGR && is_int10)
      return 10;
   else if (col_format == V_028714_SPI_SHADER_SINT16_ABGR && is_int10)
      return 11;
   else {
      if (col_format >= V_028714_SPI_SHADER_32_AR)
         --col_format; /* Skip V_028714_SPI_SHADER_32_AR  since there is no such VkFormat */

      --col_format; /* Skip V_028714_SPI_SHADER_ZERO */
      return col_format;
   }
}

static bool
radv_pipeline_needs_ps_epilog(const struct vk_graphics_pipeline_state *state,
                              VkGraphicsPipelineLibraryFlagBitsEXT lib_flags)
{
   /* Use a PS epilog when the fragment shader is compiled without the fragment output interface. */
   if ((state->shader_stages & VK_SHADER_STAGE_FRAGMENT_BIT) &&
       (lib_flags & VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT) &&
       !(lib_flags & VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_OUTPUT_INTERFACE_BIT_EXT))
      return true;

   /* These dynamic states need to compile PS epilogs on-demand. */
   if (BITSET_TEST(state->dynamic, MESA_VK_DYNAMIC_CB_BLEND_ENABLES) ||
       BITSET_TEST(state->dynamic, MESA_VK_DYNAMIC_CB_WRITE_MASKS) ||
       BITSET_TEST(state->dynamic, MESA_VK_DYNAMIC_CB_BLEND_EQUATIONS) ||
       BITSET_TEST(state->dynamic, MESA_VK_DYNAMIC_MS_ALPHA_TO_COVERAGE_ENABLE) ||
       BITSET_TEST(state->dynamic, MESA_VK_DYNAMIC_MS_ALPHA_TO_ONE_ENABLE))
      return true;

   return false;
}

static bool
radv_pipeline_uses_vrs_attachment(const struct radv_graphics_pipeline *pipeline,
                                  const struct vk_graphics_pipeline_state *state)
{
   VkPipelineCreateFlags2 create_flags = pipeline->base.create_flags;
   if (state->rp)
      create_flags |= state->pipeline_flags;

   return (create_flags & VK_PIPELINE_CREATE_2_RENDERING_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR) != 0;
}

static void
radv_pipeline_init_multisample_state(const struct radv_device *device, struct radv_graphics_pipeline *pipeline,
                                     const VkGraphicsPipelineCreateInfo *pCreateInfo,
                                     const struct vk_graphics_pipeline_state *state)
{
   struct radv_multisample_state *ms = &pipeline->ms;

   /* From the Vulkan 1.1.129 spec, 26.7. Sample Shading:
    *
    * "Sample shading is enabled for a graphics pipeline:
    *
    * - If the interface of the fragment shader entry point of the
    *   graphics pipeline includes an input variable decorated
    *   with SampleId or SamplePosition. In this case
    *   minSampleShadingFactor takes the value 1.0.
    * - Else if the sampleShadingEnable member of the
    *   VkPipelineMultisampleStateCreateInfo structure specified
    *   when creating the graphics pipeline is set to VK_TRUE. In
    *   this case minSampleShadingFactor takes the value of
    *   VkPipelineMultisampleStateCreateInfo::minSampleShading.
    *
    * Otherwise, sample shading is considered disabled."
    */
   if (state->ms && state->ms->sample_shading_enable) {
      ms->sample_shading_enable = true;
      ms->min_sample_shading = state->ms->min_sample_shading;
   }
}

static uint32_t
radv_conv_tess_prim_to_gs_out(enum tess_primitive_mode prim)
{
   switch (prim) {
   case TESS_PRIMITIVE_TRIANGLES:
   case TESS_PRIMITIVE_QUADS:
      return V_028A6C_TRISTRIP;
   case TESS_PRIMITIVE_ISOLINES:
      return V_028A6C_LINESTRIP;
   default:
      assert(0);
      return 0;
   }
}

static uint64_t
radv_dynamic_state_mask(VkDynamicState state)
{
   switch (state) {
   case VK_DYNAMIC_STATE_VIEWPORT:
   case VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT:
      return RADV_DYNAMIC_VIEWPORT;
   case VK_DYNAMIC_STATE_SCISSOR:
   case VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT:
      return RADV_DYNAMIC_SCISSOR;
   case VK_DYNAMIC_STATE_LINE_WIDTH:
      return RADV_DYNAMIC_LINE_WIDTH;
   case VK_DYNAMIC_STATE_DEPTH_BIAS:
      return RADV_DYNAMIC_DEPTH_BIAS;
   case VK_DYNAMIC_STATE_BLEND_CONSTANTS:
      return RADV_DYNAMIC_BLEND_CONSTANTS;
   case VK_DYNAMIC_STATE_DEPTH_BOUNDS:
      return RADV_DYNAMIC_DEPTH_BOUNDS;
   case VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK:
      return RADV_DYNAMIC_STENCIL_COMPARE_MASK;
   case VK_DYNAMIC_STATE_STENCIL_WRITE_MASK:
      return RADV_DYNAMIC_STENCIL_WRITE_MASK;
   case VK_DYNAMIC_STATE_STENCIL_REFERENCE:
      return RADV_DYNAMIC_STENCIL_REFERENCE;
   case VK_DYNAMIC_STATE_DISCARD_RECTANGLE_EXT:
      return RADV_DYNAMIC_DISCARD_RECTANGLE;
   case VK_DYNAMIC_STATE_SAMPLE_LOCATIONS_EXT:
      return RADV_DYNAMIC_SAMPLE_LOCATIONS;
   case VK_DYNAMIC_STATE_LINE_STIPPLE:
      return RADV_DYNAMIC_LINE_STIPPLE;
   case VK_DYNAMIC_STATE_CULL_MODE:
      return RADV_DYNAMIC_CULL_MODE;
   case VK_DYNAMIC_STATE_FRONT_FACE:
      return RADV_DYNAMIC_FRONT_FACE;
   case VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY:
      return RADV_DYNAMIC_PRIMITIVE_TOPOLOGY;
   case VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE:
      return RADV_DYNAMIC_DEPTH_TEST_ENABLE;
   case VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE:
      return RADV_DYNAMIC_DEPTH_WRITE_ENABLE;
   case VK_DYNAMIC_STATE_DEPTH_COMPARE_OP:
      return RADV_DYNAMIC_DEPTH_COMPARE_OP;
   case VK_DYNAMIC_STATE_DEPTH_BOUNDS_TEST_ENABLE:
      return RADV_DYNAMIC_DEPTH_BOUNDS_TEST_ENABLE;
   case VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE:
      return RADV_DYNAMIC_STENCIL_TEST_ENABLE;
   case VK_DYNAMIC_STATE_STENCIL_OP:
      return RADV_DYNAMIC_STENCIL_OP;
   case VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE:
      return RADV_DYNAMIC_VERTEX_INPUT_BINDING_STRIDE;
   case VK_DYNAMIC_STATE_FRAGMENT_SHADING_RATE_KHR:
      return RADV_DYNAMIC_FRAGMENT_SHADING_RATE;
   case VK_DYNAMIC_STATE_PATCH_CONTROL_POINTS_EXT:
      return RADV_DYNAMIC_PATCH_CONTROL_POINTS;
   case VK_DYNAMIC_STATE_RASTERIZER_DISCARD_ENABLE:
      return RADV_DYNAMIC_RASTERIZER_DISCARD_ENABLE;
   case VK_DYNAMIC_STATE_DEPTH_BIAS_ENABLE:
      return RADV_DYNAMIC_DEPTH_BIAS_ENABLE;
   case VK_DYNAMIC_STATE_LOGIC_OP_EXT:
      return RADV_DYNAMIC_LOGIC_OP;
   case VK_DYNAMIC_STATE_PRIMITIVE_RESTART_ENABLE:
      return RADV_DYNAMIC_PRIMITIVE_RESTART_ENABLE;
   case VK_DYNAMIC_STATE_COLOR_WRITE_ENABLE_EXT:
      return RADV_DYNAMIC_COLOR_WRITE_ENABLE;
   case VK_DYNAMIC_STATE_VERTEX_INPUT_EXT:
      return RADV_DYNAMIC_VERTEX_INPUT;
   case VK_DYNAMIC_STATE_POLYGON_MODE_EXT:
      return RADV_DYNAMIC_POLYGON_MODE;
   case VK_DYNAMIC_STATE_TESSELLATION_DOMAIN_ORIGIN_EXT:
      return RADV_DYNAMIC_TESS_DOMAIN_ORIGIN;
   case VK_DYNAMIC_STATE_LOGIC_OP_ENABLE_EXT:
      return RADV_DYNAMIC_LOGIC_OP_ENABLE;
   case VK_DYNAMIC_STATE_LINE_STIPPLE_ENABLE_EXT:
      return RADV_DYNAMIC_LINE_STIPPLE_ENABLE;
   case VK_DYNAMIC_STATE_ALPHA_TO_COVERAGE_ENABLE_EXT:
      return RADV_DYNAMIC_ALPHA_TO_COVERAGE_ENABLE;
   case VK_DYNAMIC_STATE_SAMPLE_MASK_EXT:
      return RADV_DYNAMIC_SAMPLE_MASK;
   case VK_DYNAMIC_STATE_DEPTH_CLIP_ENABLE_EXT:
      return RADV_DYNAMIC_DEPTH_CLIP_ENABLE;
   case VK_DYNAMIC_STATE_CONSERVATIVE_RASTERIZATION_MODE_EXT:
      return RADV_DYNAMIC_CONSERVATIVE_RAST_MODE;
   case VK_DYNAMIC_STATE_DEPTH_CLIP_NEGATIVE_ONE_TO_ONE_EXT:
      return RADV_DYNAMIC_DEPTH_CLIP_NEGATIVE_ONE_TO_ONE;
   case VK_DYNAMIC_STATE_PROVOKING_VERTEX_MODE_EXT:
      return RADV_DYNAMIC_PROVOKING_VERTEX_MODE;
   case VK_DYNAMIC_STATE_DEPTH_CLAMP_ENABLE_EXT:
      return RADV_DYNAMIC_DEPTH_CLAMP_ENABLE;
   case VK_DYNAMIC_STATE_COLOR_WRITE_MASK_EXT:
      return RADV_DYNAMIC_COLOR_WRITE_MASK;
   case VK_DYNAMIC_STATE_COLOR_BLEND_ENABLE_EXT:
      return RADV_DYNAMIC_COLOR_BLEND_ENABLE;
   case VK_DYNAMIC_STATE_RASTERIZATION_SAMPLES_EXT:
      return RADV_DYNAMIC_RASTERIZATION_SAMPLES;
   case VK_DYNAMIC_STATE_LINE_RASTERIZATION_MODE_EXT:
      return RADV_DYNAMIC_LINE_RASTERIZATION_MODE;
   case VK_DYNAMIC_STATE_COLOR_BLEND_EQUATION_EXT:
      return RADV_DYNAMIC_COLOR_BLEND_EQUATION;
   case VK_DYNAMIC_STATE_DISCARD_RECTANGLE_ENABLE_EXT:
      return RADV_DYNAMIC_DISCARD_RECTANGLE_ENABLE;
   case VK_DYNAMIC_STATE_DISCARD_RECTANGLE_MODE_EXT:
      return RADV_DYNAMIC_DISCARD_RECTANGLE_MODE;
   case VK_DYNAMIC_STATE_ATTACHMENT_FEEDBACK_LOOP_ENABLE_EXT:
      return RADV_DYNAMIC_ATTACHMENT_FEEDBACK_LOOP_ENABLE;
   case VK_DYNAMIC_STATE_SAMPLE_LOCATIONS_ENABLE_EXT:
      return RADV_DYNAMIC_SAMPLE_LOCATIONS_ENABLE;
   case VK_DYNAMIC_STATE_ALPHA_TO_ONE_ENABLE_EXT:
      return RADV_DYNAMIC_ALPHA_TO_ONE_ENABLE;
   case VK_DYNAMIC_STATE_DEPTH_CLAMP_RANGE_EXT:
      return RADV_DYNAMIC_DEPTH_CLAMP_RANGE;
   default:
      unreachable("Unhandled dynamic state");
   }
}

#define RADV_DYNAMIC_CB_STATES                                                                                         \
   (RADV_DYNAMIC_LOGIC_OP_ENABLE | RADV_DYNAMIC_LOGIC_OP | RADV_DYNAMIC_COLOR_WRITE_ENABLE |                           \
    RADV_DYNAMIC_COLOR_WRITE_MASK | RADV_DYNAMIC_COLOR_BLEND_ENABLE | RADV_DYNAMIC_COLOR_BLEND_EQUATION |              \
    RADV_DYNAMIC_BLEND_CONSTANTS)

static bool
radv_pipeline_is_blend_enabled(const struct radv_graphics_pipeline *pipeline, const struct vk_color_blend_state *cb)
{
   /* If we don't know then we have to assume that blend may be enabled. cb may also be NULL in this
    * case.
    */
   if (pipeline->dynamic_states & (RADV_DYNAMIC_COLOR_BLEND_ENABLE | RADV_DYNAMIC_COLOR_WRITE_MASK))
      return true;

   /* If we have the blend enable state, then cb being NULL indicates no attachments are written. */
   if (cb) {
      for (uint32_t i = 0; i < cb->attachment_count; i++) {
         if (cb->attachments[i].write_mask && cb->attachments[i].blend_enable)
            return true;
      }
   }

   return false;
}

static uint64_t
radv_pipeline_needed_dynamic_state(const struct radv_device *device, const struct radv_graphics_pipeline *pipeline,
                                   const struct vk_graphics_pipeline_state *state)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   bool has_color_att = radv_pipeline_has_color_attachments(state->rp);
   bool raster_enabled =
      !state->rs->rasterizer_discard_enable || (pipeline->dynamic_states & RADV_DYNAMIC_RASTERIZER_DISCARD_ENABLE);
   uint64_t states = RADV_DYNAMIC_ALL;

   if (pdev->info.gfx_level < GFX10_3)
      states &= ~RADV_DYNAMIC_FRAGMENT_SHADING_RATE;

   /* Disable dynamic states that are useless to mesh shading. */
   if (radv_pipeline_has_stage(pipeline, MESA_SHADER_MESH)) {
      if (!raster_enabled)
         return RADV_DYNAMIC_RASTERIZER_DISCARD_ENABLE;

      states &= ~(RADV_DYNAMIC_VERTEX_INPUT | RADV_DYNAMIC_VERTEX_INPUT_BINDING_STRIDE |
                  RADV_DYNAMIC_PRIMITIVE_RESTART_ENABLE | RADV_DYNAMIC_PRIMITIVE_TOPOLOGY);
   }

   /* Disable dynamic states that are useless when rasterization is disabled. */
   if (!raster_enabled) {
      states = RADV_DYNAMIC_PRIMITIVE_TOPOLOGY | RADV_DYNAMIC_VERTEX_INPUT_BINDING_STRIDE |
               RADV_DYNAMIC_PRIMITIVE_RESTART_ENABLE | RADV_DYNAMIC_RASTERIZER_DISCARD_ENABLE |
               RADV_DYNAMIC_VERTEX_INPUT;

      if (pipeline->active_stages & VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT)
         states |= RADV_DYNAMIC_PATCH_CONTROL_POINTS | RADV_DYNAMIC_TESS_DOMAIN_ORIGIN;

      return states;
   }

   if (!state->rs->depth_bias.enable && !(pipeline->dynamic_states & RADV_DYNAMIC_DEPTH_BIAS_ENABLE))
      states &= ~RADV_DYNAMIC_DEPTH_BIAS;

   if (!(pipeline->dynamic_states & RADV_DYNAMIC_DEPTH_BOUNDS_TEST_ENABLE) &&
       (!state->ds || !state->ds->depth.bounds_test.enable))
      states &= ~RADV_DYNAMIC_DEPTH_BOUNDS;

   if (!(pipeline->dynamic_states & RADV_DYNAMIC_STENCIL_TEST_ENABLE) &&
       (!state->ds || !state->ds->stencil.test_enable))
      states &= ~(RADV_DYNAMIC_STENCIL_COMPARE_MASK | RADV_DYNAMIC_STENCIL_WRITE_MASK | RADV_DYNAMIC_STENCIL_REFERENCE |
                  RADV_DYNAMIC_STENCIL_OP);

   if (!(pipeline->dynamic_states & RADV_DYNAMIC_DISCARD_RECTANGLE_ENABLE) && !state->dr->rectangle_count)
      states &= ~RADV_DYNAMIC_DISCARD_RECTANGLE;

   if (!(pipeline->dynamic_states & RADV_DYNAMIC_SAMPLE_LOCATIONS_ENABLE) &&
       (!state->ms || !state->ms->sample_locations_enable))
      states &= ~RADV_DYNAMIC_SAMPLE_LOCATIONS;

   if (!has_color_att || !radv_pipeline_is_blend_enabled(pipeline, state->cb))
      states &= ~RADV_DYNAMIC_BLEND_CONSTANTS;

   if (!(pipeline->active_stages & VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT))
      states &= ~(RADV_DYNAMIC_PATCH_CONTROL_POINTS | RADV_DYNAMIC_TESS_DOMAIN_ORIGIN);

   return states;
}

struct radv_ia_multi_vgt_param_helpers
radv_compute_ia_multi_vgt_param(const struct radv_device *device, struct radv_shader *const *shaders)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   struct radv_ia_multi_vgt_param_helpers ia_multi_vgt_param = {0};

   ia_multi_vgt_param.ia_switch_on_eoi = false;
   if (shaders[MESA_SHADER_FRAGMENT] && shaders[MESA_SHADER_FRAGMENT]->info.ps.prim_id_input)
      ia_multi_vgt_param.ia_switch_on_eoi = true;
   if (shaders[MESA_SHADER_GEOMETRY] && shaders[MESA_SHADER_GEOMETRY]->info.uses_prim_id)
      ia_multi_vgt_param.ia_switch_on_eoi = true;
   if (shaders[MESA_SHADER_TESS_CTRL]) {
      const struct radv_shader *tes = radv_get_shader(shaders, MESA_SHADER_TESS_EVAL);

      /* SWITCH_ON_EOI must be set if PrimID is used. */
      if (shaders[MESA_SHADER_TESS_CTRL]->info.uses_prim_id || tes->info.uses_prim_id ||
          (tes->info.merged_shader_compiled_separately && shaders[MESA_SHADER_GEOMETRY]->info.uses_prim_id))
         ia_multi_vgt_param.ia_switch_on_eoi = true;
   }

   ia_multi_vgt_param.partial_vs_wave = false;
   if (shaders[MESA_SHADER_TESS_CTRL]) {
      /* Bug with tessellation and GS on Bonaire and older 2 SE chips. */
      if ((pdev->info.family == CHIP_TAHITI || pdev->info.family == CHIP_PITCAIRN ||
           pdev->info.family == CHIP_BONAIRE) &&
          shaders[MESA_SHADER_GEOMETRY])
         ia_multi_vgt_param.partial_vs_wave = true;
      /* Needed for 028B6C_DISTRIBUTION_MODE != 0 */
      if (pdev->info.has_distributed_tess) {
         if (shaders[MESA_SHADER_GEOMETRY]) {
            if (pdev->info.gfx_level <= GFX8)
               ia_multi_vgt_param.partial_es_wave = true;
         } else {
            ia_multi_vgt_param.partial_vs_wave = true;
         }
      }
   }

   if (shaders[MESA_SHADER_GEOMETRY]) {
      /* On these chips there is the possibility of a hang if the
       * pipeline uses a GS and partial_vs_wave is not set.
       *
       * This mostly does not hit 4-SE chips, as those typically set
       * ia_switch_on_eoi and then partial_vs_wave is set for pipelines
       * with GS due to another workaround.
       *
       * Reproducer: https://bugs.freedesktop.org/show_bug.cgi?id=109242
       */
      if (pdev->info.family == CHIP_TONGA || pdev->info.family == CHIP_FIJI || pdev->info.family == CHIP_POLARIS10 ||
          pdev->info.family == CHIP_POLARIS11 || pdev->info.family == CHIP_POLARIS12 ||
          pdev->info.family == CHIP_VEGAM) {
         ia_multi_vgt_param.partial_vs_wave = true;
      }
   }

   ia_multi_vgt_param.base =
      /* The following field was moved to VGT_SHADER_STAGES_EN in GFX9. */
      S_028AA8_MAX_PRIMGRP_IN_WAVE(pdev->info.gfx_level == GFX8 ? 2 : 0) |
      S_030960_EN_INST_OPT_BASIC(pdev->info.gfx_level >= GFX9) | S_030960_EN_INST_OPT_ADV(pdev->info.gfx_level >= GFX9);

   return ia_multi_vgt_param;
}

static uint32_t
radv_get_attrib_stride(const VkPipelineVertexInputStateCreateInfo *vi, uint32_t attrib_binding)
{
   for (uint32_t i = 0; i < vi->vertexBindingDescriptionCount; i++) {
      const VkVertexInputBindingDescription *input_binding = &vi->pVertexBindingDescriptions[i];

      if (input_binding->binding == attrib_binding)
         return input_binding->stride;
   }

   return 0;
}

#define ALL_GRAPHICS_LIB_FLAGS                                                                                         \
   (VK_GRAPHICS_PIPELINE_LIBRARY_VERTEX_INPUT_INTERFACE_BIT_EXT |                                                      \
    VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT |                                                   \
    VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT |                                                             \
    VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_OUTPUT_INTERFACE_BIT_EXT)

static VkGraphicsPipelineLibraryFlagBitsEXT
shader_stage_to_pipeline_library_flags(VkShaderStageFlagBits stage)
{
   assert(util_bitcount(stage) == 1);
   switch (stage) {
   case VK_SHADER_STAGE_VERTEX_BIT:
   case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT:
   case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT:
   case VK_SHADER_STAGE_GEOMETRY_BIT:
   case VK_SHADER_STAGE_TASK_BIT_EXT:
   case VK_SHADER_STAGE_MESH_BIT_EXT:
      return VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT;
   case VK_SHADER_STAGE_FRAGMENT_BIT:
      return VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT;
   default:
      unreachable("Invalid shader stage");
   }
}

static void
radv_graphics_pipeline_import_layout(struct radv_pipeline_layout *dst, const struct radv_pipeline_layout *src)
{
   for (uint32_t s = 0; s < src->num_sets; s++) {
      if (!src->set[s].layout)
         continue;

      radv_pipeline_layout_add_set(dst, s, src->set[s].layout);
   }

   dst->independent_sets |= src->independent_sets;
   dst->push_constant_size = MAX2(dst->push_constant_size, src->push_constant_size);
}

static void
radv_pipeline_import_graphics_info(struct radv_device *device, struct radv_graphics_pipeline *pipeline,
                                   const VkGraphicsPipelineCreateInfo *pCreateInfo)
{
   /* Mark all states declared dynamic at pipeline creation. */
   if (pCreateInfo->pDynamicState) {
      uint32_t count = pCreateInfo->pDynamicState->dynamicStateCount;
      for (uint32_t s = 0; s < count; s++) {
         pipeline->dynamic_states |= radv_dynamic_state_mask(pCreateInfo->pDynamicState->pDynamicStates[s]);
      }
   }

   /* Mark all active stages at pipeline creation. */
   for (uint32_t i = 0; i < pCreateInfo->stageCount; i++) {
      const VkPipelineShaderStageCreateInfo *sinfo = &pCreateInfo->pStages[i];

      pipeline->active_stages |= sinfo->stage;
   }

   if (pipeline->active_stages & VK_SHADER_STAGE_MESH_BIT_EXT) {
      pipeline->last_vgt_api_stage = MESA_SHADER_MESH;
   } else {
      pipeline->last_vgt_api_stage = util_last_bit(pipeline->active_stages & BITFIELD_MASK(MESA_SHADER_FRAGMENT)) - 1;
   }
}

static bool
radv_should_import_lib_binaries(const VkPipelineCreateFlags2 create_flags)
{
   return !(create_flags & (VK_PIPELINE_CREATE_2_LINK_TIME_OPTIMIZATION_BIT_EXT |
                            VK_PIPELINE_CREATE_2_RETAIN_LINK_TIME_OPTIMIZATION_INFO_BIT_EXT));
}

static void
radv_graphics_pipeline_import_lib(const struct radv_device *device, struct radv_graphics_pipeline *pipeline,
                                  struct radv_graphics_lib_pipeline *lib)
{
   bool import_binaries = false;

   /* There should be no common blocks between a lib we import and the current
    * pipeline we're building.
    */
   assert((pipeline->active_stages & lib->base.active_stages) == 0);

   pipeline->dynamic_states |= lib->base.dynamic_states;
   pipeline->active_stages |= lib->base.active_stages;

   /* Import binaries when LTO is disabled and when the library doesn't retain any shaders. */
   if (lib->base.has_pipeline_binaries || radv_should_import_lib_binaries(pipeline->base.create_flags)) {
      import_binaries = true;
   }

   if (import_binaries) {
      /* Import the compiled shaders. */
      for (uint32_t s = 0; s < ARRAY_SIZE(lib->base.base.shaders); s++) {
         if (!lib->base.base.shaders[s])
            continue;

         pipeline->base.shaders[s] = radv_shader_ref(lib->base.base.shaders[s]);
      }

      /* Import the GS copy shader if present. */
      if (lib->base.base.gs_copy_shader) {
         assert(!pipeline->base.gs_copy_shader);
         pipeline->base.gs_copy_shader = radv_shader_ref(lib->base.base.gs_copy_shader);
      }
   }
}

static void
radv_pipeline_init_input_assembly_state(const struct radv_device *device, struct radv_graphics_pipeline *pipeline)
{
   pipeline->ia_multi_vgt_param = radv_compute_ia_multi_vgt_param(device, pipeline->base.shaders);
}

static bool
radv_pipeline_uses_ds_feedback_loop(const struct radv_graphics_pipeline *pipeline,
                                    const struct vk_graphics_pipeline_state *state)
{
   VkPipelineCreateFlags2 create_flags = pipeline->base.create_flags;
   if (state->rp)
      create_flags |= state->pipeline_flags;

   return (create_flags & VK_PIPELINE_CREATE_2_DEPTH_STENCIL_ATTACHMENT_FEEDBACK_LOOP_BIT_EXT) != 0;
}

void
radv_get_viewport_xform(const VkViewport *viewport, float scale[3], float translate[3])
{
   float x = viewport->x;
   float y = viewport->y;
   float half_width = 0.5f * viewport->width;
   float half_height = 0.5f * viewport->height;
   double n = viewport->minDepth;
   double f = viewport->maxDepth;

   scale[0] = half_width;
   translate[0] = half_width + x;
   scale[1] = half_height;
   translate[1] = half_height + y;

   scale[2] = (f - n);
   translate[2] = n;
}

static void
radv_pipeline_init_dynamic_state(const struct radv_device *device, struct radv_graphics_pipeline *pipeline,
                                 const struct vk_graphics_pipeline_state *state,
                                 const VkGraphicsPipelineCreateInfo *pCreateInfo)
{
   uint64_t needed_states = radv_pipeline_needed_dynamic_state(device, pipeline, state);
   struct radv_dynamic_state *dynamic = &pipeline->dynamic_state;
   uint64_t states = needed_states;

   /* Initialize non-zero values for default dynamic state. */
   dynamic->vk.rs.line.width = 1.0f;
   dynamic->vk.fsr.fragment_size.width = 1u;
   dynamic->vk.fsr.fragment_size.height = 1u;
   dynamic->vk.ds.depth.bounds_test.max = 1.0f;
   dynamic->vk.ds.stencil.front.compare_mask = ~0;
   dynamic->vk.ds.stencil.front.write_mask = ~0;
   dynamic->vk.ds.stencil.back.compare_mask = ~0;
   dynamic->vk.ds.stencil.back.write_mask = ~0;
   dynamic->vk.ms.rasterization_samples = VK_SAMPLE_COUNT_1_BIT;

   pipeline->needed_dynamic_state = needed_states;

   states &= ~pipeline->dynamic_states;

   /* Input assembly. */
   if (states & RADV_DYNAMIC_PRIMITIVE_TOPOLOGY) {
      dynamic->vk.ia.primitive_topology = radv_translate_prim(state->ia->primitive_topology);
   }

   if (states & RADV_DYNAMIC_PRIMITIVE_RESTART_ENABLE) {
      dynamic->vk.ia.primitive_restart_enable = state->ia->primitive_restart_enable;
   }

   /* Tessellation. */
   if (states & RADV_DYNAMIC_PATCH_CONTROL_POINTS) {
      dynamic->vk.ts.patch_control_points = state->ts->patch_control_points;
   }

   if (states & RADV_DYNAMIC_TESS_DOMAIN_ORIGIN) {
      dynamic->vk.ts.domain_origin = state->ts->domain_origin;
   }

   /* Viewport. */
   if (needed_states & RADV_DYNAMIC_VIEWPORT) {
      dynamic->vk.vp.viewport_count = state->vp->viewport_count;
      if (states & RADV_DYNAMIC_VIEWPORT) {
         typed_memcpy(dynamic->vk.vp.viewports, state->vp->viewports, state->vp->viewport_count);
         for (unsigned i = 0; i < dynamic->vk.vp.viewport_count; i++)
            radv_get_viewport_xform(&dynamic->vk.vp.viewports[i], dynamic->hw_vp.xform[i].scale,
                                    dynamic->hw_vp.xform[i].translate);
      }
   }

   if (needed_states & RADV_DYNAMIC_SCISSOR) {
      dynamic->vk.vp.scissor_count = state->vp->scissor_count;
      if (states & RADV_DYNAMIC_SCISSOR) {
         typed_memcpy(dynamic->vk.vp.scissors, state->vp->scissors, state->vp->scissor_count);
      }
   }

   if (states & RADV_DYNAMIC_DEPTH_CLIP_NEGATIVE_ONE_TO_ONE) {
      dynamic->vk.vp.depth_clip_negative_one_to_one = state->vp->depth_clip_negative_one_to_one;
   }

   if (states & RADV_DYNAMIC_DEPTH_CLAMP_RANGE) {
      dynamic->vk.vp.depth_clamp_mode = state->vp->depth_clamp_mode;
      dynamic->vk.vp.depth_clamp_range = state->vp->depth_clamp_range;
   }

   /* Discard rectangles. */
   if (needed_states & RADV_DYNAMIC_DISCARD_RECTANGLE) {
      dynamic->vk.dr.rectangle_count = state->dr->rectangle_count;
      if (states & RADV_DYNAMIC_DISCARD_RECTANGLE) {
         typed_memcpy(dynamic->vk.dr.rectangles, state->dr->rectangles, state->dr->rectangle_count);
      }
   }

   /* Rasterization. */
   if (states & RADV_DYNAMIC_LINE_WIDTH) {
      dynamic->vk.rs.line.width = state->rs->line.width;
   }

   if (states & RADV_DYNAMIC_DEPTH_BIAS) {
      dynamic->vk.rs.depth_bias.constant_factor = state->rs->depth_bias.constant_factor;
      dynamic->vk.rs.depth_bias.clamp = state->rs->depth_bias.clamp;
      dynamic->vk.rs.depth_bias.slope_factor = state->rs->depth_bias.slope_factor;
      dynamic->vk.rs.depth_bias.representation = state->rs->depth_bias.representation;
   }

   if (states & RADV_DYNAMIC_CULL_MODE) {
      dynamic->vk.rs.cull_mode = state->rs->cull_mode;
   }

   if (states & RADV_DYNAMIC_FRONT_FACE) {
      dynamic->vk.rs.front_face = state->rs->front_face;
   }

   if (states & RADV_DYNAMIC_LINE_STIPPLE) {
      dynamic->vk.rs.line.stipple.factor = state->rs->line.stipple.factor;
      dynamic->vk.rs.line.stipple.pattern = state->rs->line.stipple.pattern;
   }

   if (states & RADV_DYNAMIC_DEPTH_BIAS_ENABLE) {
      dynamic->vk.rs.depth_bias.enable = state->rs->depth_bias.enable;
   }

   if (states & RADV_DYNAMIC_RASTERIZER_DISCARD_ENABLE) {
      dynamic->vk.rs.rasterizer_discard_enable = state->rs->rasterizer_discard_enable;
   }

   if (states & RADV_DYNAMIC_POLYGON_MODE) {
      dynamic->vk.rs.polygon_mode = radv_translate_fill(state->rs->polygon_mode);
   }

   if (states & RADV_DYNAMIC_LINE_STIPPLE_ENABLE) {
      dynamic->vk.rs.line.stipple.enable = state->rs->line.stipple.enable;
   }

   if (states & RADV_DYNAMIC_DEPTH_CLIP_ENABLE) {
      dynamic->vk.rs.depth_clip_enable = state->rs->depth_clip_enable;
   }

   if (states & RADV_DYNAMIC_CONSERVATIVE_RAST_MODE) {
      dynamic->vk.rs.conservative_mode = state->rs->conservative_mode;
   }

   if (states & RADV_DYNAMIC_PROVOKING_VERTEX_MODE) {
      dynamic->vk.rs.provoking_vertex = state->rs->provoking_vertex;
   }

   if (states & RADV_DYNAMIC_DEPTH_CLAMP_ENABLE) {
      dynamic->vk.rs.depth_clamp_enable = state->rs->depth_clamp_enable;
   }

   if (states & RADV_DYNAMIC_LINE_RASTERIZATION_MODE) {
      dynamic->vk.rs.line.mode = state->rs->line.mode;
   }

   /* Fragment shading rate. */
   if (states & RADV_DYNAMIC_FRAGMENT_SHADING_RATE) {
      dynamic->vk.fsr = *state->fsr;
   }

   /* Multisample. */
   if (states & RADV_DYNAMIC_ALPHA_TO_COVERAGE_ENABLE) {
      dynamic->vk.ms.alpha_to_coverage_enable = state->ms->alpha_to_coverage_enable;
   }

   if (states & RADV_DYNAMIC_ALPHA_TO_ONE_ENABLE) {
      dynamic->vk.ms.alpha_to_one_enable = state->ms->alpha_to_one_enable;
   }

   if (states & RADV_DYNAMIC_SAMPLE_MASK) {
      dynamic->vk.ms.sample_mask = state->ms->sample_mask & 0xffff;
   }

   if (states & RADV_DYNAMIC_RASTERIZATION_SAMPLES) {
      dynamic->vk.ms.rasterization_samples = state->ms->rasterization_samples;
   }

   if (states & RADV_DYNAMIC_SAMPLE_LOCATIONS_ENABLE) {
      dynamic->vk.ms.sample_locations_enable = state->ms->sample_locations_enable;
   }

   if (states & RADV_DYNAMIC_SAMPLE_LOCATIONS) {
      unsigned count = state->ms->sample_locations->per_pixel * state->ms->sample_locations->grid_size.width *
                       state->ms->sample_locations->grid_size.height;

      dynamic->sample_location.per_pixel = state->ms->sample_locations->per_pixel;
      dynamic->sample_location.grid_size = state->ms->sample_locations->grid_size;
      dynamic->sample_location.count = count;
      typed_memcpy(&dynamic->sample_location.locations[0], state->ms->sample_locations->locations, count);
   }

   /* Depth stencil. */
   /* If there is no depthstencil attachment, then don't read
    * pDepthStencilState. The Vulkan spec states that pDepthStencilState may
    * be NULL in this case. Even if pDepthStencilState is non-NULL, there is
    * no need to override the depthstencil defaults in
    * radv_pipeline::dynamic_state when there is no depthstencil attachment.
    *
    * Section 9.2 of the Vulkan 1.0.15 spec says:
    *
    *    pDepthStencilState is [...] NULL if the pipeline has rasterization
    *    disabled or if the subpass of the render pass the pipeline is created
    *    against does not use a depth/stencil attachment.
    */
   if (needed_states && radv_pipeline_has_ds_attachments(state->rp)) {
      if (states & RADV_DYNAMIC_DEPTH_BOUNDS) {
         dynamic->vk.ds.depth.bounds_test.min = state->ds->depth.bounds_test.min;
         dynamic->vk.ds.depth.bounds_test.max = state->ds->depth.bounds_test.max;
      }

      if (states & RADV_DYNAMIC_STENCIL_COMPARE_MASK) {
         dynamic->vk.ds.stencil.front.compare_mask = state->ds->stencil.front.compare_mask;
         dynamic->vk.ds.stencil.back.compare_mask = state->ds->stencil.back.compare_mask;
      }

      if (states & RADV_DYNAMIC_STENCIL_WRITE_MASK) {
         dynamic->vk.ds.stencil.front.write_mask = state->ds->stencil.front.write_mask;
         dynamic->vk.ds.stencil.back.write_mask = state->ds->stencil.back.write_mask;
      }

      if (states & RADV_DYNAMIC_STENCIL_REFERENCE) {
         dynamic->vk.ds.stencil.front.reference = state->ds->stencil.front.reference;
         dynamic->vk.ds.stencil.back.reference = state->ds->stencil.back.reference;
      }

      if (states & RADV_DYNAMIC_DEPTH_TEST_ENABLE) {
         dynamic->vk.ds.depth.test_enable = state->ds->depth.test_enable;
      }

      if (states & RADV_DYNAMIC_DEPTH_WRITE_ENABLE) {
         dynamic->vk.ds.depth.write_enable = state->ds->depth.write_enable;
      }

      if (states & RADV_DYNAMIC_DEPTH_COMPARE_OP) {
         dynamic->vk.ds.depth.compare_op = state->ds->depth.compare_op;
      }

      if (states & RADV_DYNAMIC_DEPTH_BOUNDS_TEST_ENABLE) {
         dynamic->vk.ds.depth.bounds_test.enable = state->ds->depth.bounds_test.enable;
      }

      if (states & RADV_DYNAMIC_STENCIL_TEST_ENABLE) {
         dynamic->vk.ds.stencil.test_enable = state->ds->stencil.test_enable;
      }

      if (states & RADV_DYNAMIC_STENCIL_OP) {
         dynamic->vk.ds.stencil.front.op.compare = state->ds->stencil.front.op.compare;
         dynamic->vk.ds.stencil.front.op.fail = state->ds->stencil.front.op.fail;
         dynamic->vk.ds.stencil.front.op.pass = state->ds->stencil.front.op.pass;
         dynamic->vk.ds.stencil.front.op.depth_fail = state->ds->stencil.front.op.depth_fail;

         dynamic->vk.ds.stencil.back.op.compare = state->ds->stencil.back.op.compare;
         dynamic->vk.ds.stencil.back.op.fail = state->ds->stencil.back.op.fail;
         dynamic->vk.ds.stencil.back.op.pass = state->ds->stencil.back.op.pass;
         dynamic->vk.ds.stencil.back.op.depth_fail = state->ds->stencil.back.op.depth_fail;
      }
   }

   /* Color blend. */
   /* Section 9.2 of the Vulkan 1.0.15 spec says:
    *
    *    pColorBlendState is [...] NULL if the pipeline has rasterization
    *    disabled or if the subpass of the render pass the pipeline is
    *    created against does not use any color attachments.
    */
   if (states & RADV_DYNAMIC_BLEND_CONSTANTS) {
      typed_memcpy(dynamic->vk.cb.blend_constants, state->cb->blend_constants, 4);
   }

   if (radv_pipeline_has_color_attachments(state->rp)) {
      if (states & RADV_DYNAMIC_LOGIC_OP) {
         if ((pipeline->dynamic_states & RADV_DYNAMIC_LOGIC_OP_ENABLE) || state->cb->logic_op_enable) {
            dynamic->vk.cb.logic_op = radv_translate_blend_logic_op(state->cb->logic_op);
         }
      }

      if (states & RADV_DYNAMIC_COLOR_WRITE_ENABLE) {
         dynamic->vk.cb.color_write_enables = state->cb->color_write_enables;
      }

      if (states & RADV_DYNAMIC_LOGIC_OP_ENABLE) {
         dynamic->vk.cb.logic_op_enable = state->cb->logic_op_enable;
      }

      if (states & RADV_DYNAMIC_COLOR_WRITE_MASK) {
         for (unsigned i = 0; i < state->cb->attachment_count; i++) {
            dynamic->vk.cb.attachments[i].write_mask = state->cb->attachments[i].write_mask;
         }
      }

      if (states & RADV_DYNAMIC_COLOR_BLEND_ENABLE) {
         for (unsigned i = 0; i < state->cb->attachment_count; i++) {
            dynamic->vk.cb.attachments[i].blend_enable = state->cb->attachments[i].blend_enable;
         }
      }

      if (states & RADV_DYNAMIC_COLOR_BLEND_EQUATION) {
         for (unsigned i = 0; i < state->cb->attachment_count; i++) {
            const struct vk_color_blend_attachment_state *att = &state->cb->attachments[i];

            dynamic->vk.cb.attachments[i].src_color_blend_factor = att->src_color_blend_factor;
            dynamic->vk.cb.attachments[i].dst_color_blend_factor = att->dst_color_blend_factor;
            dynamic->vk.cb.attachments[i].color_blend_op = att->color_blend_op;
            dynamic->vk.cb.attachments[i].src_alpha_blend_factor = att->src_alpha_blend_factor;
            dynamic->vk.cb.attachments[i].dst_alpha_blend_factor = att->dst_alpha_blend_factor;
            dynamic->vk.cb.attachments[i].alpha_blend_op = att->alpha_blend_op;
         }
      }
   }

   if (states & RADV_DYNAMIC_DISCARD_RECTANGLE_ENABLE) {
      dynamic->vk.dr.enable = state->dr->rectangle_count > 0;
   }

   if (states & RADV_DYNAMIC_DISCARD_RECTANGLE_MODE) {
      dynamic->vk.dr.mode = state->dr->mode;
   }

   if (states & RADV_DYNAMIC_ATTACHMENT_FEEDBACK_LOOP_ENABLE) {
      bool uses_ds_feedback_loop = radv_pipeline_uses_ds_feedback_loop(pipeline, state);

      dynamic->feedback_loop_aspects =
         uses_ds_feedback_loop ? (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT) : VK_IMAGE_ASPECT_NONE;
   }

   for (uint32_t i = 0; i < MAX_RTS; i++) {
      dynamic->vk.cal.color_map[i] = state->cal ? state->cal->color_map[i] : i;
      dynamic->vk.ial.color_map[i] = state->ial ? state->ial->color_map[i] : i;
   }

   dynamic->vk.ial.depth_att = state->ial ? state->ial->depth_att : MESA_VK_ATTACHMENT_UNUSED;
   dynamic->vk.ial.stencil_att = state->ial ? state->ial->stencil_att : MESA_VK_ATTACHMENT_UNUSED;

   pipeline->dynamic_state.mask = states;
}

struct radv_shader *
radv_get_shader(struct radv_shader *const *shaders, gl_shader_stage stage)
{
   if (stage == MESA_SHADER_VERTEX) {
      if (shaders[MESA_SHADER_VERTEX])
         return shaders[MESA_SHADER_VERTEX];
      if (shaders[MESA_SHADER_TESS_CTRL])
         return shaders[MESA_SHADER_TESS_CTRL];
      if (shaders[MESA_SHADER_GEOMETRY])
         return shaders[MESA_SHADER_GEOMETRY];
   } else if (stage == MESA_SHADER_TESS_EVAL) {
      if (!shaders[MESA_SHADER_TESS_CTRL])
         return NULL;
      if (shaders[MESA_SHADER_TESS_EVAL])
         return shaders[MESA_SHADER_TESS_EVAL];
      if (shaders[MESA_SHADER_GEOMETRY])
         return shaders[MESA_SHADER_GEOMETRY];
   }
   return shaders[stage];
}

static bool
radv_should_export_multiview(const struct radv_shader_stage *stage, const struct radv_graphics_state_key *gfx_state)
{
   /* Export the layer in the last VGT stage if multiview is used.
    * Also checks for NONE stage, which happens when we have depth-only rendering.
    * When the next stage is unknown (with GPL or ESO), the layer is exported unconditionally.
    */
   return gfx_state->has_multiview_view_index && radv_is_last_vgt_stage(stage) &&
          !(stage->nir->info.outputs_written & VARYING_BIT_LAYER);
}

static void
radv_remove_point_size(const struct radv_graphics_state_key *gfx_state, nir_shader *producer, nir_shader *consumer)
{
   if ((consumer->info.inputs_read & VARYING_BIT_PSIZ) || !(producer->info.outputs_written & VARYING_BIT_PSIZ))
      return;

   /* Do not remove PSIZ if the shader uses XFB because it might be stored. */
   if (producer->xfb_info)
      return;

   /* Do not remove PSIZ if the rasterization primitive uses points. */
   if (consumer->info.stage == MESA_SHADER_FRAGMENT &&
       ((producer->info.stage == MESA_SHADER_TESS_EVAL && producer->info.tess.point_mode) ||
        (producer->info.stage == MESA_SHADER_GEOMETRY && producer->info.gs.output_primitive == MESA_PRIM_POINTS) ||
        (producer->info.stage == MESA_SHADER_MESH && producer->info.mesh.primitive_type == MESA_PRIM_POINTS)))
      return;

   nir_variable *var = nir_find_variable_with_location(producer, nir_var_shader_out, VARYING_SLOT_PSIZ);
   assert(var);

   /* Change PSIZ to a global variable which allows it to be DCE'd. */
   var->data.location = 0;
   var->data.mode = nir_var_shader_temp;

   producer->info.outputs_written &= ~VARYING_BIT_PSIZ;
   NIR_PASS_V(producer, nir_fixup_deref_modes);
   NIR_PASS(_, producer, nir_remove_dead_variables, nir_var_shader_temp, NULL);
   NIR_PASS(_, producer, nir_opt_dce);
}

static void
radv_remove_color_exports(const struct radv_graphics_state_key *gfx_state, nir_shader *nir)
{
   uint8_t color_remap[MAX_RTS];
   bool fixup_derefs = false;

   /* Do not remove color exports when a PS epilog is used because the format isn't known and the color write mask can
    * be dynamic. */
   if (gfx_state->ps.has_epilog)
      return;

   /* Shader output locations to color attachment mappings. */
   memset(color_remap, MESA_VK_ATTACHMENT_UNUSED, sizeof(color_remap));
   for (uint32_t i = 0; i < MAX_RTS; i++) {
      if (gfx_state->ps.epilog.color_map[i] != MESA_VK_ATTACHMENT_UNUSED)
         color_remap[gfx_state->ps.epilog.color_map[i]] = i;
   }

   nir_foreach_shader_out_variable (var, nir) {
      int idx = var->data.location;
      idx -= FRAG_RESULT_DATA0;

      if (idx < 0)
         continue;

      const uint8_t cb_idx = color_remap[idx];
      unsigned col_format = (gfx_state->ps.epilog.spi_shader_col_format >> (4 * cb_idx)) & 0xf;

      if (col_format == V_028714_SPI_SHADER_ZERO) {
         /* Remove the color export if it's unused or in presence of holes. */
         nir->info.outputs_written &= ~BITFIELD64_BIT(var->data.location);
         var->data.location = 0;
         var->data.mode = nir_var_shader_temp;
         fixup_derefs = true;
      }
   }

   if (fixup_derefs) {
      NIR_PASS_V(nir, nir_fixup_deref_modes);
      NIR_PASS(_, nir, nir_remove_dead_variables, nir_var_shader_temp, NULL);
      NIR_PASS(_, nir, nir_opt_dce);
   }
}

static void
merge_tess_info(struct shader_info *tes_info, struct shader_info *tcs_info)
{
   /* The Vulkan 1.0.38 spec, section 21.1 Tessellator says:
    *
    *    "PointMode. Controls generation of points rather than triangles
    *     or lines. This functionality defaults to disabled, and is
    *     enabled if either shader stage includes the execution mode.
    *
    * and about Triangles, Quads, IsoLines, VertexOrderCw, VertexOrderCcw,
    * PointMode, SpacingEqual, SpacingFractionalEven, SpacingFractionalOdd,
    * and OutputVertices, it says:
    *
    *    "One mode must be set in at least one of the tessellation
    *     shader stages."
    *
    * So, the fields can be set in either the TCS or TES, but they must
    * agree if set in both.  Our backend looks at TES, so bitwise-or in
    * the values from the TCS.
    */
   assert(tcs_info->tess.tcs_vertices_out == 0 || tes_info->tess.tcs_vertices_out == 0 ||
          tcs_info->tess.tcs_vertices_out == tes_info->tess.tcs_vertices_out);
   tes_info->tess.tcs_vertices_out |= tcs_info->tess.tcs_vertices_out;

   assert(tcs_info->tess.spacing == TESS_SPACING_UNSPECIFIED || tes_info->tess.spacing == TESS_SPACING_UNSPECIFIED ||
          tcs_info->tess.spacing == tes_info->tess.spacing);
   tes_info->tess.spacing |= tcs_info->tess.spacing;

   assert(tcs_info->tess._primitive_mode == TESS_PRIMITIVE_UNSPECIFIED ||
          tes_info->tess._primitive_mode == TESS_PRIMITIVE_UNSPECIFIED ||
          tcs_info->tess._primitive_mode == tes_info->tess._primitive_mode);
   tes_info->tess._primitive_mode |= tcs_info->tess._primitive_mode;
   tes_info->tess.ccw |= tcs_info->tess.ccw;
   tes_info->tess.point_mode |= tcs_info->tess.point_mode;

   /* Copy the merged info back to the TCS */
   tcs_info->tess.tcs_vertices_out = tes_info->tess.tcs_vertices_out;
   tcs_info->tess._primitive_mode = tes_info->tess._primitive_mode;
}

static void
radv_link_shaders(const struct radv_device *device, struct radv_shader_stage *producer_stage,
                  struct radv_shader_stage *consumer_stage, const struct radv_graphics_state_key *gfx_state)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   const enum amd_gfx_level gfx_level = pdev->info.gfx_level;
   nir_shader *producer = producer_stage->nir;
   nir_shader *consumer = consumer_stage->nir;

   if (consumer->info.stage == MESA_SHADER_FRAGMENT) {
      /* Lower the viewport index to zero when the last vertex stage doesn't export it. */
      if ((consumer->info.inputs_read & VARYING_BIT_VIEWPORT) &&
          !(producer->info.outputs_written & VARYING_BIT_VIEWPORT)) {
         NIR_PASS(_, consumer, radv_nir_lower_viewport_to_zero);
      }
   }

   if (producer_stage->key.optimisations_disabled || consumer_stage->key.optimisations_disabled)
      return;

   if (consumer->info.stage == MESA_SHADER_FRAGMENT && producer->info.has_transform_feedback_varyings) {
      nir_link_xfb_varyings(producer, consumer);
   }

   unsigned array_deref_of_vec_options =
      nir_lower_direct_array_deref_of_vec_load | nir_lower_indirect_array_deref_of_vec_load |
      nir_lower_direct_array_deref_of_vec_store | nir_lower_indirect_array_deref_of_vec_store;

   NIR_PASS(_, producer, nir_lower_array_deref_of_vec, nir_var_shader_out, NULL, array_deref_of_vec_options);
   NIR_PASS(_, consumer, nir_lower_array_deref_of_vec, nir_var_shader_in, NULL, array_deref_of_vec_options);

   nir_lower_io_arrays_to_elements(producer, consumer);
   nir_validate_shader(producer, "after nir_lower_io_arrays_to_elements");
   nir_validate_shader(consumer, "after nir_lower_io_arrays_to_elements");

   radv_nir_lower_io_to_scalar_early(producer, nir_var_shader_out);
   radv_nir_lower_io_to_scalar_early(consumer, nir_var_shader_in);

   /* Remove PSIZ from shaders when it's not needed.
    * This is typically produced by translation layers like Zink or D9VK.
    */
   if (gfx_state->enable_remove_point_size)
      radv_remove_point_size(gfx_state, producer, consumer);

   if (nir_link_opt_varyings(producer, consumer)) {
      nir_validate_shader(producer, "after nir_link_opt_varyings");
      nir_validate_shader(consumer, "after nir_link_opt_varyings");

      NIR_PASS(_, consumer, nir_opt_constant_folding);
      NIR_PASS(_, consumer, nir_opt_algebraic);
      NIR_PASS(_, consumer, nir_opt_dce);
   }

   NIR_PASS(_, producer, nir_remove_dead_variables, nir_var_shader_out, NULL);
   NIR_PASS(_, consumer, nir_remove_dead_variables, nir_var_shader_in, NULL);

   nir_remove_unused_varyings(producer, consumer);

   nir_compact_varyings(producer, consumer, true);

   nir_validate_shader(producer, "after nir_compact_varyings");
   nir_validate_shader(consumer, "after nir_compact_varyings");

   if (producer->info.stage == MESA_SHADER_MESH) {
      /* nir_compact_varyings can change the location of per-vertex and per-primitive outputs */
      nir_shader_gather_info(producer, nir_shader_get_entrypoint(producer));
   }

   const bool has_geom_or_tess =
      consumer->info.stage == MESA_SHADER_GEOMETRY || consumer->info.stage == MESA_SHADER_TESS_CTRL;
   const bool merged_gs = consumer->info.stage == MESA_SHADER_GEOMETRY && gfx_level >= GFX9;

   if (producer->info.stage == MESA_SHADER_TESS_CTRL || producer->info.stage == MESA_SHADER_MESH ||
       (producer->info.stage == MESA_SHADER_VERTEX && has_geom_or_tess) ||
       (producer->info.stage == MESA_SHADER_TESS_EVAL && merged_gs)) {
      NIR_PASS(_, producer, nir_lower_io_to_vector, nir_var_shader_out);

      if (producer->info.stage == MESA_SHADER_TESS_CTRL)
         NIR_PASS(_, producer, nir_vectorize_tess_levels);

      NIR_PASS(_, producer, nir_opt_combine_stores, nir_var_shader_out);
   }

   if (consumer->info.stage == MESA_SHADER_GEOMETRY || consumer->info.stage == MESA_SHADER_TESS_CTRL ||
       consumer->info.stage == MESA_SHADER_TESS_EVAL) {
      NIR_PASS(_, consumer, nir_lower_io_to_vector, nir_var_shader_in);
   }
}

static const gl_shader_stage graphics_shader_order[] = {
   MESA_SHADER_VERTEX,   MESA_SHADER_TESS_CTRL, MESA_SHADER_TESS_EVAL, MESA_SHADER_GEOMETRY,

   MESA_SHADER_TASK,     MESA_SHADER_MESH,

   MESA_SHADER_FRAGMENT,
};

static void
radv_link_vs(const struct radv_device *device, struct radv_shader_stage *vs_stage, struct radv_shader_stage *next_stage,
             const struct radv_graphics_state_key *gfx_state)
{
   assert(vs_stage->nir->info.stage == MESA_SHADER_VERTEX);

   if (radv_should_export_multiview(vs_stage, gfx_state)) {
      NIR_PASS(_, vs_stage->nir, radv_nir_export_multiview);
   }

   if (next_stage) {
      assert(next_stage->nir->info.stage == MESA_SHADER_TESS_CTRL ||
             next_stage->nir->info.stage == MESA_SHADER_GEOMETRY ||
             next_stage->nir->info.stage == MESA_SHADER_FRAGMENT);

      radv_link_shaders(device, vs_stage, next_stage, gfx_state);
   }
}

static void
radv_link_tcs(const struct radv_device *device, struct radv_shader_stage *tcs_stage,
              struct radv_shader_stage *tes_stage, const struct radv_graphics_state_key *gfx_state)
{
   if (!tes_stage)
      return;

   assert(tcs_stage->nir->info.stage == MESA_SHADER_TESS_CTRL);
   assert(tes_stage->nir->info.stage == MESA_SHADER_TESS_EVAL);

   radv_link_shaders(device, tcs_stage, tes_stage, gfx_state);

   /* Copy TCS info into the TES info */
   merge_tess_info(&tes_stage->nir->info, &tcs_stage->nir->info);
}

static void
radv_link_tes(const struct radv_device *device, struct radv_shader_stage *tes_stage,
              struct radv_shader_stage *next_stage, const struct radv_graphics_state_key *gfx_state)
{
   assert(tes_stage->nir->info.stage == MESA_SHADER_TESS_EVAL);

   if (radv_should_export_multiview(tes_stage, gfx_state)) {
      NIR_PASS(_, tes_stage->nir, radv_nir_export_multiview);
   }

   if (next_stage) {
      assert(next_stage->nir->info.stage == MESA_SHADER_GEOMETRY ||
             next_stage->nir->info.stage == MESA_SHADER_FRAGMENT);

      radv_link_shaders(device, tes_stage, next_stage, gfx_state);
   }
}

static void
radv_link_gs(const struct radv_device *device, struct radv_shader_stage *gs_stage, struct radv_shader_stage *fs_stage,
             const struct radv_graphics_state_key *gfx_state)
{
   assert(gs_stage->nir->info.stage == MESA_SHADER_GEOMETRY);

   if (radv_should_export_multiview(gs_stage, gfx_state)) {
      NIR_PASS(_, gs_stage->nir, radv_nir_export_multiview);
   }

   if (fs_stage) {
      assert(fs_stage->nir->info.stage == MESA_SHADER_FRAGMENT);

      radv_link_shaders(device, gs_stage, fs_stage, gfx_state);
   }
}

static void
radv_link_task(const struct radv_device *device, struct radv_shader_stage *task_stage,
               struct radv_shader_stage *mesh_stage, const struct radv_graphics_state_key *gfx_state)
{
   assert(task_stage->nir->info.stage == MESA_SHADER_TASK);

   if (mesh_stage) {
      assert(mesh_stage->nir->info.stage == MESA_SHADER_MESH);

      /* Linking task and mesh shaders shouldn't do anything for now but keep it for consistency. */
      radv_link_shaders(device, task_stage, mesh_stage, gfx_state);
   }
}

static void
radv_link_mesh(const struct radv_device *device, struct radv_shader_stage *mesh_stage,
               struct radv_shader_stage *fs_stage, const struct radv_graphics_state_key *gfx_state)
{
   assert(mesh_stage->nir->info.stage == MESA_SHADER_MESH);

   if (fs_stage) {
      assert(fs_stage->nir->info.stage == MESA_SHADER_FRAGMENT);

      nir_foreach_shader_in_variable (var, fs_stage->nir) {
         /* These variables are per-primitive when used with a mesh shader. */
         if (var->data.location == VARYING_SLOT_PRIMITIVE_ID || var->data.location == VARYING_SLOT_VIEWPORT ||
             var->data.location == VARYING_SLOT_LAYER) {
            var->data.per_primitive = true;
         }
      }

      radv_link_shaders(device, mesh_stage, fs_stage, gfx_state);
   }

   /* Lower mesh shader draw ID to zero prevent app bugs from triggering undefined behaviour. */
   if (mesh_stage->info.ms.has_task && BITSET_TEST(mesh_stage->nir->info.system_values_read, SYSTEM_VALUE_DRAW_ID))
      radv_nir_lower_draw_id_to_zero(mesh_stage->nir);
}

static void
radv_link_fs(struct radv_shader_stage *fs_stage, const struct radv_graphics_state_key *gfx_state)
{
   assert(fs_stage->nir->info.stage == MESA_SHADER_FRAGMENT);

   /* Lower the view index to map on the layer. */
   NIR_PASS(_, fs_stage->nir, radv_nir_lower_view_index);

   radv_remove_color_exports(gfx_state, fs_stage->nir);
}

static bool
radv_pipeline_needs_noop_fs(struct radv_graphics_pipeline *pipeline, const struct radv_graphics_state_key *gfx_state)
{
   if (pipeline->base.type == RADV_PIPELINE_GRAPHICS &&
       !(radv_pipeline_to_graphics(&pipeline->base)->active_stages & VK_SHADER_STAGE_FRAGMENT_BIT))
      return true;

   if (pipeline->base.type == RADV_PIPELINE_GRAPHICS_LIB &&
       (gfx_state->lib_flags & VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT) &&
       !(radv_pipeline_to_graphics_lib(&pipeline->base)->base.active_stages & VK_SHADER_STAGE_FRAGMENT_BIT))
      return true;

   return false;
}

static void
radv_remove_varyings(nir_shader *nir)
{
   /* We can't demote mesh outputs to nir_var_shader_temp yet, because
    * they don't support array derefs of vectors.
    */
   if (nir->info.stage == MESA_SHADER_MESH)
      return;

   bool fixup_derefs = false;

   nir_foreach_shader_out_variable (var, nir) {
      if (var->data.always_active_io)
         continue;

      if (var->data.location < VARYING_SLOT_VAR0)
         continue;

      nir->info.outputs_written &= ~BITFIELD64_BIT(var->data.location);
      var->data.location = 0;
      var->data.mode = nir_var_shader_temp;
      fixup_derefs = true;
   }

   if (fixup_derefs) {
      NIR_PASS_V(nir, nir_fixup_deref_modes);
      NIR_PASS(_, nir, nir_remove_dead_variables, nir_var_shader_temp, NULL);
      NIR_PASS(_, nir, nir_opt_dce);
   }
}

static void
radv_graphics_shaders_link(const struct radv_device *device, const struct radv_graphics_state_key *gfx_state,
                           struct radv_shader_stage *stages)
{
   /* Walk backwards to link */
   struct radv_shader_stage *next_stage = NULL;
   for (int i = ARRAY_SIZE(graphics_shader_order) - 1; i >= 0; i--) {
      gl_shader_stage s = graphics_shader_order[i];
      if (!stages[s].nir)
         continue;

      switch (s) {
      case MESA_SHADER_VERTEX:
         radv_link_vs(device, &stages[s], next_stage, gfx_state);
         break;
      case MESA_SHADER_TESS_CTRL:
         radv_link_tcs(device, &stages[s], next_stage, gfx_state);
         break;
      case MESA_SHADER_TESS_EVAL:
         radv_link_tes(device, &stages[s], next_stage, gfx_state);
         break;
      case MESA_SHADER_GEOMETRY:
         radv_link_gs(device, &stages[s], next_stage, gfx_state);
         break;
      case MESA_SHADER_TASK:
         radv_link_task(device, &stages[s], next_stage, gfx_state);
         break;
      case MESA_SHADER_MESH:
         radv_link_mesh(device, &stages[s], next_stage, gfx_state);
         break;
      case MESA_SHADER_FRAGMENT:
         radv_link_fs(&stages[s], gfx_state);
         break;
      default:
         unreachable("Invalid graphics shader stage");
      }

      next_stage = &stages[s];
   }
}

/**
 * Fist pass of varying optimization.
 * This function is called for each shader pair from first to last.
 *
 * 1. Run some NIR passes in preparation.
 * 2. Optimize varyings.
 * 3. If either shader changed, run algebraic optimizations.
 */
static void
radv_graphics_shaders_link_varyings_first(struct radv_shader_stage *producer_stage,
                                          struct radv_shader_stage *consumer_stage)
{
   nir_shader *producer = producer_stage->nir;
   nir_shader *consumer = consumer_stage->nir;

   /* It is expected by nir_opt_varyings that no undefined stores are present in the shader. */
   NIR_PASS(_, producer, nir_opt_undef);

   /* Update load/store alignments because inter-stage code motion may move instructions used to deduce this info. */
   NIR_PASS(_, consumer, nir_opt_load_store_update_alignments);

   /* Scalarize all I/O, because nir_opt_varyings and nir_opt_vectorize_io expect all I/O to be scalarized. */
   NIR_PASS(_, producer, nir_lower_io_to_scalar, nir_var_shader_out, NULL, NULL);
   NIR_PASS(_, consumer, nir_lower_io_to_scalar, nir_var_shader_in, NULL, NULL);

   /* Eliminate useless vec->mov copies resulting from scalarization. */
   NIR_PASS(_, producer, nir_copy_prop);

   const nir_opt_varyings_progress p = nir_opt_varyings(producer, consumer, true, 0, 0);

   /* Run algebraic optimizations on shaders that changed. */
   if (p & nir_progress_producer) {
      radv_optimize_nir_algebraic(producer, false, false);
   }
   if (p & nir_progress_consumer) {
      radv_optimize_nir_algebraic(consumer, false, false);
   }
}

/**
 * Second pass of varying optimization.
 * This function is called for each shader pair from last to fist,
 * after the first pass had already been called for each pair.
 * Done because the previous pass might have enabled additional
 * opportunities for optimization.
 *
 * 1. Optimize varyings again.
 * 2. If either shader changed, run algebraic optimizations.
 * 3. Run some NIR passes to clean up the shaders.
 */
static void
radv_graphics_shaders_link_varyings_second(struct radv_shader_stage *producer_stage,
                                           struct radv_shader_stage *consumer_stage)
{
   nir_shader *producer = producer_stage->nir;
   nir_shader *consumer = consumer_stage->nir;

   const nir_opt_varyings_progress p = nir_opt_varyings(producer, consumer, true, 0, 0);

   /* Run algebraic optimizations on shaders that changed. */
   if (p & nir_progress_producer) {
      radv_optimize_nir_algebraic(producer, true, false);
   }
   if (p & nir_progress_consumer) {
      radv_optimize_nir_algebraic(consumer, true, false);
   }

   /* Re-vectorize I/O for stages that output to memory (LDS or VRAM).
    * Don't vectorize FS inputs, doing so just regresses shader stats without any benefit.
    * There is also no benefit from re-vectorizing the outputs of the last pre-rasterization
    * stage here, because ac_nir_lower_ngg/legacy already takes care of that.
    */
   if (consumer->info.stage != MESA_SHADER_FRAGMENT) {
      NIR_PASS(_, producer, nir_opt_vectorize_io, nir_var_shader_out);
      NIR_PASS(_, consumer, nir_opt_vectorize_io, nir_var_shader_in);
   }

   /* Gather shader info; at least the I/O info likely changed
    * and changes to only the I/O info are not reflected in nir_opt_varyings_progress.
    */
   nir_shader_gather_info(producer, nir_shader_get_entrypoint(producer));
   nir_shader_gather_info(consumer, nir_shader_get_entrypoint(consumer));

   /* Recompute intrinsic bases of PS inputs in order to remove gaps. */
   if (consumer->info.stage == MESA_SHADER_FRAGMENT)
      radv_recompute_fs_input_bases(consumer);

   /* Recreate XFB info from intrinsics (nir_opt_varyings may have changed it). */
   if (producer->xfb_info) {
      nir_gather_xfb_info_from_intrinsics(producer);
   }
}

static void
radv_graphics_shaders_fill_linked_vs_io_info(struct radv_shader_stage *vs_stage,
                                             struct radv_shader_stage *consumer_stage)
{
   const unsigned num_reserved_slots = util_bitcount64(consumer_stage->nir->info.inputs_read);
   vs_stage->info.vs.num_linked_outputs = num_reserved_slots;
   vs_stage->info.outputs_linked = true;

   switch (consumer_stage->stage) {
   case MESA_SHADER_TESS_CTRL: {
      consumer_stage->info.tcs.num_linked_inputs = num_reserved_slots;
      consumer_stage->info.inputs_linked = true;
      break;
   }
   case MESA_SHADER_GEOMETRY: {
      consumer_stage->info.gs.num_linked_inputs = num_reserved_slots;
      consumer_stage->info.inputs_linked = true;
      break;
   }
   default:
      unreachable("invalid next stage for VS");
   }
}

static void
radv_graphics_shaders_fill_linked_tcs_tes_io_info(struct radv_shader_stage *tcs_stage,
                                                  struct radv_shader_stage *tes_stage)
{
   assume(tes_stage->stage == MESA_SHADER_TESS_EVAL);

   /* Count the number of per-vertex output slots we need to reserve for the TCS and TES. */
   const uint64_t per_vertex_mask =
      tes_stage->nir->info.inputs_read & ~(VARYING_BIT_TESS_LEVEL_OUTER | VARYING_BIT_TESS_LEVEL_INNER);
   const unsigned num_reserved_slots = util_bitcount64(per_vertex_mask);

   /* Count the number of per-patch output slots we need to reserve for the TCS and TES.
    * This is necessary because we need it to determine the patch size in VRAM.
    */
   const uint64_t tess_lvl_mask =
      tes_stage->nir->info.inputs_read & (VARYING_BIT_TESS_LEVEL_OUTER | VARYING_BIT_TESS_LEVEL_INNER);
   const unsigned num_reserved_patch_slots =
      util_bitcount64(tess_lvl_mask) + util_bitcount64(tes_stage->nir->info.patch_inputs_read);

   tcs_stage->info.tcs.num_linked_outputs = num_reserved_slots;
   tcs_stage->info.tcs.num_linked_patch_outputs = num_reserved_patch_slots;
   tcs_stage->info.outputs_linked = true;

   tes_stage->info.tes.num_linked_inputs = num_reserved_slots;
   tes_stage->info.tes.num_linked_patch_inputs = num_reserved_patch_slots;
   tes_stage->info.inputs_linked = true;
}

static void
radv_graphics_shaders_fill_linked_tes_gs_io_info(struct radv_shader_stage *tes_stage,
                                                 struct radv_shader_stage *gs_stage)
{
   assume(gs_stage->stage == MESA_SHADER_GEOMETRY);

   const unsigned num_reserved_slots = util_bitcount64(gs_stage->nir->info.inputs_read);
   tes_stage->info.tes.num_linked_outputs = num_reserved_slots;
   tes_stage->info.outputs_linked = true;
   gs_stage->info.gs.num_linked_inputs = num_reserved_slots;
   gs_stage->info.inputs_linked = true;
}

static void
radv_graphics_shaders_fill_linked_io_info(struct radv_shader_stage *producer_stage,
                                          struct radv_shader_stage *consumer_stage)
{
   /* We don't need to fill this info for the last pre-rasterization stage. */
   if (consumer_stage->stage == MESA_SHADER_FRAGMENT)
      return;

   switch (producer_stage->stage) {
   case MESA_SHADER_VERTEX:
      radv_graphics_shaders_fill_linked_vs_io_info(producer_stage, consumer_stage);
      break;

   case MESA_SHADER_TESS_CTRL:
      radv_graphics_shaders_fill_linked_tcs_tes_io_info(producer_stage, consumer_stage);
      break;

   case MESA_SHADER_TESS_EVAL:
      radv_graphics_shaders_fill_linked_tes_gs_io_info(producer_stage, consumer_stage);
      break;

   default:
      break;
   }
}

/**
 * Varying optimizations performed on lowered shader I/O.
 *
 * We do this after lowering shader I/O because this is more effective
 * than running the same optimizations on I/O derefs.
 */
static void
radv_graphics_shaders_link_varyings(struct radv_shader_stage *stages)
{
   /* Optimize varyings from first to last stage. */
   gl_shader_stage prev = MESA_SHADER_NONE;
   for (int i = 0; i < ARRAY_SIZE(graphics_shader_order); ++i) {
      gl_shader_stage s = graphics_shader_order[i];
      if (!stages[s].nir)
         continue;

      if (prev != MESA_SHADER_NONE) {
         if (!stages[prev].key.optimisations_disabled && !stages[s].key.optimisations_disabled)
            radv_graphics_shaders_link_varyings_first(&stages[prev], &stages[s]);
      }

      prev = s;
   }

   /* Optimize varyings from last to first stage. */
   gl_shader_stage next = MESA_SHADER_NONE;
   for (int i = ARRAY_SIZE(graphics_shader_order) - 1; i >= 0; --i) {
      gl_shader_stage s = graphics_shader_order[i];
      if (!stages[s].nir)
         continue;

      if (next != MESA_SHADER_NONE) {
         if (!stages[s].key.optimisations_disabled && !stages[next].key.optimisations_disabled)
            radv_graphics_shaders_link_varyings_second(&stages[s], &stages[next]);

         radv_graphics_shaders_fill_linked_io_info(&stages[s], &stages[next]);
      }

      next = s;
   }
}

struct radv_ps_epilog_key
radv_generate_ps_epilog_key(const struct radv_device *device, const struct radv_ps_epilog_state *state)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   const struct radv_instance *instance = radv_physical_device_instance(pdev);
   unsigned col_format = 0, is_int8 = 0, is_int10 = 0, is_float32 = 0, z_format = 0;
   struct radv_ps_epilog_key key;

   memset(&key, 0, sizeof(key));
   memset(key.color_map, MESA_VK_ATTACHMENT_UNUSED, sizeof(key.color_map));

   for (unsigned i = 0; i < state->color_attachment_count; ++i) {
      unsigned cf;
      unsigned cb_idx = state->color_attachment_mappings[i];
      VkFormat fmt = state->color_attachment_formats[i];

      if (fmt == VK_FORMAT_UNDEFINED || !(state->color_write_mask & (0xfu << (i * 4))) ||
          cb_idx == MESA_VK_ATTACHMENT_UNUSED) {
         cf = V_028714_SPI_SHADER_ZERO;
      } else {
         bool blend_enable = state->color_blend_enable & (0xfu << (i * 4));

         cf = radv_choose_spi_color_format(device, fmt, blend_enable, state->need_src_alpha & (1 << i));

         if (format_is_int8(fmt))
            is_int8 |= 1 << i;
         if (format_is_int10(fmt))
            is_int10 |= 1 << i;
         if (format_is_float32(fmt))
            is_float32 |= 1 << i;
      }

      col_format |= cf << (4 * i);

      key.color_map[i] = state->color_attachment_mappings[i];
   }

   if (!(col_format & 0xf) && state->need_src_alpha & (1 << 0)) {
      /* When a subpass doesn't have any color attachments, write the alpha channel of MRT0 when
       * alpha coverage is enabled because the depth attachment needs it.
       */
      col_format |= V_028714_SPI_SHADER_32_AR;
      key.color_map[0] = 0;
   }

   /* The output for dual source blending should have the same format as the first output. */
   if (state->mrt0_is_dual_src) {
      assert(!(col_format >> 4));
      col_format |= (col_format & 0xf) << 4;
      key.color_map[1] = 1;
   }

   z_format = ac_get_spi_shader_z_format(state->export_depth, state->export_stencil, state->export_sample_mask,
                                         state->alpha_to_coverage_via_mrtz);

   key.spi_shader_col_format = col_format;
   key.color_is_int8 = pdev->info.gfx_level < GFX8 ? is_int8 : 0;
   key.color_is_int10 = pdev->info.gfx_level < GFX8 ? is_int10 : 0;
   key.enable_mrt_output_nan_fixup = instance->drirc.enable_mrt_output_nan_fixup ? is_float32 : 0;
   key.colors_written = state->colors_written;
   key.mrt0_is_dual_src = state->mrt0_is_dual_src;
   key.export_depth = state->export_depth;
   key.export_stencil = state->export_stencil;
   key.export_sample_mask = state->export_sample_mask;
   key.alpha_to_coverage_via_mrtz = state->alpha_to_coverage_via_mrtz;
   key.spi_shader_z_format = z_format;
   key.alpha_to_one = state->alpha_to_one;

   return key;
}

static struct radv_ps_epilog_key
radv_pipeline_generate_ps_epilog_key(const struct radv_device *device, const struct vk_graphics_pipeline_state *state)
{
   struct radv_ps_epilog_state ps_epilog = {0};

   if (state->ms && state->ms->alpha_to_coverage_enable)
      ps_epilog.need_src_alpha |= 0x1;

   if (state->cb) {
      for (uint32_t i = 0; i < state->cb->attachment_count; i++) {
         VkBlendOp eqRGB = state->cb->attachments[i].color_blend_op;
         VkBlendFactor srcRGB = state->cb->attachments[i].src_color_blend_factor;
         VkBlendFactor dstRGB = state->cb->attachments[i].dst_color_blend_factor;

         /* Ignore other blend targets if dual-source blending is enabled to prevent wrong
          * behaviour.
          */
         if (i > 0 && ps_epilog.mrt0_is_dual_src)
            continue;

         ps_epilog.color_write_mask |= (unsigned)state->cb->attachments[i].write_mask << (4 * i);
         if (!((ps_epilog.color_write_mask >> (i * 4)) & 0xf))
            continue;

         if (state->cb->attachments[i].blend_enable)
            ps_epilog.color_blend_enable |= 0xfu << (i * 4);

         if (!((ps_epilog.color_blend_enable >> (i * 4)) & 0xf))
            continue;

         if (i == 0 && radv_can_enable_dual_src(&state->cb->attachments[i])) {
            ps_epilog.mrt0_is_dual_src = true;
         }

         radv_normalize_blend_factor(eqRGB, &srcRGB, &dstRGB);

         if (srcRGB == VK_BLEND_FACTOR_SRC_ALPHA || dstRGB == VK_BLEND_FACTOR_SRC_ALPHA ||
             srcRGB == VK_BLEND_FACTOR_SRC_ALPHA_SATURATE || dstRGB == VK_BLEND_FACTOR_SRC_ALPHA_SATURATE ||
             srcRGB == VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA || dstRGB == VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA)
            ps_epilog.need_src_alpha |= 1 << i;
      }
   }

   if (state->rp) {
      ps_epilog.color_attachment_count = state->rp->color_attachment_count;

      for (uint32_t i = 0; i < ps_epilog.color_attachment_count; i++) {
         ps_epilog.color_attachment_formats[i] = state->rp->color_attachment_formats[i];
      }
   }

   if (state->ms)
      ps_epilog.alpha_to_one = state->ms->alpha_to_one_enable;

   for (uint32_t i = 0; i < MAX_RTS; i++) {
      ps_epilog.color_attachment_mappings[i] = state->cal ? state->cal->color_map[i] : i;
   }

   return radv_generate_ps_epilog_key(device, &ps_epilog);
}

static struct radv_graphics_state_key
radv_generate_graphics_state_key(const struct radv_device *device, const struct vk_graphics_pipeline_state *state,
                                 VkGraphicsPipelineLibraryFlagBitsEXT lib_flags)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   struct radv_graphics_state_key key;

   memset(&key, 0, sizeof(key));

   key.lib_flags = lib_flags;
   key.has_multiview_view_index = state->rp ? !!state->rp->view_mask : 0;

   if (BITSET_TEST(state->dynamic, MESA_VK_DYNAMIC_VI)) {
      key.vs.has_prolog = true;
   }

   /* Compile the pre-rasterization stages only when the vertex input interface is missing. */
   if ((state->shader_stages && VK_SHADER_STAGE_VERTEX_BIT) && !state->vi) {
      key.vs.has_prolog = true;
   }

   /* Vertex input state */
   if (state->vi) {
      u_foreach_bit (i, state->vi->attributes_valid) {
         uint32_t binding = state->vi->attributes[i].binding;
         uint32_t offset = state->vi->attributes[i].offset;
         enum pipe_format format = radv_format_to_pipe_format(state->vi->attributes[i].format);

         key.vi.vertex_attribute_formats[i] = format;
         key.vi.vertex_attribute_bindings[i] = binding;
         key.vi.vertex_attribute_offsets[i] = offset;
         key.vi.instance_rate_divisors[i] = state->vi->bindings[binding].divisor;

         /* vertex_attribute_strides is only needed to workaround GFX6/7 offset>=stride checks. */
         if (!BITSET_TEST(state->dynamic, MESA_VK_DYNAMIC_VI_BINDING_STRIDES) && pdev->info.gfx_level < GFX8) {
            /* From the Vulkan spec 1.2.157:
             *
             * "If the bound pipeline state object was created with the
             * VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE dynamic state enabled then pStrides[i]
             * specifies the distance in bytes between two consecutive elements within the
             * corresponding buffer. In this case the VkVertexInputBindingDescription::stride state
             * from the pipeline state object is ignored."
             *
             * Make sure the vertex attribute stride is zero to avoid computing a wrong offset if
             * it's initialized to something else than zero.
             */
            key.vi.vertex_attribute_strides[i] = state->vi->bindings[binding].stride;
         }

         if (state->vi->bindings[binding].input_rate) {
            key.vi.instance_rate_inputs |= 1u << i;
         }

         const struct ac_vtx_format_info *vtx_info =
            ac_get_vtx_format_info(pdev->info.gfx_level, pdev->info.family, format);
         unsigned attrib_align = vtx_info->chan_byte_size ? vtx_info->chan_byte_size : vtx_info->element_size;

         /* If offset is misaligned, then the buffer offset must be too. Just skip updating
          * vertex_binding_align in this case.
          */
         if (offset % attrib_align == 0) {
            key.vi.vertex_binding_align[binding] = MAX2(key.vi.vertex_binding_align[binding], attrib_align);
         }
      }
   }

   if (state->ts)
      key.ts.patch_control_points = state->ts->patch_control_points;

   const bool alpha_to_coverage_unknown =
      !state->ms || BITSET_TEST(state->dynamic, MESA_VK_DYNAMIC_MS_ALPHA_TO_COVERAGE_ENABLE);
   const bool alpha_to_coverage_enabled = alpha_to_coverage_unknown || state->ms->alpha_to_coverage_enable;
   const bool alpha_to_one_unknown = !state->ms || BITSET_TEST(state->dynamic, MESA_VK_DYNAMIC_MS_ALPHA_TO_ONE_ENABLE);
   const bool alpha_to_one_enabled = alpha_to_one_unknown || state->ms->alpha_to_one_enable;

   /* alpha-to-coverage is always exported via MRTZ on GFX11 but it's also using MRTZ when
    * alpha-to-one is enabled (alpha to MRTZ.a and one to MRT0.a).
    */
   key.ms.alpha_to_coverage_via_mrtz =
      alpha_to_coverage_enabled && (pdev->info.gfx_level >= GFX11 || alpha_to_one_enabled);

   if (state->ms) {
      key.ms.sample_shading_enable = state->ms->sample_shading_enable;
      if (!BITSET_TEST(state->dynamic, MESA_VK_DYNAMIC_MS_RASTERIZATION_SAMPLES) &&
          state->ms->rasterization_samples > 1) {
         key.ms.rasterization_samples = state->ms->rasterization_samples;
      }
   }

   if (state->ia) {
      key.ia.topology = radv_translate_prim(state->ia->primitive_topology);
   }

   if (!state->vi || !(state->shader_stages & (VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT |
                                               VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_MESH_BIT_EXT))) {
      key.unknown_rast_prim = true;
   }

   if (pdev->info.gfx_level >= GFX10 && state->rs) {
      key.rs.provoking_vtx_last = state->rs->provoking_vertex == VK_PROVOKING_VERTEX_MODE_LAST_VERTEX_EXT;
   }

   key.ps.force_vrs_enabled = device->force_vrs_enabled && !radv_is_static_vrs_enabled(state);

   if ((radv_is_vrs_enabled(state) || key.ps.force_vrs_enabled) &&
       (pdev->info.family == CHIP_NAVI21 || pdev->info.family == CHIP_NAVI22 || pdev->info.family == CHIP_VANGOGH))
      key.adjust_frag_coord_z = true;

   if (radv_pipeline_needs_ps_epilog(state, lib_flags))
      key.ps.has_epilog = true;

   key.ps.epilog = radv_pipeline_generate_ps_epilog_key(device, state);

   /* Alpha to coverage is exported via MRTZ when depth/stencil/samplemask are also exported.
    * Though, when a PS epilog is needed and the MS state is NULL (with dynamic rendering), it's not
    * possible to know the info at compile time and MRTZ needs to be exported in the epilog.
    */
   if (key.ps.has_epilog) {
      if (pdev->info.gfx_level >= GFX11) {
         key.ps.exports_mrtz_via_epilog = alpha_to_coverage_unknown;
      } else {
         key.ps.exports_mrtz_via_epilog =
            (alpha_to_coverage_unknown && alpha_to_one_enabled) || (alpha_to_one_unknown && alpha_to_coverage_enabled);
      }
   }

   key.dynamic_rasterization_samples = BITSET_TEST(state->dynamic, MESA_VK_DYNAMIC_MS_RASTERIZATION_SAMPLES) ||
                                       (!!(state->shader_stages & VK_SHADER_STAGE_FRAGMENT_BIT) && !state->ms);

   if (pdev->use_ngg) {
      VkShaderStageFlags ngg_stage;

      if (state->shader_stages & VK_SHADER_STAGE_GEOMETRY_BIT) {
         ngg_stage = VK_SHADER_STAGE_GEOMETRY_BIT;
      } else if (state->shader_stages & VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT) {
         ngg_stage = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
      } else {
         ngg_stage = VK_SHADER_STAGE_VERTEX_BIT;
      }

      key.dynamic_provoking_vtx_mode =
         BITSET_TEST(state->dynamic, MESA_VK_DYNAMIC_RS_PROVOKING_VERTEX) &&
         (ngg_stage == VK_SHADER_STAGE_VERTEX_BIT || ngg_stage == VK_SHADER_STAGE_GEOMETRY_BIT);
   }

   if (!BITSET_TEST(state->dynamic, MESA_VK_DYNAMIC_IA_PRIMITIVE_TOPOLOGY) && state->ia &&
       state->ia->primitive_topology != VK_PRIMITIVE_TOPOLOGY_POINT_LIST &&
       !BITSET_TEST(state->dynamic, MESA_VK_DYNAMIC_RS_POLYGON_MODE) && state->rs &&
       state->rs->polygon_mode != VK_POLYGON_MODE_POINT) {
      key.enable_remove_point_size = true;
   }

   if (device->vk.enabled_features.smoothLines) {
      /* Make the line rasterization mode dynamic for smooth lines to conditionally enable the lowering at draw time.
       * This is because it's not possible to know if the graphics pipeline will draw lines at this point and it also
       * simplifies the implementation.
       */
      if (BITSET_TEST(state->dynamic, MESA_VK_DYNAMIC_RS_LINE_MODE) ||
          (state->rs && state->rs->line.mode == VK_LINE_RASTERIZATION_MODE_RECTANGULAR_SMOOTH))
         key.dynamic_line_rast_mode = true;

      /* For GPL, when the fragment shader is compiled without any pre-rasterization information,
       * ensure the line rasterization mode is considered dynamic because we can't know if it's
       * going to draw lines or not.
       */
      key.dynamic_line_rast_mode |= !!(lib_flags & VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT) &&
                                    !(lib_flags & VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT);
   }

   return key;
}

static struct radv_graphics_pipeline_key
radv_generate_graphics_pipeline_key(const struct radv_device *device, const VkGraphicsPipelineCreateInfo *pCreateInfo,
                                    const struct vk_graphics_pipeline_state *state,
                                    VkGraphicsPipelineLibraryFlagBitsEXT lib_flags)
{
   VkPipelineCreateFlags2 create_flags = vk_graphics_pipeline_create_flags(pCreateInfo);
   struct radv_graphics_pipeline_key key = {0};

   key.gfx_state = radv_generate_graphics_state_key(device, state, lib_flags);

   for (uint32_t i = 0; i < pCreateInfo->stageCount; i++) {
      const VkPipelineShaderStageCreateInfo *stage = &pCreateInfo->pStages[i];
      gl_shader_stage s = vk_to_mesa_shader_stage(stage->stage);

      key.stage_info[s] = radv_pipeline_get_shader_key(device, stage, create_flags, pCreateInfo->pNext);

      if (s == MESA_SHADER_MESH && (state->shader_stages & VK_SHADER_STAGE_TASK_BIT_EXT))
         key.stage_info[s].has_task_shader = true;
   }

   return key;
}

static void
radv_fill_shader_info_ngg(struct radv_device *device, struct radv_shader_stage *stages,
                          VkShaderStageFlagBits active_nir_stages)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   const struct radv_instance *instance = radv_physical_device_instance(pdev);

   if (!pdev->cache_key.use_ngg)
      return;

   if (stages[MESA_SHADER_VERTEX].nir && stages[MESA_SHADER_VERTEX].info.next_stage != MESA_SHADER_TESS_CTRL) {
      stages[MESA_SHADER_VERTEX].info.is_ngg = true;
   } else if (stages[MESA_SHADER_TESS_EVAL].nir) {
      stages[MESA_SHADER_TESS_EVAL].info.is_ngg = true;
   } else if (stages[MESA_SHADER_MESH].nir) {
      stages[MESA_SHADER_MESH].info.is_ngg = true;
   }

   if (pdev->info.gfx_level >= GFX11) {
      if (stages[MESA_SHADER_GEOMETRY].nir)
         stages[MESA_SHADER_GEOMETRY].info.is_ngg = true;
   } else {
      /* GFX10/GFX10.3 can't always enable NGG due to HW bugs/limitations. */
      if (stages[MESA_SHADER_TESS_EVAL].nir && stages[MESA_SHADER_GEOMETRY].nir &&
          stages[MESA_SHADER_GEOMETRY].nir->info.gs.invocations *
                stages[MESA_SHADER_GEOMETRY].nir->info.gs.vertices_out >
             256) {
         /* Fallback to the legacy path if tessellation is
          * enabled with extreme geometry because
          * EN_MAX_VERT_OUT_PER_GS_INSTANCE doesn't work and it
          * might hang.
          */
         stages[MESA_SHADER_TESS_EVAL].info.is_ngg = false;
      }

      struct radv_shader_stage *last_vgt_stage = NULL;
      radv_foreach_stage(i, active_nir_stages)
      {
         if (radv_is_last_vgt_stage(&stages[i])) {
            last_vgt_stage = &stages[i];
         }
      }

      if ((last_vgt_stage && last_vgt_stage->nir->xfb_info) ||
          ((instance->debug_flags & RADV_DEBUG_NO_NGG_GS) && stages[MESA_SHADER_GEOMETRY].nir)) {
         /* NGG needs to be disabled on GFX10/GFX10.3 when:
          * - streamout is used because NGG streamout isn't supported
          * - NGG GS is explictly disabled to workaround performance issues
          */
         if (stages[MESA_SHADER_TESS_EVAL].nir)
            stages[MESA_SHADER_TESS_EVAL].info.is_ngg = false;
         else
            stages[MESA_SHADER_VERTEX].info.is_ngg = false;
      }

      if (stages[MESA_SHADER_GEOMETRY].nir) {
         if (stages[MESA_SHADER_TESS_EVAL].nir)
            stages[MESA_SHADER_GEOMETRY].info.is_ngg = stages[MESA_SHADER_TESS_EVAL].info.is_ngg;
         else
            stages[MESA_SHADER_GEOMETRY].info.is_ngg = stages[MESA_SHADER_VERTEX].info.is_ngg;
      }

      /* When pre-rasterization stages are compiled separately with shader objects, NGG GS needs to
       * be disabled because if the next stage of VS/TES is GS and GS is unknown, it might use
       * streamout but it's not possible to know that when compiling VS or TES only.
       */
      if (stages[MESA_SHADER_VERTEX].nir && stages[MESA_SHADER_VERTEX].info.next_stage == MESA_SHADER_GEOMETRY &&
          !stages[MESA_SHADER_GEOMETRY].nir) {
         stages[MESA_SHADER_VERTEX].info.is_ngg = false;
      } else if (stages[MESA_SHADER_TESS_EVAL].nir &&
                 stages[MESA_SHADER_TESS_EVAL].info.next_stage == MESA_SHADER_GEOMETRY &&
                 !stages[MESA_SHADER_GEOMETRY].nir) {
         stages[MESA_SHADER_TESS_EVAL].info.is_ngg = false;
      } else if (stages[MESA_SHADER_GEOMETRY].nir &&
                 (!stages[MESA_SHADER_VERTEX].nir && !stages[MESA_SHADER_TESS_EVAL].nir)) {
         stages[MESA_SHADER_GEOMETRY].info.is_ngg = false;
      }
   }
}

static bool
radv_consider_force_vrs(const struct radv_graphics_state_key *gfx_state, const struct radv_shader_stage *last_vgt_stage,
                        const struct radv_shader_stage *fs_stage)
{
   if (!gfx_state->ps.force_vrs_enabled)
      return false;

   /* Mesh shaders aren't considered. */
   if (last_vgt_stage->info.stage == MESA_SHADER_MESH)
      return false;

   if (last_vgt_stage->nir->info.outputs_written & BITFIELD64_BIT(VARYING_SLOT_PRIMITIVE_SHADING_RATE))
      return false;

   /* VRS has no effect if there is no pixel shader. */
   if (last_vgt_stage->info.next_stage == MESA_SHADER_NONE)
      return false;

   /* Do not enable if the PS uses gl_FragCoord because it breaks postprocessing in some games, or with Primitive
    * Ordered Pixel Shading (regardless of whether per-pixel data is addressed with gl_FragCoord or a custom
    * interpolator) as that'd result in races between adjacent primitives with no common fine pixels.
    */
   nir_shader *fs_shader = fs_stage->nir;
   if (fs_shader && (BITSET_TEST(fs_shader->info.system_values_read, SYSTEM_VALUE_FRAG_COORD) ||
                     BITSET_TEST(fs_shader->info.system_values_read, SYSTEM_VALUE_PIXEL_COORD) ||
                     fs_shader->info.fs.sample_interlock_ordered || fs_shader->info.fs.sample_interlock_unordered ||
                     fs_shader->info.fs.pixel_interlock_ordered || fs_shader->info.fs.pixel_interlock_unordered)) {
      return false;
   }

   return true;
}

static gl_shader_stage
radv_get_next_stage(gl_shader_stage stage, VkShaderStageFlagBits active_nir_stages)
{
   switch (stage) {
   case MESA_SHADER_VERTEX:
      if (active_nir_stages & VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT) {
         return MESA_SHADER_TESS_CTRL;
      } else if (active_nir_stages & VK_SHADER_STAGE_GEOMETRY_BIT) {
         return MESA_SHADER_GEOMETRY;
      } else if (active_nir_stages & VK_SHADER_STAGE_FRAGMENT_BIT) {
         return MESA_SHADER_FRAGMENT;
      } else {
         return MESA_SHADER_NONE;
      }
   case MESA_SHADER_TESS_CTRL:
      return MESA_SHADER_TESS_EVAL;
   case MESA_SHADER_TESS_EVAL:
      if (active_nir_stages & VK_SHADER_STAGE_GEOMETRY_BIT) {
         return MESA_SHADER_GEOMETRY;
      } else if (active_nir_stages & VK_SHADER_STAGE_FRAGMENT_BIT) {
         return MESA_SHADER_FRAGMENT;
      } else {
         return MESA_SHADER_NONE;
      }
   case MESA_SHADER_GEOMETRY:
   case MESA_SHADER_MESH:
      if (active_nir_stages & VK_SHADER_STAGE_FRAGMENT_BIT) {
         return MESA_SHADER_FRAGMENT;
      } else {
         return MESA_SHADER_NONE;
      }
   case MESA_SHADER_TASK:
      return MESA_SHADER_MESH;
   case MESA_SHADER_FRAGMENT:
      return MESA_SHADER_NONE;
   default:
      unreachable("invalid graphics shader stage");
   }
}

static void
radv_fill_shader_info(struct radv_device *device, const enum radv_pipeline_type pipeline_type,
                      const struct radv_graphics_state_key *gfx_state, struct radv_shader_stage *stages,
                      VkShaderStageFlagBits active_nir_stages)
{
   radv_foreach_stage(i, active_nir_stages)
   {
      bool consider_force_vrs = false;

      if (radv_is_last_vgt_stage(&stages[i])) {
         consider_force_vrs = radv_consider_force_vrs(gfx_state, &stages[i], &stages[MESA_SHADER_FRAGMENT]);
      }

      radv_nir_shader_info_pass(device, stages[i].nir, &stages[i].layout, &stages[i].key, gfx_state, pipeline_type,
                                consider_force_vrs, &stages[i].info);
   }

   radv_nir_shader_info_link(device, gfx_state, stages);
}

static void
radv_declare_pipeline_args(struct radv_device *device, struct radv_shader_stage *stages,
                           const struct radv_graphics_state_key *gfx_state, VkShaderStageFlagBits active_nir_stages)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   enum amd_gfx_level gfx_level = pdev->info.gfx_level;

   if (gfx_level >= GFX9 && stages[MESA_SHADER_TESS_CTRL].nir) {
      radv_declare_shader_args(device, gfx_state, &stages[MESA_SHADER_TESS_CTRL].info, MESA_SHADER_TESS_CTRL,
                               MESA_SHADER_VERTEX, &stages[MESA_SHADER_TESS_CTRL].args);
      stages[MESA_SHADER_TESS_CTRL].info.user_sgprs_locs = stages[MESA_SHADER_TESS_CTRL].args.user_sgprs_locs;
      stages[MESA_SHADER_TESS_CTRL].info.inline_push_constant_mask =
         stages[MESA_SHADER_TESS_CTRL].args.ac.inline_push_const_mask;

      stages[MESA_SHADER_VERTEX].info.user_sgprs_locs = stages[MESA_SHADER_TESS_CTRL].info.user_sgprs_locs;
      stages[MESA_SHADER_VERTEX].info.inline_push_constant_mask =
         stages[MESA_SHADER_TESS_CTRL].info.inline_push_constant_mask;
      stages[MESA_SHADER_VERTEX].args = stages[MESA_SHADER_TESS_CTRL].args;

      active_nir_stages &= ~(1 << MESA_SHADER_VERTEX);
      active_nir_stages &= ~(1 << MESA_SHADER_TESS_CTRL);
   }

   if (gfx_level >= GFX9 && stages[MESA_SHADER_GEOMETRY].nir) {
      gl_shader_stage pre_stage = stages[MESA_SHADER_TESS_EVAL].nir ? MESA_SHADER_TESS_EVAL : MESA_SHADER_VERTEX;
      radv_declare_shader_args(device, gfx_state, &stages[MESA_SHADER_GEOMETRY].info, MESA_SHADER_GEOMETRY, pre_stage,
                               &stages[MESA_SHADER_GEOMETRY].args);
      stages[MESA_SHADER_GEOMETRY].info.user_sgprs_locs = stages[MESA_SHADER_GEOMETRY].args.user_sgprs_locs;
      stages[MESA_SHADER_GEOMETRY].info.inline_push_constant_mask =
         stages[MESA_SHADER_GEOMETRY].args.ac.inline_push_const_mask;

      stages[pre_stage].info.user_sgprs_locs = stages[MESA_SHADER_GEOMETRY].info.user_sgprs_locs;
      stages[pre_stage].info.inline_push_constant_mask = stages[MESA_SHADER_GEOMETRY].info.inline_push_constant_mask;
      stages[pre_stage].args = stages[MESA_SHADER_GEOMETRY].args;
      active_nir_stages &= ~(1 << pre_stage);
      active_nir_stages &= ~(1 << MESA_SHADER_GEOMETRY);
   }

   u_foreach_bit (i, active_nir_stages) {
      radv_declare_shader_args(device, gfx_state, &stages[i].info, i, MESA_SHADER_NONE, &stages[i].args);
      stages[i].info.user_sgprs_locs = stages[i].args.user_sgprs_locs;
      stages[i].info.inline_push_constant_mask = stages[i].args.ac.inline_push_const_mask;
   }
}

static struct radv_shader *
radv_create_gs_copy_shader(struct radv_device *device, struct vk_pipeline_cache *cache,
                           struct radv_shader_stage *gs_stage, const struct radv_graphics_state_key *gfx_state,
                           bool keep_executable_info, bool keep_statistic_info, bool skip_shaders_cache,
                           struct radv_shader_binary **gs_copy_binary)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   struct radv_instance *instance = radv_physical_device_instance(pdev);

   const struct radv_shader_info *gs_info = &gs_stage->info;
   ac_nir_gs_output_info output_info = {
      .streams = gs_info->gs.output_streams,
      .sysval_mask = gs_info->gs.output_usage_mask,
      .varying_mask = gs_info->gs.output_usage_mask,
   };
   nir_shader *nir = ac_nir_create_gs_copy_shader(
      gs_stage->nir, pdev->info.gfx_level, gs_info->outinfo.clip_dist_mask | gs_info->outinfo.cull_dist_mask,
      gs_info->outinfo.vs_output_param_offset, gs_info->outinfo.param_exports, false, false, false,
      gs_info->force_vrs_per_vertex, &output_info);

   nir->info.internal = true;

   nir_validate_shader(nir, "after ac_nir_create_gs_copy_shader");
   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));

   struct radv_shader_stage gs_copy_stage = {
      .stage = MESA_SHADER_VERTEX,
      .shader_sha1 = {0},
      .key =
         {
            .optimisations_disabled = gs_stage->key.optimisations_disabled,
         },
   };
   radv_nir_shader_info_init(gs_copy_stage.stage, MESA_SHADER_FRAGMENT, &gs_copy_stage.info);
   radv_nir_shader_info_pass(device, nir, &gs_stage->layout, &gs_stage->key, gfx_state, RADV_PIPELINE_GRAPHICS, false,
                             &gs_copy_stage.info);
   gs_copy_stage.info.wave_size = 64;      /* Wave32 not supported. */
   gs_copy_stage.info.workgroup_size = 64; /* HW VS: separate waves, no workgroups */
   gs_copy_stage.info.so = gs_info->so;
   gs_copy_stage.info.outinfo = gs_info->outinfo;
   gs_copy_stage.info.force_vrs_per_vertex = gs_info->force_vrs_per_vertex;
   gs_copy_stage.info.type = RADV_SHADER_TYPE_GS_COPY;

   radv_declare_shader_args(device, gfx_state, &gs_copy_stage.info, MESA_SHADER_VERTEX, MESA_SHADER_NONE,
                            &gs_copy_stage.args);
   gs_copy_stage.info.user_sgprs_locs = gs_copy_stage.args.user_sgprs_locs;
   gs_copy_stage.info.inline_push_constant_mask = gs_copy_stage.args.ac.inline_push_const_mask;

   NIR_PASS_V(nir, ac_nir_lower_intrinsics_to_args, pdev->info.gfx_level, pdev->info.has_ls_vgpr_init_bug,
              AC_HW_VERTEX_SHADER, 64, 64, &gs_copy_stage.args.ac);
   NIR_PASS_V(nir, radv_nir_lower_abi, pdev->info.gfx_level, &gs_copy_stage, gfx_state, pdev->info.address32_hi);

   struct radv_graphics_pipeline_key key = {0};
   bool dump_shader = radv_can_dump_shader(device, nir);

   if (dump_shader)
      simple_mtx_lock(&instance->shader_dump_mtx);

   char *nir_string = NULL;
   if (keep_executable_info || dump_shader)
      nir_string = radv_dump_nir_shaders(instance, &nir, 1);

   *gs_copy_binary = radv_shader_nir_to_asm(device, &gs_copy_stage, &nir, 1, &key.gfx_state, keep_executable_info,
                                            keep_statistic_info);
   struct radv_shader *copy_shader =
      radv_shader_create(device, cache, *gs_copy_binary, skip_shaders_cache || dump_shader);

   if (copy_shader) {
      copy_shader->nir_string = nir_string;
      radv_shader_dump_debug_info(device, dump_shader, *gs_copy_binary, copy_shader, &nir, 1, &gs_copy_stage.info);
   }

   if (dump_shader)
      simple_mtx_unlock(&instance->shader_dump_mtx);

   return copy_shader;
}

static void
radv_graphics_shaders_nir_to_asm(struct radv_device *device, struct vk_pipeline_cache *cache,
                                 struct radv_shader_stage *stages, const struct radv_graphics_state_key *gfx_state,
                                 bool keep_executable_info, bool keep_statistic_info, bool skip_shaders_cache,
                                 VkShaderStageFlagBits active_nir_stages, struct radv_shader **shaders,
                                 struct radv_shader_binary **binaries, struct radv_shader **gs_copy_shader,
                                 struct radv_shader_binary **gs_copy_binary)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   struct radv_instance *instance = radv_physical_device_instance(pdev);

   for (int s = MESA_VULKAN_SHADER_STAGES - 1; s >= 0; s--) {
      if (!(active_nir_stages & (1 << s)))
         continue;

      nir_shader *nir_shaders[2] = {stages[s].nir, NULL};
      unsigned shader_count = 1;

      /* On GFX9+, TES is merged with GS and VS is merged with TCS or GS. */
      if (pdev->info.gfx_level >= GFX9 &&
          ((s == MESA_SHADER_GEOMETRY &&
            (active_nir_stages & (VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT))) ||
           (s == MESA_SHADER_TESS_CTRL && (active_nir_stages & VK_SHADER_STAGE_VERTEX_BIT)))) {
         gl_shader_stage pre_stage;

         if (s == MESA_SHADER_GEOMETRY && (active_nir_stages & VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT)) {
            pre_stage = MESA_SHADER_TESS_EVAL;
         } else {
            pre_stage = MESA_SHADER_VERTEX;
         }

         nir_shaders[0] = stages[pre_stage].nir;
         nir_shaders[1] = stages[s].nir;
         shader_count = 2;
      }

      int64_t stage_start = os_time_get_nano();

      bool dump_shader = false;
      for (unsigned i = 0; i < shader_count; ++i)
         dump_shader |= radv_can_dump_shader(device, nir_shaders[i]);

      bool dump_nir = dump_shader && (instance->debug_flags & RADV_DEBUG_DUMP_NIR);

      if (dump_shader) {
         simple_mtx_lock(&instance->shader_dump_mtx);

         if (dump_nir) {
            for (uint32_t i = 0; i < shader_count; i++)
               nir_print_shader(nir_shaders[i], stderr);
         }
      }

      char *nir_string = NULL;
      if (keep_executable_info || dump_shader)
         nir_string = radv_dump_nir_shaders(instance, nir_shaders, shader_count);

      binaries[s] = radv_shader_nir_to_asm(device, &stages[s], nir_shaders, shader_count, gfx_state,
                                           keep_executable_info, keep_statistic_info);
      shaders[s] = radv_shader_create(device, cache, binaries[s], skip_shaders_cache || dump_shader);

      shaders[s]->nir_string = nir_string;

      radv_shader_dump_debug_info(device, dump_shader, binaries[s], shaders[s], nir_shaders, shader_count,
                                  &stages[s].info);

      if (dump_shader)
         simple_mtx_unlock(&instance->shader_dump_mtx);

      if (s == MESA_SHADER_GEOMETRY && !stages[s].info.is_ngg) {
         *gs_copy_shader =
            radv_create_gs_copy_shader(device, cache, &stages[MESA_SHADER_GEOMETRY], gfx_state, keep_executable_info,
                                       keep_statistic_info, skip_shaders_cache, gs_copy_binary);
      }

      stages[s].feedback.duration += os_time_get_nano() - stage_start;

      active_nir_stages &= ~(1 << nir_shaders[0]->info.stage);
      if (nir_shaders[1])
         active_nir_stages &= ~(1 << nir_shaders[1]->info.stage);
   }
}

static void
radv_pipeline_retain_shaders(struct radv_retained_shaders *retained_shaders, struct radv_shader_stage *stages)
{
   for (unsigned s = 0; s < MESA_VULKAN_SHADER_STAGES; s++) {
      if (stages[s].stage == MESA_SHADER_NONE)
         continue;

      int64_t stage_start = os_time_get_nano();

      /* Serialize the NIR shader to reduce memory pressure. */
      struct blob blob;

      blob_init(&blob);
      nir_serialize(&blob, stages[s].nir, true);
      blob_finish_get_buffer(&blob, &retained_shaders->stages[s].serialized_nir,
                             &retained_shaders->stages[s].serialized_nir_size);

      memcpy(retained_shaders->stages[s].shader_sha1, stages[s].shader_sha1, sizeof(stages[s].shader_sha1));
      memcpy(&retained_shaders->stages[s].key, &stages[s].key, sizeof(stages[s].key));

      stages[s].feedback.duration += os_time_get_nano() - stage_start;
   }
}

static void
radv_pipeline_import_retained_shaders(const struct radv_device *device, struct radv_graphics_lib_pipeline *lib,
                                      struct radv_shader_stage *stages)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   struct radv_retained_shaders *retained_shaders = &lib->retained_shaders;

   /* Import the stages (SPIR-V only in case of cache hits). */
   for (uint32_t i = 0; i < lib->stage_count; i++) {
      const VkPipelineShaderStageCreateInfo *sinfo = &lib->stages[i];
      gl_shader_stage s = vk_to_mesa_shader_stage(sinfo->stage);

      radv_pipeline_stage_init(lib->base.base.create_flags, sinfo,
                               &lib->layout, &lib->stage_keys[s], &stages[s]);
   }

   /* Import the NIR shaders (after SPIRV->NIR). */
   for (uint32_t s = 0; s < ARRAY_SIZE(lib->base.base.shaders); s++) {
      if (!retained_shaders->stages[s].serialized_nir_size)
         continue;

      int64_t stage_start = os_time_get_nano();

      /* Deserialize the NIR shader. */
      const struct nir_shader_compiler_options *options = &pdev->nir_options[s];
      struct blob_reader blob_reader;
      blob_reader_init(&blob_reader, retained_shaders->stages[s].serialized_nir,
                       retained_shaders->stages[s].serialized_nir_size);

      stages[s].stage = s;
      stages[s].nir = nir_deserialize(NULL, options, &blob_reader);
      stages[s].entrypoint = nir_shader_get_entrypoint(stages[s].nir)->function->name;
      memcpy(stages[s].shader_sha1, retained_shaders->stages[s].shader_sha1, sizeof(stages[s].shader_sha1));
      memcpy(&stages[s].key, &retained_shaders->stages[s].key, sizeof(stages[s].key));

      radv_shader_layout_init(&lib->layout, s, &stages[s].layout);

      stages[s].feedback.flags |= VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT;

      stages[s].feedback.duration += os_time_get_nano() - stage_start;
   }
}

static void
radv_pipeline_load_retained_shaders(const struct radv_device *device, const VkGraphicsPipelineCreateInfo *pCreateInfo,
                                    struct radv_shader_stage *stages)
{
   const VkPipelineCreateFlags2 create_flags = vk_graphics_pipeline_create_flags(pCreateInfo);
   const VkPipelineLibraryCreateInfoKHR *libs_info =
      vk_find_struct_const(pCreateInfo->pNext, PIPELINE_LIBRARY_CREATE_INFO_KHR);

   /* Nothing to load if no libs are imported. */
   if (!libs_info)
      return;

   /* Nothing to load if fast-linking is enabled and if there is no retained shaders. */
   if (radv_should_import_lib_binaries(create_flags))
      return;

   for (uint32_t i = 0; i < libs_info->libraryCount; i++) {
      VK_FROM_HANDLE(radv_pipeline, pipeline_lib, libs_info->pLibraries[i]);
      struct radv_graphics_lib_pipeline *gfx_pipeline_lib = radv_pipeline_to_graphics_lib(pipeline_lib);

      radv_pipeline_import_retained_shaders(device, gfx_pipeline_lib, stages);
   }
}

static unsigned
radv_get_rasterization_prim(const struct radv_shader_stage *stages, const struct radv_graphics_state_key *gfx_state)
{
   unsigned rast_prim;

   if (gfx_state->unknown_rast_prim)
      return -1;

   if (stages[MESA_SHADER_GEOMETRY].nir) {
      rast_prim = radv_conv_gl_prim_to_gs_out(stages[MESA_SHADER_GEOMETRY].nir->info.gs.output_primitive);
   } else if (stages[MESA_SHADER_TESS_EVAL].nir) {
      if (stages[MESA_SHADER_TESS_EVAL].nir->info.tess.point_mode) {
         rast_prim = V_028A6C_POINTLIST;
      } else {
         rast_prim = radv_conv_tess_prim_to_gs_out(stages[MESA_SHADER_TESS_EVAL].nir->info.tess._primitive_mode);
      }
   } else if (stages[MESA_SHADER_MESH].nir) {
      rast_prim = radv_conv_gl_prim_to_gs_out(stages[MESA_SHADER_MESH].nir->info.mesh.primitive_type);
   } else {
      rast_prim = radv_conv_prim_to_gs_out(gfx_state->ia.topology, false);
   }

   return rast_prim;
}

static bool
radv_is_fast_linking_enabled(const VkGraphicsPipelineCreateInfo *pCreateInfo)
{
   const VkPipelineCreateFlags2 create_flags = vk_graphics_pipeline_create_flags(pCreateInfo);
   const VkPipelineLibraryCreateInfoKHR *libs_info =
      vk_find_struct_const(pCreateInfo->pNext, PIPELINE_LIBRARY_CREATE_INFO_KHR);

   if (!libs_info)
      return false;

   return !(create_flags & VK_PIPELINE_CREATE_2_LINK_TIME_OPTIMIZATION_BIT_EXT);
}

static bool
radv_skip_graphics_pipeline_compile(const struct radv_device *device, const VkGraphicsPipelineCreateInfo *pCreateInfo)
{
   const VkPipelineBinaryInfoKHR *binary_info = vk_find_struct_const(pCreateInfo->pNext, PIPELINE_BINARY_INFO_KHR);
   const VkPipelineCreateFlags2 create_flags = vk_graphics_pipeline_create_flags(pCreateInfo);
   const struct radv_physical_device *pdev = radv_device_physical(device);
   VkShaderStageFlagBits binary_stages = 0;
   VkShaderStageFlags active_stages = 0;

   /* No compilation when pipeline binaries are imported. */
   if (binary_info && binary_info->binaryCount > 0)
      return true;

   /* Do not skip for libraries. */
   if (create_flags & VK_PIPELINE_CREATE_2_LIBRARY_BIT_KHR)
      return false;

   /* Do not skip when fast-linking isn't enabled. */
   if (!radv_is_fast_linking_enabled(pCreateInfo))
      return false;

   for (uint32_t i = 0; i < pCreateInfo->stageCount; i++) {
      const VkPipelineShaderStageCreateInfo *sinfo = &pCreateInfo->pStages[i];
      active_stages |= sinfo->stage;
   }

   const VkPipelineLibraryCreateInfoKHR *libs_info =
      vk_find_struct_const(pCreateInfo->pNext, PIPELINE_LIBRARY_CREATE_INFO_KHR);
   if (libs_info) {
      for (uint32_t i = 0; i < libs_info->libraryCount; i++) {
         VK_FROM_HANDLE(radv_pipeline, pipeline_lib, libs_info->pLibraries[i]);
         struct radv_graphics_lib_pipeline *gfx_pipeline_lib = radv_pipeline_to_graphics_lib(pipeline_lib);

         assert(pipeline_lib->type == RADV_PIPELINE_GRAPHICS_LIB);

         active_stages |= gfx_pipeline_lib->base.active_stages;

         for (uint32_t s = 0; s < MESA_VULKAN_SHADER_STAGES; s++) {
            if (!gfx_pipeline_lib->base.base.shaders[s])
               continue;

            binary_stages |= mesa_to_vk_shader_stage(s);
         }
      }
   }

   if (pdev->info.gfx_level >= GFX9) {
      /* On GFX9+, TES is merged with GS and VS is merged with TCS or GS. */
      if (binary_stages & VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT) {
         binary_stages |= VK_SHADER_STAGE_VERTEX_BIT;
      }

      if (binary_stages & VK_SHADER_STAGE_GEOMETRY_BIT) {
         if (binary_stages & VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT) {
            binary_stages |= VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
         } else {
            binary_stages |= VK_SHADER_STAGE_VERTEX_BIT;
         }
      }
   }

   /* Only skip compilation when all binaries have been imported. */
   return binary_stages == active_stages;
}

void
radv_graphics_shaders_compile(struct radv_device *device, struct vk_pipeline_cache *cache,
                              struct radv_shader_stage *stages, const struct radv_graphics_state_key *gfx_state,
                              bool keep_executable_info, bool keep_statistic_info, bool is_internal,
                              bool skip_shaders_cache, struct radv_retained_shaders *retained_shaders, bool noop_fs,
                              struct radv_shader **shaders, struct radv_shader_binary **binaries,
                              struct radv_shader **gs_copy_shader, struct radv_shader_binary **gs_copy_binary)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   const struct radv_instance *instance = radv_physical_device_instance(pdev);
   const bool nir_cache = instance->perftest_flags & RADV_PERFTEST_NIR_CACHE;
   for (unsigned s = 0; s < MESA_VULKAN_SHADER_STAGES; s++) {
      if (stages[s].stage == MESA_SHADER_NONE)
         continue;

      int64_t stage_start = os_time_get_nano();

      /* NIR might already have been imported from a library. */
      if (!stages[s].nir) {
         struct radv_spirv_to_nir_options options = {
            .lower_view_index_to_zero = !gfx_state->has_multiview_view_index,
            .lower_view_index_to_device_index = stages[s].key.view_index_from_device_index,
         };
         blake3_hash key;

         if (nir_cache) {
            radv_hash_graphics_spirv_to_nir(key, &stages[s], &options);
            stages[s].nir = radv_pipeline_cache_lookup_nir(device, cache, s, key);
         }
         if (!stages[s].nir) {
            stages[s].nir = radv_shader_spirv_to_nir(device, &stages[s], &options, is_internal);
            if (nir_cache)
               radv_pipeline_cache_insert_nir(device, cache, key, stages[s].nir);
         }
      }

      stages[s].feedback.duration += os_time_get_nano() - stage_start;
   }

   if (retained_shaders) {
      radv_pipeline_retain_shaders(retained_shaders, stages);
   }

   VkShaderStageFlagBits active_nir_stages = 0;
   for (int i = 0; i < MESA_VULKAN_SHADER_STAGES; i++) {
      if (stages[i].nir)
         active_nir_stages |= mesa_to_vk_shader_stage(i);
   }

   if (!pdev->mesh_fast_launch_2 && stages[MESA_SHADER_MESH].nir &&
       BITSET_TEST(stages[MESA_SHADER_MESH].nir->info.system_values_read, SYSTEM_VALUE_WORKGROUP_ID)) {
      nir_shader *mesh = stages[MESA_SHADER_MESH].nir;
      nir_shader *task = stages[MESA_SHADER_TASK].nir;

      /* Mesh shaders only have a 1D "vertex index" which we use
       * as "workgroup index" to emulate the 3D workgroup ID.
       */
      nir_lower_compute_system_values_options o = {
         .lower_workgroup_id_to_index = true,
         .shortcut_1d_workgroup_id = true,
         .num_workgroups[0] = task ? task->info.mesh.ts_mesh_dispatch_dimensions[0] : 0,
         .num_workgroups[1] = task ? task->info.mesh.ts_mesh_dispatch_dimensions[1] : 0,
         .num_workgroups[2] = task ? task->info.mesh.ts_mesh_dispatch_dimensions[2] : 0,
      };

      NIR_PASS(_, mesh, nir_lower_compute_system_values, &o);
   }

   radv_foreach_stage(i, active_nir_stages)
   {
      gl_shader_stage next_stage;

      if (stages[i].next_stage != MESA_SHADER_NONE) {
         next_stage = stages[i].next_stage;
      } else {
         next_stage = radv_get_next_stage(i, active_nir_stages);
      }

      radv_nir_shader_info_init(i, next_stage, &stages[i].info);
   }

   /* Determine if shaders uses NGG before linking because it's needed for some NIR pass. */
   radv_fill_shader_info_ngg(device, stages, active_nir_stages);

   if (stages[MESA_SHADER_GEOMETRY].nir) {
      unsigned nir_gs_flags = nir_lower_gs_intrinsics_per_stream;

      if (stages[MESA_SHADER_GEOMETRY].info.is_ngg) {
         nir_gs_flags |= nir_lower_gs_intrinsics_count_primitives |
                         nir_lower_gs_intrinsics_count_vertices_per_primitive |
                         nir_lower_gs_intrinsics_overwrite_incomplete;
      }

      NIR_PASS(_, stages[MESA_SHADER_GEOMETRY].nir, nir_lower_gs_intrinsics, nir_gs_flags);
   }

   /* Remove all varyings when the fragment shader is a noop. */
   if (noop_fs) {
      radv_foreach_stage(i, active_nir_stages)
      {
         if (radv_is_last_vgt_stage(&stages[i])) {
            radv_remove_varyings(stages[i].nir);
            break;
         }
      }
   }

   radv_graphics_shaders_link(device, gfx_state, stages);

   if (stages[MESA_SHADER_FRAGMENT].nir) {
      unsigned rast_prim = radv_get_rasterization_prim(stages, gfx_state);

      NIR_PASS(_, stages[MESA_SHADER_FRAGMENT].nir, radv_nir_lower_fs_barycentric, gfx_state, rast_prim);

      NIR_PASS(_, stages[MESA_SHADER_FRAGMENT].nir, nir_lower_fragcoord_wtrans);

      /* frag_depth = gl_FragCoord.z broadcasts to all samples of the fragment shader invocation,
       * so only optimize it away if we know there is only one sample per invocation.
       * Because we don't know if sample shading is used with factor 1.0f, this means
       * we only optimize single sampled shaders.
       */
      if ((gfx_state->lib_flags & VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_OUTPUT_INTERFACE_BIT_EXT) &&
          !gfx_state->dynamic_rasterization_samples && gfx_state->ms.rasterization_samples == 0)
         NIR_PASS(_, stages[MESA_SHADER_FRAGMENT].nir, nir_opt_fragdepth);
   }

   if (stages[MESA_SHADER_VERTEX].nir && !gfx_state->vs.has_prolog)
      NIR_PASS(_, stages[MESA_SHADER_VERTEX].nir, radv_nir_optimize_vs_inputs_to_const, gfx_state);

   radv_foreach_stage(i, active_nir_stages)
   {
      int64_t stage_start = os_time_get_nano();

      radv_optimize_nir(stages[i].nir, stages[i].key.optimisations_disabled);

      /* Gather info again, information such as outputs_read can be out-of-date. */
      nir_shader_gather_info(stages[i].nir, nir_shader_get_entrypoint(stages[i].nir));
      radv_nir_lower_io(device, stages[i].nir);

      stages[i].feedback.duration += os_time_get_nano() - stage_start;
   }

   if (stages[MESA_SHADER_FRAGMENT].nir) {
      bool update_info = false;
      if (gfx_state->dynamic_line_rast_mode)
         NIR_PASS(update_info, stages[MESA_SHADER_FRAGMENT].nir, nir_lower_poly_line_smooth,
                  RADV_NUM_SMOOTH_AA_SAMPLES);

      if (!gfx_state->ps.has_epilog)
         radv_nir_remap_color_attachment(stages[MESA_SHADER_FRAGMENT].nir, gfx_state);

      NIR_PASS(update_info, stages[MESA_SHADER_FRAGMENT].nir, nir_opt_frag_coord_to_pixel_coord);
      if (update_info)
         nir_shader_gather_info(stages[MESA_SHADER_FRAGMENT].nir,
                                nir_shader_get_entrypoint(stages[MESA_SHADER_FRAGMENT].nir));
   }

   /* Optimize varyings on lowered shader I/O (more efficient than optimizing I/O derefs). */
   radv_graphics_shaders_link_varyings(stages);

   /* Optimize constant clip/cull distance after linking to operate on scalar io in the last
    * pre raster stage.
    */
   radv_foreach_stage(i, active_nir_stages & (VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT))
   {
      if (stages[i].key.optimisations_disabled)
         continue;

      int64_t stage_start = os_time_get_nano();

      NIR_PASS(_, stages[i].nir, nir_opt_clip_cull_const);

      stages[i].feedback.duration += os_time_get_nano() - stage_start;
   }

   radv_fill_shader_info(device, RADV_PIPELINE_GRAPHICS, gfx_state, stages, active_nir_stages);

   radv_declare_pipeline_args(device, stages, gfx_state, active_nir_stages);

   radv_foreach_stage(i, active_nir_stages)
   {
      int64_t stage_start = os_time_get_nano();

      radv_postprocess_nir(device, gfx_state, &stages[i]);

      stages[i].feedback.duration += os_time_get_nano() - stage_start;
   }

   /* Compile NIR shaders to AMD assembly. */
   radv_graphics_shaders_nir_to_asm(device, cache, stages, gfx_state, keep_executable_info, keep_statistic_info,
                                    skip_shaders_cache, active_nir_stages, shaders, binaries, gs_copy_shader,
                                    gs_copy_binary);

   if (keep_executable_info) {
      for (int i = 0; i < MESA_VULKAN_SHADER_STAGES; ++i) {
         struct radv_shader *shader = shaders[i];
         if (!shader)
            continue;

         if (!stages[i].spirv.size)
            continue;

         shader->spirv = malloc(stages[i].spirv.size);
         memcpy(shader->spirv, stages[i].spirv.data, stages[i].spirv.size);
         shader->spirv_size = stages[i].spirv.size;
      }
   }
}

static bool
radv_should_compute_pipeline_hash(const struct radv_device *device, const enum radv_pipeline_type pipeline_type,
                                  bool fast_linking_enabled)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   const struct radv_instance *instance = radv_physical_device_instance(pdev);

   /* Skip computing the pipeline hash when GPL fast-linking is enabled because these shaders aren't
    * supposed to be cached and computing the hash is costly. Though, make sure it's always computed
    * when RGP is enabled, otherwise ISA isn't reported.
    */
   return !fast_linking_enabled ||
          ((instance->vk.trace_mode & RADV_TRACE_MODE_RGP) && pipeline_type == RADV_PIPELINE_GRAPHICS);
}

void
radv_graphics_pipeline_state_finish(struct radv_device *device, struct radv_graphics_pipeline_state *gfx_state)
{
   radv_pipeline_layout_finish(device, &gfx_state->layout);
   vk_free(&device->vk.alloc, gfx_state->vk_data);

   if (gfx_state->stages) {
      for (uint32_t i = 0; i < MESA_VULKAN_SHADER_STAGES; i++)
         ralloc_free(gfx_state->stages[i].nir);
      free(gfx_state->stages);
   }
}

VkResult
radv_generate_graphics_pipeline_state(struct radv_device *device, const VkGraphicsPipelineCreateInfo *pCreateInfo,
                                      struct radv_graphics_pipeline_state *gfx_state)
{
   VK_FROM_HANDLE(radv_pipeline_layout, pipeline_layout, pCreateInfo->layout);
   const VkPipelineCreateFlags2 create_flags = vk_graphics_pipeline_create_flags(pCreateInfo);
   const bool fast_linking_enabled = radv_is_fast_linking_enabled(pCreateInfo);
   enum radv_pipeline_type pipeline_type = RADV_PIPELINE_GRAPHICS;
   VkResult result;

   memset(gfx_state, 0, sizeof(*gfx_state));

   VkGraphicsPipelineLibraryFlagBitsEXT needed_lib_flags = ALL_GRAPHICS_LIB_FLAGS;
   if (create_flags & VK_PIPELINE_CREATE_2_LIBRARY_BIT_KHR) {
      const VkGraphicsPipelineLibraryCreateInfoEXT *lib_info =
         vk_find_struct_const(pCreateInfo->pNext, GRAPHICS_PIPELINE_LIBRARY_CREATE_INFO_EXT);
      needed_lib_flags = lib_info ? lib_info->flags : 0;
      pipeline_type = RADV_PIPELINE_GRAPHICS_LIB;
   }

   radv_pipeline_layout_init(device, &gfx_state->layout, false);

   /* If we have libraries, import them first. */
   const VkPipelineLibraryCreateInfoKHR *libs_info =
      vk_find_struct_const(pCreateInfo->pNext, PIPELINE_LIBRARY_CREATE_INFO_KHR);
   if (libs_info) {
      for (uint32_t i = 0; i < libs_info->libraryCount; i++) {
         VK_FROM_HANDLE(radv_pipeline, pipeline_lib, libs_info->pLibraries[i]);
         const struct radv_graphics_lib_pipeline *gfx_pipeline_lib = radv_pipeline_to_graphics_lib(pipeline_lib);

         vk_graphics_pipeline_state_merge(&gfx_state->vk, &gfx_pipeline_lib->graphics_state);

         radv_graphics_pipeline_import_layout(&gfx_state->layout, &gfx_pipeline_lib->layout);

         needed_lib_flags &= ~gfx_pipeline_lib->lib_flags;
      }
   }

   result = vk_graphics_pipeline_state_fill(&device->vk, &gfx_state->vk, pCreateInfo, NULL, 0, NULL, NULL,
                                            VK_SYSTEM_ALLOCATION_SCOPE_OBJECT, &gfx_state->vk_data);
   if (result != VK_SUCCESS)
      goto fail;

   if (pipeline_layout)
      radv_graphics_pipeline_import_layout(&gfx_state->layout, pipeline_layout);

   if (radv_should_compute_pipeline_hash(device, pipeline_type, fast_linking_enabled))
      radv_pipeline_layout_hash(&gfx_state->layout);

   gfx_state->compilation_required = !radv_skip_graphics_pipeline_compile(device, pCreateInfo);
   if (gfx_state->compilation_required) {
      gfx_state->key = radv_generate_graphics_pipeline_key(device, pCreateInfo, &gfx_state->vk, needed_lib_flags);

      gfx_state->stages = malloc(sizeof(struct radv_shader_stage) * MESA_VULKAN_SHADER_STAGES);
      if (!gfx_state->stages) {
         result = VK_ERROR_OUT_OF_HOST_MEMORY;
         goto fail;
      }

      for (unsigned i = 0; i < MESA_VULKAN_SHADER_STAGES; i++) {
         gfx_state->stages[i].stage = MESA_SHADER_NONE;
         gfx_state->stages[i].nir = NULL;
         gfx_state->stages[i].spirv.size = 0;
         gfx_state->stages[i].next_stage = MESA_SHADER_NONE;
      }

      for (uint32_t i = 0; i < pCreateInfo->stageCount; i++) {
         const VkPipelineShaderStageCreateInfo *sinfo = &pCreateInfo->pStages[i];
         gl_shader_stage stage = vk_to_mesa_shader_stage(sinfo->stage);

         radv_pipeline_stage_init(create_flags, sinfo, &gfx_state->layout, &gfx_state->key.stage_info[stage],
                                  &gfx_state->stages[stage]);
      }

      radv_pipeline_load_retained_shaders(device, pCreateInfo, gfx_state->stages);
   }

   return VK_SUCCESS;

fail:
   radv_graphics_pipeline_state_finish(device, gfx_state);
   return result;
}

void
radv_graphics_pipeline_hash(const struct radv_device *device, const struct radv_graphics_pipeline_state *gfx_state,
                            unsigned char *hash)
{
   struct mesa_sha1 ctx;

   _mesa_sha1_init(&ctx);
   radv_pipeline_hash(device, &gfx_state->layout, &ctx);

   _mesa_sha1_update(&ctx, &gfx_state->key.gfx_state, sizeof(gfx_state->key.gfx_state));

   for (unsigned s = 0; s < MESA_VULKAN_SHADER_STAGES; s++) {
      const struct radv_shader_stage *stage = &gfx_state->stages[s];

      if (stage->stage == MESA_SHADER_NONE)
         continue;

      _mesa_sha1_update(&ctx, stage->shader_sha1, sizeof(stage->shader_sha1));
      _mesa_sha1_update(&ctx, &stage->key, sizeof(stage->key));
   }

   _mesa_sha1_final(&ctx, hash);
}

static VkResult
radv_graphics_pipeline_compile(struct radv_graphics_pipeline *pipeline, const VkGraphicsPipelineCreateInfo *pCreateInfo,
                               const struct radv_graphics_pipeline_state *gfx_state, struct radv_device *device,
                               struct vk_pipeline_cache *cache, bool fast_linking_enabled)
{
   struct radv_shader_binary *binaries[MESA_VULKAN_SHADER_STAGES] = {NULL};
   struct radv_shader_binary *gs_copy_binary = NULL;
   bool keep_executable_info = radv_pipeline_capture_shaders(device, pipeline->base.create_flags);
   bool keep_statistic_info = radv_pipeline_capture_shader_stats(device, pipeline->base.create_flags);
   bool skip_shaders_cache = radv_pipeline_skip_shaders_cache(device, &pipeline->base);
   struct radv_shader_stage *stages = gfx_state->stages;
   const VkPipelineCreationFeedbackCreateInfo *creation_feedback =
      vk_find_struct_const(pCreateInfo->pNext, PIPELINE_CREATION_FEEDBACK_CREATE_INFO);
   VkPipelineCreationFeedback pipeline_feedback = {
      .flags = VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT,
   };
   VkResult result = VK_SUCCESS;
   const bool retain_shaders =
      !!(pipeline->base.create_flags & VK_PIPELINE_CREATE_2_RETAIN_LINK_TIME_OPTIMIZATION_INFO_BIT_EXT);
   struct radv_retained_shaders *retained_shaders = NULL;

   int64_t pipeline_start = os_time_get_nano();

   if (radv_should_compute_pipeline_hash(device, pipeline->base.type, fast_linking_enabled)) {
      radv_graphics_pipeline_hash(device, gfx_state, pipeline->base.sha1);

      pipeline->base.pipeline_hash = *(uint64_t *)pipeline->base.sha1;
   }

   /* Skip the shaders cache when any of the below are true:
    * - fast-linking is enabled because it's useless to cache unoptimized pipelines
    * - graphics pipeline libraries are created with the RETAIN_LINK_TIME_OPTIMIZATION flag and
    *   module identifiers are used (ie. no SPIR-V provided).
    */
   if (fast_linking_enabled) {
      skip_shaders_cache = true;
   } else if (retain_shaders) {
      assert(pipeline->base.create_flags & VK_PIPELINE_CREATE_2_LIBRARY_BIT_KHR);
      for (uint32_t i = 0; i < MESA_VULKAN_SHADER_STAGES; i++) {
         if (stages[i].stage != MESA_SHADER_NONE && !stages[i].spirv.size) {
            skip_shaders_cache = true;
            break;
         }
      }
   }

   bool found_in_application_cache = true;
   if (!skip_shaders_cache &&
       radv_graphics_pipeline_cache_search(device, cache, pipeline, &found_in_application_cache)) {
      if (found_in_application_cache)
         pipeline_feedback.flags |= VK_PIPELINE_CREATION_FEEDBACK_APPLICATION_PIPELINE_CACHE_HIT_BIT;

      if (retain_shaders) {
         /* For graphics pipeline libraries created with the RETAIN_LINK_TIME_OPTIMIZATION flag, we
          * need to retain the stage info because we can't know if the LTO pipelines will
          * be find in the shaders cache.
          */
         struct radv_graphics_lib_pipeline *gfx_pipeline_lib = radv_pipeline_to_graphics_lib(&pipeline->base);

         gfx_pipeline_lib->stages = radv_copy_shader_stage_create_info(device, pCreateInfo->stageCount,
                                                                       pCreateInfo->pStages, gfx_pipeline_lib->mem_ctx);
         if (!gfx_pipeline_lib->stages)
            return VK_ERROR_OUT_OF_HOST_MEMORY;

         gfx_pipeline_lib->stage_count = pCreateInfo->stageCount;

         for (unsigned i = 0; i < pCreateInfo->stageCount; i++) {
            gl_shader_stage s = vk_to_mesa_shader_stage(pCreateInfo->pStages[i].stage);
            gfx_pipeline_lib->stage_keys[s] = gfx_state->key.stage_info[s];
         }
      }

      result = VK_SUCCESS;
      goto done;
   }

   if (pipeline->base.create_flags & VK_PIPELINE_CREATE_2_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT)
      return VK_PIPELINE_COMPILE_REQUIRED;

   if (retain_shaders) {
      struct radv_graphics_lib_pipeline *gfx_pipeline_lib = radv_pipeline_to_graphics_lib(&pipeline->base);
      retained_shaders = &gfx_pipeline_lib->retained_shaders;
   }

   const bool noop_fs = radv_pipeline_needs_noop_fs(pipeline, &gfx_state->key.gfx_state);

   radv_graphics_shaders_compile(device, cache, stages, &gfx_state->key.gfx_state, keep_executable_info,
                                 keep_statistic_info, pipeline->base.is_internal, skip_shaders_cache, retained_shaders,
                                 noop_fs, pipeline->base.shaders, binaries, &pipeline->base.gs_copy_shader,
                                 &gs_copy_binary);

   if (!skip_shaders_cache) {
      radv_pipeline_cache_insert(device, cache, &pipeline->base);
   }

   free(gs_copy_binary);
   for (int i = 0; i < MESA_VULKAN_SHADER_STAGES; ++i) {
      free(binaries[i]);
      if (stages[i].nir) {
         if (radv_can_dump_shader_stats(device, stages[i].nir) && pipeline->base.shaders[i]) {
            radv_dump_shader_stats(device, &pipeline->base, pipeline->base.shaders[i], i, stderr);
         }
      }
   }

done:
   pipeline_feedback.duration = os_time_get_nano() - pipeline_start;

   if (creation_feedback) {
      *creation_feedback->pPipelineCreationFeedback = pipeline_feedback;

      if (creation_feedback->pipelineStageCreationFeedbackCount > 0) {
         uint32_t num_feedbacks = 0;

         for (uint32_t i = 0; i < pCreateInfo->stageCount; i++) {
            gl_shader_stage s = vk_to_mesa_shader_stage(pCreateInfo->pStages[i].stage);
            creation_feedback->pPipelineStageCreationFeedbacks[num_feedbacks++] = stages[s].feedback;
         }

         /* Stages imported from graphics pipeline libraries are defined as additional entries in the
          * order they were imported.
          */
         const VkPipelineLibraryCreateInfoKHR *libs_info =
            vk_find_struct_const(pCreateInfo->pNext, PIPELINE_LIBRARY_CREATE_INFO_KHR);
         if (libs_info) {
            for (uint32_t i = 0; i < libs_info->libraryCount; i++) {
               VK_FROM_HANDLE(radv_pipeline, pipeline_lib, libs_info->pLibraries[i]);
               struct radv_graphics_lib_pipeline *gfx_pipeline_lib = radv_pipeline_to_graphics_lib(pipeline_lib);

               if (!gfx_pipeline_lib->base.active_stages)
                  continue;

               radv_foreach_stage(s, gfx_pipeline_lib->base.active_stages)
               {
                  creation_feedback->pPipelineStageCreationFeedbacks[num_feedbacks++] = stages[s].feedback;
               }
            }
         }

         assert(num_feedbacks == creation_feedback->pipelineStageCreationFeedbackCount);
      }
   }

   return result;
}

struct radv_vgt_shader_key
radv_get_vgt_shader_key(const struct radv_device *device, struct radv_shader **shaders,
                        const struct radv_shader *gs_copy_shader)
{
   uint8_t hs_size = 64, gs_size = 64, vs_size = 64;
   struct radv_shader *last_vgt_shader = NULL;
   struct radv_vgt_shader_key key;

   memset(&key, 0, sizeof(key));

   if (shaders[MESA_SHADER_GEOMETRY]) {
      last_vgt_shader = shaders[MESA_SHADER_GEOMETRY];
   } else if (shaders[MESA_SHADER_TESS_EVAL]) {
      last_vgt_shader = shaders[MESA_SHADER_TESS_EVAL];
   } else if (shaders[MESA_SHADER_VERTEX]) {
      last_vgt_shader = shaders[MESA_SHADER_VERTEX];
   } else {
      assert(shaders[MESA_SHADER_MESH]);
      last_vgt_shader = shaders[MESA_SHADER_MESH];
   }

   vs_size = gs_size = last_vgt_shader->info.wave_size;
   if (gs_copy_shader)
      vs_size = gs_copy_shader->info.wave_size;

   if (shaders[MESA_SHADER_TESS_CTRL])
      hs_size = shaders[MESA_SHADER_TESS_CTRL]->info.wave_size;

   key.tess = !!shaders[MESA_SHADER_TESS_CTRL];
   key.gs = !!shaders[MESA_SHADER_GEOMETRY];
   if (last_vgt_shader->info.is_ngg) {
      key.ngg = 1;
      key.ngg_passthrough = last_vgt_shader->info.is_ngg_passthrough;
      key.ngg_streamout = last_vgt_shader->info.so.num_outputs > 0;
   }
   if (shaders[MESA_SHADER_MESH]) {
      key.mesh = 1;
      key.mesh_scratch_ring = shaders[MESA_SHADER_MESH]->info.ms.needs_ms_scratch_ring;
   }

   key.hs_wave32 = hs_size == 32;
   key.vs_wave32 = vs_size == 32;
   key.gs_wave32 = gs_size == 32;

   return key;
}

static bool
gfx103_pipeline_vrs_coarse_shading(const struct radv_device *device, const struct radv_graphics_pipeline *pipeline)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   const struct radv_instance *instance = radv_physical_device_instance(pdev);
   struct radv_shader *ps = pipeline->base.shaders[MESA_SHADER_FRAGMENT];

   if (pdev->info.gfx_level != GFX10_3)
      return false;

   if (instance->debug_flags & RADV_DEBUG_NO_VRS_FLAT_SHADING)
      return false;

   if (ps && !ps->info.ps.allow_flat_shading)
      return false;

   return true;
}

static void
radv_pipeline_init_vertex_input_state(const struct radv_device *device, struct radv_graphics_pipeline *pipeline,
                                      const struct vk_graphics_pipeline_state *state)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   const struct radv_shader *vs = radv_get_shader(pipeline->base.shaders, MESA_SHADER_VERTEX);

   if (!state->vi)
      return;

   u_foreach_bit (i, state->vi->bindings_valid) {
      pipeline->binding_stride[i] = state->vi->bindings[i].stride;
   }

   if (vs->info.vs.use_per_attribute_vb_descs) {
      const enum amd_gfx_level gfx_level = pdev->info.gfx_level;
      const enum radeon_family family = pdev->info.family;
      const struct ac_vtx_format_info *vtx_info_table = ac_get_vtx_format_info_table(gfx_level, family);

      pipeline->vertex_input.bindings_match_attrib = true;

      u_foreach_bit (i, state->vi->attributes_valid) {
         uint32_t binding = state->vi->attributes[i].binding;
         uint32_t offset = state->vi->attributes[i].offset;

         pipeline->vertex_input.attribute_mask |= BITFIELD_BIT(i);
         pipeline->vertex_input.bindings[i] = binding;
         pipeline->vertex_input.bindings_match_attrib &= binding == i;

         if (state->vi->bindings[binding].stride) {
            pipeline->vertex_input.attrib_index_offset[i] = offset / state->vi->bindings[binding].stride;
         }

         if (state->vi->bindings[binding].input_rate) {
            pipeline->vertex_input.instance_rate_inputs |= BITFIELD_BIT(i);
            pipeline->vertex_input.divisors[i] = state->vi->bindings[binding].divisor;

            if (state->vi->bindings[binding].divisor == 0) {
               pipeline->vertex_input.zero_divisors |= BITFIELD_BIT(i);
            } else if (state->vi->bindings[binding].divisor > 1) {
               pipeline->vertex_input.nontrivial_divisors |= BITFIELD_BIT(i);
            }
         }

         pipeline->vertex_input.offsets[i] = offset;

         enum pipe_format format = radv_format_to_pipe_format(state->vi->attributes[i].format);
         const struct ac_vtx_format_info *vtx_info = &vtx_info_table[format];

         pipeline->vertex_input.formats[i] = format;
         uint8_t format_align_req_minus_1 = vtx_info->chan_byte_size >= 4 ? 3 : (vtx_info->element_size - 1);
         pipeline->vertex_input.format_align_req_minus_1[i] = format_align_req_minus_1;
         uint8_t component_align_req_minus_1 =
            MIN2(vtx_info->chan_byte_size ? vtx_info->chan_byte_size : vtx_info->element_size, 4) - 1;
         pipeline->vertex_input.component_align_req_minus_1[i] = component_align_req_minus_1;
         pipeline->vertex_input.format_sizes[i] = vtx_info->element_size;
         pipeline->vertex_input.alpha_adjust_lo |= (vtx_info->alpha_adjust & 0x1) << i;
         pipeline->vertex_input.alpha_adjust_hi |= (vtx_info->alpha_adjust >> 1) << i;
         if (G_008F0C_DST_SEL_X(vtx_info->dst_sel) == V_008F0C_SQ_SEL_Z) {
            pipeline->vertex_input.post_shuffle |= BITFIELD_BIT(i);
         }

         if (!(vtx_info->has_hw_format & BITFIELD_BIT(vtx_info->num_channels - 1))) {
            pipeline->vertex_input.nontrivial_formats |= BITFIELD_BIT(i);
         }
      }
   } else {
      u_foreach_bit (i, vs->info.vs.vb_desc_usage_mask) {
         pipeline->vertex_input.bindings[i] = i;
      }
   }
}

static void
radv_pipeline_init_shader_stages_state(const struct radv_device *device, struct radv_graphics_pipeline *pipeline)
{
   for (unsigned i = 0; i < MESA_VULKAN_SHADER_STAGES; i++) {
      bool shader_exists = !!pipeline->base.shaders[i];
      if (shader_exists || i < MESA_SHADER_COMPUTE) {
         if (shader_exists)
            pipeline->base.need_indirect_descriptor_sets |=
               radv_shader_need_indirect_descriptor_sets(pipeline->base.shaders[i]);
      }
   }

   gl_shader_stage first_stage =
      radv_pipeline_has_stage(pipeline, MESA_SHADER_MESH) ? MESA_SHADER_MESH : MESA_SHADER_VERTEX;

   const struct radv_shader *shader = radv_get_shader(pipeline->base.shaders, first_stage);
   const struct radv_userdata_info *loc = radv_get_user_sgpr_info(shader, AC_UD_VS_BASE_VERTEX_START_INSTANCE);

   if (loc->sgpr_idx != -1) {
      pipeline->vtx_base_sgpr = shader->info.user_data_0;
      pipeline->vtx_base_sgpr += loc->sgpr_idx * 4;
      pipeline->vtx_emit_num = loc->num_sgprs;
      pipeline->uses_drawid = radv_get_shader(pipeline->base.shaders, first_stage)->info.vs.needs_draw_id;
      pipeline->uses_baseinstance = radv_get_shader(pipeline->base.shaders, first_stage)->info.vs.needs_base_instance;

      assert(first_stage != MESA_SHADER_MESH || !pipeline->uses_baseinstance);
   }
}

uint32_t
radv_get_vgt_gs_out(struct radv_shader **shaders, uint32_t primitive_topology, bool is_ngg)
{
   uint32_t gs_out;

   if (shaders[MESA_SHADER_GEOMETRY]) {
      gs_out = radv_conv_gl_prim_to_gs_out(shaders[MESA_SHADER_GEOMETRY]->info.gs.output_prim);
   } else if (shaders[MESA_SHADER_TESS_CTRL]) {
      if (shaders[MESA_SHADER_TESS_EVAL]->info.tes.point_mode) {
         gs_out = V_028A6C_POINTLIST;
      } else {
         gs_out = radv_conv_tess_prim_to_gs_out(shaders[MESA_SHADER_TESS_EVAL]->info.tes._primitive_mode);
      }
   } else if (shaders[MESA_SHADER_MESH]) {
      gs_out = radv_conv_gl_prim_to_gs_out(shaders[MESA_SHADER_MESH]->info.ms.output_prim);
   } else {
      gs_out = radv_conv_prim_to_gs_out(primitive_topology, is_ngg);
   }

   return gs_out;
}

static uint32_t
radv_pipeline_init_vgt_gs_out(struct radv_graphics_pipeline *pipeline, const struct vk_graphics_pipeline_state *state)
{
   const bool is_ngg = pipeline->base.shaders[pipeline->last_vgt_api_stage]->info.is_ngg;
   uint32_t primitive_topology = 0;

   if (pipeline->last_vgt_api_stage == MESA_SHADER_VERTEX)
      primitive_topology = radv_translate_prim(state->ia->primitive_topology);

   return radv_get_vgt_gs_out(pipeline->base.shaders, primitive_topology, is_ngg);
}

static void
radv_pipeline_init_extra(struct radv_graphics_pipeline *pipeline, const VkGraphicsPipelineCreateInfoRADV *radv_info,
                         const struct vk_graphics_pipeline_state *state)
{
   pipeline->custom_blend_mode = radv_info->custom_blend_mode;

   if (radv_pipeline_has_ds_attachments(state->rp)) {
      pipeline->db_render_control |= S_028000_DEPTH_CLEAR_ENABLE(radv_info->db_depth_clear);
      pipeline->db_render_control |= S_028000_STENCIL_CLEAR_ENABLE(radv_info->db_stencil_clear);
      pipeline->db_render_control |= S_028000_DEPTH_COMPRESS_DISABLE(radv_info->depth_compress_disable);
      pipeline->db_render_control |= S_028000_STENCIL_COMPRESS_DISABLE(radv_info->stencil_compress_disable);
   }
}

bool
radv_needs_null_export_workaround(const struct radv_device *device, const struct radv_shader *ps,
                                  unsigned custom_blend_mode)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   const enum amd_gfx_level gfx_level = pdev->info.gfx_level;

   if (!ps)
      return false;

   /* Ensure that some export memory is always allocated, for two reasons:
    *
    * 1) Correctness: The hardware ignores the EXEC mask if no export
    *    memory is allocated, so KILL and alpha test do not work correctly
    *    without this.
    * 2) Performance: Every shader needs at least a NULL export, even when
    *    it writes no color/depth output. The NULL export instruction
    *    stalls without this setting.
    *
    * Don't add this to CB_SHADER_MASK.
    *
    * GFX10 supports pixel shaders without exports by setting both the
    * color and Z formats to SPI_SHADER_ZERO. The hw will skip export
    * instructions if any are present.
    *
    * GFX11 requires one color output, otherwise the DCC decompression does nothing.
    *
    * Primitive Ordered Pixel Shading also requires an export, otherwise interlocking doesn't work
    * correctly before GFX11, and a hang happens on GFX11.
    */
   return (gfx_level <= GFX9 || ps->info.ps.can_discard || ps->info.ps.pops ||
           (custom_blend_mode == V_028808_CB_DCC_DECOMPRESS_GFX11 && gfx_level >= GFX11)) &&
          !ps->info.ps.writes_z && !ps->info.ps.writes_stencil && !ps->info.ps.writes_sample_mask;
}

static VkResult
radv_graphics_pipeline_import_binaries(struct radv_device *device, struct radv_graphics_pipeline *pipeline,
                                       const VkPipelineBinaryInfoKHR *binary_info)
{
   blake3_hash pipeline_hash;
   struct mesa_blake3 ctx;

   _mesa_blake3_init(&ctx);

   for (uint32_t i = 0; i < binary_info->binaryCount; i++) {
      VK_FROM_HANDLE(radv_pipeline_binary, pipeline_binary, binary_info->pPipelineBinaries[i]);
      struct radv_shader *shader;
      struct blob_reader blob;

      blob_reader_init(&blob, pipeline_binary->data, pipeline_binary->size);

      shader = radv_shader_deserialize(device, pipeline_binary->key, sizeof(pipeline_binary->key), &blob);
      if (!shader)
         return VK_ERROR_OUT_OF_DEVICE_MEMORY;

      if (shader->info.stage == MESA_SHADER_VERTEX && i > 0) {
         /* The GS copy-shader is a VS placed after all other stages. */
         pipeline->base.gs_copy_shader = shader;
      } else {
         pipeline->base.shaders[shader->info.stage] = shader;
      }

      _mesa_blake3_update(&ctx, pipeline_binary->key, sizeof(pipeline_binary->key));
   }

   _mesa_blake3_final(&ctx, pipeline_hash);

   pipeline->base.pipeline_hash = *(uint64_t *)pipeline_hash;

   pipeline->has_pipeline_binaries = true;

   return VK_SUCCESS;
}

static VkResult
radv_graphics_pipeline_init(struct radv_graphics_pipeline *pipeline, struct radv_device *device,
                            struct vk_pipeline_cache *cache, const VkGraphicsPipelineCreateInfo *pCreateInfo)
{
   bool fast_linking_enabled = radv_is_fast_linking_enabled(pCreateInfo);
   struct radv_graphics_pipeline_state gfx_state;
   VkResult result = VK_SUCCESS;

   pipeline->last_vgt_api_stage = MESA_SHADER_NONE;

   const VkPipelineLibraryCreateInfoKHR *libs_info =
      vk_find_struct_const(pCreateInfo->pNext, PIPELINE_LIBRARY_CREATE_INFO_KHR);

   /* If we have libraries, import them first. */
   if (libs_info) {
      for (uint32_t i = 0; i < libs_info->libraryCount; i++) {
         VK_FROM_HANDLE(radv_pipeline, pipeline_lib, libs_info->pLibraries[i]);
         struct radv_graphics_lib_pipeline *gfx_pipeline_lib = radv_pipeline_to_graphics_lib(pipeline_lib);

         assert(pipeline_lib->type == RADV_PIPELINE_GRAPHICS_LIB);

         radv_graphics_pipeline_import_lib(device, pipeline, gfx_pipeline_lib);
      }
   }

   radv_pipeline_import_graphics_info(device, pipeline, pCreateInfo);

   result = radv_generate_graphics_pipeline_state(device, pCreateInfo, &gfx_state);
   if (result != VK_SUCCESS)
      return result;

   const VkPipelineBinaryInfoKHR *binary_info = vk_find_struct_const(pCreateInfo->pNext, PIPELINE_BINARY_INFO_KHR);

   if (binary_info && binary_info->binaryCount > 0) {
      result = radv_graphics_pipeline_import_binaries(device, pipeline, binary_info);
   } else {
      if (gfx_state.compilation_required) {
         result =
            radv_graphics_pipeline_compile(pipeline, pCreateInfo, &gfx_state, device, cache, fast_linking_enabled);
      }
   }

   if (result != VK_SUCCESS) {
      radv_graphics_pipeline_state_finish(device, &gfx_state);
      return result;
   }

   uint32_t vgt_gs_out_prim_type = radv_pipeline_init_vgt_gs_out(pipeline, &gfx_state.vk);

   radv_pipeline_init_multisample_state(device, pipeline, pCreateInfo, &gfx_state.vk);

   if (!radv_pipeline_has_stage(pipeline, MESA_SHADER_MESH))
      radv_pipeline_init_input_assembly_state(device, pipeline);
   radv_pipeline_init_dynamic_state(device, pipeline, &gfx_state.vk, pCreateInfo);

   if (!radv_pipeline_has_stage(pipeline, MESA_SHADER_MESH))
      radv_pipeline_init_vertex_input_state(device, pipeline, &gfx_state.vk);

   radv_pipeline_init_shader_stages_state(device, pipeline);

   pipeline->is_ngg = pipeline->base.shaders[pipeline->last_vgt_api_stage]->info.is_ngg;
   pipeline->has_ngg_culling = pipeline->base.shaders[pipeline->last_vgt_api_stage]->info.has_ngg_culling;
   pipeline->force_vrs_per_vertex = pipeline->base.shaders[pipeline->last_vgt_api_stage]->info.force_vrs_per_vertex;
   pipeline->rast_prim = vgt_gs_out_prim_type;
   pipeline->uses_out_of_order_rast = gfx_state.vk.rs->rasterization_order_amd == VK_RASTERIZATION_ORDER_RELAXED_AMD;
   pipeline->uses_vrs = radv_is_vrs_enabled(&gfx_state.vk);
   pipeline->uses_vrs_attachment = radv_pipeline_uses_vrs_attachment(pipeline, &gfx_state.vk);
   pipeline->uses_vrs_coarse_shading = !pipeline->uses_vrs && gfx103_pipeline_vrs_coarse_shading(device, pipeline);

   pipeline->base.push_constant_size = gfx_state.layout.push_constant_size;
   pipeline->base.dynamic_offset_count = gfx_state.layout.dynamic_offset_count;

   const VkGraphicsPipelineCreateInfoRADV *radv_info =
      vk_find_struct_const(pCreateInfo->pNext, GRAPHICS_PIPELINE_CREATE_INFO_RADV);
   if (radv_info) {
      radv_pipeline_init_extra(pipeline, radv_info, &gfx_state.vk);
   }

   radv_graphics_pipeline_state_finish(device, &gfx_state);
   return result;
}

static VkResult
radv_graphics_pipeline_create(VkDevice _device, VkPipelineCache _cache, const VkGraphicsPipelineCreateInfo *pCreateInfo,
                              const VkAllocationCallbacks *pAllocator, VkPipeline *pPipeline)
{
   VK_FROM_HANDLE(radv_device, device, _device);
   VK_FROM_HANDLE(vk_pipeline_cache, cache, _cache);
   struct radv_graphics_pipeline *pipeline;
   VkResult result;

   pipeline = vk_zalloc2(&device->vk.alloc, pAllocator, sizeof(*pipeline), 8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (pipeline == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   radv_pipeline_init(device, &pipeline->base, RADV_PIPELINE_GRAPHICS);
   pipeline->base.create_flags = vk_graphics_pipeline_create_flags(pCreateInfo);
   pipeline->base.is_internal = _cache == device->meta_state.cache;

   result = radv_graphics_pipeline_init(pipeline, device, cache, pCreateInfo);
   if (result != VK_SUCCESS) {
      radv_pipeline_destroy(device, &pipeline->base, pAllocator);
      return result;
   }

   *pPipeline = radv_pipeline_to_handle(&pipeline->base);
   radv_rmv_log_graphics_pipeline_create(device, &pipeline->base, pipeline->base.is_internal);
   return VK_SUCCESS;
}

void
radv_destroy_graphics_pipeline(struct radv_device *device, struct radv_graphics_pipeline *pipeline)
{
   for (unsigned i = 0; i < MESA_VULKAN_SHADER_STAGES; ++i) {
      if (pipeline->base.shaders[i])
         radv_shader_unref(device, pipeline->base.shaders[i]);
   }

   if (pipeline->base.gs_copy_shader)
      radv_shader_unref(device, pipeline->base.gs_copy_shader);
}

static VkResult
radv_graphics_lib_pipeline_init(struct radv_graphics_lib_pipeline *pipeline, struct radv_device *device,
                                struct vk_pipeline_cache *cache, const VkGraphicsPipelineCreateInfo *pCreateInfo)
{
   VK_FROM_HANDLE(radv_pipeline_layout, pipeline_layout, pCreateInfo->layout);
   VkResult result;

   const VkGraphicsPipelineLibraryCreateInfoEXT *lib_info =
      vk_find_struct_const(pCreateInfo->pNext, GRAPHICS_PIPELINE_LIBRARY_CREATE_INFO_EXT);
   const VkPipelineLibraryCreateInfoKHR *libs_info =
      vk_find_struct_const(pCreateInfo->pNext, PIPELINE_LIBRARY_CREATE_INFO_KHR);
   bool fast_linking_enabled = radv_is_fast_linking_enabled(pCreateInfo);

   struct vk_graphics_pipeline_state *state = &pipeline->graphics_state;

   pipeline->base.last_vgt_api_stage = MESA_SHADER_NONE;
   pipeline->lib_flags = lib_info ? lib_info->flags : 0;

   radv_pipeline_layout_init(device, &pipeline->layout, false);

   /* If we have libraries, import them first. */
   if (libs_info) {
      for (uint32_t i = 0; i < libs_info->libraryCount; i++) {
         VK_FROM_HANDLE(radv_pipeline, pipeline_lib, libs_info->pLibraries[i]);
         struct radv_graphics_lib_pipeline *gfx_pipeline_lib = radv_pipeline_to_graphics_lib(pipeline_lib);

         vk_graphics_pipeline_state_merge(state, &gfx_pipeline_lib->graphics_state);

         radv_graphics_pipeline_import_layout(&pipeline->layout, &gfx_pipeline_lib->layout);

         radv_graphics_pipeline_import_lib(device, &pipeline->base, gfx_pipeline_lib);

         pipeline->lib_flags |= gfx_pipeline_lib->lib_flags;
      }
   }

   result = vk_graphics_pipeline_state_fill(&device->vk, state, pCreateInfo, NULL, 0, NULL, NULL,
                                            VK_SYSTEM_ALLOCATION_SCOPE_OBJECT, &pipeline->state_data);
   if (result != VK_SUCCESS)
      return result;

   radv_pipeline_import_graphics_info(device, &pipeline->base, pCreateInfo);

   if (pipeline_layout)
      radv_graphics_pipeline_import_layout(&pipeline->layout, pipeline_layout);

   const VkPipelineBinaryInfoKHR *binary_info = vk_find_struct_const(pCreateInfo->pNext, PIPELINE_BINARY_INFO_KHR);

   if (binary_info && binary_info->binaryCount > 0) {
      result = radv_graphics_pipeline_import_binaries(device, &pipeline->base, binary_info);
   } else {
      struct radv_graphics_pipeline_state gfx_state;

      result = radv_generate_graphics_pipeline_state(device, pCreateInfo, &gfx_state);
      if (result != VK_SUCCESS)
         return result;

      result =
         radv_graphics_pipeline_compile(&pipeline->base, pCreateInfo, &gfx_state, device, cache, fast_linking_enabled);

      radv_graphics_pipeline_state_finish(device, &gfx_state);
   }

   return result;
}

static VkResult
radv_graphics_lib_pipeline_create(VkDevice _device, VkPipelineCache _cache,
                                  const VkGraphicsPipelineCreateInfo *pCreateInfo,
                                  const VkAllocationCallbacks *pAllocator, VkPipeline *pPipeline)
{
   VK_FROM_HANDLE(vk_pipeline_cache, cache, _cache);
   VK_FROM_HANDLE(radv_device, device, _device);
   struct radv_graphics_lib_pipeline *pipeline;
   VkResult result;

   pipeline = vk_zalloc2(&device->vk.alloc, pAllocator, sizeof(*pipeline), 8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (pipeline == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   radv_pipeline_init(device, &pipeline->base.base, RADV_PIPELINE_GRAPHICS_LIB);
   pipeline->base.base.create_flags = vk_graphics_pipeline_create_flags(pCreateInfo);

   pipeline->mem_ctx = ralloc_context(NULL);

   result = radv_graphics_lib_pipeline_init(pipeline, device, cache, pCreateInfo);
   if (result != VK_SUCCESS) {
      radv_pipeline_destroy(device, &pipeline->base.base, pAllocator);
      return result;
   }

   *pPipeline = radv_pipeline_to_handle(&pipeline->base.base);

   return VK_SUCCESS;
}

void
radv_destroy_graphics_lib_pipeline(struct radv_device *device, struct radv_graphics_lib_pipeline *pipeline)
{
   struct radv_retained_shaders *retained_shaders = &pipeline->retained_shaders;

   radv_pipeline_layout_finish(device, &pipeline->layout);

   vk_free(&device->vk.alloc, pipeline->state_data);

   for (unsigned i = 0; i < MESA_VULKAN_SHADER_STAGES; ++i) {
      free(retained_shaders->stages[i].serialized_nir);
   }

   ralloc_free(pipeline->mem_ctx);

   radv_destroy_graphics_pipeline(device, &pipeline->base);
}

VKAPI_ATTR VkResult VKAPI_CALL
radv_CreateGraphicsPipelines(VkDevice _device, VkPipelineCache pipelineCache, uint32_t count,
                             const VkGraphicsPipelineCreateInfo *pCreateInfos, const VkAllocationCallbacks *pAllocator,
                             VkPipeline *pPipelines)
{
   VkResult result = VK_SUCCESS;
   unsigned i = 0;

   for (; i < count; i++) {
      const VkPipelineCreateFlagBits2 create_flags = vk_graphics_pipeline_create_flags(&pCreateInfos[i]);
      VkResult r;
      if (create_flags & VK_PIPELINE_CREATE_2_LIBRARY_BIT_KHR) {
         r = radv_graphics_lib_pipeline_create(_device, pipelineCache, &pCreateInfos[i], pAllocator, &pPipelines[i]);
      } else {
         r = radv_graphics_pipeline_create(_device, pipelineCache, &pCreateInfos[i], pAllocator, &pPipelines[i]);
      }
      if (r != VK_SUCCESS) {
         result = r;
         pPipelines[i] = VK_NULL_HANDLE;

         if (create_flags & VK_PIPELINE_CREATE_2_EARLY_RETURN_ON_FAILURE_BIT)
            break;
      }
   }

   for (; i < count; ++i)
      pPipelines[i] = VK_NULL_HANDLE;

   return result;
}
