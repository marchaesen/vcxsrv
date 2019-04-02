/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * based in part on anv driver which is:
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

#include "util/debug.h"
#include "util/u_atomic.h"
#include "vk_format.h"
#include "vk_util.h"

static inline bool
image_level_linear(struct tu_image *image, int level)
{
   unsigned w = u_minify(image->extent.width, level);
   return w < 16;
}

/* indexed by cpp: */
static const struct
{
   unsigned pitchalign;
   unsigned heightalign;
} tile_alignment[] = {
   [1] = { 128, 32 }, [2] = { 128, 16 }, [3] = { 128, 16 }, [4] = { 64, 16 },
   [8] = { 64, 16 },  [12] = { 64, 16 }, [16] = { 64, 16 },
};

static void
setup_slices(struct tu_image *image, const VkImageCreateInfo *pCreateInfo)
{
   enum vk_format_layout layout =
      vk_format_description(pCreateInfo->format)->layout;
   uint32_t layer_size = 0;
   uint32_t width = pCreateInfo->extent.width;
   uint32_t height = pCreateInfo->extent.height;
   uint32_t depth = pCreateInfo->extent.depth;
   bool layer_first = pCreateInfo->imageType != VK_IMAGE_TYPE_3D;
   uint32_t alignment = pCreateInfo->imageType == VK_IMAGE_TYPE_3D ? 4096 : 1;
   uint32_t cpp = vk_format_get_blocksize(pCreateInfo->format);

   uint32_t heightalign = tile_alignment[cpp].heightalign;

   for (unsigned level = 0; level < pCreateInfo->mipLevels; level++) {
      struct tu_image_level *slice = &image->levels[level];
      bool linear_level = image_level_linear(image, level);
      uint32_t aligned_height = height;
      uint32_t blocks;
      uint32_t pitchalign;

      if (image->tile_mode && !linear_level) {
         pitchalign = tile_alignment[cpp].pitchalign;
         aligned_height = align(aligned_height, heightalign);
      } else {
         pitchalign = 64;

         /* The blits used for mem<->gmem work at a granularity of
          * 32x32, which can cause faults due to over-fetch on the
          * last level.  The simple solution is to over-allocate a
          * bit the last level to ensure any over-fetch is harmless.
          * The pitch is already sufficiently aligned, but height
          * may not be:
          */
         if ((level + 1 == pCreateInfo->mipLevels))
            aligned_height = align(aligned_height, 32);
      }

      if (layout == VK_FORMAT_LAYOUT_ASTC)
         slice->pitch = util_align_npot(
            width,
            pitchalign * vk_format_get_blockwidth(pCreateInfo->format));
      else
         slice->pitch = align(width, pitchalign);

      slice->offset = layer_size;
      blocks = vk_format_get_block_count(pCreateInfo->format, slice->pitch,
                                         aligned_height);

      /* 1d array and 2d array textures must all have the same layer size
       * for each miplevel on a3xx. 3d textures can have different layer
       * sizes for high levels, but the hw auto-sizer is buggy (or at least
       * different than what this code does), so as soon as the layer size
       * range gets into range, we stop reducing it.
       */
      if (pCreateInfo->imageType == VK_IMAGE_TYPE_3D &&
          (level == 1 ||
           (level > 1 && image->levels[level - 1].size > 0xf000)))
         slice->size = align(blocks * cpp, alignment);
      else if (level == 0 || layer_first || alignment == 1)
         slice->size = align(blocks * cpp, alignment);
      else
         slice->size = image->levels[level - 1].size;

      layer_size += slice->size * depth;

      width = u_minify(width, 1);
      height = u_minify(height, 1);
      depth = u_minify(depth, 1);
   }

   image->layer_size = layer_size;
}

