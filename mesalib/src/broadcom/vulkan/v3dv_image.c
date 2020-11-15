/*
 * Copyright © 2019 Raspberry Pi
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

#include "v3dv_private.h"

#include "broadcom/cle/v3dx_pack.h"
#include "drm-uapi/drm_fourcc.h"
#include "util/format/u_format.h"
#include "util/u_math.h"
#include "vk_format_info.h"
#include "vk_util.h"
#include "vulkan/wsi/wsi_common.h"

/**
 * Computes the HW's UIFblock padding for a given height/cpp.
 *
 * The goal of the padding is to keep pages of the same color (bank number) at
 * least half a page away from each other vertically when crossing between
 * columns of UIF blocks.
 */
static uint32_t
v3d_get_ub_pad(uint32_t cpp, uint32_t height)
{
   uint32_t utile_h = v3d_utile_height(cpp);
   uint32_t uif_block_h = utile_h * 2;
   uint32_t height_ub = height / uif_block_h;

   uint32_t height_offset_in_pc = height_ub % PAGE_CACHE_UB_ROWS;

   /* For the perfectly-aligned-for-UIF-XOR case, don't add any pad. */
   if (height_offset_in_pc == 0)
      return 0;

   /* Try padding up to where we're offset by at least half a page. */
   if (height_offset_in_pc < PAGE_UB_ROWS_TIMES_1_5) {
      /* If we fit entirely in the page cache, don't pad. */
      if (height_ub < PAGE_CACHE_UB_ROWS)
         return 0;
      else
         return PAGE_UB_ROWS_TIMES_1_5 - height_offset_in_pc;
   }

   /* If we're close to being aligned to page cache size, then round up
    * and rely on XOR.
    */
   if (height_offset_in_pc > PAGE_CACHE_MINUS_1_5_UB_ROWS)
      return PAGE_CACHE_UB_ROWS - height_offset_in_pc;

   /* Otherwise, we're far enough away (top and bottom) to not need any
    * padding.
    */
   return 0;
}

