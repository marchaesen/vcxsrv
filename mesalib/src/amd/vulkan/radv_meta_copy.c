/*
 * Copyright © 2016 Intel Corporation
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

#include "radv_meta.h"
#include "vk_format.h"

static VkFormat
vk_format_for_size(int bs)
{
   switch (bs) {
   case 1:
      return VK_FORMAT_R8_UINT;
   case 2:
      return VK_FORMAT_R8G8_UINT;
   case 4:
      return VK_FORMAT_R8G8B8A8_UINT;
   case 8:
      return VK_FORMAT_R16G16B16A16_UINT;
   case 12:
      return VK_FORMAT_R32G32B32_UINT;
   case 16:
      return VK_FORMAT_R32G32B32A32_UINT;
   default:
      unreachable("Invalid format block size");
   }
}

static struct radv_meta_blit2d_surf
blit_surf_for_image_level_layer(struct radv_image *image, VkImageLayout layout,
                                const VkImageSubresourceLayers *subres,
                                VkImageAspectFlags aspect_mask)
{
   VkFormat format = radv_get_aspect_format(image, aspect_mask);

   if (!radv_dcc_enabled(image, subres->mipLevel) && !(radv_image_is_tc_compat_htile(image)))
      format = vk_format_for_size(vk_format_get_blocksize(format));

   format = vk_format_no_srgb(format);

   return (struct radv_meta_blit2d_surf){
      .format = format,
      .bs = vk_format_get_blocksize(format),
      .level = subres->mipLevel,
      .layer = subres->baseArrayLayer,
      .image = image,
      .aspect_mask = aspect_mask,
      .current_layout = layout,
   };
}

bool
radv_image_is_renderable(struct radv_device *device, struct radv_image *image)
{
   if (image->vk.format == VK_FORMAT_R32G32B32_UINT ||
       image->vk.format == VK_FORMAT_R32G32B32_SINT ||
       image->vk.format == VK_FORMAT_R32G32B32_SFLOAT)
      return false;

   if (device->physical_device->rad_info.gfx_level >= GFX9 &&
       image->vk.image_type == VK_IMAGE_TYPE_3D &&
       vk_format_get_blocksizebits(image->vk.format) == 128 &&
       vk_format_is_compressed(image->vk.format))
      return false;
   return true;
}

static void
copy_buffer_to_image(struct radv_cmd_buffer *cmd_buffer, struct radv_buffer *buffer,
                     struct radv_image *image, VkImageLayout layout,
                     const VkBufferImageCopy2 *region)
{
   struct radv_meta_saved_state saved_state;
   bool cs;

   /* The Vulkan 1.0 spec says "dstImage must have a sample count equal to
    * VK_SAMPLE_COUNT_1_BIT."
    */
   assert(image->info.samples == 1);

   cs = cmd_buffer->qf == RADV_QUEUE_COMPUTE ||
        !radv_image_is_renderable(cmd_buffer->device, image);

   /* VK_EXT_conditional_rendering says that copy commands should not be
    * affected by conditional rendering.
    */
   radv_meta_save(&saved_state, cmd_buffer,
                  (cs ? RADV_META_SAVE_COMPUTE_PIPELINE : RADV_META_SAVE_GRAPHICS_PIPELINE) |
                     RADV_META_SAVE_CONSTANTS | RADV_META_SAVE_DESCRIPTORS |
                     RADV_META_SUSPEND_PREDICATING);

   /**
    * From the Vulkan 1.0.6 spec: 18.3 Copying Data Between Images
    *    extent is the size in texels of the source image to copy in width,
    *    height and depth. 1D images use only x and width. 2D images use x, y,
    *    width and height. 3D images use x, y, z, width, height and depth.
    *
    *
    * Also, convert the offsets and extent from units of texels to units of
    * blocks - which is the highest resolution accessible in this command.
    */
   const VkOffset3D img_offset_el = vk_image_offset_to_elements(&image->vk, region->imageOffset);

   /* Start creating blit rect */
   const VkExtent3D img_extent_el = vk_image_extent_to_elements(&image->vk, region->imageExtent);
   struct radv_meta_blit2d_rect rect = {
      .width = img_extent_el.width,
      .height = img_extent_el.height,
   };

   /* Create blit surfaces */
   struct radv_meta_blit2d_surf img_bsurf = blit_surf_for_image_level_layer(
      image, layout, &region->imageSubresource, region->imageSubresource.aspectMask);

   if (!radv_is_buffer_format_supported(img_bsurf.format, NULL)) {
      uint32_t queue_mask = radv_image_queue_family_mask(image, cmd_buffer->qf,
                                                         cmd_buffer->qf);
      bool compressed =
         radv_layout_dcc_compressed(cmd_buffer->device, image, region->imageSubresource.mipLevel,
                                    layout, queue_mask);
      if (compressed) {
         radv_decompress_dcc(cmd_buffer, image,
                             &(VkImageSubresourceRange){
                                .aspectMask = region->imageSubresource.aspectMask,
                                .baseMipLevel = region->imageSubresource.mipLevel,
                                .levelCount = 1,
                                .baseArrayLayer = region->imageSubresource.baseArrayLayer,
                                .layerCount = region->imageSubresource.layerCount,
                             });
         img_bsurf.disable_compression = true;
      }
      img_bsurf.format = vk_format_for_size(vk_format_get_blocksize(img_bsurf.format));
   }

   const struct vk_image_buffer_layout buf_layout = vk_image_buffer_copy_layout(&image->vk, region);
   struct radv_meta_blit2d_buffer buf_bsurf = {
      .bs = img_bsurf.bs,
      .format = img_bsurf.format,
      .buffer = buffer,
      .offset = region->bufferOffset,
      .pitch = buf_layout.row_stride_B / buf_layout.element_size_B,
   };

   if (image->vk.image_type == VK_IMAGE_TYPE_3D)
      img_bsurf.layer = img_offset_el.z;
   /* Loop through each 3D or array slice */
   unsigned num_slices_3d = img_extent_el.depth;
   unsigned num_slices_array = region->imageSubresource.layerCount;
   unsigned slice_3d = 0;
   unsigned slice_array = 0;
   while (slice_3d < num_slices_3d && slice_array < num_slices_array) {

      rect.dst_x = img_offset_el.x;
      rect.dst_y = img_offset_el.y;

      /* Perform Blit */
      if (cs) {
         radv_meta_buffer_to_image_cs(cmd_buffer, &buf_bsurf, &img_bsurf, 1, &rect);
      } else {
         radv_meta_blit2d(cmd_buffer, NULL, &buf_bsurf, &img_bsurf, 1, &rect);
      }

      /* Once we've done the blit, all of the actual information about
       * the image is embedded in the command buffer so we can just
       * increment the offset directly in the image effectively
       * re-binding it to different backing memory.
       */
      buf_bsurf.offset += buf_layout.image_stride_B;
      img_bsurf.layer++;
      if (image->vk.image_type == VK_IMAGE_TYPE_3D)
         slice_3d++;
      else
         slice_array++;
   }

   radv_meta_restore(&saved_state, cmd_buffer);
}

