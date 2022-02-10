/*
 * Copyright © 2021 Collabora Ltd.
 *
 * Derived from tu_pipeline.c which is:
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
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

#include "panvk_cs.h"
#include "panvk_private.h"

#include "pan_bo.h"

#include "nir/nir.h"
#include "nir/nir_builder.h"
#include "spirv/nir_spirv.h"
#include "util/debug.h"
#include "util/mesa-sha1.h"
#include "util/u_atomic.h"
#include "vk_format.h"
#include "vk_util.h"

#include "panfrost/util/pan_lower_framebuffer.h"


struct panvk_pipeline_builder
{
   struct panvk_device *device;
   struct panvk_pipeline_cache *cache;
   const VkAllocationCallbacks *alloc;
   const VkGraphicsPipelineCreateInfo *create_info;
   const struct panvk_pipeline_layout *layout;

   struct panvk_shader *shaders[MESA_SHADER_STAGES];
   struct {
      uint32_t shader_offset;
      uint32_t rsd_offset;
      uint32_t sysvals_offset;
   } stages[MESA_SHADER_STAGES];
   uint32_t blend_shader_offsets[MAX_RTS];
   uint32_t shader_total_size;
   uint32_t static_state_size;
   uint32_t vpd_offset;

   bool rasterizer_discard;
   /* these states are affectd by rasterizer_discard */
   VkSampleCountFlagBits samples;
   bool use_depth_stencil_attachment;
   uint8_t active_color_attachments;
   enum pipe_format color_attachment_formats[MAX_RTS];
};

static VkResult
panvk_pipeline_builder_create_pipeline(struct panvk_pipeline_builder *builder,
                                       struct panvk_pipeline **out_pipeline)
{
   struct panvk_device *dev = builder->device;

   struct panvk_pipeline *pipeline =
      vk_object_zalloc(&dev->vk, builder->alloc,
                       sizeof(*pipeline), VK_OBJECT_TYPE_PIPELINE);
   if (!pipeline)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   pipeline->layout = builder->layout;
   *out_pipeline = pipeline;
   return VK_SUCCESS;
}

static void
panvk_pipeline_builder_finish(struct panvk_pipeline_builder *builder)
{
   for (uint32_t i = 0; i < MESA_SHADER_STAGES; i++) {
      if (!builder->shaders[i])
         continue;
      panvk_shader_destroy(builder->device, builder->shaders[i], builder->alloc);
   }
}

static bool
panvk_pipeline_static_state(struct panvk_pipeline *pipeline, uint32_t id)
{
   return !(pipeline->dynamic_state_mask & (1 << id));
}

static VkResult
panvk_pipeline_builder_compile_shaders(struct panvk_pipeline_builder *builder,
                                       struct panvk_pipeline *pipeline)
{
   const VkPipelineShaderStageCreateInfo *stage_infos[MESA_SHADER_STAGES] = {
      NULL
   };
   for (uint32_t i = 0; i < builder->create_info->stageCount; i++) {
      gl_shader_stage stage = vk_to_mesa_shader_stage(builder->create_info->pStages[i].stage);
      stage_infos[stage] = &builder->create_info->pStages[i];
   }

   /* compile shaders in reverse order */
   unsigned sysval_ubo = builder->layout->num_ubos;

   for (gl_shader_stage stage = MESA_SHADER_STAGES - 1;
        stage > MESA_SHADER_NONE; stage--) {
      const VkPipelineShaderStageCreateInfo *stage_info = stage_infos[stage];
      if (!stage_info)
         continue;

      struct panvk_shader *shader;

      shader = panvk_per_arch(shader_create)(builder->device, stage, stage_info,
                                             builder->layout, sysval_ubo,
                                             &pipeline->blend.state,
                                             panvk_pipeline_static_state(pipeline,
                                                                         VK_DYNAMIC_STATE_BLEND_CONSTANTS),
                                             builder->alloc);
      if (!shader)
         return VK_ERROR_OUT_OF_HOST_MEMORY;

      if (shader->info.sysvals.sysval_count)
         sysval_ubo++;
 
      builder->shaders[stage] = shader;
      builder->shader_total_size = ALIGN_POT(builder->shader_total_size, 128);
      builder->stages[stage].shader_offset = builder->shader_total_size;
      builder->shader_total_size +=
         util_dynarray_num_elements(&shader->binary, uint8_t);
   }

   return VK_SUCCESS;
}

static VkResult
panvk_pipeline_builder_upload_shaders(struct panvk_pipeline_builder *builder,
                                      struct panvk_pipeline *pipeline)
{
   struct panfrost_bo *bin_bo =
      panfrost_bo_create(&builder->device->physical_device->pdev,
                         builder->shader_total_size, PAN_BO_EXECUTE,
                         "Shader");

   pipeline->binary_bo = bin_bo;
   panfrost_bo_mmap(bin_bo);

   for (uint32_t i = 0; i < MESA_SHADER_STAGES; i++) {
      const struct panvk_shader *shader = builder->shaders[i];
      if (!shader)
         continue;

      memcpy(pipeline->binary_bo->ptr.cpu + builder->stages[i].shader_offset,
             util_dynarray_element(&shader->binary, uint8_t, 0),
             util_dynarray_num_elements(&shader->binary, uint8_t));
   }

   return VK_SUCCESS;
}