static void
v3d_setup_slices(struct v3dv_image *image)
{
   assert(image->cpp > 0);

   uint32_t width = image->extent.width;
   uint32_t height = image->extent.height;
   uint32_t depth = image->extent.depth;

   /* Note that power-of-two padding is based on level 1.  These are not
    * equivalent to just util_next_power_of_two(dimension), because at a
    * level 0 dimension of 9, the level 1 power-of-two padded value is 4,
    * not 8.
    */
   uint32_t pot_width = 2 * util_next_power_of_two(u_minify(width, 1));
   uint32_t pot_height = 2 * util_next_power_of_two(u_minify(height, 1));
   uint32_t pot_depth = 2 * util_next_power_of_two(u_minify(depth, 1));

   uint32_t utile_w = v3d_utile_width(image->cpp);
   uint32_t utile_h = v3d_utile_height(image->cpp);
   uint32_t uif_block_w = utile_w * 2;
   uint32_t uif_block_h = utile_h * 2;

   uint32_t block_width = vk_format_get_blockwidth(image->vk_format);
   uint32_t block_height = vk_format_get_blockheight(image->vk_format);

   assert(image->samples == VK_SAMPLE_COUNT_1_BIT ||
          image->samples == VK_SAMPLE_COUNT_4_BIT);
   bool msaa = image->samples != VK_SAMPLE_COUNT_1_BIT;

   bool uif_top = msaa;

   assert(image->array_size > 0);
   assert(depth > 0);
   assert(image->levels >= 1);

   uint32_t offset = 0;
   for (int32_t i = image->levels - 1; i >= 0; i--) {
      struct v3d_resource_slice *slice = &image->slices[i];

      uint32_t level_width, level_height, level_depth;
      if (i < 2) {
         level_width = u_minify(width, i);
         level_height = u_minify(height, i);
      } else {
         level_width = u_minify(pot_width, i);
         level_height = u_minify(pot_height, i);
      }

      if (i < 1)
         level_depth = u_minify(depth, i);
      else
         level_depth = u_minify(pot_depth, i);

      if (msaa) {
         level_width *= 2;
         level_height *= 2;
      }

      level_width = DIV_ROUND_UP(level_width, block_width);
      level_height = DIV_ROUND_UP(level_height, block_height);

      if (!image->tiled) {
         slice->tiling = VC5_TILING_RASTER;
         if (image->type == VK_IMAGE_TYPE_1D)
            level_width = align(level_width, 64 / image->cpp);
      } else {
         if ((i != 0 || !uif_top) &&
             (level_width <= utile_w || level_height <= utile_h)) {
            slice->tiling = VC5_TILING_LINEARTILE;
            level_width = align(level_width, utile_w);
            level_height = align(level_height, utile_h);
         } else if ((i != 0 || !uif_top) && level_width <= uif_block_w) {
            slice->tiling = VC5_TILING_UBLINEAR_1_COLUMN;
            level_width = align(level_width, uif_block_w);
            level_height = align(level_height, uif_block_h);
         } else if ((i != 0 || !uif_top) && level_width <= 2 * uif_block_w) {
            slice->tiling = VC5_TILING_UBLINEAR_2_COLUMN;
            level_width = align(level_width, 2 * uif_block_w);
            level_height = align(level_height, uif_block_h);
         } else {
            /* We align the width to a 4-block column of UIF blocks, but we
             * only align height to UIF blocks.
             */
            level_width = align(level_width, 4 * uif_block_w);
            level_height = align(level_height, uif_block_h);

            slice->ub_pad = v3d_get_ub_pad(image->cpp, level_height);
            level_height += slice->ub_pad * uif_block_h;

            /* If the padding set us to to be aligned to the page cache size,
             * then the HW will use the XOR bit on odd columns to get us
             * perfectly misaligned.
             */
            if ((level_height / uif_block_h) %
                (VC5_PAGE_CACHE_SIZE / VC5_UIFBLOCK_ROW_SIZE) == 0) {
               slice->tiling = VC5_TILING_UIF_XOR;
            } else {
               slice->tiling = VC5_TILING_UIF_NO_XOR;
            }
         }
      }

      slice->offset = offset;
      slice->stride = level_width * image->cpp;
      slice->padded_height = level_height;
      if (slice->tiling == VC5_TILING_UIF_NO_XOR ||
          slice->tiling == VC5_TILING_UIF_XOR) {
         slice->padded_height_of_output_image_in_uif_blocks =
            slice->padded_height / (2 * v3d_utile_height(image->cpp));
      }

      slice->size = level_height * slice->stride;
      uint32_t slice_total_size = slice->size * level_depth;

      /* The HW aligns level 1's base to a page if any of level 1 or
       * below could be UIF XOR.  The lower levels then inherit the
       * alignment for as long as necesary, thanks to being power of
       * two aligned.
       */
      if (i == 1 &&
          level_width > 4 * uif_block_w &&
          level_height > PAGE_CACHE_MINUS_1_5_UB_ROWS * uif_block_h) {
         slice_total_size = align(slice_total_size, VC5_UIFCFG_PAGE_SIZE);
      }

      offset += slice_total_size;
   }

   image->size = offset;

   /* UIF/UBLINEAR levels need to be aligned to UIF-blocks, and LT only
    * needs to be aligned to utile boundaries.  Since tiles are laid out
    * from small to big in memory, we need to align the later UIF slices
    * to UIF blocks, if they were preceded by non-UIF-block-aligned LT
    * slices.
    *
    * We additionally align to 4k, which improves UIF XOR performance.
    */
   image->alignment =
      image->tiling == VK_IMAGE_TILING_LINEAR ? image->cpp : 4096;
   uint32_t align_offset =
      align(image->slices[0].offset, image->alignment) - image->slices[0].offset;
   if (align_offset) {
      image->size += align_offset;
      for (int i = 0; i < image->levels; i++)
         image->slices[i].offset += align_offset;
   }

   /* Arrays and cube textures have a stride which is the distance from
    * one full mipmap tree to the next (64b aligned).  For 3D textures,
    * we need to program the stride between slices of miplevel 0.
    */
   if (image->type != VK_IMAGE_TYPE_3D) {
      image->cube_map_stride =
         align(image->slices[0].offset + image->slices[0].size, 64);
      image->size += image->cube_map_stride * (image->array_size - 1);
   } else {
      image->cube_map_stride = image->slices[0].size;
   }
}

uint32_t
v3dv_layer_offset(const struct v3dv_image *image, uint32_t level, uint32_t layer)
{
   const struct v3d_resource_slice *slice = &image->slices[level];

   if (image->type == VK_IMAGE_TYPE_3D)
      return image->mem_offset + slice->offset + layer * slice->size;
   else
      return image->mem_offset + slice->offset + layer * image->cube_map_stride;
}

