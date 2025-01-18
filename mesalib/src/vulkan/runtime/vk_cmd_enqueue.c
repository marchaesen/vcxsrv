/*
 * Copyright © 2019 Red Hat.
 * Copyright © 2022 Collabora, LTD
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

#include "vk_alloc.h"
#include "vk_cmd_enqueue_entrypoints.h"
#include "vk_command_buffer.h"
#include "vk_descriptor_update_template.h"
#include "vk_device.h"
#include "vk_pipeline_layout.h"
#include "vk_util.h"

static inline unsigned
vk_descriptor_type_update_size(VkDescriptorType type)
{
   switch (type) {
   case VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK:
      unreachable("handled in caller");

   case VK_DESCRIPTOR_TYPE_SAMPLER:
   case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
   case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
   case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
   case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
      return sizeof(VkDescriptorImageInfo);

   case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
   case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
      return sizeof(VkBufferView);

   case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
      return sizeof(VkAccelerationStructureKHR);

   case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
   case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
   case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
   case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
   default:
      return sizeof(VkDescriptorBufferInfo);
   }
}

static void
vk_cmd_push_descriptor_set_with_template2_khr_free(
   struct vk_cmd_queue *queue, struct vk_cmd_queue_entry *cmd)
{
   struct vk_command_buffer *cmd_buffer =
      container_of(queue, struct vk_command_buffer, cmd_queue);
   struct vk_device *device = cmd_buffer->base.device;

   struct vk_cmd_push_descriptor_set_with_template2_khr *info_ =
      &cmd->u.push_descriptor_set_with_template2_khr;

   VkPushDescriptorSetWithTemplateInfoKHR *info =
      info_->push_descriptor_set_with_template_info;

   VK_FROM_HANDLE(vk_descriptor_update_template, templ,
                  info->descriptorUpdateTemplate);
   VK_FROM_HANDLE(vk_pipeline_layout, layout, info->layout);

   vk_descriptor_update_template_unref(device, templ);
   vk_pipeline_layout_unref(device, layout);

   if (info->pNext) {
      VkPipelineLayoutCreateInfo *pnext = (void *)info->pNext;

      vk_free(queue->alloc, (void *)pnext->pSetLayouts);
      vk_free(queue->alloc, (void *)pnext->pPushConstantRanges);
      vk_free(queue->alloc, pnext);
   }
}

VKAPI_ATTR void VKAPI_CALL
vk_cmd_enqueue_CmdPushDescriptorSetWithTemplate2KHR(
   VkCommandBuffer commandBuffer,
   const VkPushDescriptorSetWithTemplateInfoKHR *pPushDescriptorSetWithTemplateInfo)
{
   VK_FROM_HANDLE(vk_command_buffer, cmd_buffer, commandBuffer);

   struct vk_cmd_queue *queue = &cmd_buffer->cmd_queue;

   struct vk_cmd_queue_entry *cmd =
      vk_zalloc(cmd_buffer->cmd_queue.alloc, sizeof(*cmd), 8,
                VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!cmd)
      return;

   cmd->type = VK_CMD_PUSH_DESCRIPTOR_SET_WITH_TEMPLATE2_KHR;
   cmd->driver_free_cb = vk_cmd_push_descriptor_set_with_template2_khr_free;
   list_addtail(&cmd->cmd_link, &cmd_buffer->cmd_queue.cmds);

   VkPushDescriptorSetWithTemplateInfoKHR *info =
      vk_zalloc(cmd_buffer->cmd_queue.alloc,
                sizeof(VkPushDescriptorSetWithTemplateInfoKHR), 8,
                VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);

   cmd->u.push_descriptor_set_with_template2_khr
      .push_descriptor_set_with_template_info = info;

   /* From the application's perspective, the vk_cmd_queue_entry can outlive the
    * template. Therefore, we take a reference here and free it when the
    * vk_cmd_queue_entry is freed, tying the lifetimes.
    */
   info->descriptorUpdateTemplate =
      pPushDescriptorSetWithTemplateInfo->descriptorUpdateTemplate;

   VK_FROM_HANDLE(vk_descriptor_update_template, templ,
                  info->descriptorUpdateTemplate);
   vk_descriptor_update_template_ref(templ);

   info->set = pPushDescriptorSetWithTemplateInfo->set;
   info->sType = pPushDescriptorSetWithTemplateInfo->sType;

   /* Similar concerns for the pipeline layout */
   info->layout = pPushDescriptorSetWithTemplateInfo->layout;

   VK_FROM_HANDLE(vk_pipeline_layout, layout, info->layout);
   vk_pipeline_layout_ref(layout);

   /* What makes this tricky is that the size of pData is implicit. We determine
    * it by walking the template and determining the ranges read by the driver.
    */
   size_t data_size = 0;
   for (unsigned i = 0; i < templ->entry_count; ++i) {
      struct vk_descriptor_template_entry entry = templ->entries[i];
      unsigned end = 0;

      /* From the spec:
       *
       *    If descriptorType is VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK then
       *    the value of stride is ignored and the stride is assumed to be 1,
       *    i.e. the descriptor update information for them is always specified
       *    as a contiguous range.
       */
      if (entry.type == VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK) {
         end = entry.offset + entry.array_count;
      } else if (entry.array_count > 0) {
         end = entry.offset + ((entry.array_count - 1) * entry.stride) +
               vk_descriptor_type_update_size(entry.type);
      }

      data_size = MAX2(data_size, end);
   }

   uint8_t *out_pData = vk_zalloc(cmd_buffer->cmd_queue.alloc, data_size, 8,
                                  VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   const uint8_t *pData = pPushDescriptorSetWithTemplateInfo->pData;

   /* Now walk the template again, copying what we actually need */
   for (unsigned i = 0; i < templ->entry_count; ++i) {
      struct vk_descriptor_template_entry entry = templ->entries[i];
      unsigned size = 0;

      if (entry.type == VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK) {
         size = entry.array_count;
      } else if (entry.array_count > 0) {
         size = ((entry.array_count - 1) * entry.stride) +
                vk_descriptor_type_update_size(entry.type);
      }

      memcpy(out_pData + entry.offset, pData + entry.offset, size);
   }

   info->pData = out_pData;

   const VkBaseInStructure *pnext = pPushDescriptorSetWithTemplateInfo->pNext;

   if (pnext) {
      switch ((int32_t)pnext->sType) {
      /* TODO: The set layouts below would need to be reference counted. Punting
       * until there's a cmd_enqueue-based driver implementing
       * VK_NV_per_stage_descriptor_set.
       */
#if 0
      case VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO:
         info->pNext =
            vk_zalloc(queue->alloc, sizeof(VkPipelineLayoutCreateInfo), 8,
                      VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
         if (info->pNext == NULL)
            goto err;

         memcpy((void *)info->pNext, pnext,
                sizeof(VkPipelineLayoutCreateInfo));

         VkPipelineLayoutCreateInfo *tmp_dst2 = (void *)info->pNext;
         VkPipelineLayoutCreateInfo *tmp_src2 = (void *)pnext;

         if (tmp_src2->pSetLayouts) {
            tmp_dst2->pSetLayouts = vk_zalloc(
               queue->alloc,
               sizeof(*tmp_dst2->pSetLayouts) * tmp_dst2->setLayoutCount, 8,
               VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
            if (tmp_dst2->pSetLayouts == NULL)
               goto err;

            memcpy(
               (void *)tmp_dst2->pSetLayouts, tmp_src2->pSetLayouts,
               sizeof(*tmp_dst2->pSetLayouts) * tmp_dst2->setLayoutCount);
         }

         if (tmp_src2->pPushConstantRanges) {
            tmp_dst2->pPushConstantRanges =
               vk_zalloc(queue->alloc,
                         sizeof(*tmp_dst2->pPushConstantRanges) *
                            tmp_dst2->pushConstantRangeCount,
                         8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
            if (tmp_dst2->pPushConstantRanges == NULL)
               goto err;

            memcpy((void *)tmp_dst2->pPushConstantRanges,
                   tmp_src2->pPushConstantRanges,
                   sizeof(*tmp_dst2->pPushConstantRanges) *
                      tmp_dst2->pushConstantRangeCount);
         }
         break;
#endif

      default:
         goto err;
      }
   }

   return;

err:
   if (cmd)
      vk_cmd_push_descriptor_set_with_template2_khr_free(queue, cmd);

   vk_command_buffer_set_error(cmd_buffer, VK_ERROR_OUT_OF_HOST_MEMORY);
}

VKAPI_ATTR void VKAPI_CALL
vk_cmd_enqueue_CmdPushDescriptorSetWithTemplateKHR(
    VkCommandBuffer                             commandBuffer,
    VkDescriptorUpdateTemplate                  descriptorUpdateTemplate,
    VkPipelineLayout                            layout,
    uint32_t                                    set,
    const void*                                 pData)
{
   const VkPushDescriptorSetWithTemplateInfoKHR two = {
      .sType = VK_STRUCTURE_TYPE_PUSH_DESCRIPTOR_SET_WITH_TEMPLATE_INFO_KHR,
      .descriptorUpdateTemplate = descriptorUpdateTemplate,
      .layout = layout,
      .set = set,
      .pData = pData,
   };

   vk_cmd_enqueue_CmdPushDescriptorSetWithTemplate2KHR(commandBuffer, &two);
}

VKAPI_ATTR void VKAPI_CALL
vk_cmd_enqueue_CmdDrawMultiEXT(VkCommandBuffer commandBuffer,
                               uint32_t drawCount,
                               const VkMultiDrawInfoEXT *pVertexInfo,
                               uint32_t instanceCount,
                               uint32_t firstInstance,
                               uint32_t stride)
{
   VK_FROM_HANDLE(vk_command_buffer, cmd_buffer, commandBuffer);

   struct vk_cmd_queue_entry *cmd =
      vk_zalloc(cmd_buffer->cmd_queue.alloc, sizeof(*cmd), 8,
                VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!cmd)
      return;

   cmd->type = VK_CMD_DRAW_MULTI_EXT;
   list_addtail(&cmd->cmd_link, &cmd_buffer->cmd_queue.cmds);

   cmd->u.draw_multi_ext.draw_count = drawCount;
   if (pVertexInfo) {
      unsigned i = 0;
      cmd->u.draw_multi_ext.vertex_info =
         vk_zalloc(cmd_buffer->cmd_queue.alloc,
                   sizeof(*cmd->u.draw_multi_ext.vertex_info) * drawCount, 8,
                   VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);

      vk_foreach_multi_draw(draw, i, pVertexInfo, drawCount, stride) {
         memcpy(&cmd->u.draw_multi_ext.vertex_info[i], draw,
                sizeof(*cmd->u.draw_multi_ext.vertex_info));
      }
   }
   cmd->u.draw_multi_ext.instance_count = instanceCount;
   cmd->u.draw_multi_ext.first_instance = firstInstance;
   cmd->u.draw_multi_ext.stride = stride;
}

VKAPI_ATTR void VKAPI_CALL
vk_cmd_enqueue_CmdDrawMultiIndexedEXT(VkCommandBuffer commandBuffer,
                                      uint32_t drawCount,
                                      const VkMultiDrawIndexedInfoEXT *pIndexInfo,
                                      uint32_t instanceCount,
                                      uint32_t firstInstance,
                                      uint32_t stride,
                                      const int32_t *pVertexOffset)
{
   VK_FROM_HANDLE(vk_command_buffer, cmd_buffer, commandBuffer);

   struct vk_cmd_queue_entry *cmd =
      vk_zalloc(cmd_buffer->cmd_queue.alloc, sizeof(*cmd), 8,
                VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!cmd)
      return;

   cmd->type = VK_CMD_DRAW_MULTI_INDEXED_EXT;
   list_addtail(&cmd->cmd_link, &cmd_buffer->cmd_queue.cmds);

   cmd->u.draw_multi_indexed_ext.draw_count = drawCount;

   if (pIndexInfo) {
      unsigned i = 0;
      cmd->u.draw_multi_indexed_ext.index_info =
         vk_zalloc(cmd_buffer->cmd_queue.alloc,
                   sizeof(*cmd->u.draw_multi_indexed_ext.index_info) * drawCount, 8,
                   VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);

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
      cmd->u.draw_multi_indexed_ext.vertex_offset =
         vk_zalloc(cmd_buffer->cmd_queue.alloc,
                   sizeof(*cmd->u.draw_multi_indexed_ext.vertex_offset), 8,
                   VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);

      memcpy(cmd->u.draw_multi_indexed_ext.vertex_offset, pVertexOffset,
             sizeof(*cmd->u.draw_multi_indexed_ext.vertex_offset));
   }
}

static void
push_descriptors_set_free(struct vk_cmd_queue *queue,
                          struct vk_cmd_queue_entry *cmd)
{
   struct vk_command_buffer *cmd_buffer =
      container_of(queue, struct vk_command_buffer, cmd_queue);
   struct vk_cmd_push_descriptor_set_khr *pds = &cmd->u.push_descriptor_set_khr;

   VK_FROM_HANDLE(vk_pipeline_layout, vk_layout, pds->layout);
   vk_pipeline_layout_unref(cmd_buffer->base.device, vk_layout);

  for (unsigned i = 0; i < pds->descriptor_write_count; i++) {
    VkWriteDescriptorSet *entry = &pds->descriptor_writes[i];
    switch (entry->descriptorType) {
    case VK_DESCRIPTOR_TYPE_SAMPLER:
    case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
    case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
    case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
    case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
       vk_free(queue->alloc, (void *)entry->pImageInfo);
       break;
    case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
    case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
       vk_free(queue->alloc, (void *)entry->pTexelBufferView);
       break;
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
    default:
       vk_free(queue->alloc, (void *)entry->pBufferInfo);
       break;
    }
  }
}

VKAPI_ATTR void VKAPI_CALL
vk_cmd_enqueue_CmdPushDescriptorSetKHR(VkCommandBuffer commandBuffer,
                                       VkPipelineBindPoint pipelineBindPoint,
                                       VkPipelineLayout layout,
                                       uint32_t set,
                                       uint32_t descriptorWriteCount,
                                       const VkWriteDescriptorSet *pDescriptorWrites)
{
   VK_FROM_HANDLE(vk_command_buffer, cmd_buffer, commandBuffer);
   struct vk_cmd_push_descriptor_set_khr *pds;

   struct vk_cmd_queue_entry *cmd =
      vk_zalloc(cmd_buffer->cmd_queue.alloc, sizeof(*cmd), 8,
                VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!cmd)
      return;

   pds = &cmd->u.push_descriptor_set_khr;

   cmd->type = VK_CMD_PUSH_DESCRIPTOR_SET_KHR;
   cmd->driver_free_cb = push_descriptors_set_free;
   list_addtail(&cmd->cmd_link, &cmd_buffer->cmd_queue.cmds);

   pds->pipeline_bind_point = pipelineBindPoint;
   pds->set = set;
   pds->descriptor_write_count = descriptorWriteCount;

   /* From the application's perspective, the vk_cmd_queue_entry can outlive the
    * layout. Take a reference.
    */
   VK_FROM_HANDLE(vk_pipeline_layout, vk_layout, layout);
   pds->layout = layout;
   vk_pipeline_layout_ref(vk_layout);

   if (pDescriptorWrites) {
      pds->descriptor_writes =
         vk_zalloc(cmd_buffer->cmd_queue.alloc,
                   sizeof(*pds->descriptor_writes) * descriptorWriteCount, 8,
                   VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
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
            pds->descriptor_writes[i].pImageInfo =
               vk_zalloc(cmd_buffer->cmd_queue.alloc,
                         sizeof(VkDescriptorImageInfo) * pds->descriptor_writes[i].descriptorCount, 8,
                         VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
            memcpy((VkDescriptorImageInfo *)pds->descriptor_writes[i].pImageInfo,
                   pDescriptorWrites[i].pImageInfo,
                   sizeof(VkDescriptorImageInfo) * pds->descriptor_writes[i].descriptorCount);
            break;
         case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
         case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
            pds->descriptor_writes[i].pTexelBufferView =
               vk_zalloc(cmd_buffer->cmd_queue.alloc,
                         sizeof(VkBufferView) * pds->descriptor_writes[i].descriptorCount, 8,
                         VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
            memcpy((VkBufferView *)pds->descriptor_writes[i].pTexelBufferView,
                   pDescriptorWrites[i].pTexelBufferView,
                   sizeof(VkBufferView) * pds->descriptor_writes[i].descriptorCount);
            break;
         case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
         case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
         case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
         case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
         default:
            pds->descriptor_writes[i].pBufferInfo =
               vk_zalloc(cmd_buffer->cmd_queue.alloc,
                         sizeof(VkDescriptorBufferInfo) * pds->descriptor_writes[i].descriptorCount, 8,
                         VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
            memcpy((VkDescriptorBufferInfo *)pds->descriptor_writes[i].pBufferInfo,
                   pDescriptorWrites[i].pBufferInfo,
                   sizeof(VkDescriptorBufferInfo) * pds->descriptor_writes[i].descriptorCount);
            break;
         }
      }
   }
}

static void
unref_pipeline_layout(struct vk_cmd_queue *queue,
                      struct vk_cmd_queue_entry *cmd)
{
   struct vk_command_buffer *cmd_buffer =
      container_of(queue, struct vk_command_buffer, cmd_queue);
   VK_FROM_HANDLE(vk_pipeline_layout, layout,
                  cmd->u.bind_descriptor_sets.layout);

   assert(cmd->type == VK_CMD_BIND_DESCRIPTOR_SETS);

   vk_pipeline_layout_unref(cmd_buffer->base.device, layout);
}

VKAPI_ATTR void VKAPI_CALL
vk_cmd_enqueue_CmdBindDescriptorSets(VkCommandBuffer commandBuffer,
                                     VkPipelineBindPoint pipelineBindPoint,
                                     VkPipelineLayout layout,
                                     uint32_t firstSet,
                                     uint32_t descriptorSetCount,
                                     const VkDescriptorSet* pDescriptorSets,
                                     uint32_t dynamicOffsetCount,
                                     const uint32_t *pDynamicOffsets)
{
   VK_FROM_HANDLE(vk_command_buffer, cmd_buffer, commandBuffer);

   struct vk_cmd_queue_entry *cmd =
      vk_zalloc(cmd_buffer->cmd_queue.alloc, sizeof(*cmd), 8,
                VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!cmd)
      return;

   cmd->type = VK_CMD_BIND_DESCRIPTOR_SETS;
   list_addtail(&cmd->cmd_link, &cmd_buffer->cmd_queue.cmds);

   /* We need to hold a reference to the descriptor set as long as this
    * command is in the queue.  Otherwise, it may get deleted out from under
    * us before the command is replayed.
    */
   vk_pipeline_layout_ref(vk_pipeline_layout_from_handle(layout));
   cmd->u.bind_descriptor_sets.layout = layout;
   cmd->driver_free_cb = unref_pipeline_layout;

   cmd->u.bind_descriptor_sets.pipeline_bind_point = pipelineBindPoint;
   cmd->u.bind_descriptor_sets.first_set = firstSet;
   cmd->u.bind_descriptor_sets.descriptor_set_count = descriptorSetCount;
   if (pDescriptorSets) {
      cmd->u.bind_descriptor_sets.descriptor_sets =
         vk_zalloc(cmd_buffer->cmd_queue.alloc,
                   sizeof(*cmd->u.bind_descriptor_sets.descriptor_sets) * descriptorSetCount, 8,
                   VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);

      memcpy(cmd->u.bind_descriptor_sets.descriptor_sets, pDescriptorSets,
             sizeof(*cmd->u.bind_descriptor_sets.descriptor_sets) * descriptorSetCount);
   }
   cmd->u.bind_descriptor_sets.dynamic_offset_count = dynamicOffsetCount;
   if (pDynamicOffsets) {
      cmd->u.bind_descriptor_sets.dynamic_offsets =
         vk_zalloc(cmd_buffer->cmd_queue.alloc,
                   sizeof(*cmd->u.bind_descriptor_sets.dynamic_offsets) * dynamicOffsetCount, 8,
                   VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);

      memcpy(cmd->u.bind_descriptor_sets.dynamic_offsets, pDynamicOffsets,
             sizeof(*cmd->u.bind_descriptor_sets.dynamic_offsets) * dynamicOffsetCount);
   }
}

#ifdef VK_ENABLE_BETA_EXTENSIONS
static void
dispatch_graph_amdx_free(struct vk_cmd_queue *queue, struct vk_cmd_queue_entry *cmd)
{
   VkDispatchGraphCountInfoAMDX *count_info = cmd->u.dispatch_graph_amdx.count_info;
   void *infos = (void *)count_info->infos.hostAddress;

   for (uint32_t i = 0; i < count_info->count; i++) {
      VkDispatchGraphInfoAMDX *info = (void *)((const uint8_t *)infos + i * count_info->stride);
      vk_free(queue->alloc, (void *)info->payloads.hostAddress);
   }

   vk_free(queue->alloc, infos);
}

VKAPI_ATTR void VKAPI_CALL
vk_cmd_enqueue_CmdDispatchGraphAMDX(VkCommandBuffer commandBuffer, VkDeviceAddress scratch,
                                    const VkDispatchGraphCountInfoAMDX *pCountInfo)
{
   VK_FROM_HANDLE(vk_command_buffer, cmd_buffer, commandBuffer);

   if (vk_command_buffer_has_error(cmd_buffer))
      return;

   VkResult result = VK_SUCCESS;
   const VkAllocationCallbacks *alloc = cmd_buffer->cmd_queue.alloc;

   struct vk_cmd_queue_entry *cmd =
      vk_zalloc(alloc, sizeof(struct vk_cmd_queue_entry), 8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!cmd) {
      result = VK_ERROR_OUT_OF_HOST_MEMORY;
      goto err;
   }

   cmd->type = VK_CMD_DISPATCH_GRAPH_AMDX;
   cmd->driver_free_cb = dispatch_graph_amdx_free;

   cmd->u.dispatch_graph_amdx.scratch = scratch;

   cmd->u.dispatch_graph_amdx.count_info =
      vk_zalloc(alloc, sizeof(VkDispatchGraphCountInfoAMDX), 8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (cmd->u.dispatch_graph_amdx.count_info == NULL)
      goto err;

   memcpy((void *)cmd->u.dispatch_graph_amdx.count_info, pCountInfo,
          sizeof(VkDispatchGraphCountInfoAMDX));

   uint32_t infos_size = pCountInfo->count * pCountInfo->stride;
   void *infos = vk_zalloc(alloc, infos_size, 8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   cmd->u.dispatch_graph_amdx.count_info->infos.hostAddress = infos;
   memcpy(infos, pCountInfo->infos.hostAddress, infos_size);

   for (uint32_t i = 0; i < pCountInfo->count; i++) {
      VkDispatchGraphInfoAMDX *info = (void *)((const uint8_t *)infos + i * pCountInfo->stride);

      uint32_t payloads_size = info->payloadCount * info->payloadStride;
      void *dst_payload = vk_zalloc(alloc, payloads_size, 8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
      memcpy(dst_payload, info->payloads.hostAddress, payloads_size);
      info->payloads.hostAddress = dst_payload;
   }

   list_addtail(&cmd->cmd_link, &cmd_buffer->cmd_queue.cmds);
   goto finish;
err:
   if (cmd) {
      vk_free(alloc, cmd);
      dispatch_graph_amdx_free(&cmd_buffer->cmd_queue, cmd);
   }

finish:
   if (unlikely(result != VK_SUCCESS))
      vk_command_buffer_set_error(cmd_buffer, result);
}
#endif

static void
vk_cmd_build_acceleration_structures_khr_free(struct vk_cmd_queue *queue,
                                              struct vk_cmd_queue_entry *cmd)
{
   struct vk_cmd_build_acceleration_structures_khr *build =
      &cmd->u.build_acceleration_structures_khr;
   
   for (uint32_t i = 0; i < build->info_count; i++) {
      vk_free(queue->alloc, (void *)build->infos[i].pGeometries);
      vk_free(queue->alloc, (void *)build->pp_build_range_infos[i]);
   }
}

VKAPI_ATTR void VKAPI_CALL
vk_cmd_enqueue_CmdBuildAccelerationStructuresKHR(
   VkCommandBuffer commandBuffer, uint32_t infoCount,
   const VkAccelerationStructureBuildGeometryInfoKHR *pInfos,
   const VkAccelerationStructureBuildRangeInfoKHR *const *ppBuildRangeInfos)
{
   VK_FROM_HANDLE(vk_command_buffer, cmd_buffer, commandBuffer);

   if (vk_command_buffer_has_error(cmd_buffer))
      return;

   struct vk_cmd_queue *queue = &cmd_buffer->cmd_queue;

   struct vk_cmd_queue_entry *cmd =
      vk_zalloc(queue->alloc, vk_cmd_queue_type_sizes[VK_CMD_BUILD_ACCELERATION_STRUCTURES_KHR], 8,
                VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!cmd)
      goto err;

   cmd->type = VK_CMD_BUILD_ACCELERATION_STRUCTURES_KHR;
   cmd->driver_free_cb = vk_cmd_build_acceleration_structures_khr_free;

   struct vk_cmd_build_acceleration_structures_khr *build =
      &cmd->u.build_acceleration_structures_khr;

   build->info_count = infoCount;
   if (pInfos) {
      build->infos = vk_zalloc(queue->alloc, sizeof(*build->infos) * infoCount, 8,
                               VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
      if (!build->infos)
         goto err;

      memcpy((VkAccelerationStructureBuildGeometryInfoKHR *)build->infos, pInfos,
             sizeof(*build->infos) * (infoCount));
   
      for (uint32_t i = 0; i < infoCount; i++) {
         uint32_t geometries_size =
            build->infos[i].geometryCount * sizeof(VkAccelerationStructureGeometryKHR);
         VkAccelerationStructureGeometryKHR *geometries =
            vk_zalloc(queue->alloc, geometries_size, 8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
         if (!geometries)
            goto err;

         if (pInfos[i].pGeometries) {
            memcpy(geometries, pInfos[i].pGeometries, geometries_size);
         } else {
            for (uint32_t j = 0; j < build->infos[i].geometryCount; j++)
               memcpy(&geometries[j], pInfos[i].ppGeometries[j], sizeof(VkAccelerationStructureGeometryKHR));
         }

         build->infos[i].pGeometries = geometries;
      }
   }
   if (ppBuildRangeInfos) {
      build->pp_build_range_infos =
         vk_zalloc(queue->alloc, sizeof(*build->pp_build_range_infos) * infoCount, 8,
                   VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
      if (!build->pp_build_range_infos)
         goto err;

      VkAccelerationStructureBuildRangeInfoKHR **pp_build_range_infos =
         (void *)build->pp_build_range_infos;

      for (uint32_t i = 0; i < infoCount; i++) {
         uint32_t build_range_size =
            build->infos[i].geometryCount * sizeof(VkAccelerationStructureBuildRangeInfoKHR);
         VkAccelerationStructureBuildRangeInfoKHR *p_build_range_infos =
            vk_zalloc(queue->alloc, build_range_size, 8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
         if (!p_build_range_infos)
            goto err;

         memcpy(p_build_range_infos, ppBuildRangeInfos[i], build_range_size);

         pp_build_range_infos[i] = p_build_range_infos;
      }
   }

   list_addtail(&cmd->cmd_link, &queue->cmds);
   return;

err:
   if (cmd)
      vk_cmd_build_acceleration_structures_khr_free(queue, cmd);

   vk_command_buffer_set_error(cmd_buffer, VK_ERROR_OUT_OF_HOST_MEMORY);
}

VKAPI_ATTR void VKAPI_CALL vk_cmd_enqueue_CmdPushConstants2KHR(
   VkCommandBuffer                             commandBuffer,
   const VkPushConstantsInfoKHR* pPushConstantsInfo)
{
   VK_FROM_HANDLE(vk_command_buffer, cmd_buffer, commandBuffer);
   struct vk_cmd_queue *queue = &cmd_buffer->cmd_queue;

   struct vk_cmd_queue_entry *cmd = vk_zalloc(queue->alloc, vk_cmd_queue_type_sizes[VK_CMD_PUSH_CONSTANTS2_KHR], 8,
                                              VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!cmd)
      return;

   cmd->type = VK_CMD_PUSH_CONSTANTS2_KHR;

   VkPushConstantsInfoKHR *info = vk_zalloc(queue->alloc, sizeof(*info), 8,
                                            VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   void *pValues = vk_zalloc(queue->alloc, pPushConstantsInfo->size, 8,
                             VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);

   memcpy(info, pPushConstantsInfo, sizeof(*info));
   memcpy(pValues, pPushConstantsInfo->pValues, pPushConstantsInfo->size);

   cmd->u.push_constants2_khr.push_constants_info = info;
   info->pValues = pValues;

   list_addtail(&cmd->cmd_link, &cmd_buffer->cmd_queue.cmds);
}

static void
vk_free_cmd_push_descriptor_set2_khr(struct vk_cmd_queue *queue,
                                     struct vk_cmd_queue_entry *cmd)
{
   ralloc_free(cmd->driver_data);
}

VKAPI_ATTR void VKAPI_CALL vk_cmd_enqueue_CmdPushDescriptorSet2KHR(
    VkCommandBuffer                             commandBuffer,
    const VkPushDescriptorSetInfoKHR*           pPushDescriptorSetInfo)
{
   VK_FROM_HANDLE(vk_command_buffer, cmd_buffer, commandBuffer);
   struct vk_cmd_queue_entry *cmd = vk_zalloc(cmd_buffer->cmd_queue.alloc, vk_cmd_queue_type_sizes[VK_CMD_PUSH_DESCRIPTOR_SET2_KHR], 8,
                                              VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);

   cmd->type = VK_CMD_PUSH_DESCRIPTOR_SET2_KHR;
   cmd->driver_free_cb = vk_free_cmd_push_descriptor_set2_khr;

   void *ctx = cmd->driver_data = ralloc_context(NULL);
   if (pPushDescriptorSetInfo) {
      cmd->u.push_descriptor_set2_khr.push_descriptor_set_info = vk_zalloc(cmd_buffer->cmd_queue.alloc, sizeof(VkPushDescriptorSetInfoKHR), 8,
                                                                           VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);

      memcpy((void*)cmd->u.push_descriptor_set2_khr.push_descriptor_set_info, pPushDescriptorSetInfo, sizeof(VkPushDescriptorSetInfoKHR));
      VkPushDescriptorSetInfoKHR *tmp_dst1 = (void *) cmd->u.push_descriptor_set2_khr.push_descriptor_set_info; (void) tmp_dst1;
      VkPushDescriptorSetInfoKHR *tmp_src1 = (void *) pPushDescriptorSetInfo; (void) tmp_src1;

      const VkBaseInStructure *pnext = tmp_dst1->pNext;
      if (pnext) {
         switch ((int32_t)pnext->sType) {
         case VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO:
            if (pnext) {
               tmp_dst1->pNext = rzalloc(ctx, VkPipelineLayoutCreateInfo);

               memcpy((void*)tmp_dst1->pNext, pnext, sizeof(VkPipelineLayoutCreateInfo));
               VkPipelineLayoutCreateInfo *tmp_dst2 = (void *) tmp_dst1->pNext; (void) tmp_dst2;
               VkPipelineLayoutCreateInfo *tmp_src2 = (void *) pnext; (void) tmp_src2;
               if (tmp_src2->pSetLayouts) {
                  tmp_dst2->pSetLayouts = rzalloc_array_size(ctx, sizeof(*tmp_dst2->pSetLayouts), tmp_dst2->setLayoutCount);

                  memcpy((void*)tmp_dst2->pSetLayouts, tmp_src2->pSetLayouts, sizeof(*tmp_dst2->pSetLayouts) * tmp_dst2->setLayoutCount);
               }
               if (tmp_src2->pPushConstantRanges) {
                  tmp_dst2->pPushConstantRanges = rzalloc_array_size(ctx, sizeof(*tmp_dst2->pPushConstantRanges), tmp_dst2->pushConstantRangeCount);

                  memcpy((void*)tmp_dst2->pPushConstantRanges, tmp_src2->pPushConstantRanges, sizeof(*tmp_dst2->pPushConstantRanges) * tmp_dst2->pushConstantRangeCount);
               }

            } else {
               tmp_dst1->pNext = NULL;
            }
            break;
         }
      }
      if (tmp_src1->pDescriptorWrites) {
         tmp_dst1->pDescriptorWrites = vk_zalloc(cmd_buffer->cmd_queue.alloc, sizeof(*tmp_dst1->pDescriptorWrites) * tmp_dst1->descriptorWriteCount, 8,
                                                 VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);

         memcpy((void*)tmp_dst1->pDescriptorWrites, tmp_src1->pDescriptorWrites, sizeof(*tmp_dst1->pDescriptorWrites) * tmp_dst1->descriptorWriteCount);
         for (unsigned i = 0; i < tmp_src1->descriptorWriteCount; i++) {
            VkWriteDescriptorSet *dstwrite = (void*)&tmp_dst1->pDescriptorWrites[i];
            const VkWriteDescriptorSet *write = &tmp_src1->pDescriptorWrites[i];
            switch (write->descriptorType) {
            case VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK: {
               const VkWriteDescriptorSetInlineUniformBlock *uniform_data = vk_find_struct_const(write->pNext, WRITE_DESCRIPTOR_SET_INLINE_UNIFORM_BLOCK);
               assert(uniform_data);
               VkWriteDescriptorSetInlineUniformBlock *dst = rzalloc(ctx, VkWriteDescriptorSetInlineUniformBlock);
               memcpy((void*)dst, uniform_data, sizeof(*uniform_data));
               dst->pData = ralloc_size(ctx, uniform_data->dataSize);
               memcpy((void*)dst->pData, uniform_data->pData, uniform_data->dataSize);
               dstwrite->pNext = dst;
               break;
            }

            case VK_DESCRIPTOR_TYPE_SAMPLER:
            case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
            case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
            case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
            case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
               dstwrite->pImageInfo = rzalloc_array(ctx, VkDescriptorImageInfo, write->descriptorCount);
               {
                  VkDescriptorImageInfo *arr = (void*)dstwrite->pImageInfo;
                  typed_memcpy(arr, write->pImageInfo, write->descriptorCount);
               }
               break;

            case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
            case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
               dstwrite->pTexelBufferView = rzalloc_array(ctx, VkBufferView, write->descriptorCount);
               {
                  VkBufferView *arr = (void*)dstwrite->pTexelBufferView;
                  typed_memcpy(arr, write->pTexelBufferView, write->descriptorCount);
               }
               break;

            case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
            case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
            case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
            case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
               dstwrite->pBufferInfo = rzalloc_array(ctx, VkDescriptorBufferInfo, write->descriptorCount);
               {
                  VkDescriptorBufferInfo *arr = (void*)dstwrite->pBufferInfo;
                  typed_memcpy(arr, write->pBufferInfo, write->descriptorCount);
               }
               break;

            case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR: {
               const VkWriteDescriptorSetAccelerationStructureKHR *accel_structs =
                  vk_find_struct_const(write->pNext, WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR);

               uint32_t accel_structs_size = sizeof(VkAccelerationStructureKHR) * accel_structs->accelerationStructureCount;
               VkWriteDescriptorSetAccelerationStructureKHR *write_accel_structs =
                  rzalloc_size(ctx, sizeof(VkWriteDescriptorSetAccelerationStructureKHR) + accel_structs_size);

               write_accel_structs->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
               write_accel_structs->accelerationStructureCount = accel_structs->accelerationStructureCount;
               write_accel_structs->pAccelerationStructures = (void *)&write_accel_structs[1];
               memcpy((void *)write_accel_structs->pAccelerationStructures, accel_structs->pAccelerationStructures, accel_structs_size);

               dstwrite->pNext = write_accel_structs;
               break;
            }

            default:
               break;
            }
         }
      }
   }

   list_addtail(&cmd->cmd_link, &cmd_buffer->cmd_queue.cmds);
}