static bool
panvk_pipeline_static_sysval(struct panvk_pipeline *pipeline,
                             unsigned id)
{
   switch (id) {
   case PAN_SYSVAL_VIEWPORT_SCALE:
   case PAN_SYSVAL_VIEWPORT_OFFSET:
      return panvk_pipeline_static_state(pipeline, VK_DYNAMIC_STATE_VIEWPORT);
   default:
      return false;
   }
}

static void
panvk_pipeline_builder_alloc_static_state_bo(struct panvk_pipeline_builder *builder,
                                             struct panvk_pipeline *pipeline)
{
   struct panfrost_device *pdev =
      &builder->device->physical_device->pdev;
   unsigned bo_size = 0;

   for (uint32_t i = 0; i < MESA_SHADER_STAGES; i++) {
      const struct panvk_shader *shader = builder->shaders[i];
      if (!shader)
         continue;

      if (pipeline->fs.dynamic_rsd && i == MESA_SHADER_FRAGMENT)
         continue;

      bo_size = ALIGN_POT(bo_size, pan_alignment(RENDERER_STATE));
      builder->stages[i].rsd_offset = bo_size;
      bo_size += pan_size(RENDERER_STATE);
      if (i == MESA_SHADER_FRAGMENT)
         bo_size += pan_size(BLEND) * MAX2(pipeline->blend.state.rt_count, 1);
   }

   if (panvk_pipeline_static_state(pipeline, VK_DYNAMIC_STATE_VIEWPORT) &&
       panvk_pipeline_static_state(pipeline, VK_DYNAMIC_STATE_SCISSOR)) {
      bo_size = ALIGN_POT(bo_size, pan_alignment(VIEWPORT));
      builder->vpd_offset = bo_size;
      bo_size += pan_size(VIEWPORT);
   }

   for (uint32_t i = 0; i < MESA_SHADER_STAGES; i++) {
      const struct panvk_shader *shader = builder->shaders[i];
      if (!shader || !shader->info.sysvals.sysval_count)
         continue;

      bool static_sysvals = true;
      for (unsigned s = 0; s < shader->info.sysvals.sysval_count; s++) {
         unsigned id = shader->info.sysvals.sysvals[i];
         static_sysvals &= panvk_pipeline_static_sysval(pipeline, id);
         switch (PAN_SYSVAL_TYPE(id)) {
         case PAN_SYSVAL_VIEWPORT_SCALE:
         case PAN_SYSVAL_VIEWPORT_OFFSET:
            pipeline->sysvals[i].dirty_mask |= PANVK_DYNAMIC_VIEWPORT;
            break;
         default:
            break;
         }
      }

      if (!static_sysvals) {
         builder->stages[i].sysvals_offset = ~0;
         continue;
      }

      bo_size = ALIGN_POT(bo_size, 16);
      builder->stages[i].sysvals_offset = bo_size;
      bo_size += shader->info.sysvals.sysval_count * 16;
   }

   if (bo_size) {
      pipeline->state_bo =
         panfrost_bo_create(pdev, bo_size, 0, "Pipeline descriptors");
      panfrost_bo_mmap(pipeline->state_bo);
   }
}

static void
panvk_pipeline_builder_upload_sysval(struct panvk_pipeline_builder *builder,
                                     struct panvk_pipeline *pipeline,
                                     unsigned id, union panvk_sysval_data *data)
{
   switch (PAN_SYSVAL_TYPE(id)) {
   case PAN_SYSVAL_VIEWPORT_SCALE:
      panvk_sysval_upload_viewport_scale(builder->create_info->pViewportState->pViewports,
                                         data);
      break;
   case PAN_SYSVAL_VIEWPORT_OFFSET:
      panvk_sysval_upload_viewport_offset(builder->create_info->pViewportState->pViewports,
                                          data);
      break;
   default:
      unreachable("Invalid static sysval");
   }
}

static void
panvk_pipeline_builder_init_sysvals(struct panvk_pipeline_builder *builder,
                                    struct panvk_pipeline *pipeline,
                                    gl_shader_stage stage)
{
   const struct panvk_shader *shader = builder->shaders[stage];

   pipeline->sysvals[stage].ids = shader->info.sysvals;
   pipeline->sysvals[stage].ubo_idx = shader->sysval_ubo;

   if (!shader->info.sysvals.sysval_count ||
       builder->stages[stage].sysvals_offset == ~0)
      return;

   union panvk_sysval_data *static_data =
      pipeline->state_bo->ptr.cpu + builder->stages[stage].sysvals_offset;

   pipeline->sysvals[stage].ubo =
      pipeline->state_bo->ptr.gpu + builder->stages[stage].sysvals_offset;

   for (unsigned i = 0; i < shader->info.sysvals.sysval_count; i++) {
      unsigned id = shader->info.sysvals.sysvals[i];

      panvk_pipeline_builder_upload_sysval(builder,
                                           pipeline,
                                           id, &static_data[i]);
   }
}