VkResult
v3dv_CreateImage(VkDevice _device,
                 const VkImageCreateInfo *pCreateInfo,
                 const VkAllocationCallbacks *pAllocator,
                 VkImage *pImage)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   struct v3dv_image *image = NULL;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);

   v3dv_assert(pCreateInfo->mipLevels > 0);
   v3dv_assert(pCreateInfo->arrayLayers > 0);
   v3dv_assert(pCreateInfo->samples > 0);
   v3dv_assert(pCreateInfo->extent.width > 0);
   v3dv_assert(pCreateInfo->extent.height > 0);
   v3dv_assert(pCreateInfo->extent.depth > 0);

   /* When using the simulator the WSI common code will see that our
    * driver wsi device doesn't match the display device and because of that
    * it will not attempt to present directly from the swapchain images,
    * instead it will use the prime blit path (use_prime_blit flag in
    * struct wsi_swapchain), where it copies the contents of the swapchain
    * images to a linear buffer with appropriate row stride for presentation.
    * As a result, on that path, swapchain images do not have any special
    * requirements and are not created with the pNext structs below.
    */
   uint64_t modifier = DRM_FORMAT_MOD_INVALID;
   if (pCreateInfo->tiling == VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT) {
      const VkImageDrmFormatModifierListCreateInfoEXT *mod_info =
         vk_find_struct_const(pCreateInfo->pNext,
                              IMAGE_DRM_FORMAT_MODIFIER_LIST_CREATE_INFO_EXT);
      assert(mod_info);
      for (uint32_t i = 0; i < mod_info->drmFormatModifierCount; i++) {
         switch (mod_info->pDrmFormatModifiers[i]) {
         case DRM_FORMAT_MOD_LINEAR:
            if (modifier == DRM_FORMAT_MOD_INVALID)
               modifier = DRM_FORMAT_MOD_LINEAR;
            break;
         case DRM_FORMAT_MOD_BROADCOM_UIF:
            modifier = DRM_FORMAT_MOD_BROADCOM_UIF;
            break;
         }
      }
   } else {
      const struct wsi_image_create_info *wsi_info =
         vk_find_struct_const(pCreateInfo->pNext, WSI_IMAGE_CREATE_INFO_MESA);
      if (wsi_info)
         modifier = DRM_FORMAT_MOD_LINEAR;
   }

   /* 1D and 1D_ARRAY textures are always raster-order */
   VkImageTiling tiling;
   if (pCreateInfo->imageType == VK_IMAGE_TYPE_1D)
      tiling = VK_IMAGE_TILING_LINEAR;
   else if (modifier == DRM_FORMAT_MOD_INVALID)
      tiling = pCreateInfo->tiling;
   else if (modifier == DRM_FORMAT_MOD_BROADCOM_UIF)
      tiling = VK_IMAGE_TILING_OPTIMAL;
   else
      tiling = VK_IMAGE_TILING_LINEAR;

   const struct v3dv_format *format = v3dv_get_format(pCreateInfo->format);
   v3dv_assert(format != NULL && format->supported);

   image = vk_zalloc2(&device->alloc, pAllocator, sizeof(*image), 8,
                      VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!image)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   assert(pCreateInfo->samples == VK_SAMPLE_COUNT_1_BIT ||
          pCreateInfo->samples == VK_SAMPLE_COUNT_4_BIT);

   image->type = pCreateInfo->imageType;
   image->extent = pCreateInfo->extent;
   image->vk_format = pCreateInfo->format;
   image->format = format;
   image->aspects = vk_format_aspects(image->vk_format);
   image->levels = pCreateInfo->mipLevels;
   image->array_size = pCreateInfo->arrayLayers;
   image->samples = pCreateInfo->samples;
   image->usage = pCreateInfo->usage;
   image->flags = pCreateInfo->flags;

   image->drm_format_mod = modifier;
   image->tiling = tiling;
   image->tiled = tiling == VK_IMAGE_TILING_OPTIMAL;

   image->cpp = vk_format_get_blocksize(image->vk_format);

   v3d_setup_slices(image);

   *pImage = v3dv_image_to_handle(image);

   return VK_SUCCESS;
}

