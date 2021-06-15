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
#include "pipe/p_context.h"
#include "vk_util.h"

static VkResult lvp_create_cmd_buffer(
   struct lvp_device *                         device,
   struct lvp_cmd_pool *                       pool,
   VkCommandBufferLevel                        level,
   VkCommandBuffer*                            pCommandBuffer)
{
   struct lvp_cmd_buffer *cmd_buffer;

   cmd_buffer = vk_alloc(&pool->alloc, sizeof(*cmd_buffer), 8,
                         VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (cmd_buffer == NULL)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &cmd_buffer->base,
                       VK_OBJECT_TYPE_COMMAND_BUFFER);
   cmd_buffer->device = device;
   cmd_buffer->pool = pool;
   list_inithead(&cmd_buffer->cmds);
   cmd_buffer->last_emit = &cmd_buffer->cmds;
   cmd_buffer->status = LVP_CMD_BUFFER_STATUS_INITIAL;
   if (pool) {
      list_addtail(&cmd_buffer->pool_link, &pool->cmd_buffers);
   } else {
      /* Init the pool_link so we can safefly call list_del when we destroy
       * the command buffer
       */
      list_inithead(&cmd_buffer->pool_link);
   }
   *pCommandBuffer = lvp_cmd_buffer_to_handle(cmd_buffer);

   return VK_SUCCESS;
}

static void
lvp_cmd_buffer_free_all_cmds(struct lvp_cmd_buffer *cmd_buffer)
{
   struct lvp_cmd_buffer_entry *tmp, *cmd;
   LIST_FOR_EACH_ENTRY_SAFE(cmd, tmp, &cmd_buffer->cmds, cmd_link) {
      list_del(&cmd->cmd_link);
      vk_free(&cmd_buffer->pool->alloc, cmd);
   }
}

static VkResult lvp_reset_cmd_buffer(struct lvp_cmd_buffer *cmd_buffer)
{
   lvp_cmd_buffer_free_all_cmds(cmd_buffer);
   list_inithead(&cmd_buffer->cmds);
   cmd_buffer->last_emit = &cmd_buffer->cmds;
   cmd_buffer->status = LVP_CMD_BUFFER_STATUS_INITIAL;
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL lvp_AllocateCommandBuffers(
   VkDevice                                    _device,
   const VkCommandBufferAllocateInfo*          pAllocateInfo,
   VkCommandBuffer*                            pCommandBuffers)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   LVP_FROM_HANDLE(lvp_cmd_pool, pool, pAllocateInfo->commandPool);

   VkResult result = VK_SUCCESS;
   uint32_t i;

   for (i = 0; i < pAllocateInfo->commandBufferCount; i++) {

      if (!list_is_empty(&pool->free_cmd_buffers)) {
         struct lvp_cmd_buffer *cmd_buffer = list_first_entry(&pool->free_cmd_buffers, struct lvp_cmd_buffer, pool_link);

         list_del(&cmd_buffer->pool_link);
         list_addtail(&cmd_buffer->pool_link, &pool->cmd_buffers);

         result = lvp_reset_cmd_buffer(cmd_buffer);
         cmd_buffer->level = pAllocateInfo->level;
         vk_object_base_reset(&cmd_buffer->base);

         pCommandBuffers[i] = lvp_cmd_buffer_to_handle(cmd_buffer);
      } else {
         result = lvp_create_cmd_buffer(device, pool, pAllocateInfo->level,
                                        &pCommandBuffers[i]);
         if (result != VK_SUCCESS)
            break;
      }
   }

   if (result != VK_SUCCESS) {
      lvp_FreeCommandBuffers(_device, pAllocateInfo->commandPool,
                             i, pCommandBuffers);
      memset(pCommandBuffers, 0,
             sizeof(*pCommandBuffers) * pAllocateInfo->commandBufferCount);
   }

   return result;
}

static void
lvp_cmd_buffer_destroy(struct lvp_cmd_buffer *cmd_buffer)
{
   lvp_cmd_buffer_free_all_cmds(cmd_buffer);
   list_del(&cmd_buffer->pool_link);
   vk_object_base_finish(&cmd_buffer->base);
   vk_free(&cmd_buffer->pool->alloc, cmd_buffer);
}

VKAPI_ATTR void VKAPI_CALL lvp_FreeCommandBuffers(
   VkDevice                                    device,
   VkCommandPool                               commandPool,
   uint32_t                                    commandBufferCount,
   const VkCommandBuffer*                      pCommandBuffers)
{
   for (uint32_t i = 0; i < commandBufferCount; i++) {
      LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, pCommandBuffers[i]);

      if (cmd_buffer) {
         if (cmd_buffer->pool) {
            list_del(&cmd_buffer->pool_link);
            list_addtail(&cmd_buffer->pool_link, &cmd_buffer->pool->free_cmd_buffers);
         } else
            lvp_cmd_buffer_destroy(cmd_buffer);
      }
   }
}

VKAPI_ATTR VkResult VKAPI_CALL lvp_ResetCommandBuffer(
   VkCommandBuffer                             commandBuffer,
   VkCommandBufferResetFlags                   flags)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);

   return lvp_reset_cmd_buffer(cmd_buffer);
}

VKAPI_ATTR VkResult VKAPI_CALL lvp_BeginCommandBuffer(
   VkCommandBuffer                             commandBuffer,
   const VkCommandBufferBeginInfo*             pBeginInfo)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   VkResult result;
   if (cmd_buffer->status != LVP_CMD_BUFFER_STATUS_INITIAL) {
      result = lvp_reset_cmd_buffer(cmd_buffer);
      if (result != VK_SUCCESS)
         return result;
   }
   cmd_buffer->status = LVP_CMD_BUFFER_STATUS_RECORDING;
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL lvp_EndCommandBuffer(
   VkCommandBuffer                             commandBuffer)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   cmd_buffer->status = LVP_CMD_BUFFER_STATUS_EXECUTABLE;
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL lvp_CreateCommandPool(
   VkDevice                                    _device,
   const VkCommandPoolCreateInfo*              pCreateInfo,
   const VkAllocationCallbacks*                pAllocator,
   VkCommandPool*                              pCmdPool)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   struct lvp_cmd_pool *pool;

   pool = vk_alloc2(&device->vk.alloc, pAllocator, sizeof(*pool), 8,
                    VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (pool == NULL)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &pool->base,
                       VK_OBJECT_TYPE_COMMAND_POOL);
   if (pAllocator)
      pool->alloc = *pAllocator;
   else
      pool->alloc = device->vk.alloc;

   list_inithead(&pool->cmd_buffers);
   list_inithead(&pool->free_cmd_buffers);

   *pCmdPool = lvp_cmd_pool_to_handle(pool);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL lvp_DestroyCommandPool(
   VkDevice                                    _device,
   VkCommandPool                               commandPool,
   const VkAllocationCallbacks*                pAllocator)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   LVP_FROM_HANDLE(lvp_cmd_pool, pool, commandPool);

   if (!pool)
      return;

   list_for_each_entry_safe(struct lvp_cmd_buffer, cmd_buffer,
                            &pool->cmd_buffers, pool_link) {
      lvp_cmd_buffer_destroy(cmd_buffer);
   }

   list_for_each_entry_safe(struct lvp_cmd_buffer, cmd_buffer,
                            &pool->free_cmd_buffers, pool_link) {
      lvp_cmd_buffer_destroy(cmd_buffer);
   }

   vk_object_base_finish(&pool->base);
   vk_free2(&device->vk.alloc, pAllocator, pool);
}