static void
panvk_pipeline_builder_init_shaders(struct panvk_pipeline_builder *builder,
                                    struct panvk_pipeline *pipeline)
{
   for (uint32_t i = 0; i < MESA_SHADER_STAGES; i++) {
      const struct panvk_shader *shader = builder->shaders[i];
      if (!shader)
         continue;

      pipeline->tls_size = MAX2(pipeline->tls_size, shader->info.tls_size);
      pipeline->wls_size = MAX2(pipeline->wls_size, shader->info.wls_size);

      if (i == MESA_SHADER_VERTEX && shader->info.vs.writes_point_size)
         pipeline->ia.writes_point_size = true;

      mali_ptr shader_ptr = pipeline->binary_bo->ptr.gpu +
                            builder->stages[i].shader_offset;

      void *rsd = pipeline->state_bo->ptr.cpu + builder->stages[i].rsd_offset;
      mali_ptr gpu_rsd = pipeline->state_bo->ptr.gpu + builder->stages[i].rsd_offset;

      if (i != MESA_SHADER_FRAGMENT) {
         panvk_per_arch(emit_non_fs_rsd)(builder->device, &shader->info, shader_ptr, rsd);
      } else if (!pipeline->fs.dynamic_rsd) {
         void *bd = rsd + pan_size(RENDERER_STATE);

         panvk_per_arch(emit_base_fs_rsd)(builder->device, pipeline, rsd);
         for (unsigned rt = 0; rt < MAX2(pipeline->blend.state.rt_count, 1); rt++) {
            panvk_per_arch(emit_blend)(builder->device, pipeline, rt, bd);
            bd += pan_size(BLEND);
         }
      } else {
         gpu_rsd = 0;
         panvk_per_arch(emit_base_fs_rsd)(builder->device, pipeline, &pipeline->fs.rsd_template);
         for (unsigned rt = 0; rt < MAX2(pipeline->blend.state.rt_count, 1); rt++) {
            panvk_per_arch(emit_blend)(builder->device, pipeline, rt,
                                       &pipeline->blend.bd_template[rt]);
         }
      }

      pipeline->rsds[i] = gpu_rsd;
      panvk_pipeline_builder_init_sysvals(builder, pipeline, i);
   }

   pipeline->num_ubos = builder->layout->num_ubos;
   for (unsigned i = 0; i < ARRAY_SIZE(pipeline->sysvals); i++) {
      if (pipeline->sysvals[i].ids.sysval_count)
         pipeline->num_ubos = MAX2(pipeline->num_ubos, pipeline->sysvals[i].ubo_idx + 1);
   }

   pipeline->num_sysvals = 0;
   for (unsigned i = 0; i < ARRAY_SIZE(pipeline->sysvals); i++)
      pipeline->num_sysvals += pipeline->sysvals[i].ids.sysval_count;
}


static void
panvk_pipeline_builder_parse_viewport(struct panvk_pipeline_builder *builder,
                                      struct panvk_pipeline *pipeline)
{
   /* The spec says:
    *
    *    pViewportState is a pointer to an instance of the
    *    VkPipelineViewportStateCreateInfo structure, and is ignored if the
    *    pipeline has rasterization disabled.
    */
   if (!builder->rasterizer_discard &&
       panvk_pipeline_static_state(pipeline, VK_DYNAMIC_STATE_VIEWPORT) &&
       panvk_pipeline_static_state(pipeline, VK_DYNAMIC_STATE_SCISSOR)) {
      void *vpd = pipeline->state_bo->ptr.cpu + builder->vpd_offset;
      panvk_per_arch(emit_viewport)(builder->create_info->pViewportState->pViewports,
                                    builder->create_info->pViewportState->pScissors,
                                    vpd);
      pipeline->vpd = pipeline->state_bo->ptr.gpu +
                      builder->vpd_offset;
   }
   if (panvk_pipeline_static_state(pipeline, VK_DYNAMIC_STATE_VIEWPORT))
      pipeline->viewport = builder->create_info->pViewportState->pViewports[0];

   if (panvk_pipeline_static_state(pipeline, VK_DYNAMIC_STATE_SCISSOR))
      pipeline->scissor = builder->create_info->pViewportState->pScissors[0];
}

static void
panvk_pipeline_builder_parse_dynamic(struct panvk_pipeline_builder *builder,
                                     struct panvk_pipeline *pipeline)
{
   const VkPipelineDynamicStateCreateInfo *dynamic_info =
      builder->create_info->pDynamicState;

   if (!dynamic_info)
      return;

   for (uint32_t i = 0; i < dynamic_info->dynamicStateCount; i++) {
      VkDynamicState state = dynamic_info->pDynamicStates[i];
      switch (state) {
      case VK_DYNAMIC_STATE_VIEWPORT ... VK_DYNAMIC_STATE_STENCIL_REFERENCE:
         pipeline->dynamic_state_mask |= 1 << state;
         break;
      default:
         unreachable("unsupported dynamic state");
      }
   }

}

static enum mali_draw_mode
translate_prim_topology(VkPrimitiveTopology in)
{
   switch (in) {
   case VK_PRIMITIVE_TOPOLOGY_POINT_LIST:
      return MALI_DRAW_MODE_POINTS;
   case VK_PRIMITIVE_TOPOLOGY_LINE_LIST:
      return MALI_DRAW_MODE_LINES;
   case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP:
      return MALI_DRAW_MODE_LINE_STRIP;
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
      return MALI_DRAW_MODE_TRIANGLES;
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
      return MALI_DRAW_MODE_TRIANGLE_STRIP;
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN:
      return MALI_DRAW_MODE_TRIANGLE_FAN;
   case VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY:
   case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY:
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY:
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY:
   case VK_PRIMITIVE_TOPOLOGY_PATCH_LIST:
   default:
      unreachable("Invalid primitive type");
   }
}

