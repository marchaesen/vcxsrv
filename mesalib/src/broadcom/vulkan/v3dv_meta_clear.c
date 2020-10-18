/*
 * Copyright © 2020 Raspberry Pi
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

#include "v3dv_private.h"

#include "broadcom/cle/v3dx_pack.h"
#include "compiler/nir/nir_builder.h"
#include "vk_format_info.h"
#include "util/u_pack_color.h"

static void
destroy_color_clear_pipeline(VkDevice _device,
                             uint64_t pipeline,
                             VkAllocationCallbacks *alloc)
{
   struct v3dv_meta_color_clear_pipeline *p =
      (struct v3dv_meta_color_clear_pipeline *) (uintptr_t) pipeline;
   v3dv_DestroyPipeline(_device, p->pipeline, alloc);
   if (p->cached)
      v3dv_DestroyRenderPass(_device, p->pass, alloc);
   vk_free(alloc, p);
}

static void
destroy_depth_clear_pipeline(VkDevice _device,
                             struct v3dv_meta_depth_clear_pipeline *p,
                             VkAllocationCallbacks *alloc)
{
   v3dv_DestroyPipeline(_device, p->pipeline, alloc);
   vk_free(alloc, p);
}

void
v3dv_meta_clear_init(struct v3dv_device *device)
{
   device->meta.color_clear.cache =
      _mesa_hash_table_create(NULL, u64_hash, u64_compare);

   device->meta.depth_clear.cache =
      _mesa_hash_table_create(NULL, u64_hash, u64_compare);
}

void
v3dv_meta_clear_finish(struct v3dv_device *device)
{
   VkDevice _device = v3dv_device_to_handle(device);

   hash_table_foreach(device->meta.color_clear.cache, entry) {
      struct v3dv_meta_color_clear_pipeline *item = entry->data;
      destroy_color_clear_pipeline(_device, (uintptr_t)item, &device->alloc);
   }
   _mesa_hash_table_destroy(device->meta.color_clear.cache, NULL);

   if (device->meta.color_clear.playout) {
      v3dv_DestroyPipelineLayout(_device, device->meta.color_clear.playout,
                                 &device->alloc);
   }

   hash_table_foreach(device->meta.depth_clear.cache, entry) {
      struct v3dv_meta_depth_clear_pipeline *item = entry->data;
      destroy_depth_clear_pipeline(_device, item, &device->alloc);
   }
   _mesa_hash_table_destroy(device->meta.depth_clear.cache, NULL);

   if (device->meta.depth_clear.playout) {
      v3dv_DestroyPipelineLayout(_device, device->meta.depth_clear.playout,
                                 &device->alloc);
   }
}

static nir_ssa_def *
gen_rect_vertices(nir_builder *b)
{
   nir_intrinsic_instr *vertex_id =
      nir_intrinsic_instr_create(b->shader,
                                 nir_intrinsic_load_vertex_id);
   nir_ssa_dest_init(&vertex_id->instr, &vertex_id->dest, 1, 32, "vertexid");
   nir_builder_instr_insert(b, &vertex_id->instr);


   /* vertex 0: -1.0, -1.0
    * vertex 1: -1.0,  1.0
    * vertex 2:  1.0, -1.0
    * vertex 3:  1.0,  1.0
    *
    * so:
    *
    * channel 0 is vertex_id < 2 ? -1.0 :  1.0
    * channel 1 is vertex id & 1 ?  1.0 : -1.0
    */

   nir_ssa_def *one = nir_imm_int(b, 1);
   nir_ssa_def *c0cmp = nir_ilt(b, &vertex_id->dest.ssa, nir_imm_int(b, 2));
   nir_ssa_def *c1cmp = nir_ieq(b, nir_iand(b, &vertex_id->dest.ssa, one), one);

   nir_ssa_def *comp[4];
   comp[0] = nir_bcsel(b, c0cmp,
                       nir_imm_float(b, -1.0f),
                       nir_imm_float(b, 1.0f));

   comp[1] = nir_bcsel(b, c1cmp,
                       nir_imm_float(b, 1.0f),
                       nir_imm_float(b, -1.0f));
   comp[2] = nir_imm_float(b, 0.0f);
   comp[3] = nir_imm_float(b, 1.0f);
   return nir_vec(b, comp, 4);
}

static nir_shader *
get_clear_rect_vs()
{
   nir_builder b;
   const nir_shader_compiler_options *options = v3dv_pipeline_get_nir_options();
   nir_builder_init_simple_shader(&b, NULL, MESA_SHADER_VERTEX, options);
   b.shader->info.name = ralloc_strdup(b.shader, "meta clear vs");

   const struct glsl_type *vec4 = glsl_vec4_type();
   nir_variable *vs_out_pos =
      nir_variable_create(b.shader, nir_var_shader_out, vec4, "gl_Position");
   vs_out_pos->data.location = VARYING_SLOT_POS;

   nir_ssa_def *pos = gen_rect_vertices(&b);
   nir_store_var(&b, vs_out_pos, pos, 0xf);

   return b.shader;
}

static nir_shader *
get_color_clear_rect_fs(uint32_t rt_idx, VkFormat format)
{
   nir_builder b;
   const nir_shader_compiler_options *options = v3dv_pipeline_get_nir_options();
   nir_builder_init_simple_shader(&b, NULL, MESA_SHADER_FRAGMENT, options);
   b.shader->info.name = ralloc_strdup(b.shader, "meta clear fs");

   enum pipe_format pformat = vk_format_to_pipe_format(format);
   const struct glsl_type *fs_out_type =
      util_format_is_float(pformat) ? glsl_vec4_type() : glsl_uvec4_type();

   nir_variable *fs_out_color =
      nir_variable_create(b.shader, nir_var_shader_out, fs_out_type, "out_color");
   fs_out_color->data.location = FRAG_RESULT_DATA0 + rt_idx;

   nir_intrinsic_instr *color_load =
      nir_intrinsic_instr_create(b.shader, nir_intrinsic_load_push_constant);
   nir_intrinsic_set_base(color_load, 0);
   nir_intrinsic_set_range(color_load, 16);
   color_load->src[0] = nir_src_for_ssa(nir_imm_int(&b, 0));
   color_load->num_components = 4;
   nir_ssa_dest_init(&color_load->instr, &color_load->dest, 4, 32, "clear color");
   nir_builder_instr_insert(&b, &color_load->instr);

   nir_store_var(&b, fs_out_color, &color_load->dest.ssa, 0xf);

   return b.shader;
}

static nir_shader *
get_depth_clear_rect_fs()
{
   nir_builder b;
   const nir_shader_compiler_options *options = v3dv_pipeline_get_nir_options();
   nir_builder_init_simple_shader(&b, NULL, MESA_SHADER_FRAGMENT, options);
   b.shader->info.name = ralloc_strdup(b.shader, "meta depth clear fs");

   nir_variable *fs_out_depth =
      nir_variable_create(b.shader, nir_var_shader_out, glsl_float_type(),
                          "out_depth");
   fs_out_depth->data.location = FRAG_RESULT_DEPTH;

   nir_intrinsic_instr *depth_load =
      nir_intrinsic_instr_create(b.shader, nir_intrinsic_load_push_constant);
   nir_intrinsic_set_base(depth_load, 0);
   nir_intrinsic_set_range(depth_load, 4);
   depth_load->src[0] = nir_src_for_ssa(nir_imm_int(&b, 0));
   depth_load->num_components = 1;
   nir_ssa_dest_init(&depth_load->instr, &depth_load->dest, 1, 32,
                     "clear depth value");
   nir_builder_instr_insert(&b, &depth_load->instr);

   nir_store_var(&b, fs_out_depth, &depth_load->dest.ssa, 0x1);

   return b.shader;
}

static VkResult
create_color_clear_pipeline_layout(struct v3dv_device *device,
                                   VkPipelineLayout *pipeline_layout)
{
   VkPipelineLayoutCreateInfo info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 0,
      .pushConstantRangeCount = 1,
      .pPushConstantRanges =
         &(VkPushConstantRange) { VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16 },
   };

   return v3dv_CreatePipelineLayout(v3dv_device_to_handle(device),
                                    &info, &device->alloc, pipeline_layout);
}

static VkResult
create_depth_clear_pipeline_layout(struct v3dv_device *device,
                                   VkPipelineLayout *pipeline_layout)
{
   VkPipelineLayoutCreateInfo info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 0,
      .pushConstantRangeCount = 1,
      .pPushConstantRanges =
         &(VkPushConstantRange) { VK_SHADER_STAGE_FRAGMENT_BIT, 0, 4 },
   };

   return v3dv_CreatePipelineLayout(v3dv_device_to_handle(device),
                                    &info, &device->alloc, pipeline_layout);
}

