/*
 * Copyright © 2016 Intel Corporation
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

#include <assert.h>
#include <stdbool.h>

#include "radv_meta.h"
#include "radv_private.h"
#include "sid.h"

enum radv_color_op {
   FAST_CLEAR_ELIMINATE,
   FMASK_DECOMPRESS,
   DCC_DECOMPRESS,
};

static nir_shader *
build_dcc_decompress_compute_shader(struct radv_device *dev)
{
   const struct glsl_type *img_type = glsl_image_type(GLSL_SAMPLER_DIM_2D, false, GLSL_TYPE_FLOAT);

   nir_builder b =
      nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, NULL, "dcc_decompress_compute");

   /* We need at least 16/16/1 to cover an entire DCC block in a single workgroup. */
   b.shader->info.workgroup_size[0] = 16;
   b.shader->info.workgroup_size[1] = 16;
   b.shader->info.workgroup_size[2] = 1;
   nir_variable *input_img = nir_variable_create(b.shader, nir_var_uniform, img_type, "in_img");
   input_img->data.descriptor_set = 0;
   input_img->data.binding = 0;

   nir_variable *output_img = nir_variable_create(b.shader, nir_var_uniform, img_type, "out_img");
   output_img->data.descriptor_set = 0;
   output_img->data.binding = 1;

   nir_ssa_def *invoc_id = nir_load_local_invocation_id(&b);
   nir_ssa_def *wg_id = nir_load_workgroup_id(&b, 32);
   nir_ssa_def *block_size =
      nir_imm_ivec4(&b, b.shader->info.workgroup_size[0], b.shader->info.workgroup_size[1],
                    b.shader->info.workgroup_size[2], 0);

   nir_ssa_def *global_id = nir_iadd(&b, nir_imul(&b, wg_id, block_size), invoc_id);

   nir_ssa_def *data = nir_image_deref_load(
      &b, 4, 32, &nir_build_deref_var(&b, input_img)->dest.ssa, global_id, nir_ssa_undef(&b, 1, 32),
      nir_imm_int(&b, 0), .image_dim = GLSL_SAMPLER_DIM_2D);

   /* We need a NIR_SCOPE_DEVICE memory_scope because ACO will avoid
    * creating a vmcnt(0) because it expects the L1 cache to keep memory
    * operations in-order for the same workgroup. The vmcnt(0) seems
    * necessary however. */
   nir_scoped_barrier(&b, .execution_scope = NIR_SCOPE_WORKGROUP, .memory_scope = NIR_SCOPE_DEVICE,
                      .memory_semantics = NIR_MEMORY_ACQ_REL, .memory_modes = nir_var_mem_ssbo);

   nir_image_deref_store(&b, &nir_build_deref_var(&b, output_img)->dest.ssa, global_id,
                         nir_ssa_undef(&b, 1, 32), data, nir_imm_int(&b, 0),
                         .image_dim = GLSL_SAMPLER_DIM_2D);
   return b.shader;
}

static VkResult
create_dcc_compress_compute(struct radv_device *device)
{
   VkResult result = VK_SUCCESS;
   nir_shader *cs = build_dcc_decompress_compute_shader(device);

   VkDescriptorSetLayoutCreateInfo ds_create_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR,
      .bindingCount = 2,
      .pBindings = (VkDescriptorSetLayoutBinding[]){
         {.binding = 0,
          .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
          .descriptorCount = 1,
          .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
          .pImmutableSamplers = NULL},
         {.binding = 1,
          .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
          .descriptorCount = 1,
          .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
          .pImmutableSamplers = NULL},
      }};

   result = radv_CreateDescriptorSetLayout(
      radv_device_to_handle(device), &ds_create_info, &device->meta_state.alloc,
      &device->meta_state.fast_clear_flush.dcc_decompress_compute_ds_layout);
   if (result != VK_SUCCESS)
      goto cleanup;

   VkPipelineLayoutCreateInfo pl_create_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1,
      .pSetLayouts = &device->meta_state.fast_clear_flush.dcc_decompress_compute_ds_layout,
      .pushConstantRangeCount = 0,
      .pPushConstantRanges = NULL,
   };

   result = radv_CreatePipelineLayout(
      radv_device_to_handle(device), &pl_create_info, &device->meta_state.alloc,
      &device->meta_state.fast_clear_flush.dcc_decompress_compute_p_layout);
   if (result != VK_SUCCESS)
      goto cleanup;

   /* compute shader */

   VkPipelineShaderStageCreateInfo pipeline_shader_stage = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_COMPUTE_BIT,
      .module = vk_shader_module_handle_from_nir(cs),
      .pName = "main",
      .pSpecializationInfo = NULL,
   };

   VkComputePipelineCreateInfo vk_pipeline_info = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage = pipeline_shader_stage,
      .flags = 0,
      .layout = device->meta_state.fast_clear_flush.dcc_decompress_compute_p_layout,
   };

   result = radv_CreateComputePipelines(
      radv_device_to_handle(device), radv_pipeline_cache_to_handle(&device->meta_state.cache), 1,
      &vk_pipeline_info, NULL,
      &device->meta_state.fast_clear_flush.dcc_decompress_compute_pipeline);
   if (result != VK_SUCCESS)
      goto cleanup;