static void
panvk_pipeline_builder_parse_input_assembly(struct panvk_pipeline_builder *builder,
                                            struct panvk_pipeline *pipeline)
{
   pipeline->ia.primitive_restart =
      builder->create_info->pInputAssemblyState->primitiveRestartEnable;
   pipeline->ia.topology =
      translate_prim_topology(builder->create_info->pInputAssemblyState->topology);
}

static enum pipe_logicop
translate_logicop(VkLogicOp in)
{
   switch (in) {
   case VK_LOGIC_OP_CLEAR: return PIPE_LOGICOP_CLEAR;
   case VK_LOGIC_OP_AND: return PIPE_LOGICOP_AND;
   case VK_LOGIC_OP_AND_REVERSE: return PIPE_LOGICOP_AND_REVERSE;
   case VK_LOGIC_OP_COPY: return PIPE_LOGICOP_COPY;
   case VK_LOGIC_OP_AND_INVERTED: return PIPE_LOGICOP_AND_INVERTED;
   case VK_LOGIC_OP_NO_OP: return PIPE_LOGICOP_NOOP;
   case VK_LOGIC_OP_XOR: return PIPE_LOGICOP_XOR;
   case VK_LOGIC_OP_OR: return PIPE_LOGICOP_OR;
   case VK_LOGIC_OP_NOR: return PIPE_LOGICOP_NOR;
   case VK_LOGIC_OP_EQUIVALENT: return PIPE_LOGICOP_EQUIV;
   case VK_LOGIC_OP_INVERT: return PIPE_LOGICOP_INVERT;
   case VK_LOGIC_OP_OR_REVERSE: return PIPE_LOGICOP_OR_REVERSE;
   case VK_LOGIC_OP_COPY_INVERTED: return PIPE_LOGICOP_COPY_INVERTED;
   case VK_LOGIC_OP_OR_INVERTED: return PIPE_LOGICOP_OR_INVERTED;
   case VK_LOGIC_OP_NAND: return PIPE_LOGICOP_NAND;
   case VK_LOGIC_OP_SET: return PIPE_LOGICOP_SET;
   default: unreachable("Invalid logicop");
   }
}

static enum blend_func
translate_blend_op(VkBlendOp in)
{
   switch (in) {
   case VK_BLEND_OP_ADD: return BLEND_FUNC_ADD;
   case VK_BLEND_OP_SUBTRACT: return BLEND_FUNC_SUBTRACT;
   case VK_BLEND_OP_REVERSE_SUBTRACT: return BLEND_FUNC_REVERSE_SUBTRACT;
   case VK_BLEND_OP_MIN: return BLEND_FUNC_MIN;
   case VK_BLEND_OP_MAX: return BLEND_FUNC_MAX;
   default: unreachable("Invalid blend op");
   }
}

static enum blend_factor
translate_blend_factor(VkBlendFactor in, bool dest_has_alpha)
{
   switch (in) {
   case VK_BLEND_FACTOR_ZERO:
   case VK_BLEND_FACTOR_ONE:
      return BLEND_FACTOR_ZERO;
   case VK_BLEND_FACTOR_SRC_COLOR:
   case VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR:
      return BLEND_FACTOR_SRC_COLOR;
   case VK_BLEND_FACTOR_DST_COLOR:
   case VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR:
      return BLEND_FACTOR_DST_COLOR;
   case VK_BLEND_FACTOR_SRC_ALPHA:
   case VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA:
      return BLEND_FACTOR_SRC_ALPHA;
   case VK_BLEND_FACTOR_DST_ALPHA:
   case VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA:
      return dest_has_alpha ? BLEND_FACTOR_DST_ALPHA : BLEND_FACTOR_ZERO;
   case VK_BLEND_FACTOR_CONSTANT_COLOR:
   case VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR:
      return BLEND_FACTOR_CONSTANT_COLOR;
   case VK_BLEND_FACTOR_CONSTANT_ALPHA:
   case VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA:
      return BLEND_FACTOR_CONSTANT_ALPHA;
   case VK_BLEND_FACTOR_SRC1_COLOR:
   case VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR:
      return BLEND_FACTOR_SRC1_COLOR;
   case VK_BLEND_FACTOR_SRC1_ALPHA:
   case VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA:
      return BLEND_FACTOR_SRC1_ALPHA;
   case VK_BLEND_FACTOR_SRC_ALPHA_SATURATE:
      return BLEND_FACTOR_SRC_ALPHA_SATURATE;
   default: unreachable("Invalid blend factor");
   }
}

static bool
inverted_blend_factor(VkBlendFactor in, bool dest_has_alpha)
{
   switch (in) {
   case VK_BLEND_FACTOR_ONE:
   case VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR:
   case VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR:
   case VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA:
   case VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR:
   case VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA:
   case VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR:
   case VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA:
      return true;
   case VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA:
      return dest_has_alpha ? true : false;
   case VK_BLEND_FACTOR_DST_ALPHA:
      return !dest_has_alpha ? true : false;
   default:
      return false;
   }
}

