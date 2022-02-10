/*
 * Copyright © 2021 Collabora Ltd.
 *
 * Derived from tu_cmd_buffer.c which is:
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

#include "panvk_private.h"

#include "pan_encoder.h"

#include "util/rounding.h"
#include "vk_format.h"

void
panvk_CmdBindVertexBuffers(VkCommandBuffer commandBuffer,
                           uint32_t firstBinding,
                           uint32_t bindingCount,
                           const VkBuffer *pBuffers,
                           const VkDeviceSize *pOffsets)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);

   assert(firstBinding + bindingCount <= MAX_VBS);

   for (uint32_t i = 0; i < bindingCount; i++) {
      struct panvk_buffer *buf = panvk_buffer_from_handle(pBuffers[i]);

      cmdbuf->state.vb.bufs[firstBinding + i].address = buf->bo->ptr.gpu + pOffsets[i];
      cmdbuf->state.vb.bufs[firstBinding + i].size = buf->size - pOffsets[i];
   }
   cmdbuf->state.vb.count = MAX2(cmdbuf->state.vb.count, firstBinding + bindingCount);
   cmdbuf->state.vb.attrib_bufs = cmdbuf->state.vb.attribs = 0;
}

void
panvk_CmdBindIndexBuffer(VkCommandBuffer commandBuffer,
                         VkBuffer buffer,
                         VkDeviceSize offset,
                         VkIndexType indexType)
{
   panvk_stub();
}

void
panvk_CmdBindDescriptorSets(VkCommandBuffer commandBuffer,
                            VkPipelineBindPoint pipelineBindPoint,
                            VkPipelineLayout _layout,
                            uint32_t firstSet,
                            uint32_t descriptorSetCount,
                            const VkDescriptorSet *pDescriptorSets,
                            uint32_t dynamicOffsetCount,
                            const uint32_t *pDynamicOffsets)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   VK_FROM_HANDLE(panvk_pipeline_layout, layout, _layout);

   struct panvk_descriptor_state *descriptors_state =
      &cmdbuf->bind_points[pipelineBindPoint].desc_state;

   for (unsigned i = 0; i < descriptorSetCount; ++i) {
      unsigned idx = i + firstSet;
      VK_FROM_HANDLE(panvk_descriptor_set, set, pDescriptorSets[i]);

      descriptors_state->sets[idx].set = set;

      if (layout->num_dynoffsets) {
         assert(dynamicOffsetCount >= set->layout->num_dynoffsets);

         descriptors_state->sets[idx].dynoffsets =
            pan_pool_alloc_aligned(&cmdbuf->desc_pool.base,
                                   ALIGN(layout->num_dynoffsets, 4) *
                                   sizeof(*pDynamicOffsets),
                                   16);
         memcpy(descriptors_state->sets[idx].dynoffsets.cpu,
                pDynamicOffsets,
                sizeof(*pDynamicOffsets) * set->layout->num_dynoffsets);
         dynamicOffsetCount -= set->layout->num_dynoffsets;
         pDynamicOffsets += set->layout->num_dynoffsets;
      }

      if (set->layout->num_ubos || set->layout->num_dynoffsets)
         descriptors_state->ubos = 0;

      if (set->layout->num_textures)
         descriptors_state->textures = 0;

      if (set->layout->num_samplers)
         descriptors_state->samplers = 0;
   }

   assert(!dynamicOffsetCount);
}

void
panvk_CmdPushConstants(VkCommandBuffer commandBuffer,
                       VkPipelineLayout layout,
                       VkShaderStageFlags stageFlags,
                       uint32_t offset,
                       uint32_t size,
                       const void *pValues)
{
   panvk_stub();
}

void
panvk_CmdBindPipeline(VkCommandBuffer commandBuffer,
                      VkPipelineBindPoint pipelineBindPoint,
                      VkPipeline _pipeline)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   VK_FROM_HANDLE(panvk_pipeline, pipeline, _pipeline);

   cmdbuf->bind_points[pipelineBindPoint].pipeline = pipeline;
   cmdbuf->state.fs_rsd = 0;
   memset(cmdbuf->bind_points[pipelineBindPoint].desc_state.sysvals, 0,
          sizeof(cmdbuf->bind_points[0].desc_state.sysvals));

   if (pipelineBindPoint == VK_PIPELINE_BIND_POINT_GRAPHICS) {
      cmdbuf->state.varyings = pipeline->varyings;

      if (!(pipeline->dynamic_state_mask & BITFIELD_BIT(VK_DYNAMIC_STATE_VIEWPORT)))
         cmdbuf->state.viewport = pipeline->viewport;
      if (!(pipeline->dynamic_state_mask & BITFIELD_BIT(VK_DYNAMIC_STATE_SCISSOR)))
         cmdbuf->state.scissor = pipeline->scissor;
   }

   /* Sysvals are passed through UBOs, we need dirty the UBO array if the
    * pipeline contain shaders using sysvals.
    */
   if (pipeline->num_sysvals)
      cmdbuf->bind_points[pipelineBindPoint].desc_state.ubos = 0;
}

