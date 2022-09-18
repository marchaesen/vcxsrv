/*
 * Copyright © 2021 Intel Corporation
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

#include "vk_command_buffer.h"

#include "vk_command_pool.h"
#include "vk_common_entrypoints.h"
#include "vk_device.h"

VkResult
vk_command_buffer_init(struct vk_command_pool *pool,
                       struct vk_command_buffer *command_buffer,
                       const struct vk_command_buffer_ops *ops,
                       VkCommandBufferLevel level)
{
   memset(command_buffer, 0, sizeof(*command_buffer));
   vk_object_base_init(pool->base.device, &command_buffer->base,
                       VK_OBJECT_TYPE_COMMAND_BUFFER);

   command_buffer->pool = pool;
   command_buffer->level = level;
   command_buffer->ops = ops;
   vk_dynamic_graphics_state_init(&command_buffer->dynamic_graphics_state);
   command_buffer->record_result = VK_SUCCESS;
   vk_cmd_queue_init(&command_buffer->cmd_queue, &pool->alloc);
   util_dynarray_init(&command_buffer->labels, NULL);
   command_buffer->region_begin = true;

   list_add(&command_buffer->pool_link, &pool->command_buffers);

   return VK_SUCCESS;
}

void
vk_command_buffer_reset(struct vk_command_buffer *command_buffer)
{
   vk_dynamic_graphics_state_clear(&command_buffer->dynamic_graphics_state);
   command_buffer->record_result = VK_SUCCESS;
   vk_command_buffer_reset_render_pass(command_buffer);
   vk_cmd_queue_reset(&command_buffer->cmd_queue);
   util_dynarray_clear(&command_buffer->labels);
   command_buffer->region_begin = true;
}

void
vk_command_buffer_finish(struct vk_command_buffer *command_buffer)
{
   list_del(&command_buffer->pool_link);
   vk_command_buffer_reset_render_pass(command_buffer);
   vk_cmd_queue_finish(&command_buffer->cmd_queue);
   util_dynarray_fini(&command_buffer->labels);
   vk_object_base_finish(&command_buffer->base);
}

void
vk_command_buffer_recycle(struct vk_command_buffer *cmd_buffer)
{
   /* Reset, returning resources to the pool.  The command buffer object
    * itself will be recycled but, if the driver supports returning other
    * resources such as batch buffers to the pool, it should do so so they're
    * not tied up in recycled command buffer objects.
    */
   cmd_buffer->ops->reset(cmd_buffer,
      VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);

   vk_object_base_recycle(&cmd_buffer->base);
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_common_ResetCommandBuffer(VkCommandBuffer commandBuffer,
                             VkCommandBufferResetFlags flags)
{
   VK_FROM_HANDLE(vk_command_buffer, cmd_buffer, commandBuffer);

   cmd_buffer->ops->reset(cmd_buffer, flags);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
vk_common_CmdExecuteCommands(VkCommandBuffer commandBuffer,
                             uint32_t commandBufferCount,
                             const VkCommandBuffer *pCommandBuffers)
{
   VK_FROM_HANDLE(vk_command_buffer, primary, commandBuffer);
   const struct vk_device_dispatch_table *disp =
      primary->base.device->command_dispatch_table;

   for (uint32_t i = 0; i < commandBufferCount; i++) {
      VK_FROM_HANDLE(vk_command_buffer, secondary, pCommandBuffers[i]);

      vk_cmd_queue_execute(&secondary->cmd_queue, commandBuffer, disp);
   }
}

VKAPI_ATTR void VKAPI_CALL
vk_common_CmdBindVertexBuffers(VkCommandBuffer commandBuffer,
                               uint32_t firstBinding,
                               uint32_t bindingCount,
                               const VkBuffer *pBuffers,
                               const VkDeviceSize *pOffsets)
{
   VK_FROM_HANDLE(vk_command_buffer, cmd_buffer, commandBuffer);
   const struct vk_device_dispatch_table *disp =
      &cmd_buffer->base.device->dispatch_table;

   disp->CmdBindVertexBuffers2(commandBuffer, firstBinding, bindingCount,
                               pBuffers, pOffsets, NULL, NULL);
}