bool
panvk_per_arch(blend_needs_lowering)(const struct panfrost_device *dev,
                                     const struct pan_blend_state *state,
                                     unsigned rt)
{
   /* LogicOp requires a blend shader */
   if (state->logicop_enable)
      return true;

   /* Not all formats can be blended by fixed-function hardware */
   if (!panfrost_blendable_formats_v7[state->rts[rt].format].internal)
      return true;

   unsigned constant_mask = pan_blend_constant_mask(state->rts[rt].equation);

   /* v6 doesn't support blend constants in FF blend equations.
    * v7 only uses the constant from RT 0 (TODO: what if it's the same
    * constant? or a constant is shared?)
    */
   if (constant_mask && (PAN_ARCH == 6 || (PAN_ARCH == 7 && rt > 0)))
      return true;

   if (!pan_blend_is_homogenous_constant(constant_mask, state->constants))
      return true;

   bool supports_2src = pan_blend_supports_2src(dev->arch);
   return !pan_blend_can_fixed_function(state->rts[rt].equation, supports_2src);
}

static void
panvk_pipeline_builder_parse_color_blend(struct panvk_pipeline_builder *builder,
                                         struct panvk_pipeline *pipeline)
{
   struct panfrost_device *pdev = &builder->device->physical_device->pdev;
   pipeline->blend.state.logicop_enable =
      builder->create_info->pColorBlendState->logicOpEnable;
   pipeline->blend.state.logicop_func =
      translate_logicop(builder->create_info->pColorBlendState->logicOp);
   pipeline->blend.state.rt_count = util_last_bit(builder->active_color_attachments);
   memcpy(pipeline->blend.state.constants,
          builder->create_info->pColorBlendState->blendConstants,
          sizeof(pipeline->blend.state.constants));

   for (unsigned i = 0; i < pipeline->blend.state.rt_count; i++) {
      const VkPipelineColorBlendAttachmentState *in =
         &builder->create_info->pColorBlendState->pAttachments[i];
      struct pan_blend_rt_state *out = &pipeline->blend.state.rts[i];

      out->format = builder->color_attachment_formats[i];

      bool dest_has_alpha = util_format_has_alpha(out->format);

      out->nr_samples = builder->create_info->pMultisampleState->rasterizationSamples;
      out->equation.blend_enable = in->blendEnable;
      out->equation.color_mask = in->colorWriteMask;
      out->equation.rgb_func = translate_blend_op(in->colorBlendOp);
      out->equation.rgb_src_factor = translate_blend_factor(in->srcColorBlendFactor, dest_has_alpha);
      out->equation.rgb_invert_src_factor = inverted_blend_factor(in->srcColorBlendFactor, dest_has_alpha);
      out->equation.rgb_dst_factor = translate_blend_factor(in->dstColorBlendFactor, dest_has_alpha);
      out->equation.rgb_invert_dst_factor = inverted_blend_factor(in->dstColorBlendFactor, dest_has_alpha);
      out->equation.alpha_func = translate_blend_op(in->alphaBlendOp);
      out->equation.alpha_src_factor = translate_blend_factor(in->srcAlphaBlendFactor, dest_has_alpha);
      out->equation.alpha_invert_src_factor = inverted_blend_factor(in->srcAlphaBlendFactor, dest_has_alpha);
      out->equation.alpha_dst_factor = translate_blend_factor(in->dstAlphaBlendFactor, dest_has_alpha);
      out->equation.alpha_invert_dst_factor = inverted_blend_factor(in->dstAlphaBlendFactor, dest_has_alpha);

      pipeline->blend.reads_dest |= pan_blend_reads_dest(out->equation);

      unsigned constant_mask =
         panvk_per_arch(blend_needs_lowering)(pdev, &pipeline->blend.state, i) ?
         0 : pan_blend_constant_mask(out->equation);
      pipeline->blend.constant[i].index = ffs(constant_mask) - 1;
      if (constant_mask && PAN_ARCH >= 6) {
         /* On Bifrost, the blend constant is expressed with a UNORM of the
          * size of the target format. The value is then shifted such that
          * used bits are in the MSB. Here we calculate the factor at pipeline
          * creation time so we only have to do a
          *   hw_constant = float_constant * factor;
          * at descriptor emission time.
          */
         const struct util_format_description *format_desc =
            util_format_description(out->format);
         unsigned chan_size = 0;
         for (unsigned c = 0; c < format_desc->nr_channels; c++)
            chan_size = MAX2(format_desc->channel[c].size, chan_size);
         pipeline->blend.constant[i].bifrost_factor =
            ((1 << chan_size) - 1) << (16 - chan_size);
      }
   }
}

static void
panvk_pipeline_builder_parse_multisample(struct panvk_pipeline_builder *builder,
                                         struct panvk_pipeline *pipeline)
{
   unsigned nr_samples =
      MAX2(builder->create_info->pMultisampleState->rasterizationSamples, 1);

   pipeline->ms.rast_samples =
      builder->create_info->pMultisampleState->rasterizationSamples;
   pipeline->ms.sample_mask =
      builder->create_info->pMultisampleState->pSampleMask ?
      builder->create_info->pMultisampleState->pSampleMask[0] : UINT16_MAX;
   pipeline->ms.min_samples =
      MAX2(builder->create_info->pMultisampleState->minSampleShading * nr_samples, 1);
}