cleanup:
   ralloc_free(cs);
   return result;
}

static VkResult
create_pass(struct radv_device *device)
{
   VkResult result;
   VkDevice device_h = radv_device_to_handle(device);
   const VkAllocationCallbacks *alloc = &device->meta_state.alloc;
   VkAttachmentDescription2 attachment;

   attachment.sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2;
   attachment.pNext = NULL;
   attachment.format = VK_FORMAT_UNDEFINED;
   attachment.samples = 1;
   attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
   attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
   attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
   attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

   result = radv_CreateRenderPass2(
      device_h,
      &(VkRenderPassCreateInfo2){
         .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2,
         .attachmentCount = 1,
         .pAttachments = &attachment,
         .subpassCount = 1,
         .pSubpasses =
            &(VkSubpassDescription2){
               .sType = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2,
               .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
               .inputAttachmentCount = 0,
               .colorAttachmentCount = 1,
               .pColorAttachments =
                  (VkAttachmentReference2[]){
                     {
                        .sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2,
                        .attachment = 0,
                        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                     },
                  },
               .pResolveAttachments = NULL,
               .pDepthStencilAttachment =
                  &(VkAttachmentReference2){
                     .sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2,
                     .attachment = VK_ATTACHMENT_UNUSED,
                  },
               .preserveAttachmentCount = 0,
               .pPreserveAttachments = NULL,
            },
         .dependencyCount = 2,
         .pDependencies =
            (VkSubpassDependency2[]){{.sType = VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2,
                                      .srcSubpass = VK_SUBPASS_EXTERNAL,
                                      .dstSubpass = 0,
                                      .srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                      .dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                      .srcAccessMask = 0,
                                      .dstAccessMask = 0,
                                      .dependencyFlags = 0},
                                     {.sType = VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2,
                                      .srcSubpass = 0,
                                      .dstSubpass = VK_SUBPASS_EXTERNAL,
                                      .srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                      .dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                      .srcAccessMask = 0,
                                      .dstAccessMask = 0,
                                      .dependencyFlags = 0}},
      },
      alloc, &device->meta_state.fast_clear_flush.pass);

   return result;
}

static VkResult
create_pipeline_layout(struct radv_device *device, VkPipelineLayout *layout)
{
   VkPipelineLayoutCreateInfo pl_create_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 0,
      .pSetLayouts = NULL,
      .pushConstantRangeCount = 0,
      .pPushConstantRanges = NULL,
   };

   return radv_CreatePipelineLayout(radv_device_to_handle(device), &pl_create_info,
                                    &device->meta_state.alloc, layout);
}