void
panvk_CmdSetViewport(VkCommandBuffer commandBuffer,
                     uint32_t firstViewport,
                     uint32_t viewportCount,
                     const VkViewport *pViewports)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   assert(viewportCount == 1);
   assert(!firstViewport);

   cmdbuf->state.viewport = pViewports[0];
   cmdbuf->state.vpd = 0;
   cmdbuf->state.dirty |= PANVK_DYNAMIC_VIEWPORT;
}

void
panvk_CmdSetScissor(VkCommandBuffer commandBuffer,
                    uint32_t firstScissor,
                    uint32_t scissorCount,
                    const VkRect2D *pScissors)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   assert(scissorCount == 1);
   assert(!firstScissor);

   cmdbuf->state.scissor = pScissors[0];
   cmdbuf->state.vpd = 0;
   cmdbuf->state.dirty |= PANVK_DYNAMIC_SCISSOR;
}

void
panvk_CmdSetLineWidth(VkCommandBuffer commandBuffer, float lineWidth)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);

   cmdbuf->state.rast.line_width = lineWidth;
   cmdbuf->state.dirty |= PANVK_DYNAMIC_LINE_WIDTH;
}

void
panvk_CmdSetDepthBias(VkCommandBuffer commandBuffer,
                      float depthBiasConstantFactor,
                      float depthBiasClamp,
                      float depthBiasSlopeFactor)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);

   cmdbuf->state.rast.depth_bias.constant_factor = depthBiasConstantFactor;
   cmdbuf->state.rast.depth_bias.clamp = depthBiasClamp;
   cmdbuf->state.rast.depth_bias.slope_factor = depthBiasSlopeFactor;
   cmdbuf->state.dirty |= PANVK_DYNAMIC_DEPTH_BIAS;
   cmdbuf->state.fs_rsd = 0;
}

void
panvk_CmdSetBlendConstants(VkCommandBuffer commandBuffer,
                           const float blendConstants[4])
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);

   for (unsigned i = 0; i < 4; i++)
      cmdbuf->state.blend.constants[i] = CLAMP(blendConstants[i], 0.0f, 1.0f);

   cmdbuf->state.dirty |= PANVK_DYNAMIC_BLEND_CONSTANTS;
   cmdbuf->state.fs_rsd = 0;
}

void
panvk_CmdSetDepthBounds(VkCommandBuffer commandBuffer,
                        float minDepthBounds,
                        float maxDepthBounds)
{
   panvk_stub();
}

void
panvk_CmdSetStencilCompareMask(VkCommandBuffer commandBuffer,
                               VkStencilFaceFlags faceMask,
                               uint32_t compareMask)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);

   if (faceMask & VK_STENCIL_FACE_FRONT_BIT)
      cmdbuf->state.zs.s_front.compare_mask = compareMask;

   if (faceMask & VK_STENCIL_FACE_BACK_BIT)
      cmdbuf->state.zs.s_back.compare_mask = compareMask;

   cmdbuf->state.dirty |= PANVK_DYNAMIC_STENCIL_COMPARE_MASK;
   cmdbuf->state.fs_rsd = 0;
}