static VkResult
create_pipeline(struct v3dv_device *device,
                struct v3dv_render_pass *pass,
                uint32_t subpass_idx,
                uint32_t samples,
                struct nir_shader *vs_nir,
                struct nir_shader *fs_nir,
                const VkPipelineVertexInputStateCreateInfo *vi_state,
                const VkPipelineDepthStencilStateCreateInfo *ds_state,
                const VkPipelineColorBlendStateCreateInfo *cb_state,
                const VkPipelineLayout layout,
                VkPipeline *pipeline)
{
   struct v3dv_shader_module vs_m;
   struct v3dv_shader_module fs_m;

   v3dv_shader_module_internal_init(&vs_m, vs_nir);
   v3dv_shader_module_internal_init(&fs_m, fs_nir);

   VkPipelineShaderStageCreateInfo stages[2] = {
      {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_VERTEX_BIT,
         .module = v3dv_shader_module_to_handle(&vs_m),
         .pName = "main",
      },
      {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
         .module = v3dv_shader_module_to_handle(&fs_m),
         .pName = "main",
      },
   };

   VkGraphicsPipelineCreateInfo info = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,

      .stageCount = fs_nir ? 2 : 1,
      .pStages = stages,

      .pVertexInputState = vi_state,

      .pInputAssemblyState = &(VkPipelineInputAssemblyStateCreateInfo) {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
         .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
         .primitiveRestartEnable = false,
      },

      .pViewportState = &(VkPipelineViewportStateCreateInfo) {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
         .viewportCount = 1,
         .scissorCount = 1,
      },

      .pRasterizationState = &(VkPipelineRasterizationStateCreateInfo) {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
         .rasterizerDiscardEnable = false,
         .polygonMode = VK_POLYGON_MODE_FILL,
         .cullMode = VK_CULL_MODE_NONE,
         .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
         .depthBiasEnable = false,
      },

      .pMultisampleState = &(VkPipelineMultisampleStateCreateInfo) {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
         .rasterizationSamples = samples,
         .sampleShadingEnable = false,
         .pSampleMask = NULL,
         .alphaToCoverageEnable = false,
         .alphaToOneEnable = false,
      },

      .pDepthStencilState = ds_state,

      .pColorBlendState = cb_state,

      /* The meta clear pipeline declares all state as dynamic.
       * As a consequence, vkCmdBindPipeline writes no dynamic state
       * to the cmd buffer. Therefore, at the end of the meta clear,
       * we need only restore dynamic state that was vkCmdSet.
       */
      .pDynamicState = &(VkPipelineDynamicStateCreateInfo) {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
         .dynamicStateCount = 6,
         .pDynamicStates = (VkDynamicState[]) {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
            VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
            VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
            VK_DYNAMIC_STATE_STENCIL_REFERENCE,
            VK_DYNAMIC_STATE_BLEND_CONSTANTS,
            VK_DYNAMIC_STATE_DEPTH_BIAS,
            VK_DYNAMIC_STATE_LINE_WIDTH,
         },
      },

      .flags = 0,
      .layout = layout,
      .renderPass = v3dv_render_pass_to_handle(pass),
      .subpass = subpass_idx,
   };

   VkResult result =
      v3dv_CreateGraphicsPipelines(v3dv_device_to_handle(device),
                                   VK_NULL_HANDLE,
                                   1, &info,
                                   &device->alloc,
                                   pipeline);

   ralloc_free(vs_nir);
   ralloc_free(fs_nir);

   return result;
}

static VkResult
create_color_clear_pipeline(struct v3dv_device *device,
                            struct v3dv_render_pass *pass,
                            uint32_t subpass_idx,
                            uint32_t rt_idx,
                            VkFormat format,
                            uint32_t samples,
                            uint32_t components,
                            VkPipelineLayout pipeline_layout,
                            VkPipeline *pipeline)
{
   nir_shader *vs_nir = get_clear_rect_vs();
   nir_shader *fs_nir = get_color_clear_rect_fs(rt_idx, format);

   const VkPipelineVertexInputStateCreateInfo vi_state = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = 0,
      .vertexAttributeDescriptionCount = 0,
   };

   const VkPipelineDepthStencilStateCreateInfo ds_state = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .depthTestEnable = false,
      .depthWriteEnable = false,
      .depthBoundsTestEnable = false,
      .stencilTestEnable = false,
   };

   assert(subpass_idx < pass->subpass_count);
   const uint32_t color_count = pass->subpasses[subpass_idx].color_count;
   assert(rt_idx < color_count);

   VkPipelineColorBlendAttachmentState blend_att_state[V3D_MAX_DRAW_BUFFERS];
   for (uint32_t i = 0; i < color_count; i++) {
      blend_att_state[i] = (VkPipelineColorBlendAttachmentState) {
         .blendEnable = false,
         .colorWriteMask = i == rt_idx ? components : 0,
      };
   }

   const VkPipelineColorBlendStateCreateInfo cb_state = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .logicOpEnable = false,
      .attachmentCount = color_count,
      .pAttachments = blend_att_state
   };

   return create_pipeline(device,
                          pass, subpass_idx,
                          samples,
                          vs_nir, fs_nir,
                          &vi_state,
                          &ds_state,
                          &cb_state,
                          pipeline_layout,
                          pipeline);
}

static VkResult
create_depth_clear_pipeline(struct v3dv_device *device,
                            VkImageAspectFlags aspects,
                            struct v3dv_render_pass *pass,
                            uint32_t subpass_idx,
                            uint32_t samples,
                            VkPipelineLayout pipeline_layout,
                            VkPipeline *pipeline)
{
   const bool has_depth = aspects & VK_IMAGE_ASPECT_DEPTH_BIT;
   const bool has_stencil = aspects & VK_IMAGE_ASPECT_STENCIL_BIT;
   assert(has_depth || has_stencil);

   nir_shader *vs_nir = get_clear_rect_vs();
   nir_shader *fs_nir = has_depth ? get_depth_clear_rect_fs() : NULL;

   const VkPipelineVertexInputStateCreateInfo vi_state = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = 0,
      .vertexAttributeDescriptionCount = 0,
   };

   const VkPipelineDepthStencilStateCreateInfo ds_state = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .depthTestEnable = has_depth,
      .depthWriteEnable = has_depth,
      .depthCompareOp = VK_COMPARE_OP_ALWAYS,
      .depthBoundsTestEnable = false,
      .stencilTestEnable = has_stencil,
      .front = {
         .passOp = VK_STENCIL_OP_REPLACE,
         .compareOp = VK_COMPARE_OP_ALWAYS,
         /* compareMask, writeMask and reference are dynamic state */
      },
      .back = { 0 },
   };

   assert(subpass_idx < pass->subpass_count);
   VkPipelineColorBlendAttachmentState blend_att_state[V3D_MAX_DRAW_BUFFERS] = { 0 };
   const VkPipelineColorBlendStateCreateInfo cb_state = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .logicOpEnable = false,
      .attachmentCount = pass->subpasses[subpass_idx].color_count,
      .pAttachments = blend_att_state,
   };

   return create_pipeline(device,
                          pass, subpass_idx,
                          samples,
                          vs_nir, fs_nir,
                          &vi_state,
                          &ds_state,
                          &cb_state,
                          pipeline_layout,
                          pipeline);
}

static VkResult
create_color_clear_render_pass(struct v3dv_device *device,
                               uint32_t rt_idx,
                               VkFormat format,
                               uint32_t samples,
                               VkRenderPass *pass)
{
   VkAttachmentDescription att = {
      .format = format,
      .samples = samples,
      .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
      .initialLayout = VK_IMAGE_LAYOUT_GENERAL,
      .finalLayout = VK_IMAGE_LAYOUT_GENERAL,
   };

   VkAttachmentReference att_ref = {
      .attachment = rt_idx,
      .layout = VK_IMAGE_LAYOUT_GENERAL,
   };

   VkSubpassDescription subpass = {
      .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
      .inputAttachmentCount = 0,
      .colorAttachmentCount = 1,
      .pColorAttachments = &att_ref,
      .pResolveAttachments = NULL,
      .pDepthStencilAttachment = NULL,
      .preserveAttachmentCount = 0,
      .pPreserveAttachments = NULL,
   };

   VkRenderPassCreateInfo info = {
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
      .attachmentCount = 1,
      .pAttachments = &att,
      .subpassCount = 1,
      .pSubpasses = &subpass,
      .dependencyCount = 0,
      .pDependencies = NULL,
   };

   return v3dv_CreateRenderPass(v3dv_device_to_handle(device),
                                &info, &device->alloc, pass);
}