static VkResult
create_pipeline(struct radv_device *device, VkShaderModule vs_module_h, VkPipelineLayout layout)
{
   VkResult result;
   VkDevice device_h = radv_device_to_handle(device);

   nir_shader *fs_module = radv_meta_build_nir_fs_noop();

   if (!fs_module) {
      /* XXX: Need more accurate error */
      result = VK_ERROR_OUT_OF_HOST_MEMORY;
      goto cleanup;
   }

   const VkPipelineShaderStageCreateInfo stages[2] = {
      {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_VERTEX_BIT,
         .module = vs_module_h,
         .pName = "main",
      },
      {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
         .module = vk_shader_module_handle_from_nir(fs_module),
         .pName = "main",
      },
   };

   const VkPipelineVertexInputStateCreateInfo vi_state = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = 0,
      .vertexAttributeDescriptionCount = 0,
   };

   const VkPipelineInputAssemblyStateCreateInfo ia_state = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
      .primitiveRestartEnable = false,
   };

   const VkPipelineColorBlendStateCreateInfo blend_state = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .logicOpEnable = false,
      .attachmentCount = 1,
      .pAttachments = (VkPipelineColorBlendAttachmentState[]){
         {
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
         },
      }};
   const VkPipelineRasterizationStateCreateInfo rs_state = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .depthClampEnable = false,
      .rasterizerDiscardEnable = false,
      .polygonMode = VK_POLYGON_MODE_FILL,
      .cullMode = VK_CULL_MODE_NONE,
      .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
   };

   result = radv_graphics_pipeline_create(
      device_h, radv_pipeline_cache_to_handle(&device->meta_state.cache),
      &(VkGraphicsPipelineCreateInfo){
         .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
         .stageCount = 2,
         .pStages = stages,

         .pVertexInputState = &vi_state,
         .pInputAssemblyState = &ia_state,

         .pViewportState =
            &(VkPipelineViewportStateCreateInfo){
               .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
               .viewportCount = 1,
               .scissorCount = 1,
            },
         .pRasterizationState = &rs_state,
         .pMultisampleState =
            &(VkPipelineMultisampleStateCreateInfo){
               .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
               .rasterizationSamples = 1,
               .sampleShadingEnable = false,
               .pSampleMask = NULL,
               .alphaToCoverageEnable = false,
               .alphaToOneEnable = false,
            },
         .pColorBlendState = &blend_state,
         .pDynamicState =
            &(VkPipelineDynamicStateCreateInfo){
               .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
               .dynamicStateCount = 2,
               .pDynamicStates =
                  (VkDynamicState[]){
                     VK_DYNAMIC_STATE_VIEWPORT,
                     VK_DYNAMIC_STATE_SCISSOR,
                  },
            },
         .layout = layout,
         .renderPass = device->meta_state.fast_clear_flush.pass,
         .subpass = 0,
      },
      &(struct radv_graphics_pipeline_create_info){
         .use_rectlist = true,
         .custom_blend_mode = V_028808_CB_ELIMINATE_FAST_CLEAR,
      },
      &device->meta_state.alloc, &device->meta_state.fast_clear_flush.cmask_eliminate_pipeline);
   if (result != VK_SUCCESS)
      goto cleanup;

   result = radv_graphics_pipeline_create(
      device_h, radv_pipeline_cache_to_handle(&device->meta_state.cache),
      &(VkGraphicsPipelineCreateInfo){
         .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
         .stageCount = 2,
         .pStages = stages,

         .pVertexInputState = &vi_state,
         .pInputAssemblyState = &ia_state,

         .pViewportState =
            &(VkPipelineViewportStateCreateInfo){
               .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
               .viewportCount = 1,
               .scissorCount = 1,
            },
         .pRasterizationState = &rs_state,
         .pMultisampleState =
            &(VkPipelineMultisampleStateCreateInfo){
               .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
               .rasterizationSamples = 1,
               .sampleShadingEnable = false,
               .pSampleMask = NULL,
               .alphaToCoverageEnable = false,
               .alphaToOneEnable = false,
            },
         .pColorBlendState = &blend_state,
         .pDynamicState =
            &(VkPipelineDynamicStateCreateInfo){
               .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
               .dynamicStateCount = 2,
               .pDynamicStates =
                  (VkDynamicState[]){
                     VK_DYNAMIC_STATE_VIEWPORT,
                     VK_DYNAMIC_STATE_SCISSOR,
                  },
            },
         .layout = layout,
         .renderPass = device->meta_state.fast_clear_flush.pass,
         .subpass = 0,
      },
      &(struct radv_graphics_pipeline_create_info){
         .use_rectlist = true,
         .custom_blend_mode = V_028808_CB_FMASK_DECOMPRESS,
      },
      &device->meta_state.alloc, &device->meta_state.fast_clear_flush.fmask_decompress_pipeline);
   if (result != VK_SUCCESS)
      goto cleanup;

   result = radv_graphics_pipeline_create(
      device_h, radv_pipeline_cache_to_handle(&device->meta_state.cache),
      &(VkGraphicsPipelineCreateInfo){
         .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
         .stageCount = 2,
         .pStages = stages,

         .pVertexInputState = &vi_state,
         .pInputAssemblyState = &ia_state,

         .pViewportState =
            &(VkPipelineViewportStateCreateInfo){
               .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
               .viewportCount = 1,
               .scissorCount = 1,
            },
         .pRasterizationState = &rs_state,
         .pMultisampleState =
            &(VkPipelineMultisampleStateCreateInfo){
               .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
               .rasterizationSamples = 1,
               .sampleShadingEnable = false,
               .pSampleMask = NULL,
               .alphaToCoverageEnable = false,
               .alphaToOneEnable = false,
            },
         .pColorBlendState = &blend_state,
         .pDynamicState =
            &(VkPipelineDynamicStateCreateInfo){
               .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
               .dynamicStateCount = 2,
               .pDynamicStates =
                  (VkDynamicState[]){
                     VK_DYNAMIC_STATE_VIEWPORT,
                     VK_DYNAMIC_STATE_SCISSOR,
                  },
            },
         .layout = layout,
         .renderPass = device->meta_state.fast_clear_flush.pass,
         .subpass = 0,
      },
      &(struct radv_graphics_pipeline_create_info){
         .use_rectlist = true,
         .custom_blend_mode = V_028808_CB_DCC_DECOMPRESS,
      },
      &device->meta_state.alloc, &device->meta_state.fast_clear_flush.dcc_decompress_pipeline);
   if (result != VK_SUCCESS)
      goto cleanup;

   goto cleanup;

