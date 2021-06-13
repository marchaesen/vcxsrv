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

#include "tu_cs.h"

static uint32_t
tu6_plane_count(VkFormat format)
{
   switch (format) {
   default:
      return 1;
   case VK_FORMAT_G8_B8R8_2PLANE_420_UNORM:
   case VK_FORMAT_D32_SFLOAT_S8_UINT:
      return 2;
   case VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM:
      return 3;
   }
}

static VkFormat
tu6_plane_format(VkFormat format, uint32_t plane)
{
   switch (format) {
   case VK_FORMAT_G8_B8R8_2PLANE_420_UNORM:
      /* note: with UBWC, and Y plane UBWC is different from R8_UNORM */
      return plane ? VK_FORMAT_R8G8_UNORM : VK_FORMAT_R8_UNORM;
   case VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM:
      return VK_FORMAT_R8_UNORM;
   case VK_FORMAT_D32_SFLOAT_S8_UINT:
      return plane ? VK_FORMAT_S8_UINT : VK_FORMAT_D32_SFLOAT;
   default:
      return format;
   }
}

static uint32_t
tu6_plane_index(VkFormat format, VkImageAspectFlags aspect_mask)
{
   switch (aspect_mask) {
   default:
      return 0;
   case VK_IMAGE_ASPECT_PLANE_1_BIT:
      return 1;
   case VK_IMAGE_ASPECT_PLANE_2_BIT:
      return 2;
   case VK_IMAGE_ASPECT_STENCIL_BIT:
      return format == VK_FORMAT_D32_SFLOAT_S8_UINT;
   }
}

static void
compose_swizzle(unsigned char *swiz, const VkComponentMapping *mapping)
{
   unsigned char src_swiz[4] = { swiz[0], swiz[1], swiz[2], swiz[3] };
   VkComponentSwizzle vk_swiz[4] = {
      mapping->r, mapping->g, mapping->b, mapping->a
   };
   for (int i = 0; i < 4; i++) {
      switch (vk_swiz[i]) {
      case VK_COMPONENT_SWIZZLE_IDENTITY:
         swiz[i] = src_swiz[i];
         break;
      case VK_COMPONENT_SWIZZLE_R...VK_COMPONENT_SWIZZLE_A:
         swiz[i] = src_swiz[vk_swiz[i] - VK_COMPONENT_SWIZZLE_R];
         break;
      case VK_COMPONENT_SWIZZLE_ZERO:
         swiz[i] = A6XX_TEX_ZERO;
         break;
      case VK_COMPONENT_SWIZZLE_ONE:
         swiz[i] = A6XX_TEX_ONE;
         break;
      default:
         unreachable("unexpected swizzle");
      }
   }
}

static uint32_t
tu6_texswiz(const VkComponentMapping *comps,
            const struct tu_sampler_ycbcr_conversion *conversion,
            VkFormat format,
            VkImageAspectFlagBits aspect_mask,
            bool has_z24uint_s8uint)
{
   unsigned char swiz[4] = {
      A6XX_TEX_X, A6XX_TEX_Y, A6XX_TEX_Z, A6XX_TEX_W,
   };

   switch (format) {
   case VK_FORMAT_G8B8G8R8_422_UNORM:
   case VK_FORMAT_B8G8R8G8_422_UNORM:
   case VK_FORMAT_G8_B8R8_2PLANE_420_UNORM:
   case VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM:
      swiz[0] = A6XX_TEX_Z;
      swiz[1] = A6XX_TEX_X;
      swiz[2] = A6XX_TEX_Y;
      break;
   case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
   case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
      /* same hardware format is used for BC1_RGB / BC1_RGBA */
      swiz[3] = A6XX_TEX_ONE;
      break;
   case VK_FORMAT_D24_UNORM_S8_UINT:
      if (aspect_mask == VK_IMAGE_ASPECT_STENCIL_BIT) {
         if (!has_z24uint_s8uint) {
            /* using FMT6_8_8_8_8_UINT */
            swiz[0] = A6XX_TEX_W;
            swiz[1] = A6XX_TEX_ZERO;
         } else {
            /* using FMT6_Z24_UINT_S8_UINT */
            swiz[0] = A6XX_TEX_Y;
            swiz[1] = A6XX_TEX_ZERO;
         }
      }
   default:
      break;
   }

   compose_swizzle(swiz, comps);
   if (conversion)
      compose_swizzle(swiz, &conversion->components);

   return A6XX_TEX_CONST_0_SWIZ_X(swiz[0]) |
          A6XX_TEX_CONST_0_SWIZ_Y(swiz[1]) |
          A6XX_TEX_CONST_0_SWIZ_Z(swiz[2]) |
          A6XX_TEX_CONST_0_SWIZ_W(swiz[3]);
}