VKAPI_ATTR void VKAPI_CALL
radv_CmdCopyBufferToImage2(VkCommandBuffer commandBuffer,
                           const VkCopyBufferToImageInfo2 *pCopyBufferToImageInfo)
{
   RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   RADV_FROM_HANDLE(radv_buffer, src_buffer, pCopyBufferToImageInfo->srcBuffer);
   RADV_FROM_HANDLE(radv_image, dst_image, pCopyBufferToImageInfo->dstImage);

   for (unsigned r = 0; r < pCopyBufferToImageInfo->regionCount; r++) {
      copy_buffer_to_image(cmd_buffer, src_buffer, dst_image,
                           pCopyBufferToImageInfo->dstImageLayout,
                           &pCopyBufferToImageInfo->pRegions[r]);
   }

   if (cmd_buffer->device->physical_device->emulate_etc2 &&
       vk_format_description(dst_image->vk.format)->layout == UTIL_FORMAT_LAYOUT_ETC) {
      cmd_buffer->state.flush_bits |=
         RADV_CMD_FLAG_CS_PARTIAL_FLUSH | RADV_CMD_FLAG_PS_PARTIAL_FLUSH |
         radv_src_access_flush(cmd_buffer, VK_ACCESS_TRANSFER_WRITE_BIT, dst_image) |
         radv_dst_access_flush(
            cmd_buffer, VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT, dst_image);
      for (unsigned r = 0; r < pCopyBufferToImageInfo->regionCount; r++) {
         radv_meta_decode_etc(cmd_buffer, dst_image, pCopyBufferToImageInfo->dstImageLayout,
                              &pCopyBufferToImageInfo->pRegions[r].imageSubresource,
                              pCopyBufferToImageInfo->pRegions[r].imageOffset,
                              pCopyBufferToImageInfo->pRegions[r].imageExtent);
      }
   }
}