cleanup:
   ralloc_free(fs_module);
   return result;
}

void
radv_device_finish_meta_fast_clear_flush_state(struct radv_device *device)
{
   struct radv_meta_state *state = &device->meta_state;

   radv_DestroyPipeline(radv_device_to_handle(device),
                        state->fast_clear_flush.dcc_decompress_pipeline, &state->alloc);
   radv_DestroyPipeline(radv_device_to_handle(device),
                        state->fast_clear_flush.fmask_decompress_pipeline, &state->alloc);
   radv_DestroyPipeline(radv_device_to_handle(device),
                        state->fast_clear_flush.cmask_eliminate_pipeline, &state->alloc);
   radv_DestroyRenderPass(radv_device_to_handle(device), state->fast_clear_flush.pass,
                          &state->alloc);
   radv_DestroyPipelineLayout(radv_device_to_handle(device), state->fast_clear_flush.p_layout,
                              &state->alloc);

   radv_DestroyPipeline(radv_device_to_handle(device),
                        state->fast_clear_flush.dcc_decompress_compute_pipeline, &state->alloc);
   radv_DestroyPipelineLayout(radv_device_to_handle(device),
                              state->fast_clear_flush.dcc_decompress_compute_p_layout,
                              &state->alloc);
   radv_DestroyDescriptorSetLayout(radv_device_to_handle(device),
                                   state->fast_clear_flush.dcc_decompress_compute_ds_layout,
                                   &state->alloc);
}

