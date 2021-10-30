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

#include "vk_common_entrypoints.h"
#include "vk_device.h"
#include "vk_util.h"

VKAPI_ATTR void VKAPI_CALL
vk_common_CmdCopyBuffer(VkCommandBuffer commandBuffer,
                        VkBuffer srcBuffer,
                        VkBuffer dstBuffer,
                        uint32_t regionCount,
                        const VkBufferCopy *pRegions)
{
   /* We don't have a vk_command_buffer object but we can assume, since we're
    * using common dispatch, that it's a vk_object of some sort.
    */
   struct vk_object_base *disp = (struct vk_object_base *)commandBuffer;

   STACK_ARRAY(VkBufferCopy2KHR, region2s, regionCount);

   for (uint32_t r = 0; r < regionCount; r++) {
      region2s[r] = (VkBufferCopy2KHR) {
         .sType      = VK_STRUCTURE_TYPE_BUFFER_COPY_2_KHR,
         .srcOffset  = pRegions[r].srcOffset,
         .dstOffset  = pRegions[r].dstOffset,
         .size       = pRegions[r].size,
      };
   }

   VkCopyBufferInfo2KHR info = {
      .sType         = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2_KHR,
      .srcBuffer     = srcBuffer,
      .dstBuffer     = dstBuffer,
      .regionCount   = regionCount,
      .pRegions      = region2s,
   };

   disp->device->dispatch_table.CmdCopyBuffer2KHR(commandBuffer, &info);

   STACK_ARRAY_FINISH(region2s);
}

VKAPI_ATTR void VKAPI_CALL
vk_common_CmdCopyImage(VkCommandBuffer commandBuffer,
                       VkImage srcImage,
                       VkImageLayout srcImageLayout,
                       VkImage dstImage,
                       VkImageLayout dstImageLayout,
                       uint32_t regionCount,
                       const VkImageCopy *pRegions)
{
   /* We don't have a vk_command_buffer object but we can assume, since we're
    * using common dispatch, that it's a vk_object of some sort.
    */
   struct vk_object_base *disp = (struct vk_object_base *)commandBuffer;

   STACK_ARRAY(VkImageCopy2KHR, region2s, regionCount);

   for (uint32_t r = 0; r < regionCount; r++) {
      region2s[r] = (VkImageCopy2KHR) {
         .sType            = VK_STRUCTURE_TYPE_IMAGE_COPY_2_KHR,
         .srcSubresource   = pRegions[r].srcSubresource,
         .srcOffset        = pRegions[r].srcOffset,
         .dstSubresource   = pRegions[r].dstSubresource,
         .dstOffset        = pRegions[r].dstOffset,
         .extent           = pRegions[r].extent,
      };
   }

   VkCopyImageInfo2KHR info = {
      .sType            = VK_STRUCTURE_TYPE_COPY_IMAGE_INFO_2_KHR,
      .srcImage         = srcImage,
      .srcImageLayout   = srcImageLayout,
      .dstImage         = dstImage,
      .dstImageLayout   = dstImageLayout,
      .regionCount      = regionCount,
      .pRegions         = region2s,
   };

   disp->device->dispatch_table.CmdCopyImage2KHR(commandBuffer, &info);

   STACK_ARRAY_FINISH(region2s);
}

VKAPI_ATTR void VKAPI_CALL
vk_common_CmdCopyBufferToImage(VkCommandBuffer commandBuffer,
                               VkBuffer srcBuffer,
                               VkImage dstImage,
                               VkImageLayout dstImageLayout,
                               uint32_t regionCount,
                               const VkBufferImageCopy *pRegions)
{
   /* We don't have a vk_command_buffer object but we can assume, since we're
    * using common dispatch, that it's a vk_object of some sort.
    */
   struct vk_object_base *disp = (struct vk_object_base *)commandBuffer;

   STACK_ARRAY(VkBufferImageCopy2KHR, region2s, regionCount);

   for (uint32_t r = 0; r < regionCount; r++) {
      region2s[r] = (VkBufferImageCopy2KHR) {
         .sType               = VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2_KHR,
         .bufferOffset        = pRegions[r].bufferOffset,
         .bufferRowLength     = pRegions[r].bufferRowLength,
         .bufferImageHeight   = pRegions[r].bufferImageHeight,
         .imageSubresource    = pRegions[r].imageSubresource,
         .imageOffset         = pRegions[r].imageOffset,
         .imageExtent         = pRegions[r].imageExtent,
      };
   }

   VkCopyBufferToImageInfo2KHR info = {
      .sType            = VK_STRUCTURE_TYPE_COPY_BUFFER_TO_IMAGE_INFO_2_KHR,
      .srcBuffer        = srcBuffer,
      .dstImage         = dstImage,
      .dstImageLayout   = dstImageLayout,
      .regionCount      = regionCount,
      .pRegions         = region2s,
   };

   disp->device->dispatch_table.CmdCopyBufferToImage2KHR(commandBuffer, &info);

   STACK_ARRAY_FINISH(region2s);
}