static inline uint64_t
get_color_clear_pipeline_cache_key(uint32_t rt_idx,
                                   VkFormat format,
                                   uint32_t samples,
                                   uint32_t components)
{
   assert(rt_idx < V3D_MAX_DRAW_BUFFERS);

   uint64_t key = 0;
   uint32_t bit_offset = 0;

   key |= rt_idx;
   bit_offset += 2;

   key |= ((uint64_t) format) << bit_offset;
   bit_offset += 32;

   key |= ((uint64_t) samples) << bit_offset;
   bit_offset += 4;

   key |= ((uint64_t) components) << bit_offset;
   bit_offset += 4;

   assert(bit_offset <= 64);
   return key;
}

static inline uint64_t
get_depth_clear_pipeline_cache_key(VkImageAspectFlags aspects,
                                   VkFormat format,
                                   uint32_t samples)
{
   uint64_t key = 0;
   uint32_t bit_offset = 0;

   key |= format;
   bit_offset += 32;

   key |= ((uint64_t) samples) << bit_offset;
   bit_offset += 4;

   const bool has_depth = (aspects & VK_IMAGE_ASPECT_DEPTH_BIT) ? 1 : 0;
   key |= ((uint64_t) has_depth) << bit_offset;
   bit_offset++;

   const bool has_stencil = (aspects & VK_IMAGE_ASPECT_STENCIL_BIT) ? 1 : 0;
   key |= ((uint64_t) has_stencil) << bit_offset;
   bit_offset++;;

   assert(bit_offset <= 64);
   return key;
}

static VkResult
get_color_clear_pipeline(struct v3dv_device *device,
                         struct v3dv_render_pass *pass,
                         uint32_t subpass_idx,
                         uint32_t rt_idx,
                         uint32_t attachment_idx,
                         VkFormat format,
                         uint32_t samples,
                         uint32_t components,
                         struct v3dv_meta_color_clear_pipeline **pipeline)
{
   assert(vk_format_is_color(format));

   VkResult result = VK_SUCCESS;

   mtx_lock(&device->meta.mtx);
   if (!device->meta.color_clear.playout) {
      result =
         create_color_clear_pipeline_layout(device,
                                            &device->meta.color_clear.playout);
   }
   mtx_unlock(&device->meta.mtx);
   if (result != VK_SUCCESS)
      return result;

   /* If pass != NULL it means that we are emitting the clear as a draw call
    * in the current pass bound by the application. In that case, we can't
    * cache the pipeline, since it will be referencing that pass and the
    * application could be destroying it at any point. Hopefully, the perf
    * impact is not too big since we still have the device pipeline cache
    * around and we won't end up re-compiling the clear shader.
    *
    * FIXME: alternatively, we could refcount (or maybe clone) the render pass
    * provided by the application and include it in the pipeline key setup
    * to make caching safe in this scenario, however, based on tests with
    * vkQuake3, the fact that we are not caching here doesn't seem to have
    * any significant impact in performance, so it might not be worth it.
    */
   const bool can_cache_pipeline = (pass == NULL);

   uint64_t key;
   if (can_cache_pipeline) {
      key =
         get_color_clear_pipeline_cache_key(rt_idx, format, samples, components);
      mtx_lock(&device->meta.mtx);
      struct hash_entry *entry =
         _mesa_hash_table_search(device->meta.color_clear.cache, &key);
      if (entry) {
         mtx_unlock(&device->meta.mtx);
         *pipeline = entry->data;
         return VK_SUCCESS;
      }
   }

   *pipeline = vk_zalloc2(&device->alloc, NULL, sizeof(**pipeline), 8,
                          VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);

   if (*pipeline == NULL) {
      result = VK_ERROR_OUT_OF_HOST_MEMORY;
      goto fail;
   }

   if (!pass) {
      result = create_color_clear_render_pass(device,
                                              rt_idx,
                                              format,
                                              samples,
                                              &(*pipeline)->pass);
      if (result != VK_SUCCESS)
         goto fail;

      pass = v3dv_render_pass_from_handle((*pipeline)->pass);
   } else {
      (*pipeline)->pass = v3dv_render_pass_to_handle(pass);
   }

   result = create_color_clear_pipeline(device,
                                        pass,
                                        subpass_idx,
                                        rt_idx,
                                        format,
                                        samples,
                                        components,
                                        device->meta.color_clear.playout,
                                        &(*pipeline)->pipeline);
   if (result != VK_SUCCESS)
      goto fail;

   if (can_cache_pipeline) {
      (*pipeline)->key = key;
      (*pipeline)->cached = true;
      _mesa_hash_table_insert(device->meta.color_clear.cache,
                              &(*pipeline)->key, *pipeline);

      mtx_unlock(&device->meta.mtx);
   }

   return VK_SUCCESS;

fail:
   if (can_cache_pipeline)
      mtx_unlock(&device->meta.mtx);

   VkDevice _device = v3dv_device_to_handle(device);
   if (*pipeline) {
      if ((*pipeline)->cached)
         v3dv_DestroyRenderPass(_device, (*pipeline)->pass, &device->alloc);
      if ((*pipeline)->pipeline)
         v3dv_DestroyPipeline(_device, (*pipeline)->pipeline, &device->alloc);
      vk_free(&device->alloc, *pipeline);
      *pipeline = NULL;
   }

   return result;
}

static VkResult
get_depth_clear_pipeline(struct v3dv_device *device,
                         VkImageAspectFlags aspects,
                         struct v3dv_render_pass *pass,
                         uint32_t subpass_idx,
                         uint32_t attachment_idx,
                         struct v3dv_meta_depth_clear_pipeline **pipeline)
{
   assert(subpass_idx < pass->subpass_count);
   assert(attachment_idx != VK_ATTACHMENT_UNUSED);
   assert(attachment_idx < pass->attachment_count);

   VkResult result = VK_SUCCESS;

   mtx_lock(&device->meta.mtx);
   if (!device->meta.depth_clear.playout) {
      result =
         create_depth_clear_pipeline_layout(device,
                                            &device->meta.depth_clear.playout);
   }
   mtx_unlock(&device->meta.mtx);
   if (result != VK_SUCCESS)
      return result;

   const uint32_t samples = pass->attachments[attachment_idx].desc.samples;
   const VkFormat format = pass->attachments[attachment_idx].desc.format;
   assert(vk_format_is_depth_or_stencil(format));

   const uint64_t key =
      get_depth_clear_pipeline_cache_key(aspects, format, samples);
   mtx_lock(&device->meta.mtx);
   struct hash_entry *entry =
      _mesa_hash_table_search(device->meta.depth_clear.cache, &key);
   if (entry) {
      mtx_unlock(&device->meta.mtx);
      *pipeline = entry->data;
      return VK_SUCCESS;
   }

   *pipeline = vk_zalloc2(&device->alloc, NULL, sizeof(**pipeline), 8,
                          VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);

   if (*pipeline == NULL) {
      result = VK_ERROR_OUT_OF_HOST_MEMORY;
      goto fail;
   }

   result = create_depth_clear_pipeline(device,
                                        aspects,
                                        pass,
                                        subpass_idx,
                                        samples,
                                        device->meta.depth_clear.playout,
                                        &(*pipeline)->pipeline);
   if (result != VK_SUCCESS)
      goto fail;

   (*pipeline)->key = key;
   _mesa_hash_table_insert(device->meta.depth_clear.cache,
                           &(*pipeline)->key, *pipeline);

   mtx_unlock(&device->meta.mtx);
   return VK_SUCCESS;

fail:
   mtx_unlock(&device->meta.mtx);

   VkDevice _device = v3dv_device_to_handle(device);
   if (*pipeline) {
      if ((*pipeline)->pipeline)
         v3dv_DestroyPipeline(_device, (*pipeline)->pipeline, &device->alloc);
      vk_free(&device->alloc, *pipeline);
      *pipeline = NULL;
   }

   return result;
}