void
panvk_CmdSetStencilWriteMask(VkCommandBuffer commandBuffer,
                             VkStencilFaceFlags faceMask,
                             uint32_t writeMask)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);

   if (faceMask & VK_STENCIL_FACE_FRONT_BIT)
      cmdbuf->state.zs.s_front.write_mask = writeMask;

   if (faceMask & VK_STENCIL_FACE_BACK_BIT)
      cmdbuf->state.zs.s_back.write_mask = writeMask;

   cmdbuf->state.dirty |= PANVK_DYNAMIC_STENCIL_WRITE_MASK;
   cmdbuf->state.fs_rsd = 0;
}

void
panvk_CmdSetStencilReference(VkCommandBuffer commandBuffer,
                             VkStencilFaceFlags faceMask,
                             uint32_t reference)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);

   if (faceMask & VK_STENCIL_FACE_FRONT_BIT)
      cmdbuf->state.zs.s_front.ref = reference;

   if (faceMask & VK_STENCIL_FACE_BACK_BIT)
      cmdbuf->state.zs.s_back.ref = reference;

   cmdbuf->state.dirty |= PANVK_DYNAMIC_STENCIL_REFERENCE;
   cmdbuf->state.fs_rsd = 0;
}

void
panvk_CmdExecuteCommands(VkCommandBuffer commandBuffer,
                         uint32_t commandBufferCount,
                         const VkCommandBuffer *pCmdBuffers)
{
   panvk_stub();
}

VkResult
panvk_CreateCommandPool(VkDevice _device,
                        const VkCommandPoolCreateInfo *pCreateInfo,
                        const VkAllocationCallbacks *pAllocator,
                        VkCommandPool *pCmdPool)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   struct panvk_cmd_pool *pool;

   pool = vk_object_alloc(&device->vk, pAllocator, sizeof(*pool),
                          VK_OBJECT_TYPE_COMMAND_POOL);
   if (pool == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   if (pAllocator)
      pool->alloc = *pAllocator;
   else
      pool->alloc = device->vk.alloc;

   list_inithead(&pool->active_cmd_buffers);
   list_inithead(&pool->free_cmd_buffers);

   pool->queue_family_index = pCreateInfo->queueFamilyIndex;
   panvk_bo_pool_init(&pool->desc_bo_pool);
   panvk_bo_pool_init(&pool->varying_bo_pool);
   panvk_bo_pool_init(&pool->tls_bo_pool);
   *pCmdPool = panvk_cmd_pool_to_handle(pool);
   return VK_SUCCESS;
}

static void
panvk_cmd_prepare_clear_values(struct panvk_cmd_buffer *cmdbuf,
                               const VkClearValue *in)
{
   for (unsigned i = 0; i < cmdbuf->state.pass->attachment_count; i++) {
       const struct panvk_render_pass_attachment *attachment =
          &cmdbuf->state.pass->attachments[i];
       enum pipe_format fmt = attachment->format;

       if (util_format_is_depth_or_stencil(fmt)) {
          if (attachment->load_op == VK_ATTACHMENT_LOAD_OP_CLEAR ||
              attachment->stencil_load_op == VK_ATTACHMENT_LOAD_OP_CLEAR) {
             cmdbuf->state.clear[i].depth = in[i].depthStencil.depth;
             cmdbuf->state.clear[i].stencil = in[i].depthStencil.stencil;
          } else {
             cmdbuf->state.clear[i].depth = 0;
             cmdbuf->state.clear[i].stencil = 0;
          }
       } else {
          if (attachment->load_op == VK_ATTACHMENT_LOAD_OP_CLEAR) {
             union pipe_color_union *col = (union pipe_color_union *) &in[i].color;
             pan_pack_color(cmdbuf->state.clear[i].color, col, fmt, false);
          } else {
             memset(cmdbuf->state.clear[i].color, 0, sizeof(cmdbuf->state.clear[0].color));
          }
       }
   }
}

