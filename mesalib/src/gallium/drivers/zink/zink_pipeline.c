/*
 * Copyright 2018 Collabora Ltd.
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

#include "zink_pipeline.h"

#include "zink_compiler.h"
#include "zink_context.h"
#include "zink_program.h"
#include "zink_render_pass.h"
#include "zink_screen.h"
#include "zink_state.h"

#include "util/u_debug.h"
#include "util/u_prim.h"

VkPipeline
zink_create_gfx_pipeline(struct zink_screen *screen,
                         struct zink_gfx_program *prog,
                         struct zink_gfx_pipeline_state *state,
                         VkPrimitiveTopology primitive_topology)
{
   VkPipelineVertexInputStateCreateInfo vertex_input_state = {};
   vertex_input_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
   vertex_input_state.pVertexBindingDescriptions = state->bindings;
   vertex_input_state.vertexBindingDescriptionCount = state->element_state->num_bindings;
   vertex_input_state.pVertexAttributeDescriptions = state->element_state->attribs;
   vertex_input_state.vertexAttributeDescriptionCount = state->element_state->num_attribs;

   VkPipelineVertexInputDivisorStateCreateInfoEXT vdiv_state = {};
   if (state->divisors_present) {
       vertex_input_state.pNext = &vdiv_state;
       vdiv_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_DIVISOR_STATE_CREATE_INFO_EXT;
       vdiv_state.vertexBindingDivisorCount = state->divisors_present;
       vdiv_state.pVertexBindingDivisors = state->divisors;
   }

   VkPipelineInputAssemblyStateCreateInfo primitive_state = {};
   primitive_state.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
   primitive_state.topology = primitive_topology;
   switch (primitive_topology) {
   case VK_PRIMITIVE_TOPOLOGY_POINT_LIST:
   case VK_PRIMITIVE_TOPOLOGY_LINE_LIST:
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
   case VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY:
   case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY:
   case VK_PRIMITIVE_TOPOLOGY_PATCH_LIST:
      if (state->primitive_restart)
         debug_printf("restart_index set with unsupported primitive topology %u\n", primitive_topology);
      primitive_state.primitiveRestartEnable = VK_FALSE;
      break;
   default:
      primitive_state.primitiveRestartEnable = state->primitive_restart ? VK_TRUE : VK_FALSE;
   }

   VkPipelineColorBlendStateCreateInfo blend_state = {};
   blend_state.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
   blend_state.pAttachments = state->blend_state->attachments;
   blend_state.attachmentCount = state->num_attachments;
   blend_state.logicOpEnable = state->blend_state->logicop_enable;
   blend_state.logicOp = state->blend_state->logicop_func;

   VkPipelineMultisampleStateCreateInfo ms_state = {};
   ms_state.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
   ms_state.rasterizationSamples = state->rast_samples;
   ms_state.alphaToCoverageEnable = state->blend_state->alpha_to_coverage;
   ms_state.alphaToOneEnable = state->blend_state->alpha_to_one;
   ms_state.pSampleMask = state->sample_mask ? &state->sample_mask : NULL;

   VkPipelineViewportStateCreateInfo viewport_state = {};
   viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
   viewport_state.viewportCount = state->num_viewports;
   viewport_state.pViewports = NULL;
   viewport_state.scissorCount = state->num_viewports;
   viewport_state.pScissors = NULL;

   VkPipelineRasterizationStateCreateInfo rast_state = {};
   rast_state.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;

   rast_state.depthClampEnable = state->rast_state->depth_clamp;
   rast_state.rasterizerDiscardEnable = state->rast_state->rasterizer_discard;
   rast_state.polygonMode = state->rast_state->polygon_mode;
   rast_state.cullMode = state->rast_state->cull_mode;
   rast_state.frontFace = state->rast_state->front_face;

   rast_state.depthBiasEnable = VK_TRUE;
   rast_state.depthBiasConstantFactor = 0.0;
   rast_state.depthBiasClamp = 0.0;
   rast_state.depthBiasSlopeFactor = 0.0;
   rast_state.lineWidth = 1.0f;

   VkPipelineDepthStencilStateCreateInfo depth_stencil_state = {};
   depth_stencil_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
   depth_stencil_state.depthTestEnable = state->depth_stencil_alpha_state->depth_test;
   depth_stencil_state.depthCompareOp = state->depth_stencil_alpha_state->depth_compare_op;
   depth_stencil_state.depthBoundsTestEnable = state->depth_stencil_alpha_state->depth_bounds_test;
   depth_stencil_state.minDepthBounds = state->depth_stencil_alpha_state->min_depth_bounds;
   depth_stencil_state.maxDepthBounds = state->depth_stencil_alpha_state->max_depth_bounds;
   depth_stencil_state.stencilTestEnable = state->depth_stencil_alpha_state->stencil_test;
   depth_stencil_state.front = state->depth_stencil_alpha_state->stencil_front;
   depth_stencil_state.back = state->depth_stencil_alpha_state->stencil_back;
   depth_stencil_state.depthWriteEnable = state->depth_stencil_alpha_state->depth_write;

   VkDynamicState dynamicStateEnables[] = {
      VK_DYNAMIC_STATE_VIEWPORT,
      VK_DYNAMIC_STATE_SCISSOR,
      VK_DYNAMIC_STATE_LINE_WIDTH,
      VK_DYNAMIC_STATE_DEPTH_BIAS,
      VK_DYNAMIC_STATE_BLEND_CONSTANTS,
      VK_DYNAMIC_STATE_STENCIL_REFERENCE,
   };

   VkPipelineDynamicStateCreateInfo pipelineDynamicStateCreateInfo = {};
   pipelineDynamicStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
   pipelineDynamicStateCreateInfo.pDynamicStates = dynamicStateEnables;
   pipelineDynamicStateCreateInfo.dynamicStateCount = ARRAY_SIZE(dynamicStateEnables);

   VkGraphicsPipelineCreateInfo pci = {};
   pci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
   pci.layout = prog->layout;
   pci.renderPass = state->render_pass->render_pass;
   pci.pVertexInputState = &vertex_input_state;
   pci.pInputAssemblyState = &primitive_state;
   pci.pRasterizationState = &rast_state;
   pci.pColorBlendState = &blend_state;
   pci.pMultisampleState = &ms_state;
   pci.pViewportState = &viewport_state;
   pci.pDepthStencilState = &depth_stencil_state;
   pci.pDynamicState = &pipelineDynamicStateCreateInfo;

   VkPipelineTessellationStateCreateInfo tci = {};
   VkPipelineTessellationDomainOriginStateCreateInfo tdci = {};
   if (prog->shaders[PIPE_SHADER_TESS_CTRL] && prog->shaders[PIPE_SHADER_TESS_EVAL]) {
      tci.sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO;
      tci.patchControlPoints = state->vertices_per_patch;
      pci.pTessellationState = &tci;
      tci.pNext = &tdci;
      tdci.sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_DOMAIN_ORIGIN_STATE_CREATE_INFO;
      tdci.domainOrigin = VK_TESSELLATION_DOMAIN_ORIGIN_LOWER_LEFT;
   }

   VkPipelineShaderStageCreateInfo shader_stages[ZINK_SHADER_COUNT];
   uint32_t num_stages = 0;
   for (int i = 0; i < ZINK_SHADER_COUNT; ++i) {
      if (!prog->modules[i])
         continue;

      VkPipelineShaderStageCreateInfo stage = {};
      stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
      stage.stage = zink_shader_stage(i);
      stage.module = prog->modules[i]->shader;
      stage.pName = "main";
      shader_stages[num_stages++] = stage;
   }
   assert(num_stages > 0);

   pci.pStages = shader_stages;
   pci.stageCount = num_stages;

   VkPipeline pipeline;
   if (vkCreateGraphicsPipelines(screen->dev, VK_NULL_HANDLE, 1, &pci,
                                 NULL, &pipeline) != VK_SUCCESS) {
      debug_printf("vkCreateGraphicsPipelines failed\n");
      return VK_NULL_HANDLE;
   }

   return pipeline;
}