static VkFormat
get_color_format_for_depth_stencil_format(VkFormat format)
{
   /* For single depth/stencil aspect formats, we just choose a compatible
    * 1 channel format, but for combined depth/stencil we want an RGBA format
    * so we can specify the channels we want to write.
    */
   switch (format) {
   case VK_FORMAT_D16_UNORM:
      return VK_FORMAT_R16_UINT;
   case VK_FORMAT_D32_SFLOAT:
      return VK_FORMAT_R32_SFLOAT;
   case VK_FORMAT_X8_D24_UNORM_PACK32:
   case VK_FORMAT_D24_UNORM_S8_UINT:
      return VK_FORMAT_R8G8B8A8_UINT;
   default:
      unreachable("Unsupported depth/stencil format");
   };
}

/**
 * Emits a scissored quad in the clear color, however, unlike the subpass
 * versions, this creates its own framebuffer setup with a single color
 * attachment, and therefore spanws new jobs, making it much slower than the
 * subpass version.
 *
 * This path is only used when we have clears on layers other than the
 * base layer in a framebuffer attachment, since we don't currently
 * support any form of layered rendering that would allow us to implement
 * this in the subpass version.
 *
 * Notice this can also handle depth/stencil formats by rendering to the
 * depth/stencil target using a compatible color format.
 */
static void
emit_color_clear_rect(struct v3dv_cmd_buffer *cmd_buffer,
                      uint32_t attachment_idx,
                      VkFormat rt_format,
                      uint32_t rt_samples,
                      uint32_t rt_components,
                      VkClearColorValue clear_color,
                      const VkClearRect *rect)
{
   assert(cmd_buffer->state.pass);
   struct v3dv_device *device = cmd_buffer->device;
   struct v3dv_render_pass *pass = cmd_buffer->state.pass;

   assert(attachment_idx != VK_ATTACHMENT_UNUSED &&
          attachment_idx < pass->attachment_count);

   struct v3dv_meta_color_clear_pipeline *pipeline = NULL;
   VkResult result =
      get_color_clear_pipeline(device,
                               NULL, 0, /* Not using current subpass */
                               0, attachment_idx,
                               rt_format, rt_samples, rt_components,
                               &pipeline);
   if (result != VK_SUCCESS) {
      if (result == VK_ERROR_OUT_OF_HOST_MEMORY)
         v3dv_flag_oom(cmd_buffer, NULL);
      return;
   }
   assert(pipeline && pipeline->pipeline && pipeline->pass);

   /* Since we are not emitting the draw call in the current subpass we should
    * be caching the clear pipeline and we don't have to take care of destorying
    * it below.
    */
   assert(pipeline->cached);

   /* Store command buffer state for the current subpass before we interrupt
    * it to emit the color clear pass and then finish the job for the
    * interrupted subpass.
    */
   v3dv_cmd_buffer_meta_state_push(cmd_buffer, false);
   v3dv_cmd_buffer_finish_job(cmd_buffer);

   struct v3dv_framebuffer *subpass_fb =
      v3dv_framebuffer_from_handle(cmd_buffer->state.meta.framebuffer);
   VkCommandBuffer cmd_buffer_handle = v3dv_cmd_buffer_to_handle(cmd_buffer);
   VkDevice device_handle = v3dv_device_to_handle(cmd_buffer->device);

   /* If we are clearing a depth/stencil attachment as a color attachment
    * then we need to configure the framebuffer to the compatible color
    * format.
    */
   const struct v3dv_image_view *att_iview =
      subpass_fb->attachments[attachment_idx];
   const bool is_depth_or_stencil =
      vk_format_is_depth_or_stencil(att_iview->vk_format);

   /* Emit the pass for each attachment layer, which creates a framebuffer
    * for each selected layer of the attachment and then renders a scissored
    * quad in the clear color.
    */
   uint32_t dirty_dynamic_state = 0;
   for (uint32_t i = 0; i < rect->layerCount; i++) {
      VkImageViewCreateInfo fb_layer_view_info = {
         .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
         .image = v3dv_image_to_handle((struct v3dv_image *)att_iview->image),
         .viewType =
            v3dv_image_type_to_view_type(att_iview->image->type),
         .format = is_depth_or_stencil ? rt_format : att_iview->vk_format,
         .subresourceRange = {
            .aspectMask = is_depth_or_stencil ? VK_IMAGE_ASPECT_COLOR_BIT :
                                                att_iview->aspects,
            .baseMipLevel = att_iview->base_level,
            .levelCount = att_iview->max_level - att_iview->base_level + 1,
            .baseArrayLayer = att_iview->first_layer + rect->baseArrayLayer + i,
            .layerCount = 1,
         },
      };
      VkImageView fb_attachment;
      result = v3dv_CreateImageView(v3dv_device_to_handle(device),
                                    &fb_layer_view_info,
                                    &device->alloc, &fb_attachment);
      if (result != VK_SUCCESS)
         goto fail;

      v3dv_cmd_buffer_add_private_obj(
         cmd_buffer, (uintptr_t)fb_attachment,
         (v3dv_cmd_buffer_private_obj_destroy_cb)v3dv_DestroyImageView);

      VkFramebufferCreateInfo fb_info = {
         .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
         .renderPass = v3dv_render_pass_to_handle(pass),
         .attachmentCount = 1,
         .pAttachments = &fb_attachment,
         .width = subpass_fb->width,
         .height = subpass_fb->height,
         .layers = 1,
      };

      VkFramebuffer fb;
      result = v3dv_CreateFramebuffer(device_handle, &fb_info,
                                      &cmd_buffer->device->alloc, &fb);
      if (result != VK_SUCCESS)
         goto fail;

      v3dv_cmd_buffer_add_private_obj(
         cmd_buffer, (uintptr_t)fb,
         (v3dv_cmd_buffer_private_obj_destroy_cb)v3dv_DestroyFramebuffer);

      VkRenderPassBeginInfo rp_info = {
         .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
         .renderPass = pipeline->pass,
         .framebuffer = fb,
         .renderArea = {
            .offset = { rect->rect.offset.x, rect->rect.offset.y },
            .extent = { rect->rect.extent.width, rect->rect.extent.height } },
         .clearValueCount = 0,
      };

      v3dv_CmdBeginRenderPass(cmd_buffer_handle, &rp_info,
                              VK_SUBPASS_CONTENTS_INLINE);

      struct v3dv_job *job = cmd_buffer->state.job;
      if (!job)
         goto fail;
      job->is_subpass_continue = true;

      v3dv_CmdPushConstants(cmd_buffer_handle,
                            device->meta.color_clear.playout,
                            VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16,
                            &clear_color);

      v3dv_CmdBindPipeline(cmd_buffer_handle,
                           VK_PIPELINE_BIND_POINT_GRAPHICS,
                           pipeline->pipeline);

      const VkViewport viewport = {
         .x = rect->rect.offset.x,
         .y = rect->rect.offset.y,
         .width = rect->rect.extent.width,
         .height = rect->rect.extent.height,
         .minDepth = 0.0f,
         .maxDepth = 1.0f
      };
      v3dv_CmdSetViewport(cmd_buffer_handle, 0, 1, &viewport);
      v3dv_CmdSetScissor(cmd_buffer_handle, 0, 1, &rect->rect);

      v3dv_CmdDraw(cmd_buffer_handle, 4, 1, 0, 0);

      v3dv_CmdEndRenderPass(cmd_buffer_handle);
   }

   /* The clear pipeline sets viewport and scissor state, so we need
    * to restore it
    */
   dirty_dynamic_state = V3DV_CMD_DIRTY_VIEWPORT | V3DV_CMD_DIRTY_SCISSOR;

fail:
   v3dv_cmd_buffer_meta_state_pop(cmd_buffer, dirty_dynamic_state, true);
}