VKAPI_ATTR VkResult VKAPI_CALL lvp_ResetCommandPool(
   VkDevice                                    device,
   VkCommandPool                               commandPool,
   VkCommandPoolResetFlags                     flags)
{
   LVP_FROM_HANDLE(lvp_cmd_pool, pool, commandPool);
   VkResult result;

   list_for_each_entry(struct lvp_cmd_buffer, cmd_buffer,
                       &pool->cmd_buffers, pool_link) {
      result = lvp_reset_cmd_buffer(cmd_buffer);
      if (result != VK_SUCCESS)
         return result;
   }
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL lvp_TrimCommandPool(
   VkDevice                                    device,
   VkCommandPool                               commandPool,
   VkCommandPoolTrimFlags                      flags)
{
   LVP_FROM_HANDLE(lvp_cmd_pool, pool, commandPool);

   if (!pool)
      return;

   list_for_each_entry_safe(struct lvp_cmd_buffer, cmd_buffer,
                            &pool->free_cmd_buffers, pool_link) {
      lvp_cmd_buffer_destroy(cmd_buffer);
   }
}

static struct lvp_cmd_buffer_entry *cmd_buf_entry_alloc_size(struct lvp_cmd_buffer *cmd_buffer,
                                                             uint32_t extra_size,
                                                             enum lvp_cmds type)
{
   struct lvp_cmd_buffer_entry *cmd;
   uint32_t cmd_size = sizeof(*cmd) + extra_size;
   cmd = vk_alloc(&cmd_buffer->pool->alloc,
                  cmd_size,
                  8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!cmd)
      return NULL;

   cmd->cmd_type = type;
   return cmd;
}

static struct lvp_cmd_buffer_entry *cmd_buf_entry_alloc(struct lvp_cmd_buffer *cmd_buffer,
                                                        enum lvp_cmds type)
{
   return cmd_buf_entry_alloc_size(cmd_buffer, 0, type);
}

static void cmd_buf_queue(struct lvp_cmd_buffer *cmd_buffer,
                          struct lvp_cmd_buffer_entry *cmd)
{
   switch (cmd->cmd_type) {
   case LVP_CMD_BIND_DESCRIPTOR_SETS:
   case LVP_CMD_PUSH_DESCRIPTOR_SET:
      list_add(&cmd->cmd_link, cmd_buffer->last_emit);
      cmd_buffer->last_emit = &cmd->cmd_link;
      break;
   case LVP_CMD_NEXT_SUBPASS:
   case LVP_CMD_DRAW:
   case LVP_CMD_DRAW_INDEXED:
   case LVP_CMD_DRAW_INDIRECT:
   case LVP_CMD_DRAW_INDEXED_INDIRECT:
   case LVP_CMD_DISPATCH:
   case LVP_CMD_DISPATCH_INDIRECT:
      cmd_buffer->last_emit = &cmd->cmd_link;
      FALLTHROUGH;
   default:
      list_addtail(&cmd->cmd_link, &cmd_buffer->cmds);
   }
}

static void
state_setup_attachments(struct lvp_attachment_state *attachments,
                        struct lvp_render_pass *pass,
                        const VkClearValue *clear_values)
{
   for (uint32_t i = 0; i < pass->attachment_count; ++i) {
      struct lvp_render_pass_attachment *att = &pass->attachments[i];
      VkImageAspectFlags att_aspects = vk_format_aspects(att->format);
      VkImageAspectFlags clear_aspects = 0;
      if (att_aspects == VK_IMAGE_ASPECT_COLOR_BIT) {
         /* color attachment */
         if (att->load_op == VK_ATTACHMENT_LOAD_OP_CLEAR) {
            clear_aspects |= VK_IMAGE_ASPECT_COLOR_BIT;
         }
      } else {
         /* depthstencil attachment */
         if ((att_aspects & VK_IMAGE_ASPECT_DEPTH_BIT) &&
             att->load_op == VK_ATTACHMENT_LOAD_OP_CLEAR) {
            clear_aspects |= VK_IMAGE_ASPECT_DEPTH_BIT;
            if ((att_aspects & VK_IMAGE_ASPECT_STENCIL_BIT) &&
                att->stencil_load_op == VK_ATTACHMENT_LOAD_OP_DONT_CARE)
               clear_aspects |= VK_IMAGE_ASPECT_STENCIL_BIT;
         }
         if ((att_aspects & VK_IMAGE_ASPECT_STENCIL_BIT) &&
             att->stencil_load_op == VK_ATTACHMENT_LOAD_OP_CLEAR) {
            clear_aspects |= VK_IMAGE_ASPECT_STENCIL_BIT;
         }
      }
      attachments[i].pending_clear_aspects = clear_aspects;
      if (clear_aspects)
         attachments[i].clear_value = clear_values[i];
   }
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdBeginRenderPass2(
    VkCommandBuffer                             commandBuffer,
    const VkRenderPassBeginInfo*                pRenderPassBeginInfo,
    const VkSubpassBeginInfo*                   pSubpassBeginInfo)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   LVP_FROM_HANDLE(lvp_render_pass, pass, pRenderPassBeginInfo->renderPass);
   LVP_FROM_HANDLE(lvp_framebuffer, framebuffer, pRenderPassBeginInfo->framebuffer);
   const struct VkRenderPassAttachmentBeginInfo *attachment_info =
      vk_find_struct_const(pRenderPassBeginInfo->pNext,
                           RENDER_PASS_ATTACHMENT_BEGIN_INFO);
   struct lvp_cmd_buffer_entry *cmd;
   uint32_t cmd_size = pass->attachment_count * sizeof(struct lvp_attachment_state);

   if (attachment_info)
      cmd_size += attachment_info->attachmentCount * sizeof(struct lvp_image_view *);

   cmd = cmd_buf_entry_alloc_size(cmd_buffer, cmd_size, LVP_CMD_BEGIN_RENDER_PASS);
   if (!cmd)
      return;

   cmd->u.begin_render_pass.render_pass = pass;
   cmd->u.begin_render_pass.framebuffer = framebuffer;
   cmd->u.begin_render_pass.render_area = pRenderPassBeginInfo->renderArea;

   cmd->u.begin_render_pass.attachments = (struct lvp_attachment_state *)(cmd + 1);
   cmd->u.begin_render_pass.imageless_views = NULL;
   if (attachment_info) {
      cmd->u.begin_render_pass.imageless_views = (struct lvp_image_view **)(cmd->u.begin_render_pass.attachments + pass->attachment_count);
      for (unsigned i = 0; i < attachment_info->attachmentCount; i++)
         cmd->u.begin_render_pass.imageless_views[i] = lvp_image_view_from_handle(attachment_info->pAttachments[i]);
   }

   state_setup_attachments(cmd->u.begin_render_pass.attachments, pass, pRenderPassBeginInfo->pClearValues);

   cmd_buf_queue(cmd_buffer, cmd);
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdNextSubpass2(
    VkCommandBuffer                             commandBuffer,
    const VkSubpassBeginInfo*                   pSubpassBeginInfo,
    const VkSubpassEndInfo*                     pSubpassEndInfo)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   struct lvp_cmd_buffer_entry *cmd;

   cmd = cmd_buf_entry_alloc(cmd_buffer, LVP_CMD_NEXT_SUBPASS);
   if (!cmd)
      return;

   cmd->u.next_subpass.contents = pSubpassBeginInfo->contents;

   cmd_buf_queue(cmd_buffer, cmd);
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdBindVertexBuffers(
   VkCommandBuffer                             commandBuffer,
   uint32_t                                    firstBinding,
   uint32_t                                    bindingCount,
   const VkBuffer*                             pBuffers,
   const VkDeviceSize*                         pOffsets)
{
   lvp_CmdBindVertexBuffers2EXT(commandBuffer, firstBinding,
      bindingCount, pBuffers, pOffsets, NULL, NULL);
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdBindPipeline(
   VkCommandBuffer                             commandBuffer,
   VkPipelineBindPoint                         pipelineBindPoint,
   VkPipeline                                  _pipeline)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   LVP_FROM_HANDLE(lvp_pipeline, pipeline, _pipeline);
   struct lvp_cmd_buffer_entry *cmd;

   cmd = cmd_buf_entry_alloc(cmd_buffer, LVP_CMD_BIND_PIPELINE);
   if (!cmd)
      return;

   cmd->u.pipeline.bind_point = pipelineBindPoint;
   cmd->u.pipeline.pipeline = pipeline;

   cmd_buf_queue(cmd_buffer, cmd);
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdBindDescriptorSets(
   VkCommandBuffer                             commandBuffer,
   VkPipelineBindPoint                         pipelineBindPoint,
   VkPipelineLayout                            _layout,
   uint32_t                                    firstSet,
   uint32_t                                    descriptorSetCount,
   const VkDescriptorSet*                      pDescriptorSets,
   uint32_t                                    dynamicOffsetCount,
   const uint32_t*                             pDynamicOffsets)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   LVP_FROM_HANDLE(lvp_pipeline_layout, layout, _layout);
   struct lvp_cmd_buffer_entry *cmd;
   struct lvp_descriptor_set **sets;
   uint32_t *offsets;
   int i;
   uint32_t cmd_size = descriptorSetCount * sizeof(struct lvp_descriptor_set *) + dynamicOffsetCount * sizeof(uint32_t);

   cmd = cmd_buf_entry_alloc_size(cmd_buffer, cmd_size, LVP_CMD_BIND_DESCRIPTOR_SETS);
   if (!cmd)
      return;

   cmd->u.descriptor_sets.bind_point = pipelineBindPoint;
   cmd->u.descriptor_sets.first = firstSet;
   cmd->u.descriptor_sets.count = descriptorSetCount;

   for (i = 0; i < layout->num_sets; i++)
      cmd->u.descriptor_sets.set_layout[i] = layout->set[i].layout;
   sets = (struct lvp_descriptor_set **)(cmd + 1);
   for (i = 0; i < descriptorSetCount; i++) {

      sets[i] = lvp_descriptor_set_from_handle(pDescriptorSets[i]);
   }
   cmd->u.descriptor_sets.sets = sets;

   cmd->u.descriptor_sets.dynamic_offset_count = dynamicOffsetCount;
   offsets = (uint32_t *)(sets + descriptorSetCount);
   for (i = 0; i < dynamicOffsetCount; i++)
      offsets[i] = pDynamicOffsets[i];
   cmd->u.descriptor_sets.dynamic_offsets = offsets;

   cmd_buf_queue(cmd_buffer, cmd);
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdDraw(
   VkCommandBuffer                             commandBuffer,
   uint32_t                                    vertexCount,
   uint32_t                                    instanceCount,
   uint32_t                                    firstVertex,
   uint32_t                                    firstInstance)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   struct lvp_cmd_buffer_entry *cmd;

   uint32_t cmd_size = sizeof(struct pipe_draw_start_count_bias);
   cmd = cmd_buf_entry_alloc_size(cmd_buffer, cmd_size, LVP_CMD_DRAW);
   if (!cmd)
      return;

   cmd->u.draw.instance_count = instanceCount;
   cmd->u.draw.first_instance = firstInstance;
   cmd->u.draw.draw_count = 1;
   cmd->u.draw.draws[0].start = firstVertex;
   cmd->u.draw.draws[0].count = vertexCount;

   cmd_buf_queue(cmd_buffer, cmd);
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdEndRenderPass2(
   VkCommandBuffer                             commandBuffer,
   const VkSubpassEndInfo*                     pSubpassEndInfo)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   struct lvp_cmd_buffer_entry *cmd;

   cmd = cmd_buf_entry_alloc(cmd_buffer, LVP_CMD_END_RENDER_PASS);
   if (!cmd)
      return;

   cmd_buf_queue(cmd_buffer, cmd);
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdSetViewport(
   VkCommandBuffer                             commandBuffer,
   uint32_t                                    firstViewport,
   uint32_t                                    viewportCount,
   const VkViewport*                           pViewports)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   struct lvp_cmd_buffer_entry *cmd;
   int i;

   cmd = cmd_buf_entry_alloc(cmd_buffer, LVP_CMD_SET_VIEWPORT);
   if (!cmd)
      return;

   cmd->u.set_viewport.first_viewport = firstViewport;
   cmd->u.set_viewport.viewport_count = viewportCount;
   for (i = 0; i < viewportCount; i++)
      cmd->u.set_viewport.viewports[i] = pViewports[i];

   cmd_buf_queue(cmd_buffer, cmd);
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdSetScissor(
   VkCommandBuffer                             commandBuffer,
   uint32_t                                    firstScissor,
   uint32_t                                    scissorCount,
   const VkRect2D*                             pScissors)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   struct lvp_cmd_buffer_entry *cmd;
   int i;

   cmd = cmd_buf_entry_alloc(cmd_buffer, LVP_CMD_SET_SCISSOR);
   if (!cmd)
      return;

   cmd->u.set_scissor.first_scissor = firstScissor;
   cmd->u.set_scissor.scissor_count = scissorCount;
   for (i = 0; i < scissorCount; i++)
      cmd->u.set_scissor.scissors[i] = pScissors[i];

   cmd_buf_queue(cmd_buffer, cmd);
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdSetLineWidth(
   VkCommandBuffer                             commandBuffer,
   float                                       lineWidth)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   struct lvp_cmd_buffer_entry *cmd;

   cmd = cmd_buf_entry_alloc(cmd_buffer, LVP_CMD_SET_LINE_WIDTH);
   if (!cmd)
      return;

   cmd->u.set_line_width.line_width = lineWidth;

   cmd_buf_queue(cmd_buffer, cmd);
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdSetDepthBias(
   VkCommandBuffer                             commandBuffer,
   float                                       depthBiasConstantFactor,
   float                                       depthBiasClamp,
   float                                       depthBiasSlopeFactor)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   struct lvp_cmd_buffer_entry *cmd;

   cmd = cmd_buf_entry_alloc(cmd_buffer, LVP_CMD_SET_DEPTH_BIAS);
   if (!cmd)
      return;

   cmd->u.set_depth_bias.constant_factor = depthBiasConstantFactor;
   cmd->u.set_depth_bias.clamp = depthBiasClamp;
   cmd->u.set_depth_bias.slope_factor = depthBiasSlopeFactor;

   cmd_buf_queue(cmd_buffer, cmd);
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdSetBlendConstants(
   VkCommandBuffer                             commandBuffer,
   const float                                 blendConstants[4])
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   struct lvp_cmd_buffer_entry *cmd;

   cmd = cmd_buf_entry_alloc(cmd_buffer, LVP_CMD_SET_BLEND_CONSTANTS);
   if (!cmd)
      return;

   memcpy(cmd->u.set_blend_constants.blend_constants, blendConstants, 4 * sizeof(float));

   cmd_buf_queue(cmd_buffer, cmd);
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdSetDepthBounds(
   VkCommandBuffer                             commandBuffer,
   float                                       minDepthBounds,
   float                                       maxDepthBounds)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   struct lvp_cmd_buffer_entry *cmd;

   cmd = cmd_buf_entry_alloc(cmd_buffer, LVP_CMD_SET_DEPTH_BOUNDS);
   if (!cmd)
      return;

   cmd->u.set_depth_bounds.min_depth = minDepthBounds;
   cmd->u.set_depth_bounds.max_depth = maxDepthBounds;

   cmd_buf_queue(cmd_buffer, cmd);
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdSetStencilCompareMask(
   VkCommandBuffer                             commandBuffer,
   VkStencilFaceFlags                          faceMask,
   uint32_t                                    compareMask)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   struct lvp_cmd_buffer_entry *cmd;

   cmd = cmd_buf_entry_alloc(cmd_buffer, LVP_CMD_SET_STENCIL_COMPARE_MASK);
   if (!cmd)
      return;

   cmd->u.stencil_vals.face_mask = faceMask;
   cmd->u.stencil_vals.value = compareMask;

   cmd_buf_queue(cmd_buffer, cmd);
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdSetStencilWriteMask(
   VkCommandBuffer                             commandBuffer,
   VkStencilFaceFlags                          faceMask,
   uint32_t                                    writeMask)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   struct lvp_cmd_buffer_entry *cmd;

   cmd = cmd_buf_entry_alloc(cmd_buffer, LVP_CMD_SET_STENCIL_WRITE_MASK);
   if (!cmd)
      return;

   cmd->u.stencil_vals.face_mask = faceMask;
   cmd->u.stencil_vals.value = writeMask;

   cmd_buf_queue(cmd_buffer, cmd);
}


VKAPI_ATTR void VKAPI_CALL lvp_CmdSetStencilReference(
   VkCommandBuffer                             commandBuffer,
   VkStencilFaceFlags                          faceMask,
   uint32_t                                    reference)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   struct lvp_cmd_buffer_entry *cmd;

   cmd = cmd_buf_entry_alloc(cmd_buffer, LVP_CMD_SET_STENCIL_REFERENCE);
   if (!cmd)
      return;

   cmd->u.stencil_vals.face_mask = faceMask;
   cmd->u.stencil_vals.value = reference;

   cmd_buf_queue(cmd_buffer, cmd);
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdPushConstants(
   VkCommandBuffer                             commandBuffer,
   VkPipelineLayout                            layout,
   VkShaderStageFlags                          stageFlags,
   uint32_t                                    offset,
   uint32_t                                    size,
   const void*                                 pValues)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   struct lvp_cmd_buffer_entry *cmd;

   cmd = cmd_buf_entry_alloc_size(cmd_buffer, (size - 4), LVP_CMD_PUSH_CONSTANTS);
   if (!cmd)
      return;

   cmd->u.push_constants.stage = stageFlags;
   cmd->u.push_constants.offset = offset;
   cmd->u.push_constants.size = size;
   memcpy(cmd->u.push_constants.val, pValues, size);

   cmd_buf_queue(cmd_buffer, cmd);
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdBindIndexBuffer(
   VkCommandBuffer                             commandBuffer,
   VkBuffer                                    _buffer,
   VkDeviceSize                                offset,
   VkIndexType                                 indexType)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   LVP_FROM_HANDLE(lvp_buffer, buffer, _buffer);
   struct lvp_cmd_buffer_entry *cmd;

   cmd = cmd_buf_entry_alloc(cmd_buffer, LVP_CMD_BIND_INDEX_BUFFER);
   if (!cmd)
      return;

   cmd->u.index_buffer.buffer = buffer;
   cmd->u.index_buffer.offset = offset;
   cmd->u.index_buffer.index_type = indexType;

   cmd_buf_queue(cmd_buffer, cmd);
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdDrawIndexed(
   VkCommandBuffer                             commandBuffer,
   uint32_t                                    indexCount,
   uint32_t                                    instanceCount,
   uint32_t                                    firstIndex,
   int32_t                                     vertexOffset,
   uint32_t                                    firstInstance)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   struct lvp_cmd_buffer_entry *cmd;

   uint32_t cmd_size = sizeof(struct pipe_draw_start_count_bias);
   cmd = cmd_buf_entry_alloc_size(cmd_buffer, cmd_size, LVP_CMD_DRAW_INDEXED);
   if (!cmd)
      return;

   cmd->u.draw_indexed.instance_count = instanceCount;
   cmd->u.draw_indexed.first_instance = firstInstance;
   cmd->u.draw_indexed.draw_count = 1;
   cmd->u.draw_indexed.draws[0].start = firstIndex;
   cmd->u.draw_indexed.draws[0].count = indexCount;
   cmd->u.draw_indexed.draws[0].index_bias = vertexOffset;
   cmd->u.draw_indexed.calc_start = true;

   cmd_buf_queue(cmd_buffer, cmd);
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdDrawIndirect(
   VkCommandBuffer                             commandBuffer,
   VkBuffer                                    _buffer,
   VkDeviceSize                                offset,
   uint32_t                                    drawCount,
   uint32_t                                    stride)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   LVP_FROM_HANDLE(lvp_buffer, buf, _buffer);
   struct lvp_cmd_buffer_entry *cmd;

   cmd = cmd_buf_entry_alloc(cmd_buffer, LVP_CMD_DRAW_INDIRECT);
   if (!cmd)
      return;

   cmd->u.draw_indirect.offset = offset;
   cmd->u.draw_indirect.buffer = buf;
   cmd->u.draw_indirect.draw_count = drawCount;
   cmd->u.draw_indirect.stride = stride;

   cmd_buf_queue(cmd_buffer, cmd);
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdDrawIndexedIndirect(
   VkCommandBuffer                             commandBuffer,
   VkBuffer                                    _buffer,
   VkDeviceSize                                offset,
   uint32_t                                    drawCount,
   uint32_t                                    stride)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   LVP_FROM_HANDLE(lvp_buffer, buf, _buffer);
   struct lvp_cmd_buffer_entry *cmd;

   cmd = cmd_buf_entry_alloc(cmd_buffer, LVP_CMD_DRAW_INDEXED_INDIRECT);
   if (!cmd)
      return;

   cmd->u.draw_indirect.offset = offset;
   cmd->u.draw_indirect.buffer = buf;
   cmd->u.draw_indirect.draw_count = drawCount;
   cmd->u.draw_indirect.stride = stride;

   cmd_buf_queue(cmd_buffer, cmd);
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdDispatch(
   VkCommandBuffer                             commandBuffer,
   uint32_t                                    x,
   uint32_t                                    y,
   uint32_t                                    z)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   struct lvp_cmd_buffer_entry *cmd;

   cmd = cmd_buf_entry_alloc(cmd_buffer, LVP_CMD_DISPATCH);
   if (!cmd)
      return;

   cmd->u.dispatch.x = x;
   cmd->u.dispatch.y = y;
   cmd->u.dispatch.z = z;
   cmd->u.dispatch.base_x = 0;
   cmd->u.dispatch.base_y = 0;
   cmd->u.dispatch.base_z = 0;

   cmd_buf_queue(cmd_buffer, cmd);
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdDispatchIndirect(
   VkCommandBuffer                             commandBuffer,
   VkBuffer                                    _buffer,
   VkDeviceSize                                offset)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   struct lvp_cmd_buffer_entry *cmd;

   cmd = cmd_buf_entry_alloc(cmd_buffer, LVP_CMD_DISPATCH_INDIRECT);
   if (!cmd)
      return;

   cmd->u.dispatch_indirect.buffer = lvp_buffer_from_handle(_buffer);
   cmd->u.dispatch_indirect.offset = offset;

   cmd_buf_queue(cmd_buffer, cmd);
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdExecuteCommands(
   VkCommandBuffer                             commandBuffer,
   uint32_t                                    commandBufferCount,
   const VkCommandBuffer*                      pCmdBuffers)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   struct lvp_cmd_buffer_entry *cmd;
   uint32_t cmd_size = commandBufferCount * sizeof(struct lvp_cmd_buffer *);

   cmd = cmd_buf_entry_alloc_size(cmd_buffer, cmd_size, LVP_CMD_EXECUTE_COMMANDS);
   if (!cmd)
      return;

   cmd->u.execute_commands.command_buffer_count = commandBufferCount;
   for (unsigned i = 0; i < commandBufferCount; i++)
      cmd->u.execute_commands.cmd_buffers[i] = lvp_cmd_buffer_from_handle(pCmdBuffers[i]);

   cmd_buf_queue(cmd_buffer, cmd);
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdSetEvent(VkCommandBuffer commandBuffer,
                     VkEvent _event,
                     VkPipelineStageFlags stageMask)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   LVP_FROM_HANDLE(lvp_event, event, _event);
   struct lvp_cmd_buffer_entry *cmd;

   cmd = cmd_buf_entry_alloc(cmd_buffer, LVP_CMD_SET_EVENT);
   if (!cmd)
      return;

   cmd->u.event_set.event = event;
   cmd->u.event_set.value = true;
   cmd->u.event_set.flush = !!(stageMask == VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);

   cmd_buf_queue(cmd_buffer, cmd);
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdResetEvent(VkCommandBuffer commandBuffer,
                       VkEvent _event,
                       VkPipelineStageFlags stageMask)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   LVP_FROM_HANDLE(lvp_event, event, _event);
   struct lvp_cmd_buffer_entry *cmd;

   cmd = cmd_buf_entry_alloc(cmd_buffer, LVP_CMD_SET_EVENT);
   if (!cmd)
      return;

   cmd->u.event_set.event = event;
   cmd->u.event_set.value = false;
   cmd->u.event_set.flush = !!(stageMask == VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);

   cmd_buf_queue(cmd_buffer, cmd);

}

VKAPI_ATTR void VKAPI_CALL lvp_CmdWaitEvents(VkCommandBuffer commandBuffer,
                       uint32_t eventCount,
                       const VkEvent* pEvents,
                       VkPipelineStageFlags srcStageMask,
                       VkPipelineStageFlags dstStageMask,
                       uint32_t memoryBarrierCount,
                       const VkMemoryBarrier* pMemoryBarriers,
                       uint32_t bufferMemoryBarrierCount,
                       const VkBufferMemoryBarrier* pBufferMemoryBarriers,
                       uint32_t imageMemoryBarrierCount,
                       const VkImageMemoryBarrier* pImageMemoryBarriers)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   struct lvp_cmd_buffer_entry *cmd;
   uint32_t cmd_size = 0;

   cmd_size += eventCount * sizeof(struct lvp_event *);
   cmd_size += memoryBarrierCount * sizeof(VkMemoryBarrier);
   cmd_size += bufferMemoryBarrierCount * sizeof(VkBufferMemoryBarrier);
   cmd_size += imageMemoryBarrierCount * sizeof(VkImageMemoryBarrier);

   cmd = cmd_buf_entry_alloc_size(cmd_buffer, cmd_size, LVP_CMD_WAIT_EVENTS);
   if (!cmd)
      return;

   cmd->u.wait_events.src_stage_mask = srcStageMask;
   cmd->u.wait_events.dst_stage_mask = dstStageMask;
   cmd->u.wait_events.event_count = eventCount;
   cmd->u.wait_events.events = (struct lvp_event **)(cmd + 1);
   for (unsigned i = 0; i < eventCount; i++)
      cmd->u.wait_events.events[i] = lvp_event_from_handle(pEvents[i]);
   cmd->u.wait_events.memory_barrier_count = memoryBarrierCount;
   cmd->u.wait_events.buffer_memory_barrier_count = bufferMemoryBarrierCount;
   cmd->u.wait_events.image_memory_barrier_count = imageMemoryBarrierCount;

   /* TODO finish off this */
   cmd_buf_queue(cmd_buffer, cmd);
}