void
tu_cs_image_ref(struct tu_cs *cs, const struct tu_image_view *iview, uint32_t layer)
{
   tu_cs_emit(cs, iview->PITCH);
   tu_cs_emit(cs, iview->layer_size >> 6);
   tu_cs_emit_qw(cs, iview->base_addr + iview->layer_size * layer);
}

void
tu_cs_image_stencil_ref(struct tu_cs *cs, const struct tu_image_view *iview, uint32_t layer)
{
   tu_cs_emit(cs, iview->stencil_PITCH);
   tu_cs_emit(cs, iview->stencil_layer_size >> 6);
   tu_cs_emit_qw(cs, iview->stencil_base_addr + iview->stencil_layer_size * layer);
}

void
tu_cs_image_ref_2d(struct tu_cs *cs, const struct tu_image_view *iview, uint32_t layer, bool src)
{
   tu_cs_emit_qw(cs, iview->base_addr + iview->layer_size * layer);
   /* SP_PS_2D_SRC_PITCH has shifted pitch field */
   tu_cs_emit(cs, iview->PITCH << (src ? 9 : 0));
}

void
tu_cs_image_flag_ref(struct tu_cs *cs, const struct tu_image_view *iview, uint32_t layer)
{
   tu_cs_emit_qw(cs, iview->ubwc_addr + iview->ubwc_layer_size * layer);
   tu_cs_emit(cs, iview->FLAG_BUFFER_PITCH);
}