void
v3dv_GetImageSubresourceLayout(VkDevice device,
                               VkImage _image,
                               const VkImageSubresource *subresource,
                               VkSubresourceLayout *layout)
{
   V3DV_FROM_HANDLE(v3dv_image, image, _image);

   const struct v3d_resource_slice *slice =
      &image->slices[subresource->mipLevel];
   layout->offset =
      v3dv_layer_offset(image, subresource->mipLevel, subresource->arrayLayer);
   layout->rowPitch = slice->stride;
   layout->depthPitch = image->cube_map_stride;
   layout->arrayPitch = image->cube_map_stride;

   if (image->type != VK_IMAGE_TYPE_3D) {
      layout->size = slice->size;
   } else {
      /* For 3D images, the size of the slice represents the size of a 2D slice
       * in the 3D image, so we have to multiply by the depth extent of the
       * miplevel. For levels other than the first, we just compute the size
       * as the distance between consecutive levels (notice that mip levels are
       * arranged in memory from last to first).
       */
      if (subresource->mipLevel == 0) {
         layout->size = slice->size * image->extent.depth;
      } else {
            const struct v3d_resource_slice *prev_slice =
               &image->slices[subresource->mipLevel - 1];
            layout->size = prev_slice->offset - slice->offset;
      }
   }
}

VkResult
v3dv_GetImageDrmFormatModifierPropertiesEXT(
   VkDevice device,
   VkImage _image,
   VkImageDrmFormatModifierPropertiesEXT *pProperties)
{
   V3DV_FROM_HANDLE(v3dv_image, image, _image);

   assert(pProperties->sType ==
          VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_PROPERTIES_EXT);

   pProperties->drmFormatModifier = image->drm_format_mod;

   return VK_SUCCESS;
}

void
v3dv_DestroyImage(VkDevice _device,
                  VkImage _image,
                  const VkAllocationCallbacks* pAllocator)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   V3DV_FROM_HANDLE(v3dv_image, image, _image);
   vk_free2(&device->alloc, pAllocator, image);
}

VkImageViewType
v3dv_image_type_to_view_type(VkImageType type)
{
   switch (type) {
   case VK_IMAGE_TYPE_1D: return VK_IMAGE_VIEW_TYPE_1D;
   case VK_IMAGE_TYPE_2D: return VK_IMAGE_VIEW_TYPE_2D;
   case VK_IMAGE_TYPE_3D: return VK_IMAGE_VIEW_TYPE_3D;
   default:
      unreachable("Invalid image type");
   }
}

/*
 * This method translates pipe_swizzle to the swizzle values used at the
 * packet TEXTURE_SHADER_STATE
 *
 * FIXME: C&P from v3d, common place?
 */
static uint32_t
translate_swizzle(unsigned char pipe_swizzle)
{
   switch (pipe_swizzle) {
   case PIPE_SWIZZLE_0:
      return 0;
   case PIPE_SWIZZLE_1:
      return 1;
   case PIPE_SWIZZLE_X:
   case PIPE_SWIZZLE_Y:
   case PIPE_SWIZZLE_Z:
   case PIPE_SWIZZLE_W:
      return 2 + pipe_swizzle;
   default:
      unreachable("unknown swizzle");
   }
}

/*
 * Packs and ensure bo for the shader state (the latter can be temporal).
 */