/* copy a 2KHR struct to the base struct */
static inline void
copy_2_struct_to_base(void *base, const void *struct2, size_t struct_size)
{
   size_t offset = align(sizeof(VkStructureType) + sizeof(void*), 8);
   memcpy(base, ((uint8_t*)struct2) + offset, struct_size);
}

/* copy an array of 2KHR structs to an array of base structs */
#define COPY_STRUCT2_ARRAY(count, base, struct2, struct_type) \
   do { \
      for (unsigned _i = 0; _i < (count); _i++) \
         copy_2_struct_to_base(&base[_i], &struct2[_i], sizeof(struct_type)); \
   } while (0)

VKAPI_ATTR void VKAPI_CALL lvp_CmdCopyBufferToImage2KHR(
   VkCommandBuffer                             commandBuffer,
   const VkCopyBufferToImageInfo2KHR          *info)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   LVP_FROM_HANDLE(lvp_buffer, src_buffer, info->srcBuffer);
   LVP_FROM_HANDLE(lvp_image, dst_image, info->dstImage);
   struct lvp_cmd_buffer_entry *cmd;
   uint32_t cmd_size = info->regionCount * sizeof(VkBufferImageCopy);

   cmd = cmd_buf_entry_alloc_size(cmd_buffer, cmd_size, LVP_CMD_COPY_BUFFER_TO_IMAGE);
   if (!cmd)
      return;

   cmd->u.buffer_to_img.src = src_buffer;
   cmd->u.buffer_to_img.dst = dst_image;
   cmd->u.buffer_to_img.dst_layout = info->dstImageLayout;
   cmd->u.buffer_to_img.region_count = info->regionCount;

   {
      VkBufferImageCopy *regions;

      regions = (VkBufferImageCopy *)(cmd + 1);
      COPY_STRUCT2_ARRAY(info->regionCount, regions, info->pRegions, VkBufferImageCopy);
      cmd->u.buffer_to_img.regions = regions;
   }

   cmd_buf_queue(cmd_buffer, cmd);
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdCopyImageToBuffer2KHR(
   VkCommandBuffer                             commandBuffer,
   const VkCopyImageToBufferInfo2KHR          *info)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   LVP_FROM_HANDLE(lvp_image, src_image, info->srcImage);
   LVP_FROM_HANDLE(lvp_buffer, dst_buffer, info->dstBuffer);
   struct lvp_cmd_buffer_entry *cmd;
   uint32_t cmd_size = info->regionCount * sizeof(VkBufferImageCopy);

   cmd = cmd_buf_entry_alloc_size(cmd_buffer, cmd_size, LVP_CMD_COPY_IMAGE_TO_BUFFER);
   if (!cmd)
      return;

   cmd->u.img_to_buffer.src = src_image;
   cmd->u.img_to_buffer.dst = dst_buffer;
   cmd->u.img_to_buffer.src_layout = info->srcImageLayout;
   cmd->u.img_to_buffer.region_count = info->regionCount;

   {
      VkBufferImageCopy *regions;

      regions = (VkBufferImageCopy *)(cmd + 1);
      COPY_STRUCT2_ARRAY(info->regionCount, regions, info->pRegions, VkBufferImageCopy);
      cmd->u.img_to_buffer.regions = regions;
   }

   cmd_buf_queue(cmd_buffer, cmd);
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdCopyImage2KHR(
   VkCommandBuffer                             commandBuffer,
   const VkCopyImageInfo2KHR                  *info)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   LVP_FROM_HANDLE(lvp_image, src_image, info->srcImage);
   LVP_FROM_HANDLE(lvp_image, dest_image, info->dstImage);
   struct lvp_cmd_buffer_entry *cmd;
   uint32_t cmd_size = info->regionCount * sizeof(VkImageCopy);

   cmd = cmd_buf_entry_alloc_size(cmd_buffer, cmd_size, LVP_CMD_COPY_IMAGE);
   if (!cmd)
      return;

   cmd->u.copy_image.src = src_image;
   cmd->u.copy_image.dst = dest_image;
   cmd->u.copy_image.src_layout = info->srcImageLayout;
   cmd->u.copy_image.dst_layout = info->dstImageLayout;
   cmd->u.copy_image.region_count = info->regionCount;

   {
      VkImageCopy *regions;

      regions = (VkImageCopy *)(cmd + 1);
      COPY_STRUCT2_ARRAY(info->regionCount, regions, info->pRegions, VkImageCopy);
      cmd->u.copy_image.regions = regions;
   }

   cmd_buf_queue(cmd_buffer, cmd);
}