void
tu_image_view_init(struct tu_image_view *iview,
                   const VkImageViewCreateInfo *pCreateInfo,
                   bool has_z24uint_s8uint)
{
   TU_FROM_HANDLE(tu_image, image, pCreateInfo->image);
   const VkImageSubresourceRange *range = &pCreateInfo->subresourceRange;
   VkFormat format = pCreateInfo->format;
   VkImageAspectFlagBits aspect_mask = pCreateInfo->subresourceRange.aspectMask;

   const struct VkSamplerYcbcrConversionInfo *ycbcr_conversion =
      vk_find_struct_const(pCreateInfo->pNext, SAMPLER_YCBCR_CONVERSION_INFO);
   const struct tu_sampler_ycbcr_conversion *conversion = ycbcr_conversion ?
      tu_sampler_ycbcr_conversion_from_handle(ycbcr_conversion->conversion) : NULL;

   iview->image = image;

   memset(iview->descriptor, 0, sizeof(iview->descriptor));

   struct fdl_layout *layout =
      &image->layout[tu6_plane_index(image->vk_format, aspect_mask)];

   uint32_t width = u_minify(layout->width0, range->baseMipLevel);
   uint32_t height = u_minify(layout->height0, range->baseMipLevel);
   uint32_t storage_depth = tu_get_layerCount(image, range);
   if (pCreateInfo->viewType == VK_IMAGE_VIEW_TYPE_3D) {
      storage_depth = u_minify(image->layout[0].depth0, range->baseMipLevel);
   }

   uint32_t depth = storage_depth;
   if (pCreateInfo->viewType == VK_IMAGE_VIEW_TYPE_CUBE ||
       pCreateInfo->viewType == VK_IMAGE_VIEW_TYPE_CUBE_ARRAY) {
      /* Cubes are treated as 2D arrays for storage images, so only divide the
       * depth by 6 for the texture descriptor.
       */
      depth /= 6;
   }

   uint64_t base_addr = image->bo->iova + image->bo_offset +
      fdl_surface_offset(layout, range->baseMipLevel, range->baseArrayLayer);
   uint64_t ubwc_addr = image->bo->iova + image->bo_offset +
      fdl_ubwc_offset(layout, range->baseMipLevel, range->baseArrayLayer);

   uint32_t pitch = fdl_pitch(layout, range->baseMipLevel);
   uint32_t ubwc_pitch = fdl_ubwc_pitch(layout, range->baseMipLevel);
   uint32_t layer_size = fdl_layer_stride(layout, range->baseMipLevel);

   if (aspect_mask != VK_IMAGE_ASPECT_COLOR_BIT)
      format = tu6_plane_format(format, tu6_plane_index(format, aspect_mask));

   struct tu_native_format fmt = tu6_format_texture(format, layout->tile_mode);
   /* note: freedreno layout assumes no TILE_ALL bit for non-UBWC color formats
    * this means smaller mipmap levels have a linear tile mode.
    * Depth/stencil formats have non-linear tile mode.
    */
   fmt.tile_mode = fdl_tile_mode(layout, range->baseMipLevel);

   bool ubwc_enabled = fdl_ubwc_enabled(layout, range->baseMipLevel);

   bool is_d24s8 = (format == VK_FORMAT_D24_UNORM_S8_UINT ||
                    format == VK_FORMAT_X8_D24_UNORM_PACK32);

   if (is_d24s8 && ubwc_enabled)
      fmt.fmt = FMT6_Z24_UNORM_S8_UINT_AS_R8G8B8A8;

   unsigned fmt_tex = fmt.fmt;
   if (is_d24s8) {
      if (aspect_mask & VK_IMAGE_ASPECT_DEPTH_BIT)
         fmt_tex = FMT6_Z24_UNORM_S8_UINT;
      if (aspect_mask == VK_IMAGE_ASPECT_STENCIL_BIT)
         fmt_tex = has_z24uint_s8uint ? FMT6_Z24_UINT_S8_UINT : FMT6_8_8_8_8_UINT;
      /* TODO: also use this format with storage descriptor ? */
   }

   iview->descriptor[0] =
      A6XX_TEX_CONST_0_TILE_MODE(fmt.tile_mode) |
      COND(vk_format_is_srgb(format), A6XX_TEX_CONST_0_SRGB) |
      A6XX_TEX_CONST_0_FMT(fmt_tex) |
      A6XX_TEX_CONST_0_SAMPLES(tu_msaa_samples(layout->nr_samples)) |
      A6XX_TEX_CONST_0_SWAP(fmt.swap) |
      tu6_texswiz(&pCreateInfo->components, conversion, format, aspect_mask, has_z24uint_s8uint) |
      A6XX_TEX_CONST_0_MIPLVLS(tu_get_levelCount(image, range) - 1);
   iview->descriptor[1] = A6XX_TEX_CONST_1_WIDTH(width) | A6XX_TEX_CONST_1_HEIGHT(height);
   iview->descriptor[2] =
      A6XX_TEX_CONST_2_PITCHALIGN(layout->pitchalign - 6) |
      A6XX_TEX_CONST_2_PITCH(pitch) |
      A6XX_TEX_CONST_2_TYPE(tu6_tex_type(pCreateInfo->viewType, false));
   iview->descriptor[3] = A6XX_TEX_CONST_3_ARRAY_PITCH(layer_size);
   iview->descriptor[4] = base_addr;
   iview->descriptor[5] = (base_addr >> 32) | A6XX_TEX_CONST_5_DEPTH(depth);

   if (layout->tile_all)
      iview->descriptor[3] |= A6XX_TEX_CONST_3_TILE_ALL;

   if (format == VK_FORMAT_G8_B8R8_2PLANE_420_UNORM ||
       format == VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM) {
      /* chroma offset re-uses MIPLVLS bits */
      assert(tu_get_levelCount(image, range) == 1);
      if (conversion) {
         if (conversion->chroma_offsets[0] == VK_CHROMA_LOCATION_MIDPOINT)
            iview->descriptor[0] |= A6XX_TEX_CONST_0_CHROMA_MIDPOINT_X;
         if (conversion->chroma_offsets[1] == VK_CHROMA_LOCATION_MIDPOINT)
            iview->descriptor[0] |= A6XX_TEX_CONST_0_CHROMA_MIDPOINT_Y;
      }

      uint64_t base_addr[3];

      iview->descriptor[3] |= A6XX_TEX_CONST_3_TILE_ALL;
      if (ubwc_enabled) {
         iview->descriptor[3] |= A6XX_TEX_CONST_3_FLAG;
         /* no separate ubwc base, image must have the expected layout */
         for (uint32_t i = 0; i < 3; i++) {
            base_addr[i] = image->bo->iova + image->bo_offset +
               fdl_ubwc_offset(&image->layout[i], range->baseMipLevel, range->baseArrayLayer);
         }
      } else {
         for (uint32_t i = 0; i < 3; i++) {
            base_addr[i] = image->bo->iova + image->bo_offset +
               fdl_surface_offset(&image->layout[i], range->baseMipLevel, range->baseArrayLayer);
         }
      }

      iview->descriptor[4] = base_addr[0];
      iview->descriptor[5] |= base_addr[0] >> 32;
      iview->descriptor[6] =
         A6XX_TEX_CONST_6_PLANE_PITCH(fdl_pitch(&image->layout[1], range->baseMipLevel));
      iview->descriptor[7] = base_addr[1];
      iview->descriptor[8] = base_addr[1] >> 32;
      iview->descriptor[9] = base_addr[2];
      iview->descriptor[10] = base_addr[2] >> 32;

      assert(pCreateInfo->viewType != VK_IMAGE_VIEW_TYPE_3D);
      return;
   }

   if (ubwc_enabled) {
      uint32_t block_width, block_height;
      fdl6_get_ubwc_blockwidth(layout, &block_width, &block_height);

      iview->descriptor[3] |= A6XX_TEX_CONST_3_FLAG;
      iview->descriptor[7] = ubwc_addr;
      iview->descriptor[8] = ubwc_addr >> 32;
      iview->descriptor[9] |= A6XX_TEX_CONST_9_FLAG_BUFFER_ARRAY_PITCH(layout->ubwc_layer_size >> 2);
      iview->descriptor[10] |=
         A6XX_TEX_CONST_10_FLAG_BUFFER_PITCH(ubwc_pitch) |
         A6XX_TEX_CONST_10_FLAG_BUFFER_LOGW(util_logbase2_ceil(DIV_ROUND_UP(width, block_width))) |
         A6XX_TEX_CONST_10_FLAG_BUFFER_LOGH(util_logbase2_ceil(DIV_ROUND_UP(height, block_height)));
   }

   if (pCreateInfo->viewType == VK_IMAGE_VIEW_TYPE_3D) {
      iview->descriptor[3] |=
         A6XX_TEX_CONST_3_MIN_LAYERSZ(layout->slices[image->level_count - 1].size0);
   }

   iview->SP_PS_2D_SRC_INFO = A6XX_SP_PS_2D_SRC_INFO(
      .color_format = fmt.fmt,
      .tile_mode = fmt.tile_mode,
      .color_swap = fmt.swap,
      .flags = ubwc_enabled,
      .srgb = vk_format_is_srgb(format),
      .samples = tu_msaa_samples(layout->nr_samples),
      .samples_average = layout->nr_samples > 1 &&
                           !vk_format_is_int(format) &&
                           !vk_format_is_depth_or_stencil(format),
      .unk20 = 1,
      .unk22 = 1).value;
   iview->SP_PS_2D_SRC_SIZE =
      A6XX_SP_PS_2D_SRC_SIZE(.width = width, .height = height).value;

   /* note: these have same encoding for MRT and 2D (except 2D PITCH src) */
   iview->PITCH = A6XX_RB_DEPTH_BUFFER_PITCH(pitch).value;
   iview->FLAG_BUFFER_PITCH = A6XX_RB_DEPTH_FLAG_BUFFER_PITCH(
      .pitch = ubwc_pitch, .array_pitch = layout->ubwc_layer_size >> 2).value;

   iview->base_addr = base_addr;
   iview->ubwc_addr = ubwc_addr;
   iview->layer_size = layer_size;
   iview->ubwc_layer_size = layout->ubwc_layer_size;

   /* Don't set fields that are only used for attachments/blit dest if COLOR
    * is unsupported.
    */
   if (!(fmt.supported & FMT_COLOR))
      return;

   struct tu_native_format cfmt = tu6_format_color(format, layout->tile_mode);
   cfmt.tile_mode = fmt.tile_mode;

   if (is_d24s8 && ubwc_enabled)
      cfmt.fmt = FMT6_Z24_UNORM_S8_UINT_AS_R8G8B8A8;

   memset(iview->storage_descriptor, 0, sizeof(iview->storage_descriptor));

   iview->storage_descriptor[0] =
      A6XX_IBO_0_FMT(fmt.fmt) |
      A6XX_IBO_0_TILE_MODE(fmt.tile_mode);
   iview->storage_descriptor[1] =
      A6XX_IBO_1_WIDTH(width) |
      A6XX_IBO_1_HEIGHT(height);
   iview->storage_descriptor[2] =
      A6XX_IBO_2_PITCH(pitch) |
      A6XX_IBO_2_TYPE(tu6_tex_type(pCreateInfo->viewType, true));
   iview->storage_descriptor[3] = A6XX_IBO_3_ARRAY_PITCH(layer_size);

   iview->storage_descriptor[4] = base_addr;
   iview->storage_descriptor[5] = (base_addr >> 32) | A6XX_IBO_5_DEPTH(storage_depth);

   if (ubwc_enabled) {
      iview->storage_descriptor[3] |= A6XX_IBO_3_FLAG | A6XX_IBO_3_UNK27;
      iview->storage_descriptor[7] |= ubwc_addr;
      iview->storage_descriptor[8] |= ubwc_addr >> 32;
      iview->storage_descriptor[9] = A6XX_IBO_9_FLAG_BUFFER_ARRAY_PITCH(layout->ubwc_layer_size >> 2);
      iview->storage_descriptor[10] =
         A6XX_IBO_10_FLAG_BUFFER_PITCH(ubwc_pitch);
   }

   iview->extent.width = width;
   iview->extent.height = height;
   iview->need_y2_align =
      (fmt.tile_mode == TILE6_LINEAR && range->baseMipLevel != image->level_count - 1);

   iview->ubwc_enabled = ubwc_enabled;

   iview->RB_MRT_BUF_INFO = A6XX_RB_MRT_BUF_INFO(0,
                              .color_tile_mode = cfmt.tile_mode,
                              .color_format = cfmt.fmt,
                              .color_swap = cfmt.swap).value;

   iview->SP_FS_MRT_REG = A6XX_SP_FS_MRT_REG(0,
                              .color_format = cfmt.fmt,
                              .color_sint = vk_format_is_sint(format),
                              .color_uint = vk_format_is_uint(format)).value;

   iview->RB_2D_DST_INFO = A6XX_RB_2D_DST_INFO(
      .color_format = cfmt.fmt,
      .tile_mode = cfmt.tile_mode,
      .color_swap = cfmt.swap,
      .flags = ubwc_enabled,
      .srgb = vk_format_is_srgb(format)).value;

   iview->RB_BLIT_DST_INFO = A6XX_RB_BLIT_DST_INFO(
      .tile_mode = cfmt.tile_mode,
      .samples = tu_msaa_samples(layout->nr_samples),
      .color_format = cfmt.fmt,
      .color_swap = cfmt.swap,
      .flags = ubwc_enabled).value;

   if (image->vk_format == VK_FORMAT_D32_SFLOAT_S8_UINT) {
      layout = &image->layout[1];
      iview->stencil_base_addr = image->bo->iova + image->bo_offset +
         fdl_surface_offset(layout, range->baseMipLevel, range->baseArrayLayer);
      iview->stencil_layer_size = fdl_layer_stride(layout, range->baseMipLevel);
      iview->stencil_PITCH = A6XX_RB_STENCIL_BUFFER_PITCH(fdl_pitch(layout, range->baseMipLevel)).value;
   }
}

