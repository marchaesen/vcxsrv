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
#include "util/format/u_format.h"
#include "vk_format.h"
#include "vk_util.h"
#include "drm-uapi/drm_fourcc.h"

static inline bool
image_level_linear(struct tu_image *image, int level, bool ubwc)
{
   unsigned w = u_minify(image->extent.width, level);
   /* all levels are tiled/compressed with UBWC */
   return ubwc ? false : (w < 16);
}

enum a6xx_tile_mode
tu6_get_image_tile_mode(struct tu_image *image, int level)
{
   if (image_level_linear(image, level, !!image->layout.ubwc_size))
      return TILE6_LINEAR;
   else
      return image->layout.tile_mode;
}

VkResult
tu_image_create(VkDevice _device,
                const VkImageCreateInfo *pCreateInfo,
                const VkAllocationCallbacks *alloc,
                VkImage *pImage,
                uint64_t modifier)
{
   TU_FROM_HANDLE(tu_device, device, _device);
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

   image->layout.tile_mode = TILE6_3;
   bool ubwc_enabled = true;

   /* disable tiling when linear is requested and for compressed formats */
   if (pCreateInfo->tiling == VK_IMAGE_TILING_LINEAR ||
       modifier == DRM_FORMAT_MOD_LINEAR ||
       vk_format_is_compressed(image->vk_format)) {
      image->layout.tile_mode = TILE6_LINEAR;
      ubwc_enabled = false;
   }

   /* using UBWC with D24S8 breaks the "stencil read" copy path (why?)
    * (causes any deqp tests that need to check stencil to fail)
    * disable UBWC for this format until we properly support copy aspect masks
    */
   if (image->vk_format == VK_FORMAT_D24_UNORM_S8_UINT)
      ubwc_enabled = false;

   /* UBWC can't be used with E5B9G9R9 */
   if (image->vk_format == VK_FORMAT_E5B9G9R9_UFLOAT_PACK32)
      ubwc_enabled = false;

   if (image->extent.depth > 1) {
      tu_finishme("UBWC with 3D textures");
      ubwc_enabled = false;
   }

   /* Disable UBWC for storage images.
    *
    * The closed GL driver skips UBWC for storage images (and additionally
    * uses linear for writeonly images).  We seem to have image tiling working
    * in freedreno in general, so turnip matches that.  freedreno also enables
    * UBWC on images, but it's not really tested due to the lack of
    * UBWC-enabled mipmaps in freedreno currently.  Just match the closed GL
    * behavior of no UBWC.
   */
   if (image->usage & VK_IMAGE_USAGE_STORAGE_BIT)
      ubwc_enabled = false;

   uint32_t ubwc_blockwidth, ubwc_blockheight;
   fdl6_get_ubwc_blockwidth(&image->layout,
                            &ubwc_blockwidth, &ubwc_blockheight);
   if (!ubwc_blockwidth) {
      tu_finishme("UBWC for cpp=%d", image->layout.cpp);
      ubwc_enabled = false;
   }

   /* expect UBWC enabled if we asked for it */
   assert(modifier != DRM_FORMAT_MOD_QCOM_COMPRESSED || ubwc_enabled);

   fdl6_layout(&image->layout, vk_format_to_pipe_format(image->vk_format),
               image->samples,
               pCreateInfo->extent.width,
               pCreateInfo->extent.height,
               pCreateInfo->extent.depth,
               pCreateInfo->mipLevels,
               pCreateInfo->arrayLayers,
               pCreateInfo->imageType == VK_IMAGE_TYPE_3D,
               ubwc_enabled);

   *pImage = tu_image_to_handle(image);

   return VK_SUCCESS;
}