VkResult
tu_image_create(VkDevice _device,
                const struct tu_image_create_info *create_info,
                const VkAllocationCallbacks *alloc,
                VkImage *pImage)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   const VkImageCreateInfo *pCreateInfo = create_info->vk_info;
   struct tu_image *image = NULL;
   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);

   tu_assert(pCreateInfo->mipLevels > 0);
   tu_assert(pCreateInfo->arrayLayers > 0);
   tu_assert(pCreateInfo->samples > 0);
   tu_assert(pCreateInfo->extent.width > 0);
   tu_assert(pCreateInfo->extent.height > 0);
   tu_assert(pCreateInfo->extent.depth > 0);

   image = vk_zalloc2(&device->alloc, alloc, sizeof(*image), 8,
                      VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!image)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   image->type = pCreateInfo->imageType;

   image->vk_format = pCreateInfo->format;
   image->tiling = pCreateInfo->tiling;
   image->usage = pCreateInfo->usage;
   image->flags = pCreateInfo->flags;
   image->extent = pCreateInfo->extent;
   image->level_count = pCreateInfo->mipLevels;
   image->layer_count = pCreateInfo->arrayLayers;

   image->exclusive = pCreateInfo->sharingMode == VK_SHARING_MODE_EXCLUSIVE;
   if (pCreateInfo->sharingMode == VK_SHARING_MODE_CONCURRENT) {
      for (uint32_t i = 0; i < pCreateInfo->queueFamilyIndexCount; ++i)
         if (pCreateInfo->pQueueFamilyIndices[i] ==
             VK_QUEUE_FAMILY_EXTERNAL)
            image->queue_family_mask |= (1u << TU_MAX_QUEUE_FAMILIES) - 1u;
         else
            image->queue_family_mask |=
               1u << pCreateInfo->pQueueFamilyIndices[i];
   }

   image->shareable =
      vk_find_struct_const(pCreateInfo->pNext,
                           EXTERNAL_MEMORY_IMAGE_CREATE_INFO) != NULL;

   image->tile_mode = pCreateInfo->tiling == VK_IMAGE_TILING_OPTIMAL ? 3 : 0;
   setup_slices(image, pCreateInfo);

   image->size = image->layer_size * pCreateInfo->arrayLayers;
   *pImage = tu_image_to_handle(image);

   return VK_SUCCESS;
}

void
tu_image_view_init(struct tu_image_view *iview,
                   struct tu_device *device,
                   const VkImageViewCreateInfo *pCreateInfo)
{
   TU_FROM_HANDLE(tu_image, image, pCreateInfo->image);
   const VkImageSubresourceRange *range = &pCreateInfo->subresourceRange;

   switch (image->type) {
   case VK_IMAGE_TYPE_1D:
   case VK_IMAGE_TYPE_2D:
      assert(range->baseArrayLayer + tu_get_layerCount(image, range) <=
             image->layer_count);
      break;
   case VK_IMAGE_TYPE_3D:
      assert(range->baseArrayLayer + tu_get_layerCount(image, range) <=
             tu_minify(image->extent.depth, range->baseMipLevel));
      break;
   default:
      unreachable("bad VkImageType");
   }

   iview->image = image;
   iview->type = pCreateInfo->viewType;
   iview->vk_format = pCreateInfo->format;
   iview->aspect_mask = pCreateInfo->subresourceRange.aspectMask;

   if (iview->aspect_mask == VK_IMAGE_ASPECT_STENCIL_BIT) {
      iview->vk_format = vk_format_stencil_only(iview->vk_format);
   } else if (iview->aspect_mask == VK_IMAGE_ASPECT_DEPTH_BIT) {
      iview->vk_format = vk_format_depth_only(iview->vk_format);
   }

   // should we minify?
   iview->extent = image->extent;

   iview->base_layer = range->baseArrayLayer;
   iview->layer_count = tu_get_layerCount(image, range);
   iview->base_mip = range->baseMipLevel;
   iview->level_count = tu_get_levelCount(image, range);
}

unsigned
tu_image_queue_family_mask(const struct tu_image *image,
                           uint32_t family,
                           uint32_t queue_family)
{
   if (!image->exclusive)
      return image->queue_family_mask;
   if (family == VK_QUEUE_FAMILY_EXTERNAL)
      return (1u << TU_MAX_QUEUE_FAMILIES) - 1u;
   if (family == VK_QUEUE_FAMILY_IGNORED)
      return 1u << queue_family;
   return 1u << family;
}

