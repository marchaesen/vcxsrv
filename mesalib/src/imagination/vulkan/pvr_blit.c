/*
 * Copyright © 2022 Imagination Technologies Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <assert.h>
#include <stdint.h>
#include <vulkan/vulkan.h>

#include "pvr_csb.h"
#include "pvr_private.h"
#include "util/list.h"
#include "vk_alloc.h"
#include "vk_command_buffer.h"
#include "vk_command_pool.h"
#include "vk_log.h"

/* TODO: Investigate where this limit comes from. */
#define PVR_MAX_TRANSFER_SIZE_IN_TEXELS 2048U

void pvr_CmdBlitImage2KHR(VkCommandBuffer commandBuffer,
                          const VkBlitImageInfo2 *pBlitImageInfo)
{
   assert(!"Unimplemented");
}

VkResult
pvr_copy_or_resolve_color_image_region(struct pvr_cmd_buffer *cmd_buffer,
                                       const struct pvr_image *src,
                                       const struct pvr_image *dst,
                                       const VkImageCopy2 *region)
{
   assert(!"Unimplemented");
   return VK_SUCCESS;
}

void pvr_CmdCopyImageToBuffer2KHR(
   VkCommandBuffer commandBuffer,
   const VkCopyImageToBufferInfo2 *pCopyImageToBufferInfo)
{
   assert(!"Unimplemented");
}

void pvr_CmdCopyImage2KHR(VkCommandBuffer commandBuffer,
                          const VkCopyImageInfo2 *pCopyImageInfo)
{
   assert(!"Unimplemented");
}

void pvr_CmdUpdateBuffer(VkCommandBuffer commandBuffer,
                         VkBuffer dstBuffer,
                         VkDeviceSize dstOffset,
                         VkDeviceSize dataSize,
                         const void *pData)
{
   assert(!"Unimplemented");
}

void pvr_CmdFillBuffer(VkCommandBuffer commandBuffer,
                       VkBuffer dstBuffer,
                       VkDeviceSize dstOffset,
                       VkDeviceSize fillSize,
                       uint32_t data)
{
   assert(!"Unimplemented");
}

void pvr_CmdCopyBufferToImage2KHR(
   VkCommandBuffer commandBuffer,
   const VkCopyBufferToImageInfo2 *pCopyBufferToImageInfo)
{
   assert(!"Unimplemented");
}

void pvr_CmdClearColorImage(VkCommandBuffer commandBuffer,
                            VkImage _image,
                            VkImageLayout imageLayout,
                            const VkClearColorValue *pColor,
                            uint32_t rangeCount,
                            const VkImageSubresourceRange *pRanges)
{
   assert(!"Unimplemented");
}

void pvr_CmdClearDepthStencilImage(VkCommandBuffer commandBuffer,
                                   VkImage image_h,
                                   VkImageLayout imageLayout,
                                   const VkClearDepthStencilValue *pDepthStencil,
                                   uint32_t rangeCount,
                                   const VkImageSubresourceRange *pRanges)
{
   assert(!"Unimplemented");
}

void pvr_CmdCopyBuffer2KHR(VkCommandBuffer commandBuffer,
                           const VkCopyBufferInfo2 *pCopyBufferInfo)
{
   PVR_FROM_HANDLE(pvr_cmd_buffer, cmd_buffer, commandBuffer);
   PVR_FROM_HANDLE(pvr_buffer, src, pCopyBufferInfo->srcBuffer);
   PVR_FROM_HANDLE(pvr_buffer, dst, pCopyBufferInfo->dstBuffer);
   const size_t regions_size =
      pCopyBufferInfo->regionCount * sizeof(*pCopyBufferInfo->pRegions);
   struct pvr_transfer_cmd *transfer_cmd;

   PVR_CHECK_COMMAND_BUFFER_BUILDING_STATE(cmd_buffer);

   transfer_cmd = vk_alloc(&cmd_buffer->vk.pool->alloc,
                           sizeof(*transfer_cmd) + regions_size,
                           8U,
                           VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!transfer_cmd) {
      cmd_buffer->state.status =
         vk_error(cmd_buffer, VK_ERROR_OUT_OF_HOST_MEMORY);

      return;
   }

   transfer_cmd->src = src;
   transfer_cmd->dst = dst;
   transfer_cmd->region_count = pCopyBufferInfo->regionCount;
   memcpy(transfer_cmd->regions, pCopyBufferInfo->pRegions, regions_size);

   pvr_cmd_buffer_add_transfer_cmd(cmd_buffer, transfer_cmd);
}

void pvr_CmdClearAttachments(VkCommandBuffer commandBuffer,
                             uint32_t attachmentCount,
                             const VkClearAttachment *pAttachments,
                             uint32_t rectCount,
                             const VkClearRect *pRects)
{
   assert(!"Unimplemented");
}

void pvr_CmdResolveImage2KHR(VkCommandBuffer commandBuffer,
                             const VkResolveImageInfo2 *pResolveImageInfo)
{
   assert(!"Unimplemented");
}
