/*
 * Copyright © 2021 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "panvk_cmd_meta.h"
#include "panvk_entrypoints.h"

static bool
copy_to_image_use_gfx_pipeline(struct panvk_device *dev,
                               struct panvk_image *dst_img)
{
   struct panvk_instance *instance =
      to_panvk_instance(dev->vk.physical->instance);

   if (instance->debug_flags & PANVK_DEBUG_COPY_GFX)
      return true;

   /* Writes to AFBC images must go through the graphics pipeline. */
   if (drm_is_afbc(dst_img->vk.drm_format_mod))
      return true;

   return false;
}

void
panvk_per_arch(cmd_meta_compute_start)(
   struct panvk_cmd_buffer *cmdbuf,
   struct panvk_cmd_meta_compute_save_ctx *save_ctx)
{
   const struct panvk_descriptor_set *set0 =
      cmdbuf->state.compute.desc_state.sets[0];
   struct panvk_descriptor_set *push_set0 =
      cmdbuf->state.compute.desc_state.push_sets[0];

   save_ctx->set0 = set0;
   if (push_set0 && push_set0 == set0) {
      save_ctx->push_set0.desc_count = push_set0->desc_count;
      save_ctx->push_set0.descs_dev_addr = push_set0->descs.dev;
      memcpy(save_ctx->push_set0.desc_storage, push_set0->descs.host,
             push_set0->desc_count * PANVK_DESCRIPTOR_SIZE);
   }

   save_ctx->push_constants = cmdbuf->state.push_constants;
   save_ctx->cs.shader = cmdbuf->state.compute.shader;
   save_ctx->cs.desc = cmdbuf->state.compute.cs.desc;
}

void
panvk_per_arch(cmd_meta_compute_end)(
   struct panvk_cmd_buffer *cmdbuf,
   const struct panvk_cmd_meta_compute_save_ctx *save_ctx)
{
   struct panvk_descriptor_set *push_set0 =
      cmdbuf->state.compute.desc_state.push_sets[0];

   cmdbuf->state.compute.desc_state.sets[0] = save_ctx->set0;
   if (save_ctx->push_set0.desc_count) {
      memcpy(push_set0->descs.host, save_ctx->push_set0.desc_storage,
             save_ctx->push_set0.desc_count * PANVK_DESCRIPTOR_SIZE);
      push_set0->descs.dev = save_ctx->push_set0.descs_dev_addr;
      push_set0->desc_count = save_ctx->push_set0.desc_count;
   }

   cmdbuf->state.push_constants = save_ctx->push_constants;
   compute_state_set_dirty(cmdbuf, PUSH_UNIFORMS);

   cmdbuf->state.compute.shader = save_ctx->cs.shader;
   cmdbuf->state.compute.cs.desc = save_ctx->cs.desc;
   compute_state_set_dirty(cmdbuf, CS);
   compute_state_set_dirty(cmdbuf, DESC_STATE);
}

void
panvk_per_arch(cmd_meta_gfx_start)(
   struct panvk_cmd_buffer *cmdbuf,
   struct panvk_cmd_meta_graphics_save_ctx *save_ctx)
{
   const struct panvk_descriptor_set *set0 =
      cmdbuf->state.gfx.desc_state.sets[0];
   struct panvk_descriptor_set *push_set0 =
      cmdbuf->state.gfx.desc_state.push_sets[0];

   save_ctx->set0 = set0;
   if (push_set0 && push_set0 == set0) {
      save_ctx->push_set0.desc_count = push_set0->desc_count;
      save_ctx->push_set0.descs_dev_addr = push_set0->descs.dev;
      memcpy(save_ctx->push_set0.desc_storage, push_set0->descs.host,
             push_set0->desc_count * PANVK_DESCRIPTOR_SIZE);
   }

   save_ctx->push_constants = cmdbuf->state.push_constants;
   save_ctx->fs.shader = cmdbuf->state.gfx.fs.shader;
   save_ctx->fs.desc = cmdbuf->state.gfx.fs.desc;
   save_ctx->vs.shader = cmdbuf->state.gfx.vs.shader;
   save_ctx->vs.desc = cmdbuf->state.gfx.vs.desc;
   save_ctx->vb0 = cmdbuf->state.gfx.vb.bufs[0];