static void
emit_ds_clear_rect(struct v3dv_cmd_buffer *cmd_buffer,
                   VkImageAspectFlags aspects,
                   uint32_t attachment_idx,
                   VkClearDepthStencilValue clear_ds,
                   const VkClearRect *rect)
{
   assert(cmd_buffer->state.pass);
   assert(attachment_idx != VK_ATTACHMENT_UNUSED);
   assert(attachment_idx < cmd_buffer->state.pass->attachment_count);

   VkFormat format =
      cmd_buffer->state.pass->attachments[attachment_idx].desc.format;
   assert ((aspects & ~vk_format_aspects(format)) == 0);

   uint32_t samples =
      cmd_buffer->state.pass->attachments[attachment_idx].desc.samples;

   enum pipe_format pformat = vk_format_to_pipe_format(format);
   VkClearColorValue clear_color;
   uint32_t clear_zs =
      util_pack_z_stencil(pformat, clear_ds.depth, clear_ds.stencil);

   /* We implement depth/stencil clears by turning them into color clears
    * with a compatible color format.
    */
   VkFormat color_format = get_color_format_for_depth_stencil_format(format);

   uint32_t comps;
   if (color_format == VK_FORMAT_R8G8B8A8_UINT) {
    /* We are clearing a D24 format so we need to select the channels that we
     * are being asked to clear to avoid clearing aspects that should be
     * preserved. Also, the hardware uses the MSB channels to store the D24
     * component, so we need to shift the components in the clear value to
     * match that.
     */
      comps = 0;
      if (aspects & VK_IMAGE_ASPECT_STENCIL_BIT) {
         comps |= VK_COLOR_COMPONENT_R_BIT;
         clear_color.uint32[0] = clear_zs >> 24;
      }
      if (aspects & VK_IMAGE_ASPECT_DEPTH_BIT) {
         comps |= VK_COLOR_COMPONENT_G_BIT |
                  VK_COLOR_COMPONENT_B_BIT |
                  VK_COLOR_COMPONENT_A_BIT;
         clear_color.uint32[1] = (clear_zs >>  0) & 0xff;
         clear_color.uint32[2] = (clear_zs >>  8) & 0xff;
         clear_color.uint32[3] = (clear_zs >> 16) & 0xff;
      }
   } else {
      /* For anything else we use a single component format */
      comps = VK_COLOR_COMPONENT_R_BIT;
      clear_color.uint32[0] = clear_zs;
   }

   emit_color_clear_rect(cmd_buffer, attachment_idx,
                         color_format, samples, comps,
                         clear_color, rect);
}

/* Emits a scissored quad in the clear color.
 *
 * This path only works for clears to the base layer in the framebuffer, since
 * we don't currently support any form of layered rendering.
 */
static void
emit_subpass_color_clear_rects(struct v3dv_cmd_buffer *cmd_buffer,
                               struct v3dv_render_pass *pass,
                               struct v3dv_subpass *subpass,
                               uint32_t rt_idx,
                               const VkClearColorValue *clear_color,
                               uint32_t rect_count,
                               const VkClearRect *rects)
{
   /* Skip if attachment is unused in the current subpass */
   assert(rt_idx < subpass->color_count);
   const uint32_t attachment_idx = subpass->color_attachments[rt_idx].attachment;
   if (attachment_idx == VK_ATTACHMENT_UNUSED)
      return;

   /* Obtain a pipeline for this clear */
   assert(attachment_idx < cmd_buffer->state.pass->attachment_count);
   const VkFormat format =
      cmd_buffer->state.pass->attachments[attachment_idx].desc.format;
   const VkFormat samples =
      cmd_buffer->state.pass->attachments[attachment_idx].desc.samples;
   const uint32_t components = VK_COLOR_COMPONENT_R_BIT |
                               VK_COLOR_COMPONENT_G_BIT |
                               VK_COLOR_COMPONENT_B_BIT |
                               VK_COLOR_COMPONENT_A_BIT;
   struct v3dv_meta_color_clear_pipeline *pipeline = NULL;
   VkResult result = get_color_clear_pipeline(cmd_buffer->device,
                                              pass,
                                              cmd_buffer->state.subpass_idx,
                                              rt_idx,
                                              attachment_idx,
                                              format,
                                              samples,
                                              components,
                                              &pipeline);
   if (result != VK_SUCCESS) {
      if (result == VK_ERROR_OUT_OF_HOST_MEMORY)
         v3dv_flag_oom(cmd_buffer, NULL);
      return;
   }
   assert(pipeline && pipeline->pipeline);

   /* Emit clear rects */
   v3dv_cmd_buffer_meta_state_push(cmd_buffer, false);

   VkCommandBuffer cmd_buffer_handle = v3dv_cmd_buffer_to_handle(cmd_buffer);
   v3dv_CmdPushConstants(cmd_buffer_handle,
                         cmd_buffer->device->meta.depth_clear.playout,
                         VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16,
                         clear_color->float32);

   v3dv_CmdBindPipeline(cmd_buffer_handle,
                        VK_PIPELINE_BIND_POINT_GRAPHICS,
                        pipeline->pipeline);

   uint32_t dynamic_states = V3DV_CMD_DIRTY_VIEWPORT | V3DV_CMD_DIRTY_SCISSOR;

   for (uint32_t i = 0; i < rect_count; i++) {
      assert(rects[i].baseArrayLayer == 0 && rects[i].layerCount == 1);
      const VkViewport viewport = {
         .x = rects[i].rect.offset.x,
         .y = rects[i].rect.offset.y,
         .width = rects[i].rect.extent.width,
         .height = rects[i].rect.extent.height,
         .minDepth = 0.0f,
         .maxDepth = 1.0f
      };
      v3dv_CmdSetViewport(cmd_buffer_handle, 0, 1, &viewport);
      v3dv_CmdSetScissor(cmd_buffer_handle, 0, 1, &rects[i].rect);
      v3dv_CmdDraw(cmd_buffer_handle, 4, 1, 0, 0);
   }

   /* Subpass pipelines can't be cached because they include a reference to the
    * render pass currently bound by the application, which means that we need
    * to destroy them manually here.
    */
   assert(!pipeline->cached);
   v3dv_cmd_buffer_add_private_obj(
      cmd_buffer, (uintptr_t)pipeline,
      (v3dv_cmd_buffer_private_obj_destroy_cb) destroy_color_clear_pipeline);

   v3dv_cmd_buffer_meta_state_pop(cmd_buffer, dynamic_states, false);
}

/* Emits a scissored quad, clearing the depth aspect by writing to gl_FragDepth
 * and the stencil aspect by using stencil testing.
 *
 * This path only works for clears to the base layer in the framebuffer, since
 * we don't currently support any form of layered rendering.
 */
static void
emit_subpass_ds_clear_rects(struct v3dv_cmd_buffer *cmd_buffer,
                            struct v3dv_render_pass *pass,
                            struct v3dv_subpass *subpass,
                            VkImageAspectFlags aspects,
                            const VkClearDepthStencilValue *clear_ds,
                            uint32_t rect_count,
                            const VkClearRect *rects)
{
   /* Skip if attachment is unused in the current subpass */
   const uint32_t attachment_idx = subpass->ds_attachment.attachment;
   if (attachment_idx == VK_ATTACHMENT_UNUSED)
      return;

   /* Obtain a pipeline for this clear */
   assert(attachment_idx < cmd_buffer->state.pass->attachment_count);
   struct v3dv_meta_depth_clear_pipeline *pipeline = NULL;
   VkResult result = get_depth_clear_pipeline(cmd_buffer->device,
                                              aspects,
                                              pass,
                                              cmd_buffer->state.subpass_idx,
                                              attachment_idx,
                                              &pipeline);
   if (result != VK_SUCCESS) {
      if (result == VK_ERROR_OUT_OF_HOST_MEMORY)
         v3dv_flag_oom(cmd_buffer, NULL);
      return;
   }
   assert(pipeline && pipeline->pipeline);

   /* Emit clear rects */
   v3dv_cmd_buffer_meta_state_push(cmd_buffer, false);

   VkCommandBuffer cmd_buffer_handle = v3dv_cmd_buffer_to_handle(cmd_buffer);
   v3dv_CmdPushConstants(cmd_buffer_handle,
                         cmd_buffer->device->meta.depth_clear.playout,
                         VK_SHADER_STAGE_FRAGMENT_BIT, 0, 4,
                         &clear_ds->depth);

   v3dv_CmdBindPipeline(cmd_buffer_handle,
                        VK_PIPELINE_BIND_POINT_GRAPHICS,
                        pipeline->pipeline);

   uint32_t dynamic_states = V3DV_CMD_DIRTY_VIEWPORT | V3DV_CMD_DIRTY_SCISSOR;
   if (aspects & VK_IMAGE_ASPECT_STENCIL_BIT) {
      v3dv_CmdSetStencilReference(cmd_buffer_handle,
                                  VK_STENCIL_FACE_FRONT_AND_BACK,
                                  clear_ds->stencil);
      v3dv_CmdSetStencilWriteMask(cmd_buffer_handle,
                                  VK_STENCIL_FACE_FRONT_AND_BACK, 0xff);
      v3dv_CmdSetStencilCompareMask(cmd_buffer_handle,
                                    VK_STENCIL_FACE_FRONT_AND_BACK, 0xff);
      dynamic_states |= VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK |
                        VK_DYNAMIC_STATE_STENCIL_WRITE_MASK |
                        VK_DYNAMIC_STATE_STENCIL_REFERENCE;
   }

   for (uint32_t i = 0; i < rect_count; i++) {
      assert(rects[i].baseArrayLayer == 0 && rects[i].layerCount == 1);
      const VkViewport viewport = {
         .x = rects[i].rect.offset.x,
         .y = rects[i].rect.offset.y,
         .width = rects[i].rect.extent.width,
         .height = rects[i].rect.extent.height,
         .minDepth = 0.0f,
         .maxDepth = 1.0f
      };
      v3dv_CmdSetViewport(cmd_buffer_handle, 0, 1, &viewport);
      v3dv_CmdSetScissor(cmd_buffer_handle, 0, 1, &rects[i].rect);
      v3dv_CmdDraw(cmd_buffer_handle, 4, 1, 0, 0);
   }

   v3dv_cmd_buffer_meta_state_pop(cmd_buffer, dynamic_states, false);
}