static void
copy_image_to_buffer(struct radv_cmd_buffer *cmd_buffer, struct radv_buffer *buffer,
                     struct radv_image *image, VkImageLayout layout,
                     const VkBufferImageCopy2 *region)
{
   if (cmd_buffer->qf == RADV_QUEUE_TRANSFER) {
      /* RADV_QUEUE_TRANSFER should only be used for the prime blit */
      assert(!region->imageOffset.x && !region->imageOffset.y && !region->imageOffset.z);
      assert(image->vk.image_type == VK_IMAGE_TYPE_2D);
      assert(image->info.width == region->imageExtent.width);
      assert(image->info.height == region->imageExtent.height);
      ASSERTED bool res = radv_sdma_copy_image(cmd_buffer, image, buffer, region);
      assert(res);
      radv_cs_add_buffer(cmd_buffer->device->ws, cmd_buffer->cs, image->bindings[0].bo);
      radv_cs_add_buffer(cmd_buffer->device->ws, cmd_buffer->cs, buffer->bo);
      return;
   }

   struct radv_meta_saved_state saved_state;

   /* VK_EXT_conditional_rendering says that copy commands should not be
    * affected by conditional rendering.
    */
   radv_meta_save(&saved_state, cmd_buffer,
                  RADV_META_SAVE_COMPUTE_PIPELINE | RADV_META_SAVE_CONSTANTS |
                     RADV_META_SAVE_DESCRIPTORS | RADV_META_SUSPEND_PREDICATING);

   /**
    * From the Vulkan 1.0.6 spec: 18.3 Copying Data Between Images
    *    extent is the size in texels of the source image to copy in width,
    *    height and depth. 1D images use only x and width. 2D images use x, y,
    *    width and height. 3D images use x, y, z, width, height and depth.
    *
    *
    * Also, convert the offsets and extent from units of texels to units of
    * blocks - which is the highest resolution accessible in this command.
    */
   const VkOffset3D img_offset_el = vk_image_offset_to_elements(&image->vk, region->imageOffset);
   const VkExtent3D bufferExtent = {
      .width = region->bufferRowLength ? region->bufferRowLength : region->imageExtent.width,
      .height = region->bufferImageHeight ? region->bufferImageHeight : region->imageExtent.height,
   };
   const VkExtent3D buf_extent_el = vk_image_extent_to_elements(&image->vk, bufferExtent);

   /* Start creating blit rect */
   const VkExtent3D img_extent_el = vk_image_extent_to_elements(&image->vk, region->imageExtent);
   struct radv_meta_blit2d_rect rect = {
      .width = img_extent_el.width,
      .height = img_extent_el.height,
   };

   /* Create blit surfaces */
   struct radv_meta_blit2d_surf img_info = blit_surf_for_image_level_layer(
      image, layout, &region->imageSubresource, region->imageSubresource.aspectMask);

   if (!radv_is_buffer_format_supported(img_info.format, NULL)) {
      uint32_t queue_mask = radv_image_queue_family_mask(image, cmd_buffer->qf,
                                                         cmd_buffer->qf);
      bool compressed =
         radv_layout_dcc_compressed(cmd_buffer->device, image, region->imageSubresource.mipLevel,
                                    layout, queue_mask);
      if (compressed) {
         radv_decompress_dcc(cmd_buffer, image,
                             &(VkImageSubresourceRange){
                                .aspectMask = region->imageSubresource.aspectMask,
                                .baseMipLevel = region->imageSubresource.mipLevel,
                                .levelCount = 1,
                                .baseArrayLayer = region->imageSubresource.baseArrayLayer,
                                .layerCount = region->imageSubresource.layerCount,
                             });
         img_info.disable_compression = true;
      }
      img_info.format = vk_format_for_size(vk_format_get_blocksize(img_info.format));
   }

   struct radv_meta_blit2d_buffer buf_info = {
      .bs = img_info.bs,
      .format = img_info.format,
      .buffer = buffer,
      .offset = region->bufferOffset,
      .pitch = buf_extent_el.width,
   };

   if (image->vk.image_type == VK_IMAGE_TYPE_3D)
      img_info.layer = img_offset_el.z;
   /* Loop through each 3D or array slice */
   unsigned num_slices_3d = img_extent_el.depth;
   unsigned num_slices_array = region->imageSubresource.layerCount;
   unsigned slice_3d = 0;
   unsigned slice_array = 0;
   while (slice_3d < num_slices_3d && slice_array < num_slices_array) {

      rect.src_x = img_offset_el.x;
      rect.src_y = img_offset_el.y;

      /* Perform Blit */
      radv_meta_image_to_buffer(cmd_buffer, &img_info, &buf_info, 1, &rect);

      buf_info.offset += buf_extent_el.width * buf_extent_el.height * buf_info.bs;
      img_info.layer++;
      if (image->vk.image_type == VK_IMAGE_TYPE_3D)
         slice_3d++;
      else
         slice_array++;
   }

   radv_meta_restore(&saved_state, cmd_buffer);
}