static VkResult
radv_device_init_meta_fast_clear_flush_state_internal(struct radv_device *device)
{
   VkResult res = VK_SUCCESS;

   mtx_lock(&device->meta_state.mtx);
   if (device->meta_state.fast_clear_flush.cmask_eliminate_pipeline) {
      mtx_unlock(&device->meta_state.mtx);
      return VK_SUCCESS;
   }

   nir_shader *vs_module = radv_meta_build_nir_vs_generate_vertices();
   if (!vs_module) {
      /* XXX: Need more accurate error */
      res = VK_ERROR_OUT_OF_HOST_MEMORY;
      goto fail;
   }

   res = create_pass(device);
   if (res != VK_SUCCESS)
      goto fail;

   res = create_pipeline_layout(device, &device->meta_state.fast_clear_flush.p_layout);
   if (res != VK_SUCCESS)
      goto fail;

   VkShaderModule vs_module_h = vk_shader_module_handle_from_nir(vs_module);
   res = create_pipeline(device, vs_module_h, device->meta_state.fast_clear_flush.p_layout);
   if (res != VK_SUCCESS)
      goto fail;

   res = create_dcc_compress_compute(device);
   if (res != VK_SUCCESS)
      goto fail;

   goto cleanup;

fail:
   radv_device_finish_meta_fast_clear_flush_state(device);

cleanup:
   ralloc_free(vs_module);
   mtx_unlock(&device->meta_state.mtx);

   return res;
}

VkResult
radv_device_init_meta_fast_clear_flush_state(struct radv_device *device, bool on_demand)
{
   if (on_demand)
      return VK_SUCCESS;

   return radv_device_init_meta_fast_clear_flush_state_internal(device);
}

static void
radv_emit_set_predication_state_from_image(struct radv_cmd_buffer *cmd_buffer,
                                           struct radv_image *image, uint64_t pred_offset,
                                           bool value)
{
   uint64_t va = 0;

   if (value) {
      va = radv_buffer_get_va(image->bo) + image->offset;
      va += pred_offset;
   }

   si_emit_set_predication_state(cmd_buffer, true, PREDICATION_OP_BOOL64, va);
}

static void
radv_process_color_image_layer(struct radv_cmd_buffer *cmd_buffer, struct radv_image *image,
                               const VkImageSubresourceRange *range, int level, int layer,
                               bool flush_cb)
{
   struct radv_device *device = cmd_buffer->device;
   struct radv_image_view iview;
   uint32_t width, height;

   width = radv_minify(image->info.width, range->baseMipLevel + level);
   height = radv_minify(image->info.height, range->baseMipLevel + level);

   radv_image_view_init(&iview, device,
                        &(VkImageViewCreateInfo){
                           .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                           .image = radv_image_to_handle(image),
                           .viewType = radv_meta_get_view_type(image),
                           .format = image->vk_format,
                           .subresourceRange =
                              {
                                 .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                 .baseMipLevel = range->baseMipLevel + level,
                                 .levelCount = 1,
                                 .baseArrayLayer = range->baseArrayLayer + layer,
                                 .layerCount = 1,
                              },
                        },
                        NULL);

   VkFramebuffer fb_h;
   radv_CreateFramebuffer(
      radv_device_to_handle(device),
      &(VkFramebufferCreateInfo){.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                                 .attachmentCount = 1,
                                 .pAttachments = (VkImageView[]){radv_image_view_to_handle(&iview)},
                                 .width = width,
                                 .height = height,
                                 .layers = 1},
      &cmd_buffer->pool->alloc, &fb_h);

   radv_cmd_buffer_begin_render_pass(cmd_buffer,
                                     &(VkRenderPassBeginInfo){
                                        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                                        .renderPass = device->meta_state.fast_clear_flush.pass,
                                        .framebuffer = fb_h,
                                        .renderArea = {.offset =
                                                          {
                                                             0,
                                                             0,
                                                          },
                                                       .extent =
                                                          {
                                                             width,
                                                             height,
                                                          }},
                                        .clearValueCount = 0,
                                        .pClearValues = NULL,
                                     },
                                     NULL);

   radv_cmd_buffer_set_subpass(cmd_buffer, &cmd_buffer->state.pass->subpasses[0]);

   if (flush_cb)
      cmd_buffer->state.flush_bits |=
         radv_dst_access_flush(cmd_buffer, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, image);

   radv_CmdDraw(radv_cmd_buffer_to_handle(cmd_buffer), 3, 1, 0, 0);

   if (flush_cb)
      cmd_buffer->state.flush_bits |=
         radv_src_access_flush(cmd_buffer, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, image);

   radv_cmd_buffer_end_render_pass(cmd_buffer);

   radv_DestroyFramebuffer(radv_device_to_handle(device), fb_h, &cmd_buffer->pool->alloc);
}