static void
emit_tlb_clear_store(struct v3dv_cmd_buffer *cmd_buffer,
                     struct v3dv_cl *cl,
                     uint32_t attachment_idx,
                     uint32_t layer,
                     uint32_t buffer)
{
   const struct v3dv_image_view *iview =
      cmd_buffer->state.framebuffer->attachments[attachment_idx];
   const struct v3dv_image *image = iview->image;
   const struct v3d_resource_slice *slice = &image->slices[iview->base_level];
   uint32_t layer_offset = v3dv_layer_offset(image,
                                             iview->base_level,
                                             iview->first_layer + layer);

   cl_emit(cl, STORE_TILE_BUFFER_GENERAL, store) {
      store.buffer_to_store = buffer;
      store.address = v3dv_cl_address(image->mem->bo, layer_offset);
      store.clear_buffer_being_stored = false;

      store.output_image_format = iview->format->rt_type;
      store.r_b_swap = iview->swap_rb;
      store.memory_format = slice->tiling;

      if (slice->tiling == VC5_TILING_UIF_NO_XOR ||
          slice->tiling == VC5_TILING_UIF_XOR) {
         store.height_in_ub_or_stride =
            slice->padded_height_of_output_image_in_uif_blocks;
      } else if (slice->tiling == VC5_TILING_RASTER) {
         store.height_in_ub_or_stride = slice->stride;
      }

      if (image->samples > VK_SAMPLE_COUNT_1_BIT)
         store.decimate_mode = V3D_DECIMATE_MODE_ALL_SAMPLES;
      else
         store.decimate_mode = V3D_DECIMATE_MODE_SAMPLE_0;
   }
}

static void
emit_tlb_clear_stores(struct v3dv_cmd_buffer *cmd_buffer,
                      struct v3dv_cl *cl,
                      uint32_t attachment_count,
                      const VkClearAttachment *attachments,
                      uint32_t layer)
{
   struct v3dv_cmd_buffer_state *state = &cmd_buffer->state;
   const struct v3dv_subpass *subpass =
      &state->pass->subpasses[state->subpass_idx];

   bool has_stores = false;
   for (uint32_t i = 0; i < attachment_count; i++) {
      uint32_t attachment_idx;
      uint32_t buffer;
      if (attachments[i].aspectMask & (VK_IMAGE_ASPECT_DEPTH_BIT |
                                       VK_IMAGE_ASPECT_STENCIL_BIT)) {
         attachment_idx = subpass->ds_attachment.attachment;
         buffer = v3dv_zs_buffer_from_aspect_bits(attachments[i].aspectMask);
      } else {
         uint32_t rt_idx = attachments[i].colorAttachment;
         attachment_idx = subpass->color_attachments[rt_idx].attachment;
         buffer = RENDER_TARGET_0 + rt_idx;
      }

      if (attachment_idx == VK_ATTACHMENT_UNUSED)
         continue;

      has_stores = true;
      emit_tlb_clear_store(cmd_buffer, cl, attachment_idx, layer, buffer);
   }

   if (!has_stores) {
      cl_emit(cl, STORE_TILE_BUFFER_GENERAL, store) {
         store.buffer_to_store = NONE;
      }
   }
}

static void
emit_tlb_clear_per_tile_rcl(struct v3dv_cmd_buffer *cmd_buffer,
                            uint32_t attachment_count,
                            const VkClearAttachment *attachments,
                            uint32_t layer)
{
   struct v3dv_job *job = cmd_buffer->state.job;
   assert(job);

   struct v3dv_cl *cl = &job->indirect;
   v3dv_cl_ensure_space(cl, 200, 1);
   v3dv_return_if_oom(cmd_buffer, NULL);

   struct v3dv_cl_reloc tile_list_start = v3dv_cl_get_address(cl);

   cl_emit(cl, TILE_COORDINATES_IMPLICIT, coords);

   cl_emit(cl, END_OF_LOADS, end); /* Nothing to load */

   cl_emit(cl, PRIM_LIST_FORMAT, fmt) {
      fmt.primitive_type = LIST_TRIANGLES;
   }

   cl_emit(cl, BRANCH_TO_IMPLICIT_TILE_LIST, branch);

   emit_tlb_clear_stores(cmd_buffer, cl, attachment_count, attachments, layer);

   cl_emit(cl, END_OF_TILE_MARKER, end);

   cl_emit(cl, RETURN_FROM_SUB_LIST, ret);

   cl_emit(&job->rcl, START_ADDRESS_OF_GENERIC_TILE_LIST, branch) {
      branch.start = tile_list_start;
      branch.end = v3dv_cl_get_address(cl);
   }
}

static void
emit_tlb_clear_layer_rcl(struct v3dv_cmd_buffer *cmd_buffer,
                         uint32_t attachment_count,
                         const VkClearAttachment *attachments,
                         uint32_t layer)
{
   const struct v3dv_cmd_buffer_state *state = &cmd_buffer->state;
   const struct v3dv_framebuffer *framebuffer = state->framebuffer;

   struct v3dv_job *job = cmd_buffer->state.job;
   struct v3dv_cl *rcl = &job->rcl;

   const struct v3dv_frame_tiling *tiling = &job->frame_tiling;

   const uint32_t tile_alloc_offset =
      64 * layer * tiling->draw_tiles_x * tiling->draw_tiles_y;
   cl_emit(rcl, MULTICORE_RENDERING_TILE_LIST_SET_BASE, list) {
      list.address = v3dv_cl_address(job->tile_alloc, tile_alloc_offset);
   }

   cl_emit(rcl, MULTICORE_RENDERING_SUPERTILE_CFG, config) {
      config.number_of_bin_tile_lists = 1;
      config.total_frame_width_in_tiles = tiling->draw_tiles_x;
      config.total_frame_height_in_tiles = tiling->draw_tiles_y;

      config.supertile_width_in_tiles = tiling->supertile_width;
      config.supertile_height_in_tiles = tiling->supertile_height;

      config.total_frame_width_in_supertiles =
         tiling->frame_width_in_supertiles;
      config.total_frame_height_in_supertiles =
         tiling->frame_height_in_supertiles;
   }

   /* Emit the clear and also the workaround for GFXH-1742 */
   for (int i = 0; i < 2; i++) {
      cl_emit(rcl, TILE_COORDINATES, coords);
      cl_emit(rcl, END_OF_LOADS, end);
      cl_emit(rcl, STORE_TILE_BUFFER_GENERAL, store) {
         store.buffer_to_store = NONE;
      }
      if (i == 0) {
         cl_emit(rcl, CLEAR_TILE_BUFFERS, clear) {
            clear.clear_z_stencil_buffer = true;
            clear.clear_all_render_targets = true;
         }
      }
      cl_emit(rcl, END_OF_TILE_MARKER, end);
   }

   cl_emit(rcl, FLUSH_VCD_CACHE, flush);

   emit_tlb_clear_per_tile_rcl(cmd_buffer, attachment_count, attachments, layer);

   uint32_t supertile_w_in_pixels =
      tiling->tile_width * tiling->supertile_width;
   uint32_t supertile_h_in_pixels =
      tiling->tile_height * tiling->supertile_height;

   const uint32_t max_render_x = framebuffer->width - 1;
   const uint32_t max_render_y = framebuffer->height - 1;
   const uint32_t max_x_supertile = max_render_x / supertile_w_in_pixels;
   const uint32_t max_y_supertile = max_render_y / supertile_h_in_pixels;

   for (int y = 0; y <= max_y_supertile; y++) {
      for (int x = 0; x <= max_x_supertile; x++) {
         cl_emit(rcl, SUPERTILE_COORDINATES, coords) {
            coords.column_number_in_supertiles = x;
            coords.row_number_in_supertiles = y;
         }
      }
   }
}

