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
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   VkResult result = vk_command_buffer_init(&cmd_buffer->vk, &device->vk);
   if (result != VK_SUCCESS) {
      vk_free(&pool->alloc, cmd_buffer);
      return result;
   }

   cmd_buffer->device = device;
   cmd_buffer->pool = pool;

   cmd_buffer->queue.alloc = &pool->alloc;
   list_inithead(&cmd_buffer->queue.cmds);

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

static VkResult lvp_reset_cmd_buffer(struct lvp_cmd_buffer *cmd_buffer)
{
   vk_command_buffer_reset(&cmd_buffer->vk);

   vk_free_queue(&cmd_buffer->queue);
   list_inithead(&cmd_buffer->queue.cmds);
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
         vk_command_buffer_finish(&cmd_buffer->vk);
         VkResult init_result =
            vk_command_buffer_init(&cmd_buffer->vk, &device->vk);
         if (init_result != VK_SUCCESS)
            result = init_result;

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
   vk_free_queue(&cmd_buffer->queue);
   list_del(&cmd_buffer->pool_link);
   vk_command_buffer_finish(&cmd_buffer->vk);
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
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

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

VKAPI_ATTR void VKAPI_CALL lvp_CmdDrawMultiEXT(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    drawCount,
    const VkMultiDrawInfoEXT                   *pVertexInfo,
    uint32_t                                    instanceCount,
    uint32_t                                    firstInstance,
    uint32_t                                    stride)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);

   struct vk_cmd_queue_entry *cmd = vk_zalloc(cmd_buffer->queue.alloc,
                                              sizeof(*cmd), 8,
                                              VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (!cmd)
      return;

   cmd->type = VK_CMD_DRAW_MULTI_EXT;
   list_addtail(&cmd->cmd_link, &cmd_buffer->queue.cmds);

   cmd->u.draw_multi_ext.draw_count = drawCount;
   if (pVertexInfo) {
      unsigned i = 0;
      cmd->u.draw_multi_ext.vertex_info = vk_zalloc(cmd_buffer->queue.alloc,
                                                    sizeof(*cmd->u.draw_multi_ext.vertex_info) * drawCount,
                                                    8,
                                                    VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
      vk_foreach_multi_draw(draw, i, pVertexInfo, drawCount, stride)
         memcpy(&cmd->u.draw_multi_ext.vertex_info[i], draw, sizeof(*cmd->u.draw_multi_ext.vertex_info));
   }
   cmd->u.draw_multi_ext.instance_count = instanceCount;
   cmd->u.draw_multi_ext.first_instance = firstInstance;
   cmd->u.draw_multi_ext.stride = stride;
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdDrawMultiIndexedEXT(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    drawCount,
    const VkMultiDrawIndexedInfoEXT            *pIndexInfo,
    uint32_t                                    instanceCount,
    uint32_t                                    firstInstance,
    uint32_t                                    stride,
    const int32_t                              *pVertexOffset)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);

   struct vk_cmd_queue_entry *cmd = vk_zalloc(cmd_buffer->queue.alloc,
                                              sizeof(*cmd), 8,
                                              VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (!cmd)
      return;

   cmd->type = VK_CMD_DRAW_MULTI_INDEXED_EXT;
   list_addtail(&cmd->cmd_link, &cmd_buffer->queue.cmds);

   cmd->u.draw_multi_indexed_ext.draw_count = drawCount;

   if (pIndexInfo) {
      unsigned i = 0;
      cmd->u.draw_multi_indexed_ext.index_info = vk_zalloc(cmd_buffer->queue.alloc,
                                                           sizeof(*cmd->u.draw_multi_indexed_ext.index_info) * drawCount,
                                                           8,
                                                           VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
      vk_foreach_multi_draw_indexed(draw, i, pIndexInfo, drawCount, stride) {
         cmd->u.draw_multi_indexed_ext.index_info[i].firstIndex = draw->firstIndex;
         cmd->u.draw_multi_indexed_ext.index_info[i].indexCount = draw->indexCount;
         if (pVertexOffset == NULL)
            cmd->u.draw_multi_indexed_ext.index_info[i].vertexOffset = draw->vertexOffset;
      }
   }

   cmd->u.draw_multi_indexed_ext.instance_count = instanceCount;
   cmd->u.draw_multi_indexed_ext.first_instance = firstInstance;
   cmd->u.draw_multi_indexed_ext.stride = stride;

   if (pVertexOffset) {
      cmd->u.draw_multi_indexed_ext.vertex_offset = vk_zalloc(cmd_buffer->queue.alloc, sizeof(*cmd->u.draw_multi_indexed_ext.vertex_offset), 8, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
      memcpy(cmd->u.draw_multi_indexed_ext.vertex_offset, pVertexOffset, sizeof(*cmd->u.draw_multi_indexed_ext.vertex_offset));
   }
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdPushDescriptorSetKHR(
   VkCommandBuffer                             commandBuffer,
   VkPipelineBindPoint                         pipelineBindPoint,
   VkPipelineLayout                            layout,
   uint32_t                                    set,
   uint32_t                                    descriptorWriteCount,
   const VkWriteDescriptorSet*                 pDescriptorWrites)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   struct vk_cmd_push_descriptor_set_khr *pds;

   struct vk_cmd_queue_entry *cmd = vk_zalloc(cmd_buffer->queue.alloc,
                                              sizeof(*cmd), 8,
                                              VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (!cmd)
      return;

   pds = &cmd->u.push_descriptor_set_khr;

   cmd->type = VK_CMD_PUSH_DESCRIPTOR_SET_KHR;
   list_addtail(&cmd->cmd_link, &cmd_buffer->queue.cmds);

   pds->pipeline_bind_point = pipelineBindPoint;
   pds->layout = layout;
   pds->set = set;
   pds->descriptor_write_count = descriptorWriteCount;

   if (pDescriptorWrites) {
      pds->descriptor_writes = vk_zalloc(cmd_buffer->queue.alloc,
                                         sizeof(*pds->descriptor_writes) * descriptorWriteCount,
                                         8,
                                         VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
      memcpy(pds->descriptor_writes,
             pDescriptorWrites,
             sizeof(*pds->descriptor_writes) * descriptorWriteCount);

      for (unsigned i = 0; i < descriptorWriteCount; i++) {
         switch (pds->descriptor_writes[i].descriptorType) {
         case VK_DESCRIPTOR_TYPE_SAMPLER:
         case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
         case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
         case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
         case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
            pds->descriptor_writes[i].pImageInfo = vk_zalloc(cmd_buffer->queue.alloc,
                                         sizeof(VkDescriptorImageInfo) * pds->descriptor_writes[i].descriptorCount,
                                         8,
                                         VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
            memcpy((VkDescriptorImageInfo *)pds->descriptor_writes[i].pImageInfo,
                   pDescriptorWrites[i].pImageInfo,
                   sizeof(VkDescriptorImageInfo) * pds->descriptor_writes[i].descriptorCount);
            break;
         case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
         case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
            pds->descriptor_writes[i].pTexelBufferView = vk_zalloc(cmd_buffer->queue.alloc,
                                         sizeof(VkBufferView) * pds->descriptor_writes[i].descriptorCount,
                                         8,
                                         VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
            memcpy((VkBufferView *)pds->descriptor_writes[i].pTexelBufferView,
                   pDescriptorWrites[i].pTexelBufferView,
                   sizeof(VkBufferView) * pds->descriptor_writes[i].descriptorCount);
            break;
         case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
         case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
         case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
         case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
         default:
            pds->descriptor_writes[i].pBufferInfo = vk_zalloc(cmd_buffer->queue.alloc,
                                         sizeof(VkDescriptorBufferInfo) * pds->descriptor_writes[i].descriptorCount,
                                         8,
                                         VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
            memcpy((VkDescriptorBufferInfo *)pds->descriptor_writes[i].pBufferInfo,
                   pDescriptorWrites[i].pBufferInfo,
                   sizeof(VkDescriptorBufferInfo) * pds->descriptor_writes[i].descriptorCount);
            break;
         }
      }
   }
}

VKAPI_ATTR void VKAPI_CALL lvp_CmdPushDescriptorSetWithTemplateKHR(
   VkCommandBuffer                             commandBuffer,
   VkDescriptorUpdateTemplate                  descriptorUpdateTemplate,
   VkPipelineLayout                            layout,
   uint32_t                                    set,
   const void*                                 pData)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   LVP_FROM_HANDLE(lvp_descriptor_update_template, templ, descriptorUpdateTemplate);
   size_t info_size = 0;
   struct vk_cmd_queue_entry *cmd = vk_zalloc(cmd_buffer->queue.alloc,
                                              sizeof(*cmd), 8,
                                              VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (!cmd)
      return;

   cmd->type = VK_CMD_PUSH_DESCRIPTOR_SET_WITH_TEMPLATE_KHR;

   list_addtail(&cmd->cmd_link, &cmd_buffer->queue.cmds);

   cmd->u.push_descriptor_set_with_template_khr.descriptor_update_template = descriptorUpdateTemplate;
   cmd->u.push_descriptor_set_with_template_khr.layout = layout;
   cmd->u.push_descriptor_set_with_template_khr.set = set;

   for (unsigned i = 0; i < templ->entry_count; i++) {
      VkDescriptorUpdateTemplateEntry *entry = &templ->entry[i];

      switch (entry->descriptorType) {
      case VK_DESCRIPTOR_TYPE_SAMPLER:
      case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
      case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
      case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
      case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
         info_size += sizeof(VkDescriptorImageInfo) * entry->descriptorCount;
         break;
      case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
         info_size += sizeof(VkBufferView) * entry->descriptorCount;
         break;
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
      default:
         info_size += sizeof(VkDescriptorBufferInfo) * entry->descriptorCount;
         break;
      }
   }

   cmd->u.push_descriptor_set_with_template_khr.data = vk_zalloc(cmd_buffer->queue.alloc, info_size, 8, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);

   uint64_t offset = 0;
   for (unsigned i = 0; i < templ->entry_count; i++) {
      VkDescriptorUpdateTemplateEntry *entry = &templ->entry[i];

      unsigned size = 0;
      switch (entry->descriptorType) {
      case VK_DESCRIPTOR_TYPE_SAMPLER:
      case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
      case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
      case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
      case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
         size = sizeof(VkDescriptorImageInfo);
         break;
      case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
         size = sizeof(VkBufferView);
         break;
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
      default:
         size = sizeof(VkDescriptorBufferInfo);
         break;
      }
      for (unsigned i = 0; i < entry->descriptorCount; i++) {
         memcpy((uint8_t*)cmd->u.push_descriptor_set_with_template_khr.data + offset, (const uint8_t*)pData + entry->offset + i * entry->stride, size);
         offset += size;
      }
   }
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
   struct vk_cmd_queue_entry *cmd = vk_zalloc(cmd_buffer->queue.alloc,
                                              sizeof(*cmd), 8,
                                              VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (!cmd)
      return;

   cmd->type = VK_CMD_BIND_DESCRIPTOR_SETS;
   list_addtail(&cmd->cmd_link, &cmd_buffer->queue.cmds);

   /* _layout could have been destroyed by when this command executes */
   struct lvp_descriptor_set_layout **set_layout = vk_zalloc(cmd_buffer->queue.alloc, sizeof(*set_layout) * layout->num_sets, 8, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   cmd->driver_data = set_layout;
   for (unsigned i = 0; i < layout->num_sets; i++)
      set_layout[i] = layout->set[i].layout;

   cmd->u.bind_descriptor_sets.pipeline_bind_point = pipelineBindPoint;
   cmd->u.bind_descriptor_sets.first_set = firstSet;
   cmd->u.bind_descriptor_sets.descriptor_set_count = descriptorSetCount;
   if (pDescriptorSets) {
      cmd->u.bind_descriptor_sets.descriptor_sets = vk_zalloc(cmd_buffer->queue.alloc, sizeof(*cmd->u.bind_descriptor_sets.descriptor_sets) * descriptorSetCount, 8, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
      memcpy(( VkDescriptorSet* )cmd->u.bind_descriptor_sets.descriptor_sets, pDescriptorSets, sizeof(*cmd->u.bind_descriptor_sets.descriptor_sets) * descriptorSetCount);
   }
   cmd->u.bind_descriptor_sets.dynamic_offset_count = dynamicOffsetCount;
   if (pDynamicOffsets) {
      cmd->u.bind_descriptor_sets.dynamic_offsets = vk_zalloc(cmd_buffer->queue.alloc, sizeof(*cmd->u.bind_descriptor_sets.dynamic_offsets) * dynamicOffsetCount, 8, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
      memcpy(( uint32_t* )cmd->u.bind_descriptor_sets.dynamic_offsets, pDynamicOffsets, sizeof(*cmd->u.bind_descriptor_sets.dynamic_offsets) * dynamicOffsetCount);
   }
}


VKAPI_ATTR void VKAPI_CALL lvp_CmdBeginRendering(VkCommandBuffer                             commandBuffer,
 const VkRenderingInfoKHR*                           pRenderingInfo
)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   struct vk_cmd_queue *queue = &cmd_buffer->queue;
   struct vk_cmd_queue_entry *cmd = vk_zalloc(queue->alloc,
                                              sizeof(*cmd), 8,
                                              VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (!cmd)
      return;

   cmd->type = VK_CMD_BEGIN_RENDERING;
   list_addtail(&cmd->cmd_link, &queue->cmds);

   if (pRenderingInfo) {
      cmd->u.begin_rendering.rendering_info = vk_zalloc(queue->alloc, sizeof(VkRenderingInfoKHR), 8, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
      memcpy((void*)cmd->u.begin_rendering.rendering_info, pRenderingInfo, sizeof(VkRenderingInfoKHR));
   VkRenderingInfoKHR *tmp_dst1 = (void *) cmd->u.begin_rendering.rendering_info; (void) tmp_dst1;
   VkRenderingInfoKHR *tmp_src1 = (void *) pRenderingInfo; (void) tmp_src1;   
   
      const VkBaseInStructure *pnext = tmp_dst1->pNext;
      if (pnext) {
         switch ((int32_t)pnext->sType) {
         
      case VK_STRUCTURE_TYPE_DEVICE_GROUP_RENDER_PASS_BEGIN_INFO:
         if (pnext) {
      tmp_dst1->pNext = vk_zalloc(queue->alloc, sizeof(VkDeviceGroupRenderPassBeginInfo), 8, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
      memcpy((void*)tmp_dst1->pNext, pnext, sizeof(VkDeviceGroupRenderPassBeginInfo));
   VkDeviceGroupRenderPassBeginInfo *tmp_dst2 = (void *) tmp_dst1->pNext; (void) tmp_dst2;
   VkDeviceGroupRenderPassBeginInfo *tmp_src2 = (void *) pnext; (void) tmp_src2;   
   tmp_dst2->pDeviceRenderAreas = vk_zalloc(queue->alloc, sizeof(*tmp_dst2->pDeviceRenderAreas) * tmp_dst2->deviceRenderAreaCount, 8, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   memcpy(( VkRect2D*  )tmp_dst2->pDeviceRenderAreas, tmp_src2->pDeviceRenderAreas, sizeof(*tmp_dst2->pDeviceRenderAreas) * tmp_dst2->deviceRenderAreaCount);
   } else {
      tmp_dst1->pNext = NULL;
   }
         break;
      
      case VK_STRUCTURE_TYPE_RENDERING_FRAGMENT_SHADING_RATE_ATTACHMENT_INFO_KHR:
         if (pnext) {
      tmp_dst1->pNext = vk_zalloc(queue->alloc, sizeof(VkRenderingFragmentShadingRateAttachmentInfoKHR), 8, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
      memcpy((void*)tmp_dst1->pNext, pnext, sizeof(VkRenderingFragmentShadingRateAttachmentInfoKHR));
   VkRenderingFragmentShadingRateAttachmentInfoKHR *tmp_dst2 = (void *) tmp_dst1->pNext; (void) tmp_dst2;
   VkRenderingFragmentShadingRateAttachmentInfoKHR *tmp_src2 = (void *) pnext; (void) tmp_src2;   
      } else {
      tmp_dst1->pNext = NULL;
   }
         break;
      
      case VK_STRUCTURE_TYPE_RENDERING_FRAGMENT_DENSITY_MAP_ATTACHMENT_INFO_EXT:
         if (pnext) {
      tmp_dst1->pNext = vk_zalloc(queue->alloc, sizeof(VkRenderingFragmentDensityMapAttachmentInfoEXT), 8, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
      memcpy((void*)tmp_dst1->pNext, pnext, sizeof(VkRenderingFragmentDensityMapAttachmentInfoEXT));
   VkRenderingFragmentDensityMapAttachmentInfoEXT *tmp_dst2 = (void *) tmp_dst1->pNext; (void) tmp_dst2;
   VkRenderingFragmentDensityMapAttachmentInfoEXT *tmp_src2 = (void *) pnext; (void) tmp_src2;   
      } else {
      tmp_dst1->pNext = NULL;
   }
         break;
      
      case VK_STRUCTURE_TYPE_MULTIVIEW_PER_VIEW_ATTRIBUTES_INFO_NVX:
         if (pnext) {
      tmp_dst1->pNext = vk_zalloc(queue->alloc, sizeof(VkMultiviewPerViewAttributesInfoNVX), 8, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
      memcpy((void*)tmp_dst1->pNext, pnext, sizeof(VkMultiviewPerViewAttributesInfoNVX));
   VkMultiviewPerViewAttributesInfoNVX *tmp_dst2 = (void *) tmp_dst1->pNext; (void) tmp_dst2;
   VkMultiviewPerViewAttributesInfoNVX *tmp_src2 = (void *) pnext; (void) tmp_src2;   
      } else {
      tmp_dst1->pNext = NULL;
   }
         break;
      
         }
      }
      tmp_dst1->pColorAttachments = vk_zalloc(queue->alloc, sizeof(*tmp_dst1->pColorAttachments) * tmp_dst1->colorAttachmentCount, 8, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   memcpy(( VkRenderingAttachmentInfoKHR*             )tmp_dst1->pColorAttachments, tmp_src1->pColorAttachments, sizeof(*tmp_dst1->pColorAttachments) * tmp_dst1->colorAttachmentCount);
      if (tmp_src1->pDepthAttachment) {
         tmp_dst1->pDepthAttachment = vk_zalloc(queue->alloc, sizeof(VkRenderingAttachmentInfoKHR), 8, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
         memcpy((void*)tmp_dst1->pDepthAttachment, tmp_src1->pDepthAttachment, sizeof(VkRenderingAttachmentInfoKHR));
      }
      if (tmp_src1->pStencilAttachment) {
         tmp_dst1->pStencilAttachment = vk_zalloc(queue->alloc, sizeof(VkRenderingAttachmentInfoKHR), 8, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
         memcpy((void*)tmp_dst1->pStencilAttachment, tmp_src1->pStencilAttachment, sizeof(VkRenderingAttachmentInfoKHR));
      }
   } else {
      cmd->u.begin_rendering.rendering_info = NULL;
   }
}