void
panvk_cmd_fb_info_set_subpass(struct panvk_cmd_buffer *cmdbuf)
{
   const struct panvk_subpass *subpass = cmdbuf->state.subpass;
   struct pan_fb_info *fbinfo = &cmdbuf->state.fb.info;
   const struct panvk_framebuffer *fb = cmdbuf->state.framebuffer;
   const struct panvk_clear_value *clears = cmdbuf->state.clear;
   struct panvk_image_view *view;

   fbinfo->nr_samples = 1;
   fbinfo->rt_count = subpass->color_count;
   memset(&fbinfo->bifrost.pre_post.dcds, 0, sizeof(fbinfo->bifrost.pre_post.dcds));

   for (unsigned cb = 0; cb < subpass->color_count; cb++) {
      int idx = subpass->color_attachments[cb].idx;
      view = idx != VK_ATTACHMENT_UNUSED ?
             fb->attachments[idx].iview : NULL;
      if (!view)
         continue;
      fbinfo->rts[cb].view = &view->pview;
      fbinfo->rts[cb].clear = subpass->color_attachments[cb].clear;
      fbinfo->rts[cb].preload = subpass->color_attachments[cb].preload;
      fbinfo->rts[cb].crc_valid = &cmdbuf->state.fb.crc_valid[cb];

      memcpy(fbinfo->rts[cb].clear_value, clears[idx].color,
             sizeof(fbinfo->rts[cb].clear_value));
      fbinfo->nr_samples =
         MAX2(fbinfo->nr_samples, view->pview.image->layout.nr_samples);
   }

   if (subpass->zs_attachment.idx != VK_ATTACHMENT_UNUSED) {
      view = fb->attachments[subpass->zs_attachment.idx].iview;
      const struct util_format_description *fdesc =
         util_format_description(view->pview.format);

      fbinfo->nr_samples =
         MAX2(fbinfo->nr_samples, view->pview.image->layout.nr_samples);

      if (util_format_has_depth(fdesc)) {
         fbinfo->zs.clear.z = subpass->zs_attachment.clear;
         fbinfo->zs.clear_value.depth = clears[subpass->zs_attachment.idx].depth;
         fbinfo->zs.view.zs = &view->pview;
      }

      if (util_format_has_stencil(fdesc)) {
         fbinfo->zs.clear.s = subpass->zs_attachment.clear;
         fbinfo->zs.clear_value.stencil = clears[subpass->zs_attachment.idx].stencil;
         if (!fbinfo->zs.view.zs)
            fbinfo->zs.view.s = &view->pview;
      }
   }
}

void
panvk_cmd_fb_info_init(struct panvk_cmd_buffer *cmdbuf)
{
   struct pan_fb_info *fbinfo = &cmdbuf->state.fb.info;
   const struct panvk_framebuffer *fb = cmdbuf->state.framebuffer;

   memset(cmdbuf->state.fb.crc_valid, 0, sizeof(cmdbuf->state.fb.crc_valid));

   *fbinfo = (struct pan_fb_info) {
      .width = fb->width,
      .height = fb->height,
      .extent.maxx = fb->width - 1,
      .extent.maxy = fb->height - 1,
   };
}