static void
emit_tlb_clear_job(struct v3dv_cmd_buffer *cmd_buffer,
                   uint32_t attachment_count,
                   const VkClearAttachment *attachments,
                   uint32_t base_layer,
                   uint32_t layer_count)
{
   const struct v3dv_cmd_buffer_state *state = &cmd_buffer->state;
   const struct v3dv_framebuffer *framebuffer = state->framebuffer;
   const struct v3dv_subpass *subpass =
      &state->pass->subpasses[state->subpass_idx];
   struct v3dv_job *job = cmd_buffer->state.job;
   assert(job);

   /* Check how many color attachments we have and also if we have a
    * depth/stencil attachment.
    */
   uint32_t color_attachment_count = 0;
   VkClearAttachment color_attachments[4];
   const VkClearDepthStencilValue *ds_clear_value = NULL;
   uint8_t internal_depth_type = V3D_INTERNAL_TYPE_DEPTH_32F;
   for (uint32_t i = 0; i < attachment_count; i++) {
      if (attachments[i].aspectMask & (VK_IMAGE_ASPECT_DEPTH_BIT |
                                       VK_IMAGE_ASPECT_STENCIL_BIT)) {
         assert(subpass->ds_attachment.attachment != VK_ATTACHMENT_UNUSED);
         ds_clear_value = &attachments[i].clearValue.depthStencil;
         struct v3dv_render_pass_attachment *att =
            &state->pass->attachments[subpass->ds_attachment.attachment];
         internal_depth_type = v3dv_get_internal_depth_type(att->desc.format);
      } else if (attachments[i].aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) {
         color_attachments[color_attachment_count++] = attachments[i];
      }
   }

   uint8_t internal_bpp;
   bool msaa;
   v3dv_framebuffer_compute_internal_bpp_msaa(framebuffer, subpass,
                                              &internal_bpp, &msaa);

   v3dv_job_start_frame(job,
                        framebuffer->width,
                        framebuffer->height,
                        framebuffer->layers,
                        color_attachment_count,
                        internal_bpp, msaa);

   struct v3dv_cl *rcl = &job->rcl;
   v3dv_cl_ensure_space_with_branch(rcl, 200 +
                                    layer_count * 256 *
                                    cl_packet_length(SUPERTILE_COORDINATES));
   v3dv_return_if_oom(cmd_buffer, NULL);

   const struct v3dv_frame_tiling *tiling = &job->frame_tiling;
   cl_emit(rcl, TILE_RENDERING_MODE_CFG_COMMON, config) {
      config.early_z_disable = true;
      config.image_width_pixels = framebuffer->width;
      config.image_height_pixels = framebuffer->height;
      config.number_of_render_targets = MAX2(color_attachment_count, 1);
      config.multisample_mode_4x = false; /* FIXME */
      config.maximum_bpp_of_all_render_targets = tiling->internal_bpp;
      config.internal_depth_type = internal_depth_type;
   }

   for (uint32_t i = 0; i < color_attachment_count; i++) {
      uint32_t rt_idx = color_attachments[i].colorAttachment;
      uint32_t attachment_idx = subpass->color_attachments[rt_idx].attachment;
      if (attachment_idx == VK_ATTACHMENT_UNUSED)
         continue;

      const struct v3dv_render_pass_attachment *attachment =
         &state->pass->attachments[attachment_idx];

      uint32_t internal_type, internal_bpp, internal_size;
      const struct v3dv_format *format =
         v3dv_get_format(attachment->desc.format);
      v3dv_get_internal_type_bpp_for_output_format(format->rt_type,
                                                   &internal_type,
                                                   &internal_bpp);
      internal_size = 4 << internal_bpp;

      uint32_t clear_color[4] = { 0 };
      v3dv_get_hw_clear_color(&color_attachments[i].clearValue.color,
                              internal_type,
                              internal_size,
                              clear_color);

      struct v3dv_image_view *iview = framebuffer->attachments[attachment_idx];
      const struct v3dv_image *image = iview->image;
      const struct v3d_resource_slice *slice = &image->slices[iview->base_level];

      uint32_t clear_pad = 0;
      if (slice->tiling == VC5_TILING_UIF_NO_XOR ||
          slice->tiling == VC5_TILING_UIF_XOR) {
         int uif_block_height = v3d_utile_height(image->cpp) * 2;

         uint32_t implicit_padded_height =
            align(framebuffer->height, uif_block_height) / uif_block_height;

         if (slice->padded_height_of_output_image_in_uif_blocks -
             implicit_padded_height >= 15) {
            clear_pad = slice->padded_height_of_output_image_in_uif_blocks;
         }
      }

      cl_emit(rcl, TILE_RENDERING_MODE_CFG_CLEAR_COLORS_PART1, clear) {
         clear.clear_color_low_32_bits = clear_color[0];
         clear.clear_color_next_24_bits = clear_color[1] & 0xffffff;
         clear.render_target_number = i;
      };

      if (iview->internal_bpp >= V3D_INTERNAL_BPP_64) {
         cl_emit(rcl, TILE_RENDERING_MODE_CFG_CLEAR_COLORS_PART2, clear) {
            clear.clear_color_mid_low_32_bits =
              ((clear_color[1] >> 24) | (clear_color[2] << 8));
            clear.clear_color_mid_high_24_bits =
              ((clear_color[2] >> 24) | ((clear_color[3] & 0xffff) << 8));
            clear.render_target_number = i;
         };
      }

      if (iview->internal_bpp >= V3D_INTERNAL_BPP_128 || clear_pad) {
         cl_emit(rcl, TILE_RENDERING_MODE_CFG_CLEAR_COLORS_PART3, clear) {
            clear.uif_padded_height_in_uif_blocks = clear_pad;
            clear.clear_color_high_16_bits = clear_color[3] >> 16;
            clear.render_target_number = i;
         };
      }
   }

   cl_emit(rcl, TILE_RENDERING_MODE_CFG_COLOR, rt) {
      v3dv_render_pass_setup_render_target(cmd_buffer, 0,
                                           &rt.render_target_0_internal_bpp,
                                           &rt.render_target_0_internal_type,
                                           &rt.render_target_0_clamp);
      v3dv_render_pass_setup_render_target(cmd_buffer, 1,
                                           &rt.render_target_1_internal_bpp,
                                           &rt.render_target_1_internal_type,
                                           &rt.render_target_1_clamp);
      v3dv_render_pass_setup_render_target(cmd_buffer, 2,
                                           &rt.render_target_2_internal_bpp,
                                           &rt.render_target_2_internal_type,
                                           &rt.render_target_2_clamp);
      v3dv_render_pass_setup_render_target(cmd_buffer, 3,
                                           &rt.render_target_3_internal_bpp,
                                           &rt.render_target_3_internal_type,
                                           &rt.render_target_3_clamp);
   }

   cl_emit(rcl, TILE_RENDERING_MODE_CFG_ZS_CLEAR_VALUES, clear) {
      clear.z_clear_value = ds_clear_value ? ds_clear_value->depth : 1.0f;
      clear.stencil_clear_value = ds_clear_value ? ds_clear_value->stencil : 0;
   };

   cl_emit(rcl, TILE_LIST_INITIAL_BLOCK_SIZE, init) {
      init.use_auto_chained_tile_lists = true;
      init.size_of_first_block_in_chained_tile_lists =
         TILE_ALLOCATION_BLOCK_SIZE_64B;
   }

   for (int layer = base_layer; layer < base_layer + layer_count; layer++) {
      emit_tlb_clear_layer_rcl(cmd_buffer,
                               attachment_count,
                               attachments,
                               layer);
   }

   cl_emit(rcl, END_OF_RENDERING, end);
}

static void
emit_tlb_clear(struct v3dv_cmd_buffer *cmd_buffer,
               uint32_t attachment_count,
               const VkClearAttachment *attachments,
               uint32_t base_layer,
               uint32_t layer_count)
{
   struct v3dv_job *job =
      v3dv_cmd_buffer_start_job(cmd_buffer, cmd_buffer->state.subpass_idx,
                                V3DV_JOB_TYPE_GPU_CL);

   /* vkCmdClearAttachments runs inside a render pass */
   job->is_subpass_continue = true;

   emit_tlb_clear_job(cmd_buffer,
                      attachment_count,
                      attachments,
                      base_layer, layer_count);

   v3dv_cmd_buffer_subpass_resume(cmd_buffer, cmd_buffer->state.subpass_idx);
}