   save_ctx->dyn_state.all = cmdbuf->vk.dynamic_graphics_state;
   save_ctx->dyn_state.vi = cmdbuf->state.gfx.dynamic.vi;
   save_ctx->dyn_state.sl = cmdbuf->state.gfx.dynamic.sl;
   save_ctx->occlusion_query = cmdbuf->state.gfx.occlusion_query;

   /* Ensure occlusion queries are disabled */
   cmdbuf->state.gfx.occlusion_query.ptr = 0;
   cmdbuf->state.gfx.occlusion_query.mode = MALI_OCCLUSION_MODE_DISABLED;
   gfx_state_set_dirty(cmdbuf, OQ);
}

void
panvk_per_arch(cmd_meta_gfx_end)(
   struct panvk_cmd_buffer *cmdbuf,
   const struct panvk_cmd_meta_graphics_save_ctx *save_ctx)
{
   struct panvk_descriptor_set *push_set0 =
      cmdbuf->state.gfx.desc_state.push_sets[0];

   cmdbuf->state.gfx.desc_state.sets[0] = save_ctx->set0;
   if (save_ctx->push_set0.desc_count) {
      memcpy(push_set0->descs.host, save_ctx->push_set0.desc_storage,
             save_ctx->push_set0.desc_count * PANVK_DESCRIPTOR_SIZE);
      push_set0->descs.dev = save_ctx->push_set0.descs_dev_addr;
      push_set0->desc_count = save_ctx->push_set0.desc_count;
   }

   cmdbuf->state.push_constants = save_ctx->push_constants;
   gfx_state_set_dirty(cmdbuf, VS_PUSH_UNIFORMS);
   gfx_state_set_dirty(cmdbuf, FS_PUSH_UNIFORMS);

   cmdbuf->state.gfx.fs.shader = save_ctx->fs.shader;
   cmdbuf->state.gfx.fs.desc = save_ctx->fs.desc;
   cmdbuf->state.gfx.vs.shader = save_ctx->vs.shader;
   cmdbuf->state.gfx.vs.desc = save_ctx->vs.desc;
   cmdbuf->state.gfx.vb.bufs[0] = save_ctx->vb0;

#if PAN_ARCH <= 7
   cmdbuf->state.gfx.vs.attribs = 0;
   cmdbuf->state.gfx.vs.attrib_bufs = 0;
   cmdbuf->state.gfx.fs.rsd = 0;
#else
   cmdbuf->state.gfx.fs.desc.res_table = 0;
   cmdbuf->state.gfx.vs.desc.res_table = 0;
#endif

   cmdbuf->vk.dynamic_graphics_state = save_ctx->dyn_state.all;
   cmdbuf->state.gfx.dynamic.vi = save_ctx->dyn_state.vi;
   cmdbuf->state.gfx.dynamic.sl = save_ctx->dyn_state.sl;
   cmdbuf->state.gfx.occlusion_query = save_ctx->occlusion_query;
   memcpy(cmdbuf->vk.dynamic_graphics_state.dirty,
          cmdbuf->vk.dynamic_graphics_state.set,
          sizeof(cmdbuf->vk.dynamic_graphics_state.set));
   gfx_state_set_dirty(cmdbuf, VS);
   gfx_state_set_dirty(cmdbuf, FS);
   gfx_state_set_dirty(cmdbuf, VB);
   gfx_state_set_dirty(cmdbuf, OQ);
   gfx_state_set_dirty(cmdbuf, DESC_STATE);
   gfx_state_set_dirty(cmdbuf, RENDER_STATE);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdBlitImage2)(VkCommandBuffer commandBuffer,
                              const VkBlitImageInfo2 *pBlitImageInfo)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   struct panvk_cmd_meta_graphics_save_ctx save = {0};

   panvk_per_arch(cmd_meta_gfx_start)(cmdbuf, &save);
   vk_meta_blit_image2(&cmdbuf->vk, &dev->meta, pBlitImageInfo);
   panvk_per_arch(cmd_meta_gfx_end)(cmdbuf, &save);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdResolveImage2)(VkCommandBuffer commandBuffer,
                                 const VkResolveImageInfo2 *pResolveImageInfo)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   struct panvk_cmd_meta_graphics_save_ctx save = {0};

   panvk_per_arch(cmd_meta_gfx_start)(cmdbuf, &save);
   vk_meta_resolve_image2(&cmdbuf->vk, &dev->meta, pResolveImageInfo);
   panvk_per_arch(cmd_meta_gfx_end)(cmdbuf, &save);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdClearAttachments)(VkCommandBuffer commandBuffer,
                                    uint32_t attachmentCount,
                                    const VkClearAttachment *pAttachments,
                                    uint32_t rectCount,
                                    const VkClearRect *pRects)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   const struct pan_fb_info *fbinfo = &cmdbuf->state.gfx.render.fb.info;
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   struct panvk_cmd_meta_graphics_save_ctx save = {0};
   struct vk_meta_rendering_info render = {
      .view_mask = cmdbuf->state.gfx.render.view_mask,
      .samples = fbinfo->nr_samples,
      .color_attachment_count = fbinfo->rt_count,
      .depth_attachment_format = cmdbuf->state.gfx.render.z_attachment.fmt,
      .stencil_attachment_format = cmdbuf->state.gfx.render.s_attachment.fmt,
   };
   /* Multiview is not supported pre-v10 */
   assert(cmdbuf->state.gfx.render.view_mask == 0 || PAN_ARCH >= 10);

   for (uint32_t i = 0; i < render.color_attachment_count; i++) {
       render.color_attachment_formats[i] =
          cmdbuf->state.gfx.render.color_attachments.fmts[i];
       render.color_attachment_write_masks[i] =
          VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
   }

   panvk_per_arch(cmd_meta_gfx_start)(cmdbuf, &save);
   vk_meta_clear_attachments(&cmdbuf->vk, &dev->meta, &render, attachmentCount,
                             pAttachments, rectCount, pRects);
   panvk_per_arch(cmd_meta_gfx_end)(cmdbuf, &save);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdClearDepthStencilImage)(
   VkCommandBuffer commandBuffer, VkImage image, VkImageLayout imageLayout,
   const VkClearDepthStencilValue *pDepthStencil, uint32_t rangeCount,
   const VkImageSubresourceRange *pRanges)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   VK_FROM_HANDLE(panvk_image, img, image);
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   struct panvk_cmd_meta_graphics_save_ctx save = {0};

   panvk_per_arch(cmd_meta_gfx_start)(cmdbuf, &save);
   vk_meta_clear_depth_stencil_image(&cmdbuf->vk, &dev->meta, &img->vk,
                                     imageLayout, pDepthStencil, rangeCount,
                                     pRanges);
   panvk_per_arch(cmd_meta_gfx_end)(cmdbuf, &save);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdClearColorImage)(VkCommandBuffer commandBuffer, VkImage image,
                                   VkImageLayout imageLayout,
                                   const VkClearColorValue *pColor,
                                   uint32_t rangeCount,
                                   const VkImageSubresourceRange *pRanges)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   VK_FROM_HANDLE(panvk_image, img, image);
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   struct panvk_cmd_meta_graphics_save_ctx save = {0};

   panvk_per_arch(cmd_meta_gfx_start)(cmdbuf, &save);
   vk_meta_clear_color_image(&cmdbuf->vk, &dev->meta, &img->vk, imageLayout,
                             img->vk.format, pColor, rangeCount, pRanges);
   panvk_per_arch(cmd_meta_gfx_end)(cmdbuf, &save);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdCopyBuffer2)(VkCommandBuffer commandBuffer,
                               const VkCopyBufferInfo2 *pCopyBufferInfo)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   struct panvk_cmd_meta_compute_save_ctx save = {0};

   panvk_per_arch(cmd_meta_compute_start)(cmdbuf, &save);
   vk_meta_copy_buffer(&cmdbuf->vk, &dev->meta, pCopyBufferInfo);
   panvk_per_arch(cmd_meta_compute_end)(cmdbuf, &save);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdCopyBufferToImage2)(
   VkCommandBuffer commandBuffer,
   const VkCopyBufferToImageInfo2 *pCopyBufferToImageInfo)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   VK_FROM_HANDLE(panvk_image, img, pCopyBufferToImageInfo->dstImage);
   struct vk_meta_copy_image_properties img_props =
      panvk_meta_copy_get_image_properties(img);
   bool use_gfx_pipeline = copy_to_image_use_gfx_pipeline(dev, img);

   if (use_gfx_pipeline) {
      struct panvk_cmd_meta_graphics_save_ctx save = {0};

      panvk_per_arch(cmd_meta_gfx_start)(cmdbuf, &save);
      vk_meta_copy_buffer_to_image(&cmdbuf->vk, &dev->meta,
                                   pCopyBufferToImageInfo, &img_props,
                                   VK_PIPELINE_BIND_POINT_GRAPHICS);
      panvk_per_arch(cmd_meta_gfx_end)(cmdbuf, &save);
   } else {
      struct panvk_cmd_meta_compute_save_ctx save = {0};

      panvk_per_arch(cmd_meta_compute_start)(cmdbuf, &save);
      vk_meta_copy_buffer_to_image(&cmdbuf->vk, &dev->meta,
                                   pCopyBufferToImageInfo, &img_props,
                                   VK_PIPELINE_BIND_POINT_COMPUTE);
      panvk_per_arch(cmd_meta_compute_end)(cmdbuf, &save);
   }
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdCopyImageToBuffer2)(
   VkCommandBuffer commandBuffer,
   const VkCopyImageToBufferInfo2 *pCopyImageToBufferInfo)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   VK_FROM_HANDLE(panvk_image, img, pCopyImageToBufferInfo->srcImage);
   struct vk_meta_copy_image_properties img_props =
      panvk_meta_copy_get_image_properties(img);
   struct panvk_cmd_meta_compute_save_ctx save = {0};

   panvk_per_arch(cmd_meta_compute_start)(cmdbuf, &save);
   vk_meta_copy_image_to_buffer(&cmdbuf->vk, &dev->meta, pCopyImageToBufferInfo,
                                &img_props);
   panvk_per_arch(cmd_meta_compute_end)(cmdbuf, &save);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdFillBuffer)(VkCommandBuffer commandBuffer, VkBuffer dstBuffer,
                              VkDeviceSize dstOffset, VkDeviceSize fillSize,
                              uint32_t data)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   struct panvk_cmd_meta_compute_save_ctx save = {0};

   panvk_per_arch(cmd_meta_compute_start)(cmdbuf, &save);
   vk_meta_fill_buffer(&cmdbuf->vk, &dev->meta, dstBuffer, dstOffset, fillSize,
                       data);
   panvk_per_arch(cmd_meta_compute_end)(cmdbuf, &save);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdUpdateBuffer)(VkCommandBuffer commandBuffer,
                                VkBuffer dstBuffer, VkDeviceSize dstOffset,
                                VkDeviceSize dataSize, const void *pData)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   struct panvk_cmd_meta_compute_save_ctx save = {0};

   panvk_per_arch(cmd_meta_compute_start)(cmdbuf, &save);
   vk_meta_update_buffer(&cmdbuf->vk, &dev->meta, dstBuffer, dstOffset,
                         dataSize, pData);
   panvk_per_arch(cmd_meta_compute_end)(cmdbuf, &save);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdCopyImage2)(VkCommandBuffer commandBuffer,
                              const VkCopyImageInfo2 *pCopyImageInfo)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   struct panvk_device *dev = to_panvk_device(cmdbuf->vk.base.device);
   VK_FROM_HANDLE(panvk_image, src_img, pCopyImageInfo->srcImage);
   VK_FROM_HANDLE(panvk_image, dst_img, pCopyImageInfo->dstImage);
   struct vk_meta_copy_image_properties src_img_props =
      panvk_meta_copy_get_image_properties(src_img);
   struct vk_meta_copy_image_properties dst_img_props =
      panvk_meta_copy_get_image_properties(dst_img);
   bool use_gfx_pipeline = copy_to_image_use_gfx_pipeline(dev, dst_img);

   if (use_gfx_pipeline) {
      struct panvk_cmd_meta_graphics_save_ctx save = {0};

      panvk_per_arch(cmd_meta_gfx_start)(cmdbuf, &save);
      vk_meta_copy_image(&cmdbuf->vk, &dev->meta, pCopyImageInfo,
                         &src_img_props, &dst_img_props,
                         VK_PIPELINE_BIND_POINT_GRAPHICS);
      panvk_per_arch(cmd_meta_gfx_end)(cmdbuf, &save);
   } else {
      struct panvk_cmd_meta_compute_save_ctx save = {0};

      panvk_per_arch(cmd_meta_compute_start)(cmdbuf, &save);
      vk_meta_copy_image(&cmdbuf->vk, &dev->meta, pCopyImageInfo,
                         &src_img_props, &dst_img_props,
                         VK_PIPELINE_BIND_POINT_COMPUTE);
      panvk_per_arch(cmd_meta_compute_end)(cmdbuf, &save);
   }
}
