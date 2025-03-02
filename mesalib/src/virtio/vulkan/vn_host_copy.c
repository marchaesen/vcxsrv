/*
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "vn_common.h"

#include "venus-protocol/vn_protocol_driver_host_copy.h"
#include "vk_format.h"

#include "vn_device.h"

VkResult
vn_TransitionImageLayout(VkDevice device,
                         uint32_t transitionCount,
                         const VkHostImageLayoutTransitionInfo *pTransitions)
{
   struct vn_device *dev = vn_device_from_handle(device);

   vn_async_vkTransitionImageLayout(dev->primary_ring, device,
                                    transitionCount, pTransitions);

   return VK_SUCCESS;
}

VkResult
vn_CopyImageToImage(VkDevice device,
                    const VkCopyImageToImageInfo *pCopyImageToImageInfo)
{
   struct vn_device *dev = vn_device_from_handle(device);

   vn_async_vkCopyImageToImage(dev->primary_ring, device,
                               pCopyImageToImageInfo);

   return VK_SUCCESS;
}

static size_t
vn_get_memcpy_size(VkDevice dev_handle,
                   VkImage img_handle,
                   const VkImageSubresourceLayers *subres_layers)
{
   VK_FROM_HANDLE(vk_image, img_vk, img_handle);

   const uint32_t layer_count =
      vk_image_subresource_layer_count(img_vk, subres_layers);
   size_t total_size = 0;

   for (uint32_t i = 0; i < layer_count; i++) {
      const VkImageSubresource2 subres = {
         .sType = VK_STRUCTURE_TYPE_IMAGE_SUBRESOURCE_2,
         .imageSubresource =
            (VkImageSubresource){
               .aspectMask = subres_layers->aspectMask,
               .mipLevel = subres_layers->mipLevel,
               .arrayLayer = subres_layers->baseArrayLayer + i,
            },
      };

      VkSubresourceHostMemcpySize copy_size = {
         .sType = VK_STRUCTURE_TYPE_SUBRESOURCE_HOST_MEMCPY_SIZE,
      };
      VkSubresourceLayout2 layout = {
         .sType = VK_STRUCTURE_TYPE_SUBRESOURCE_LAYOUT_2,
         .pNext = &copy_size,
      };

      vn_GetImageSubresourceLayout2(dev_handle, img_handle, &subres, &layout);

      total_size += copy_size.size;
   }

   return total_size;
}

static size_t
vn_get_copy_size(VkImage img_handle,
                 const VkImageSubresourceLayers *subres_layers,
                 uint32_t mem_row_length,
                 uint32_t mem_img_height,
                 VkExtent3D img_extent)
{
   VK_FROM_HANDLE(vk_image, img_vk, img_handle);

   VkFormat format = img_vk->format;

   /* Per spec: Table 30. Depth/Stencil Aspect Copy Table */
   const bool copy_depth =
      subres_layers->aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT;
   const bool copy_stencil =
      subres_layers->aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT;
   if (copy_depth && !copy_stencil)
      format = vk_format_depth_only(format);
   else if (!copy_depth && copy_stencil)
      format = vk_format_stencil_only(format);

   const uint32_t width = mem_row_length ?: img_extent.width;
   const uint32_t height = mem_img_height ?: img_extent.height;
   const uint32_t bw = vk_format_get_blockwidth(format);
   const uint32_t bh = vk_format_get_blockheight(format);
   const uint32_t bs = vk_format_get_blocksize(format);

   /* Per spec: Copying Data Between Buffers and Images */
   const size_t row_extent = DIV_ROUND_UP(width, bw) * bs;
   const size_t slice_extent = DIV_ROUND_UP(height, bh) * row_extent;
   const size_t layer_extent = img_extent.depth * slice_extent;

   const uint32_t layer_count =
      vk_image_subresource_layer_count(img_vk, subres_layers);

   /* Venus must use the theoretically minimum size to avoid OOB access */
   const size_t last_layer_offset = (layer_count - 1) * layer_extent;
   const size_t last_slice_offset =
      (img_extent.depth - 1) * slice_extent + last_layer_offset;
   const size_t last_row_offset =
      (DIV_ROUND_UP(img_extent.height, bh) - 1) * row_extent +
      last_slice_offset;
   const size_t last_row_size = DIV_ROUND_UP(img_extent.width, bw) * bs;

   return last_row_size + last_row_offset;
}