void
panvk_CmdBeginRenderPass2(VkCommandBuffer commandBuffer,
                          const VkRenderPassBeginInfo *pRenderPassBegin,
                          const VkSubpassBeginInfo *pSubpassBeginInfo)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   VK_FROM_HANDLE(panvk_render_pass, pass, pRenderPassBegin->renderPass);
   VK_FROM_HANDLE(panvk_framebuffer, fb, pRenderPassBegin->framebuffer);

   cmdbuf->state.pass = pass;
   cmdbuf->state.subpass = pass->subpasses;
   cmdbuf->state.framebuffer = fb;
   cmdbuf->state.render_area = pRenderPassBegin->renderArea;
   cmdbuf->state.batch = vk_zalloc(&cmdbuf->pool->alloc,
                                   sizeof(*cmdbuf->state.batch), 8,
                                   VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   util_dynarray_init(&cmdbuf->state.batch->jobs, NULL);
   util_dynarray_init(&cmdbuf->state.batch->event_ops, NULL);
   assert(pRenderPassBegin->clearValueCount <= pass->attachment_count);
   cmdbuf->state.clear =
      vk_zalloc(&cmdbuf->pool->alloc,
                sizeof(*cmdbuf->state.clear) * pass->attachment_count,
                8, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   panvk_cmd_prepare_clear_values(cmdbuf, pRenderPassBegin->pClearValues);
   panvk_cmd_fb_info_init(cmdbuf);
   panvk_cmd_fb_info_set_subpass(cmdbuf);
}

void
panvk_CmdBeginRenderPass(VkCommandBuffer cmd,
                         const VkRenderPassBeginInfo *info,
                         VkSubpassContents contents)
{
   VkSubpassBeginInfo subpass_info = {
      .sType = VK_STRUCTURE_TYPE_SUBPASS_BEGIN_INFO,
      .contents = contents
   };

   return panvk_CmdBeginRenderPass2(cmd, info, &subpass_info);
}

void
panvk_cmd_preload_fb_after_batch_split(struct panvk_cmd_buffer *cmdbuf)
{
   for (unsigned i = 0; i < cmdbuf->state.fb.info.rt_count; i++) {
      if (cmdbuf->state.fb.info.rts[i].view) {
         cmdbuf->state.fb.info.rts[i].clear = false;
         cmdbuf->state.fb.info.rts[i].preload = true;
      }
   }

   if (cmdbuf->state.fb.info.zs.view.zs) {
      cmdbuf->state.fb.info.zs.clear.z = false;
      cmdbuf->state.fb.info.zs.preload.z = true;
   }

   if (cmdbuf->state.fb.info.zs.view.s ||
       (cmdbuf->state.fb.info.zs.view.zs &&
        util_format_is_depth_and_stencil(cmdbuf->state.fb.info.zs.view.zs->format))) {
      cmdbuf->state.fb.info.zs.clear.s = false;
      cmdbuf->state.fb.info.zs.preload.s = true;
   }
}

struct panvk_batch *
panvk_cmd_open_batch(struct panvk_cmd_buffer *cmdbuf)
{
   assert(!cmdbuf->state.batch);
   cmdbuf->state.batch = vk_zalloc(&cmdbuf->pool->alloc,
                                   sizeof(*cmdbuf->state.batch), 8,
                                   VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   assert(cmdbuf->state.batch);
   return cmdbuf->state.batch;
}

void
panvk_CmdDrawIndexed(VkCommandBuffer commandBuffer,
                     uint32_t indexCount,
                     uint32_t instanceCount,
                     uint32_t firstIndex,
                     int32_t vertexOffset,
                     uint32_t firstInstance)
{
   panvk_stub();
}

void
panvk_CmdDrawIndirect(VkCommandBuffer commandBuffer,
                      VkBuffer _buffer,
                      VkDeviceSize offset,
                      uint32_t drawCount,
                      uint32_t stride)
{
   panvk_stub();
}

void
panvk_CmdDrawIndexedIndirect(VkCommandBuffer commandBuffer,
                             VkBuffer _buffer,
                             VkDeviceSize offset,
                             uint32_t drawCount,
                             uint32_t stride)
{
   panvk_stub();
}

void
panvk_CmdDispatchBase(VkCommandBuffer commandBuffer,
                      uint32_t base_x,
                      uint32_t base_y,
                      uint32_t base_z,
                      uint32_t x,
                      uint32_t y,
                      uint32_t z)
{
   panvk_stub();
}

void
panvk_CmdDispatch(VkCommandBuffer commandBuffer,
                  uint32_t x,
                  uint32_t y,
                  uint32_t z)
{
   panvk_stub();
}

void
panvk_CmdDispatchIndirect(VkCommandBuffer commandBuffer,
                          VkBuffer _buffer,
                          VkDeviceSize offset)
{
   panvk_stub();
}

void
panvk_CmdSetDeviceMask(VkCommandBuffer commandBuffer, uint32_t deviceMask)
{
   panvk_stub();
}