VKAPI_ATTR void VKAPI_CALL
vk_common_CmdCopyImageToBuffer(VkCommandBuffer commandBuffer,
                               VkImage srcImage,
                               VkImageLayout srcImageLayout,
                               VkBuffer dstBuffer,
                               uint32_t regionCount,
                               const VkBufferImageCopy *pRegions)
{
   /* We don't have a vk_command_buffer object but we can assume, since we're
    * using common dispatch, that it's a vk_object of some sort.
    */
   struct vk_object_base *disp = (struct vk_object_base *)commandBuffer;

   STACK_ARRAY(VkBufferImageCopy2KHR, region2s, regionCount);

   for (uint32_t r = 0; r < regionCount; r++) {
      region2s[r] = (VkBufferImageCopy2KHR) {
         .sType               = VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2_KHR,
         .bufferOffset        = pRegions[r].bufferOffset,
         .bufferRowLength     = pRegions[r].bufferRowLength,
         .bufferImageHeight   = pRegions[r].bufferImageHeight,
         .imageSubresource    = pRegions[r].imageSubresource,
         .imageOffset         = pRegions[r].imageOffset,
         .imageExtent         = pRegions[r].imageExtent,
      };
   }

   VkCopyImageToBufferInfo2KHR info = {
      .sType            = VK_STRUCTURE_TYPE_COPY_IMAGE_TO_BUFFER_INFO_2_KHR,
      .srcImage         = srcImage,
      .srcImageLayout   = srcImageLayout,
      .dstBuffer        = dstBuffer,
      .regionCount      = regionCount,
      .pRegions         = region2s,
   };

   disp->device->dispatch_table.CmdCopyImageToBuffer2KHR(commandBuffer, &info);

   STACK_ARRAY_FINISH(region2s);
}

VKAPI_ATTR void VKAPI_CALL
vk_common_CmdBlitImage(VkCommandBuffer commandBuffer,
                       VkImage srcImage,
                       VkImageLayout srcImageLayout,
                       VkImage dstImage,
                       VkImageLayout dstImageLayout,
                       uint32_t regionCount,
                       const VkImageBlit *pRegions,
                       VkFilter filter)
{
   /* We don't have a vk_command_buffer object but we can assume, since we're
    * using common dispatch, that it's a vk_object of some sort.
    */
   struct vk_object_base *disp = (struct vk_object_base *)commandBuffer;

   STACK_ARRAY(VkImageBlit2KHR, region2s, regionCount);

   for (uint32_t r = 0; r < regionCount; r++) {
      region2s[r] = (VkImageBlit2KHR) {
         .sType            = VK_STRUCTURE_TYPE_IMAGE_BLIT_2_KHR,
         .srcSubresource   = pRegions[r].srcSubresource,
         .srcOffsets       = {
            pRegions[r].srcOffsets[0],
            pRegions[r].srcOffsets[1],
         },
         .dstSubresource   = pRegions[r].dstSubresource,
         .dstOffsets       = {
            pRegions[r].dstOffsets[0],
            pRegions[r].dstOffsets[1],
         },
      };
   }

   VkBlitImageInfo2KHR info = {
      .sType            = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2_KHR,
      .srcImage         = srcImage,
      .srcImageLayout   = srcImageLayout,
      .dstImage         = dstImage,
      .dstImageLayout   = dstImageLayout,
      .regionCount      = regionCount,
      .pRegions         = region2s,
      .filter           = filter,
   };

   disp->device->dispatch_table.CmdBlitImage2KHR(commandBuffer, &info);

   STACK_ARRAY_FINISH(region2s);
}

VKAPI_ATTR void VKAPI_CALL
vk_common_CmdResolveImage(VkCommandBuffer commandBuffer,
                          VkImage srcImage,
                          VkImageLayout srcImageLayout,
                          VkImage dstImage,
                          VkImageLayout dstImageLayout,
                          uint32_t regionCount,
                          const VkImageResolve *pRegions)
{
   /* We don't have a vk_command_buffer object but we can assume, since we're
    * using common dispatch, that it's a vk_object of some sort.
    */
   struct vk_object_base *disp = (struct vk_object_base *)commandBuffer;

   STACK_ARRAY(VkImageResolve2KHR, region2s, regionCount);

   for (uint32_t r = 0; r < regionCount; r++) {
      region2s[r] = (VkImageResolve2KHR) {
         .sType            = VK_STRUCTURE_TYPE_IMAGE_RESOLVE_2_KHR,
         .srcSubresource   = pRegions[r].srcSubresource,
         .srcOffset        = pRegions[r].srcOffset,
         .dstSubresource   = pRegions[r].dstSubresource,
         .dstOffset        = pRegions[r].dstOffset,
         .extent           = pRegions[r].extent,
      };
   }

   VkResolveImageInfo2KHR info = {
      .sType            = VK_STRUCTURE_TYPE_RESOLVE_IMAGE_INFO_2_KHR,
      .srcImage         = srcImage,
      .srcImageLayout   = srcImageLayout,
      .dstImage         = dstImage,
      .dstImageLayout   = dstImageLayout,
      .regionCount      = regionCount,
      .pRegions         = region2s,
   };

   disp->device->dispatch_table.CmdResolveImage2KHR(commandBuffer, &info);

   STACK_ARRAY_FINISH(region2s);
}