static void
radv_process_color_image(struct radv_cmd_buffer *cmd_buffer, struct radv_image *image,
                         const VkImageSubresourceRange *subresourceRange, enum radv_color_op op)
{
   struct radv_device *device = cmd_buffer->device;
   struct radv_meta_saved_state saved_state;
   bool old_predicating = false;
   bool flush_cb = false;
   uint64_t pred_offset;
   VkPipeline *pipeline;

   switch (op) {
   case FAST_CLEAR_ELIMINATE:
      pipeline = &device->meta_state.fast_clear_flush.cmask_eliminate_pipeline;
      pred_offset = image->fce_pred_offset;
      break;
   case FMASK_DECOMPRESS:
      pipeline = &device->meta_state.fast_clear_flush.fmask_decompress_pipeline;
      pred_offset = 0; /* FMASK_DECOMPRESS is never predicated. */

      /* Flushing CB is required before and after FMASK_DECOMPRESS. */
      flush_cb = true;
      break;
   case DCC_DECOMPRESS:
      pipeline = &device->meta_state.fast_clear_flush.dcc_decompress_pipeline;
      pred_offset = image->dcc_pred_offset;

      /* Flushing CB is required before and after DCC_DECOMPRESS. */
      flush_cb = true;
      break;
   default:
      unreachable("Invalid color op");
   }

   if (radv_dcc_enabled(image, subresourceRange->baseMipLevel) &&
       (image->info.array_size != radv_get_layerCount(image, subresourceRange) ||
        subresourceRange->baseArrayLayer != 0)) {
      /* Only use predication if the image has DCC with mipmaps or
       * if the range of layers covers the whole image because the
       * predication is based on mip level.
       */
      pred_offset = 0;
   }

   if (!*pipeline) {
      VkResult ret;

      ret = radv_device_init_meta_fast_clear_flush_state_internal(device);
      if (ret != VK_SUCCESS) {
         cmd_buffer->record_result = ret;
         return;
      }
   }

   radv_meta_save(&saved_state, cmd_buffer, RADV_META_SAVE_GRAPHICS_PIPELINE | RADV_META_SAVE_PASS);

   if (pred_offset) {
      pred_offset += 8 * subresourceRange->baseMipLevel;

      old_predicating = cmd_buffer->state.predicating;

      radv_emit_set_predication_state_from_image(cmd_buffer, image, pred_offset, true);
      cmd_buffer->state.predicating = true;
   }

   radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer), VK_PIPELINE_BIND_POINT_GRAPHICS,
                        *pipeline);

   for (uint32_t l = 0; l < radv_get_levelCount(image, subresourceRange); ++l) {
      uint32_t width, height;

      /* Do not decompress levels without DCC. */
      if (op == DCC_DECOMPRESS && !radv_dcc_enabled(image, subresourceRange->baseMipLevel + l))
         continue;

      width = radv_minify(image->info.width, subresourceRange->baseMipLevel + l);
      height = radv_minify(image->info.height, subresourceRange->baseMipLevel + l);

      radv_CmdSetViewport(radv_cmd_buffer_to_handle(cmd_buffer), 0, 1,
                          &(VkViewport){.x = 0,
                                        .y = 0,
                                        .width = width,
                                        .height = height,
                                        .minDepth = 0.0f,
                                        .maxDepth = 1.0f});

      radv_CmdSetScissor(radv_cmd_buffer_to_handle(cmd_buffer), 0, 1,
                         &(VkRect2D){
                            .offset = {0, 0},
                            .extent = {width, height},
                         });

      for (uint32_t s = 0; s < radv_get_layerCount(image, subresourceRange); s++) {
         radv_process_color_image_layer(cmd_buffer, image, subresourceRange, l, s, flush_cb);
      }
   }

   cmd_buffer->state.flush_bits |=
      RADV_CMD_FLAG_FLUSH_AND_INV_CB | RADV_CMD_FLAG_FLUSH_AND_INV_CB_META;

   if (pred_offset) {
      pred_offset += 8 * subresourceRange->baseMipLevel;

      cmd_buffer->state.predicating = old_predicating;

      radv_emit_set_predication_state_from_image(cmd_buffer, image, pred_offset, false);

      if (cmd_buffer->state.predication_type != -1) {
         /* Restore previous conditional rendering user state. */
         si_emit_set_predication_state(cmd_buffer, cmd_buffer->state.predication_type,
                                       cmd_buffer->state.predication_op,
                                       cmd_buffer->state.predication_va);
      }
   }

   radv_meta_restore(&saved_state, cmd_buffer);

   /* Clear the image's fast-clear eliminate predicate because FMASK_DECOMPRESS and DCC_DECOMPRESS
    * also perform a fast-clear eliminate.
    */
   radv_update_fce_metadata(cmd_buffer, image, subresourceRange, false);

   /* Mark the image as being decompressed. */
   if (op == DCC_DECOMPRESS)
      radv_update_dcc_metadata(cmd_buffer, image, subresourceRange, false);
}