VKAPI_ATTR void VKAPI_CALL
radv_CmdCopyImageToBuffer2(VkCommandBuffer commandBuffer,
                           const VkCopyImageToBufferInfo2 *pCopyImageToBufferInfo)
{
   RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   RADV_FROM_HANDLE(radv_image, src_image, pCopyImageToBufferInfo->srcImage);
   RADV_FROM_HANDLE(radv_buffer, dst_buffer, pCopyImageToBufferInfo->dstBuffer);

   for (unsigned r = 0; r < pCopyImageToBufferInfo->regionCount; r++) {
      copy_image_to_buffer(cmd_buffer, dst_buffer, src_image,
                           pCopyImageToBufferInfo->srcImageLayout,
                           &pCopyImageToBufferInfo->pRegions[r]);
   }
}

static void
copy_image(struct radv_cmd_buffer *cmd_buffer, struct radv_image *src_image,
           VkImageLayout src_image_layout, struct radv_image *dst_image,
           VkImageLayout dst_image_layout, const VkImageCopy2 *region)
{
   struct radv_meta_saved_state saved_state;
   bool cs;

   /* From the Vulkan 1.0 spec:
    *
    *    vkCmdCopyImage can be used to copy image data between multisample
    *    images, but both images must have the same number of samples.
    */
   assert(src_image->info.samples == dst_image->info.samples);

   cs = cmd_buffer->qf == RADV_QUEUE_COMPUTE ||
        !radv_image_is_renderable(cmd_buffer->device, dst_image);

   /* VK_EXT_conditional_rendering says that copy commands should not be
    * affected by conditional rendering.
    */
   radv_meta_save(&saved_state, cmd_buffer,
                  (cs ? RADV_META_SAVE_COMPUTE_PIPELINE : RADV_META_SAVE_GRAPHICS_PIPELINE) |
                     RADV_META_SAVE_CONSTANTS | RADV_META_SAVE_DESCRIPTORS |
                     RADV_META_SUSPEND_PREDICATING);

   if (cs) {
      /* For partial copies, HTILE should be decompressed before copying because the metadata is
       * re-initialized to the uncompressed state after.
       */
      uint32_t queue_mask = radv_image_queue_family_mask(dst_image, cmd_buffer->qf,
                                                         cmd_buffer->qf);

      if (radv_layout_is_htile_compressed(cmd_buffer->device, dst_image, dst_image_layout,
                                          queue_mask) &&
          (region->dstOffset.x || region->dstOffset.y || region->dstOffset.z ||
           region->extent.width != dst_image->info.width ||
           region->extent.height != dst_image->info.height ||
           region->extent.depth != dst_image->info.depth)) {
         u_foreach_bit(i, region->dstSubresource.aspectMask) {
            unsigned aspect_mask = 1u << i;
            radv_expand_depth_stencil(cmd_buffer, dst_image,
                                      &(VkImageSubresourceRange){
                                         .aspectMask = aspect_mask,
                                         .baseMipLevel = region->dstSubresource.mipLevel,
                                         .levelCount = 1,
                                         .baseArrayLayer = region->dstSubresource.baseArrayLayer,
                                         .layerCount = region->dstSubresource.layerCount,
                                      }, NULL);
         }
      }
   }

   VkImageAspectFlags src_aspects[3] = { region->srcSubresource.aspectMask };
   VkImageAspectFlags dst_aspects[3] = { region->dstSubresource.aspectMask };
   unsigned aspect_count = 1;

   if (region->srcSubresource.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT &&
       src_image->plane_count > 1) {
      static const VkImageAspectFlags all_planes[3] = {
         VK_IMAGE_ASPECT_PLANE_0_BIT,
         VK_IMAGE_ASPECT_PLANE_1_BIT,
         VK_IMAGE_ASPECT_PLANE_2_BIT
      };

      aspect_count = src_image->plane_count;
      for (unsigned i = 0; i < aspect_count; i++) {
         src_aspects[i] = all_planes[i];
         dst_aspects[i] = all_planes[i];
      }
   }

   for (unsigned a = 0; a < aspect_count; ++a) {
      /* Create blit surfaces */
      struct radv_meta_blit2d_surf b_src = blit_surf_for_image_level_layer(
         src_image, src_image_layout, &region->srcSubresource, src_aspects[a]);

      struct radv_meta_blit2d_surf b_dst = blit_surf_for_image_level_layer(
         dst_image, dst_image_layout, &region->dstSubresource, dst_aspects[a]);

      uint32_t dst_queue_mask = radv_image_queue_family_mask(
         dst_image, cmd_buffer->qf, cmd_buffer->qf);
      bool dst_compressed = radv_layout_dcc_compressed(cmd_buffer->device, dst_image,
                                                       region->dstSubresource.mipLevel,
                                                       dst_image_layout, dst_queue_mask);
      uint32_t src_queue_mask = radv_image_queue_family_mask(
         src_image, cmd_buffer->qf, cmd_buffer->qf);
      bool src_compressed = radv_layout_dcc_compressed(cmd_buffer->device, src_image,
                                                       region->srcSubresource.mipLevel,
                                                       src_image_layout, src_queue_mask);
      bool need_dcc_sign_reinterpret = false;

      if (!src_compressed ||
          (radv_dcc_formats_compatible(cmd_buffer->device->physical_device->rad_info.gfx_level,
                                       b_src.format, b_dst.format, &need_dcc_sign_reinterpret) &&
           !need_dcc_sign_reinterpret)) {
         b_src.format = b_dst.format;
      } else if (!dst_compressed) {
         b_dst.format = b_src.format;
      } else {
         radv_decompress_dcc(cmd_buffer, dst_image,
                             &(VkImageSubresourceRange){
                                .aspectMask = dst_aspects[a],
                                .baseMipLevel = region->dstSubresource.mipLevel,
                                .levelCount = 1,
                                .baseArrayLayer = region->dstSubresource.baseArrayLayer,
                                .layerCount = region->dstSubresource.layerCount,
                             });
         b_dst.format = b_src.format;
         b_dst.disable_compression = true;
      }

      /**
       * From the Vulkan 1.0.6 spec: 18.4 Copying Data Between Buffers and Images
       *    imageExtent is the size in texels of the image to copy in width, height
       *    and depth. 1D images use only x and width. 2D images use x, y, width
       *    and height. 3D images use x, y, z, width, height and depth.
       *
       * Also, convert the offsets and extent from units of texels to units of
       * blocks - which is the highest resolution accessible in this command.
       */
      const VkOffset3D dst_offset_el =
         vk_image_offset_to_elements(&dst_image->vk, region->dstOffset);
      const VkOffset3D src_offset_el =
         vk_image_offset_to_elements(&src_image->vk, region->srcOffset);

      /*
       * From Vulkan 1.0.68, "Copying Data Between Images":
       *    "When copying between compressed and uncompressed formats
       *     the extent members represent the texel dimensions of the
       *     source image and not the destination."
       * However, we must use the destination image type to avoid
       * clamping depth when copying multiple layers of a 2D image to
       * a 3D image.
       */
      const VkExtent3D img_extent_el = vk_image_extent_to_elements(&src_image->vk, region->extent);

      /* Start creating blit rect */
      struct radv_meta_blit2d_rect rect = {
         .width = img_extent_el.width,
         .height = img_extent_el.height,
      };

      if (src_image->vk.image_type == VK_IMAGE_TYPE_3D)
         b_src.layer = src_offset_el.z;

      if (dst_image->vk.image_type == VK_IMAGE_TYPE_3D)
         b_dst.layer = dst_offset_el.z;

      /* Loop through each 3D or array slice */
      unsigned num_slices_3d = img_extent_el.depth;
      unsigned num_slices_array = region->dstSubresource.layerCount;
      unsigned slice_3d = 0;
      unsigned slice_array = 0;
      while (slice_3d < num_slices_3d && slice_array < num_slices_array) {

         /* Finish creating blit rect */
         rect.dst_x = dst_offset_el.x;
         rect.dst_y = dst_offset_el.y;
         rect.src_x = src_offset_el.x;
         rect.src_y = src_offset_el.y;

         /* Perform Blit */
         if (cs) {
            radv_meta_image_to_image_cs(cmd_buffer, &b_src, &b_dst, 1, &rect);
         } else {
            if (radv_can_use_fmask_copy(cmd_buffer, b_src.image, b_dst.image, 1, &rect)) {
               radv_fmask_copy(cmd_buffer, &b_src, &b_dst);
            } else {
               radv_meta_blit2d(cmd_buffer, &b_src, NULL, &b_dst, 1, &rect);
            }
         }

         b_src.layer++;
         b_dst.layer++;
         if (dst_image->vk.image_type == VK_IMAGE_TYPE_3D)
            slice_3d++;
         else
            slice_array++;
      }
   }

   if (cs) {
      /* Fixup HTILE after a copy on compute. */
      uint32_t queue_mask = radv_image_queue_family_mask(dst_image, cmd_buffer->qf,
                                                         cmd_buffer->qf);

      if (radv_layout_is_htile_compressed(cmd_buffer->device, dst_image, dst_image_layout,
                                          queue_mask)) {

         cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_CS_PARTIAL_FLUSH | RADV_CMD_FLAG_INV_VCACHE;

         VkImageSubresourceRange range = {
            .aspectMask = region->dstSubresource.aspectMask,
            .baseMipLevel = region->dstSubresource.mipLevel,
            .levelCount = 1,
            .baseArrayLayer = region->dstSubresource.baseArrayLayer,
            .layerCount = region->dstSubresource.layerCount,
         };

         uint32_t htile_value = radv_get_htile_initial_value(cmd_buffer->device, dst_image);

         cmd_buffer->state.flush_bits |= radv_clear_htile(cmd_buffer, dst_image, &range, htile_value);
      }
   }

   radv_meta_restore(&saved_state, cmd_buffer);
}