static enum mali_stencil_op
translate_stencil_op(VkStencilOp in)
{
   switch (in) {
   case VK_STENCIL_OP_KEEP: return MALI_STENCIL_OP_KEEP;
   case VK_STENCIL_OP_ZERO: return MALI_STENCIL_OP_ZERO;
   case VK_STENCIL_OP_REPLACE: return MALI_STENCIL_OP_REPLACE;
   case VK_STENCIL_OP_INCREMENT_AND_CLAMP: return MALI_STENCIL_OP_INCR_SAT;
   case VK_STENCIL_OP_DECREMENT_AND_CLAMP: return MALI_STENCIL_OP_DECR_SAT;
   case VK_STENCIL_OP_INCREMENT_AND_WRAP: return MALI_STENCIL_OP_INCR_WRAP;
   case VK_STENCIL_OP_DECREMENT_AND_WRAP: return MALI_STENCIL_OP_DECR_WRAP;
   case VK_STENCIL_OP_INVERT: return MALI_STENCIL_OP_INVERT;
   default: unreachable("Invalid stencil op");
   }
}

static void
panvk_pipeline_builder_parse_zs(struct panvk_pipeline_builder *builder,
                                struct panvk_pipeline *pipeline)
{
   pipeline->zs.z_test = builder->create_info->pDepthStencilState->depthTestEnable;
   pipeline->zs.z_write = builder->create_info->pDepthStencilState->depthWriteEnable;
   pipeline->zs.z_compare_func =
      panvk_per_arch(translate_compare_func)(builder->create_info->pDepthStencilState->depthCompareOp);
   pipeline->zs.s_test = builder->create_info->pDepthStencilState->stencilTestEnable;
   pipeline->zs.s_front.fail_op =
      translate_stencil_op(builder->create_info->pDepthStencilState->front.failOp);
   pipeline->zs.s_front.pass_op =
      translate_stencil_op(builder->create_info->pDepthStencilState->front.passOp);
   pipeline->zs.s_front.z_fail_op =
      translate_stencil_op(builder->create_info->pDepthStencilState->front.depthFailOp);
   pipeline->zs.s_front.compare_func =
      panvk_per_arch(translate_compare_func)(builder->create_info->pDepthStencilState->front.compareOp);
   pipeline->zs.s_front.compare_mask =
      builder->create_info->pDepthStencilState->front.compareMask;
   pipeline->zs.s_front.write_mask =
      builder->create_info->pDepthStencilState->front.writeMask;
   pipeline->zs.s_front.ref =
      builder->create_info->pDepthStencilState->front.reference;
   pipeline->zs.s_back.fail_op =
      translate_stencil_op(builder->create_info->pDepthStencilState->back.failOp);
   pipeline->zs.s_back.pass_op =
      translate_stencil_op(builder->create_info->pDepthStencilState->back.passOp);
   pipeline->zs.s_back.z_fail_op =
      translate_stencil_op(builder->create_info->pDepthStencilState->back.depthFailOp);
   pipeline->zs.s_back.compare_func =
      panvk_per_arch(translate_compare_func)(builder->create_info->pDepthStencilState->back.compareOp);
   pipeline->zs.s_back.compare_mask =
      builder->create_info->pDepthStencilState->back.compareMask;
   pipeline->zs.s_back.write_mask =
      builder->create_info->pDepthStencilState->back.writeMask;
   pipeline->zs.s_back.ref =
      builder->create_info->pDepthStencilState->back.reference;
}

static void
panvk_pipeline_builder_parse_rast(struct panvk_pipeline_builder *builder,
                                  struct panvk_pipeline *pipeline)
{
   pipeline->rast.clamp_depth = builder->create_info->pRasterizationState->depthClampEnable;
   pipeline->rast.depth_bias.enable = builder->create_info->pRasterizationState->depthBiasEnable;
   pipeline->rast.depth_bias.constant_factor =
      builder->create_info->pRasterizationState->depthBiasConstantFactor;
   pipeline->rast.depth_bias.clamp = builder->create_info->pRasterizationState->depthBiasClamp;
   pipeline->rast.depth_bias.slope_factor = builder->create_info->pRasterizationState->depthBiasSlopeFactor;
   pipeline->rast.front_ccw = builder->create_info->pRasterizationState->frontFace == VK_FRONT_FACE_COUNTER_CLOCKWISE;
   pipeline->rast.cull_front_face = builder->create_info->pRasterizationState->cullMode & VK_CULL_MODE_FRONT_BIT;
   pipeline->rast.cull_back_face = builder->create_info->pRasterizationState->cullMode & VK_CULL_MODE_BACK_BIT;
}

static bool
panvk_fs_required(struct panvk_pipeline *pipeline)
{
   const struct pan_shader_info *info = &pipeline->fs.info;

   /* If we generally have side effects */
   if (info->fs.sidefx)
      return true;

    /* If colour is written we need to execute */
    const struct pan_blend_state *blend = &pipeline->blend.state;
    for (unsigned i = 0; i < blend->rt_count; ++i) {
       if (blend->rts[i].equation.color_mask)
          return true;
    }

    /* If depth is written and not implied we need to execute.
     * TODO: Predicate on Z/S writes being enabled */
    return (info->fs.writes_depth || info->fs.writes_stencil);
}