VkResult
vn_CopyImageToMemory(VkDevice device,
                     const VkCopyImageToMemoryInfo *pCopyImageToMemoryInfo)
{
   struct vn_device *dev = vn_device_from_handle(device);
   const VkCopyImageToMemoryInfo *info = pCopyImageToMemoryInfo;
   const VkImageToMemoryCopy *regions = info->pRegions;

   for (uint32_t i = 0; i < info->regionCount; i++) {
      const size_t data_size =
         (info->flags & VK_HOST_IMAGE_COPY_MEMCPY)
            ? vn_get_memcpy_size(device, info->srcImage,
                                 &regions[i].imageSubresource)
            : vn_get_copy_size(info->srcImage, &regions[i].imageSubresource,
                               regions[i].memoryRowLength,
                               regions[i].memoryImageHeight,
                               regions[i].imageExtent);

      const VkCopyImageToMemoryInfoMESA local_info = {
         .sType = VK_STRUCTURE_TYPE_COPY_IMAGE_TO_MEMORY_INFO_MESA,
         .flags = info->flags,
         .srcImage = info->srcImage,
         .srcImageLayout = info->srcImageLayout,
         .memoryRowLength = regions[i].memoryRowLength,
         .memoryImageHeight = regions[i].memoryImageHeight,
         .imageSubresource = regions[i].imageSubresource,
         .imageOffset = regions[i].imageOffset,
         .imageExtent = regions[i].imageExtent,
      };

      /* We do per region copy here for the optimal performance via renderer
       * side in-place host pointer encoding: the temp alloc and memcpy to
       * reply shmem are both skipped. The flatten overhead is trivial as
       * compared to the host copy perf win.
       */
      VkResult ret = vn_call_vkCopyImageToMemoryMESA(
         dev->primary_ring, device, &local_info, data_size,
         regions[i].pHostPointer);
      if (ret != VK_SUCCESS)
         return vn_error(dev->instance, ret);
   }

   return VK_SUCCESS;
}

VkResult
vn_CopyMemoryToImage(VkDevice device,
                     const VkCopyMemoryToImageInfo *pCopyMemoryToImageInfo)
{
   struct vn_device *dev = vn_device_from_handle(device);
   const VkCopyMemoryToImageInfo *info = pCopyMemoryToImageInfo;
   const VkMemoryToImageCopy *regions = info->pRegions;

   STACK_ARRAY(VkMemoryToImageCopyMESA, local_regions, info->regionCount);

   for (uint32_t i = 0; i < info->regionCount; i++) {
      const size_t data_size =
         (info->flags & VK_HOST_IMAGE_COPY_MEMCPY)
            ? vn_get_memcpy_size(device, info->dstImage,
                                 &regions[i].imageSubresource)
            : vn_get_copy_size(info->dstImage, &regions[i].imageSubresource,
                               regions[i].memoryRowLength,
                               regions[i].memoryImageHeight,
                               regions[i].imageExtent);

      local_regions[i] = (VkMemoryToImageCopyMESA){
         .sType = VK_STRUCTURE_TYPE_MEMORY_TO_IMAGE_COPY_MESA,
         .dataSize = data_size,
         .pData = regions[i].pHostPointer,
         .memoryRowLength = regions[i].memoryRowLength,
         .memoryImageHeight = regions[i].memoryImageHeight,
         .imageSubresource = regions[i].imageSubresource,
         .imageOffset = regions[i].imageOffset,
         .imageExtent = regions[i].imageExtent,
      };
   }

   const VkCopyMemoryToImageInfoMESA local_info = {
      .sType = VK_STRUCTURE_TYPE_COPY_MEMORY_TO_IMAGE_INFO_MESA,
      .flags = info->flags,
      .dstImage = info->dstImage,
      .dstImageLayout = info->dstImageLayout,
      .regionCount = info->regionCount,
      .pRegions = local_regions,
   };

   vn_async_vkCopyMemoryToImageMESA(dev->primary_ring, device, &local_info);

   STACK_ARRAY_FINISH(local_regions);

   return VK_SUCCESS;
}
