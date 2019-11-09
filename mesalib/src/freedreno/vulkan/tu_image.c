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

enum a6xx_tile_mode
tu6_get_image_tile_mode(struct tu_image *image, int level)
{
   if (image_level_linear(image, level))
      return TILE6_LINEAR;
   else
      return image->tile_mode;
}

/* indexed by cpp, including msaa 2x and 4x: */
static const struct {
   unsigned pitchalign;
   unsigned heightalign;
} tile_alignment[] = {
   [1]  = { 128, 32 },
   [2]  = { 128, 16 },
   [3]  = {  64, 32 },
   [4]  = {  64, 16 },
   [6]  = {  64, 16 },
   [8]  = {  64, 16 },
   [12] = {  64, 16 },
   [16] = {  64, 16 },
   [24] = {  64, 16 },
   [32] = {  64, 16 },
   [48] = {  64, 16 },
   [64] = {  64, 16 },

   /* special case for r8g8: */
   [0]  = { 64, 32 },
};

static void
setup_slices(struct tu_image *image, const VkImageCreateInfo *pCreateInfo)
{
   VkFormat format = pCreateInfo->format;
   enum vk_format_layout layout = vk_format_description(format)->layout;
   uint32_t layer_size = 0;
   int ta = image->cpp;

   /* The r8g8 format seems to not play by the normal tiling rules: */
   if (image->cpp == 2 && vk_format_get_nr_components(format) == 2)
      ta = 0;

   for (unsigned level = 0; level < pCreateInfo->mipLevels; level++) {
      struct tu_image_level *slice = &image->levels[level];
      uint32_t width = u_minify(pCreateInfo->extent.width, level);
      uint32_t height = u_minify(pCreateInfo->extent.height, level);
      uint32_t depth = u_minify(pCreateInfo->extent.depth, level);
      uint32_t aligned_height = height;
      uint32_t blocks;
      uint32_t pitchalign;

      if (image->tile_mode && !image_level_linear(image, level)) {
         /* tiled levels of 3D textures are rounded up to PoT dimensions: */
         if (pCreateInfo->imageType == VK_IMAGE_TYPE_3D) {
            width = util_next_power_of_two(width);
            height = aligned_height = util_next_power_of_two(height);
         }
         pitchalign = tile_alignment[ta].pitchalign;
         aligned_height = align(aligned_height, tile_alignment[ta].heightalign);
      } else {
         pitchalign = 64;
      }

      /* The blits used for mem<->gmem work at a granularity of
       * 32x32, which can cause faults due to over-fetch on the
       * last level.  The simple solution is to over-allocate a
       * bit the last level to ensure any over-fetch is harmless.
       * The pitch is already sufficiently aligned, but height
       * may not be:
       */
      if (level + 1 == pCreateInfo->mipLevels)
         aligned_height = align(aligned_height, 32);

      if (layout == VK_FORMAT_LAYOUT_ASTC)
         slice->pitch =
            util_align_npot(width, pitchalign * vk_format_get_blockwidth(format));
      else
         slice->pitch = align(width, pitchalign);

      slice->offset = layer_size;
      blocks = vk_format_get_block_count(format, slice->pitch, aligned_height);

      /* 1d array and 2d array textures must all have the same layer size
       * for each miplevel on a6xx. 3d textures can have different layer
       * sizes for high levels, but the hw auto-sizer is buggy (or at least
       * different than what this code does), so as soon as the layer size
       * range gets into range, we stop reducing it.
       */
      if (pCreateInfo->imageType == VK_IMAGE_TYPE_3D) {
         if (level < 1 || image->levels[level - 1].size > 0xf000) {
            slice->size = align(blocks * image->cpp, 4096);
         } else {
            slice->size = image->levels[level - 1].size;
         }
      } else {
         slice->size = blocks * image->cpp;
      }

      layer_size += slice->size * depth;
   }

   image->layer_size = align(layer_size, 4096);
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
   image->samples = pCreateInfo->samples;
   image->cpp = vk_format_get_blocksize(image->vk_format) * image->samples;

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

   image->tile_mode = TILE6_3;

   if (pCreateInfo->tiling == VK_IMAGE_TILING_LINEAR ||
       /* compressed textures can't use tiling? */
       vk_format_is_compressed(image->vk_format) ||
       /* scanout needs to be linear (what about tiling modifiers?) */
       create_info->scanout ||
       /* image_to_image copy doesn't deal with tiling+swap */
       tu6_get_native_format(image->vk_format)->swap ||
       /* r8g8 formats are tiled different and could break image_to_image copy */
       (image->cpp == 2 && vk_format_get_nr_components(image->vk_format) == 2))
      image->tile_mode = TILE6_LINEAR;

   setup_slices(image, pCreateInfo);

   image->size = image->layer_size * pCreateInfo->arrayLayers;
   *pImage = tu_image_to_handle(image);

   return VK_SUCCESS;
}

static enum a6xx_tex_fetchsize
tu6_fetchsize(VkFormat format)
{
   if (vk_format_description(format)->layout == VK_FORMAT_LAYOUT_ASTC)
      return TFETCH6_16_BYTE;

   switch (vk_format_get_blocksize(format) / vk_format_get_blockwidth(format)) {
   case 1: return TFETCH6_1_BYTE;
   case 2: return TFETCH6_2_BYTE;
   case 4: return TFETCH6_4_BYTE;
   case 8: return TFETCH6_8_BYTE;
   case 16: return TFETCH6_16_BYTE;
   default:
      unreachable("bad block size");
   }
}