#define PANVK_DYNAMIC_FS_RSD_MASK \
        ((1 << VK_DYNAMIC_STATE_DEPTH_BIAS) | \
         (1 << VK_DYNAMIC_STATE_BLEND_CONSTANTS) | \
         (1 << VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK) | \
         (1 << VK_DYNAMIC_STATE_STENCIL_WRITE_MASK) | \
         (1 << VK_DYNAMIC_STATE_STENCIL_REFERENCE))

static void
panvk_pipeline_builder_init_fs_state(struct panvk_pipeline_builder *builder,
                                     struct panvk_pipeline *pipeline)
{
   if (!builder->shaders[MESA_SHADER_FRAGMENT])
      return;

   pipeline->fs.dynamic_rsd =
      pipeline->dynamic_state_mask & PANVK_DYNAMIC_FS_RSD_MASK;
   pipeline->fs.address = pipeline->binary_bo->ptr.gpu +
                          builder->stages[MESA_SHADER_FRAGMENT].shader_offset;
   pipeline->fs.info = builder->shaders[MESA_SHADER_FRAGMENT]->info;
   pipeline->fs.rt_mask = builder->active_color_attachments;
   pipeline->fs.required = panvk_fs_required(pipeline);
}

static void
panvk_pipeline_update_varying_slot(struct panvk_varyings_info *varyings,
                                   gl_shader_stage stage,
                                   const struct pan_shader_varying *varying,
                                   bool input)
{
   bool fs = stage == MESA_SHADER_FRAGMENT;
   gl_varying_slot loc = varying->location;
   enum panvk_varying_buf_id buf_id =
      panvk_varying_buf_id(fs, loc);

   varyings->stage[stage].loc[varyings->stage[stage].count++] = loc;

   if (panvk_varying_is_builtin(stage, loc)) {
      varyings->buf_mask |= 1 << buf_id;
      return;
   }

   assert(loc < ARRAY_SIZE(varyings->varying));

   enum pipe_format new_fmt = varying->format;
   enum pipe_format old_fmt = varyings->varying[loc].format;

   BITSET_SET(varyings->active, loc);

   /* We expect inputs to either be set by a previous stage or be built
    * in, skip the entry if that's not the case, we'll emit a const
    * varying returning zero for those entries.
    */
   if (input && old_fmt == PIPE_FORMAT_NONE)
      return;

   unsigned new_size = util_format_get_blocksize(new_fmt);
   unsigned old_size = util_format_get_blocksize(old_fmt);

   if (old_size < new_size)
      varyings->varying[loc].format = new_fmt;

   varyings->buf_mask |= 1 << buf_id;
}

static void
panvk_pipeline_builder_collect_varyings(struct panvk_pipeline_builder *builder,
                                        struct panvk_pipeline *pipeline)
{
   for (uint32_t s = 0; s < MESA_SHADER_STAGES; s++) {
      if (!builder->shaders[s])
         continue;

      const struct pan_shader_info *info = &builder->shaders[s]->info;

      for (unsigned i = 0; i < info->varyings.input_count; i++) {
         panvk_pipeline_update_varying_slot(&pipeline->varyings, s,
                                            &info->varyings.input[i],
                                            true);
      }

      for (unsigned i = 0; i < info->varyings.output_count; i++) {
         panvk_pipeline_update_varying_slot(&pipeline->varyings, s,
                                            &info->varyings.output[i],
                                            false);
      }
   }

   /* TODO: Xfb */
   gl_varying_slot loc;
   BITSET_FOREACH_SET(loc, pipeline->varyings.active, VARYING_SLOT_MAX) {
      if (pipeline->varyings.varying[loc].format == PIPE_FORMAT_NONE)
         continue;

      enum panvk_varying_buf_id buf_id =
         panvk_varying_buf_id(false, loc);
      unsigned buf_idx = panvk_varying_buf_index(&pipeline->varyings, buf_id);
      unsigned varying_sz = panvk_varying_size(&pipeline->varyings, loc);

      pipeline->varyings.varying[loc].buf = buf_idx;
      pipeline->varyings.varying[loc].offset =
         pipeline->varyings.buf[buf_idx].stride;
      pipeline->varyings.buf[buf_idx].stride += varying_sz;
   }
}

static void
panvk_pipeline_builder_parse_vertex_input(struct panvk_pipeline_builder *builder,
                                          struct panvk_pipeline *pipeline)
{
   struct panvk_attribs_info *attribs = &pipeline->attribs;
   const VkPipelineVertexInputStateCreateInfo *info =
      builder->create_info->pVertexInputState;

   for (unsigned i = 0; i < info->vertexBindingDescriptionCount; i++) {
      const VkVertexInputBindingDescription *desc =
         &info->pVertexBindingDescriptions[i];
      attribs->buf_count = MAX2(desc->binding + 1, attribs->buf_count);
      attribs->buf[desc->binding].stride = desc->stride;
      attribs->buf[desc->binding].special = false;
   }

   for (unsigned i = 0; i < info->vertexAttributeDescriptionCount; i++) {
      const VkVertexInputAttributeDescription *desc =
         &info->pVertexAttributeDescriptions[i];
      attribs->attrib[desc->location].buf = desc->binding;
      attribs->attrib[desc->location].format =
         vk_format_to_pipe_format(desc->format);
      attribs->attrib[desc->location].offset = desc->offset;
   }

   const struct pan_shader_info *vs =
      &builder->shaders[MESA_SHADER_VERTEX]->info;