bool
ubwc_possible(VkFormat format, VkImageType type, VkImageUsageFlags usage, bool has_z24uint_s8uint,
              VkSampleCountFlagBits samples)
{
   /* no UBWC with compressed formats, E5B9G9R9, S8_UINT
    * (S8_UINT because separate stencil doesn't have UBWC-enable bit)
    */
   if (vk_format_is_compressed(format) ||
       format == VK_FORMAT_E5B9G9R9_UFLOAT_PACK32 ||
       format == VK_FORMAT_S8_UINT)
      return false;

   if (type == VK_IMAGE_TYPE_3D) {
      tu_finishme("UBWC with 3D textures");
      return false;
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
   if (usage & VK_IMAGE_USAGE_STORAGE_BIT)
      return false;

   /* Disable UBWC for D24S8 on A630 in some cases
    *
    * VK_IMAGE_ASPECT_STENCIL_BIT image view requires to be able to sample
    * from the stencil component as UINT, however no format allows this
    * on a630 (the special FMT6_Z24_UINT_S8_UINT format is missing)
    *
    * It must be sampled as FMT6_8_8_8_8_UINT, which is not UBWC-compatible
    *
    * Additionally, the special AS_R8G8B8A8 format is broken without UBWC,
    * so we have to fallback to 8_8_8_8_UNORM when UBWC is disabled
    */
   if (!has_z24uint_s8uint &&
       format == VK_FORMAT_D24_UNORM_S8_UINT &&
       (usage & (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT)))
      return false;

   if (!has_z24uint_s8uint && samples > VK_SAMPLE_COUNT_1_BIT)
      return false;

   return true;
}

VkResult
tu_CreateImage(VkDevice _device,
               const VkImageCreateInfo *pCreateInfo,
               const VkAllocationCallbacks *alloc,
               VkImage *pImage)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   uint64_t modifier = DRM_FORMAT_MOD_INVALID;
   const VkSubresourceLayout *plane_layouts = NULL;
   struct tu_image *image;

   if (pCreateInfo->tiling == VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT) {
      const VkImageDrmFormatModifierListCreateInfoEXT *mod_info =
         vk_find_struct_const(pCreateInfo->pNext,
                              IMAGE_DRM_FORMAT_MODIFIER_LIST_CREATE_INFO_EXT);
      const VkImageDrmFormatModifierExplicitCreateInfoEXT *drm_explicit_info =
         vk_find_struct_const(pCreateInfo->pNext,
                              IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT);

      assert(mod_info || drm_explicit_info);

      if (mod_info) {
         modifier = DRM_FORMAT_MOD_LINEAR;
         for (unsigned i = 0; i < mod_info->drmFormatModifierCount; i++) {
            if (mod_info->pDrmFormatModifiers[i] == DRM_FORMAT_MOD_QCOM_COMPRESSED)
               modifier = DRM_FORMAT_MOD_QCOM_COMPRESSED;
         }
      } else {
         modifier = drm_explicit_info->drmFormatModifier;
         assert(modifier == DRM_FORMAT_MOD_LINEAR ||
                modifier == DRM_FORMAT_MOD_QCOM_COMPRESSED);
         plane_layouts = drm_explicit_info->pPlaneLayouts;
      }
   } else {
      const struct wsi_image_create_info *wsi_info =
         vk_find_struct_const(pCreateInfo->pNext, WSI_IMAGE_CREATE_INFO_MESA);
      if (wsi_info && wsi_info->scanout)
         modifier = DRM_FORMAT_MOD_LINEAR;
   }

#ifdef ANDROID
   const VkNativeBufferANDROID *gralloc_info =
      vk_find_struct_const(pCreateInfo->pNext, NATIVE_BUFFER_ANDROID);
   int dma_buf;
   if (gralloc_info) {
      VkResult result = tu_gralloc_info(device, gralloc_info, &dma_buf, &modifier);
      if (result != VK_SUCCESS)
         return result;
   }
#endif

   image = vk_object_zalloc(&device->vk, alloc, sizeof(*image),
                            VK_OBJECT_TYPE_IMAGE);
   if (!image)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   const VkExternalMemoryImageCreateInfo *external_info =
      vk_find_struct_const(pCreateInfo->pNext, EXTERNAL_MEMORY_IMAGE_CREATE_INFO);
   image->shareable = external_info != NULL;

   image->vk_format = pCreateInfo->format;
   image->level_count = pCreateInfo->mipLevels;
   image->layer_count = pCreateInfo->arrayLayers;

   enum a6xx_tile_mode tile_mode = TILE6_3;
   bool ubwc_enabled =
      !(device->physical_device->instance->debug_flags & TU_DEBUG_NOUBWC);

   /* use linear tiling if requested */
   if (pCreateInfo->tiling == VK_IMAGE_TILING_LINEAR || modifier == DRM_FORMAT_MOD_LINEAR) {
      tile_mode = TILE6_LINEAR;
      ubwc_enabled = false;
   }

   /* Mutable images can be reinterpreted as any other compatible format.
    * This is a problem with UBWC (compression for different formats is different),
    * but also tiling ("swap" affects how tiled formats are stored in memory)
    * Depth and stencil formats cannot be reintepreted as another format, and
    * cannot be linear with sysmem rendering, so don't fall back for those.
    *
    * TODO:
    * - if the fmt_list contains only formats which are swapped, but compatible
    *   with each other (B8G8R8A8_UNORM and B8G8R8A8_UINT for example), then
    *   tiling is still possible
    * - figure out which UBWC compressions are compatible to keep it enabled
    */
   if ((pCreateInfo->flags & VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT) &&
       !vk_format_is_depth_or_stencil(image->vk_format)) {
      const VkImageFormatListCreateInfo *fmt_list =
         vk_find_struct_const(pCreateInfo->pNext, IMAGE_FORMAT_LIST_CREATE_INFO);
      bool may_be_swapped = true;
      if (fmt_list) {
         may_be_swapped = false;
         for (uint32_t i = 0; i < fmt_list->viewFormatCount; i++) {
            if (tu6_format_color(fmt_list->pViewFormats[i], TILE6_LINEAR).swap) {
               may_be_swapped = true;
               break;
            }
         }
      }
      if (may_be_swapped)
         tile_mode = TILE6_LINEAR;
      ubwc_enabled = false;
   }

   if (!ubwc_possible(image->vk_format, pCreateInfo->imageType, pCreateInfo->usage,
                      device->physical_device->info.a6xx.has_z24uint_s8uint, pCreateInfo->samples))
      ubwc_enabled = false;

   /* expect UBWC enabled if we asked for it */
   assert(modifier != DRM_FORMAT_MOD_QCOM_COMPRESSED || ubwc_enabled);

   for (uint32_t i = 0; i < tu6_plane_count(image->vk_format); i++) {
      struct fdl_layout *layout = &image->layout[i];
      VkFormat format = tu6_plane_format(image->vk_format, i);
      uint32_t width0 = pCreateInfo->extent.width;
      uint32_t height0 = pCreateInfo->extent.height;

      if (i > 0) {
         switch (image->vk_format) {
         case VK_FORMAT_G8_B8R8_2PLANE_420_UNORM:
         case VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM:
            /* half width/height on chroma planes */
            width0 = (width0 + 1) >> 1;
            height0 = (height0 + 1) >> 1;
            break;
         case VK_FORMAT_D32_SFLOAT_S8_UINT:
            /* no UBWC for separate stencil */
            ubwc_enabled = false;
            break;
         default:
            break;
         }
      }

      struct fdl_explicit_layout plane_layout;

      if (plane_layouts) {
         /* only expect simple 2D images for now */
         if (pCreateInfo->mipLevels != 1 ||
            pCreateInfo->arrayLayers != 1 ||
            pCreateInfo->extent.depth != 1)
            goto invalid_layout;

         plane_layout.offset = plane_layouts[i].offset;
         plane_layout.pitch = plane_layouts[i].rowPitch;
         /* note: use plane_layouts[0].arrayPitch to support array formats */
      }

      layout->tile_mode = tile_mode;
      layout->ubwc = ubwc_enabled;

      if (!fdl6_layout(layout, vk_format_to_pipe_format(format),
                       pCreateInfo->samples,
                       width0, height0,
                       pCreateInfo->extent.depth,
                       pCreateInfo->mipLevels,
                       pCreateInfo->arrayLayers,
                       pCreateInfo->imageType == VK_IMAGE_TYPE_3D,
                       plane_layouts ? &plane_layout : NULL)) {
         assert(plane_layouts); /* can only fail with explicit layout */
         goto invalid_layout;
      }

      /* fdl6_layout can't take explicit offset without explicit pitch
       * add offset manually for extra layouts for planes
       */
      if (!plane_layouts && i > 0) {
         uint32_t offset = ALIGN_POT(image->total_size, 4096);
         for (int i = 0; i < pCreateInfo->mipLevels; i++) {
            layout->slices[i].offset += offset;
            layout->ubwc_slices[i].offset += offset;
         }
         layout->size += offset;
      }

      image->total_size = MAX2(image->total_size, layout->size);
   }

   const struct util_format_description *desc = util_format_description(image->layout[0].format);
   if (util_format_has_depth(desc) && !(device->instance->debug_flags & TU_DEBUG_NOLRZ))
   {
      /* Depth plane is the first one */
      struct fdl_layout *layout = &image->layout[0];
      unsigned width = layout->width0;
      unsigned height = layout->height0;

      /* LRZ buffer is super-sampled */
      switch (layout->nr_samples) {
      case 4:
         width *= 2;
         /* fallthru */
      case 2:
         height *= 2;
         break;
      default:
         break;
      }

      unsigned lrz_pitch  = align(DIV_ROUND_UP(width, 8), 32);
      unsigned lrz_height = align(DIV_ROUND_UP(height, 8), 16);

      image->lrz_height = lrz_height;
      image->lrz_pitch = lrz_pitch;
      image->lrz_offset = image->total_size;
      unsigned lrz_size = lrz_pitch * lrz_height * 2;
      image->total_size += lrz_size;
   }

   *pImage = tu_image_to_handle(image);

#ifdef ANDROID
   if (gralloc_info)
      return tu_import_memory_from_gralloc_handle(_device, dma_buf, alloc, *pImage);
#endif
   return VK_SUCCESS;

invalid_layout:
   vk_object_free(&device->vk, alloc, image);
   return vk_error(device->instance, VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT);
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

#ifdef ANDROID
   if (image->owned_memory != VK_NULL_HANDLE)
      tu_FreeMemory(_device, image->owned_memory, pAllocator);
#endif

   vk_object_free(&device->vk, pAllocator, image);
}