static void
pack_texture_shader_state_helper(struct v3dv_device *device,
                                 struct v3dv_image_view *image_view,
                                 bool for_cube_map_array_storage)
{
   assert(!for_cube_map_array_storage ||
          image_view->type == VK_IMAGE_VIEW_TYPE_CUBE_ARRAY);
   const uint32_t index = for_cube_map_array_storage ? 1 : 0;

   assert(image_view->image);
   const struct v3dv_image *image = image_view->image;

   assert(image->samples == VK_SAMPLE_COUNT_1_BIT ||
          image->samples == VK_SAMPLE_COUNT_4_BIT);
   const uint32_t msaa_scale = image->samples == VK_SAMPLE_COUNT_1_BIT ? 1 : 2;

   v3dv_pack(image_view->texture_shader_state[index], TEXTURE_SHADER_STATE, tex) {

      tex.level_0_is_strictly_uif =
         (image->slices[0].tiling == VC5_TILING_UIF_XOR ||
          image->slices[0].tiling == VC5_TILING_UIF_NO_XOR);

      tex.level_0_xor_enable = (image->slices[0].tiling == VC5_TILING_UIF_XOR);

      if (tex.level_0_is_strictly_uif)
         tex.level_0_ub_pad = image->slices[0].ub_pad;

      /* FIXME: v3d never sets uif_xor_disable, but uses it on the following
       * check so let's set the default value
       */
      tex.uif_xor_disable = false;
      if (tex.uif_xor_disable ||
          tex.level_0_is_strictly_uif) {
         tex.extended = true;
      }

      tex.base_level = image_view->base_level;
      tex.max_level = image_view->max_level;

      tex.swizzle_r = translate_swizzle(image_view->swizzle[0]);
      tex.swizzle_g = translate_swizzle(image_view->swizzle[1]);
      tex.swizzle_b = translate_swizzle(image_view->swizzle[2]);
      tex.swizzle_a = translate_swizzle(image_view->swizzle[3]);

      tex.texture_type = image_view->format->tex_type;

      if (image->type == VK_IMAGE_TYPE_3D) {
         tex.image_depth = image->extent.depth;
      } else {
         tex.image_depth = (image_view->last_layer - image_view->first_layer) + 1;
      }

      /* Empirical testing with CTS shows that when we are sampling from cube
       * arrays we want to set image depth to layers / 6, but not when doing
       * image load/store.
       */
      if (image_view->type == VK_IMAGE_VIEW_TYPE_CUBE_ARRAY &&
          !for_cube_map_array_storage) {
         assert(tex.image_depth % 6 == 0);
         tex.image_depth /= 6;
      }

      tex.image_height = image->extent.height * msaa_scale;
      tex.image_width = image->extent.width * msaa_scale;

      /* On 4.x, the height of a 1D texture is redefined to be the
       * upper 14 bits of the width (which is only usable with txf).
       */
      if (image->type == VK_IMAGE_TYPE_1D) {
         tex.image_height = tex.image_width >> 14;
      }
      tex.image_width &= (1 << 14) - 1;
      tex.image_height &= (1 << 14) - 1;

      tex.array_stride_64_byte_aligned = image->cube_map_stride / 64;

      tex.srgb = vk_format_is_srgb(image_view->vk_format);

      /* At this point we don't have the job. That's the reason the first
       * parameter is NULL, to avoid a crash when cl_pack_emit_reloc tries to
       * add the bo to the job. This also means that we need to add manually
       * the image bo to the job using the texture.
       */
      const uint32_t base_offset =
         image->mem->bo->offset +
         v3dv_layer_offset(image, 0, image_view->first_layer);
      tex.texture_base_pointer = v3dv_cl_address(NULL, base_offset);
   }
}

static void
pack_texture_shader_state(struct v3dv_device *device,
                          struct v3dv_image_view *iview)
{
   pack_texture_shader_state_helper(device, iview, false);
   if (iview->type == VK_IMAGE_VIEW_TYPE_CUBE_ARRAY)
      pack_texture_shader_state_helper(device, iview, true);
}

static enum pipe_swizzle
vk_component_mapping_to_pipe_swizzle(VkComponentSwizzle comp,
                                     VkComponentSwizzle swz)
{
   if (swz == VK_COMPONENT_SWIZZLE_IDENTITY)
      swz = comp;

   switch (swz) {
   case VK_COMPONENT_SWIZZLE_ZERO:
      return PIPE_SWIZZLE_0;
   case VK_COMPONENT_SWIZZLE_ONE:
      return PIPE_SWIZZLE_1;
   case VK_COMPONENT_SWIZZLE_R:
      return PIPE_SWIZZLE_X;
   case VK_COMPONENT_SWIZZLE_G:
      return PIPE_SWIZZLE_Y;
   case VK_COMPONENT_SWIZZLE_B:
      return PIPE_SWIZZLE_Z;
   case VK_COMPONENT_SWIZZLE_A:
      return PIPE_SWIZZLE_W;
   default:
      unreachable("Unknown VkComponentSwizzle");
   };
}