   if (vs->attribute_count >= PAN_VERTEX_ID) {
      attribs->buf[attribs->buf_count].special = true;
      attribs->buf[attribs->buf_count].special_id = PAN_VERTEX_ID;
      attribs->attrib[PAN_VERTEX_ID].buf = attribs->buf_count++;
      attribs->attrib[PAN_VERTEX_ID].format = PIPE_FORMAT_R32_UINT;
   }

   if (vs->attribute_count >= PAN_INSTANCE_ID) {
      attribs->buf[attribs->buf_count].special = true;
      attribs->buf[attribs->buf_count].special_id = PAN_INSTANCE_ID;
      attribs->attrib[PAN_INSTANCE_ID].buf = attribs->buf_count++;
      attribs->attrib[PAN_INSTANCE_ID].format = PIPE_FORMAT_R32_UINT;
   }

   attribs->attrib_count = MAX2(attribs->attrib_count, vs->attribute_count);
}

static VkResult
panvk_pipeline_builder_build(struct panvk_pipeline_builder *builder,
                             struct panvk_pipeline **pipeline)
{
   VkResult result = panvk_pipeline_builder_create_pipeline(builder, pipeline);
   if (result != VK_SUCCESS)
      return result;

   /* TODO: make those functions return a result and handle errors */
   panvk_pipeline_builder_parse_dynamic(builder, *pipeline);
   panvk_pipeline_builder_parse_color_blend(builder, *pipeline);
   panvk_pipeline_builder_compile_shaders(builder, *pipeline);
   panvk_pipeline_builder_collect_varyings(builder, *pipeline);
   panvk_pipeline_builder_parse_input_assembly(builder, *pipeline);
   panvk_pipeline_builder_parse_multisample(builder, *pipeline);
   panvk_pipeline_builder_parse_zs(builder, *pipeline);
   panvk_pipeline_builder_parse_rast(builder, *pipeline);
   panvk_pipeline_builder_parse_vertex_input(builder, *pipeline);


   panvk_pipeline_builder_upload_shaders(builder, *pipeline);
   panvk_pipeline_builder_init_fs_state(builder, *pipeline);
   panvk_pipeline_builder_alloc_static_state_bo(builder, *pipeline);
   panvk_pipeline_builder_init_shaders(builder, *pipeline);
   panvk_pipeline_builder_parse_viewport(builder, *pipeline);

   return VK_SUCCESS;
}

static void
panvk_pipeline_builder_init_graphics(struct panvk_pipeline_builder *builder,
                                     struct panvk_device *dev,
                                     struct panvk_pipeline_cache *cache,
                                     const VkGraphicsPipelineCreateInfo *create_info,
                                     const VkAllocationCallbacks *alloc)
{
   VK_FROM_HANDLE(panvk_pipeline_layout, layout, create_info->layout);
   assert(layout);
   *builder = (struct panvk_pipeline_builder) {
      .device = dev,
      .cache = cache,
      .layout = layout,
      .create_info = create_info,
      .alloc = alloc,
   };

   builder->rasterizer_discard =
      create_info->pRasterizationState->rasterizerDiscardEnable;

   if (builder->rasterizer_discard) {
      builder->samples = VK_SAMPLE_COUNT_1_BIT;
   } else {
      builder->samples = create_info->pMultisampleState->rasterizationSamples;

      const struct panvk_render_pass *pass = panvk_render_pass_from_handle(create_info->renderPass);
      const struct panvk_subpass *subpass = &pass->subpasses[create_info->subpass];

      builder->use_depth_stencil_attachment =
         subpass->zs_attachment.idx != VK_ATTACHMENT_UNUSED;

      assert(subpass->color_count <= create_info->pColorBlendState->attachmentCount);
      builder->active_color_attachments = 0;
      for (uint32_t i = 0; i < subpass->color_count; i++) {
         uint32_t idx = subpass->color_attachments[i].idx;
         if (idx == VK_ATTACHMENT_UNUSED)
            continue;

         builder->active_color_attachments |= 1 << i;
         builder->color_attachment_formats[i] = pass->attachments[idx].format;
      }
   }
}

VkResult
panvk_per_arch(CreateGraphicsPipelines)(VkDevice device,
                                        VkPipelineCache pipelineCache,
                                        uint32_t count,
                                        const VkGraphicsPipelineCreateInfo *pCreateInfos,
                                        const VkAllocationCallbacks *pAllocator,
                                        VkPipeline *pPipelines)
{
   VK_FROM_HANDLE(panvk_device, dev, device);
   VK_FROM_HANDLE(panvk_pipeline_cache, cache, pipelineCache);

   for (uint32_t i = 0; i < count; i++) {
      struct panvk_pipeline_builder builder;
      panvk_pipeline_builder_init_graphics(&builder, dev, cache,
                                           &pCreateInfos[i], pAllocator);

      struct panvk_pipeline *pipeline;
      VkResult result = panvk_pipeline_builder_build(&builder, &pipeline);
      panvk_pipeline_builder_finish(&builder);

      if (result != VK_SUCCESS) {
         for (uint32_t j = 0; j < i; j++) {
            panvk_DestroyPipeline(device, pPipelines[j], pAllocator);
            pPipelines[j] = VK_NULL_HANDLE;
         }

         return result;
      }

      pPipelines[i] = panvk_pipeline_to_handle(pipeline);
   }

   return VK_SUCCESS;
}