static enum a6xx_tex_fetchsize
tu6_fetchsize(VkFormat format)
{
   if (vk_format_description(format)->layout == UTIL_FORMAT_LAYOUT_ASTC)
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
tu6_texswiz(const VkComponentMapping *comps,
            VkFormat format,
            VkImageAspectFlagBits aspect_mask)
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
   const unsigned char *fmt_swiz = vk_format_description(format)->swizzle;

   for (unsigned i = 0; i < 4; i++) {
      swiz[i] = (swiz[i] == VK_COMPONENT_SWIZZLE_IDENTITY) ? i : vk_swizzle[swiz[i]];
      /* if format has 0/1 in channel, use that (needed for bc1_rgb) */
      if (swiz[i] < 4) {
         if (aspect_mask == VK_IMAGE_ASPECT_STENCIL_BIT &&
             format == VK_FORMAT_D24_UNORM_S8_UINT)
            swiz[i] = A6XX_TEX_Y;
         switch (fmt_swiz[swiz[i]]) {
         case PIPE_SWIZZLE_0: swiz[i] = A6XX_TEX_ZERO; break;
         case PIPE_SWIZZLE_1: swiz[i] = A6XX_TEX_ONE;  break;
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

   // should we minify?
   iview->extent = image->extent;

   iview->base_layer = range->baseArrayLayer;
   iview->layer_count = tu_get_layerCount(image, range);
   iview->base_mip = range->baseMipLevel;
   iview->level_count = tu_get_levelCount(image, range);

   memset(iview->descriptor, 0, sizeof(iview->descriptor));

   const struct tu_native_format *fmt = tu6_get_native_format(iview->vk_format);
   uint64_t base_addr = tu_image_base(image, iview->base_mip, iview->base_layer);
   uint64_t ubwc_addr = tu_image_ubwc_base(image, iview->base_mip, iview->base_layer);

   uint32_t pitch = tu_image_stride(image, iview->base_mip) / vk_format_get_blockwidth(iview->vk_format);
   enum a6xx_tile_mode tile_mode = tu6_get_image_tile_mode(image, iview->base_mip);
   uint32_t width = u_minify(image->extent.width, iview->base_mip);
   uint32_t height = u_minify(image->extent.height, iview->base_mip);
   uint32_t depth = pCreateInfo->viewType == VK_IMAGE_VIEW_TYPE_3D ?
      u_minify(image->extent.depth, iview->base_mip) : iview->layer_count;

   unsigned fmt_tex = fmt->tex;
   if (iview->aspect_mask == VK_IMAGE_ASPECT_STENCIL_BIT &&
       iview->vk_format == VK_FORMAT_D24_UNORM_S8_UINT)
      fmt_tex = TFMT6_S8Z24_UINT;

   iview->descriptor[0] =
      A6XX_TEX_CONST_0_TILE_MODE(tile_mode) |
      COND(vk_format_is_srgb(iview->vk_format), A6XX_TEX_CONST_0_SRGB) |
      A6XX_TEX_CONST_0_FMT(fmt_tex) |
      A6XX_TEX_CONST_0_SAMPLES(tu_msaa_samples(image->samples)) |
      A6XX_TEX_CONST_0_SWAP(image->layout.tile_mode ? WZYX : fmt->swap) |
      tu6_texswiz(&pCreateInfo->components, iview->vk_format, iview->aspect_mask) |
      A6XX_TEX_CONST_0_MIPLVLS(iview->level_count - 1);
   iview->descriptor[1] = A6XX_TEX_CONST_1_WIDTH(width) | A6XX_TEX_CONST_1_HEIGHT(height);
   iview->descriptor[2] =
      A6XX_TEX_CONST_2_FETCHSIZE(tu6_fetchsize(iview->vk_format)) |
      A6XX_TEX_CONST_2_PITCH(pitch) |
      A6XX_TEX_CONST_2_TYPE(tu6_tex_type(pCreateInfo->viewType));
   iview->descriptor[3] = A6XX_TEX_CONST_3_ARRAY_PITCH(tu_layer_size(image, iview->base_mip));
   iview->descriptor[4] = base_addr;
   iview->descriptor[5] = (base_addr >> 32) | A6XX_TEX_CONST_5_DEPTH(depth);

   if (image->layout.ubwc_size) {
      uint32_t block_width, block_height;
      fdl6_get_ubwc_blockwidth(&image->layout,
                               &block_width, &block_height);

      iview->descriptor[3] |= A6XX_TEX_CONST_3_FLAG | A6XX_TEX_CONST_3_TILE_ALL;
      iview->descriptor[7] = ubwc_addr;
      iview->descriptor[8] = ubwc_addr >> 32;
      iview->descriptor[9] |= A6XX_TEX_CONST_9_FLAG_BUFFER_ARRAY_PITCH(tu_image_ubwc_size(image, iview->base_mip) >> 2);
      iview->descriptor[10] |=
         A6XX_TEX_CONST_10_FLAG_BUFFER_PITCH(tu_image_ubwc_pitch(image, iview->base_mip)) |
         A6XX_TEX_CONST_10_FLAG_BUFFER_LOGW(util_logbase2_ceil(DIV_ROUND_UP(width, block_width))) |
         A6XX_TEX_CONST_10_FLAG_BUFFER_LOGH(util_logbase2_ceil(DIV_ROUND_UP(height, block_height)));
   }

   if (pCreateInfo->viewType == VK_IMAGE_VIEW_TYPE_3D) {
      iview->descriptor[3] |=
         A6XX_TEX_CONST_3_MIN_LAYERSZ(image->layout.slices[image->level_count - 1].size0);
   }

   if (image->usage & VK_IMAGE_USAGE_STORAGE_BIT) {
      memset(iview->storage_descriptor, 0, sizeof(iview->storage_descriptor));

      iview->storage_descriptor[0] =
         A6XX_IBO_0_FMT(fmt->tex) |
         A6XX_IBO_0_TILE_MODE(tile_mode);
      iview->storage_descriptor[1] =
         A6XX_IBO_1_WIDTH(width) |
         A6XX_IBO_1_HEIGHT(height);
      iview->storage_descriptor[2] =
         A6XX_IBO_2_PITCH(pitch) |
         A6XX_IBO_2_TYPE(tu6_tex_type(pCreateInfo->viewType));
      iview->storage_descriptor[3] = A6XX_IBO_3_ARRAY_PITCH(tu_layer_size(image, iview->base_mip));

      iview->storage_descriptor[4] = base_addr;
      iview->storage_descriptor[5] = (base_addr >> 32) | A6XX_IBO_5_DEPTH(depth);

      if (image->layout.ubwc_size) {
         iview->storage_descriptor[3] |= A6XX_IBO_3_FLAG | A6XX_IBO_3_UNK27;
         iview->storage_descriptor[7] |= ubwc_addr;
         iview->storage_descriptor[8] |= ubwc_addr >> 32;
         iview->storage_descriptor[9] = A6XX_IBO_9_FLAG_BUFFER_ARRAY_PITCH(tu_image_ubwc_size(image, iview->base_mip) >> 2);
         iview->storage_descriptor[10] =
            A6XX_IBO_10_FLAG_BUFFER_PITCH(tu_image_ubwc_pitch(image, iview->base_mip));
      }
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

   uint64_t modifier = DRM_FORMAT_MOD_INVALID;
   if (pCreateInfo->tiling == VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT) {
      const VkImageDrmFormatModifierListCreateInfoEXT *mod_info =
         vk_find_struct_const(pCreateInfo->pNext,
                              IMAGE_DRM_FORMAT_MODIFIER_LIST_CREATE_INFO_EXT);

      modifier = DRM_FORMAT_MOD_LINEAR;
      for (unsigned i = 0; i < mod_info->drmFormatModifierCount; i++) {
         if (mod_info->pDrmFormatModifiers[i] == DRM_FORMAT_MOD_QCOM_COMPRESSED)
            modifier = DRM_FORMAT_MOD_QCOM_COMPRESSED;
      }
   } else {
      const struct wsi_image_create_info *wsi_info =
         vk_find_struct_const(pCreateInfo->pNext, WSI_IMAGE_CREATE_INFO_MESA);
      if (wsi_info && wsi_info->scanout)
         modifier = DRM_FORMAT_MOD_LINEAR;
   }

   return tu_image_create(device, pCreateInfo, pAllocator, pImage, modifier);
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

   const struct fdl_slice *slice = image->layout.slices + pSubresource->mipLevel;

   pLayout->offset = fdl_surface_offset(&image->layout,
                                        pSubresource->mipLevel,
                                        pSubresource->arrayLayer);
   pLayout->size = slice->size0;
   pLayout->rowPitch =
      slice->pitch * vk_format_get_blocksize(image->vk_format);
   pLayout->arrayPitch = image->layout.layer_size;
   pLayout->depthPitch = slice->size0;

   if (image->layout.ubwc_size) {
      /* UBWC starts at offset 0 */
      pLayout->offset = 0;
      /* UBWC scanout won't match what the kernel wants if we have levels/layers */
      assert(image->level_count == 1 && image->layer_count == 1);
   }
}

VkResult tu_GetImageDrmFormatModifierPropertiesEXT(
    VkDevice                                    device,
    VkImage                                     _image,
    VkImageDrmFormatModifierPropertiesEXT*      pProperties)
{
   TU_FROM_HANDLE(tu_image, image, _image);

   assert(pProperties->sType ==
          VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_PROPERTIES_EXT);

   /* TODO invent a modifier for tiled but not UBWC buffers */

   if (!image->layout.tile_mode)
      pProperties->drmFormatModifier = DRM_FORMAT_MOD_LINEAR;
   else if (image->layout.ubwc_size)
      pProperties->drmFormatModifier = DRM_FORMAT_MOD_QCOM_COMPRESSED;
   else
      pProperties->drmFormatModifier = DRM_FORMAT_MOD_INVALID;

   return VK_SUCCESS;
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

   view->buffer = buffer;

   enum VkFormat vfmt = pCreateInfo->format;
   enum pipe_format pfmt = vk_format_to_pipe_format(vfmt);
   const struct tu_native_format *fmt = tu6_get_native_format(vfmt);

   uint32_t range;
   if (pCreateInfo->range == VK_WHOLE_SIZE)
      range = buffer->size - pCreateInfo->offset;
   else
      range = pCreateInfo->range;
   uint32_t elements = range / util_format_get_blocksize(pfmt);

   static const VkComponentMapping components = {
      .r = VK_COMPONENT_SWIZZLE_R,
      .g = VK_COMPONENT_SWIZZLE_G,
      .b = VK_COMPONENT_SWIZZLE_B,
      .a = VK_COMPONENT_SWIZZLE_A,
   };

   uint64_t iova = tu_buffer_iova(buffer) + pCreateInfo->offset;

   memset(&view->descriptor, 0, sizeof(view->descriptor));

   view->descriptor[0] =
      A6XX_TEX_CONST_0_TILE_MODE(TILE6_LINEAR) |
      A6XX_TEX_CONST_0_SWAP(fmt->swap) |
      A6XX_TEX_CONST_0_FMT(fmt->tex) |
      A6XX_TEX_CONST_0_MIPLVLS(0) |
      tu6_texswiz(&components, vfmt, VK_IMAGE_ASPECT_COLOR_BIT);
      COND(vk_format_is_srgb(vfmt), A6XX_TEX_CONST_0_SRGB);
   view->descriptor[1] =
      A6XX_TEX_CONST_1_WIDTH(elements & MASK(15)) |
      A6XX_TEX_CONST_1_HEIGHT(elements >> 15);
   view->descriptor[2] =
      A6XX_TEX_CONST_2_UNK4 |
      A6XX_TEX_CONST_2_UNK31;
   view->descriptor[4] = iova;
   view->descriptor[5] = iova >> 32;
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