VkResult
v3dv_CreateImageView(VkDevice _device,
                     const VkImageViewCreateInfo *pCreateInfo,
                     const VkAllocationCallbacks *pAllocator,
                     VkImageView *pView)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   V3DV_FROM_HANDLE(v3dv_image, image, pCreateInfo->image);
   struct v3dv_image_view *iview;

   iview = vk_zalloc2(&device->alloc, pAllocator, sizeof(*iview), 8,
                      VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (iview == NULL)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   const VkImageSubresourceRange *range = &pCreateInfo->subresourceRange;

   assert(range->layerCount > 0);
   assert(range->baseMipLevel < image->levels);

#ifdef DEBUG
   switch (image->type) {
   case VK_IMAGE_TYPE_1D:
   case VK_IMAGE_TYPE_2D:
      assert(range->baseArrayLayer + v3dv_layer_count(image, range) - 1 <=
             image->array_size);
      break;
   case VK_IMAGE_TYPE_3D:
      assert(range->baseArrayLayer + v3dv_layer_count(image, range) - 1
             <= u_minify(image->extent.depth, range->baseMipLevel));
      /* VK_KHR_maintenance1 */
      assert(pCreateInfo->viewType != VK_IMAGE_VIEW_TYPE_2D ||
             ((image->flags & VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT) &&
              range->levelCount == 1 && range->layerCount == 1));
      assert(pCreateInfo->viewType != VK_IMAGE_VIEW_TYPE_2D_ARRAY ||
             ((image->flags & VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT) &&
              range->levelCount == 1));
      break;
   default:
      unreachable("bad VkImageType");
   }
#endif

   iview->image = image;
   iview->aspects = range->aspectMask;
   iview->type = pCreateInfo->viewType;

   iview->base_level = range->baseMipLevel;
   iview->max_level = iview->base_level + v3dv_level_count(image, range) - 1;
   iview->extent = (VkExtent3D) {
      .width  = u_minify(image->extent.width , iview->base_level),
      .height = u_minify(image->extent.height, iview->base_level),
      .depth  = u_minify(image->extent.depth , iview->base_level),
   };

   iview->first_layer = range->baseArrayLayer;
   iview->last_layer = range->baseArrayLayer +
                       v3dv_layer_count(image, range) - 1;
   iview->offset =
      v3dv_layer_offset(image, iview->base_level, iview->first_layer);

   /* If we have D24S8 format but the view only selects the stencil aspect
    * we want to re-interpret the format as RGBA8_UINT, then map our stencil
    * data reads to the R component and ignore the GBA channels that contain
    * the depth aspect data.
    */
   VkFormat format;
   uint8_t image_view_swizzle[4];
   if (pCreateInfo->format == VK_FORMAT_D24_UNORM_S8_UINT &&
       range->aspectMask == VK_IMAGE_ASPECT_STENCIL_BIT) {
      format = VK_FORMAT_R8G8B8A8_UINT;
      image_view_swizzle[0] = PIPE_SWIZZLE_X;
      image_view_swizzle[1] = PIPE_SWIZZLE_0;
      image_view_swizzle[2] = PIPE_SWIZZLE_0;
      image_view_swizzle[3] = PIPE_SWIZZLE_1;
   } else {
      format = pCreateInfo->format;

      /* FIXME: we are doing this vk to pipe swizzle mapping just to call
       * util_format_compose_swizzles. Would be good to check if it would be
       * better to reimplement the latter using vk component
       */
      image_view_swizzle[0] =
         vk_component_mapping_to_pipe_swizzle(VK_COMPONENT_SWIZZLE_R,
                                              pCreateInfo->components.r);
      image_view_swizzle[1] =
         vk_component_mapping_to_pipe_swizzle(VK_COMPONENT_SWIZZLE_G,
                                              pCreateInfo->components.g);
      image_view_swizzle[2] =
         vk_component_mapping_to_pipe_swizzle(VK_COMPONENT_SWIZZLE_B,
                                              pCreateInfo->components.b);
      image_view_swizzle[3] =
         vk_component_mapping_to_pipe_swizzle(VK_COMPONENT_SWIZZLE_A,
                                              pCreateInfo->components.a);
   }

   iview->vk_format = format;
   iview->format = v3dv_get_format(format);
   assert(iview->format && iview->format->supported);

   if (vk_format_is_depth_or_stencil(iview->vk_format)) {
      iview->internal_type = v3dv_get_internal_depth_type(iview->vk_format);
   } else {
      v3dv_get_internal_type_bpp_for_output_format(iview->format->rt_type,
                                                   &iview->internal_type,
                                                   &iview->internal_bpp);
   }

   const uint8_t *format_swizzle = v3dv_get_format_swizzle(format);
   util_format_compose_swizzles(format_swizzle, image_view_swizzle,
                                iview->swizzle);
   iview->swap_rb = iview->swizzle[0] == PIPE_SWIZZLE_Z;

   pack_texture_shader_state(device, iview);

   *pView = v3dv_image_view_to_handle(iview);

   return VK_SUCCESS;
}

