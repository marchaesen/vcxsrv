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

#include "vk_common_entrypoints.h"

static void
lvp_cmd_buffer_destroy(struct vk_command_buffer *cmd_buffer)
{
   vk_command_buffer_finish(cmd_buffer);
   vk_free(&cmd_buffer->pool->alloc, cmd_buffer);
}

static VkResult
lvp_create_cmd_buffer(struct vk_command_pool *pool,
                      struct vk_command_buffer **cmd_buffer_out)
{
   struct lvp_device *device =
      container_of(pool->base.device, struct lvp_device, vk);
   struct lvp_cmd_buffer *cmd_buffer;

   cmd_buffer = vk_alloc(&pool->alloc, sizeof(*cmd_buffer), 8,
                         VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (cmd_buffer == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   VkResult result = vk_command_buffer_init(pool, &cmd_buffer->vk,
                                            &lvp_cmd_buffer_ops, 0);
   if (result != VK_SUCCESS) {
      vk_free(&pool->alloc, cmd_buffer);
      return result;
   }

   cmd_buffer->device = device;

   cmd_buffer->status = LVP_CMD_BUFFER_STATUS_INITIAL;
   *cmd_buffer_out = &cmd_buffer->vk;

   return VK_SUCCESS;
}

static void
lvp_reset_cmd_buffer(struct vk_command_buffer *vk_cmd_buffer,
                     UNUSED VkCommandBufferResetFlags flags)
{
   struct lvp_cmd_buffer *cmd_buffer =
      container_of(vk_cmd_buffer, struct lvp_cmd_buffer, vk);

   vk_command_buffer_reset(&cmd_buffer->vk);

   cmd_buffer->status = LVP_CMD_BUFFER_STATUS_INITIAL;
}

const struct vk_command_buffer_ops lvp_cmd_buffer_ops = {
   .create = lvp_create_cmd_buffer,
   .reset = lvp_reset_cmd_buffer,
   .destroy = lvp_cmd_buffer_destroy,
};

VKAPI_ATTR VkResult VKAPI_CALL lvp_BeginCommandBuffer(
   VkCommandBuffer                             commandBuffer,
   const VkCommandBufferBeginInfo*             pBeginInfo)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   if (cmd_buffer->status != LVP_CMD_BUFFER_STATUS_INITIAL)
      lvp_reset_cmd_buffer(&cmd_buffer->vk, 0);
   cmd_buffer->status = LVP_CMD_BUFFER_STATUS_RECORDING;
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL lvp_EndCommandBuffer(
   VkCommandBuffer                             commandBuffer)
{
   LVP_FROM_HANDLE(lvp_cmd_buffer, cmd_buffer, commandBuffer);
   VkResult result = vk_command_buffer_get_record_result(&cmd_buffer->vk);

   cmd_buffer->status = result == VK_SUCCESS ?
      LVP_CMD_BUFFER_STATUS_EXECUTABLE :
      LVP_CMD_BUFFER_STATUS_INVALID;

   return result;
}

static void
lvp_free_CmdPushDescriptorSetWithTemplateKHR(struct vk_cmd_queue *queue, struct vk_cmd_queue_entry *cmd)
{
   struct lvp_device *device = cmd->driver_data;
   LVP_FROM_HANDLE(lvp_descriptor_update_template, templ, cmd->u.push_descriptor_set_with_template_khr.descriptor_update_template);
   lvp_descriptor_template_templ_unref(device, templ);
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
   struct vk_cmd_queue_entry *cmd = vk_zalloc(cmd_buffer->vk.cmd_queue.alloc,
                                              sizeof(*cmd), 8,
                                              VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (!cmd)
      return;

   cmd->type = VK_CMD_PUSH_DESCRIPTOR_SET_WITH_TEMPLATE_KHR;

   list_addtail(&cmd->cmd_link, &cmd_buffer->vk.cmd_queue.cmds);
   cmd->driver_free_cb = lvp_free_CmdPushDescriptorSetWithTemplateKHR;
   cmd->driver_data = cmd_buffer->device;

   cmd->u.push_descriptor_set_with_template_khr.descriptor_update_template = descriptorUpdateTemplate;
   lvp_descriptor_template_templ_ref(templ);
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

   cmd->u.push_descriptor_set_with_template_khr.data = vk_zalloc(cmd_buffer->vk.cmd_queue.alloc, info_size, 8, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);

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