static void
radv_fast_clear_eliminate(struct radv_cmd_buffer *cmd_buffer, struct radv_image *image,
                          const VkImageSubresourceRange *subresourceRange)
{
   struct radv_barrier_data barrier = {0};

   barrier.layout_transitions.fast_clear_eliminate = 1;
   radv_describe_layout_transition(cmd_buffer, &barrier);

   radv_process_color_image(cmd_buffer, image, subresourceRange, FAST_CLEAR_ELIMINATE);
}

static void
radv_fmask_decompress(struct radv_cmd_buffer *cmd_buffer, struct radv_image *image,
                      const VkImageSubresourceRange *subresourceRange)
{
   struct radv_barrier_data barrier = {0};

   barrier.layout_transitions.fmask_decompress = 1;
   radv_describe_layout_transition(cmd_buffer, &barrier);

   radv_process_color_image(cmd_buffer, image, subresourceRange, FMASK_DECOMPRESS);
}

void
radv_fast_clear_flush_image_inplace(struct radv_cmd_buffer *cmd_buffer, struct radv_image *image,
                                    const VkImageSubresourceRange *subresourceRange)
{
   if (radv_image_has_fmask(image) && !image->tc_compatible_cmask) {
      if (radv_image_has_dcc(image) && radv_image_has_cmask(image)) {
         /* MSAA images with DCC and CMASK might have been fast-cleared and might require a FCE but
          * FMASK_DECOMPRESS can't eliminate DCC fast clears.
          */
         radv_fast_clear_eliminate(cmd_buffer, image, subresourceRange);
      }

      radv_fmask_decompress(cmd_buffer, image, subresourceRange);
   } else {
      /* Skip fast clear eliminate for images that support comp-to-single fast clears. */
      if (image->support_comp_to_single)
         return;

      radv_fast_clear_eliminate(cmd_buffer, image, subresourceRange);
   }
}