void
v3dv_DestroyImageView(VkDevice _device,
                      VkImageView imageView,
                      const VkAllocationCallbacks* pAllocator)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   V3DV_FROM_HANDLE(v3dv_image_view, image_view, imageView);

   vk_free2(&device->alloc, pAllocator, image_view);
}

static void
pack_texture_shader_state_from_buffer_view(struct v3dv_device *device,
                                           struct v3dv_buffer_view *buffer_view)
{
   assert(buffer_view->buffer);
   const struct v3dv_buffer *buffer = buffer_view->buffer;

   v3dv_pack(buffer_view->texture_shader_state, TEXTURE_SHADER_STATE, tex) {
      tex.swizzle_r = translate_swizzle(PIPE_SWIZZLE_X);
      tex.swizzle_g = translate_swizzle(PIPE_SWIZZLE_Y);
      tex.swizzle_b = translate_swizzle(PIPE_SWIZZLE_Z);
      tex.swizzle_a = translate_swizzle(PIPE_SWIZZLE_W);

      tex.image_depth = 1;

      /* On 4.x, the height of a 1D texture is redefined to be the upper 14
       * bits of the width (which is only usable with txf) (or in other words,
       * we are providing a 28 bit field for size, but split on the usual
       * 14bit height/width).
       */
      tex.image_width = buffer_view->size;
      tex.image_height = tex.image_width >> 14;
      tex.image_width &= (1 << 14) - 1;
      tex.image_height &= (1 << 14) - 1;

      tex.texture_type = buffer_view->format->tex_type;
      tex.srgb = vk_format_is_srgb(buffer_view->vk_format);

      /* At this point we don't have the job. That's the reason the first
       * parameter is NULL, to avoid a crash when cl_pack_emit_reloc tries to
       * add the bo to the job. This also means that we need to add manually
       * the image bo to the job using the texture.
       */
      const uint32_t base_offset =
         buffer->mem->bo->offset +
         buffer->mem_offset +
         buffer_view->offset;

      tex.texture_base_pointer = v3dv_cl_address(NULL, base_offset);
   }
}

VkResult
v3dv_CreateBufferView(VkDevice _device,
                      const VkBufferViewCreateInfo *pCreateInfo,
                      const VkAllocationCallbacks *pAllocator,
                      VkBufferView *pView)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);

   const struct v3dv_buffer *buffer =
      v3dv_buffer_from_handle(pCreateInfo->buffer);

   struct v3dv_buffer_view *view =
      vk_alloc2(&device->alloc, pAllocator, sizeof(*view), 8,
                VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!view)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   uint32_t range;
   if (pCreateInfo->range == VK_WHOLE_SIZE)
      range = buffer->size - pCreateInfo->offset;
   else
      range = pCreateInfo->range;

   enum pipe_format pipe_format = vk_format_to_pipe_format(pCreateInfo->format);
   uint32_t num_elements = range / util_format_get_blocksize(pipe_format);

   view->buffer = buffer;
   view->offset = pCreateInfo->offset;
   view->size = view->offset + range;
   view->num_elements = num_elements;
   view->vk_format = pCreateInfo->format;
   view->format = v3dv_get_format(view->vk_format);

   v3dv_get_internal_type_bpp_for_output_format(view->format->rt_type,
                                                &view->internal_type,
                                                &view->internal_bpp);

   if (buffer->usage & VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT ||
       buffer->usage & VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT)
      pack_texture_shader_state_from_buffer_view(device, view);

   *pView = v3dv_buffer_view_to_handle(view);

   return VK_SUCCESS;
}

void
v3dv_DestroyBufferView(VkDevice _device,
                       VkBufferView bufferView,
                       const VkAllocationCallbacks *pAllocator)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   V3DV_FROM_HANDLE(v3dv_buffer_view, buffer_view, bufferView);

   vk_free2(&device->alloc, pAllocator, buffer_view);
}