VkResult
tu_CreateImage(VkDevice device,
               const VkImageCreateInfo *pCreateInfo,
               const VkAllocationCallbacks *pAllocator,
               VkImage *pImage)
{
#ifdef ANDROID
   const VkNativeBufferANDROID *gralloc_info =
      vk_find_struct_const(pCreateInfo->pNext, NATIVE_BUFFER_ANDROID);

   if (gralloc_info)
      return tu_image_from_gralloc(device, pCreateInfo, gralloc_info,
                                   pAllocator, pImage);
#endif

   return tu_image_create(device,
                          &(struct tu_image_create_info) {
                             .vk_info = pCreateInfo,
                             .scanout = false,
                          },
                          pAllocator, pImage);
}

void
tu_DestroyImage(VkDevice _device,
                VkImage _image,
                const VkAllocationCallbacks *pAllocator)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   TU_FROM_HANDLE(tu_image, image, _image);

   if (!image)
      return;

   if (image->owned_memory != VK_NULL_HANDLE)
      tu_FreeMemory(_device, image->owned_memory, pAllocator);

   vk_free2(&device->alloc, pAllocator, image);
}

void
tu_GetImageSubresourceLayout(VkDevice _device,
                             VkImage _image,
                             const VkImageSubresource *pSubresource,
                             VkSubresourceLayout *pLayout)
{
   TU_FROM_HANDLE(tu_image, image, _image);

   const uint32_t layer_offset = image->layer_size * pSubresource->arrayLayer;
   const struct tu_image_level *level =
      image->levels + pSubresource->mipLevel;

   pLayout->offset = layer_offset + level->offset;
   pLayout->size = level->size;
   pLayout->rowPitch =
      level->pitch * vk_format_get_blocksize(image->vk_format);
   pLayout->arrayPitch = image->layer_size;
   pLayout->depthPitch = level->size;
}

VkResult
tu_CreateImageView(VkDevice _device,
                   const VkImageViewCreateInfo *pCreateInfo,
                   const VkAllocationCallbacks *pAllocator,
                   VkImageView *pView)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   struct tu_image_view *view;

   view = vk_alloc2(&device->alloc, pAllocator, sizeof(*view), 8,
                    VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (view == NULL)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   tu_image_view_init(view, device, pCreateInfo);

   *pView = tu_image_view_to_handle(view);

   return VK_SUCCESS;
}

void
tu_DestroyImageView(VkDevice _device,
                    VkImageView _iview,
                    const VkAllocationCallbacks *pAllocator)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   TU_FROM_HANDLE(tu_image_view, iview, _iview);

   if (!iview)
      return;
   vk_free2(&device->alloc, pAllocator, iview);
}

void
tu_buffer_view_init(struct tu_buffer_view *view,
                    struct tu_device *device,
                    const VkBufferViewCreateInfo *pCreateInfo)
{
   TU_FROM_HANDLE(tu_buffer, buffer, pCreateInfo->buffer);

   view->range = pCreateInfo->range == VK_WHOLE_SIZE
                    ? buffer->size - pCreateInfo->offset
                    : pCreateInfo->range;
   view->vk_format = pCreateInfo->format;
}

VkResult
tu_CreateBufferView(VkDevice _device,
                    const VkBufferViewCreateInfo *pCreateInfo,
                    const VkAllocationCallbacks *pAllocator,
                    VkBufferView *pView)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   struct tu_buffer_view *view;

   view = vk_alloc2(&device->alloc, pAllocator, sizeof(*view), 8,
                    VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!view)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   tu_buffer_view_init(view, device, pCreateInfo);

   *pView = tu_buffer_view_to_handle(view);

   return VK_SUCCESS;
}

void
tu_DestroyBufferView(VkDevice _device,
                     VkBufferView bufferView,
                     const VkAllocationCallbacks *pAllocator)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   TU_FROM_HANDLE(tu_buffer_view, view, bufferView);

   if (!view)
      return;

   vk_free2(&device->alloc, pAllocator, view);
}