VKAPI_ATTR void VKAPI_CALL lvp_CmdCopyBuffer2KHR(
   VkCommandBuffer                             commandBuffer,
   const VkCopyBufferInfo2KHR                 *info)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   LVP_FROM_HANDLE(lvp_buffer, src_buffer, info->srcBuffer);
   LVP_FROM_HANDLE(lvp_buffer, dest_buffer, info->dstBuffer);
   struct lvp_cmd_buffer_entry *cmd;
   uint32_t cmd_size = info->regionCount * sizeof(VkBufferCopy);

   cmd = cmd_buf_entry_alloc_size(cmd_buffer, cmd_size, LVP_CMD_COPY_BUFFER);
   if (!cmd)
      return;

   cmd->u.copy_buffer.src = src_buffer;
   cmd->u.copy_buffer.dst = dest_buffer;
   cmd->u.copy_buffer.region_count = info->regionCount;

   {
      VkBufferCopy *regions;

      regions = (VkBufferCopy *)(cmd + 1);
      COPY_STRUCT2_ARRAY(info->regionCount, regions, info->pRegions, VkBufferCopy);
      cmd->u.copy_buffer.regions = regions;
   }

   cmd_buf_queue(cmd_buffer, cmd);
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdBlitImage2KHR(
   VkCommandBuffer                             commandBuffer,
   const VkBlitImageInfo2KHR                  *info)
{
   
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   LVP_FROM_HANDLE(lvp_image, src_image, info->srcImage);
   LVP_FROM_HANDLE(lvp_image, dest_image, info->dstImage);
   struct lvp_cmd_buffer_entry *cmd;
   uint32_t cmd_size = info->regionCount * sizeof(VkImageBlit);

   cmd = cmd_buf_entry_alloc_size(cmd_buffer, cmd_size, LVP_CMD_BLIT_IMAGE);
   if (!cmd)
      return;

   cmd->u.blit_image.src = src_image;
   cmd->u.blit_image.dst = dest_image;
   cmd->u.blit_image.src_layout = info->srcImageLayout;
   cmd->u.blit_image.dst_layout = info->dstImageLayout;
   cmd->u.blit_image.filter = info->filter;
   cmd->u.blit_image.region_count = info->regionCount;

   {
      VkImageBlit *regions;

      regions = (VkImageBlit *)(cmd + 1);
      COPY_STRUCT2_ARRAY(info->regionCount, regions, info->pRegions, VkImageBlit);
      cmd->u.blit_image.regions = regions;
   }

   cmd_buf_queue(cmd_buffer, cmd);
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdClearAttachments(
   VkCommandBuffer                             commandBuffer,
   uint32_t                                    attachmentCount,
   const VkClearAttachment*                    pAttachments,
   uint32_t                                    rectCount,
   const VkClearRect*                          pRects)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   struct lvp_cmd_buffer_entry *cmd;
   uint32_t cmd_size = attachmentCount * sizeof(VkClearAttachment) + rectCount * sizeof(VkClearRect);

   cmd = cmd_buf_entry_alloc_size(cmd_buffer, cmd_size, LVP_CMD_CLEAR_ATTACHMENTS);
   if (!cmd)
      return;

   cmd->u.clear_attachments.attachment_count = attachmentCount;
   cmd->u.clear_attachments.attachments = (VkClearAttachment *)(cmd + 1);
   for (unsigned i = 0; i < attachmentCount; i++)
      cmd->u.clear_attachments.attachments[i] = pAttachments[i];
   cmd->u.clear_attachments.rect_count = rectCount;
   cmd->u.clear_attachments.rects = (VkClearRect *)(cmd->u.clear_attachments.attachments + attachmentCount);
   for (unsigned i = 0; i < rectCount; i++)
      cmd->u.clear_attachments.rects[i] = pRects[i];

   cmd_buf_queue(cmd_buffer, cmd);
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdFillBuffer(
   VkCommandBuffer                             commandBuffer,
   VkBuffer                                    dstBuffer,
   VkDeviceSize                                dstOffset,
   VkDeviceSize                                fillSize,
   uint32_t                                    data)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   LVP_FROM_HANDLE(lvp_buffer, dst_buffer, dstBuffer);
   struct lvp_cmd_buffer_entry *cmd;

   cmd = cmd_buf_entry_alloc(cmd_buffer, LVP_CMD_FILL_BUFFER);
   if (!cmd)
      return;

   cmd->u.fill_buffer.buffer = dst_buffer;
   cmd->u.fill_buffer.offset = dstOffset;
   cmd->u.fill_buffer.fill_size = fillSize;
   cmd->u.fill_buffer.data = data;

   cmd_buf_queue(cmd_buffer, cmd);
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdUpdateBuffer(
   VkCommandBuffer                             commandBuffer,
   VkBuffer                                    dstBuffer,
   VkDeviceSize                                dstOffset,
   VkDeviceSize                                dataSize,
   const void*                                 pData)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   LVP_FROM_HANDLE(lvp_buffer, dst_buffer, dstBuffer);
   struct lvp_cmd_buffer_entry *cmd;

   cmd = cmd_buf_entry_alloc_size(cmd_buffer, dataSize, LVP_CMD_UPDATE_BUFFER);
   if (!cmd)
      return;

   cmd->u.update_buffer.buffer = dst_buffer;
   cmd->u.update_buffer.offset = dstOffset;
   cmd->u.update_buffer.data_size = dataSize;
   memcpy(cmd->u.update_buffer.data, pData, dataSize);

   cmd_buf_queue(cmd_buffer, cmd);
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdClearColorImage(
   VkCommandBuffer                             commandBuffer,
   VkImage                                     image_h,
   VkImageLayout                               imageLayout,
   const VkClearColorValue*                    pColor,
   uint32_t                                    rangeCount,
   const VkImageSubresourceRange*              pRanges)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   LVP_FROM_HANDLE(lvp_image, image, image_h);
   struct lvp_cmd_buffer_entry *cmd;
   uint32_t cmd_size = rangeCount * sizeof(VkImageSubresourceRange);

   cmd = cmd_buf_entry_alloc_size(cmd_buffer, cmd_size, LVP_CMD_CLEAR_COLOR_IMAGE);
   if (!cmd)
      return;

   cmd->u.clear_color_image.image = image;
   cmd->u.clear_color_image.layout = imageLayout;
   cmd->u.clear_color_image.clear_val = *pColor;
   cmd->u.clear_color_image.range_count = rangeCount;
   cmd->u.clear_color_image.ranges = (VkImageSubresourceRange *)(cmd + 1);
   for (unsigned i = 0; i < rangeCount; i++)
      cmd->u.clear_color_image.ranges[i] = pRanges[i];

   cmd_buf_queue(cmd_buffer, cmd);
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdClearDepthStencilImage(
   VkCommandBuffer                             commandBuffer,
   VkImage                                     image_h,
   VkImageLayout                               imageLayout,
   const VkClearDepthStencilValue*             pDepthStencil,
   uint32_t                                    rangeCount,
   const VkImageSubresourceRange*              pRanges)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   LVP_FROM_HANDLE(lvp_image, image, image_h);
   struct lvp_cmd_buffer_entry *cmd;
   uint32_t cmd_size = rangeCount * sizeof(VkImageSubresourceRange);

   cmd = cmd_buf_entry_alloc_size(cmd_buffer, cmd_size, LVP_CMD_CLEAR_DEPTH_STENCIL_IMAGE);
   if (!cmd)
      return;

   cmd->u.clear_ds_image.image = image;
   cmd->u.clear_ds_image.layout = imageLayout;
   cmd->u.clear_ds_image.clear_val = *pDepthStencil;
   cmd->u.clear_ds_image.range_count = rangeCount;
   cmd->u.clear_ds_image.ranges = (VkImageSubresourceRange *)(cmd + 1);
   for (unsigned i = 0; i < rangeCount; i++)
      cmd->u.clear_ds_image.ranges[i] = pRanges[i];

   cmd_buf_queue(cmd_buffer, cmd);
}


