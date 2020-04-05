/*
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

#include "tu_private.h"
#include "tu_blit.h"
#include "tu_cs.h"

static void
clear_image(struct tu_cmd_buffer *cmdbuf,
            struct tu_image *image,
            uint32_t clear_value[4],
            const VkImageSubresourceRange *range)
{
   uint32_t level_count = tu_get_levelCount(image, range);
   uint32_t layer_count = tu_get_layerCount(image, range);

   if (image->type == VK_IMAGE_TYPE_3D) {
      assert(layer_count == 1);
      assert(range->baseArrayLayer == 0);
   }

   for (unsigned j = 0; j < level_count; j++) {
      if (image->type == VK_IMAGE_TYPE_3D)
         layer_count = u_minify(image->extent.depth, range->baseMipLevel + j);

      tu_blit(cmdbuf, &cmdbuf->cs, &(struct tu_blit) {
         .dst = tu_blit_surf_whole(image, range->baseMipLevel + j, range->baseArrayLayer),
         .layers = layer_count,
         .clear_value = {clear_value[0], clear_value[1], clear_value[2], clear_value[3]},
         .type = TU_BLIT_CLEAR,
      });
   }
}

void
tu_CmdClearColorImage(VkCommandBuffer commandBuffer,
                      VkImage image_h,
                      VkImageLayout imageLayout,
                      const VkClearColorValue *pColor,
                      uint32_t rangeCount,
                      const VkImageSubresourceRange *pRanges)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmdbuf, commandBuffer);
   TU_FROM_HANDLE(tu_image, image, image_h);
   uint32_t clear_value[4] = {};

   tu_2d_clear_color(pColor, image->vk_format, clear_value);

   tu_bo_list_add(&cmdbuf->bo_list, image->bo, MSM_SUBMIT_BO_WRITE);

   for (unsigned i = 0; i < rangeCount; i++)
      clear_image(cmdbuf, image, clear_value, pRanges + i);
}

void
tu_CmdClearDepthStencilImage(VkCommandBuffer commandBuffer,
                             VkImage image_h,
                             VkImageLayout imageLayout,
                             const VkClearDepthStencilValue *pDepthStencil,
                             uint32_t rangeCount,
                             const VkImageSubresourceRange *pRanges)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmdbuf, commandBuffer);
   TU_FROM_HANDLE(tu_image, image, image_h);
   uint32_t clear_value[4] = {};

   tu_2d_clear_zs(pDepthStencil, image->vk_format, clear_value);

   tu_bo_list_add(&cmdbuf->bo_list, image->bo, MSM_SUBMIT_BO_WRITE);

   for (unsigned i = 0; i < rangeCount; i++)
      clear_image(cmdbuf, image, clear_value, pRanges + i);
}

void
tu_clear_sysmem_attachment(struct tu_cmd_buffer *cmd,
                           struct tu_cs *cs,
                           uint32_t attachment,
                           const VkClearValue *value,
                           const VkClearRect *rect)
{
   if (!cmd->state.framebuffer) {
      tu_finishme("sysmem CmdClearAttachments in secondary command buffer");
      return;
   }

   const struct tu_image_view *iview =
      cmd->state.framebuffer->attachments[attachment].attachment;

   uint32_t clear_vals[4] = { 0 };
   if (iview->aspect_mask & (VK_IMAGE_ASPECT_DEPTH_BIT |
                             VK_IMAGE_ASPECT_STENCIL_BIT)) {
      tu_2d_clear_zs(&value->depthStencil, iview->vk_format,
                     clear_vals);
   } else {
      tu_2d_clear_color(&value->color, iview->vk_format,
                        clear_vals);
   }

   tu_blit(cmd, cs, &(struct tu_blit) {
      .dst = sysmem_attachment_surf(iview, rect->baseArrayLayer, &rect->rect),
      .layers = rect->layerCount,
      .clear_value = { clear_vals[0], clear_vals[1], clear_vals[2], clear_vals[3] },
      .type = TU_BLIT_CLEAR,
   });
}

void
tu_clear_gmem_attachment(struct tu_cmd_buffer *cmd,
                         struct tu_cs *cs,
                         uint32_t attachment,
                         uint8_t component_mask,
                         const VkClearValue *value)
{
   VkFormat fmt = cmd->state.pass->attachments[attachment].format;

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_BLIT_DST_INFO, 1);
   tu_cs_emit(cs, A6XX_RB_BLIT_DST_INFO_COLOR_FORMAT(tu6_format_gmem(fmt)));

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_BLIT_INFO, 1);
   tu_cs_emit(cs, A6XX_RB_BLIT_INFO_GMEM | A6XX_RB_BLIT_INFO_CLEAR_MASK(component_mask));

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_BLIT_BASE_GMEM, 1);
   tu_cs_emit(cs, cmd->state.pass->attachments[attachment].gmem_offset);

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_UNKNOWN_88D0, 1);
   tu_cs_emit(cs, 0);

   uint32_t clear_vals[4] = { 0 };
   tu_pack_clear_value(value, fmt, clear_vals);

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_BLIT_CLEAR_COLOR_DW0, 4);
   tu_cs_emit(cs, clear_vals[0]);
   tu_cs_emit(cs, clear_vals[1]);
   tu_cs_emit(cs, clear_vals[2]);
   tu_cs_emit(cs, clear_vals[3]);

   tu6_emit_event_write(cmd, cs, BLIT, false);
}

void
tu_CmdClearAttachments(VkCommandBuffer commandBuffer,
                       uint32_t attachmentCount,
                       const VkClearAttachment *pAttachments,
                       uint32_t rectCount,
                       const VkClearRect *pRects)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   const struct tu_subpass *subpass = cmd->state.subpass;
   struct tu_cs *cs = &cmd->draw_cs;

   tu_cond_exec_start(cs, CP_COND_EXEC_0_RENDER_MODE_GMEM);

   for (unsigned i = 0; i < rectCount; i++) {
      unsigned x1 = pRects[i].rect.offset.x;
      unsigned y1 = pRects[i].rect.offset.y;
      unsigned x2 = x1 + pRects[i].rect.extent.width - 1;
      unsigned y2 = y1 + pRects[i].rect.extent.height - 1;

      tu_cs_emit_pkt4(cs, REG_A6XX_RB_BLIT_SCISSOR_TL, 2);
      tu_cs_emit(cs, A6XX_RB_BLIT_SCISSOR_TL_X(x1) | A6XX_RB_BLIT_SCISSOR_TL_Y(y1));
      tu_cs_emit(cs, A6XX_RB_BLIT_SCISSOR_BR_X(x2) | A6XX_RB_BLIT_SCISSOR_BR_Y(y2));

      for (unsigned j = 0; j < attachmentCount; j++) {
         uint32_t a;
         unsigned clear_mask = 0;
         if (pAttachments[j].aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) {
            clear_mask = 0xf;
            a = subpass->color_attachments[pAttachments[j].colorAttachment].attachment;
         } else {
            a = subpass->depth_stencil_attachment.attachment;
            if (pAttachments[j].aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT)
               clear_mask |= 1;
            if (pAttachments[j].aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT)
               clear_mask |= 2;
         }

         if (a == VK_ATTACHMENT_UNUSED)
               continue;

         tu_clear_gmem_attachment(cmd, cs, a, clear_mask,
                                  &pAttachments[j].clearValue);

      }
   }

   tu_cond_exec_end(cs);

   tu_cond_exec_start(cs, CP_COND_EXEC_0_RENDER_MODE_SYSMEM);

   for (unsigned i = 0; i < rectCount; i++) {
      for (unsigned j = 0; j < attachmentCount; j++) {
         uint32_t a;
         unsigned clear_mask = 0;
         if (pAttachments[j].aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) {
            clear_mask = 0xf;
            a = subpass->color_attachments[pAttachments[j].colorAttachment].attachment;
         } else {
            a = subpass->depth_stencil_attachment.attachment;
            if (pAttachments[j].aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT)
               clear_mask |= 1;
            if (pAttachments[j].aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT)
               clear_mask |= 2;
            if (clear_mask != 3)
               tu_finishme("sysmem depth/stencil only clears");
         }

         if (a == VK_ATTACHMENT_UNUSED)
               continue;

         tu_clear_sysmem_attachment(cmd, cs, a,
                                    &pAttachments[j].clearValue,
                                    &pRects[i]);
      }
   }

   tu_cond_exec_end(cs);
}