void
tu_GetImageSubresourceLayout(VkDevice _device,
                             VkImage _image,
                             const VkImageSubresource *pSubresource,
                             VkSubresourceLayout *pLayout)
{
   TU_FROM_HANDLE(tu_image, image, _image);

   struct fdl_layout *layout =
      &image->layout[tu6_plane_index(image->vk_format, pSubresource->aspectMask)];
   const struct fdl_slice *slice = layout->slices + pSubresource->mipLevel;

   pLayout->offset =
      fdl_surface_offset(layout, pSubresource->mipLevel, pSubresource->arrayLayer);
   pLayout->rowPitch = fdl_pitch(layout, pSubresource->mipLevel);
   pLayout->arrayPitch = fdl_layer_stride(layout, pSubresource->mipLevel);
   pLayout->depthPitch = slice->size0;
   pLayout->size = pLayout->depthPitch * layout->depth0;

   if (fdl_ubwc_enabled(layout, pSubresource->mipLevel)) {
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

   /* TODO invent a modifier for tiled but not UBWC buffers */

   if (!image->layout[0].tile_mode)
      pProperties->drmFormatModifier = DRM_FORMAT_MOD_LINEAR;
   else if (image->layout[0].ubwc_layer_size)
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

   view = vk_object_alloc(&device->vk, pAllocator, sizeof(*view),
                          VK_OBJECT_TYPE_IMAGE_VIEW);
   if (view == NULL)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   tu_image_view_init(view, pCreateInfo, device->physical_device->info.a6xx.has_z24uint_s8uint);

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

   vk_object_free(&device->vk, pAllocator, iview);
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
   const struct tu_native_format fmt = tu6_format_texture(vfmt, TILE6_LINEAR);

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
      A6XX_TEX_CONST_0_SWAP(fmt.swap) |
      A6XX_TEX_CONST_0_FMT(fmt.fmt) |
      A6XX_TEX_CONST_0_MIPLVLS(0) |
      tu6_texswiz(&components, NULL, vfmt, VK_IMAGE_ASPECT_COLOR_BIT, false);
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

   view = vk_object_alloc(&device->vk, pAllocator, sizeof(*view),
                          VK_OBJECT_TYPE_BUFFER_VIEW);
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

   vk_object_free(&device->vk, pAllocator, view);
}