static bool
is_subrect(const VkRect2D *r0, const VkRect2D *r1)
{
   return r0->offset.x <= r1->offset.x &&
          r0->offset.y <= r1->offset.y &&
          r0->offset.x + r0->extent.width >= r1->offset.x + r1->extent.width &&
          r0->offset.y + r0->extent.height >= r1->offset.y + r1->extent.height;
}

static bool
can_use_tlb_clear(struct v3dv_cmd_buffer *cmd_buffer,
                  uint32_t rect_count,
                  const VkClearRect* rects)
{
   const struct v3dv_framebuffer *framebuffer = cmd_buffer->state.framebuffer;

   const VkRect2D *render_area = &cmd_buffer->state.render_area;

   /* Check if we are clearing a single region covering the entire framebuffer
    * and that we are not constrained by the current render area.
    *
    * From the Vulkan 1.0 spec:
    *
    *   "The vkCmdClearAttachments command is not affected by the bound
    *    pipeline state."
    *
    * So we can ignore scissor and viewport state for this check.
    */
   const VkRect2D fb_rect = {
      { 0, 0 },
      { framebuffer->width, framebuffer->height }
   };

   return rect_count == 1 &&
          is_subrect(&rects[0].rect, &fb_rect) &&
          is_subrect(render_area, &fb_rect);
}

static void
handle_deferred_clear_attachments(struct v3dv_cmd_buffer *cmd_buffer,
                                  uint32_t attachmentCount,
                                  const VkClearAttachment *pAttachments,
                                  uint32_t rectCount,
                                  const VkClearRect *pRects)
{
   /* Finish the current job */
   v3dv_cmd_buffer_finish_job(cmd_buffer);

   /* Add a deferred clear attachments job right after that we will process
    * when we execute this secondary command buffer into a primary.
    */
   struct v3dv_job *job =
      v3dv_cmd_buffer_create_cpu_job(cmd_buffer->device,
                                     V3DV_JOB_TYPE_CPU_CLEAR_ATTACHMENTS,
                                     cmd_buffer,
                                     cmd_buffer->state.subpass_idx);
   v3dv_return_if_oom(cmd_buffer, NULL);

   job->cpu.clear_attachments.rects =
      vk_alloc(&cmd_buffer->device->alloc,
               sizeof(VkClearRect) * rectCount, 8,
               VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (!job->cpu.clear_attachments.rects) {
      v3dv_flag_oom(cmd_buffer, NULL);
      return;
   }

   job->cpu.clear_attachments.attachment_count = attachmentCount;
   memcpy(job->cpu.clear_attachments.attachments, pAttachments,
          sizeof(VkClearAttachment) * attachmentCount);

   job->cpu.clear_attachments.rect_count = rectCount;
   memcpy(job->cpu.clear_attachments.rects, pRects,
          sizeof(VkClearRect) * rectCount);

   list_addtail(&job->list_link, &cmd_buffer->jobs);

   /* Resume the subpass so we can continue recording commands */
   v3dv_cmd_buffer_subpass_resume(cmd_buffer,
                                  cmd_buffer->state.subpass_idx);
}

static bool
all_clear_rects_in_base_layer(uint32_t rect_count, const VkClearRect *rects)
{
   for (uint32_t i = 0; i < rect_count; i++) {
      if (rects[i].baseArrayLayer != 0 || rects[i].layerCount != 1)
         return false;
   }
   return true;
}

void
v3dv_CmdClearAttachments(VkCommandBuffer commandBuffer,
                         uint32_t attachmentCount,
                         const VkClearAttachment *pAttachments,
                         uint32_t rectCount,
                         const VkClearRect *pRects)
{
   V3DV_FROM_HANDLE(v3dv_cmd_buffer, cmd_buffer, commandBuffer);

   /* We can only clear attachments in the current subpass */
   assert(attachmentCount <= 5); /* 4 color + D/S */

   /* Clear attachments may clear multiple layers of the framebuffer, which
    * currently requires that we emit multiple jobs (one per layer) and
    * therefore requires that we have the framebuffer information available
    * to select the destination layers.
    *
    * For secondary command buffers the framebuffer state may not be available
    * until they are executed inside a primary command buffer, so in that case
    * we need to defer recording of the command until that moment.
    *
    * FIXME: once we add support for geometry shaders in the driver we could
    * avoid emitting a job per layer to implement this by always using the clear
    * rect path below with a passthrough geometry shader to select the layer to
    * clear. If we did that we would not need to special case secondary command
    * buffers here and we could ensure that any secondary command buffer in a
    * render pass only has on job with a partial CL, which would simplify things
    * quite a bit.
    */
   if (!cmd_buffer->state.framebuffer) {
      assert(cmd_buffer->level == VK_COMMAND_BUFFER_LEVEL_SECONDARY);
      handle_deferred_clear_attachments(cmd_buffer,
                                        attachmentCount, pAttachments,
                                        rectCount, pRects);
      return;
   }

   assert(cmd_buffer->state.framebuffer);

   struct v3dv_render_pass *pass = cmd_buffer->state.pass;

   assert(cmd_buffer->state.subpass_idx < pass->subpass_count);
   struct v3dv_subpass *subpass =
      &cmd_buffer->state.pass->subpasses[cmd_buffer->state.subpass_idx];

   /* First we try to handle this by emitting a clear rect inside the
    * current job for this subpass. This should be optimal but this method
    * cannot handle clearing layers other than the base layer, since we don't
    * support any form of layered rendering yet.
    */
   if (all_clear_rects_in_base_layer(rectCount, pRects)) {
      for (uint32_t i = 0; i < attachmentCount; i++) {
         if (pAttachments[i].aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) {
            emit_subpass_color_clear_rects(cmd_buffer, pass, subpass,
                                           pAttachments[i].colorAttachment,
                                           &pAttachments[i].clearValue.color,
                                           rectCount, pRects);
         } else {
            emit_subpass_ds_clear_rects(cmd_buffer, pass, subpass,
                                        pAttachments[i].aspectMask,
                                        &pAttachments[i].clearValue.depthStencil,
                                        rectCount, pRects);
         }
      }
      return;
   }

   perf_debug("Falling back to slow path for vkCmdClearAttachments due to "
              "clearing layers other than the base array layer.\n");

   /* If we can't handle this as a draw call inside the current job then we
    * will have to spawn jobs for the clears, which will be slow. In that case,
    * try to use the TLB to clear if possible.
    */
   if (can_use_tlb_clear(cmd_buffer, rectCount, pRects)) {
      emit_tlb_clear(cmd_buffer, attachmentCount, pAttachments,
                     pRects[0].baseArrayLayer, pRects[0].layerCount);
      return;
   }

   /* Otherwise, fall back to drawing rects with the clear value using a
    * separate job. This is the slowest path.
    */
   for (uint32_t i = 0; i < attachmentCount; i++) {
      uint32_t attachment_idx = VK_ATTACHMENT_UNUSED;

      if (pAttachments[i].aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) {
         uint32_t rt_idx = pAttachments[i].colorAttachment;
         attachment_idx = subpass->color_attachments[rt_idx].attachment;
      } else if (pAttachments[i].aspectMask & (VK_IMAGE_ASPECT_DEPTH_BIT |
                                               VK_IMAGE_ASPECT_STENCIL_BIT)) {
         attachment_idx = subpass->ds_attachment.attachment;
      }

      if (attachment_idx == VK_ATTACHMENT_UNUSED)
         continue;

      if (pAttachments[i].aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) {
         const uint32_t components = VK_COLOR_COMPONENT_R_BIT |
                                     VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT |
                                     VK_COLOR_COMPONENT_A_BIT;
         const uint32_t samples =
            cmd_buffer->state.pass->attachments[attachment_idx].desc.samples;
         const VkFormat format =
            cmd_buffer->state.pass->attachments[attachment_idx].desc.format;
         for (uint32_t j = 0; j < rectCount; j++) {
            emit_color_clear_rect(cmd_buffer,
                                  attachment_idx,
                                  format,
                                  samples,
                                  components,
                                  pAttachments[i].clearValue.color,
                                  &pRects[j]);
         }
      } else {
         for (uint32_t j = 0; j < rectCount; j++) {
            emit_ds_clear_rect(cmd_buffer,
                               pAttachments[i].aspectMask,
                               attachment_idx,
                               pAttachments[i].clearValue.depthStencil,
                               &pRects[j]);
         }
      }
   }
}