static void
radv_decompress_dcc_compute(struct radv_cmd_buffer *cmd_buffer, struct radv_image *image,
                            const VkImageSubresourceRange *subresourceRange)
{
   struct radv_meta_saved_state saved_state;
   struct radv_image_view load_iview = {0};
   struct radv_image_view store_iview = {0};
   struct radv_device *device = cmd_buffer->device;

   cmd_buffer->state.flush_bits |=
      radv_dst_access_flush(cmd_buffer, VK_ACCESS_SHADER_WRITE_BIT, image);

   if (!cmd_buffer->device->meta_state.fast_clear_flush.cmask_eliminate_pipeline) {
      VkResult ret = radv_device_init_meta_fast_clear_flush_state_internal(cmd_buffer->device);
      if (ret != VK_SUCCESS) {
         cmd_buffer->record_result = ret;
         return;
      }
   }

   radv_meta_save(&saved_state, cmd_buffer,
                  RADV_META_SAVE_DESCRIPTORS | RADV_META_SAVE_COMPUTE_PIPELINE);

   radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer), VK_PIPELINE_BIND_POINT_COMPUTE,
                        device->meta_state.fast_clear_flush.dcc_decompress_compute_pipeline);

   for (uint32_t l = 0; l < radv_get_levelCount(image, subresourceRange); l++) {
      uint32_t width, height;

      /* Do not decompress levels without DCC. */
      if (!radv_dcc_enabled(image, subresourceRange->baseMipLevel + l))
         continue;

      width = radv_minify(image->info.width, subresourceRange->baseMipLevel + l);
      height = radv_minify(image->info.height, subresourceRange->baseMipLevel + l);

      for (uint32_t s = 0; s < radv_get_layerCount(image, subresourceRange); s++) {
         radv_image_view_init(
            &load_iview, cmd_buffer->device,
            &(VkImageViewCreateInfo){
               .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
               .image = radv_image_to_handle(image),
               .viewType = VK_IMAGE_VIEW_TYPE_2D,
               .format = image->vk_format,
               .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                    .baseMipLevel = subresourceRange->baseMipLevel + l,
                                    .levelCount = 1,
                                    .baseArrayLayer = subresourceRange->baseArrayLayer + s,
                                    .layerCount = 1},
            },
            &(struct radv_image_view_extra_create_info){.enable_compression = true});
         radv_image_view_init(
            &store_iview, cmd_buffer->device,
            &(VkImageViewCreateInfo){
               .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
               .image = radv_image_to_handle(image),
               .viewType = VK_IMAGE_VIEW_TYPE_2D,
               .format = image->vk_format,
               .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                    .baseMipLevel = subresourceRange->baseMipLevel + l,
                                    .levelCount = 1,
                                    .baseArrayLayer = subresourceRange->baseArrayLayer + s,
                                    .layerCount = 1},
            },
            &(struct radv_image_view_extra_create_info){.disable_compression = true});

         radv_meta_push_descriptor_set(
            cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
            device->meta_state.fast_clear_flush.dcc_decompress_compute_p_layout, 0, /* set */
            2, /* descriptorWriteCount */
            (VkWriteDescriptorSet[]){{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                      .dstBinding = 0,
                                      .dstArrayElement = 0,
                                      .descriptorCount = 1,
                                      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                      .pImageInfo =
                                         (VkDescriptorImageInfo[]){
                                            {
                                               .sampler = VK_NULL_HANDLE,
                                               .imageView = radv_image_view_to_handle(&load_iview),
                                               .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                                            },
                                         }},
                                     {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                      .dstBinding = 1,
                                      .dstArrayElement = 0,
                                      .descriptorCount = 1,
                                      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                      .pImageInfo = (VkDescriptorImageInfo[]){
                                         {
                                            .sampler = VK_NULL_HANDLE,
                                            .imageView = radv_image_view_to_handle(&store_iview),
                                            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                                         },
                                      }}});

         radv_unaligned_dispatch(cmd_buffer, width, height, 1);
      }
   }

   /* Mark this image as actually being decompressed. */
   radv_update_dcc_metadata(cmd_buffer, image, subresourceRange, false);

   radv_meta_restore(&saved_state, cmd_buffer);

   cmd_buffer->state.flush_bits |=
      RADV_CMD_FLAG_CS_PARTIAL_FLUSH | RADV_CMD_FLAG_INV_VCACHE |
      radv_src_access_flush(cmd_buffer, VK_ACCESS_SHADER_WRITE_BIT, image);

   /* Initialize the DCC metadata as "fully expanded". */
   cmd_buffer->state.flush_bits |= radv_init_dcc(cmd_buffer, image, subresourceRange, 0xffffffff);
}

void
radv_decompress_dcc(struct radv_cmd_buffer *cmd_buffer, struct radv_image *image,
                    const VkImageSubresourceRange *subresourceRange)
{
   struct radv_barrier_data barrier = {0};

   barrier.layout_transitions.dcc_decompress = 1;
   radv_describe_layout_transition(cmd_buffer, &barrier);

   if (cmd_buffer->queue_family_index == RADV_QUEUE_GENERAL)
      radv_process_color_image(cmd_buffer, image, subresourceRange, DCC_DECOMPRESS);
   else
      radv_decompress_dcc_compute(cmd_buffer, image, subresourceRange);
}