static uint32_t
tu6_texswiz(const VkComponentMapping *comps, const unsigned char *fmt_swiz)
{
   unsigned char swiz[4] = {comps->r, comps->g, comps->b, comps->a};
   unsigned char vk_swizzle[] = {
      [VK_COMPONENT_SWIZZLE_ZERO] = A6XX_TEX_ZERO,
      [VK_COMPONENT_SWIZZLE_ONE]  = A6XX_TEX_ONE,
      [VK_COMPONENT_SWIZZLE_R] = A6XX_TEX_X,
      [VK_COMPONENT_SWIZZLE_G] = A6XX_TEX_Y,
      [VK_COMPONENT_SWIZZLE_B] = A6XX_TEX_Z,
      [VK_COMPONENT_SWIZZLE_A] = A6XX_TEX_W,
   };
   for (unsigned i = 0; i < 4; i++) {
      swiz[i] = (swiz[i] == VK_COMPONENT_SWIZZLE_IDENTITY) ? i : vk_swizzle[swiz[i]];
      /* if format has 0/1 in channel, use that (needed for bc1_rgb) */
      if (swiz[i] < 4) {
         switch (fmt_swiz[swiz[i]]) {
         case VK_SWIZZLE_0: swiz[i] = A6XX_TEX_ZERO; break;
         case VK_SWIZZLE_1: swiz[i] = A6XX_TEX_ONE;  break;
         }
      }
   }

   return A6XX_TEX_CONST_0_SWIZ_X(swiz[0]) |
          A6XX_TEX_CONST_0_SWIZ_Y(swiz[1]) |
          A6XX_TEX_CONST_0_SWIZ_Z(swiz[2]) |
          A6XX_TEX_CONST_0_SWIZ_W(swiz[3]);
}

static enum a6xx_tex_type
tu6_tex_type(VkImageViewType type)
{
   switch (type) {
   default:
   case VK_IMAGE_VIEW_TYPE_1D:
   case VK_IMAGE_VIEW_TYPE_1D_ARRAY:
      return A6XX_TEX_1D;
   case VK_IMAGE_VIEW_TYPE_2D:
   case VK_IMAGE_VIEW_TYPE_2D_ARRAY:
      return A6XX_TEX_2D;
   case VK_IMAGE_VIEW_TYPE_3D:
      return A6XX_TEX_3D;
   case VK_IMAGE_VIEW_TYPE_CUBE:
   case VK_IMAGE_VIEW_TYPE_CUBE_ARRAY:
      return A6XX_TEX_CUBE;
   }
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

   memset(iview->descriptor, 0, sizeof(iview->descriptor));

   const struct tu_native_format *fmt = tu6_get_native_format(iview->vk_format);
   struct tu_image_level *slice0 = &image->levels[iview->base_mip];
   uint64_t base_addr = image->bo->iova + iview->base_layer * image->layer_size + slice0->offset;
   uint32_t pitch = (slice0->pitch / vk_format_get_blockwidth(iview->vk_format)) *
                        vk_format_get_blocksize(iview->vk_format);
   enum a6xx_tile_mode tile_mode =
      image_level_linear(image, iview->base_mip) ? TILE6_LINEAR : image->tile_mode;

   iview->descriptor[0] =
      A6XX_TEX_CONST_0_TILE_MODE(tile_mode) |
      COND(vk_format_is_srgb(iview->vk_format), A6XX_TEX_CONST_0_SRGB) |
      A6XX_TEX_CONST_0_FMT(fmt->tex) |
      A6XX_TEX_CONST_0_SAMPLES(tu_msaa_samples(image->samples)) |
      A6XX_TEX_CONST_0_SWAP(image->tile_mode ? WZYX : fmt->swap) |
      tu6_texswiz(&pCreateInfo->components, vk_format_description(iview->vk_format)->swizzle) |
      A6XX_TEX_CONST_0_MIPLVLS(iview->level_count - 1);
   iview->descriptor[1] =
      A6XX_TEX_CONST_1_WIDTH(u_minify(image->extent.width, iview->base_mip)) |
      A6XX_TEX_CONST_1_HEIGHT(u_minify(image->extent.height, iview->base_mip));
   iview->descriptor[2] =
      A6XX_TEX_CONST_2_FETCHSIZE(tu6_fetchsize(iview->vk_format)) |
      A6XX_TEX_CONST_2_PITCH(pitch) |
      A6XX_TEX_CONST_2_TYPE(tu6_tex_type(pCreateInfo->viewType));
   iview->descriptor[3] = 0;
   iview->descriptor[4] = base_addr;
   iview->descriptor[5] = base_addr >> 32;

   if (pCreateInfo->viewType != VK_IMAGE_VIEW_TYPE_3D) {
      iview->descriptor[3] |= A6XX_TEX_CONST_3_ARRAY_PITCH(image->layer_size);
      iview->descriptor[5] |= A6XX_TEX_CONST_5_DEPTH(iview->layer_count);
   } else {
      iview->descriptor[3] |=
         A6XX_TEX_CONST_3_MIN_LAYERSZ(image->levels[image->level_count - 1].size) |
         A6XX_TEX_CONST_3_ARRAY_PITCH(slice0->size);
      iview->descriptor[5] |=
         A6XX_TEX_CONST_5_DEPTH(u_minify(image->extent.depth, iview->base_mip));
   }
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

   const struct wsi_image_create_info *wsi_info =
      vk_find_struct_const(pCreateInfo->pNext, WSI_IMAGE_CREATE_INFO_MESA);
   bool scanout = wsi_info && wsi_info->scanout;

   return tu_image_create(device,
                          &(struct tu_image_create_info) {
                             .vk_info = pCreateInfo,
                             .scanout = scanout,
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