VKAPI_ATTR void VKAPI_CALL lvp_CmdResolveImage2KHR(
   VkCommandBuffer                             commandBuffer,
   const VkResolveImageInfo2KHR               *info)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   LVP_FROM_HANDLE(lvp_image, src_image, info->srcImage);
   LVP_FROM_HANDLE(lvp_image, dst_image, info->dstImage);
   struct lvp_cmd_buffer_entry *cmd;
   uint32_t cmd_size = info->regionCount * sizeof(VkImageResolve);

   cmd = cmd_buf_entry_alloc_size(cmd_buffer, cmd_size, LVP_CMD_RESOLVE_IMAGE);
   if (!cmd)
      return;

   cmd->u.resolve_image.src = src_image;
   cmd->u.resolve_image.dst = dst_image;
   cmd->u.resolve_image.src_layout = info->srcImageLayout;
   cmd->u.resolve_image.dst_layout = info->dstImageLayout;
   cmd->u.resolve_image.region_count = info->regionCount;
   cmd->u.resolve_image.regions = (VkImageResolve *)(cmd + 1);
   COPY_STRUCT2_ARRAY(info->regionCount, cmd->u.resolve_image.regions, info->pRegions, VkImageResolve);

   cmd_buf_queue(cmd_buffer, cmd);
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdResetQueryPool(
   VkCommandBuffer                             commandBuffer,
   VkQueryPool                                 queryPool,
   uint32_t                                    firstQuery,
   uint32_t                                    queryCount)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   LVP_FROM_HANDLE(lvp_query_pool, query_pool, queryPool);
   struct lvp_cmd_buffer_entry *cmd;

   cmd = cmd_buf_entry_alloc(cmd_buffer, LVP_CMD_RESET_QUERY_POOL);
   if (!cmd)
      return;

   cmd->u.query.pool = query_pool;
   cmd->u.query.query = firstQuery;
   cmd->u.query.index = queryCount;

   cmd_buf_queue(cmd_buffer, cmd);
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdBeginQueryIndexedEXT(
   VkCommandBuffer                             commandBuffer,
   VkQueryPool                                 queryPool,
   uint32_t                                    query,
   VkQueryControlFlags                         flags,
   uint32_t                                    index)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   LVP_FROM_HANDLE(lvp_query_pool, query_pool, queryPool);
   struct lvp_cmd_buffer_entry *cmd;

   cmd = cmd_buf_entry_alloc(cmd_buffer, LVP_CMD_BEGIN_QUERY);
   if (!cmd)
      return;

   cmd->u.query.pool = query_pool;
   cmd->u.query.query = query;
   cmd->u.query.index = index;
   cmd->u.query.precise = true;

   cmd_buf_queue(cmd_buffer, cmd);
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdBeginQuery(
   VkCommandBuffer                             commandBuffer,
   VkQueryPool                                 queryPool,
   uint32_t                                    query,
   VkQueryControlFlags                         flags)
{
   lvp_CmdBeginQueryIndexedEXT(commandBuffer, queryPool, query, flags, 0);
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdEndQueryIndexedEXT(
   VkCommandBuffer                             commandBuffer,
   VkQueryPool                                 queryPool,
   uint32_t                                    query,
   uint32_t                                    index)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   LVP_FROM_HANDLE(lvp_query_pool, query_pool, queryPool);
   struct lvp_cmd_buffer_entry *cmd;

   cmd = cmd_buf_entry_alloc(cmd_buffer, LVP_CMD_END_QUERY);
   if (!cmd)
      return;

   cmd->u.query.pool = query_pool;
   cmd->u.query.query = query;
   cmd->u.query.index = index;

   cmd_buf_queue(cmd_buffer, cmd);
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdEndQuery(
   VkCommandBuffer                             commandBuffer,
   VkQueryPool                                 queryPool,
   uint32_t                                    query)
{
   lvp_CmdEndQueryIndexedEXT(commandBuffer, queryPool, query, 0);
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdWriteTimestamp(
   VkCommandBuffer                             commandBuffer,
   VkPipelineStageFlagBits                     pipelineStage,
   VkQueryPool                                 queryPool,
   uint32_t                                    query)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   LVP_FROM_HANDLE(lvp_query_pool, query_pool, queryPool);
   struct lvp_cmd_buffer_entry *cmd;

   cmd = cmd_buf_entry_alloc(cmd_buffer, LVP_CMD_WRITE_TIMESTAMP);
   if (!cmd)
      return;

   cmd->u.query.pool = query_pool;
   cmd->u.query.query = query;
   cmd->u.query.flush = !(pipelineStage == VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);

   cmd_buf_queue(cmd_buffer, cmd);
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdCopyQueryPoolResults(
   VkCommandBuffer                             commandBuffer,
   VkQueryPool                                 queryPool,
   uint32_t                                    firstQuery,
   uint32_t                                    queryCount,
   VkBuffer                                    dstBuffer,
   VkDeviceSize                                dstOffset,
   VkDeviceSize                                stride,
   VkQueryResultFlags                          flags)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   LVP_FROM_HANDLE(lvp_query_pool, query_pool, queryPool);
   LVP_FROM_HANDLE(lvp_buffer, buffer, dstBuffer);
   struct lvp_cmd_buffer_entry *cmd;

   cmd = cmd_buf_entry_alloc(cmd_buffer, LVP_CMD_COPY_QUERY_POOL_RESULTS);
   if (!cmd)
      return;

   cmd->u.copy_query_pool_results.pool = query_pool;
   cmd->u.copy_query_pool_results.first_query = firstQuery;
   cmd->u.copy_query_pool_results.query_count = queryCount;
   cmd->u.copy_query_pool_results.dst = buffer;
   cmd->u.copy_query_pool_results.dst_offset = dstOffset;
   cmd->u.copy_query_pool_results.stride = stride;
   cmd->u.copy_query_pool_results.flags = flags;

   cmd_buf_queue(cmd_buffer, cmd);
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdPipelineBarrier(
   VkCommandBuffer                             commandBuffer,
   VkPipelineStageFlags                        srcStageMask,
   VkPipelineStageFlags                        destStageMask,
   VkBool32                                    byRegion,
   uint32_t                                    memoryBarrierCount,
   const VkMemoryBarrier*                      pMemoryBarriers,
   uint32_t                                    bufferMemoryBarrierCount,
   const VkBufferMemoryBarrier*                pBufferMemoryBarriers,
   uint32_t                                    imageMemoryBarrierCount,
   const VkImageMemoryBarrier*                 pImageMemoryBarriers)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   struct lvp_cmd_buffer_entry *cmd;
   uint32_t cmd_size = 0;

   cmd_size += memoryBarrierCount * sizeof(VkMemoryBarrier);
   cmd_size += bufferMemoryBarrierCount * sizeof(VkBufferMemoryBarrier);
   cmd_size += imageMemoryBarrierCount * sizeof(VkImageMemoryBarrier);

   cmd = cmd_buf_entry_alloc_size(cmd_buffer, cmd_size, LVP_CMD_PIPELINE_BARRIER);
   if (!cmd)
      return;

   cmd->u.pipeline_barrier.src_stage_mask = srcStageMask;
   cmd->u.pipeline_barrier.dst_stage_mask = destStageMask;
   cmd->u.pipeline_barrier.by_region = byRegion;
   cmd->u.pipeline_barrier.memory_barrier_count = memoryBarrierCount;
   cmd->u.pipeline_barrier.buffer_memory_barrier_count = bufferMemoryBarrierCount;
   cmd->u.pipeline_barrier.image_memory_barrier_count = imageMemoryBarrierCount;

   /* TODO finish off this */
   cmd_buf_queue(cmd_buffer, cmd);
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdDrawIndirectCount(
    VkCommandBuffer                             commandBuffer,
    VkBuffer                                    buffer,
    VkDeviceSize                                offset,
    VkBuffer                                    countBuffer,
    VkDeviceSize                                countBufferOffset,
    uint32_t                                    maxDrawCount,
    uint32_t                                    stride)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   LVP_FROM_HANDLE(lvp_buffer, buf, buffer);
   LVP_FROM_HANDLE(lvp_buffer, count_buf, countBuffer);
   struct lvp_cmd_buffer_entry *cmd;

   cmd = cmd_buf_entry_alloc(cmd_buffer, LVP_CMD_DRAW_INDIRECT_COUNT);
   if (!cmd)
      return;

   cmd->u.draw_indirect_count.offset = offset;
   cmd->u.draw_indirect_count.buffer = buf;
   cmd->u.draw_indirect_count.count_buffer_offset = countBufferOffset;
   cmd->u.draw_indirect_count.count_buffer = count_buf;
   cmd->u.draw_indirect_count.max_draw_count = maxDrawCount;
   cmd->u.draw_indirect_count.stride = stride;

   cmd_buf_queue(cmd_buffer, cmd);
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdDrawIndexedIndirectCount(
    VkCommandBuffer                             commandBuffer,
    VkBuffer                                    buffer,
    VkDeviceSize                                offset,
    VkBuffer                                    countBuffer,
    VkDeviceSize                                countBufferOffset,
    uint32_t                                    maxDrawCount,
    uint32_t                                    stride)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   LVP_FROM_HANDLE(lvp_buffer, buf, buffer);
   LVP_FROM_HANDLE(lvp_buffer, count_buf, countBuffer);
   struct lvp_cmd_buffer_entry *cmd;

   cmd = cmd_buf_entry_alloc(cmd_buffer, LVP_CMD_DRAW_INDEXED_INDIRECT_COUNT);
   if (!cmd)
      return;

   cmd->u.draw_indirect_count.offset = offset;
   cmd->u.draw_indirect_count.buffer = buf;
   cmd->u.draw_indirect_count.count_buffer_offset = countBufferOffset;
   cmd->u.draw_indirect_count.count_buffer = count_buf;
   cmd->u.draw_indirect_count.max_draw_count = maxDrawCount;
   cmd->u.draw_indirect_count.stride = stride;

   cmd_buf_queue(cmd_buffer, cmd);
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdPushDescriptorSetKHR(
   VkCommandBuffer                             commandBuffer,
   VkPipelineBindPoint                         pipelineBindPoint,
   VkPipelineLayout                            _layout,
   uint32_t                                    set,
   uint32_t                                    descriptorWriteCount,
   const VkWriteDescriptorSet*                 pDescriptorWrites)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   LVP_FROM_HANDLE(lvp_pipeline_layout, layout, _layout);
   struct lvp_cmd_buffer_entry *cmd;
   int cmd_size = 0;

   cmd_size += descriptorWriteCount * sizeof(struct lvp_write_descriptor);

   int count_descriptors = 0;

   for (unsigned i = 0; i < descriptorWriteCount; i++) {
      count_descriptors += pDescriptorWrites[i].descriptorCount;
   }
   cmd_size += count_descriptors * sizeof(union lvp_descriptor_info);
   cmd = cmd_buf_entry_alloc_size(cmd_buffer, cmd_size, LVP_CMD_PUSH_DESCRIPTOR_SET);
   if (!cmd)
      return;

   cmd->u.push_descriptor_set.bind_point = pipelineBindPoint;
   cmd->u.push_descriptor_set.layout = layout;
   cmd->u.push_descriptor_set.set = set;
   cmd->u.push_descriptor_set.descriptor_write_count = descriptorWriteCount;
   cmd->u.push_descriptor_set.descriptors = (struct lvp_write_descriptor *)(cmd + 1);
   cmd->u.push_descriptor_set.infos = (union lvp_descriptor_info *)(cmd->u.push_descriptor_set.descriptors + descriptorWriteCount);

   unsigned descriptor_index = 0;

   for (unsigned i = 0; i < descriptorWriteCount; i++) {
      struct lvp_write_descriptor *desc = &cmd->u.push_descriptor_set.descriptors[i];

      /* dstSet is ignored */
      desc->dst_binding = pDescriptorWrites[i].dstBinding;
      desc->dst_array_element = pDescriptorWrites[i].dstArrayElement;
      desc->descriptor_count = pDescriptorWrites[i].descriptorCount;
      desc->descriptor_type = pDescriptorWrites[i].descriptorType;

      for (unsigned j = 0; j < desc->descriptor_count; j++) {
         union lvp_descriptor_info *info = &cmd->u.push_descriptor_set.infos[descriptor_index + j];
         switch (desc->descriptor_type) {
         case VK_DESCRIPTOR_TYPE_SAMPLER:
            info->sampler = lvp_sampler_from_handle(pDescriptorWrites[i].pImageInfo[j].sampler);
            break;
         case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
            info->sampler = lvp_sampler_from_handle(pDescriptorWrites[i].pImageInfo[j].sampler);
            info->iview = lvp_image_view_from_handle(pDescriptorWrites[i].pImageInfo[j].imageView);
            info->image_layout = pDescriptorWrites[i].pImageInfo[j].imageLayout;
            break;
         case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
         case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
         case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
            info->iview = lvp_image_view_from_handle(pDescriptorWrites[i].pImageInfo[j].imageView);
            info->image_layout = pDescriptorWrites[i].pImageInfo[j].imageLayout;
            break;
         case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
         case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
            info->buffer_view = lvp_buffer_view_from_handle(pDescriptorWrites[i].pTexelBufferView[j]);
            break;
         case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
         case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
         case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
         case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
         default:
            info->buffer = lvp_buffer_from_handle(pDescriptorWrites[i].pBufferInfo[j].buffer);
            info->offset = pDescriptorWrites[i].pBufferInfo[j].offset;
            info->range = pDescriptorWrites[i].pBufferInfo[j].range;
            break;
         }
      }
      descriptor_index += desc->descriptor_count;
   }
   cmd_buf_queue(cmd_buffer, cmd);
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdPushDescriptorSetWithTemplateKHR(
   VkCommandBuffer                             commandBuffer,
   VkDescriptorUpdateTemplate                  descriptorUpdateTemplate,
   VkPipelineLayout                            _layout,
   uint32_t                                    set,
   const void*                                 pData)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   LVP_FROM_HANDLE(lvp_descriptor_update_template, templ, descriptorUpdateTemplate);
   int cmd_size = 0;
   struct lvp_cmd_buffer_entry *cmd;

   cmd_size += templ->entry_count * sizeof(struct lvp_write_descriptor);

   int count_descriptors = 0;
   for (unsigned i = 0; i < templ->entry_count; i++) {
      VkDescriptorUpdateTemplateEntry *entry = &templ->entry[i];
      count_descriptors += entry->descriptorCount;
   }
   cmd_size += count_descriptors * sizeof(union lvp_descriptor_info);

   cmd = cmd_buf_entry_alloc_size(cmd_buffer, cmd_size, LVP_CMD_PUSH_DESCRIPTOR_SET);
   if (!cmd)
      return;

   cmd->u.push_descriptor_set.bind_point = templ->bind_point;
   cmd->u.push_descriptor_set.layout = templ->pipeline_layout;
   cmd->u.push_descriptor_set.set = templ->set;
   cmd->u.push_descriptor_set.descriptor_write_count = templ->entry_count;
   cmd->u.push_descriptor_set.descriptors = (struct lvp_write_descriptor *)(cmd + 1);
   cmd->u.push_descriptor_set.infos = (union lvp_descriptor_info *)(cmd->u.push_descriptor_set.descriptors + templ->entry_count);

   unsigned descriptor_index = 0;

   for (unsigned i = 0; i < templ->entry_count; i++) {
      struct lvp_write_descriptor *desc = &cmd->u.push_descriptor_set.descriptors[i];
      struct VkDescriptorUpdateTemplateEntry *entry = &templ->entry[i];
      const uint8_t *pSrc = ((const uint8_t *) pData) + entry->offset;

      /* dstSet is ignored */
      desc->dst_binding = entry->dstBinding;
      desc->dst_array_element = entry->dstArrayElement;
      desc->descriptor_count = entry->descriptorCount;
      desc->descriptor_type = entry->descriptorType;

      for (unsigned j = 0; j < desc->descriptor_count; j++) {
         union lvp_descriptor_info *info = &cmd->u.push_descriptor_set.infos[descriptor_index + j];
         switch (desc->descriptor_type) {
         case VK_DESCRIPTOR_TYPE_SAMPLER:
            info->sampler = lvp_sampler_from_handle(*(VkSampler *)pSrc);
            break;
         case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: {
            VkDescriptorImageInfo *image_info = (VkDescriptorImageInfo *)pSrc;
            info->sampler = lvp_sampler_from_handle(image_info->sampler);
            info->iview = lvp_image_view_from_handle(image_info->imageView);
            info->image_layout = image_info->imageLayout;
            break;
         }
         case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
         case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
         case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT: {
            VkDescriptorImageInfo *image_info = (VkDescriptorImageInfo *)pSrc;
            info->iview = lvp_image_view_from_handle(image_info->imageView);
            info->image_layout = image_info->imageLayout;
            break;
         }
         case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
         case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
            info->buffer_view = lvp_buffer_view_from_handle(*(VkBufferView *)pSrc);
            break;
         case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
         case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
         case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
         case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
         default: {
            VkDescriptorBufferInfo *buffer_info = (VkDescriptorBufferInfo *)pSrc;
            info->buffer = lvp_buffer_from_handle(buffer_info->buffer);
            info->offset = buffer_info->offset;
            info->range = buffer_info->range;
            break;
         }
         }
         pSrc += entry->stride;
      }
      descriptor_index += desc->descriptor_count;
   }
   cmd_buf_queue(cmd_buffer, cmd);
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdBindTransformFeedbackBuffersEXT(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstBinding,
    uint32_t                                    bindingCount,
    const VkBuffer*                             pBuffers,
    const VkDeviceSize*                         pOffsets,
    const VkDeviceSize*                         pSizes)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   struct lvp_cmd_buffer_entry *cmd;
   uint32_t cmd_size = 0;

   cmd_size += bindingCount * (sizeof(struct lvp_buffer *) + sizeof(VkDeviceSize) * 2);

   cmd = cmd_buf_entry_alloc_size(cmd_buffer, cmd_size, LVP_CMD_BIND_TRANSFORM_FEEDBACK_BUFFERS);
   if (!cmd)
      return;

   cmd->u.bind_transform_feedback_buffers.first_binding = firstBinding;
   cmd->u.bind_transform_feedback_buffers.binding_count = bindingCount;
   cmd->u.bind_transform_feedback_buffers.buffers = (struct lvp_buffer **)(cmd + 1);
   cmd->u.bind_transform_feedback_buffers.offsets = (VkDeviceSize *)(cmd->u.bind_transform_feedback_buffers.buffers + bindingCount);
   cmd->u.bind_transform_feedback_buffers.sizes = (VkDeviceSize *)(cmd->u.bind_transform_feedback_buffers.offsets + bindingCount);

   for (unsigned i = 0; i < bindingCount; i++) {
      cmd->u.bind_transform_feedback_buffers.buffers[i] = lvp_buffer_from_handle(pBuffers[i]);
      cmd->u.bind_transform_feedback_buffers.offsets[i] = pOffsets[i];
      cmd->u.bind_transform_feedback_buffers.sizes[i] = pSizes[i];
   }
   cmd_buf_queue(cmd_buffer, cmd);
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdBeginTransformFeedbackEXT(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstCounterBuffer,
    uint32_t                                    counterBufferCount,
    const VkBuffer*                             pCounterBuffers,
    const VkDeviceSize*                         pCounterBufferOffsets)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   struct lvp_cmd_buffer_entry *cmd;
   uint32_t cmd_size = 0;

   cmd_size += counterBufferCount * (sizeof(struct lvp_buffer *) + sizeof(VkDeviceSize));

   cmd = cmd_buf_entry_alloc_size(cmd_buffer, cmd_size, LVP_CMD_BEGIN_TRANSFORM_FEEDBACK);
   if (!cmd)
      return;

   cmd->u.begin_transform_feedback.first_counter_buffer = firstCounterBuffer;
   cmd->u.begin_transform_feedback.counter_buffer_count = counterBufferCount;
   cmd->u.begin_transform_feedback.counter_buffers = (struct lvp_buffer **)(cmd + 1);
   cmd->u.begin_transform_feedback.counter_buffer_offsets = (VkDeviceSize *)(cmd->u.begin_transform_feedback.counter_buffers + counterBufferCount);

   for (unsigned i = 0; i < counterBufferCount; i++) {
      if (pCounterBuffers)
         cmd->u.begin_transform_feedback.counter_buffers[i] = lvp_buffer_from_handle(pCounterBuffers[i]);
      else
         cmd->u.begin_transform_feedback.counter_buffers[i] = NULL;
      if (pCounterBufferOffsets)
         cmd->u.begin_transform_feedback.counter_buffer_offsets[i] = pCounterBufferOffsets[i];
      else
         cmd->u.begin_transform_feedback.counter_buffer_offsets[i] = 0;
   }
   cmd_buf_queue(cmd_buffer, cmd);
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdEndTransformFeedbackEXT(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstCounterBuffer,
    uint32_t                                    counterBufferCount,
    const VkBuffer*                             pCounterBuffers,
    const VkDeviceSize*                         pCounterBufferOffsets)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   struct lvp_cmd_buffer_entry *cmd;
   uint32_t cmd_size = 0;

   cmd_size += counterBufferCount * (sizeof(struct lvp_buffer *) + sizeof(VkDeviceSize));

   cmd = cmd_buf_entry_alloc_size(cmd_buffer, cmd_size, LVP_CMD_END_TRANSFORM_FEEDBACK);
   if (!cmd)
      return;

   cmd->u.begin_transform_feedback.first_counter_buffer = firstCounterBuffer;
   cmd->u.begin_transform_feedback.counter_buffer_count = counterBufferCount;
   cmd->u.begin_transform_feedback.counter_buffers = (struct lvp_buffer **)(cmd + 1);
   cmd->u.begin_transform_feedback.counter_buffer_offsets = (VkDeviceSize *)(cmd->u.begin_transform_feedback.counter_buffers + counterBufferCount);

   for (unsigned i = 0; i < counterBufferCount; i++) {
      if (pCounterBuffers)
         cmd->u.begin_transform_feedback.counter_buffers[i] = lvp_buffer_from_handle(pCounterBuffers[i]);
      else
         cmd->u.begin_transform_feedback.counter_buffers[i] = NULL;
      if (pCounterBufferOffsets)
         cmd->u.begin_transform_feedback.counter_buffer_offsets[i] = pCounterBufferOffsets[i];
      else
         cmd->u.begin_transform_feedback.counter_buffer_offsets[i] = 0;
   }
   cmd_buf_queue(cmd_buffer, cmd);
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdDrawIndirectByteCountEXT(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    instanceCount,
    uint32_t                                    firstInstance,
    VkBuffer                                    counterBuffer,
    VkDeviceSize                                counterBufferOffset,
    uint32_t                                    counterOffset,
    uint32_t                                    vertexStride)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   struct lvp_cmd_buffer_entry *cmd;

   cmd = cmd_buf_entry_alloc(cmd_buffer, LVP_CMD_DRAW_INDIRECT_BYTE_COUNT);
   if (!cmd)
      return;

   cmd->u.draw_indirect_byte_count.instance_count = instanceCount;
   cmd->u.draw_indirect_byte_count.first_instance = firstInstance;
   cmd->u.draw_indirect_byte_count.counter_buffer = lvp_buffer_from_handle(counterBuffer);
   cmd->u.draw_indirect_byte_count.counter_buffer_offset = counterBufferOffset;
   cmd->u.draw_indirect_byte_count.counter_offset = counterOffset;
   cmd->u.draw_indirect_byte_count.vertex_stride = vertexStride;

   cmd_buf_queue(cmd_buffer, cmd);
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdSetDeviceMask(
   VkCommandBuffer commandBuffer,
   uint32_t deviceMask)
{
   /* No-op */
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdDispatchBase(
   VkCommandBuffer                             commandBuffer,
   uint32_t                                    base_x,
   uint32_t                                    base_y,
   uint32_t                                    base_z,
   uint32_t                                    x,
   uint32_t                                    y,
   uint32_t                                    z)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   struct lvp_cmd_buffer_entry *cmd;

   cmd = cmd_buf_entry_alloc(cmd_buffer, LVP_CMD_DISPATCH);
   if (!cmd)
      return;

   cmd->u.dispatch.x = x;
   cmd->u.dispatch.y = y;
   cmd->u.dispatch.z = z;
   cmd->u.dispatch.base_x = base_x;
   cmd->u.dispatch.base_y = base_y;
   cmd->u.dispatch.base_z = base_z;
   cmd_buf_queue(cmd_buffer, cmd);
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdBeginConditionalRenderingEXT(
   VkCommandBuffer commandBuffer,
   const VkConditionalRenderingBeginInfoEXT *pConditionalRenderingBegin)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   struct lvp_cmd_buffer_entry *cmd;
   cmd = cmd_buf_entry_alloc(cmd_buffer, LVP_CMD_BEGIN_CONDITIONAL_RENDERING);
   if (!cmd)
      return;

   cmd->u.begin_conditional_rendering.buffer = lvp_buffer_from_handle(pConditionalRenderingBegin->buffer);
   cmd->u.begin_conditional_rendering.offset = pConditionalRenderingBegin->offset;
   cmd->u.begin_conditional_rendering.inverted = pConditionalRenderingBegin->flags & VK_CONDITIONAL_RENDERING_INVERTED_BIT_EXT;
   cmd_buf_queue(cmd_buffer, cmd);
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdEndConditionalRenderingEXT(
   VkCommandBuffer commandBuffer)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   struct lvp_cmd_buffer_entry *cmd;
   cmd = cmd_buf_entry_alloc(cmd_buffer, LVP_CMD_END_CONDITIONAL_RENDERING);
   if (!cmd)
      return;
   cmd_buf_queue(cmd_buffer, cmd);
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdSetCullModeEXT(
    VkCommandBuffer                             commandBuffer,
    VkCullModeFlags                             cullMode)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   struct lvp_cmd_buffer_entry *cmd;

   cmd = cmd_buf_entry_alloc(cmd_buffer, LVP_CMD_SET_CULL_MODE);
   if (!cmd)
      return;

   cmd->u.set_cull_mode.cull_mode = cullMode;
   cmd_buf_queue(cmd_buffer, cmd);
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdSetFrontFaceEXT(
    VkCommandBuffer                             commandBuffer,
    VkFrontFace                                 frontFace)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   struct lvp_cmd_buffer_entry *cmd;

   cmd = cmd_buf_entry_alloc(cmd_buffer, LVP_CMD_SET_FRONT_FACE);
   if (!cmd)
      return;

   cmd->u.set_front_face.front_face = frontFace;
   cmd_buf_queue(cmd_buffer, cmd);
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdSetPrimitiveTopologyEXT(
    VkCommandBuffer                             commandBuffer,
    VkPrimitiveTopology                         primitiveTopology)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   struct lvp_cmd_buffer_entry *cmd;

   cmd = cmd_buf_entry_alloc(cmd_buffer, LVP_CMD_SET_PRIMITIVE_TOPOLOGY);
   if (!cmd)
      return;

   cmd->u.set_primitive_topology.prim = primitiveTopology;
   cmd_buf_queue(cmd_buffer, cmd);
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdSetViewportWithCountEXT(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    viewportCount,
    const VkViewport*                           pViewports)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   struct lvp_cmd_buffer_entry *cmd;
   int i;

   cmd = cmd_buf_entry_alloc(cmd_buffer, LVP_CMD_SET_VIEWPORT);
   if (!cmd)
      return;

   cmd->u.set_viewport.first_viewport = UINT32_MAX;
   cmd->u.set_viewport.viewport_count = viewportCount;
   for (i = 0; i < viewportCount; i++)
      cmd->u.set_viewport.viewports[i] = pViewports[i];

   cmd_buf_queue(cmd_buffer, cmd);
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdSetScissorWithCountEXT(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    scissorCount,
    const VkRect2D*                             pScissors)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   struct lvp_cmd_buffer_entry *cmd;
   int i;

   cmd = cmd_buf_entry_alloc(cmd_buffer, LVP_CMD_SET_SCISSOR);
   if (!cmd)
      return;

   cmd->u.set_scissor.first_scissor = UINT32_MAX;
   cmd->u.set_scissor.scissor_count = scissorCount;
   for (i = 0; i < scissorCount; i++)
      cmd->u.set_scissor.scissors[i] = pScissors[i];

   cmd_buf_queue(cmd_buffer, cmd);
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdBindVertexBuffers2EXT(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstBinding,
    uint32_t                                    bindingCount,
    const VkBuffer*                             pBuffers,
    const VkDeviceSize*                         pOffsets,
    const VkDeviceSize*                         pSizes,
    const VkDeviceSize*                         pStrides)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   struct lvp_cmd_buffer_entry *cmd;
   struct lvp_buffer **buffers;
   VkDeviceSize *offsets;
   VkDeviceSize *sizes;
   VkDeviceSize *strides;
   int i;
   uint32_t array_count = pStrides ? 3 : 2;
   uint32_t cmd_size = bindingCount * sizeof(struct lvp_buffer *) + bindingCount * array_count * sizeof(VkDeviceSize);

   cmd = cmd_buf_entry_alloc_size(cmd_buffer, cmd_size, LVP_CMD_BIND_VERTEX_BUFFERS);
   if (!cmd)
      return;

   cmd->u.vertex_buffers.first = firstBinding;
   cmd->u.vertex_buffers.binding_count = bindingCount;

   buffers = (struct lvp_buffer **)(cmd + 1);
   offsets = (VkDeviceSize *)(buffers + bindingCount);
   sizes = (VkDeviceSize *)(offsets + bindingCount);
   strides = (VkDeviceSize *)(sizes + bindingCount);
   for (i = 0; i < bindingCount; i++) {
      buffers[i] = lvp_buffer_from_handle(pBuffers[i]);
      offsets[i] = pOffsets[i];
      if (pSizes)
         sizes[i] = pSizes[i];
      else
         sizes[i] = 0;

      if (pStrides)
         strides[i] = pStrides[i];
   }
   cmd->u.vertex_buffers.buffers = buffers;
   cmd->u.vertex_buffers.offsets = offsets;
   cmd->u.vertex_buffers.sizes = sizes;
   cmd->u.vertex_buffers.strides = pStrides ? strides : NULL;

   cmd_buf_queue(cmd_buffer, cmd);
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdSetDepthTestEnableEXT(
    VkCommandBuffer                             commandBuffer,
    VkBool32                                    depthTestEnable)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   struct lvp_cmd_buffer_entry *cmd;

   cmd = cmd_buf_entry_alloc(cmd_buffer, LVP_CMD_SET_DEPTH_TEST_ENABLE);
   if (!cmd)
      return;

   cmd->u.set_depth_test_enable.depth_test_enable = depthTestEnable;
   cmd_buf_queue(cmd_buffer, cmd);
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdSetDepthWriteEnableEXT(
    VkCommandBuffer                             commandBuffer,
    VkBool32                                    depthWriteEnable)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   struct lvp_cmd_buffer_entry *cmd;

   cmd = cmd_buf_entry_alloc(cmd_buffer, LVP_CMD_SET_DEPTH_WRITE_ENABLE);
   if (!cmd)
      return;

   cmd->u.set_depth_write_enable.depth_write_enable = depthWriteEnable;
   cmd_buf_queue(cmd_buffer, cmd);
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdSetDepthCompareOpEXT(
    VkCommandBuffer                             commandBuffer,
    VkCompareOp                                 depthCompareOp)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   struct lvp_cmd_buffer_entry *cmd;

   cmd = cmd_buf_entry_alloc(cmd_buffer, LVP_CMD_SET_DEPTH_COMPARE_OP);
   if (!cmd)
      return;

   cmd->u.set_depth_compare_op.depth_op = depthCompareOp;
   cmd_buf_queue(cmd_buffer, cmd);
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdSetDepthBoundsTestEnableEXT(
    VkCommandBuffer                             commandBuffer,
    VkBool32                                    depthBoundsTestEnable)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   struct lvp_cmd_buffer_entry *cmd;

   cmd = cmd_buf_entry_alloc(cmd_buffer, LVP_CMD_SET_DEPTH_BOUNDS_TEST_ENABLE);
   if (!cmd)
      return;

   cmd->u.set_depth_bounds_test_enable.depth_bounds_test_enable = depthBoundsTestEnable;
   cmd_buf_queue(cmd_buffer, cmd);
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdSetStencilTestEnableEXT(
    VkCommandBuffer                             commandBuffer,
    VkBool32                                    stencilTestEnable)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   struct lvp_cmd_buffer_entry *cmd;

   cmd = cmd_buf_entry_alloc(cmd_buffer, LVP_CMD_SET_STENCIL_TEST_ENABLE);
   if (!cmd)
      return;

   cmd->u.set_stencil_test_enable.stencil_test_enable = stencilTestEnable;
   cmd_buf_queue(cmd_buffer, cmd);
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdSetStencilOpEXT(
    VkCommandBuffer                             commandBuffer,
    VkStencilFaceFlags                          faceMask,
    VkStencilOp                                 failOp,
    VkStencilOp                                 passOp,
    VkStencilOp                                 depthFailOp,
    VkCompareOp                                 compareOp)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   struct lvp_cmd_buffer_entry *cmd;

   cmd = cmd_buf_entry_alloc(cmd_buffer, LVP_CMD_SET_STENCIL_OP);
   if (!cmd)
      return;

   cmd->u.set_stencil_op.face_mask = faceMask;
   cmd->u.set_stencil_op.fail_op = failOp;
   cmd->u.set_stencil_op.pass_op = passOp;
   cmd->u.set_stencil_op.depth_fail_op = depthFailOp;
   cmd->u.set_stencil_op.compare_op = compareOp;
   cmd_buf_queue(cmd_buffer, cmd);
}