VKAPI_ATTR void VKAPI_CALL
radv_CmdCopyImage2(VkCommandBuffer commandBuffer, const VkCopyImageInfo2 *pCopyImageInfo)
{
   RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   RADV_FROM_HANDLE(radv_image, src_image, pCopyImageInfo->srcImage);
   RADV_FROM_HANDLE(radv_image, dst_image, pCopyImageInfo->dstImage);

   for (unsigned r = 0; r < pCopyImageInfo->regionCount; r++) {
      copy_image(cmd_buffer, src_image, pCopyImageInfo->srcImageLayout, dst_image,
                 pCopyImageInfo->dstImageLayout, &pCopyImageInfo->pRegions[r]);
   }

   if (cmd_buffer->device->physical_device->emulate_etc2 &&
       vk_format_description(dst_image->vk.format)->layout == UTIL_FORMAT_LAYOUT_ETC) {
      cmd_buffer->state.flush_bits |=
         RADV_CMD_FLAG_CS_PARTIAL_FLUSH | RADV_CMD_FLAG_PS_PARTIAL_FLUSH |
         radv_src_access_flush(cmd_buffer, VK_ACCESS_TRANSFER_WRITE_BIT, dst_image) |
         radv_dst_access_flush(
            cmd_buffer, VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT, dst_image);
      for (unsigned r = 0; r < pCopyImageInfo->regionCount; r++) {
         radv_meta_decode_etc(cmd_buffer, dst_image, pCopyImageInfo->dstImageLayout,
                              &pCopyImageInfo->pRegions[r].dstSubresource,
                              pCopyImageInfo->pRegions[r].dstOffset,
                              pCopyImageInfo->pRegions[r].extent);
      }
   }
}
